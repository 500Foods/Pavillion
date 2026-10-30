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
    getColumns() {
      return this.liveColumns || [];
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
    MoveColumnsModule: mod("moveColumns"),
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
  expect(column(table, "swatch")).toMatchObject({ width: 10, minWidth: 10, resizable: false });
  expect(column(table, "from")).toMatchObject({ widthGrow: 2, minWidth: 96 });
  expect(column(table, "subject")).toMatchObject({ widthGrow: 3, minWidth: 120 });
  expect(column(table, "preview")).toMatchObject({ widthGrow: 2, minWidth: 80 });
  expect(column(table, "from").width).toBeUndefined();
  expect(column(table, "subject").width).toBeUndefined();
  expect(column(table, "preview").width).toBeUndefined();
  expect(column(table, "receivedAt")).toMatchObject({ width: 108, minWidth: 72 });
  expect(column(table, "flags")).toMatchObject({ width: 84, minWidth: 72 });
  expect(column(table, "hasAttachment")).toMatchObject({ width: 36, minWidth: 36 });
  expect(table.options.layout).toBe("fitColumns");
  expect(table.options.movableColumns).toBe(true);
  expect(table.options.columnDefaults.resizable).toBe("header");
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

test("mail list zoom scales the table and leaves the pane alone", async () => {
  const pane = document.createElement("div");
  pane.className = "pane pane-list";
  document.body.append(pane);
  const list = createMailList(fakeApi([first]), pane);
  const before = made.length;
  list.setZoom(1.8, 1.5);
  await list.open("mbx:foods/a1:in");
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  list.setZoom(1.8, 1.5);
  expect(pane.style.zoom).toBe("");
  expect(pane.querySelector(".mail-list-table").style.fontSize).toBe("150%");
  list.stop();
  pane.remove();
});

function sizedColumn(field, width, grow) {
  return {
    getField: () => field,
    getWidth: () => width,
    isVisible: () => true,
    getElement: () => null,
    getDefinition: () => (grow ? { field, widthGrow: grow } : { field, width }),
  };
}

test("saved From, Subject, and preview widths come back as grow weights", async () => {
  const pane = document.createElement("div");
  document.body.append(pane);
  const saved = [];
  let stored = { from: 200, subject: 300, preview: 100, receivedAt: 140 };
  const list = createMailList(fakeApi([first]), pane, {
    tableWidths: () => stored,
    saveTableWidths(id, widths) {
      saved.push([id, widths]);
      stored = widths;
    },
    mailboxOf(id) {
      return id.endsWith(":out") ? { role: "sent" } : { role: "inbox" };
    },
  });
  const before = made.length;
  await list.open("mbx:foods/a1:in");
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const table = made[before];
  expect(column(table, "from").widthGrow).toBe(200);
  expect(column(table, "subject").widthGrow).toBe(300);
  expect(column(table, "preview").widthGrow).toBe(100);
  expect(column(table, "from").width).toBeUndefined();
  expect(column(table, "receivedAt")).toMatchObject({ width: 140, minWidth: 72 });
  table.liveColumns = [
    sizedColumn("from", 180, 200),
    sizedColumn("subject", 340, 300),
    sizedColumn("preview", 90, 100),
  ];
  table.handlers.tableBuilt();
  table.handlers.columnResized(sizedColumn("subject", 340, 300));
  expect(saved.at(-1)).toEqual(["mail", { receivedAt: 140, from: 180, subject: 340, preview: 90 }]);

  table.handlers.columnResized(sizedColumn("receivedAt", 150, 0));
  expect(saved.at(-1)).toEqual(["mail", { from: 180, subject: 340, preview: 90, receivedAt: 150 }]);

  await list.open("mbx:foods/a1:out");
  await vi.waitFor(() => expect(made.length).toBe(before + 2));
  expect(column(made[before + 1], "to").widthGrow).toBe(180);
  expect(column(made[before + 1], "to").width).toBeUndefined();
  made[before + 1].liveColumns = [
    sizedColumn("to", 160, 180),
    sizedColumn("subject", 300, 340),
    sizedColumn("preview", 110, 90),
  ];
  made[before + 1].handlers.tableBuilt();
  made[before + 1].handlers.columnResized(sizedColumn("to", 160, 180));
  expect(saved.at(-1)).toEqual(["mail", { receivedAt: 150, from: 160, subject: 300, preview: 110 }]);
  list.stop();
  pane.remove();
});

test("a partial grow width keeps the coded From, Subject, and preview weights", async () => {
  const pane = document.createElement("div");
  document.body.append(pane);
  const list = createMailList(fakeApi([first]), pane, {
    tableWidths: () => ({ subject: 300, from: 5000 }),
  });
  const before = made.length;
  await list.open("mbx:foods/a1:in");
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const table = made.at(-1);
  expect(column(table, "from").widthGrow).toBe(2);
  expect(column(table, "subject").widthGrow).toBe(3);
  expect(column(table, "preview").widthGrow).toBe(2);
  list.stop();
  pane.remove();
});

test("opening a folder again selects that folder's message and scroll", async () => {
  const pane = document.createElement("div");
  document.body.append(pane);
  let place = { mailbox: "mbx:foods/a1:in", selected: "b", top: 20, left: 4 };
  const other = {
    accountId: "foods",
    total: 1,
    messages: [{ id: "b", from: "Bea", subject: "Other" }],
  };
  const list = createMailList(fakeApi([first, other]), pane, {
    tablePlace: () => place,
    saveTablePlace(_id, next) {
      place = next;
    },
    mailboxOf() {
      return { role: "inbox" };
    },
  });
  const before = made.length;
  await list.open("mbx:foods/a1:in");
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const table = made[before];
  expect(table.options.scroll).toBeUndefined();
  const holder = document.createElement("div");
  holder.className = "tabulator-tableholder";
  table.el.append(holder);
  Object.defineProperty(table.el, "offsetWidth", { configurable: true, value: 400 });
  Object.defineProperty(table.el, "offsetHeight", { configurable: true, value: 300 });
  table.handlers.tableBuilt();
  expect(table.picked).toBe("b");
  expect(holder.scrollTop).toBe(20);
  expect(holder.scrollLeft).toBe(4);

  await list.open("mbx:foods/a1:else");
  expect(table.picked).toBeNull();
  expect(holder.scrollTop).toBe(0);
  expect(place.mailbox).toBe("mbx:foods/a1:in");
  expect(place.selected).toBe("b");
  list.stop();
  pane.remove();
});

test("page, home, and end move the selected message", async () => {
  const root = document.createElement("div");
  const pane = document.createElement("div");
  root.append(pane);
  document.body.append(root);
  const keys = createKeys(root);
  const messages = [
    { id: "a", from: "A", subject: "A" },
    { id: "b", from: "B", subject: "B" },
    { id: "c", from: "C", subject: "C" },
    { id: "d", from: "D", subject: "D" },
  ];
  const list = createMailList(fakeApi([{ accountId: "foods", total: 4, messages }]), pane, {
    keys,
    mailboxOf() {
      return { role: "inbox" };
    },
  });
  const before = made.length;
  await list.open("mbx:foods/a1:in");
  await vi.waitFor(() => expect(made.length).toBe(before + 1));
  const table = made[before];
  const host = pane.querySelector(".mail-list-table");
  const row = document.createElement("div");
  row.className = "tabulator-row";
  row.getBoundingClientRect = () => ({ height: 20, width: 10, top: 0, left: 0, bottom: 20, right: 10, x: 0, y: 0 });
  const holder = document.createElement("div");
  holder.className = "tabulator-tableholder";
  Object.defineProperty(holder, "clientHeight", { value: 50 });
  host.append(row, holder);
  table.handlers.tableBuilt();
  host.tabIndex = 0;
  host.focus();
  const press = (key) => {
    document.dispatchEvent(new KeyboardEvent("keydown", { key, bubbles: true }));
  };
  press("End");
  expect(table.picked).toBe("d");
  press("Home");
  expect(table.picked).toBe("a");
  press("PageDown");
  expect(table.picked).toBe("c");
  press("PageUp");
  expect(table.picked).toBe("a");
  list.stop();
  keys.destroy();
  root.remove();
});
