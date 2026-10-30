/* Accounts-style settings table: search, column chooser, groups, footer navigator.
   The caller owns the rows and the editor on the right. */
import { clampWidth } from "../common/column-widths.js";
import { createTable, INDICATOR_WIDTH } from "../common/table.js";
import { iconClass } from "../common/icons/registry.js";
import { openColumnChooser } from "../common/column-chooser.js";
import { moduleFooter, moduleToolbar, mountBar, navTarget, paintFooterNav, tableNavAction } from "../common/module-bar.js";

export function cleanHidden(hidden, chooser) {
  const allowed = new Set((chooser || []).filter((column) => !column.locked).map((column) => column.field));
  if (!Array.isArray(hidden)) {
    return [];
  }
  return hidden.filter((field) => allowed.has(field));
}

/* Keep known fields, unique, then append any missing field in shipped order. */
export function cleanOrder(order, fields) {
  const allowed = (fields || []).filter((field) => typeof field === "string" && field !== "");
  const allow = new Set(allowed);
  const out = [];
  const seen = new Set();
  if (Array.isArray(order)) {
    for (const field of order) {
      if (allow.has(field) && !seen.has(field)) {
        out.push(field);
        seen.add(field);
      }
    }
  }
  for (const field of allowed) {
    if (!seen.has(field)) {
      out.push(field);
    }
  }
  return out;
}

export function orderColumns(columns, order) {
  const rank = new Map((order || []).map((field, index) => [field, index]));
  return [...(columns || [])].sort((a, b) => {
    const ai = rank.has(a.field) ? rank.get(a.field) : 1000;
    const bi = rank.has(b.field) ? rank.get(b.field) : 1000;
    return ai - bi;
  });
}

export function cleanWidths(widths, fields) {
  const out = {};
  if (!widths || typeof widths !== "object") {
    return out;
  }
  for (const field of fields || []) {
    const value = clampWidth(widths[field]);
    if (value !== null) {
      out[field] = value;
    }
  }
  return out;
}

/* Group row: chevron, label, count. Built with textContent. */
export function setGroupHeader(value, count, documentRef = globalThis.document) {
  const toggle = documentRef.createElement("span");
  toggle.className = "pav-group-toggle";
  toggle.setAttribute("aria-hidden", "true");
  const chevron = documentRef.createElement("i");
  chevron.className = `icon ${iconClass("tree.group.chevron")}`;
  chevron.setAttribute("aria-hidden", "true");
  toggle.append(chevron);
  const span = documentRef.createElement("span");
  span.className = "pav-group-label";
  span.textContent = value;
  const num = documentRef.createElement("span");
  num.className = "pav-group-count";
  num.textContent = String(count);
  const wrap = documentRef.createElement("span");
  wrap.className = "pav-group";
  wrap.append(toggle, span, num);
  return wrap;
}

/* Build the split chrome when the template has not (tests). */
export function ensureSplit(root, ids) {
  if (!root || root.querySelector(`#${ids.tree}`)) {
    return;
  }
  const doc = root.ownerDocument;
  const toolbar = doc.createElement("div");
  toolbar.id = ids.toolbar;
  toolbar.className = "button-bar pav-module-toolbar";
  toolbar.setAttribute("role", "toolbar");
  const body = doc.createElement("div");
  body.className = "settings-body";
  const split = doc.createElement("div");
  split.className = "settings-split";
  const pane = doc.createElement("div");
  pane.id = ids.pane;
  pane.className = "settings-tree-pane";
  const tree = doc.createElement("div");
  tree.id = ids.tree;
  tree.className = "settings-tree";
  pane.append(tree);
  const handle = doc.createElement("button");
  handle.type = "button";
  handle.className = "split-resize";
  handle.dataset.split = ids.split;
  handle.setAttribute("aria-label", ids.splitLabel || "Resize panes");
  const form = doc.createElement("div");
  form.id = ids.form;
  form.className = "settings-form-pane";
  split.append(pane, handle, form);
  body.append(split);
  const footer = doc.createElement("div");
  footer.id = ids.footer;
  footer.className = "button-bar pav-module-footer";
  footer.setAttribute("role", "toolbar");
  root.append(toolbar, body, footer);
}

function columnWidth(column) {
  if (Number.isFinite(column.width)) {
    return column.width;
  }
  if (Number.isFinite(column.minWidth)) {
    return column.minWidth;
  }
  return 80;
}

/* spec.ids, tableId, title, noun, searchId, searchLabel, empty, chooser,
   chooserLabel, widthFields, growFields, columns(doc, hidden, widths), groupBy,
   groupOrder(data), getRows(), match(row, query), getSelected(), onSelect(id),
   actions, footerEnd, onRefresh, onPaintNav, tableHidden, saveTableHidden,
   tableWidths, saveTableWidths, tableOrder, saveTableOrder. */
export function mountModuleTable(root, spec) {
  const doc = root.ownerDocument;
  const ids = spec.ids;
  let table = null;
  let query = "";
  let toolbarEl = null;
  let footerEl = null;
  let restoringOrder = false;
  let tableScale = { width: 1, font: 1 };
  const rowId = spec.rowId || "id";

  function hiddenColumns() {
    const raw = typeof spec.tableHidden === "function" ? spec.tableHidden(spec.tableId) : [];
    return cleanHidden(raw, spec.chooser);
  }

  /* Two or more grow columns remember how they share the spare width.
     One grow column stays the slack; the fixed column beside it is saved. */
  function growFields() {
    const fixed = new Set(spec.widthFields || []);
    const listed = Array.isArray(spec.growFields) ? spec.growFields.filter((field) => !fixed.has(field)) : [];
    return listed.length > 1 ? listed : [];
  }

  function widthKeys() {
    return [...(spec.widthFields || []), ...growFields()];
  }

  function savedWidths() {
    const raw = typeof spec.tableWidths === "function" ? spec.tableWidths(spec.tableId) : {};
    return cleanWidths(raw, widthKeys());
  }

  function fieldOrder() {
    return (spec.chooser || []).map((column) => column.field);
  }

  function savedOrder() {
    const raw = typeof spec.tableOrder === "function" ? spec.tableOrder(spec.tableId) : [];
    return cleanOrder(raw, fieldOrder());
  }

  function currentColumns() {
    const columns = spec.columns(doc, hiddenColumns(), savedWidths());
    return orderColumns(columns, savedOrder()).map((column) => ({ ...column, resizable: true }));
  }

  /* Same floor as Accounts: live widths for the fixed columns, minWidth for a
     stretching column that is not a saved width, plus the scrollbar. */
  function sizePane(columns) {
    const pane = root.querySelector(`#${ids.pane}`);
    if (!pane) {
      return;
    }
    const live = table && typeof table.widths === "function" ? table.widths() : {};
    const fields = spec.widthFields || [];
    let sum = INDICATOR_WIDTH;
    for (const column of columns) {
      if (column.visible === false) {
        continue;
      }
      const grows = column.widthGrow && !fields.includes(column.field);
      if (grows) {
        sum += Number.isFinite(column.minWidth) ? column.minWidth : columnWidth(column);
      } else if (Number.isFinite(live[column.field])) {
        sum += live[column.field];
      } else {
        sum += columnWidth(column);
      }
    }
    const bar = table && typeof table.scrollbarWidth === "function" ? table.scrollbarWidth() : 0;
    pane.style.minWidth = `${Math.ceil(sum + bar) + 2}px`;
  }

  function paintNav() {
    const shown = table ? table.ids() : [];
    if (footerEl) {
      paintFooterNav(footerEl, shown, spec.getSelected());
    }
    if (typeof spec.onPaintNav === "function") {
      spec.onPaintNav({ toolbar: toolbarEl, footer: footerEl, ids: shown });
    }
  }

  function onColumnResized(field, width) {
    const fixed = spec.widthFields || [];
    const grown = growFields();
    if (typeof spec.saveTableWidths === "function") {
      const next = { ...savedWidths() };
      let write = false;
      if (fixed.includes(field)) {
        const kept = clampWidth(width);
        if (kept !== null) {
          next[field] = kept;
          write = true;
        }
      }
      const live = table && typeof table.widths === "function" ? table.widths() : null;
      let shared = false;
      if (live && grown.includes(field)) {
        const share = {};
        shared = true;
        for (const name of grown) {
          const kept = clampWidth(live[name]);
          if (kept === null) {
            shared = false;
            break;
          }
          share[name] = kept;
        }
        if (shared) {
          Object.assign(next, share);
          write = true;
        }
      }
      if (live && !fixed.includes(field) && !shared) {
        /* Dragging the only grow column moves the fixed column beside it.
           That column does not get its own columnResized. A hidden partner
           leaves the same situation: save the fixed widths that moved. */
        for (const name of fixed) {
          const kept = clampWidth(live[name]);
          if (kept !== null && next[name] !== kept) {
            next[name] = kept;
            write = true;
          }
        }
      }
      if (write) {
        spec.saveTableWidths(spec.tableId, cleanWidths(next, widthKeys()));
      }
    }
    sizePane(currentColumns());
  }

  function onColumnMoved(order) {
    if (restoringOrder || typeof spec.saveTableOrder !== "function") {
      return;
    }
    spec.saveTableOrder(spec.tableId, cleanOrder(order, fieldOrder()));
  }

  function chooseColumns(anchor) {
    openColumnChooser(anchor, {
      label: spec.chooserLabel || "Columns",
      placement: "below-right",
      columns: spec.chooser,
      hidden: hiddenColumns(),
      onChange(hidden, field) {
        const next = cleanHidden(hidden, spec.chooser);
        if (field === null && typeof spec.saveTableWidths === "function") {
          spec.saveTableWidths(spec.tableId, {});
          if (table) {
            const fresh = spec.columns(doc, [], {});
            const weights = {};
            for (const column of fresh) {
              if ((spec.widthFields || []).includes(column.field) && Number.isFinite(column.width)) {
                table.setColumnWidth(column.field, column.width);
              }
              if (growFields().includes(column.field) && Number.isFinite(column.widthGrow)) {
                weights[column.field] = column.widthGrow;
              }
            }
            if (typeof table.setGrowWeights === "function") {
              table.setGrowWeights(weights);
            }
          }
        }
        if (field === null && typeof spec.saveTableOrder === "function") {
          if (table && typeof table.setColumnOrder === "function") {
            restoringOrder = true;
            try {
              table.setColumnOrder(spec.columns(doc, [], {}).map((column) => column.field));
            } finally {
              restoringOrder = false;
            }
          }
          spec.saveTableOrder(spec.tableId, []);
        }
        if (typeof spec.saveTableHidden === "function") {
          spec.saveTableHidden(spec.tableId, next);
        }
        if (table) {
          for (const column of spec.chooser) {
            if (!column.locked) {
              table.setColumnVisible(column.field, !next.includes(column.field));
            }
          }
        }
        sizePane(spec.columns(doc, next, savedWidths()));
      },
    });
  }

  function applySearch(value) {
    query = String(value || "");
    if (table) {
      const needle = query.trim();
      table.setFilter(needle === "" ? null : (row) => spec.match(row, needle));
    }
    paintNav();
  }

  function savedScroll() {
    if (typeof spec.tableScroll !== "function") {
      return null;
    }
    return spec.tableScroll(spec.tableId);
  }

  function pinnedScroll(pos) {
    return Boolean(pos && (pos.top > 0 || pos.left > 0));
  }

  function navigate(action, page = 1) {
    if (!table) {
      return;
    }
    const id = navTarget(table.ids(), spec.getSelected(), action, page);
    if (id !== null && id !== spec.getSelected()) {
      spec.onSelect(id);
    }
  }

  function onTreeKey(event) {
    const target = event.target;
    if (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target instanceof HTMLSelectElement || target instanceof HTMLButtonElement) {
      return;
    }
    const action = tableNavAction(event);
    if (action === "") {
      return;
    }
    event.preventDefault();
    const page = (action === "pagePrev" || action === "pageNext") && typeof table.pageCount === "function"
      ? table.pageCount()
      : 1;
    navigate(action, page);
  }

  function paint() {
    const treeEl = root.querySelector(`#${ids.tree}`);
    if (!treeEl) {
      return;
    }
    const rows = spec.getRows() || [];
    let created = false;
    let openingScroll = null;
    if (rows.length === 0) {
      if (table) {
        table.destroy();
        table = null;
      }
      treeEl.replaceChildren();
      const empty = doc.createElement("p");
      empty.className = "settings-empty";
      empty.textContent = spec.empty || "";
      treeEl.append(empty);
      paintNav();
      return;
    }
    if (!table) {
      created = true;
      openingScroll = savedScroll();
      treeEl.replaceChildren();
      const columns = currentColumns();
      sizePane(columns);
      table = createTable(treeEl, {
        indicator: true,
        onIndicatorHeader: (anchor) => chooseColumns(anchor),
        columns,
        data: rows,
        rowId,
        onRowClick: (row) => spec.onSelect(row[rowId]),
        groupBy: spec.groupBy,
        groupHeader: (value, count) => (spec.groupHeader || setGroupHeader)(value, count, doc),
        groupOrder: spec.groupOrder,
        select: true,
        filter: true,
        onRender: () => {
          paintNav();
          sizePane(currentColumns());
        },
        fitTitles: (spec.widthFields || []).filter((field) => savedWidths()[field] === undefined),
        movable: true,
        onColumnResized,
        onColumnMoved,
        onWidths: () => sizePane(currentColumns()),
        scroll: openingScroll,
        onScroll(pos) {
          if (typeof spec.saveTableScroll === "function") {
            spec.saveTableScroll(spec.tableId, pos);
          }
        },
      });
      if (typeof table.setScale === "function") {
        table.setScale(tableScale.width, tableScale.font);
      }
      if (query.trim() !== "") {
        table.setFilter((row) => spec.match(row, query.trim()));
      }
      if (!treeEl.dataset.keys) {
        treeEl.dataset.keys = "1";
        treeEl.addEventListener("keydown", onTreeKey);
      }
    } else {
      table.setData(rows);
    }
    table.select(spec.getSelected(), { reveal: created ? !pinnedScroll(openingScroll) : false });
    paintNav();
  }

  function reselect(reveal = true) {
    if (table) {
      table.select(spec.getSelected(), { reveal: reveal !== false });
    }
    paintNav();
  }

  toolbarEl = mountBar(root.querySelector(`#${ids.toolbar}`), moduleToolbar({
    title: spec.title,
    actions: spec.actions || [],
    search: {
      id: spec.searchId,
      label: spec.searchLabel,
      placeholder: spec.searchPlaceholder || spec.searchLabel,
      onInput: applySearch,
    },
  }, doc));
  footerEl = mountBar(root.querySelector(`#${ids.footer}`), moduleFooter({
    label: spec.footerLabel || `${spec.title} actions`,
    noun: spec.noun,
    onRefresh: () => {
      if (typeof spec.onRefresh === "function") {
        spec.onRefresh();
      } else {
        paint();
      }
    },
    onNav: navigate,
    end: spec.footerEnd || [],
  }, doc));
  paint();
  return {
    paint,
    reselect,
    navigate,
    setZoom(widthScale, fontScale) {
      tableScale = { width: widthScale, font: fontScale };
      if (table && typeof table.setScale === "function") {
        table.setScale(widthScale, fontScale);
      }
    },
  };
}
