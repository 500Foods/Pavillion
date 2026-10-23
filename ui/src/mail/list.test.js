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
      this.options.data = data;
      return Promise.resolve();
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

import { createMailList, formatListCount } from "./list.js";

function fakeApi(replies) {
  const calls = [];
  const listeners = new Set();
  let n = 0;
  return {
    calls,
    message: {
      list(body) {
        calls.push(body);
        const reply = replies[Math.min(n, replies.length - 1)];
        n += 1;
        return Promise.resolve(reply);
      },
    },
    on(name, fn) {
      listeners.add({ name, fn });
      return () => {
        for (const item of listeners) {
          if (item.fn === fn) {
            listeners.delete(item);
          }
        }
      };
    },
    emit(name, payload) {
      for (const item of [...listeners]) {
        if (item.name === name) {
          item.fn(payload);
        }
      }
    },
  };
}

const first = {
  accountId: "foods",
  total: 5,
  messages: [
    { id: "a", from: "<img src=x onerror=alert(1)>", subject: "<b>Hi</b>" },
    { id: "b", from: "Ann", subject: "Hello" },
  ],
};

const second = {
  accountId: "foods",
  total: 12000,
  messages: [{ id: "c", from: "Bea", subject: "Next" }],
};

test("formatListCount groups digits and says how many of the total are listed", () => {
  expect(formatListCount(3, 3)).toBe("3");
  expect(formatListCount(0, 0)).toBe("0");
  expect(formatListCount(1000, 1000)).toBe("1,000");
  expect(formatListCount(2, 5)).toBe("2 of 5");
  expect(formatListCount(1000, 12345)).toBe("1,000 of 12,345");
  expect(formatListCount(1.5, 2)).toBe("");
});

test("the mail list shows From and Subject as text and reloads for that account", async () => {
  const pane = document.createElement("div");
  document.body.append(pane);
  const api = fakeApi([first, second, first]);
  const list = createMailList(api, pane);
  expect(pane.querySelector(".mail-list-count").textContent).toBe("Select a folder.");

  await list.open("mbx:foods/a1:in");
  await vi.waitFor(() => expect(made.length).toBe(1));
  const table = made[0];
  expect(table.options.height).toBe("100%");
  expect(table.options.renderVertical).toBe("virtual");
  expect(table.options.selectableRows).toBeUndefined();
  expect(table.handlers.rowClick).toBeUndefined();
  expect(table.options.columns.map((column) => column.title)).toEqual(["From", "Subject"]);
  expect(table.options.columns.every((column) => column.headerSort === false)).toBe(true);
  expect(table.options.data).toEqual(first.messages);
  expect(pane.querySelector(".mail-list-count").textContent).toBe("2 of 5");

  const from = table.options.columns[0].formatter({ getValue: () => first.messages[0].from });
  const subject = table.options.columns[1].formatter({ getValue: () => first.messages[0].subject });
  expect(from.textContent).toBe("<img src=x onerror=alert(1)>");
  expect(from.querySelector("img")).toBeNull();
  expect(from.childNodes.length).toBe(1);
  expect(from.childNodes[0].nodeType).toBe(Node.TEXT_NODE);
  expect(subject.textContent).toBe("<b>Hi</b>");
  expect(subject.querySelector("b")).toBeNull();

  pane.querySelector(".mail-list-table").click();
  expect(api.calls).toHaveLength(1);
  expect(api.calls[0]).toEqual({ view: { kind: "mailbox", id: "mbx:foods/a1:in" } });

  table.handlers.tableBuilt();
  api.emit("mail.messages_changed", { accountId: "courses", created: ["x"], updated: [], destroyed: [] });
  await Promise.resolve();
  expect(api.calls).toHaveLength(1);

  api.emit("mail.messages_changed", { accountId: "foods", created: ["c"], updated: [], destroyed: [] });
  await vi.waitFor(() => expect(api.calls).toHaveLength(2));
  expect(table.replaced.at(-1)).toEqual(second.messages);
  expect(pane.querySelector(".mail-list-count").textContent).toBe("1 of 12,000");

  await list.open("");
  expect(pane.querySelector(".mail-list-count").textContent).toBe("Select a folder.");
  expect(table.replaced.at(-1)).toEqual([]);

  api.emit("mail.messages_changed", { accountId: "foods", created: [], updated: ["a"], destroyed: [] });
  await Promise.resolve();
  expect(api.calls).toHaveLength(2);

  list.stop();
  expect(table.destroyed).toBe(true);
  pane.remove();
});
