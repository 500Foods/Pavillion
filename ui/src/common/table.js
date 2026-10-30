/* The shared table (N Lock, Shared table component; TABULATOR.md): the only
   module that imports Tabulator, loaded on first use as its own chunk, core
   plus the modules a table asks for (never TabulatorFull). Screens call
   createTable(el, {columns, data, rowId, onRowClick, rowClass, groupBy,
   groupHeader, groupOrder, footerElement, select, onRender, download, filter,
   indicator, onIndicatorHeader, fitTitles, onColumnResized, onWidths, movable,
   onColumnMoved, scroll, onScroll}).
   scroll is {top, left} in pixels on the row holder, applied once when the
   table is built. onScroll reports later movement of that holder.
   select(id) scrolls that row into view. select(id, {reveal:false}) leaves
   the scroll where it is, which is how a restored position stays put.
   The table fills el (height 100%, layout fitColumns, virtual rows).
   ResizeTable redraws it when the pane or a split changes size. A column
   with widthGrow shares the spare width and is not given a fixed width
   (fitColumns treats a missing widthGrow as 1, so every other column is
   given a width). A table that names none grows its last movable column.
   The fixed columns size to the header and the rendered cells. The first
   real measure may shrink a coded width; after that a measure only widens.
   lockWidth keeps a saved width. A column resize writes that width and the
   difference comes out of the widthGrow columns, so the rows stay the width
   of the pane. Dragging the only widthGrow column moves the fixed column
   beside it. A horizontal bar appears only when the minimums do not fit.
   Header handles are the default. Columns can be dragged; movable:false
   opts out. There is no column movable flag in Tabulator 6.6.
   The look is .pav-table on --pav-* variables in theme.css; no Tabulator
   stylesheet is loaded. Formatters, headers and the footer are DOM built
   with textContent. */
import { iconClass } from "./icons/registry.js";
import { cleanScroll } from "./table-place.js";

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
     a drag source (TABULATOR.md). Moving and header resizing are the default. */
  if (options.movable !== false) {
    names.push("MoveColumnsModule");
  }
  names.push("ResizeColumnsModule");
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

/* fitColumns gives every column without a width a share of the spare space
   (widthGrow defaults to 1). A designated column keeps widthGrow and loses
   any width. Every other column gets a width so it stays out of that share.
   lockWidth is ours, not a Tabulator option. resizable:true becomes the
   header handle so a row cell is not a resize target. */
function prepareColumn(column) {
  const copy = { ...column };
  delete copy.lockWidth;
  if (Number(copy.widthGrow) > 0) {
    delete copy.width;
  } else if (!Number.isFinite(copy.width)) {
    copy.width = Number.isFinite(copy.minWidth) ? copy.minWidth : 40;
  }
  if (copy.resizable === true) {
    copy.resizable = "header";
  }
  return copy;
}

/* A table that names no widthGrow column still fills: the last movable
   column takes the spare width. */
function ensureGrow(columns) {
  if (columns.some((column) => Number(column.widthGrow) > 0 && column.visible !== false)) {
    return columns;
  }
  for (let i = columns.length - 1; i >= 0; i -= 1) {
    const column = columns[i];
    if (column.visible === false || column.frozen || column.resizable === false) {
      continue;
    }
    delete column.width;
    column.widthGrow = 1;
    if (!Number.isFinite(column.minWidth)) {
      column.minWidth = 48;
    }
    break;
  }
  return columns;
}

function layoutColumns(columns) {
  return ensureGrow(columns.map(prepareColumn));
}

/* The Tabulator options for a createTable call (exported for tests). */
export function tabulatorOptions(options, data, gate, documentRef = globalThis.document) {
  const allColumns = options.indicator
    ? [indicatorColumn(gate.stateOf || (() => null), options.rowId || "id", documentRef, options.onIndicatorHeader), ...(options.columns || [])]
    : options.columns || [];
  const columns = layoutColumns(options.download ? allColumns : allColumns.map(withoutDownload));
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
    columnDefaults: { resizable: "header", ...(sortable ? { headerSortTristate: true } : {}) },
    ...(sortable ? { headerSortElement: (_column, dir) => sortIcon(dir, documentRef) } : {}),
    ...(options.groupBy ? { groupBy: options.groupBy, groupStartOpen: true, groupToggleElement: "header" } : {}),
    ...(typeof options.groupHeader === "function"
      ? { groupHeader: (value, count, rows) => options.groupHeader(value, count, rows) }
      : {}),
    ...((options.columns || []).some((c) => c.bottomCalc) ? { columnCalcs: "table" } : {}),
    ...(options.select ? { selectableRows: 1, selectableRowsCheck: () => gate.open } : {}),
    ...(options.movable !== false ? { movableColumns: true } : {}),
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
  let dragField = null;
  let dragStart = 0;
  let neighborField = null;
  let neighborStart = 0;
  let adjusting = false;
  let balancing = false;
  let fitting = false;
  let filledAt = -1;
  let filledFixed = -1;
  /* Saved widths (lockWidth) are not content-fitted. A resize this session
     joins the same set so the next measure does not undo it. */
  const lockedFields = new Set((options.columns || []).filter((column) => column.lockWidth && column.field).map((column) => column.field));
  const userSized = new Set();
  /* A field that has been measured, or set with setColumnWidth. The first
     measure may shrink it. Later measures only widen. */
  const fitted = new Set();
  const base = {};
  /* A saved scroll is applied once per build. While it is pending, holder
     scroll events are ignored so the initial 0 does not replace it.
     pendingReveal is a select() that arrived before the table was built.
     openingScroll is kept for the redraw that runs when a hidden pane is
     shown: the hidden build can give up on the position before the holder
     can scroll. */
  const openingScroll = cleanScroll(options.scroll);
  let scrollPending = openingScroll;
  let scrollTries = 0;
  let applyingScroll = false;
  let pendingReveal = false;
  let showObserver = null;
  let showRedrawn = false;
  for (const column of options.columns || []) {
    if (column.field && Number.isFinite(column.width) && !(Number(column.widthGrow) > 0)) {
      base[column.field] = column.width;
    }
  }
  if (options.indicator) {
    base[INDICATOR_FIELD] = INDICATOR_WIDTH;
  }

  function columnDef(column) {
    if (!column || typeof column.getDefinition !== "function") {
      return null;
    }
    return column.getDefinition() || null;
  }

  function isGrow(def) {
    return !!def && Number(def.widthGrow) > 0;
  }

  function columnsNow() {
    if (!table || typeof table.getColumns !== "function") {
      return [];
    }
    const list = table.getColumns();
    return Array.isArray(list) ? list : [];
  }

  function visibleColumns() {
    return columnsNow().filter((column) => typeof column.isVisible !== "function" || column.isVisible());
  }

  function holderEl() {
    return el.querySelector(".tabulator-tableholder");
  }

  function holderWidth() {
    const holder = holderEl();
    const width = holder ? holder.clientWidth : 0;
    return Number.isFinite(width) && width > 0 ? width : 0;
  }

  function scrollPinned(pos) {
    return Boolean(pos && (pos.top > 0 || pos.left > 0));
  }

  function writeScroll(pos) {
    const holder = holderEl();
    if (!holder || !pos) {
      return false;
    }
    applyingScroll = true;
    try {
      holder.scrollTop = pos.top;
      holder.scrollLeft = pos.left;
    } finally {
      applyingScroll = false;
    }
    return holder.scrollTop === pos.top && holder.scrollLeft === pos.left;
  }

  function hasBox() {
    return el.offsetWidth > 0 || el.offsetHeight > 0;
  }

  function onHolderScroll() {
    if (applyingScroll || scrollPending || !hasBox() || typeof options.onScroll !== "function") {
      return;
    }
    const holder = holderEl();
    if (!holder) {
      return;
    }
    const pos = cleanScroll({ top: holder.scrollTop, left: holder.scrollLeft });
    if (pos) {
      options.onScroll(pos);
    }
  }

  function watchScroll() {
    const holder = holderEl();
    if (!holder || holder.dataset.pavScroll === "1") {
      return;
    }
    holder.dataset.pavScroll = "1";
    holder.addEventListener("scroll", onHolderScroll);
  }

  function revealRow(id) {
    if (built && rowPresent(id) && table.getRow(id)) {
      void table.scrollToRow(id, "nearest", false);
    }
  }

  /* tableBuilt, then one renderComplete if the value did not stick. A short
     list clamps the offset; after two tries the user's own scroll is saved. */
  function settleScroll() {
    if (destroyed) {
      return;
    }
    watchScroll();
    if (!scrollPending) {
      if (pendingReveal) {
        revealRow(current);
      }
      pendingReveal = false;
      return;
    }
    const pinned = scrollPinned(scrollPending);
    scrollTries += 1;
    const stuck = writeScroll(scrollPending);
    if (stuck || scrollTries >= 2 || !pinned) {
      scrollPending = null;
      if (!pinned && pendingReveal) {
        revealRow(current);
      }
      pendingReveal = false;
    }
  }

  /* Virtual rows are not drawn while the element has no box. WebKit's first
     visibility callback says the element is already showing, so ResizeTable
     does not force a redraw. Draw once when a box appears, and put the saved
     scroll back. */
  function redrawWhenShown() {
    if (destroyed || !table || showRedrawn || !hasBox()) {
      return;
    }
    showRedrawn = true;
    if (showObserver) {
      showObserver.disconnect();
      showObserver = null;
    }
    if (scrollPinned(openingScroll)) {
      scrollPending = openingScroll;
      scrollTries = 0;
      pendingReveal = false;
    }
    if (typeof table.redraw === "function") {
      table.redraw(true);
    }
    markSelected();
    relayout();
    settleScroll();
  }

  function armShowRedraw() {
    if (destroyed || hasBox() || typeof ResizeObserver !== "function") {
      return;
    }
    showObserver = new ResizeObserver(() => {
      redrawWhenShown();
    });
    showObserver.observe(el);
  }

  function minOf(def) {
    const stated = def && Number(def.minWidth);
    return Number.isFinite(stated) && stated > 0 ? stated : 40;
  }

  function applyScale() {
    el.style.fontSize = fontScale === 1 ? "" : `${Math.round(fontScale * 1000) / 10}%`;
    if (!built || typeof table.getColumn !== "function") {
      return;
    }
    for (const [field, width] of Object.entries(base)) {
      const column = table.getColumn(field);
      if (!column) {
        continue;
      }
      const px = Math.round(width * widthScale);
      const def = columnDef(column);
      /* fitColumns counts definition.width, not the live width. Keep them
         equal or the spare-width columns are sized against a stale total. */
      if (def && !isGrow(def)) {
        def.width = px;
      }
      column.setWidth(px);
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

  /* Header width: title, sort icon, the 2 px gap, and the cell padding.
     0 when the pane has no layout, so tests and hidden panes change nothing. */
  function headerNeed(column) {
    const colEl = column && typeof column.getElement === "function" ? column.getElement() : null;
    const title = colEl && colEl.querySelector(".tabulator-col-title");
    const view = documentRef.defaultView;
    if (!title || !view || title.scrollWidth <= 0) {
      return 0;
    }
    const sorter = colEl.querySelector(".tabulator-col-sorter");
    const style = view.getComputedStyle(colEl);
    const pad = (parseFloat(style.paddingLeft) || 0) + (parseFloat(style.paddingRight) || 0)
      + (parseFloat(style.borderLeftWidth) || 0) + (parseFloat(style.borderRightWidth) || 0);
    return Math.ceil(title.scrollWidth + (sorter ? sorter.offsetWidth + 2 : 0) + pad + 2);
  }

  /* The widest rendered cell. Virtual rows only have the cells on screen.
     A child scrollWidth is the text; the cell's own width is the column. */
  function cellNeed(column) {
    if (!column || typeof column.getCells !== "function" || !documentRef.defaultView) {
      return 0;
    }
    const view = documentRef.defaultView;
    let need = 0;
    for (const cell of column.getCells()) {
      const cellEl = cell && typeof cell.getElement === "function" ? cell.getElement() : null;
      if (!cellEl) {
        continue;
      }
      const style = view.getComputedStyle(cellEl);
      const pad = (parseFloat(style.paddingLeft) || 0) + (parseFloat(style.paddingRight) || 0)
        + (parseFloat(style.borderLeftWidth) || 0) + (parseFloat(style.borderRightWidth) || 0);
      const child = cellEl.firstElementChild;
      const content = child && child.scrollWidth > 0 ? child.scrollWidth : 0;
      if (content > 0) {
        need = Math.max(need, Math.ceil(content + pad + 2));
      }
    }
    return need;
  }

  function rememberFixed(column) {
    const field = column.getField();
    const def = columnDef(column);
    const px = column.getWidth();
    if (!def || !field || isGrow(def) || !Number.isFinite(px) || px <= 0) {
      return;
    }
    def.width = px;
    base[field] = px / widthScale;
    userSized.add(field);
  }

  /* Share `free` across grow columns by widthGrow, honouring minWidth.
     The last flexible column takes the leftover pixel so the sum matches. */
  function assignFlex(pool, free) {
    const widths = new Map();
    let rest = pool.slice();
    let space = free;
    let guard = rest.length + 2;
    while (rest.length > 0 && guard > 0) {
      guard -= 1;
      const units = rest.reduce((sum, item) => sum + item.grow, 0) || 1;
      const share = space / units;
      const stuck = rest.filter((item) => share * item.grow < item.min);
      if (stuck.length === 0) {
        let used = 0;
        rest.forEach((item, index) => {
          const width = index === rest.length - 1 ? space - used : Math.max(item.min, Math.floor(share * item.grow));
          widths.set(item.column, width);
          used += width;
        });
        break;
      }
      if (stuck.length === rest.length) {
        for (const item of rest) {
          widths.set(item.column, item.min);
        }
        break;
      }
      for (const item of stuck) {
        widths.set(item.column, item.min);
        space -= item.min;
      }
      const stuckSet = new Set(stuck);
      rest = rest.filter((item) => !stuckSet.has(item));
    }
    return widths;
  }

  function widthOf(column) {
    if (!column || typeof column.getWidth !== "function") {
      return 0;
    }
    const width = column.getWidth();
    return Number.isFinite(width) ? width : 0;
  }

  function fixedSum(excludeField) {
    let sum = 0;
    for (const column of visibleColumns()) {
      const def = columnDef(column);
      const field = typeof column.getField === "function" ? column.getField() : "";
      if (isGrow(def) && field !== excludeField) {
        continue;
      }
      sum += widthOf(column);
    }
    return sum;
  }

  /* Give the spare width to the widthGrow columns. excludeField is a grow
     column the user is dragging: it keeps the dragged width and the other
     grow columns take what remains. */
  function balance(excludeField) {
    if (balancing || !built) {
      return;
    }
    const total = holderWidth();
    if (total <= 0) {
      return;
    }
    const columns = visibleColumns();
    if (columns.length === 0) {
      return;
    }
    const flex = [];
    let fixed = 0;
    for (const column of columns) {
      const def = columnDef(column);
      const field = typeof column.getField === "function" ? column.getField() : "";
      if (isGrow(def) && field !== excludeField) {
        flex.push({ column, grow: Number(def.widthGrow) || 1, min: minOf(def) });
      } else {
        fixed += widthOf(column);
      }
    }
    if (flex.length === 0) {
      return;
    }
    const widths = assignFlex(flex, total - fixed);
    balancing = true;
    adjusting = true;
    try {
      for (const [column, width] of widths) {
        if (widthOf(column) !== width && typeof column.setWidth === "function") {
          column.setWidth(width);
        }
      }
    } finally {
      adjusting = false;
      balancing = false;
    }
    if (!excludeField) {
      filledAt = total;
      filledFixed = fixed;
    }
  }

  function otherGrowCount(column) {
    const field = column.getField();
    return visibleColumns().filter((item) => {
      const def = columnDef(item);
      return isGrow(def) && item.getField() !== field;
    }).length;
  }

  /* The column that shares the dragged edge. The handle sits on the right,
     so the next fixed column is first. */
  function neighborFixed(column) {
    const list = visibleColumns();
    const field = column.getField();
    const index = list.findIndex((item) => item.getField() === field);
    const ok = (item) => {
      const def = columnDef(item);
      return def && !isGrow(def) && def.resizable !== false;
    };
    if (index < 0) {
      return null;
    }
    for (let i = index + 1; i < list.length; i += 1) {
      if (ok(list[i])) {
        return list[i];
      }
    }
    for (let i = index - 1; i >= 0; i -= 1) {
      if (ok(list[i])) {
        return list[i];
      }
    }
    return null;
  }

  /* TABULATOR.md Fitted titles: widen a listed column whose rendered title,
     sort icon, gap and padding do not fit. Only widens; 0 in a DOM with no
     layout, so tests and hidden panes change nothing. */
  function fitTitles() {
    if (!built || !Array.isArray(options.fitTitles) || typeof table.getColumn !== "function") {
      return;
    }
    for (const field of options.fitTitles) {
      const column = table.getColumn(field);
      if (!column) {
        continue;
      }
      const need = headerNeed(column);
      if (need > column.getWidth()) {
        column.setWidth(need);
        base[field] = need / widthScale;
        const def = columnDef(column);
        if (def && !isGrow(def)) {
          def.width = need;
        }
      }
    }
  }

  /* Size fixed columns to the header and the cells on screen. The first
     real measure may shrink a coded width. After that, only widen, so a
     scroll does not jitter. Grow columns, unresizable columns, saved
     widths, and a width the user just set are left alone. */
  function fitContents() {
    if (!built || fitting || balancing || dragField) {
      return;
    }
    fitting = true;
    try {
      let changed = false;
      for (const column of visibleColumns()) {
        const def = columnDef(column);
        const field = typeof column.getField === "function" ? column.getField() : "";
        if (!def || !field || isGrow(def) || def.frozen || def.resizable === false || lockedFields.has(field) || userSized.has(field)) {
          continue;
        }
        const measured = Math.max(headerNeed(column), cellNeed(column));
        if (measured <= 0 || typeof column.getWidth !== "function") {
          continue;
        }
        const current = column.getWidth();
        if (!Number.isFinite(current)) {
          continue;
        }
        const next = Math.max(minOf(def), measured);
        if (fitted.has(field) && next <= current) {
          if (def.width !== current) {
            def.width = current;
            base[field] = current / widthScale;
          }
          continue;
        }
        fitted.add(field);
        if (current === next) {
          if (def.width !== next) {
            def.width = next;
            base[field] = next / widthScale;
          }
          continue;
        }
        def.width = next;
        base[field] = next / widthScale;
        if (typeof column.setWidth === "function") {
          column.setWidth(next);
        }
        changed = true;
      }
      if (changed) {
        balance(null);
      }
    } finally {
      fitting = false;
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
    if (!built || balancing) {
      return;
    }
    tipHeaders();
    fitTitles();
    fitContents();
    const total = holderWidth();
    if (!dragField && total > 0) {
      const fixed = fixedSum(null);
      if (total !== filledAt || fixed !== filledFixed) {
        balance(null);
      }
    }
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
      settleScroll();
      armShowRedraw();
    });
    table.on("dataSorted", () => queueMicrotask(rendered));
    /* Redraws (a hidden pane shown, zoom), resizes and visibility changes.
       The saved scroll is applied once. A later renderComplete does not
       put it back after the user has moved the list. */
    table.on("renderComplete", () => {
      relayout();
      if (scrollPending) {
        settleScroll();
      } else {
        watchScroll();
      }
    });
    table.on("columnVisibilityChanged", () => queueMicrotask(relayout));
    table.on("columnResizing", (column) => {
      if (!column || typeof column.getField !== "function") {
        return;
      }
      dragField = column.getField();
      dragStart = column.getWidth();
      neighborField = null;
      neighborStart = 0;
      const def = columnDef(column);
      if (def && isGrow(def) && otherGrowCount(column) === 0) {
        const neighbor = neighborFixed(column);
        if (neighbor) {
          neighborField = neighbor.getField();
          neighborStart = neighbor.getWidth();
        }
      }
    });
    /* While the pointer moves, keep the pane full. A fixed column keeps the
       dragged width and the widthGrow columns share what is left. The only
       widthGrow column instead moves the fixed column beside it. */
    table.on("columnWidth", (column) => {
      if (adjusting || balancing || !dragField || !column || column.getField() !== dragField) {
        return;
      }
      const def = columnDef(column);
      if (!def) {
        return;
      }
      if (!isGrow(def)) {
        rememberFixed(column);
        balance(null);
        return;
      }
      if (otherGrowCount(column) > 0) {
        balance(dragField);
        return;
      }
      if (!neighborField || typeof table.getColumn !== "function") {
        return;
      }
      const neighbor = table.getColumn(neighborField);
      const neighborDef = columnDef(neighbor);
      if (!neighbor || !neighborDef) {
        return;
      }
      const delta = column.getWidth() - dragStart;
      const next = Math.max(minOf(neighborDef), Math.round(neighborStart - delta));
      adjusting = true;
      try {
        neighborDef.width = next;
        base[neighborField] = next / widthScale;
        userSized.add(neighborField);
        if (neighbor.getWidth() !== next) {
          neighbor.setWidth(next);
        }
      } finally {
        adjusting = false;
      }
    });
    table.on("columnResized", (column) => {
      const field = column && typeof column.getField === "function" ? column.getField() : "";
      const def = columnDef(column);
      if (def && field && !isGrow(def)) {
        rememberFixed(column);
      } else if (field && Object.prototype.hasOwnProperty.call(base, field) && column) {
        base[field] = column.getWidth() / widthScale;
      }
      dragField = null;
      neighborField = null;
      if (typeof options.onColumnResized === "function" && field) {
        options.onColumnResized(field, Math.round(column.getWidth() / widthScale));
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
      return pending;
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
      const column = built && typeof table.getColumn === "function" ? table.getColumn(field) : null;
      if (!Number.isFinite(width)) {
        return;
      }
      base[field] = width;
      userSized.delete(field);
      lockedFields.delete(field);
      /* Reset restores a coded width. Count it as measured so the next
         layout does not immediately shrink it back to the cells. */
      fitted.add(field);
      if (column) {
        const px = Math.round(width * widthScale);
        const def = columnDef(column);
        if (def && !isGrow(def)) {
          def.width = px;
        }
        column.setWidth(px);
        balance(null);
      }
    },
    /* Reset puts the coded widthGrow weights back. Those columns have no
       fixed width, so the next balance shares the pane by the new weights. */
    setGrowWeights(weights) {
      if (!built || !weights || typeof weights !== "object" || typeof table.getColumn !== "function") {
        return;
      }
      for (const [field, weight] of Object.entries(weights)) {
        if (!Number.isFinite(weight) || weight <= 0) {
          continue;
        }
        const column = table.getColumn(field);
        const def = columnDef(column);
        if (!def) {
          continue;
        }
        def.widthGrow = weight;
        delete def.width;
      }
      balance(null);
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
    /* Single selection, set only here. The default scrolls the row into
       view. reveal:false leaves a restored pixel scroll alone. A pending
       non-zero scroll wins over that scroll-into-view. */
    select(id, opts) {
      const previous = current;
      current = id;
      const reveal = !opts || opts.reveal !== false;
      reformat(previous);
      reformat(id);
      if (!built) {
        pendingReveal = reveal;
        return;
      }
      markSelected();
      if (scrollPending && scrollPinned(scrollPending)) {
        return;
      }
      if (reveal) {
        revealRow(id);
      }
    },
    /* Folder changes. replaceData keeps the previous scroll, so the caller
       sets the position for the rows just loaded. */
    setScroll(pos) {
      const clean = cleanScroll(pos) || { top: 0, left: 0 };
      pendingReveal = false;
      scrollTries = 0;
      if (!built) {
        scrollPending = scrollPinned(clean) ? clean : null;
        return;
      }
      const stuck = writeScroll(clean);
      if (stuck || !scrollPinned(clean)) {
        scrollPending = null;
      } else {
        /* The new rows may not have a scroll height yet. One renderComplete retries. */
        scrollPending = clean;
        scrollTries = 1;
      }
      watchScroll();
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
      if (showObserver) {
        showObserver.disconnect();
        showObserver = null;
      }
      if (table) {
        table.destroy();
        table = null;
      }
      el.classList.remove("pav-table");
    },
  };
}
