/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test, vi } from "vitest";
import { createApi } from "./api.js";
import { mount, unmount } from "./index.js";
import { mockTransport } from "./transport/mock.js";

afterEach(() => {
  unmount();
  vi.useRealTimers();
  document.body.replaceChildren();
});

function ids() {
  return [...document.querySelectorAll("#tablist [role='tab']")].map((node) => node.dataset.tabId);
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

async function boot(ui) {
  const el = document.createElement("div");
  document.body.append(el);
  const api = createApi(mockTransport());
  if (ui) {
    await api.prefs.ui.set(ui);
  }
  await mount(el, api, { hostServices: { setRootFontSize() {} } });
  return el.querySelector(".pav-root");
}

test("suite cells start hidden, and hiding Mail removes that cell and its home", async () => {
  const root = await boot();
  expect(root.querySelector('[data-section="bots"]').hidden).toBe(true);
  expect(root.querySelector('[data-section="github"]').hidden).toBe(true);
  expect(root.querySelector('[data-nav="github"]')).toBeNull();
  expect(root.querySelector('[data-section="tasks"]').hidden).toBe(true);
  expect(root.querySelector('[data-section="notes"]').hidden).toBe(true);
  expect(root.querySelector('[data-section="argent"]').hidden).toBe(true);
  expect(root.querySelector('[data-section="mail"]').hidden).toBe(false);
  expect(root.querySelector('[data-section="files"]').hidden).toBe(false);
  expect(ids()).toContain("mail:home");
  expect([...root.querySelectorAll("[data-heading]")].map((node) => node.textContent)).toEqual([
    "Features",
    "Themes",
    "Icons",
    "Fonts",
    "Tabs",
    "Zooms",
  ]);
  expect(root.querySelector('[data-settings="features"]').hidden).toBe(false);
  expect(root.querySelector('[data-feature="bots"]').checked).toBe(false);
  expect(root.querySelector('[data-feature="github"]').checked).toBe(false);
  expect(root.querySelector('[data-feature="tasks"]').checked).toBe(false);

  root.querySelector('[data-tab-id="mail:home"]').focus();
  key(root, "5", { ctrlKey: true });
  expect(root.querySelector('[data-section="files"]').getAttribute("aria-pressed")).toBe("true");
  expect(ids()).toContain("files:home");
  key(root, "4", { ctrlKey: true });
  expect(root.querySelector('[data-tab-id="tasks:home"]')).toBeNull();

  root.querySelector('[data-section="settings"]').click();
  root.querySelector('[data-feature="mail"]').click();
  expect(root.querySelector('[data-section="mail"]').hidden).toBe(true);
  expect(root.querySelector('[data-tab-id="mail:home"]')).toBeNull();
  expect(root.querySelector('[data-tab-id="settings:home"]').getAttribute("aria-selected")).toBe("true");

  root.querySelector('[data-tab-id="settings:home"]').focus();
  key(root, "1", { ctrlKey: true });
  expect(root.querySelector('[data-tab-id="mail:home"]')).toBeNull();
  key(root, "T", { ctrlKey: true, shiftKey: true });
  expect(root.querySelector('[data-tab-id="mail:home"]')).toBeNull();

  for (const name of ["contacts", "calendars", "files"]) {
    root.querySelector(`[data-feature="${name}"]`).click();
  }
  for (const name of ["mail", "contacts", "calendars", "tasks", "files", "notes", "argent", "bots", "github"]) {
    expect(root.querySelector(`[data-section="${name}"]`).hidden).toBe(true);
  }
  const bottom = [...root.querySelectorAll(".nav > .nav-buttons")[1].querySelectorAll("button")];
  expect(bottom.map((node) => node.getAttribute("aria-label"))).toEqual([
    "Grok Bots",
    "GitHub",
    "Activity",
    "Settings",
    "Update",
    "Help",
  ]);
  expect(bottom[0].hidden).toBe(true);
  expect(bottom[1].hidden).toBe(true);
  for (const button of bottom.slice(2)) {
    expect(button.hidden).toBe(false);
  }
  expect(root.querySelector('[data-nav="settings"]').hidden).toBe(false);

  root.querySelector('[data-feature="bots"]').click();
  const bots = root.querySelector('[data-section="bots"]');
  expect(bots.hidden).toBe(false);
  bots.click();
  expect(root.querySelector("#panel-bots").hidden).toBe(false);
  expect(root.querySelector('[data-nav="settings"]').hidden).toBe(false);
  expect(root.querySelector('[data-section="bots"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-section="settings"]').getAttribute("aria-pressed")).toBe("false");
  expect(root.querySelector("#bots-transcript").textContent).toContain("No bots yet.");
  const add = root.querySelector("#bots-add");
  expect(add.disabled).toBe(true);
  add.click();
  expect(root.querySelector("#bots-transcript").textContent).toContain("No bots yet.");
  expect(ids()).toContain("bots:home");

  root.querySelector('[data-feature="github"]').click();
  const github = root.querySelector('[data-section="github"]');
  expect(github.hidden).toBe(false);
  github.click();
  expect(root.querySelector("#panel-github").hidden).toBe(false);
  expect(root.querySelector("#panel-github").textContent).toContain("GitHub is not available yet.");
  expect(root.querySelector('[data-nav="settings"]').hidden).toBe(false);
  expect(root.querySelector('[data-section="github"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-section="settings"]').getAttribute("aria-pressed")).toBe("false");
  expect(root.querySelector('[data-nav="github"]')).toBeNull();
  expect(ids()).toContain("github:home");

  root.querySelector('[data-tab-id="bots:home"]').focus();
  key(root, "5", { ctrlKey: true });
  expect(root.querySelector('[data-tab-id="files:home"]')).toBeNull();
  expect(root.querySelector('[data-section="settings"]').getAttribute("aria-pressed")).toBe("false");

  root.querySelector('[data-feature="files"]').click();
  root.querySelector('[data-tab-id="settings:home"]').focus();
  key(root, "5", { ctrlKey: true });
  expect(root.querySelector('[data-section="files"]').getAttribute("aria-pressed")).toBe("true");
  expect(ids()).toContain("files:home");

  root.querySelector('[data-feature="tasks"]').click();
  root.querySelector('[data-tab-id="files:home"]').focus();
  key(root, "4", { ctrlKey: true });
  expect(root.querySelector("#panel-tasks").textContent).toContain("Tasks are not available yet.");
  expect(root.querySelector('[data-nav="tasks"]').hidden).toBe(false);

  root.querySelector('[data-feature="bots"]').click();
  expect(root.querySelector('[data-section="bots"]').hidden).toBe(true);
  expect(root.querySelector('[data-tab-id="bots:home"]')).toBeNull();
  root.querySelector('[data-tab-id="tasks:home"]').focus();
  key(root, "5", { ctrlKey: true });
  expect(root.querySelector('[data-tab-id="bots:home"]')).toBeNull();
});

test("a hidden Mail home is left out of restore", async () => {
  const root = await boot({
    features: { mail: false },
    session: {
      restore: true,
      shutdown: "clean",
      tabs: [
        {
          id: "mail:home",
          section: "mail",
          kind: "home",
          key: "",
          title: "Mail",
          icon: "",
          context: null,
          pinned: false,
          muted: false,
          order: 1,
          active: true,
        },
        {
          id: "settings:home",
          section: "settings",
          kind: "home",
          key: "",
          title: "Settings",
          icon: "",
          context: { heading: "features" },
          pinned: false,
          muted: false,
          order: 2,
          active: false,
        },
      ],
    },
  });
  expect(ids()).toEqual(["settings:home"]);
  expect(root.querySelector(".pav-toast")).toBeNull();
  expect(root.querySelector('[data-section="mail"]').hidden).toBe(true);
  expect(root.querySelector('[data-heading="features"]').getAttribute("aria-current")).toBe("true");
});

test("Mail off at launch opens the nav on Contacts", async () => {
  const root = await boot({ features: { mail: false } });
  expect(root.querySelector('[data-section="mail"]').hidden).toBe(true);
  expect(root.querySelector('[data-section="contacts"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-nav="contacts"]').hidden).toBe(false);
  expect(root.querySelector('[data-nav="mail"]').hidden).toBe(true);
  expect(root.querySelector('[data-tab-id="mail:home"]')).toBeNull();
});

test("GitHub is not the nav fallback when every top section is off", async () => {
  const root = await boot({
    features: {
      mail: false,
      contacts: false,
      calendars: false,
      tasks: false,
      files: false,
      notes: false,
      argent: false,
      github: true,
    },
  });
  expect(root.querySelector('[data-nav="settings"]').hidden).toBe(false);
  expect(root.querySelector('[data-section="settings"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-section="github"]').hidden).toBe(false);
  expect(root.querySelector('[data-section="github"]').getAttribute("aria-pressed")).toBe("false");
  expect(root.querySelector('[data-tab-id="github:home"]')).toBeNull();
  expect(root.querySelector('[data-nav="github"]')).toBeNull();
});

test("Settings has no checkbox, and Activity, Update, and Help start on", async () => {
  const root = await boot();
  const settings = root.querySelector('[data-rail-key="settings"]');
  expect(settings.querySelector("[data-feature]")).toBeNull();
  expect(settings.querySelector(".feature-label").textContent).toBe("Settings");
  expect(root.querySelector(".feature-divider").textContent).toBe("Bottom");
  expect(root.querySelector(".feature-side").textContent).toBe("Top");
  expect(root.querySelector('[data-rail-key="mail"] .feature-icon .icon').className).toContain("fa-paper-plane-top");
  expect(root.querySelector('[data-rail-key="tasks"]').classList.contains("is-off")).toBe(true);
  expect(root.querySelector('[data-rail-key="mail"]').classList.contains("is-off")).toBe(false);
  for (const name of ["activity", "update", "help"]) {
    expect(root.querySelector(`[data-feature="${name}"]`).checked).toBe(true);
    expect(root.querySelector(`[data-chord="${name}"]`).value).toBe("");
  }
  expect(root.querySelector('[data-chord="mail"]').value).toBe("1");
  expect(root.querySelector('[data-chord="argent"]').value).toBe("7");
});

test("hiding Activity closes its home, and everything but Settings can hide", async () => {
  const root = await boot();
  root.querySelector('[data-section="activity"]').click();
  expect(ids()).toContain("activity:home");
  root.querySelector('[data-feature="activity"]').click();
  expect(root.querySelector('[data-rail="activity"]').hidden).toBe(true);
  expect(root.querySelector('[data-tab-id="activity:home"]')).toBeNull();
  for (const box of root.querySelectorAll("[data-feature]")) {
    if (box.checked) {
      box.click();
    }
  }
  expect(root.querySelector('[data-rail="settings"]').hidden).toBe(false);
  expect(root.querySelector('[data-section="settings"]').getAttribute("aria-pressed")).toBe("true");
  for (const name of ["mail", "contacts", "calendars", "files", "update", "help"]) {
    expect(root.querySelector(`[data-rail="${name}"]`).hidden).toBe(true);
  }
});

test("a stored settings flag does not hide Settings", async () => {
  const root = await boot({
    features: {
      settings: false,
      mail: false,
      contacts: false,
      calendars: false,
      tasks: false,
      files: false,
      notes: false,
      argent: false,
    },
  });
  expect(root.querySelector('[data-rail="settings"]').hidden).toBe(false);
  expect(root.querySelector('[data-rail-key="settings"] [data-feature]')).toBeNull();
  expect(root.querySelector('[data-section="settings"]').getAttribute("aria-pressed")).toBe("true");
});

test("a saved order puts Settings on top and Calendars on the bottom", async () => {
  const root = await boot({
    rail: {
      top: ["settings", "mail", "contacts", "tasks", "files", "notes", "argent"],
      bottom: ["calendars", "bots", "github", "activity", "update", "help"],
    },
  });
  const bars = root.querySelectorAll(".nav > .nav-buttons");
  const top = [...bars[0].querySelectorAll("button")].map((node) => node.dataset.rail);
  const bottom = [...bars[1].querySelectorAll("button")].map((node) => node.dataset.rail);
  expect(top[0]).toBe("settings");
  expect(bottom[0]).toBe("calendars");
  expect(bottom[1]).toBe("bots");
});

test("dragging Calendars below the divider moves that button to the bottom rail", async () => {
  const root = await boot();
  const list = root.querySelector("#feature-list");
  const children = [...list.children];
  children.forEach((node, i) => {
    node.getBoundingClientRect = () => ({
      x: 0,
      y: i * 40,
      left: 0,
      top: i * 40,
      right: 200,
      bottom: (i + 1) * 40,
      width: 200,
      height: 40,
      toJSON() {
        return {};
      },
    });
  });
  const row = list.querySelector('[data-rail-key="calendars"]');
  const pointer = (type, clientY) => new PointerEvent(type, {
    bubbles: true,
    cancelable: true,
    clientY,
    clientX: 8,
    button: 0,
    pointerId: 1,
  });
  const grip = row.querySelector(".feature-grip");
  grip.dispatchEvent(pointer("pointerdown", 90));
  grip.dispatchEvent(pointer("pointermove", 1000));
  grip.dispatchEvent(pointer("pointerup", 1000));
  const bottom = root.querySelectorAll(".nav > .nav-buttons")[1];
  const labels = [...bottom.querySelectorAll("button")].map((node) => node.dataset.rail);
  expect(labels[labels.length - 1]).toBe("calendars");
  expect(root.querySelector('[data-rail="calendars"]').parentElement).toBe(bottom);
  const order = [...list.children].map((node) => node.dataset.railKey || "divider");
  expect(order.indexOf("calendars")).toBeGreaterThan(order.indexOf("divider"));
});

function box(node, x, y, w, h) {
  node.getBoundingClientRect = () => ({
    x,
    y,
    left: x,
    top: y,
    right: x + w,
    bottom: y + h,
    width: w,
    height: h,
    toJSON() {
      return {};
    },
  });
}

function pointerAt(type, clientX, clientY) {
  return new PointerEvent(type, {
    bubbles: true,
    cancelable: true,
    clientX,
    clientY,
    button: 0,
    pointerId: 1,
  });
}

test("dragging a rail icon left reorders that bar and does not open it", async () => {
  const root = await boot();
  const bars = root.querySelectorAll(".nav > .nav-buttons");
  box(bars[0], 0, 0, 200, 40);
  box(bars[1], 0, 400, 200, 40);
  const visible = [...bars[0].querySelectorAll("button")].filter((node) => !node.hidden);
  visible.forEach((node, i) => box(node, i * 40, 0, 40, 40));
  const files = root.querySelector('[data-rail="files"]');
  files.dispatchEvent(pointerAt("pointerdown", 130, 20));
  files.dispatchEvent(pointerAt("pointermove", 10, 20));
  files.dispatchEvent(pointerAt("pointerup", 10, 20));
  files.dispatchEvent(new MouseEvent("click", { bubbles: true, cancelable: true }));
  const top = [...bars[0].querySelectorAll("button")].map((node) => node.dataset.rail);
  expect(top[0]).toBe("files");
  expect(top.indexOf("tasks")).toBeGreaterThan(top.indexOf("calendars"));
  expect(root.querySelector('[data-section="mail"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-section="files"]').getAttribute("aria-pressed")).toBe("false");
  expect(root.querySelector(".rail-drop")).toBeNull();
  const order = [...root.querySelector("#feature-list").children].map((node) => node.dataset.railKey || node.textContent);
  expect(order[0]).toBe("Top");
  expect(order.indexOf("files")).toBeLessThan(order.indexOf("mail"));
  expect(root.querySelector('[data-chord="files"]').value).toBe("5");
});

test("dragging a rail icon onto the other bar keeps hidden buttons in place", async () => {
  const root = await boot();
  const bars = root.querySelectorAll(".nav > .nav-buttons");
  box(bars[0], 0, 0, 200, 40);
  box(bars[1], 0, 400, 200, 40);
  const topVisible = [...bars[0].querySelectorAll("button")].filter((node) => !node.hidden);
  const bottomVisible = [...bars[1].querySelectorAll("button")].filter((node) => !node.hidden);
  topVisible.forEach((node, i) => box(node, i * 40, 0, 40, 40));
  bottomVisible.forEach((node, i) => box(node, i * 40, 400, 40, 40));
  const calendars = root.querySelector('[data-rail="calendars"]');
  calendars.dispatchEvent(pointerAt("pointerdown", 90, 20));
  calendars.dispatchEvent(pointerAt("pointermove", 10, 420));
  calendars.dispatchEvent(pointerAt("pointerup", 10, 420));
  const bottom = [...bars[1].querySelectorAll("button")].map((node) => node.dataset.rail);
  expect(bottom.slice(0, 3)).toEqual(["bots", "github", "calendars"]);
  expect(root.querySelector('[data-rail="calendars"]').parentElement).toBe(bars[1]);
  expect([...bars[0].querySelectorAll("button")].some((node) => node.dataset.rail === "calendars")).toBe(false);
  const order = [...root.querySelector("#feature-list").children].map((node) => node.dataset.railKey || "divider");
  expect(order.indexOf("calendars")).toBeGreaterThan(order.indexOf("divider"));
  expect(order.indexOf("github")).toBeLessThan(order.indexOf("calendars"));
});

test("a rail press under 4 px does not reorder", async () => {
  const root = await boot();
  const bars = root.querySelectorAll(".nav > .nav-buttons");
  box(bars[0], 0, 0, 200, 40);
  box(bars[1], 0, 400, 200, 40);
  const files = root.querySelector('[data-rail="files"]');
  const before = [...bars[0].querySelectorAll("button")].map((node) => node.dataset.rail);
  files.dispatchEvent(pointerAt("pointerdown", 130, 20));
  files.dispatchEvent(pointerAt("pointermove", 132, 21));
  files.dispatchEvent(pointerAt("pointerup", 132, 21));
  expect([...bars[0].querySelectorAll("button")].map((node) => node.dataset.rail)).toEqual(before);
  files.dispatchEvent(new MouseEvent("click", { bubbles: true, cancelable: true }));
  expect(root.querySelector('[data-section="files"]').getAttribute("aria-pressed")).toBe("true");
});

test("a shortcut can move to another feature, and a hidden one does nothing", async () => {
  const root = await boot();
  const contacts = root.querySelector('[data-chord="contacts"]');
  contacts.value = "1";
  contacts.dispatchEvent(new Event("change", { bubbles: true }));
  expect(root.querySelector('[data-chord="mail"]').value).toBe("");
  expect(contacts.value).toBe("1");
  root.querySelector('[data-tab-id="mail:home"]').focus();
  key(root, "1", { ctrlKey: true });
  expect(ids()).toContain("contacts:home");
  expect(root.querySelector('[data-section="contacts"]').getAttribute("aria-pressed")).toBe("true");

  const mail = root.querySelector('[data-chord="mail"]');
  mail.value = "2";
  mail.dispatchEvent(new Event("change", { bubbles: true }));
  expect(contacts.value).toBe("1");
  expect(mail.value).toBe("2");
  root.querySelector('[data-feature="mail"]').click();
  root.querySelector('[data-tab-id="contacts:home"]').focus();
  key(root, "2", { ctrlKey: true });
  expect(root.querySelector('[data-tab-id="mail:home"]')).toBeNull();
  expect(root.querySelector('[data-section="contacts"]').getAttribute("aria-pressed")).toBe("true");
});

test("a GitHub shortcut opens the home and leaves the nav", async () => {
  const root = await boot();
  const github = root.querySelector('[data-chord="github"]');
  github.value = "8";
  github.dispatchEvent(new Event("change", { bubbles: true }));
  root.querySelector('[data-tab-id="mail:home"]').focus();
  key(root, "8", { ctrlKey: true });
  expect(root.querySelector('[data-tab-id="github:home"]')).toBeNull();
  root.querySelector('[data-feature="github"]').click();
  root.querySelector('[data-tab-id="mail:home"]').focus();
  key(root, "8", { ctrlKey: true });
  expect(ids()).toContain("github:home");
  expect(root.querySelector('[data-nav="mail"]').hidden).toBe(false);
  expect(root.querySelector('[data-section="github"]').getAttribute("aria-pressed")).toBe("true");
  expect(root.querySelector('[data-section="mail"]').getAttribute("aria-pressed")).toBe("false");
});

test("an Update shortcut runs sync and does nothing while Update is hidden", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const transport = mockTransport();
  const seen = [];
  const call = transport.call.bind(transport);
  transport.call = async (cmd, args) => {
    seen.push(cmd);
    return call(cmd, args);
  };
  const api = createApi(transport);
  await mount(el, api, { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  const update = root.querySelector('[data-chord="update"]');
  update.value = "9";
  update.dispatchEvent(new Event("change", { bubbles: true }));
  root.querySelector('[data-feature="update"]').click();
  root.querySelector('[data-tab-id="mail:home"]').focus();
  key(root, "9", { ctrlKey: true });
  expect(seen.includes("sync.now")).toBe(false);
  expect(root.querySelector('[data-tab-id="update:home"]')).toBeNull();
  root.querySelector('[data-feature="update"]').click();
  root.querySelector('[data-tab-id="mail:home"]').focus();
  key(root, "9", { ctrlKey: true });
  expect(seen.includes("sync.now")).toBe(true);
  expect(root.querySelector('[data-tab-id="update:home"]')).toBeNull();
});

test("Add explains itself in a tooltip", async () => {
  const root = await boot({ features: { bots: true } });
  root.querySelector('[data-section="bots"]').click();
  vi.useFakeTimers();
  const host = root.querySelector("#bots-add-host");
  host.dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
  await vi.advanceTimersByTimeAsync(500);
  const tip = root.querySelector(".pav-tooltip");
  expect(tip.hidden).toBe(false);
  expect(tip.textContent).toBe("A bot cannot be connected yet.");
});
