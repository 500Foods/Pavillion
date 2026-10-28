/* The shared table (N Lock, Shared table component; TABULATOR.md): the only
   module that imports Tabulator, loaded on first use as its own chunk, core
   plus the modules a table asks for (never TabulatorFull). Screens call
   createTable(el, {columns, data, rowId, onRowClick, rowClass, groupBy,
   groupHeader, groupOrder, footerElement, select, onRender, download, filter,
   indicator, onIndicatorHeader, fitTitles, onColumnResized, onWidths, movable,
   onColumnMoved}). Columns resize only when they set resizable:true
   (TABULATOR.md Fitted titles, resizing). movable sets Tabulator
   movableColumns and registers MoveColumnsModule. There is no column
   movable flag in Tabulator 6.6.
   The table fills el (height 100%, virtual rows) and ResizeTable redraws it
   on resize and when a hidden panel is shown. The look is .pav-table on
   --pav-* variables in theme.css; no Tabulator stylesheet is loaded.
   Formatters, headers and the footer are DOM built with textContent. */
import { iconClass } from "./icons/registry.js";

let loading = null;
const registered = new Set();

function loadTabulator() {
  if (!loading) {
    loading = import("./tabulator-lib.js");
  }
  return loading;
}

/* Module names (export names) a table needs for its options. */
export function modulesFor(options) {
  const names = ["FormatModule", "InteractionModule", "ResizeTableModule"];
  const columns = options.columns || [];
  if (options.groupBy) {
    names.push("GroupRowsModule");
  }
  if (columns.some((c) => c.headerSort !== false)) {
    names.push("SortModule");
  }
  if (columns.some((c) => c.bottomCalc)) {
    names.push("ColumnCalcsModule");
  }
  if (options.select) {
    names.push("SelectRowModule");
  }
  if (options.download) {
    names.push("ExportModule", "DownloadModule");
  }
  if (options.filter) {
    names.push("FilterModule");
  }
  if (options.indicator || columns.some((c) => c.frozen)) {
    names.push("FrozenColumnsModule");
  }
  /* After FrozenColumns, so a frozen indicator is already marked and is not
     a drag source (TABULATOR.md Movable columns). */
  if (options.movable) {
    names.push("MoveColumnsModule");
  }
  if (columns.some((c) => c.resizable === true)) {
    names.push("ResizeColumnsModule");
  }
  return names;
}

function register(lib, names) {
  const fresh = names.filter((name) => !registered.has(name) && lib[name]);
  if (fresh.length > 0) {
    lib.Tabulator.registerModule(fresh.map((name) => lib[name]));
    for (const name of fresh) {
      registered.add(name);
    }
  }
}

/* Tri-state header sort icon in the kit style (K-Q12). */
export function sortIcon(dir, documentRef = globalThis.document) {
  const icon = documentRef.createElement("i");
  const name = dir === "asc" ? iconClass("table.sort.asc") : dir === "desc" ? iconClass("table.sort.desc") : iconClass("table.sort");
  icon.className = `pav-sort-icon ${name}`;
  icon.setAttribute("aria-hidden", "true");
  return icon;
}

/* Row indicator column (TABULATOR.md): a frozen narrow first column showing
   the current row (caret) or a row state set by the page (editing: pen). */
export const INDICATOR_FIELD = "__pav_indicator";
export const INDICATOR_WIDTH = 40;
const INDICATOR_ICONS = { current: iconClass("table.current"), editing: iconClass("table.editing") };

function faIcon(name, documentRef) {
  const icon = documentRef.createElement("i");
  icon.className = name.includes(" ") ? name : `fa-sharp-duotone fa-solid ${name}`;
  icon.setAttribute("aria-hidden", "true");
  return icon;
}

/* stateOf(id) returns "current", "editing" or null. onHeader(button), when
   given, makes the header dots a button that opens the column chooser
   (TABULATOR.md Row indicator column). */
function indicatorHeader(onHeader, documentRef) {
  const icon = faIcon(iconClass("table.menu"), documentRef);
  if (typeof onHeader !== "function") {
    return icon;
  }
  const button = documentRef.createElement("button");
  button.type = "button";
  button.className = "pav-table-indicator-button";
  button.setAttribute("aria-label", "Choose columns");
  button.setAttribute("aria-haspopup", "dialog");
  button.setAttribute("aria-expanded", "false");
  button.append(icon);
  button.addEventListener("click", (event) => {
    event.stopPropagation();
    onHeader(button);
  });
  return button;
}

export function indicatorColumn(stateOf, rowId = "id", documentRef = globalThis.document, onHeader = null) {
  return {
    field: INDICATOR_FIELD,
    title: typeof onHeader === "function" ? "Choose columns" : "Row indicator",
    titleFormatter: () => indicatorHeader(onHeader, documentRef),
    width: INDICATOR_WIDTH,
    minWidth: INDICATOR_WIDTH,
    frozen: true,
    headerSort: false,
    resizable: false,
    download: false,
    hozAlign: "center",
    headerHozAlign: "center",
    cssClass: "pav-table-indicator pav-col-divider-right",
    formatter: (cell) => {
      const state = stateOf(cell.getRow().getData()[rowId]);
      const name = INDICATOR_ICONS[state];
      return name ? faIcon(name, documentRef) : "";
    },
  };
}

/* rowClass(data) names one pav-row-* class (or none) for the row. */
function markRow(row, rowClass) {
  const el = row.getElement();
  for (const name of [...el.classList]) {
    if (name.startsWith("pav-row-")) {
      el.classList.remove(name);
    }
  }
  const cls = rowClass(row.getData());
  if (cls) {
    el.classList.add(cls);
  }
}

/* TABULATOR.md Download options: without the Download module, Tabulator
   rejects these column keys, so strip them from the copies it gets. */
const DOWNLOAD_KEYS = ["download", "titleDownload", "accessorDownload"];
function withoutDownload(column) {
  const copy = { ...column };
  for (const key of DOWNLOAD_KEYS) {
    delete copy[key];
  }
  return copy;
}

/* The Tabulator options for a createTable call (exported for tests). */
export function tabulatorOptions(options, data, gate, documentRef = globalThis.document) {
  const allColumns = options.indicator
    ? [indicatorColumn(gate.stateOf || (() => null), options.rowId || "id", documentRef, options.onIndicatorHeader), ...(options.columns || [])]
    : options.columns || [];
  const columns = options.download ? allColumns : allColumns.map(withoutDownload);
  const sortable = (options.columns || []).some((c) => c.headerSort !== false);
  return {
    data,
    columns,
    index: options.rowId || "id",
    layout: "fitColumns",
    height: "100%",
    renderVertical: "virtual",
    autoResize: true,
    placeholder: options.placeholder || "",
    columnDefaults: { resizable: false, ...(sortable ? { headerSortTristate: true } : {}) },
    ...(sortable ? { headerSortElement: (_column, dir) => sortIcon(dir, documentRef) } : {}),
    ...(options.groupBy ? { groupBy: options.groupBy, groupStartOpen: true, groupToggleElement: "header" } : {}),
    ...(typeof options.groupHeader === "function"
      ? { groupHeader: (value, count, rows) => options.groupHeader(value, count, rows) }
      : {}),
    ...((options.columns || []).some((c) => c.bottomCalc) ? { columnCalcs: "table" } : {}),
    ...(options.select ? { selectableRows: 1, selectableRowsCheck: () => gate.open } : {}),
    ...(options.movable ? { movableColumns: true } : {}),
    ...(options.download ? { downloadRowRange: "all", downloadConfig: { rowGroups: false, columnCalcs: false } } : {}),
    ...(options.footerElement ? { footerElement: options.footerElement } : {}),
    ...(typeof options.rowClass === "function" ? { rowFormatter: (row) => markRow(row, options.rowClass) } : {}),
  };
}

export function createTable(el, options) {
  el.classList.add("pav-table");
  if (options.select) {
    el.tabIndex = 0;
  }
  let table = null;
  let built = false;
  let destroyed = false;
  let data = options.data || [];
  let current = null;
  let filterFn = null;
  const gate = { open: false, stateOf: null };
  /* TABULATOR.md zoom in place: base (100%) widths of the fixed columns, and
     the current width and font scales. */
  let pending = Promise.resolve();
  let widthScale = 1;
  let fontScale = 1;
  const base = {};
  for (const column of options.columns || []) {
    if (column.field && Number.isFinite(column.width)) {
      base[column.field] = column.width;
    }
  }
  if (options.indicator) {
    base[INDICATOR_FIELD] = INDICATOR_WIDTH;
  }

  function applyScale() {
    el.style.fontSize = fontScale === 1 ? "" : `${Math.round(fontScale * 1000) / 10}%`;
    if (!built || typeof table.getColumn !== "function") {
      return;
    }
    for (const [field, width] of Object.entries(base)) {
      const column = table.getColumn(field);
      if (column) {
        column.setWidth(Math.round(width * widthScale));
      }
    }
  }
  const states = new Map();
  gate.stateOf = (id) => (states.get(id) || (id === current && current !== null ? "current" : null));

  /* Tabulator's getRow warns on a miss. Ask it only when this id is in the
     rows we handed the table. */
  function rowPresent(id) {
    const key = options.rowId || "id";
    return id !== null && id !== undefined && data.some((row) => row && row[key] === id);
  }

  function reformat(id) {
    if (built && options.indicator && rowPresent(id)) {
      const row = table.getRow(id);
      if (row) {
        row.reformat();
      }
    }
  }
  const documentRef = el.ownerDocument || globalThis.document;

  function applyGroupOrder() {
    if (built && typeof options.groupOrder === "function") {
      table.setGroupValues([options.groupOrder(data)]);
    }
  }

  function markSelected() {
    if (!built || !options.select) {
      return;
    }
    gate.open = true;
    try {
      table.deselectRow();
      if (rowPresent(current) && table.getRow(current)) {
        table.selectRow(current);
      }
    } finally {
      gate.open = false;
    }
  }

  /* TABULATOR.md Fitted titles: widen a listed column whose rendered title,
     sort icon, gap and padding do not fit. Only widens; 0 in a DOM with no
     layout, so tests and hidden panes change nothing. */
  function fitTitles() {
    if (!built || !Array.isArray(options.fitTitles) || typeof table.getColumn !== "function") {
      return;
    }
    const view = documentRef.defaultView;
    for (const field of options.fitTitles) {
      const column = table.getColumn(field);
      const colEl = column && column.getElement();
      const title = colEl && colEl.querySelector(".tabulator-col-title");
      if (!title || !view) {
        continue;
      }
      const sorter = colEl.querySelector(".tabulator-col-sorter");
      const style = view.getComputedStyle(colEl);
      const pad = (parseFloat(style.paddingLeft) || 0) + (parseFloat(style.paddingRight) || 0)
        + (parseFloat(style.borderLeftWidth) || 0) + (parseFloat(style.borderRightWidth) || 0);
      const need = Math.ceil(title.scrollWidth + (sorter ? sorter.offsetWidth + 2 : 0) + pad + 2);
      if (title.scrollWidth > 0 && need > column.getWidth()) {
        column.setWidth(need);
        base[field] = need / widthScale;
      }
    }
  }

  /* TABULATOR.md full-height dividers: a 1 px background line on the table
     holder at each divider column edge, measured from the header cells. */
  function paintDividers() {
    const holder = el.querySelector(".tabulator-tableholder");
    if (!built || !holder) {
      return;
    }
    const box = holder.getBoundingClientRect();
    /* CSS zoom: client rects are scaled, background offsets are not. */
    const scale = holder.offsetWidth > 0 && box.width > 0 ? box.width / holder.offsetWidth : 1;
    const base = box.left;
    const xs = [];
    for (const colEl of el.querySelectorAll(".tabulator-header .tabulator-col")) {
      if (colEl.offsetParent === null && colEl.getBoundingClientRect().width === 0) {
        continue;
      }
      const rect = colEl.getBoundingClientRect();
      if (colEl.classList.contains("pav-col-divider-left")) {
        xs.push(Math.round((rect.left - base) / scale + holder.scrollLeft));
      }
      if (colEl.classList.contains("pav-col-divider-right")) {
        xs.push(Math.round((rect.right - base) / scale + holder.scrollLeft - 1));
      }
    }
    const line = "linear-gradient(var(--pav-table-divider), var(--pav-table-divider))";
    holder.style.backgroundImage = xs.length ? xs.map(() => line).join(", ") : "";
    holder.style.backgroundSize = xs.length ? xs.map(() => "1px 100%").join(", ") : "";
    holder.style.backgroundPosition = xs.length ? xs.map((x) => `${x}px 0`).join(", ") : "";
  }

  /* After anything that can change column widths: fit, dividers, then tell
     the page (pane minimum). */
  /* D-UI Tooltips, TABULATOR.md Header tooltips: every header cell gets the
     shared tooltip from its column title (data-pav-tip on the whole cell, so
     the sort icon and padding count) and never a native title. Tabulator
     reuses column elements across redraws, so this is idempotent. */
  function tipHeaders() {
    if (!table || typeof table.getColumns !== "function") {
      return;
    }
    for (const column of table.getColumns()) {
      const colEl = column.getElement();
      const def = typeof column.getDefinition === "function" ? column.getDefinition() || {} : {};
      if (!colEl) {
        continue;
      }
      for (const el of [colEl, ...colEl.querySelectorAll("[title]")]) {
        el.removeAttribute("title");
      }
      if (typeof def.title === "string" && def.title !== "") {
        colEl.dataset.pavTip = def.title;
      }
    }
  }

  function relayout() {
    if (!built) {
      return;
    }
    tipHeaders();
    fitTitles();
    paintDividers();
    if (typeof options.onWidths === "function") {
      options.onWidths();
    }
  }

  function rendered() {
    relayout();
    if (typeof options.onRender === "function") {
      options.onRender();
    }
  }

  void loadTabulator().then((lib) => {
    if (destroyed) {
      return;
    }
    register(lib, modulesFor(options));
    const initial = data;
    table = new lib.Tabulator(el, tabulatorOptions(options, initial, gate, documentRef));
    table.on("tableBuilt", () => {
      built = true;
      if (widthScale !== 1 || fontScale !== 1) {
        applyScale();
      }
      if (data !== initial) {
        /* setData arrived while the table was building. */
        void table.replaceData(data).then(() => {
          markSelected();
          rendered();
        });
      }
      if (filterFn) {
        table.setFilter(filterFn);
      }
      applyGroupOrder();
      markSelected();
      rendered();
    });
    table.on("dataSorted", () => queueMicrotask(rendered));
    /* Redraws (a hidden pane shown, zoom), resizes and visibility changes. */
    table.on("renderComplete", () => relayout());
    table.on("columnVisibilityChanged", () => queueMicrotask(relayout));
    table.on("columnResized", (column) => {
      if (column.getField() in base) {
        base[column.getField()] = column.getWidth() / widthScale;
      }
      if (typeof options.onColumnResized === "function") {
        options.onColumnResized(column.getField(), Math.round(column.getWidth() / widthScale));
      }
      relayout();
    });
    table.on("columnMoved", (_column, columns) => {
      if (typeof options.onColumnMoved !== "function" || !Array.isArray(columns)) {
        return;
      }
      const order = [];
      for (const column of columns) {
        const field = column && typeof column.getField === "function" ? column.getField() : "";
        if (field && field !== INDICATOR_FIELD) {
          order.push(field);
        }
      }
      options.onColumnMoved(order);
    });
    if (typeof options.onRowClick === "function") {
      table.on("rowClick", (_event, row) => options.onRowClick(row.getData()));
    }
  });

  return {
    setData(next) {
      data = next;
      if (built) {
        applyGroupOrder();
        pending = table.replaceData(next).then(() => {
          markSelected();
          rendered();
        });
      }
    },
    /* Zoom in place (TABULATOR.md): font scale on the element, fixed columns at
       base x width scale; the pane is not touched. */
    setScale(nextWidthScale, nextFontScale = nextWidthScale) {
      widthScale = Number.isFinite(nextWidthScale) && nextWidthScale > 0 ? nextWidthScale : 1;
      fontScale = Number.isFinite(nextFontScale) && nextFontScale > 0 ? nextFontScale : 1;
      applyScale();
      if (built) {
        table.redraw(true);
        relayout();
      }
    },
    /* TABULATOR.md Refresh refits: after any pending data load, redraw(true),
       re-apply base widths x scale, fit titles, dividers, onWidths, and keep the
       current row selected. Resolves when done. */
    refit() {
      return pending.catch(() => {}).then(() => {
        if (!built || destroyed) {
          return;
        }
        table.redraw(true);
        applyScale();
        relayout();
        markSelected();
      });
    },
    /* After a zoom change or a pane shown: redraw and re-measure. */
    redraw() {
      if (built) {
        table.redraw(true);
        relayout();
      }
    },
    /* Vertical scrollbar width of the rows area (pane minimum). */
    scrollbarWidth() {
      const holder = el.querySelector(".tabulator-tableholder");
      return holder ? Math.max(0, holder.offsetWidth - holder.clientWidth) : 0;
    },
    /* Put data columns into this field order (Reset). The indicator stays put. */
    setColumnOrder(fields) {
      if (!built || !Array.isArray(fields) || typeof table.getColumn !== "function") {
        return;
      }
      const list = fields.filter((field) => field && field !== INDICATOR_FIELD && table.getColumn(field));
      for (let i = 1; i < list.length; i += 1) {
        const column = table.getColumn(list[i]);
        if (column && typeof column.move === "function") {
          column.move(list[i - 1], true);
        }
      }
    },
    /* Set one column's width (Reset restores the coded widths). */
    setColumnWidth(field, width) {
      const column = built ? table.getColumn(field) : null;
      if (Number.isFinite(width)) {
        base[field] = width;
        if (column) {
          column.setWidth(Math.round(width * widthScale));
        }
      }
    },
    /* Current widths of the shown columns, by field (pane minimum). */
    widths() {
      const out = {};
      if (built && typeof table.getColumns === "function") {
        for (const column of table.getColumns()) {
          const field = column.getField();
          if (field && column.isVisible()) {
            out[field] = Math.round(column.getWidth() / widthScale);
          }
        }
      }
      return out;
    },
    /* Column chooser (TABULATOR.md): show or hide one column by field. */
    setColumnVisible(field, on) {
      if (built) {
        if (on) {
          table.showColumn(field);
        } else {
          table.hideColumn(field);
        }
      }
    },
    /* D-10: a row predicate (null clears); groups with no match disappear. */
    setFilter(fn) {
      filterFn = typeof fn === "function" ? fn : null;
      if (built && options.filter) {
        if (filterFn) {
          table.setFilter(filterFn);
        } else {
          table.clearFilter(true);
        }
        queueMicrotask(rendered);
      }
    },
    /* Single selection, set only here; scrolls the row into view. */
    select(id) {
      const previous = current;
      current = id;
      reformat(previous);
      reformat(id);
      if (built) {
        markSelected();
        if (rowPresent(id) && table.getRow(id)) {
          void table.scrollToRow(id, "nearest", false);
        }
      }
    },
    /* Row indicator: set "editing" (or null) on a row; current is automatic. */
    setRowState(id, state) {
      if (state) {
        states.set(id, state);
      } else {
        states.delete(id);
      }
      reformat(id);
    },
    rowState: (id) => gate.stateOf(id),
    /* Row ids in display order: groups as shown, rows sorted within them. */
    ids() {
      if (!built) {
        return data.map((row) => row[options.rowId || "id"]);
      }
      const rows = options.groupBy
        ? table.getGroups().flatMap((group) => group.getRows())
        : table.getRows("active");
      /* Groups list only rows that pass the filter. */
      return rows.map((row) => row.getIndex());
    },
    download(filename) {
      if (built && options.download) {
        table.download("csv", filename);
        return true;
      }
      return false;
    },
    destroy() {
      destroyed = true;
      if (table) {
        table.destroy();
        table = null;
      }
      el.classList.remove("pav-table");
    },
  };
}
