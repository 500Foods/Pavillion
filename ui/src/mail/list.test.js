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
    deselectRow() {
      this.picked = null;
    }
    selectRow(id) {
      this.picked = id;
    }
    getRow(id) {
      const rows = this.options.data || [];
      return rows.some((row) => row.id === id) ? { id } : null;
    }
    getRows() {
      return (this.options.data || []).map((row) => ({ getIndex: () => row.id }));
    }
    scrollToRow() {
      return Promise.resolve();
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

import { createKeys } from "../common/keys.js";
import { formatInstant } from "../formats.js";
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
  messages: [
    { id: "a", from: "Ann", subject: "Still", seen: false },
    { id: "c", from: "Bea", subject: "Next", seen: true },
  ],
};

test("formatListCount groups digits and says how many of the total are listed", () => {
  expect(formatListCount(3, 3)).toBe("3");
  expect(formatListCount(0, 0)).toBe("0");
  expect(formatListCount(1000, 1000)).toBe("1,000");
  expect(formatListCount(2, 5)).toBe("2 of 5");
  expect(formatListCount(1000, 12345)).toBe("1,000 of 12,345");
  expect(formatListCount(1.5, 2)).toBe("");
});

function column(table, field) {
  return table.options.columns.find((item) => item.field === field);
}

function cell(data, field) {
  return {
    getValue: () => data[field],
    getRow: () => ({ getData: () => data }),
  };
}

test("the mail list shows the folder columns as text and reloads for that account", async () => {
  const root = document.createElement("div");
  const pane = document.createElement("div");
  root.append(pane);
  document.body.append(root);
  const keys = createKeys(root);
  const api = fakeApi([first, second, first]);
  const list = createMailList(api, pane, {
    keys,
    mailboxOf(id) {
      if (id === "mbx:foods/a1:out") {
        return { role: "sent", swatch: "#336699" };
      }
      return { role: "inbox", swatch: "red" };
    },
  });
  expect(pane.querySelector(".mail-list-count").textContent).toBe("Select a folder.");

  await list.open("mbx:foods/a1:in");
  await vi.waitFor(() => expect(made.length).toBe(1));
  const table = made[0];
  expect(table.options.height).toBe("100%");
  expect(table.options.renderVertical).toBe("virtual");
  expect(table.options.pagination).toBeUndefined();
  expect(table.options.selectableRows).toBe(1);
  expect(typeof table.handlers.rowClick).toBe("function");
  expect(table.options.columns.map((item) => item.field)).toEqual([
    "swatch",
    "from",
    "subject",
    "preview",
    "receivedAt",
    "flags",
    "hasAttachment",
  ]);
  expect(column(table, "from").title).toBe("From");
  expect(table.options.columns.every((item) => item.headerSort === false)).toBe(true);
  expect(table.options.data).toEqual(first.messages);
  expect(pane.querySelector(".mail-list-count").textContent).toBe("2 of 5");

  const sample = {
    id: "a",
    from: "<img src=x onerror=alert(1)>",
    to: "<b>Pat</b>",
    subject: "<b>Hi</b>",
    preview: "<script>x</script>",
    receivedAt: Date.parse("2020-01-15T20:00:00.000Z"),
    seen: false,
    flagged: true,
    answered: true,
    forwarded: true,
    hasAttachment: true,
  };
  const from = column(table, "from").formatter(cell(sample, "from"));
  const subject = column(table, "subject").formatter(cell(sample, "subject"));
  const preview = column(table, "preview").formatter(cell(sample, "preview"));
  const when = column(table, "receivedAt").formatter(cell(sample, "receivedAt"));
  const flags = column(table, "flags").formatter(cell(sample, "flags"));
  const file = column(table, "hasAttachment").formatter(cell(sample, "hasAttachment"));
  const bar = column(table, "swatch").formatter(cell(sample, "swatch"));
  expect(from.textContent).toBe("<img src=x onerror=alert(1)>");
  expect(from.querySelector("img")).toBeNull();
  expect(from.childNodes.length).toBe(1);
  expect(from.childNodes[0].nodeType).toBe(Node.TEXT_NODE);
  expect(subject.textContent).toBe("<b>Hi</b>");
  expect(subject.querySelector("b")).toBeNull();
  expect(preview.textContent).toBe("<script>x</script>");
  expect(preview.querySelector("script")).toBeNull();
  expect(when.textContent).toBe("2020-01-15");
  expect(when.textContent).toBe(formatInstant(sample.receivedAt, "dateShort"));
  expect(flags.querySelector(".mail-unread")).not.toBeNull();
  expect(flags.querySelector(".mail-flag")).not.toBeNull();
  expect(flags.querySelector(".mail-answered")).not.toBeNull();
  expect(flags.querySelector(".mail-forwarded")).not.toBeNull();
  expect(flags.querySelector("img, b, script")).toBeNull();
  expect(file.classList.contains("mail-file")).toBe(true);
  expect(bar.style.background).toBe("");

  pane.querySelector(".mail-list-table").click();
  expect(api.calls).toHaveLength(1);
  expect(api.calls[0]).toEqual({ view: { kind: "mailbox", id: "mbx:foods/a1:in" } });

  table.handlers.tableBuilt();
  table.handlers.rowClick(null, { getData: () => first.messages[0] });
  expect(table.picked).toBe("a");

  const host = pane.querySelector(".mail-list-table");
  host.focus();
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "j", bubbles: true }));
  expect(table.picked).toBe("b");
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "k", bubbles: true }));
  expect(table.picked).toBe("a");
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true }));
  expect(table.picked).toBe("a");
  expect(api.calls).toHaveLength(1);

  api.emit("mail.messages_changed", { accountId: "courses", created: ["x"], updated: [], destroyed: [] });
  await Promise.resolve();
  expect(api.calls).toHaveLength(1);

  api.emit("mail.messages_changed", { accountId: "foods", created: ["c"], updated: [], destroyed: [] });
  await vi.waitFor(() => expect(api.calls).toHaveLength(2));
  expect(table.replaced.at(-1)).toEqual(second.messages);
  expect(pane.querySelector(".mail-list-count").textContent).toBe("2 of 12,000");
  expect(table.picked).toBe("a");

  await list.open("mbx:foods/a1:out");
  await vi.waitFor(() => expect(made.length).toBe(2));
  expect(column(made[1], "to").title).toBe("To");
  const sentBar = column(made[1], "swatch").formatter(cell(sample, "swatch"));
  expect(sentBar.style.backgroundColor).toBe("rgb(51, 102, 153)");
  made[1].handlers.tableBuilt();

  await list.open("");
  expect(pane.querySelector(".mail-list-count").textContent).toBe("Select a folder.");
  expect(made[1].replaced.at(-1)).toEqual([]);

  api.emit("mail.messages_changed", { accountId: "foods", created: [], updated: ["a"], destroyed: [] });
  await Promise.resolve();
  expect(api.calls).toHaveLength(3);

  list.stop();
  expect(made[1].destroyed).toBe(true);
  keys.destroy();
  root.remove();
});
