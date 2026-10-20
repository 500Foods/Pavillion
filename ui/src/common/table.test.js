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
      made.push(this);
    }
    on(name, fn) {
      this.handlers[name] = fn;
    }
    redraw() {
      this.redraws = (this.redraws || 0) + 1;
    }
    replaceData(data) {
      this.replaced.push(data);
      return Promise.resolve();
    }
    destroy() {
      this.destroyed = true;
    }
  }
  return { Tabulator, FormatModule: "format", InteractionModule: "interaction" };
});

/* A ResizeObserver stand-in the test can fire. */
const observers = [];
globalThis.ResizeObserver = class {
  constructor(cb) {
    this.cb = cb;
    this.targets = [];
    observers.push(this);
  }
  observe(el) {
    this.targets.push(el);
  }
  disconnect() {
    this.disconnected = true;
  }
};

import { createTable } from "./table.js";
import { accountColumns } from "../settings/accounts-table.js";

test("the shared table loads Tabulator with only the modules it needs and wires row clicks", async () => {
  const el = document.createElement("div");
  const clicks = [];
  const handle = createTable(el, { columns: [{ field: "a" }], data: [{ id: "x" }], onRowClick: (row) => clicks.push(row.id) });
  expect(el.classList.contains("pav-table")).toBe(true);
  await vi.waitFor(() => expect(made.length).toBe(1));
  const t = made[0];
  const { Tabulator } = await import("tabulator-tables");
  expect(Tabulator.registered).toEqual(["format", "interaction"]);
  expect(t.options.layout).toBe("fitColumns");
  t.handlers.tableBuilt();
  handle.setData([{ id: "y" }]);
  expect(t.replaced.at(-1)).toEqual([{ id: "y" }]);
  t.handlers.rowClick(new Event("click"), { getData: () => ({ id: "y" }) });
  expect(clicks).toEqual(["y"]);
  const observer = observers.at(-1);
  expect(observer.targets).toEqual([el]);
  observer.cb([]);
  expect(t.redraws).toBe(1);
  handle.destroy();
  expect(t.destroyed).toBe(true);
  expect(observer.disconnected).toBe(true);
});

test("the accounts table fills its width with Name and keeps the other columns narrow", () => {
  const columns = accountColumns(document);
  const name = columns.find((c) => c.field === "name");
  expect(name.widthGrow).toBe(1);
  expect(name.width).toBeUndefined();
  for (const column of columns.filter((c) => c.field !== "name")) {
    expect(column.width).toBeLessThanOrEqual(96);
    expect(column.resizable).toBe(false);
  }
});
