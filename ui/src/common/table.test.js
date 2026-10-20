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
      return this.options.data.find((r) => r.id === id) ? { id } : false;
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
    TabulatorFull: "full",
  };
});

import { createTable, modulesFor, sortIcon } from "./table.js";
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
    "ColumnCalcsModule", "SelectRowModule", "ExportModule", "DownloadModule",
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
  expect(t.options.groupToggleElement).toBe(false);
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
    expect(column.width).toBeLessThanOrEqual(104);
    expect(column.resizable).toBe(false);
  }
});
