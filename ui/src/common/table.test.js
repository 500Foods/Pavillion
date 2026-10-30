/**
 * @vitest-environment jsdom
 */
import { expect, test, vi } from "vitest";

const made = [];
vi.mock("tabulator-tables", () => {
  class Tabulator {
    static registered = [];
    static registerModule(list) {
      Tabulator.registered.push(...list);
    }
    constructor(el, options) {
      this.el = el;
      this.options = options;
      this.handlers = {};
      this.replaced = [];
      this.selected = [];
      this.calls = [];
      made.push(this);
    }
    on(name, fn) {
      this.handlers[name] = fn;
    }
    replaceData(data) {
      this.replaced.push(data);
      this.options.data = data;
      return Promise.resolve();
    }
    getRow(id) {
      this.calls.push(["getRow", id]);
      return this.options.data.find((r) => r.id === id) ? { id, reformat: () => this.calls.push(["reformat", id]) } : false;
    }
    selectRow(id) {
      if (this.options.selectableRowsCheck && !this.options.selectableRowsCheck()) {
        return;
      }
      this.selected = [id];
    }
    deselectRow() {
      this.selected = [];
    }
    scrollToRow(id) {
      this.calls.push(["scrollToRow", id]);
      return Promise.resolve();
    }
    setGroupValues(values) {
      this.calls.push(["setGroupValues", values]);
    }
    getGroups() {
      const keys = [...new Set(this.options.data.map((r) => r.g))];
      return keys.map((k) => ({ getRows: () => this.options.data.filter((r) => r.g === k).map((r) => ({ getIndex: () => r.id })) }));
    }
    getRows() {
      return this.options.data.map((r) => ({ getIndex: () => r.id }));
    }
    download(type, name) {
      this.calls.push(["download", type, name]);
    }
    destroy() {
      this.destroyed = true;
    }
  }
  const mod = (name) => name;
  return {
    Tabulator,
    FormatModule: mod("format"),
    InteractionModule: mod("interaction"),
    ResizeTableModule: mod("resizeTable"),
    GroupRowsModule: mod("groupRows"),
    SortModule: mod("sort"),
    ColumnCalcsModule: mod("columnCalcs"),
    SelectRowModule: mod("selectRow"),
    ExportModule: mod("export"),
    DownloadModule: mod("download"),
    ResizeColumnsModule: mod("resizeColumns"),
    FilterModule: mod("filter"),
    FrozenColumnsModule: mod("frozenColumns"),
    MoveColumnsModule: mod("moveColumns"),
    TabulatorFull: "full",
  };
});

import { createTable, indicatorColumn, INDICATOR_FIELD, INDICATOR_WIDTH, modulesFor, sortIcon, tabulatorOptions } from "./table.js";
import { accountColumns } from "../settings/accounts-table.js";

test("a plain table registers the shared modules, fills its element, and loads data once", async () => {
  const el = document.createElement("div");
  const clicks = [];
  const data = [{ id: "x" }];
  const handle = createTable(el, { columns: [{ field: "a", headerSort: false }], data, onRowClick: (row) => clicks.push(row.id) });
  expect(el.classList.contains("pav-table")).toBe(true);
  await vi.waitFor(() => expect(made.length).toBe(1));
  const t = made[0];
  const { Tabulator } = await import("tabulator-tables");
  expect(Tabulator.registered).toEqual(["format", "interaction", "resizeTable", "moveColumns", "resizeColumns"]);
  expect(Tabulator.registered).not.toContain("full");
  expect(t.options.layout).toBe("fitColumns");
  expect(t.options.height).toBe("100%");
  expect(t.options.renderVertical).toBe("virtual");
  expect(t.options.autoResize).toBe(true);
  expect(t.options.data).toBe(data);
  t.handlers.tableBuilt();
  expect(t.replaced).toEqual([]); /* no second load */
  handle.setData([{ id: "y" }]);
  expect(t.replaced.at(-1)).toEqual([{ id: "y" }]);
  t.handlers.rowClick(new Event("click"), { getData: () => ({ id: "y" }) });
  expect(clicks).toEqual(["y"]);
  handle.destroy();
  expect(t.destroyed).toBe(true);
});

test("the accounts options: tri-state FA sort, table calcs, single select, groups, download", async () => {
  const el = document.createElement("div");
  const footer = document.createElement("div");
  const renders = [];
  const columns = accountColumns(document);
  const data = [{ id: "a", g: "one" }, { id: "b", g: "two" }, { id: "c", g: "one" }];
  const handle = createTable(el, {
    columns,
    data,
    groupBy: "g",
    groupOrder: (rows) => [...new Set(rows.map((r) => r.g))],
    footerElement: footer,
    select: true,
    download: true,
    onRender: () => renders.push(1),
  });
  expect(modulesFor({ columns, groupBy: "g", select: true, download: true })).toEqual([
    "FormatModule", "InteractionModule", "ResizeTableModule", "GroupRowsModule", "SortModule",
    "ColumnCalcsModule", "SelectRowModule", "ExportModule", "DownloadModule", "MoveColumnsModule", "ResizeColumnsModule",
  ]);
  await vi.waitFor(() => expect(made.length).toBe(2));
  const t = made[1];
  expect(t.options.columnDefaults.headerSortTristate).toBe(true);
  expect(t.options.headerSortElement(null, "none").className).toContain("fa-sort");
  expect(t.options.headerSortElement(null, "asc").className).toContain("fa-sort-up");
  expect(t.options.headerSortElement(null, "desc").className).toContain("fa-sort-down");
  expect(sortIcon("asc", document).className).toContain("fa-sharp-duotone fa-solid");
  expect(t.options.columnCalcs).toBe("table");
  expect(t.options.selectableRows).toBe(1);
  expect(t.options.footerElement).toBe(footer);
  expect(t.options.groupToggleElement).toBe("header");
  expect(t.options.groupStartOpen).toBe(true);
  expect(t.options.downloadRowRange).toBe("all");
  expect(t.options.downloadConfig).toEqual({ rowGroups: false, columnCalcs: false });
  expect(el.tabIndex).toBe(0);
  /* A click cannot toggle the selection; only the page selects. */
  t.selectRow("a");
  expect(t.selected).toEqual([]);
  t.handlers.tableBuilt();
  expect(t.calls).toContainEqual(["setGroupValues", [["one", "two"]]]);
  handle.select("c");
  expect(t.selected).toEqual(["c"]);
  expect(t.calls).toContainEqual(["scrollToRow", "c"]);
  expect(handle.ids()).toEqual(["a", "c", "b"]);
  handle.setData([{ id: "c", g: "one" }]);
  await vi.waitFor(() => expect(t.selected).toEqual(["c"])); /* kept across replaceData */
  const asked = () => t.calls.filter((call) => call[0] === "getRow" && call[1] === "c").length;
  const before = asked();
  handle.setData([{ id: "gone", g: "one" }]);
  await vi.waitFor(() => expect(t.replaced.at(-1)).toEqual([{ id: "gone", g: "one" }]));
  await Promise.resolve();
  expect(asked()).toBe(before);
  expect(renders.length).toBeGreaterThan(0);
  expect(handle.download("x.csv")).toBe(true);
  expect(t.calls).toContainEqual(["download", "csv", "x.csv"]);
  handle.destroy();
});

test("the accounts table fills its width with Name and keeps the other columns narrow", () => {
  const columns = accountColumns(document).filter((c) => c.visible !== false);
  const name = columns.find((c) => c.field === "name");
  expect(name.widthGrow).toBe(1);
  expect(name.width).toBeUndefined();
  for (const column of columns.filter((c) => c.field !== "name")) {
    expect(column.width).toBeLessThanOrEqual(100);
    expect(column.resizable).toBe(column.field !== "account");
  }
});

test("indicatorColumn: narrow, frozen, unsortable, not exported, no calc; icons by state", () => {
  const states = { a: "current", b: "editing" };
  const col = indicatorColumn((id) => states[id] || null);
  expect(col).toMatchObject({ field: INDICATOR_FIELD, width: INDICATOR_WIDTH, frozen: true, headerSort: false, resizable: false, download: false });
  expect(col.bottomCalc).toBeUndefined();
  expect(col.titleFormatter().className).toBe("fa-sharp-duotone fa-solid fa-ellipsis-vertical");
  const cell = (id) => ({ getRow: () => ({ getData: () => ({ id }) }) });
  expect(col.formatter(cell("a")).className).toContain("fa-caret-right");
  expect(col.formatter(cell("b")).className).toContain("fa-pen");
  expect(col.formatter(cell("c"))).toBe("");
});

test("the indicator option adds the frozen first column and follows select and setRowState", async () => {
  expect(modulesFor({ columns: [], indicator: true })).toContain("FrozenColumnsModule");
  expect(modulesFor({ columns: [{ field: "a", headerSort: false }] })).not.toContain("FrozenColumnsModule");
  const el = document.createElement("div");
  const data = [{ id: "a" }, { id: "b" }];
  const before = made.length;
  const handle = createTable(el, { indicator: true, select: true, columns: [{ field: "n", headerSort: false }], data });
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const t = made.at(-1);
  expect(t.options.columns.map((c) => c.field)).toEqual([INDICATOR_FIELD, "n"]);
  t.handlers.tableBuilt();
  const ind = t.options.columns[0];
  const icon = (id) => {
    const out = ind.formatter({ getRow: () => ({ getData: () => ({ id }) }) });
    return out === "" ? "" : out.className.split(" ").at(-1);
  };
  expect(icon("a")).toBe("");
  handle.select("a");
  expect(icon("a")).toBe("fa-caret-right");
  handle.select("b");
  expect(icon("a")).toBe("");
  expect(icon("b")).toBe("fa-caret-right");
  expect(t.calls.filter((c) => c[0] === "reformat").map((c) => c[1])).toEqual(["a", "a", "b"]);
  handle.setRowState("a", "editing");
  expect(icon("a")).toBe("fa-pen");
  expect(handle.rowState("a")).toBe("editing");
  handle.setRowState("a", null);
  expect(icon("a")).toBe("");
  handle.destroy();
});

test("without the option there is no indicator column", () => {
  const cols = accountColumns(document);
  expect(cols.some((c) => c.field === INDICATOR_FIELD)).toBe(false);
});

test("resize round: header handles by default, indicator divider, resize events, fitted titles, widths", async () => {
  const { tabulatorOptions, indicatorColumn } = await import("./table.js");
  expect(tabulatorOptions({ columns: [{ field: "a", headerSort: false }] }, [], { open: false }).columnDefaults).toEqual({ resizable: "header" });
  expect(indicatorColumn(() => null).cssClass).toBe("pav-table-indicator pav-col-divider-right");
  expect(modulesFor({ columns: [{ field: "a", resizable: true }] })).toContain("ResizeColumnsModule");
  expect(modulesFor({ columns: [{ field: "a" }] })).toContain("ResizeColumnsModule");
  const el = document.createElement("div");
  document.body.append(el);
  const resized = [];
  const before = made.length;
  const handle = createTable(el, {
    columns: [{ field: "a", width: 40, resizable: true, headerSort: false }, { field: "b", width: 60, headerSort: false }],
    data: [{ id: "x" }],
    fitTitles: ["a", "b"],
    onColumnResized: (field, width) => resized.push([field, width]),
  });
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const t = made.at(-1);
  /* Fake columns: "a" has a 50 px title (+ 10 sorter + 2 gap + 8 padding + 2), "b" fits. */
  const mkCol = (field, width, titleWidth, visible = true) => {
    const colEl = document.createElement("div");
    colEl.style.paddingLeft = "4px";
    colEl.style.paddingRight = "4px";
    const title = document.createElement("span");
    title.className = "tabulator-col-title";
    Object.defineProperty(title, "scrollWidth", { value: titleWidth });
    const sorter = document.createElement("span");
    sorter.className = "tabulator-col-sorter";
    Object.defineProperty(sorter, "offsetWidth", { value: 10 });
    colEl.append(title, sorter);
    const col = { width, getField: () => field, getElement: () => colEl, getWidth: () => col.width, setWidth: (w) => { col.width = w; }, isVisible: () => visible };
    return col;
  };
  const cols = { a: mkCol("a", 40, 50), b: mkCol("b", 60, 20), c: mkCol("c", 70, 0, false) };
  t.getColumn = (f) => cols[f];
  t.getColumns = () => Object.values(cols);
  t.handlers.tableBuilt();
  expect(cols.a.width).toBe(72);
  expect(cols.b.width).toBe(60);
  t.handlers.renderComplete();
  expect(cols.a.width).toBe(72); /* only widens, and it fits now */
  t.handlers.columnResized(cols.b);
  expect(resized).toEqual([["b", 60]]);
  handle.setColumnWidth("b", 90);
  expect(cols.b.width).toBe(90);
  expect(handle.widths()).toEqual({ a: 72, b: 90 });
  handle.destroy();
});

test("K-Q14: full-height dividers on the holder, onWidths after relayout, redraw, scrollbar width", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const before = made.length;
  let widthsCalls = 0;
  const handle = createTable(el, { columns: [{ field: "a", headerSort: false }], data: [{ id: "x" }], onWidths: () => { widthsCalls += 1; } });
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const t = made.at(-1);
  t.redraws = [];
  t.redraw = (force) => t.redraws.push(force);
  /* Build the DOM Tabulator would: a header with two divider columns and the holder. */
  const header = document.createElement("div");
  header.className = "tabulator-header";
  const rect = (left, width) => () => ({ left, right: left + width, width, top: 0, bottom: 20, height: 20 });
  const mk = (cls, left, width) => {
    const c = document.createElement("div");
    c.className = `tabulator-col ${cls}`;
    c.getBoundingClientRect = rect(left, width);
    header.append(c);
  };
  mk("pav-col-divider-right", 100, 22);
  mk("", 122, 60);
  mk("pav-col-divider-left", 182, 44);
  const holder = document.createElement("div");
  holder.className = "tabulator-tableholder";
  holder.getBoundingClientRect = rect(100, 400);
  Object.defineProperty(holder, "offsetWidth", { value: 400 });
  Object.defineProperty(holder, "clientWidth", { value: 385 });
  el.append(header, holder);
  t.handlers.tableBuilt();
  expect(holder.style.backgroundPosition).toBe("21px 0px, 82px 0px");
  expect(holder.style.backgroundSize).toBe("1px 100%, 1px 100%");
  expect(holder.style.backgroundImage).toContain("var(--pav-table-divider)");
  expect(widthsCalls).toBeGreaterThan(0);
  const n = widthsCalls;
  handle.redraw();
  expect(t.redraws).toEqual([true]);
  expect(widthsCalls).toBe(n + 1);
  expect(handle.scrollbarWidth()).toBe(15);
  handle.destroy();
});

test("zoom in place: setScale sets the font, scales fixed columns from base widths, reports base widths", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const resized = [];
  const before = made.length;
  const handle = createTable(el, {
    indicator: true,
    columns: [{ field: "n", width: 60, resizable: true, headerSort: false }, { field: "name", widthGrow: 1, headerSort: false }],
    data: [{ id: "x" }],
    onColumnResized: (f, w) => resized.push([f, w]),
  });
  handle.setScale(1.5, 1.25); /* before build: kept and applied at build */
  expect(el.style.fontSize).toBe("125%");
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const t = made.at(-1);
  const cols = {};
  const mk = (field, width) => {
    const col = { width, getField: () => field, getElement: () => document.createElement("div"), getWidth: () => col.width, setWidth: (w) => { col.width = w; }, isVisible: () => true };
    cols[field] = col;
  };
  mk(INDICATOR_FIELD, INDICATOR_WIDTH);
  mk("n", 60);
  mk("name", 300);
  t.getColumn = (f) => cols[f];
  t.getColumns = () => Object.values(cols);
  t.redraw = () => {};
  t.handlers.tableBuilt();
  expect(cols.n.width).toBe(90);
  expect(cols[INDICATOR_FIELD].width).toBe(Math.round(INDICATOR_WIDTH * 1.5));
  expect(cols.name.width).toBe(300); /* Name absorbs; never scaled */
  cols.n.width = 120;
  t.handlers.columnResized(cols.n);
  expect(resized).toEqual([["n", 80]]);
  handle.setScale(1, 1);
  expect(el.style.fontSize).toBe("");
  expect(cols.n.width).toBe(80);
  expect(handle.widths().n).toBe(80);
  handle.setColumnWidth("n", 60);
  handle.setScale(2, 2);
  expect(cols.n.width).toBe(120);
  expect(el.style.fontSize).toBe("200%");
  handle.destroy();
});

test("refit waits for the data load, then redraw(true), re-applies scaled widths, keeps the selection", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  let widthsCalls = 0;
  const before = made.length;
  const handle = createTable(el, {
    select: true,
    columns: [{ field: "n", width: 60, headerSort: false }],
    data: [{ id: "a" }, { id: "b" }],
    onWidths: () => { widthsCalls += 1; },
  });
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const t = made.at(-1);
  const order = [];
  const col = { width: 60, getField: () => "n", getElement: () => document.createElement("div"), getWidth: () => col.width, setWidth: (w) => { col.width = w; order.push(["setWidth", w]); }, isVisible: () => true };
  t.getColumn = (f) => (f === "n" ? col : undefined);
  t.getColumns = () => [col];
  t.redraw = (force) => order.push(["redraw", force]);
  let release;
  t.replaceData = (data) => { t.options.data = data; order.push(["replace"]); return new Promise((r) => { release = r; }); };
  t.handlers.tableBuilt();
  handle.select("b");
  handle.setScale(1.5);
  order.length = 0;
  col.width = 999; /* drawn at a wrong width */
  handle.setData([{ id: "a" }, { id: "b" }, { id: "c" }]);
  const done = handle.refit();
  await Promise.resolve();
  expect(order.some((o) => o[0] === "redraw")).toBe(false); /* waits for the load */
  release();
  await done;
  const redrawAt = order.findIndex((o) => o[0] === "redraw" && o[1] === true);
  expect(redrawAt).toBeGreaterThan(order.findIndex((o) => o[0] === "replace"));
  expect(col.width).toBe(90);
  expect(t.selected).toEqual(["b"]);
  expect(widthsCalls).toBeGreaterThan(0);
  handle.destroy();
});

test("D-UI Tooltips: header cells get the shared tooltip from the title, no native title, across redraws", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const before = made.length;
  const handle = createTable(el, { columns: [{ field: "mail", title: "Mail", headerSort: false }], data: [{ id: "x" }] });
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const t = made.at(-1);
  const mk = (title) => {
    const colEl = document.createElement("div");
    colEl.title = title;
    const inner = document.createElement("span");
    inner.className = "tabulator-col-title";
    inner.title = title;
    colEl.append(inner);
    return { getElement: () => colEl, getDefinition: () => ({ title }), getField: () => title, isVisible: () => true };
  };
  const cols = [mk("Mail"), mk("Account icon"), mk("")];
  t.getColumns = () => cols;
  t.getColumn = () => null;
  t.handlers.tableBuilt();
  t.handlers.renderComplete();
  expect(cols.map((c) => c.getElement().dataset.pavTip)).toEqual(["Mail", "Account icon", undefined]);
  for (const c of cols) {
    expect(c.getElement().querySelectorAll("[title]").length).toBe(0);
    expect(c.getElement().hasAttribute("title")).toBe(false);
  }
  handle.destroy();
});

test("indicatorColumn: onHeader turns the dots into a Choose columns button", () => {
  const seen = [];
  const col = indicatorColumn(() => null, "id", document, (button) => seen.push(button));
  expect(col.title).toBe("Choose columns");
  const button = col.titleFormatter();
  expect(button.tagName).toBe("BUTTON");
  expect(button.getAttribute("aria-label")).toBe("Choose columns");
  button.click();
  expect(seen).toEqual([button]);
  expect(indicatorColumn(() => null).title).toBe("Row indicator");
  expect(indicatorColumn(() => null).titleFormatter().tagName).toBe("I");
});

test("tabulatorOptions strips download keys unless the Download module is on", async () => {
  const { tabulatorOptions } = await import("./table.js");
  const cols = [{ field: "a", download: false, titleDownload: "A", accessorDownload: () => "x" }];
  const off = tabulatorOptions({ columns: cols, indicator: true }, [], { open: false });
  expect(off.columns.every((c) => !("download" in c) && !("titleDownload" in c) && !("accessorDownload" in c))).toBe(true);
  expect(cols[0].download).toBe(false);
  const on = tabulatorOptions({ columns: cols, download: true }, [], { open: false });
  expect(on.columns[0].titleDownload).toBe("A");
});

test("movable columns register MoveColumns, keep the indicator fixed, and report field order", async () => {
  expect(modulesFor({ movable: true, indicator: true, columns: [{ field: "a", headerSort: false }] })).toEqual([
    "FormatModule", "InteractionModule", "ResizeTableModule", "FrozenColumnsModule", "MoveColumnsModule", "ResizeColumnsModule",
  ]);
  expect(modulesFor({ movable: false, columns: [{ field: "a", headerSort: false }] })).not.toContain("MoveColumnsModule");
  const opts = tabulatorOptions({ movable: true, indicator: true, columns: [{ field: "a", resizable: true, headerSort: false }] }, [], { open: false });
  expect(opts.movableColumns).toBe(true);
  expect(opts.columns[0]).toMatchObject({ field: INDICATOR_FIELD, frozen: true, resizable: false });
  expect("movable" in opts.columns[0]).toBe(false);
  expect(tabulatorOptions({ movable: false, columns: [{ field: "a", headerSort: false }] }, [], { open: false }).movableColumns).toBeUndefined();
  expect(tabulatorOptions({ columns: [{ field: "a", headerSort: false }] }, [], { open: false }).movableColumns).toBe(true);
  const el = document.createElement("div");
  const moved = [];
  const before = made.length;
  const handle = createTable(el, {
    movable: true,
    indicator: true,
    columns: [{ field: "a", headerSort: false }, { field: "b", headerSort: false }],
    data: [{ id: "x" }],
    onColumnMoved: (order) => moved.push(order),
  });
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const t = made.at(-1);
  t.handlers.tableBuilt();
  t.handlers.columnMoved(null, [
    { getField: () => INDICATOR_FIELD },
    { getField: () => "b" },
    { getField: () => "a" },
  ]);
  expect(moved).toEqual([["b", "a"]]);
  const calls = [];
  t.getColumn = (field) => ({ move: (to, after) => calls.push([field, to, after]) });
  handle.setColumnOrder(["b", "a"]);
  expect(calls).toEqual([["a", "b", true]]);
  handle.destroy();
});

test("column prep: widthGrow drops its width, lockWidth stays off the wire, and the last movable column grows", () => {
  const prepared = tabulatorOptions({
    columns: [
      { field: "date", headerSort: false, lockWidth: true, resizable: true },
      { field: "subject", width: 80, widthGrow: 2, minWidth: 48, headerSort: false },
    ],
  }, [], { open: false });
  expect(prepared.columnDefaults).toEqual({ resizable: "header" });
  expect(prepared.movableColumns).toBe(true);
  expect(prepared.columns[0].lockWidth).toBeUndefined();
  expect(prepared.columns[0]).toMatchObject({ field: "date", width: 40, resizable: "header" });
  expect(prepared.columns[1].width).toBeUndefined();
  expect(prepared.columns[1]).toMatchObject({ field: "subject", widthGrow: 2, minWidth: 48 });
  const grown = tabulatorOptions({
    columns: [
      { field: "a", headerSort: false },
      { field: "b", width: 80, minWidth: 30, headerSort: false },
      { field: "c", width: 20, resizable: false, headerSort: false },
    ],
  }, [], { open: false });
  expect(grown.columns[0]).toMatchObject({ field: "a", width: 40 });
  expect(grown.columns[0].widthGrow).toBeUndefined();
  expect(grown.columns[1].width).toBeUndefined();
  expect(grown.columns[1]).toMatchObject({ field: "b", widthGrow: 1, minWidth: 30 });
  expect(grown.columns[2]).toMatchObject({ field: "c", width: 20, resizable: false });
  expect(modulesFor({ movable: false, columns: [{ field: "a", headerSort: false }] })).toEqual([
    "FormatModule", "InteractionModule", "ResizeTableModule", "ResizeColumnsModule",
  ]);
});

function headerCell(titleWidth) {
  const colEl = document.createElement("div");
  colEl.style.paddingLeft = "4px";
  colEl.style.paddingRight = "4px";
  const title = document.createElement("span");
  title.className = "tabulator-col-title";
  Object.defineProperty(title, "scrollWidth", { value: titleWidth });
  const sorter = document.createElement("span");
  sorter.className = "tabulator-col-sorter";
  Object.defineProperty(sorter, "offsetWidth", { value: 10 });
  colEl.append(title, sorter);
  return colEl;
}

function textCell(px) {
  const el = document.createElement("div");
  el.style.paddingLeft = "4px";
  el.style.paddingRight = "4px";
  const child = document.createElement("span");
  Object.defineProperty(child, "scrollWidth", { value: px });
  el.append(child);
  return { getElement: () => el };
}

function liveColumn(field, width, source, extra = {}) {
  const column = {
    width,
    def: { ...source },
    getField: () => field,
    getDefinition() { return column.def; },
    getElement: extra.getElement || (() => document.createElement("div")),
    getWidth() { return column.width; },
    setWidth(next) { column.width = next; },
    isVisible: () => extra.visible !== false,
    getCells: extra.getCells || (() => []),
  };
  delete column.def.lockWidth;
  return column;
}

/* A holder of `holderWidth` and one fake column per field, installed before
   tableBuilt so the first layout can fill it. */
async function mountFilled(columns, specs, holderWidth) {
  const el = document.createElement("div");
  document.body.append(el);
  const before = made.length;
  const handle = createTable(el, { columns, data: [{ id: "x" }] });
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const t = made.at(-1);
  const cols = {};
  for (const column of columns) {
    const spec = specs[column.field] || {};
    cols[column.field] = liveColumn(column.field, spec.width, column, spec);
  }
  t.getColumn = (field) => cols[field];
  t.getColumns = () => columns.map((column) => cols[column.field]);
  const holder = document.createElement("div");
  holder.className = "tabulator-tableholder";
  let width = holderWidth;
  Object.defineProperty(holder, "clientWidth", { get: () => width });
  el.append(holder);
  t.handlers.tableBuilt();
  return {
    el,
    t,
    handle,
    cols,
    setHolder(next) { width = next; },
    drag(field, next) {
      const column = cols[field];
      t.handlers.columnResizing(column);
      column.width = next;
      t.handlers.columnWidth(column);
      t.handlers.columnResized(column);
    },
  };
}

test("fixed columns fit their contents once, then only widen, and the grow column takes the rest", async () => {
  const dateHeader = headerCell(50);
  let dateCells = [];
  const view = await mountFilled(
    [
      { field: "date", title: "Date", width: 100, minWidth: 40, headerSort: false },
      { field: "size", title: "Size", width: 90, minWidth: 40, lockWidth: true, headerSort: false },
      { field: "subject", title: "Subject", widthGrow: 1, minWidth: 40, headerSort: false },
    ],
    {
      date: { width: 100, getElement: () => dateHeader, getCells: () => dateCells },
      size: { width: 90, getElement: () => headerCell(20) },
      subject: { width: 50 },
    },
    400,
  );
  try {
    expect(view.cols.date.width).toBe(72);
    expect(view.cols.date.def.width).toBe(72);
    expect(view.cols.size.width).toBe(90);
    expect(view.cols.subject.width).toBe(238);
    expect(view.cols.subject.def.width).toBeUndefined();
    dateCells = [textCell(120)];
    view.t.handlers.renderComplete();
    expect(view.cols.date.width).toBe(130);
    expect(view.cols.subject.width).toBe(180);
    dateCells = [textCell(10)];
    view.t.handlers.renderComplete();
    expect(view.cols.date.width).toBe(130);
    expect(view.cols.subject.width).toBe(180);
  } finally {
    view.handle.destroy();
    view.el.remove();
  }
});

test("resizing a column gives the difference to the widthGrow columns and keeps the holder full", async () => {
  const view = await mountFilled(
    [
      { field: "date", title: "Date", width: 100, minWidth: 40, headerSort: false },
      { field: "subject", title: "Subject", widthGrow: 1, minWidth: 80, headerSort: false },
      { field: "from", title: "From", width: 80, minWidth: 40, headerSort: false },
      { field: "size", title: "Size", width: 60, minWidth: 40, headerSort: false },
    ],
    {
      date: { width: 100 },
      subject: { width: 10 },
      from: { width: 80 },
      size: { width: 60 },
    },
    400,
  );
  try {
    expect(view.cols.subject.width).toBe(160);
    /* Size is not beside Subject. The spare still comes out of Subject. */
    view.drag("size", 90);
    expect(view.cols.size.width).toBe(90);
    expect(view.cols.size.def.width).toBe(90);
    expect(view.cols.from.width).toBe(80);
    expect(view.cols.date.width).toBe(100);
    expect(view.cols.subject.width).toBe(130);
    view.t.handlers.renderComplete();
    expect(view.cols.size.width).toBe(90);
    expect(view.cols.subject.width).toBe(130);
    /* The only grow column moves the fixed column on its other side. */
    view.drag("subject", 150);
    expect(view.cols.from.width).toBe(60);
    expect(view.cols.from.def.width).toBe(60);
    expect(view.cols.subject.width).toBe(150);
    expect(view.cols.date.width).toBe(100);
    expect(view.cols.size.width).toBe(90);
    /* Past the neighbour's minimum, Subject is pulled back so the row still fills. */
    view.drag("subject", 220);
    expect(view.cols.from.width).toBe(40);
    expect(view.cols.subject.width).toBe(170);
    expect(view.cols.date.width + view.cols.subject.width + view.cols.from.width + view.cols.size.width).toBe(400);
  } finally {
    view.handle.destroy();
    view.el.remove();
  }
});

test("several widthGrow columns share the spare by weight, and a dragged one sticks until the pane changes", async () => {
  const view = await mountFilled(
    [
      { field: "from", title: "From", widthGrow: 2, minWidth: 40, headerSort: false },
      { field: "subject", title: "Subject", widthGrow: 3, minWidth: 40, headerSort: false },
      { field: "preview", title: "Preview", widthGrow: 2, minWidth: 40, headerSort: false },
      { field: "time", title: "Time", width: 100, minWidth: 40, headerSort: false },
    ],
    {
      from: { width: 40 },
      subject: { width: 40 },
      preview: { width: 40 },
      time: { width: 100 },
    },
    450,
  );
  try {
    expect(view.cols.from.width).toBe(100);
    expect(view.cols.subject.width).toBe(150);
    expect(view.cols.preview.width).toBe(100);
    expect(view.cols.time.width).toBe(100);
    view.drag("subject", 180);
    expect(view.cols.subject.width).toBe(180);
    expect(view.cols.from.width).toBe(85);
    expect(view.cols.preview.width).toBe(85);
    expect(view.cols.time.width).toBe(100);
    view.setHolder(500);
    view.t.handlers.renderComplete();
    expect(view.cols.from.width).toBe(114);
    expect(view.cols.subject.width).toBe(171);
    expect(view.cols.preview.width).toBe(115);
    expect(view.cols.time.width).toBe(100);
    view.handle.setGrowWeights({ from: 1, subject: 1, preview: 1 });
    expect(view.cols.from.def.widthGrow).toBe(1);
    expect(view.cols.subject.def.widthGrow).toBe(1);
    expect(view.cols.preview.def.widthGrow).toBe(1);
    expect(view.cols.from.def.width).toBeUndefined();
    expect(view.cols.from.width).toBe(133);
    expect(view.cols.subject.width).toBe(133);
    expect(view.cols.preview.width).toBe(134);
    expect(view.cols.time.width).toBe(100);
  } finally {
    view.handle.destroy();
    view.el.remove();
  }
});

test("a widthGrow column stays at its minimum when the fixed columns leave no spare", async () => {
  const view = await mountFilled(
    [
      { field: "date", title: "Date", width: 100, minWidth: 40, headerSort: false },
      { field: "subject", title: "Subject", widthGrow: 1, minWidth: 80, headerSort: false },
      { field: "size", title: "Size", width: 80, minWidth: 40, headerSort: false },
    ],
    {
      date: { width: 100 },
      subject: { width: 40 },
      size: { width: 80 },
    },
    200,
  );
  try {
    expect(view.cols.subject.width).toBe(80);
    expect(view.cols.date.width + view.cols.size.width + view.cols.subject.width).toBe(260);
    view.drag("date", 120);
    expect(view.cols.date.width).toBe(120);
    expect(view.cols.subject.width).toBe(80);
  } finally {
    view.handle.destroy();
    view.el.remove();
  }
});

test("select can leave the scroll alone, and a saved scroll is restored once", async () => {
  const el = document.createElement("div");
  Object.defineProperty(el, "offsetWidth", { configurable: true, value: 300 });
  Object.defineProperty(el, "offsetHeight", { configurable: true, value: 200 });
  const scrolls = [];
  const before = made.length;
  const handle = createTable(el, {
    columns: [{ field: "n", headerSort: false }],
    data: [{ id: "a" }, { id: "b" }, { id: "c" }],
    select: true,
    scroll: { top: 40, left: 8 },
    onScroll: (pos) => scrolls.push(pos),
  });
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const t = made[before];
  expect(t.options.scroll).toBeUndefined();
  const holder = document.createElement("div");
  holder.className = "tabulator-tableholder";
  el.append(holder);
  const jumped = [];
  t.rowManager = {
    renderer: {
      scrollTop: 0,
      scrollRows(top) {
        this.scrollTop = top;
        jumped.push(top);
      },
    },
  };
  handle.select("c", { reveal: false });
  t.handlers.tableBuilt();
  expect(holder.scrollTop).toBe(40);
  expect(holder.scrollLeft).toBe(8);
  expect(jumped).toEqual([40]);
  expect(t.selected).toEqual(["c"]);
  expect(t.calls.filter((call) => call[0] === "scrollToRow")).toEqual([]);
  t.handlers.renderComplete();
  expect(holder.scrollTop).toBe(40);
  /* A redraw's own scroll event is not the user, and it must not replace the save. */
  holder.scrollTop = 5;
  holder.scrollLeft = 1;
  holder.dispatchEvent(new Event("scroll"));
  expect(scrolls).toEqual([]);
  t.handlers.renderComplete();
  expect(holder.scrollTop).toBe(40);
  expect(holder.scrollLeft).toBe(8);
  holder.dispatchEvent(new WheelEvent("wheel", { bubbles: true }));
  holder.scrollTop = 12;
  holder.scrollLeft = 3;
  holder.dispatchEvent(new Event("scroll"));
  expect(scrolls).toContainEqual({ top: 12, left: 3 });
  t.handlers.renderComplete();
  expect(holder.scrollTop).toBe(12);
  handle.select("a");
  expect(t.calls).toContainEqual(["scrollToRow", "a"]);
  handle.destroy();
});

test("a table built with no box redraws once it is shown", async () => {
  let notify = null;
  const RealObserver = globalThis.ResizeObserver;
  globalThis.ResizeObserver = class {
    constructor(fn) {
      notify = fn;
    }
    observe() {}
    disconnect() {}
  };
  try {
    const el = document.createElement("div");
    const before = made.length;
    const handle = createTable(el, {
      columns: [{ field: "n", headerSort: false }],
      data: [{ id: "a" }, { id: "b" }],
      select: true,
      scroll: { top: 40, left: 8 },
    });
    await vi.waitFor(() => expect(made.length).toBe(before + 1));
    const t = made[before];
    const redraws = [];
    const holder = document.createElement("div");
    holder.className = "tabulator-tableholder";
    el.append(holder);
    /* Tabulator's redraw sets the holder to the top, including after the
       render that would otherwise restore the saved position. */
    t.redraw = (force) => {
      redraws.push(force);
      holder.scrollTop = 0;
      holder.scrollLeft = 0;
      if (t.handlers.renderComplete) {
        t.handlers.renderComplete();
      }
      holder.scrollTop = 0;
      holder.scrollLeft = 0;
    };
    handle.select("b", { reveal: false });
    t.handlers.tableBuilt();
    expect(redraws).toEqual([]);
    expect(holder.scrollTop).toBe(0);
    Object.defineProperty(el, "offsetWidth", { configurable: true, value: 300 });
    Object.defineProperty(el, "offsetHeight", { configurable: true, value: 120 });
    notify();
    expect(redraws).toEqual([true]);
    expect(holder.scrollTop).toBe(40);
    expect(holder.scrollLeft).toBe(8);
    expect(t.selected).toEqual(["b"]);
    holder.scrollTop = 0;
    holder.scrollLeft = 0;
    t.handlers.renderComplete();
    expect(holder.scrollTop).toBe(40);
    expect(holder.scrollLeft).toBe(8);
    notify();
    expect(redraws).toEqual([true]);
    handle.destroy();
  } finally {
    globalThis.ResizeObserver = RealObserver;
  }
});
