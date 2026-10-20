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
    TabulatorFull: "full",
  };
});

import { createTable, indicatorColumn, INDICATOR_FIELD, INDICATOR_WIDTH, modulesFor, sortIcon } from "./table.js";
import { accountColumns } from "../settings/accounts-table.js";

test("a plain table registers only the base modules, fills its element, and loads data once", async () => {
  const el = document.createElement("div");
  const clicks = [];
  const data = [{ id: "x" }];
  const handle = createTable(el, { columns: [{ field: "a", headerSort: false }], data, onRowClick: (row) => clicks.push(row.id) });
  expect(el.classList.contains("pav-table")).toBe(true);
  await vi.waitFor(() => expect(made.length).toBe(1));
  const t = made[0];
  const { Tabulator } = await import("tabulator-tables");
  expect(Tabulator.registered).toEqual(["format", "interaction", "resizeTable"]);
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
    "ColumnCalcsModule", "SelectRowModule", "ExportModule", "DownloadModule", "ResizeColumnsModule",
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

test("resize round: columns default unresizable, indicator divider, resize events, fitted titles, widths", async () => {
  const { tabulatorOptions, indicatorColumn } = await import("./table.js");
  expect(tabulatorOptions({ columns: [{ field: "a", headerSort: false }] }, [], { open: false }).columnDefaults).toEqual({ resizable: false });
  expect(indicatorColumn(() => null).cssClass).toBe("pav-table-indicator pav-col-divider-right");
  expect(modulesFor({ columns: [{ field: "a", resizable: true }] })).toContain("ResizeColumnsModule");
  expect(modulesFor({ columns: [{ field: "a" }] })).not.toContain("ResizeColumnsModule");
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
