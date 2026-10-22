/**
 * @vitest-environment jsdom
 */
import { expect, test, vi } from "vitest";
import { createApi } from "../api.js";
import { createMailNav } from "./nav.js";

function transport() {
  const listeners = new Set();
  const calls = [];
  let accounts = [{ id: "foods", number: "001", name: "Foods" }, { id: "courses", number: "002", name: "Courses" }];
  let nodes = [
    { id: "foods", parentId: null, kind: "account", label: "Foods", role: null, order: 0, unread: 4, total: 10, hasChildren: true, swatch: "#2a6" },
    { id: "mbx:foods:in", parentId: "foods", kind: "mailbox", label: "Inbox", role: "inbox", order: 0, unread: 3, total: 8, hasChildren: false, swatch: null },
    { id: "mbx:foods:later", parentId: "foods", kind: "mailbox", label: "Later", role: null, order: 1, unread: 1, total: 2, hasChildren: false, swatch: null },
    { id: "courses", parentId: null, kind: "account", label: "Courses", role: null, order: 1, unread: 0, total: 1, hasChildren: true, swatch: "#48c" },
    { id: "mbx:courses:in", parentId: "courses", kind: "mailbox", label: "Inbox", role: "inbox", order: 0, unread: 0, total: 1, hasChildren: false, swatch: null },
  ];
  return {
    calls,
    setNodes(next) {
      nodes = next;
    },
    setAccounts(next) {
      accounts = next;
    },
    call(cmd, args) {
      calls.push({ cmd, args });
      if (cmd === "account.list") {
        return Promise.resolve({ accounts });
      }
      if (cmd === "mailbox.list") {
        const accountId = args && args.accountId;
        const list = accountId ? nodes.filter((node) => node.id === accountId || node.parentId === accountId || (typeof node.parentId === "string" && node.parentId.startsWith(accountId))) : nodes;
        return Promise.resolve({ nodes: list });
      }
      if (cmd === "log.client") {
        return Promise.resolve({});
      }
      return Promise.reject({ code: "bad_args" });
    },
    on(handler) {
      listeners.add(handler);
      return () => listeners.delete(handler);
    },
    emit(name, payload) {
      for (const handler of listeners) {
        handler(name, payload);
      }
    },
  };
}

async function settled() {
  for (let i = 0; i < 8; i += 1) {
    await Promise.resolve();
  }
}

function row(container, id) {
  return container.querySelector(`[data-node-id="${id}"][role="treeitem"]`);
}

test("loads account roots with numbers and role icons", async () => {
  const el = document.createElement("div");
  const raw = transport();
  const api = createApi(raw);
  const nav = createMailNav(api, el);
  await settled();

  const foods = row(el, "foods");
  const inbox = row(el, "mbx:foods:in");
  const later = row(el, "mbx:foods:later");
  expect(foods.querySelector(".tree-row-text").textContent).toBe("001 Foods");
  expect(foods.querySelector(".tree-row-badge-unread").textContent).toBe("4");
  expect(foods.querySelector(".tree-row-swatch-color").style.backgroundColor).not.toBe("");
  expect(inbox.querySelector("i:not(.tree-row-disclosure)").className).toContain("fa-inbox");
  expect(later.querySelector("i:not(.tree-row-disclosure)").className).toContain("fa-folder");
  expect(inbox.closest("li").parentElement.parentElement.getAttribute("data-node-id")).toBe("foods");
  expect(raw.calls.some((call) => call.cmd === "mailbox.list" && Object.keys(call.args).length === 0)).toBe(true);

  nav.stop();
});

test("a count change patches the row in place and leaves the other account", async () => {
  const el = document.createElement("div");
  const raw = transport();
  const api = createApi(raw);
  const nav = createMailNav(api, el);
  await settled();

  const inbox = row(el, "mbx:foods:in");
  const courses = row(el, "courses");
  raw.setNodes([
    { id: "foods", parentId: null, kind: "account", label: "Foods", role: null, order: 0, unread: 1, total: 10, hasChildren: true, swatch: "#2a6" },
    { id: "mbx:foods:in", parentId: "foods", kind: "mailbox", label: "Inbox", role: "inbox", order: 0, unread: 0, total: 8, hasChildren: false, swatch: null },
    { id: "mbx:foods:later", parentId: "foods", kind: "mailbox", label: "Later", role: null, order: 1, unread: 1, total: 2, hasChildren: false, swatch: null },
  ]);
  raw.emit("mail.mailboxes_changed", { accountId: "foods" });
  await settled();

  expect(row(el, "mbx:foods:in")).toBe(inbox);
  expect(inbox.querySelector(".tree-row-badge-unread")).toBeNull();
  expect(inbox.classList.contains("tree-row-unread")).toBe(false);
  expect(row(el, "courses")).toBe(courses);
  expect(courses.querySelector(".tree-row-text").textContent).toBe("002 Courses");

  nav.stop();
});

test("a new mailbox is inserted and a removed one goes away", async () => {
  const el = document.createElement("div");
  const raw = transport();
  const api = createApi(raw);
  const nav = createMailNav(api, el);
  await settled();

  raw.setNodes([
    { id: "foods", parentId: null, kind: "account", label: "Foods", role: null, order: 0, unread: 3, total: 9, hasChildren: true, swatch: "#2a6" },
    { id: "mbx:foods:in", parentId: "foods", kind: "mailbox", label: "Inbox", role: "inbox", order: 0, unread: 3, total: 8, hasChildren: false, swatch: null },
    { id: "mbx:foods:sent", parentId: "foods", kind: "mailbox", label: "Sent", role: "sent", order: 1, unread: 0, total: 1, hasChildren: false, swatch: null },
    { id: "courses", parentId: null, kind: "account", label: "Courses", role: null, order: 1, unread: 0, total: 1, hasChildren: true, swatch: "#48c" },
    { id: "mbx:courses:in", parentId: "courses", kind: "mailbox", label: "Inbox", role: "inbox", order: 0, unread: 0, total: 1, hasChildren: false, swatch: null },
  ]);
  raw.emit("mail.mailboxes_changed", { accountId: "foods" });
  await settled();

  expect(row(el, "mbx:foods:later")).toBeNull();
  expect(row(el, "mbx:foods:sent").querySelector("i:not(.tree-row-disclosure)").className).toContain("fa-paper-plane");
  expect(row(el, "courses")).not.toBeNull();

  nav.stop();
});

test("sync.state marks the account root and keeps the mark across a later list", async () => {
  const el = document.createElement("div");
  const raw = transport();
  const api = createApi(raw);
  const nav = createMailNav(api, el);
  await settled();

  const foods = row(el, "foods");
  raw.emit("sync.state", { accountId: "foods", state: "error:auth", detail: "" });
  await settled();

  expect(row(el, "foods")).toBe(foods);
  expect(foods.classList.contains("tree-row-error")).toBe(true);
  expect(foods.querySelector("[data-region='status'] i").getAttribute("data-pav-tip")).toBe("error:auth");
  expect(row(el, "mbx:foods:in").querySelector("[data-region='status']")).toBeNull();

  raw.emit("mail.mailboxes_changed", { accountId: "foods" });
  await settled();
  expect(row(el, "foods").classList.contains("tree-row-error")).toBe(true);

  raw.emit("sync.state", { accountId: "foods", state: "idle", detail: "" });
  await settled();
  expect(row(el, "foods").querySelector("[data-region='status']")).toBeNull();
  expect(row(el, "foods").classList.contains("tree-row-error")).toBe(false);

  nav.stop();
});

test("a list failure is a warning and leaves the tree", async () => {
  const el = document.createElement("div");
  const raw = transport();
  const api = createApi(raw);
  const nav = createMailNav(api, el);
  await settled();
  const foods = row(el, "foods");

  const previous = raw.call.bind(raw);
  raw.call = (cmd, args) => {
    if (cmd === "mailbox.list") {
      return Promise.reject({ code: "internal" });
    }
    return previous(cmd, args);
  };
  raw.emit("mail.mailboxes_changed", { accountId: "foods" });
  await settled();

  expect(row(el, "foods")).toBe(foods);
  const messages = raw.calls.filter((call) => call.cmd === "log.client").map((call) => call.args.message);
  expect(messages).toContain("mailbox tree failed: internal");

  nav.stop();
});

test("activity progress fills the pie and updates the tooltip on a period", async () => {
  const now = vi.spyOn(Date, "now");
  now.mockReturnValue(1_000_000);
  const el = document.createElement("div");
  const raw = transport();
  const api = createApi(raw);
  const nav = createMailNav(api, el);
  await settled();

  raw.emit("sync.state", { accountId: "foods", state: "syncing" });
  await settled();
  const pie = row(el, "foods").querySelector(".tree-sync-pie");
  expect(pie.getAttribute("data-pav-tip")).toBe("Syncing");
  expect(pie.style.getPropertyValue("--pav-sync-turn")).toBe("0%");

  raw.emit("activity.changed", {
    activity: { id: "1", kind: "sync.mail", accountId: "foods", state: "running", progress: { done: 0, total: null, unit: "messages" } },
  });
  await settled();
  expect(pie.getAttribute("data-pav-tip")).toBe("Syncing");

  now.mockReturnValue(1_000_200);
  raw.emit("activity.changed", {
    activity: { id: "1", kind: "sync.mail", accountId: "foods", state: "running", progress: { done: 2, total: 8, unit: "messages" } },
  });
  await settled();
  expect(pie.style.getPropertyValue("--pav-sync-turn")).toBe("25%");
  expect(pie.getAttribute("data-pav-tip")).toBe("25%");

  now.mockReturnValue(1_000_400);
  raw.emit("activity.changed", {
    activity: { id: "1", kind: "sync.mail", accountId: "foods", state: "running", progress: { done: 4, total: 8, unit: "messages" } },
  });
  await settled();
  expect(pie.style.getPropertyValue("--pav-sync-turn")).toBe("50%");
  expect(pie.getAttribute("data-pav-tip")).toBe("25%");

  now.mockReturnValue(1_001_500);
  raw.emit("activity.changed", {
    activity: { id: "1", kind: "sync.mail", accountId: "foods", state: "running", progress: { done: 6, total: 8, unit: "messages" } },
  });
  await settled();
  expect(pie.style.getPropertyValue("--pav-sync-turn")).toBe("75%");
  expect(pie.getAttribute("data-pav-tip")).toBe("75%");

  raw.emit("mail.mailboxes_changed", { accountId: "foods" });
  await settled();
  const kept = row(el, "foods").querySelector(".tree-sync-pie");
  expect(kept.getAttribute("data-pav-tip")).toBe("75%");
  expect(kept.style.getPropertyValue("--pav-sync-turn")).toBe("75%");

  raw.emit("sync.state", { accountId: "foods", state: "idle" });
  await settled();
  expect(row(el, "foods").querySelector(".tree-sync-pie")).toBeNull();
  expect(row(el, "foods").getAttribute("aria-busy")).toBeNull();

  nav.stop();
  now.mockRestore();
});

function debugMarks(raw) {
  return raw.calls
    .filter((call) => call.cmd === "log.client" && call.args.level === "debug")
    .map((call) => call.args.message);
}

test("a reload marks the lists, the render, and a mailbox change", async () => {
  const el = document.createElement("div");
  const raw = transport();
  const api = createApi(raw);
  const nav = createMailNav(api, el);
  await settled();

  expect(debugMarks(raw)).toEqual([
    "reload 1 account.list sent all",
    "reload 1 account.list returned all",
    "reload 1 mailbox.list sent all",
    "reload 1 mailbox.list returned all",
    "reload 1 render started 5",
    "reload 1 render finished 5",
    "reload 1 ready",
  ]);

  raw.emit("mail.mailboxes_changed", { accountId: "foods" });
  await settled();

  expect(debugMarks(raw).slice(7)).toEqual([
    "mailboxes changed foods",
    "reload 2 account.list sent foods",
    "reload 2 account.list returned foods",
    "reload 2 mailbox.list sent foods",
    "reload 2 mailbox.list returned foods",
    "reload 2 render started 3",
    "reload 2 render finished 3",
    "reload 2 ready",
  ]);

  nav.stop();
});

test("a deferred nav lists mailboxes on start and keeps earlier sync state", async () => {
  const el = document.createElement("div");
  const raw = transport();
  const api = createApi(raw);
  const nav = createMailNav(api, el, { defer: true });
  await settled();

  expect(raw.calls.some((call) => call.cmd === "mailbox.list")).toBe(false);
  expect(row(el, "foods")).toBeNull();

  raw.emit("mail.mailboxes_changed", { accountId: "foods" });
  raw.emit("accounts.changed", {});
  raw.emit("sync.state", { accountId: "foods", state: "syncing" });
  await settled();
  expect(raw.calls.some((call) => call.cmd === "mailbox.list")).toBe(false);

  await nav.start();
  await settled();
  expect(raw.calls.filter((call) => call.cmd === "mailbox.list")).toHaveLength(1);
  const pie = row(el, "foods").querySelector(".tree-sync-pie");
  expect(pie).not.toBeNull();
  expect(pie.getAttribute("data-pav-tip")).toBe("Syncing");
  expect(row(el, "foods").querySelector("i.fa-spinner")).toBeNull();

  await nav.start();
  await settled();
  expect(raw.calls.filter((call) => call.cmd === "mailbox.list")).toHaveLength(1);

  nav.stop();
});
