/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test } from "vitest";
import { createApi } from "../api.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";
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

  root.querySelector('[data-mailbox="drafts"]').click();
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
