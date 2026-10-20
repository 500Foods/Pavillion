/* The shared table (N Lock, Shared table component; TABULATOR.md): the only
   module that imports Tabulator, loaded on first use as its own chunk, core
   plus the modules a table asks for (never TabulatorFull). Screens call
   createTable(el, {columns, data, rowId, onRowClick, rowClass, groupBy,
   groupHeader, groupOrder, footerElement, select, onRender, download, filter,
   indicator}).
   The table fills el (height 100%, virtual rows) and ResizeTable redraws it
   on resize and when a hidden panel is shown. The look is .pav-table on
   --pav-* variables in theme.css; no Tabulator stylesheet is loaded.
   Formatters, headers and the footer are DOM built with textContent. */
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
  const name = dir === "asc" ? "fa-sort-up" : dir === "desc" ? "fa-sort-down" : "fa-sort";
  icon.className = `pav-sort-icon fa-sharp-duotone fa-solid ${name}`;
  icon.setAttribute("aria-hidden", "true");
  return icon;
}

/* Row indicator column (TABULATOR.md): a frozen narrow first column showing
   the current row (caret) or a row state set by the page (editing: pen). */
export const INDICATOR_FIELD = "__pav_indicator";
export const INDICATOR_WIDTH = 22;
const INDICATOR_ICONS = { current: "fa-caret-right", editing: "fa-pen" };

function faIcon(name, documentRef) {
  const icon = documentRef.createElement("i");
  icon.className = `fa-sharp-duotone fa-solid ${name}`;
  icon.setAttribute("aria-hidden", "true");
  return icon;
}

/* stateOf(id) returns "current", "editing" or null. */
export function indicatorColumn(stateOf, rowId = "id", documentRef = globalThis.document) {
  return {
    field: INDICATOR_FIELD,
    title: "Row indicator",
    titleFormatter: () => faIcon("fa-ellipsis-vertical", documentRef),
    width: INDICATOR_WIDTH,
    minWidth: INDICATOR_WIDTH,
    frozen: true,
    headerSort: false,
    resizable: false,
    download: false,
    hozAlign: "center",
    headerHozAlign: "center",
    cssClass: "pav-table-indicator",
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

/* The Tabulator options for a createTable call (exported for tests). */
export function tabulatorOptions(options, data, gate, documentRef = globalThis.document) {
  const columns = options.indicator
    ? [indicatorColumn(gate.stateOf || (() => null), options.rowId || "id", documentRef), ...(options.columns || [])]
    : options.columns || [];
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
    ...(sortable
      ? { columnDefaults: { headerSortTristate: true }, headerSortElement: (_column, dir) => sortIcon(dir, documentRef) }
      : {}),
    ...(options.groupBy ? { groupBy: options.groupBy, groupStartOpen: true, groupToggleElement: "header" } : {}),
    ...(typeof options.groupHeader === "function"
      ? { groupHeader: (value, count, rows) => options.groupHeader(value, count, rows) }
      : {}),
    ...((options.columns || []).some((c) => c.bottomCalc) ? { columnCalcs: "table" } : {}),
    ...(options.select ? { selectableRows: 1, selectableRowsCheck: () => gate.open } : {}),
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
  const states = new Map();
  gate.stateOf = (id) => (states.get(id) || (id === current && current !== null ? "current" : null));

  function reformat(id) {
    if (built && options.indicator && id !== null && id !== undefined) {
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
      if (current !== null && table.getRow(current)) {
        table.selectRow(current);
      }
    } finally {
      gate.open = false;
    }
  }

  function rendered() {
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
    if (typeof options.onRowClick === "function") {
      table.on("rowClick", (_event, row) => options.onRowClick(row.getData()));
    }
  });

  return {
    setData(next) {
      data = next;
      if (built) {
        applyGroupOrder();
        void table.replaceData(next).then(() => {
          markSelected();
          rendered();
        });
      }
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
        if (id !== null && table.getRow(id)) {
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
