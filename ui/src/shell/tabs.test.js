/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test } from "vitest";
import { createApi } from "../api.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";
import { createMenu } from "../common/menu.js";
import { KIND_ICONS, createTabs } from "./tabs.js";

afterEach(() => {
  unmount();
  document.body.replaceChildren();
});

function harness(options = {}) {
  const root = document.createElement("div");
  const workspace = document.createElement("div");
  workspace.className = "workspace";
  const tablist = document.createElement("div");
  tablist.id = "tablist";
  workspace.append(tablist);
  for (const id of ["mail", "contacts", "help"]) {
    const panel = document.createElement("div");
    panel.id = `panel-${id}`;
    panel.className = "panel";
    panel.hidden = id !== "mail";
    workspace.append(panel);
  }
  root.append(workspace);
  document.body.append(root);
  const sections = {
    mail: { title: "Mail", nav: true },
    contacts: { title: "Contacts", nav: true },
    help: { title: "Help", nav: false },
  };
  let iconClass = "fa-envelope";
  const tabs = createTabs(tablist, {
    workspace,
    sections,
    homeIcon() {
      const icon = document.createElement("i");
      icon.className = iconClass;
      return icon;
    },
    iconLabel: () => iconClass,
    confirm: options.confirm,
    onReveal: options.onReveal,
    menu: options.menu,
    strip: options.strip,
    maxTabs: options.maxTabs,
  });
  return {
    root,
    workspace,
    tablist,
    tabs,
    setIcon(name) {
      iconClass = name;
    },
  };
}

test("max tabs blocks a new sub-tab and still opens a home", async () => {
  const events = [];
  const { tabs } = harness({ maxTabs: 2 });
  tabs.subscribe((change) => {
    if (change.type === "limit") {
      events.push(change.max);
    }
  });
  tabs.openHome("mail");
  expect(tabs.openTab({ section: "help", kind: "test", key: "1", title: "One", icon: KIND_ICONS.file })).not.toBeNull();
  expect(tabs.openTab({ section: "help", kind: "test", key: "2", title: "Two", icon: KIND_ICONS.file })).toBeNull();
  expect(tabs.openTab({ section: "help", kind: "test", key: "3", title: "Three", icon: KIND_ICONS.file })).toBeNull();
  expect(events).toEqual([2]);
  expect(tabs.list().map((item) => item.id)).toEqual(["mail:home", "help:test:1"]);
  expect(tabs.openHome("contacts")).not.toBeNull();
  expect(tabs.openTab({ section: "help", kind: "test", key: "1", title: "One", icon: KIND_ICONS.file }).id).toBe("help:test:1");
  expect(await tabs.closeTab("help:test:1")).toBe(true);
  expect(await tabs.closeTab("contacts:home")).toBe(true);
  expect(tabs.openTab({ section: "help", kind: "test", key: "2", title: "Two", icon: KIND_ICONS.file })).not.toBeNull();
  expect(tabs.openTab({ section: "help", kind: "test", key: "3", title: "Three", icon: KIND_ICONS.file })).toBeNull();
  expect(events).toEqual([2, 2]);
});

test("one id is one tab and a second open only activates it", () => {
  const { tablist, tabs } = harness();
  const events = [];
  tabs.subscribe((change) => events.push(change.type));
  const first = tabs.openTab({
    section: "help",
    kind: "test",
    key: "1",
    title: "Test 1",
    icon: KIND_ICONS.file,
  });
  const node = tablist.querySelector('[data-tab-id="help:test:1"]');
  const again = tabs.openTab({
    section: "help",
    kind: "test",
    key: "1",
    title: "Test 1",
  });
  expect(again.id).toBe(first.id);
  expect(tabs.list()).toHaveLength(1);
  expect(node.isConnected).toBe(true);
  expect(tablist.querySelectorAll("[role='tab']")).toHaveLength(1);
  expect(events).toEqual(["open", "activate", "activate"]);
});

test("closing the middle tab activates and focuses the tab on its left", async () => {
  const { tablist, tabs } = harness();
  tabs.openHome("mail");
  tabs.openTab({ section: "help", kind: "test", key: "1", title: "Test 1", icon: KIND_ICONS.file });
  tabs.openTab({ section: "help", kind: "test", key: "2", title: "Test 2", icon: KIND_ICONS.file });
  tabs.openTab({ section: "help", kind: "test", key: "3", title: "Test 3", icon: KIND_ICONS.file });
  const events = [];
  tabs.subscribe((change) => events.push(change.type));
  const closed = await tabs.closeTab("help:test:2");
  expect(closed).toBe(true);
  const active = tabs.list().find((item) => item.active);
  expect(active.id).toBe("help:test:1");
  expect(document.activeElement).toBe(tablist.querySelector('[data-tab-id="help:test:1"]'));
  expect(tablist.querySelector('[data-tab-id="help:test:2"]')).toBeNull();
  expect(events.map((type) => type)).toEqual(["close", "activate"]);
});

test("every tab can close, including a home tab", async () => {
  const { workspace, tablist, tabs } = harness();
  tabs.openHome("mail");
  const home = tablist.querySelector('[data-tab-id="mail:home"]');
  expect(home.querySelector(".tab-close").getAttribute("aria-label")).toBe("Close Mail");
  expect(home.querySelector(".tab-icon i").className).toBe("fa-envelope");
  expect(home.querySelector(".tab-title").textContent).toBe("Mail");
  home.dispatchEvent(new MouseEvent("auxclick", { button: 1, bubbles: true }));
  await Promise.resolve();
  expect(tabs.list()).toEqual([]);
  expect(workspace.querySelector("#panel-mail").hidden).toBe(true);
});

test("middle-click closes a sub-tab and reports the change", async () => {
  const { tablist, tabs } = harness();
  const events = [];
  tabs.subscribe((change) => events.push(change.type));
  tabs.openHome("help");
  tabs.openTab({ section: "help", kind: "test", key: "1", title: "Test 1", icon: KIND_ICONS.file });
  const tab = tablist.querySelector('[data-tab-id="help:test:1"]');
  expect(tab.querySelector(".tab-icon i").className).toBe(KIND_ICONS.file);
  expect(tab.querySelector(".tab-close").getAttribute("aria-label")).toBe("Close Test 1");
  tab.dispatchEvent(new MouseEvent("auxclick", { button: 1, bubbles: true }));
  await Promise.resolve();
  expect(tabs.list().map((item) => item.id)).toEqual(["help:home"]);
  expect(events).toContain("close");
  const home = tablist.querySelector('[data-tab-id="help:home"]');
  expect(home.getAttribute("aria-selected")).toBe("true");
  expect(document.activeElement).toBe(home);
});

test("a home selection is stored and a dirty tab stays until confirm", async () => {
  let allow = false;
  const { workspace, tabs } = harness({
    confirm() {
      return Promise.resolve(allow);
    },
  });
  const opened = [];
  const closed = [];
  tabs.register("draft", {
    create(el, ctx) {
      el.dataset.created = ctx.title;
    },
    onOpen(ctx) {
      opened.push(ctx.key);
    },
    onClose() {
      closed.push("closed");
    },
    unsaved() {
      return true;
    },
  });
  tabs.openHome("mail", { mailboxId: "drafts" });
  expect(workspace.querySelector("#panel-mail").dataset.tabContext).toBe(JSON.stringify({ mailboxId: "drafts" }));
  tabs.openTab({ section: "mail", kind: "draft", key: "a", title: "Draft" });
  expect(workspace.querySelector('.panel[data-tab-id="mail:draft:a"]').dataset.created).toBe("Draft");
  expect(opened).toEqual(["a"]);
  expect(await tabs.closeTab("mail:draft:a")).toBe(false);
  expect(tabs.list().some((item) => item.id === "mail:draft:a")).toBe(true);
  allow = true;
  expect(await tabs.closeTab("mail:draft:a")).toBe(true);
  expect(closed).toEqual(["closed"]);
  expect(tabs.list().find((item) => item.active).id).toBe("mail:home");
});

test("dragging a tab left reorders the strip", () => {
  const { tablist, tabs } = harness();
  tabs.openHome("mail");
  tabs.openTab({ section: "help", kind: "test", key: "1", title: "Test 1", icon: KIND_ICONS.file });
  tabs.openTab({ section: "help", kind: "test", key: "2", title: "Test 2", icon: KIND_ICONS.file });
  const row = [...tablist.querySelectorAll("[role='tab']")];
  row.forEach((node, i) => {
    node.getBoundingClientRect = () => ({
      x: i * 100,
      y: 0,
      left: i * 100,
      top: 0,
      right: (i + 1) * 100,
      bottom: 30,
      width: 100,
      height: 30,
      toJSON() {
        return {};
      },
    });
  });
  const mover = row[2];
  const pointer = (type, clientX) => new PointerEvent(type, {
    bubbles: true,
    cancelable: true,
    clientX,
    button: 0,
    pointerId: 1,
  });
  mover.dispatchEvent(pointer("pointerdown", 250));
  mover.dispatchEvent(pointer("pointermove", 20));
  mover.dispatchEvent(pointer("pointerup", 20));
  expect(tabs.list().map((item) => item.id)).toEqual(["help:test:2", "mail:home", "help:test:1"]);
  expect([...tablist.querySelectorAll("[role='tab']")].map((node) => node.dataset.tabId)).toEqual([
    "help:test:2",
    "mail:home",
    "help:test:1",
  ]);
});

test("double-click reveals that section and a single click does not", () => {
  const seen = [];
  const { tablist, tabs } = harness({
    onReveal(section) {
      seen.push(section);
    },
  });
  tabs.openTab({ section: "help", kind: "test", key: "1", title: "Test 1", icon: KIND_ICONS.file });
  const tab = tablist.querySelector('[data-tab-id="help:test:1"]');
  tab.click();
  expect(seen).toEqual([]);
  tab.dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
  expect(seen).toEqual(["help"]);
});

test("refreshIcons replaces the home icon node", () => {
  const { tablist, tabs, setIcon } = harness();
  tabs.openHome("mail");
  setIcon("fa-folder");
  tabs.refreshIcons();
  const icon = tablist.querySelector('[data-tab-id="mail:home"] .tab-icon i');
  expect(icon.className).toBe("fa-folder");
  expect(tabs.list()[0].icon).toBe("fa-folder");
});

test("debug Help opens test tabs and a nav click returns home", async () => {
  const transport = mockTransport();
  const call = transport.call;
  transport.call = async (cmd, args) => {
    const result = await call(cmd, args);
    if (cmd === "system.status" && result) {
      result.debug = true;
    }
    return result;
  };
  const el = document.createElement("div");
  document.body.append(el);
  await mount(el, createApi(transport), { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  const opener = root.querySelector("#open-test-tab");
  expect(opener.hidden).toBe(false);
  opener.click();
  opener.click();
  opener.click();
  expect([...root.querySelectorAll("#tablist .tab-title")].map((node) => node.textContent)).toEqual([
    "Mail",
    "Test 1",
    "Test 2",
    "Test 3",
  ]);
  const middle = root.querySelector('[data-tab-id="help:test:2"]');
  expect(middle.querySelector(".tab-icon i").className).toContain("fa-file");
  middle.querySelector(".tab-close").click();
  const left = root.querySelector('#tablist [data-tab-id="help:test:1"]');
  expect(left.getAttribute("aria-selected")).toBe("true");
  expect(document.activeElement).toBe(left);
  const panel = root.querySelector('.panel[data-tab-id="help:test:1"]');
  expect(panel.hidden).toBe(false);
  expect(panel.textContent).toBe("Test 1.");
  expect(left.getAttribute("aria-controls")).toBe(panel.id);

  const mail = root.querySelector('#tablist [data-tab-id="mail:home"]');
  expect(mail.querySelector(".tab-close").getAttribute("aria-label")).toBe("Close Mail");
  expect(mail.querySelector(".tab-icon i, .tab-icon svg, .tab-icon img")).not.toBeNull();
  expect(root.querySelector('.nav-buttons [data-section="mail"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-nav="mail"]').hidden).toBe(false);

  root.querySelector('.nav-buttons [data-section="contacts"]').click();
  expect(root.querySelector("#panel-contacts").hidden).toBe(false);
  expect(root.querySelector('.nav-buttons [data-section="contacts"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-nav="contacts"]').hidden).toBe(false);

  left.click();
  expect(root.querySelector('.panel[data-tab-id="help:test:1"]').hidden).toBe(false);
  expect(root.querySelector('.nav-buttons [data-section="contacts"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-nav="contacts"]').hidden).toBe(false);

  left.dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
  expect(root.querySelector('#tablist [data-tab-id="help:home"]').getAttribute("aria-selected")).toBe("true");
  expect(root.querySelector("#panel-help").hidden).toBe(false);
  expect(root.querySelector('.nav-buttons [data-section="help"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-nav="contacts"]').hidden).toBe(false);

  mail.dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
  expect(mail.getAttribute("aria-selected")).toBe("true");
  expect(root.querySelector("#panel-mail").hidden).toBe(false);
  expect(root.querySelector('.nav-buttons [data-section="mail"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-nav="mail"]').hidden).toBe(false);

  mail.querySelector(".tab-close").click();
  expect(root.querySelector('#tablist [data-tab-id="mail:home"]')).toBeNull();
  root.querySelector('.nav-buttons [data-section="mail"]').click();
  expect(root.querySelector('#tablist [data-tab-id="mail:home"]').getAttribute("aria-selected")).toBe("true");

  root.querySelector('[data-nav="mail"] [role="treeitem"][data-node-id="drafts"]').click();
  expect(root.querySelector("#panel-mail").hidden).toBe(false);
  expect(root.querySelector("#panel-mail").dataset.tabContext).toBe(JSON.stringify({ mailboxId: "drafts" }));
  expect(root.querySelector('#tablist [data-tab-id="mail:home"]').getAttribute("aria-selected")).toBe("true");

  root.querySelector('#letters-nav [data-letter="C"]').click();
  expect(root.querySelector("#panel-contacts").hidden).toBe(false);
  expect(root.querySelector("#panel-contacts").dataset.tabContext).toBe(JSON.stringify({ letter: "C" }));

  root.querySelector('[data-heading="fonts"]').click();
  expect(root.querySelector("#panel-settings").hidden).toBe(false);
  expect(root.querySelector('[data-settings="fonts"]').hidden).toBe(false);
  expect(root.querySelector("#panel-settings").dataset.tabContext).toBe(JSON.stringify({ heading: "fonts" }));
});

function place(nodes) {
  nodes.forEach((node, i) => {
    node.getBoundingClientRect = () => ({
      x: i * 100,
      y: 0,
      left: i * 100,
      top: 0,
      right: (i + 1) * 100,
      bottom: 30,
      width: 100,
      height: 30,
      toJSON() {
        return {};
      },
    });
  });
}

function drag(node, clientX) {
  const pointer = (type, x) => new PointerEvent(type, {
    bubbles: true,
    cancelable: true,
    clientX: x,
    button: 0,
    pointerId: 1,
  });
  node.dispatchEvent(pointer("pointerdown", clientX));
  node.dispatchEvent(pointer("pointermove", clientX + 40));
  node.dispatchEvent(pointer("pointerup", clientX));
}

test("pinning keeps the pinned group on the left", () => {
  const { tablist, tabs } = harness();
  tabs.openHome("mail");
  tabs.openTab({ section: "help", kind: "test", key: "1", title: "Test 1", icon: KIND_ICONS.file });
  tabs.openTab({ section: "help", kind: "test", key: "2", title: "Test 2", icon: KIND_ICONS.file });
  expect(tabs.setPinned("mail:home", true)).toBe(false);
  expect(tabs.setPinned("help:test:1", true)).toBe(true);
  expect(tabs.setPinned("help:test:2", true)).toBe(true);
  expect(tabs.list().map((item) => item.id)).toEqual(["help:test:1", "help:test:2", "mail:home"]);
  expect(tablist.querySelector('[data-tab-id="help:test:1"]').classList.contains("is-pinned")).toBe(true);
  const pinned = tablist.querySelector('[data-tab-id="help:test:2"]');
  const mail = tablist.querySelector('[data-tab-id="mail:home"]');
  place([...tablist.querySelectorAll("[role='tab']")]);
  drag(pinned, 280);
  expect(tabs.list().map((item) => item.id)).toEqual(["help:test:1", "help:test:2", "mail:home"]);
  drag(mail, 10);
  expect(tabs.list().map((item) => item.id)).toEqual(["help:test:1", "help:test:2", "mail:home"]);
  expect(tabs.setPinned("help:test:1", false)).toBe(true);
  expect(tabs.list().map((item) => item.id)).toEqual(["help:test:2", "help:test:1", "mail:home"]);
});

test("duplicate adds the next #n key and home cannot duplicate", () => {
  const { tabs } = harness();
  tabs.openHome("mail");
  tabs.openTab({
    section: "help",
    kind: "test",
    key: "1",
    title: "Test 1",
    icon: KIND_ICONS.file,
    context: { n: 1 },
  });
  const copy = tabs.duplicate("help:test:1");
  expect(copy.id).toBe("help:test:1#2");
  expect(copy.title).toBe("Test 1");
  expect(copy.context).toEqual({ n: 1 });
  expect(copy.active).toBe(true);
  expect(tabs.duplicate("help:test:1#2").id).toBe("help:test:1#3");
  expect(tabs.duplicate("mail:home")).toBeNull();
});

test("close all sub-tabs spares home and pinned tabs", async () => {
  const { tabs } = harness();
  tabs.openHome("mail");
  tabs.openTab({ section: "help", kind: "test", key: "1", title: "Test 1", icon: KIND_ICONS.file });
  tabs.openTab({ section: "help", kind: "test", key: "2", title: "Test 2", icon: KIND_ICONS.file });
  tabs.setPinned("help:test:1", true);
  await tabs.closeAllSubTabs();
  expect(tabs.list().map((item) => item.id)).toEqual(["help:test:1", "mail:home"]);
});

test("cancel on an unsaved tab stops the rest of close all", async () => {
  let calls = 0;
  const { tabs } = harness({
    confirm() {
      calls += 1;
      return false;
    },
  });
  tabs.register("draft", { unsaved: () => true });
  tabs.openHome("mail");
  tabs.openTab({ section: "mail", kind: "draft", key: "a", title: "A" });
  tabs.openTab({ section: "mail", kind: "draft", key: "b", title: "B" });
  expect(await tabs.closeAllSubTabs()).toBe(false);
  expect(calls).toBe(1);
  expect(tabs.list().map((item) => item.id)).toEqual(["mail:home", "mail:draft:a", "mail:draft:b"]);
});

test("close others and close to the right spare pinned tabs", async () => {
  const { tabs } = harness();
  tabs.openHome("mail");
  tabs.openTab({ section: "help", kind: "test", key: "1", title: "Test 1", icon: KIND_ICONS.file });
  tabs.openTab({ section: "help", kind: "test", key: "2", title: "Test 2", icon: KIND_ICONS.file });
  tabs.setPinned("help:test:1", true);
  await tabs.closeToTheRight("mail:home");
  expect(tabs.list().map((item) => item.id)).toEqual(["help:test:1", "mail:home"]);
  tabs.openTab({ section: "help", kind: "test", key: "2", title: "Test 2", icon: KIND_ICONS.file });
  await tabs.closeOthers("help:test:2");
  expect(tabs.list().map((item) => item.id)).toEqual(["help:test:1", "help:test:2"]);
  expect(tabs.list().find((item) => item.active).id).toBe("help:test:2");
});

test("reopen restores the last twenty closes and activates an id that is already open", async () => {
  const { tabs } = harness();
  for (let n = 1; n <= 21; n += 1) {
    tabs.openTab({ section: "help", kind: "test", key: String(n), title: `T${n}`, icon: KIND_ICONS.file });
    expect(await tabs.closeTab(`help:test:${n}`)).toBe(true);
  }
  expect(tabs.reopenClosed().id).toBe("help:test:21");
  expect(tabs.reopenClosed().id).toBe("help:test:20");
  tabs.openTab({ section: "help", kind: "test", key: "19", title: "T19", icon: KIND_ICONS.file });
  const again = tabs.reopenClosed();
  expect(again.id).toBe("help:test:19");
  expect(tabs.list().filter((item) => item.id === "help:test:19")).toHaveLength(1);
  expect(again.active).toBe(true);

  tabs.openTab({ section: "help", kind: "test", key: "8", title: "Pinned", icon: KIND_ICONS.file });
  tabs.setPinned("help:test:8", true);
  await tabs.closeTab("help:test:8");
  expect(tabs.reopenClosed().pinned).toBe(true);
});

test("reload runs onOpen again for the active view", () => {
  const seen = [];
  const { tabs } = harness();
  tabs.register("test", {
    onOpen(ctx) {
      seen.push(ctx.key);
    },
  });
  tabs.openTab({ section: "help", kind: "test", key: "1", title: "Test 1", icon: KIND_ICONS.file });
  expect(seen).toEqual(["1"]);
  expect(tabs.reloadActive()).toBe(true);
  expect(seen).toEqual(["1", "1"]);
  tabs.openHome("mail");
  expect(tabs.reloadActive()).toBe(false);
});

test("mute applies only to a browse tab", () => {
  const { tablist, tabs } = harness();
  tabs.openTab({ section: "help", kind: "test", key: "1", title: "Test 1", icon: KIND_ICONS.file });
  expect(tabs.setMuted("help:test:1", true)).toBe(false);
  tabs.openTab({ section: "help", kind: "browse", key: "1", title: "Page", icon: KIND_ICONS.browse });
  expect(tabs.setMuted("help:browse:1", true)).toBe(true);
  expect(tablist.querySelector('[data-tab-id="help:browse:1"]').classList.contains("is-muted")).toBe(true);
  expect(tabs.list().find((item) => item.id === "help:browse:1").muted).toBe(true);
});

test("the tab menu and the strip menu expose the lock items", () => {
  const root = document.createElement("div");
  document.body.append(root);
  const workspace = document.createElement("div");
  const tablist = document.createElement("div");
  const strip = document.createElement("div");
  strip.className = "tabs";
  const spacer = document.createElement("div");
  spacer.className = "tabs-spacer";
  workspace.append(strip);
  strip.append(tablist, spacer);
  root.append(workspace);
  const menu = createMenu(root);
  const tabs = createTabs(tablist, {
    workspace,
    sections: {
      mail: { title: "Mail", nav: true },
      help: { title: "Help", nav: false },
    },
    menu,
    strip,
  });
  tabs.openHome("mail");
  tabs.openTab({ section: "help", kind: "test", key: "1", title: "Test 1", icon: KIND_ICONS.file });
  const testTab = tablist.querySelector('[data-tab-id="help:test:1"]');
  testTab.focus();
  testTab.dispatchEvent(new KeyboardEvent("keydown", {
    key: "F10",
    shiftKey: true,
    bubbles: true,
    cancelable: true,
  }));
  const labels = () => [...root.querySelectorAll("[role='menuitem']")].map((item) => item.textContent);
  expect(labels()).toEqual([
    "Pin",
    "Duplicate",
    "Mute",
    "Close",
    "Close others",
    "Close to the right",
    "Reopen closed tab",
  ]);
  expect(root.querySelector("[role='menuitem']").getAttribute("aria-disabled")).toBeNull();
  root.querySelector("[role='menuitem']").click();
  expect(tabs.list()[0].id).toBe("help:test:1");
  expect(tabs.list()[0].pinned).toBe(true);

  const home = tablist.querySelector('[data-tab-id="mail:home"]');
  home.dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: 4, clientY: 4 }));
  const items = [...root.querySelectorAll("[role='menuitem']")];
  expect(items.find((item) => item.textContent === "Pin").getAttribute("aria-disabled")).toBe("true");
  expect(items.find((item) => item.textContent === "Duplicate").getAttribute("aria-disabled")).toBe("true");
  expect(items.find((item) => item.textContent === "Mute").getAttribute("aria-disabled")).toBe("true");

  tabs.openTab({ section: "help", kind: "test", key: "2", title: "Test 2", icon: KIND_ICONS.file });
  spacer.dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: 12, clientY: 12 }));
  const stripItems = [...root.querySelectorAll("[role='menuitem']")];
  expect(stripItems.map((item) => item.textContent)).toEqual(["Vertical tabs", "Close all sub-tabs"]);
  expect(stripItems[0].getAttribute("aria-disabled")).toBe("true");
  stripItems[1].click();
  expect(tabs.list().map((item) => item.id)).toEqual(["help:test:1", "mail:home"]);
  tabs.destroy();
  menu.destroy();
});

function debugTransport() {
  const transport = mockTransport();
  const call = transport.call;
  transport.call = async (cmd, args) => {
    const result = await call(cmd, args);
    if (cmd === "system.status" && result) {
      result.debug = true;
    }
    return result;
  };
  return transport;
}

function key(target, name, extra) {
  const event = new KeyboardEvent("keydown", {
    key: name,
    bubbles: true,
    cancelable: true,
    ...extra,
  });
  target.dispatchEvent(event);
  return event;
}

test("workspace shortcuts switch and close, and a text field ignores them", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  await mount(el, createApi(debugTransport()), { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  root.querySelector("#open-test-tab").click();
  const testTab = root.querySelector('[data-tab-id="help:test:1"]');
  const field = root.querySelector("#theme-folder");
  field.focus();
  const blocked = key(field, "w", { ctrlKey: true });
  expect(blocked.defaultPrevented).toBe(false);
  expect(testTab.isConnected).toBe(true);

  testTab.focus();
  const closed = key(testTab, "w", { ctrlKey: true });
  expect(closed.defaultPrevented).toBe(true);
  expect(root.querySelector('[data-tab-id="help:test:1"]')).toBeNull();
  const reopened = key(root, "T", { ctrlKey: true, shiftKey: true });
  expect(reopened.defaultPrevented).toBe(true);
  const restored = root.querySelector('[data-tab-id="help:test:1"]');
  expect(restored.getAttribute("aria-selected")).toBe("true");

  const section = key(root, "2", { ctrlKey: true });
  expect(section.defaultPrevented).toBe(true);
  expect(root.querySelector('.nav-buttons [data-section="contacts"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-nav="contacts"]').hidden).toBe(false);
  expect(root.querySelector("#panel-contacts").hidden).toBe(false);

  restored.click();
  expect(root.querySelector('.nav-buttons [data-section="contacts"]').getAttribute("aria-pressed")).toBe("true");
  const previous = key(restored, "Tab", { ctrlKey: true, shiftKey: true });
  expect(previous.defaultPrevented).toBe(true);
  expect(root.querySelector('[data-tab-id="mail:home"]').getAttribute("aria-selected")).toBe("true");
  expect(root.querySelector('.nav-buttons [data-section="contacts"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-nav="mail"]').hidden).toBe(true);

  const first = key(root, "1", { altKey: true });
  expect(first.defaultPrevented).toBe(true);
  expect(root.querySelector('[data-tab-id="mail:home"]').getAttribute("aria-selected")).toBe("true");
  const reload = key(root, "r", { ctrlKey: true });
  expect(reload.defaultPrevented).toBe(true);
  expect(root.querySelector('[data-tab-id="help:test:1"]')).not.toBeNull();

  const day = root.querySelector("#month div");
  expect(day).not.toBeNull();
  day.dispatchEvent(new MouseEvent("mousedown", { bubbles: true, cancelable: true }));
  const mail = root.querySelector('[data-tab-id="mail:home"]');
  mail.blur();
  expect(document.activeElement).toBe(document.body);
  const fromPage = key(document.body, "w", { ctrlKey: true });
  expect(fromPage.defaultPrevented).toBe(true);
  expect(root.querySelector('[data-tab-id="mail:home"]')).toBeNull();
  expect(root.querySelector('[data-tab-id="help:test:1"]')).not.toBeNull();
});
