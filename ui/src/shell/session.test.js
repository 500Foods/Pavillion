/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test, vi } from "vitest";
import { createApi } from "../api.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";
import { KIND_ICONS, createTabs } from "./tabs.js";
import { planRestore, serializeTabs, SESSION_MS } from "./session.js";

afterEach(() => {
  unmount();
  vi.useRealTimers();
  document.body.replaceChildren();
});

function harness() {
  const root = document.createElement("div");
  const workspace = document.createElement("div");
  workspace.className = "workspace";
  const tablist = document.createElement("div");
  tablist.id = "tablist";
  workspace.append(tablist);
  for (const id of ["mail", "help"]) {
    const panel = document.createElement("div");
    panel.id = `panel-${id}`;
    panel.className = "panel";
    panel.hidden = true;
    workspace.append(panel);
  }
  root.append(workspace);
  document.body.append(root);
  const opened = [];
  const tabs = createTabs(tablist, {
    workspace,
    sections: {
      mail: { title: "Mail", nav: true },
      help: { title: "Help", nav: false },
    },
    homeIcon() {
      const icon = document.createElement("i");
      icon.className = "fa-envelope";
      return icon;
    },
    iconLabel: () => "fa-envelope",
  });
  tabs.register("message", {
    resolve(ctx) {
      return Boolean(ctx && ctx.ok === true);
    },
    create(el) {
      el.dataset.created = "yes";
    },
    onOpen(ctx) {
      opened.push(ctx.id);
    },
  });
  tabs.register("test", {
    create(el) {
      el.dataset.created = "yes";
    },
    onOpen(ctx) {
      opened.push(ctx.id);
    },
  });
  return { workspace, tablist, tabs, opened };
}

function tab(fields) {
  return {
    icon: "",
    context: null,
    pinned: false,
    muted: false,
    order: 1,
    active: false,
    ...fields,
  };
}

test("serialize keeps pin, mute, order, and the active tab", () => {
  const { tabs } = harness();
  tabs.openHome("mail");
  const pinned = tabs.openTab({
    section: "help",
    kind: "test",
    key: "1",
    title: "Test 1",
    icon: KIND_ICONS.file,
    context: { n: 1 },
  });
  tabs.setPinned(pinned.id, true);
  const browse = tabs.openTab({
    section: "help",
    kind: "browse",
    key: "a",
    title: "Page",
    context: { url: "https://example.com" },
  });
  tabs.setMuted(browse.id, true);
  const saved = serializeTabs(tabs.list());
  expect(saved.map((item) => item.id)).toEqual(["help:test:1", "mail:home", "help:browse:a"]);
  expect(saved[0]).toMatchObject({ pinned: true, active: false, context: { n: 1 } });
  expect(saved[2]).toMatchObject({ muted: true, active: true, kind: "browse" });
  const plan = planRestore({ restore: true, shutdown: "clean", tabs: saved });
  expect(plan.mode).toBe("tabs");
  expect(plan.unexpected).toBe(false);
  expect(plan.tabs.map((item) => item.id)).toEqual(saved.map((item) => item.id));
  expect(plan.tabs[2].active).toBe(true);
});

test("restore plans the empty strip, the first launch, and a quick start", () => {
  expect(planRestore(null).mode).toBe("first");
  expect(planRestore({ restore: true }).mode).toBe("first");
  expect(planRestore({ restore: true, shutdown: "open" }).unexpected).toBe(false);
  expect(planRestore({ restore: true, shutdown: "clean", tabs: [] }).mode).toBe("empty");
  expect(planRestore({ restore: false }).mode).toBe("quick");
  expect(planRestore({ restore: false, shutdown: "clean", tabs: [] })).toMatchObject({
    mode: "quick",
    tabs: [],
    unexpected: false,
  });
  const testOnly = tab({
    id: "help:test:1",
    section: "help",
    kind: "test",
    key: "1",
    title: "Test 1",
    active: true,
  });
  const mail = tab({
    id: "mail:home",
    section: "mail",
    kind: "home",
    key: "",
    title: "Mail",
    order: 2,
  });
  const off = planRestore({ restore: false, shutdown: "clean", tabs: [testOnly, mail] });
  expect(off.mode).toBe("quick");
  expect(off.tabs).toEqual([]);
  expect(off.unexpected).toBe(false);
  const crashed = planRestore({ restore: true, tabs: [testOnly, mail] }, () => {
    throw new Error("resolve must not run");
  });
  expect(crashed).toMatchObject({ mode: "quick", tabs: [], unexpected: true });
  expect(planRestore({ restore: false, shutdown: "open", tabs: [mail] }).unexpected).toBe(true);
  expect(planRestore({ restore: true, shutdown: "open", tabs: [] }).mode).toBe("quick");
  const pin = tab({
    id: "help:test:1",
    section: "help",
    kind: "test",
    key: "1",
    title: "Pin",
    pinned: true,
    order: 2,
  });
  const active = tab({
    id: "help:test:2",
    section: "help",
    kind: "test",
    key: "2",
    title: "Active",
    order: 3,
    active: true,
  });
  const later = tab({
    id: "help:test:3",
    section: "help",
    kind: "test",
    key: "3",
    title: "Later",
    order: 4,
  });
  const capped = planRestore({
    restore: true,
    shutdown: "clean",
    maxTabs: 3,
    tabs: [mail, pin, active, later],
  });
  expect(capped.limited).toBe(1);
  expect(capped.tabs.map((item) => item.id)).toEqual(["help:test:1", "mail:home", "help:test:2"]);
  expect(capped.tabs.find((item) => item.active).id).toBe("help:test:2");
});

test("a dropped context leaves the tab on its left active, and background tabs stay unloaded", () => {
  const { workspace, tablist, tabs, opened } = harness();
  const gone = tab({
    id: "mail:message:gone",
    section: "mail",
    kind: "message",
    key: "gone",
    title: "Gone",
    context: { ok: false },
    order: 2,
    active: true,
  });
  const mail = tab({
    id: "mail:home",
    section: "mail",
    kind: "home",
    key: "",
    title: "Mail",
    order: 1,
  });
  const kept = tab({
    id: "mail:message:kept",
    section: "mail",
    kind: "message",
    key: "kept",
    title: "Kept",
    context: { ok: true },
    pinned: true,
    order: 3,
  });
  const plan = planRestore({ restore: true, shutdown: "clean", tabs: [mail, gone, kept] }, (item) => {
    return tabs.resolves(item);
  });
  expect(plan.dropped.map((item) => item.title)).toEqual(["Gone"]);
  expect(plan.tabs.map((item) => item.id)).toEqual(["mail:message:kept", "mail:home"]);
  expect(plan.tabs[1].active).toBe(true);
  const extra = tabs.restore(plan.tabs);
  expect(extra).toEqual([]);
  expect(opened).toEqual([]);
  expect(workspace.querySelector("#panel-mail").hidden).toBe(false);
  expect(workspace.querySelector('.panel[data-tab-id="mail:message:kept"]')).toBeNull();
  expect(tablist.querySelector('[data-tab-id="mail:message:kept"]').classList.contains("is-pinned")).toBe(true);
  tablist.querySelector('[data-tab-id="mail:message:kept"]').click();
  expect(opened).toEqual(["mail:message:kept"]);
  expect(workspace.querySelector('.panel[data-tab-id="mail:message:kept"]').dataset.created).toBe("yes");
});

test("pins move ahead of a home tab that was stored first", () => {
  const plan = planRestore({
    restore: true,
    shutdown: "clean",
    tabs: [
      tab({
        id: "mail:home",
        section: "mail",
        kind: "home",
        key: "",
        title: "Mail",
        pinned: true,
        order: 1,
        active: true,
      }),
      tab({
        id: "help:test:1",
        section: "help",
        kind: "test",
        key: "1",
        title: "Test 1",
        pinned: true,
        order: 2,
      }),
    ],
  });
  expect(plan.tabs.map((item) => [item.id, item.pinned, item.active])).toEqual([
    ["help:test:1", true, false],
    ["mail:home", false, true],
  ]);
});

async function boot(session) {
  const el = document.createElement("div");
  document.body.append(el);
  const api = createApi(mockTransport());
  if (session) {
    await api.prefs.ui.set({ session });
  }
  await mount(el, api);
  return { el, api };
}

function ids() {
  return [...document.querySelectorAll("#tablist [role='tab']")].map((node) => node.dataset.tabId);
}

test("a saved session restores order, the pin, and the settings heading", async () => {
  await boot({
    restore: true,
    shutdown: "clean",
    tabs: [
      tab({
        id: "help:test:1",
        section: "help",
        kind: "test",
        key: "1",
        title: "Test 1",
        icon: KIND_ICONS.file,
        context: { n: 1 },
        pinned: true,
        order: 1,
      }),
      tab({
        id: "settings:home",
        section: "settings",
        kind: "home",
        key: "",
        title: "Settings",
        context: { heading: "fonts" },
        order: 2,
        active: true,
      }),
      tab({
        id: "nope:home",
        section: "nope",
        kind: "home",
        key: "",
        title: "Missing",
        order: 3,
      }),
    ],
  });
  expect(ids()).toEqual(["help:test:1", "settings:home"]);
  expect(document.querySelector('[data-tab-id="help:test:1"]').classList.contains("is-pinned")).toBe(true);
  expect(document.querySelector('[data-tab-id="help:test:1"]').getAttribute("aria-selected")).toBe("false");
  expect(document.querySelector('.panel[data-tab-id="help:test:1"]')).toBeNull();
  expect(document.querySelector("#panel-settings").hidden).toBe(false);
  expect(document.querySelector('[data-settings="fonts"]').hidden).toBe(false);
  expect(document.querySelector('[data-settings="zoom"]').hidden).toBe(true);
  expect(document.querySelector('[data-heading="fonts"]').getAttribute("aria-current")).toBe("true");
  expect(document.querySelector('.nav-buttons [data-section="settings"]').getAttribute("aria-pressed")).toBe("true");
  expect(document.querySelector(".pav-toast").textContent).toContain("Missing could not be restored.");
  document.querySelector('[data-tab-id="help:test:1"]').click();
  expect(document.querySelector('.panel[data-tab-id="help:test:1"]').textContent).toBe("Test 1.");
  expect(document.querySelector('.nav-buttons [data-section="settings"]').getAttribute("aria-pressed")).toBe("true");
});

test("restore off opens nothing, and a clean empty save stays empty", async () => {
  const { el } = await boot({
    restore: false,
    shutdown: "clean",
    tabs: [
      tab({
        id: "mail:home",
        section: "mail",
        kind: "home",
        key: "",
        title: "Mail",
        context: { mailboxId: "drafts" },
        order: 2,
      }),
      tab({
        id: "help:test:1",
        section: "help",
        kind: "test",
        key: "1",
        title: "Test 1",
        order: 1,
        active: true,
      }),
    ],
  });
  expect(ids()).toEqual([]);
  expect(document.querySelector("#panel-mail").hidden).toBe(true);
  const draftsRow = document.querySelector('[data-nav="mail"] [role="treeitem"][data-node-id="drafts"]');
  expect(draftsRow.getAttribute("aria-current")).not.toBe("true");
  expect(document.querySelector(".pav-toast")).toBeNull();
  document.querySelector('.nav-buttons [data-section="settings"]').click();
  expect(ids()).toEqual(["settings:home"]);
  expect(document.querySelector("#panel-settings").hidden).toBe(false);
  expect(document.querySelector("#panel-mail").hidden).toBe(true);
  unmount();
  el.remove();
  await boot({ restore: true, shutdown: "clean", tabs: [] });
  expect(ids()).toEqual([]);
  expect(document.querySelector("#panel-mail").hidden).toBe(true);
  expect(document.querySelector(".pav-toast")).toBeNull();
});

test("max tabs keeps the active tab and the pin, and the field saves", async () => {
  const { api } = await boot({
    restore: true,
    shutdown: "clean",
    maxTabs: 3,
    tabs: [
      tab({
        id: "mail:home",
        section: "mail",
        kind: "home",
        key: "",
        title: "Mail",
        order: 1,
      }),
      tab({
        id: "help:test:1",
        section: "help",
        kind: "test",
        key: "1",
        title: "Pin",
        pinned: true,
        order: 2,
      }),
      tab({
        id: "help:test:2",
        section: "help",
        kind: "test",
        key: "2",
        title: "Active",
        order: 3,
        active: true,
      }),
      tab({
        id: "help:test:3",
        section: "help",
        kind: "test",
        key: "3",
        title: "Later",
        order: 4,
      }),
    ],
  });
  expect(ids()).toEqual(["help:test:1", "mail:home", "help:test:2"]);
  expect(document.querySelector('.panel[data-tab-id="help:test:3"]')).toBeNull();
  expect(document.querySelector(".pav-toast").textContent).toContain("Max tabs is 3. Some tabs stayed closed.");
  document.querySelector('[data-heading="tabs"]').click();
  const field = document.querySelector("#session-max");
  expect(field.value).toBe("3");
  field.value = "12";
  field.dispatchEvent(new Event("change", { bubbles: true }));
  await new Promise((resolve) => {
    setTimeout(resolve, 400);
  });
  expect((await api.prefs.ui.get()).session.maxTabs).toBe(12);
  document.querySelector('.nav-buttons [data-section="settings"]').click();
  expect(ids()).toContain("settings:home");
});

test("an unexpected shutdown opens nothing, and the next quit keeps the tab that was opened", async () => {
  const { api } = await boot({
    restore: true,
    tabs: [
      tab({
        id: "help:test:1",
        section: "help",
        kind: "test",
        key: "1",
        title: "Test 1",
        order: 1,
        active: true,
      }),
    ],
  });
  expect(ids()).toEqual([]);
  expect(document.querySelector("#panel-mail").hidden).toBe(true);
  expect(document.querySelector('.panel[data-tab-id="help:test:1"]')).toBeNull();
  expect(document.querySelector(".pav-toast").textContent).toContain("Unexpected shutdown, performing quick start.");
  document.querySelector('.nav-buttons [data-section="settings"]').click();
  expect(ids()).toEqual(["settings:home"]);
  expect(document.querySelector("#panel-settings").hidden).toBe(false);
  window.dispatchEvent(new Event("pagehide"));
  await new Promise((resolve) => {
    setTimeout(resolve, 0);
  });
  const saved = await api.prefs.ui.get();
  expect(saved.session.shutdown).toBe("clean");
  expect(saved.session.tabs.map((item) => item.id)).toEqual(["settings:home"]);
});

test("a failed open marker does not load the saved tabs", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const transport = mockTransport();
  const api = createApi(transport);
  await api.prefs.ui.set({
    session: {
      restore: true,
      shutdown: "clean",
      tabs: [
        tab({
          id: "help:test:1",
          section: "help",
          kind: "test",
          key: "1",
          title: "Test 1",
          order: 1,
          active: true,
        }),
      ],
    },
  });
  const inner = transport.call;
  transport.call = async (cmd, args) => {
    if (cmd === "prefs.ui.set") {
      throw new Error("disk full");
    }
    return inner(cmd, args);
  };
  await mount(el, api);
  expect(ids()).toEqual([]);
  expect(document.querySelector("#panel-mail").hidden).toBe(true);
  expect(document.querySelector('.panel[data-tab-id="help:test:1"]')).toBeNull();
  const notices = [...document.querySelectorAll(".pav-toast")].map((node) => node.textContent);
  expect(notices.some((text) => text.includes("Could not save the profile."))).toBe(true);
  expect(notices.some((text) => text.includes("Unexpected shutdown"))).toBe(false);
  const saved = await api.prefs.ui.get();
  expect(saved.session.shutdown).toBe("clean");
  expect(saved.session.tabs[0].id).toBe("help:test:1");
});

test("the quit hook stores a clean session before the window goes", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const api = createApi(mockTransport());
  const host = {};
  await mount(el, api, { hostServices: host });
  expect(ids()).toEqual(["mail:home"]);
  expect(document.querySelector("#app-version")).toBeNull();
  expect(typeof host.onQuit).toBe("function");
  await host.onQuit();
  const saved = await api.prefs.ui.get();
  expect(saved.session.shutdown).toBe("clean");
  expect(saved.session.tabs.map((item) => item.id)).toEqual(["mail:home"]);
  window.dispatchEvent(new Event("pagehide"));
  await new Promise((resolve) => {
    setTimeout(resolve, 0);
  });
  expect((await api.prefs.ui.get()).session.shutdown).toBe("clean");
});

test("quitting saves the open tabs, and the switch turns restore off", async () => {
  const { api } = await boot();
  expect(ids()).toEqual(["mail:home"]);
  const running = await api.prefs.ui.get();
  expect(running.session.shutdown).toBe("open");
  expect(running.session.tabs).toEqual([]);
  window.dispatchEvent(new Event("pagehide"));
  await new Promise((resolve) => {
    setTimeout(resolve, 0);
  });
  const saved = await api.prefs.ui.get();
  expect(saved.session.restore).toBe(true);
  expect(saved.session.shutdown).toBe("clean");
  expect(saved.session.maxTabs).toBe(50);
  expect(saved.session.tabs).toMatchObject([{ id: "mail:home", kind: "home", active: true }]);

  document.querySelector('[data-heading="tabs"]').click();
  const box = document.querySelector("#session-restore");
  const maxField = document.querySelector("#session-max");
  expect(box.checked).toBe(true);
  expect(maxField.value).toBe("50");
  expect(document.querySelector('[data-settings="tabs"]').hidden).toBe(false);
  box.checked = false;
  box.dispatchEvent(new Event("change", { bubbles: true }));
  await new Promise((resolve) => {
    setTimeout(resolve, 400);
  });
  const switched = await api.prefs.ui.get();
  expect(switched.session.restore).toBe(false);
  expect(switched.session.shutdown).toBe("open");
});

test("a tab change is written on the session interval", async () => {
  vi.useFakeTimers();
  const { api } = await boot();
  document.querySelector('[data-nav="mail"] [role="treeitem"][data-node-id="sent"]').click();
  await vi.advanceTimersByTimeAsync(SESSION_MS);
  const saved = await api.prefs.ui.get();
  expect(saved.session.tabs[0].context).toEqual({ mailboxId: "sent" });
  expect(saved.session.shutdown).toBe("open");
});
