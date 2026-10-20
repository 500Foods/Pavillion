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

import { createTable } from "./table.js";

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
  handle.destroy();
  expect(t.destroyed).toBe(true);
});
