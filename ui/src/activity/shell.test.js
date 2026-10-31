/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test, vi } from "vitest";
import { createApi } from "../api.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";

afterEach(() => {
  unmount();
  document.body.replaceChildren();
  delete window.matchMedia;
});

function running(kind) {
  return {
    id: "act-9",
    kind,
    accountId: null,
    title: "Demo activity",
    detail: null,
    state: "running",
    startedMs: Date.parse("2026-10-14T17:04:01.123Z"),
    endedMs: null,
    progress: { done: 1, total: 4, unit: "steps" },
    etaMs: 5000,
    cancellable: true,
    error: null,
    parentId: null,
    dismissed: false,
  };
}

test("Update calls sync.now and shows the pie before a sync activity is running", async () => {
  const transport = mockTransport();
  const el = document.createElement("div");
  document.body.append(el);
  await mount(el, createApi(transport), { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  const update = root.querySelector("#update");
  expect(update.getAttribute("aria-label")).toBe("Update");
  expect(update.classList.contains("is-busy")).toBe(false);
  expect(root.querySelector("#demo-activity").hidden).toBe(true);
  expect(root.querySelector("#open-test-tab").hidden).toBe(true);
  expect(root.querySelector(".activity-account-name")).toBeNull();

  update.click();
  await Promise.resolve();
  expect(transport.seen.some((item) => item.cmd === "sync.now" && item.args.accountId === undefined)).toBe(true);
  expect(update.classList.contains("is-busy")).toBe(false);
  expect(update.querySelector(".update-pie").hidden).toBe(false);

  transport.emit("activity.changed", { activity: running("sync.mail") });
  expect(update.classList.contains("is-busy")).toBe(true);
  expect(update.getAttribute("aria-label")).toBe("1 syncing, 25%");
  expect(root.querySelector(".update-badge").hidden).toBe(true);
  expect(update.querySelector(".update-pie").hidden).toBe(false);
  const account = root.querySelector(".activity-account-name");
  expect(account.textContent).toBe("Pavillion");
  expect(root.querySelector(".activity-account-num").textContent).toBe("000");
});

test("a sync the Update button did not start leaves the status line empty", async () => {
  const transport = mockTransport();
  const el = document.createElement("div");
  document.body.append(el);
  await mount(el, createApi(transport), { hostServices: { setRootFontSize() {} } });
  const message = el.querySelector("#tabs-message");
  transport.emit("activity.changed", { activity: running("sync.mail") });
  transport.emit("activity.changed", { activity: { ...running("sync.mail"), state: "done" } });
  expect(message.hidden).toBe(true);
  expect(message.textContent).toBe("");
});

test("Update posts started, then the elapsed time when that sync finishes", async () => {
  const transport = mockTransport();
  const el = document.createElement("div");
  document.body.append(el);
  await mount(el, createApi(transport), { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  const message = root.querySelector("#tabs-message");
  const update = root.querySelector("#update");
  const started = Date.now();
  const clock = vi.spyOn(Date, "now").mockReturnValue(started);
  try {
    update.click();
    await Promise.resolve();
    expect(message.hidden).toBe(false);
    expect(message.classList.contains("is-shown")).toBe(true);
    expect(message.textContent).toBe("Update Started");

    transport.emit("activity.changed", { activity: running("sync.mail") });
    clock.mockReturnValue(started + 184000);
    transport.emit("activity.changed", { activity: { ...running("sync.mail"), state: "done" } });
    expect(message.textContent).toBe("Updated in 3m4s");
    expect(message.classList.contains("is-shown")).toBe(true);
  } finally {
    clock.mockRestore();
  }
});

test("stopping Update does not post Updated", async () => {
  const transport = mockTransport();
  const el = document.createElement("div");
  document.body.append(el);
  await mount(el, createApi(transport), { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  const message = root.querySelector("#tabs-message");
  const update = root.querySelector("#update");

  update.click();
  await Promise.resolve();
  expect(message.textContent).toBe("Update Started");
  transport.emit("activity.changed", { activity: running("sync.mail") });
  update.click();
  transport.emit("activity.changed", { activity: { ...running("sync.mail"), state: "done" } });
  await Promise.resolve();
  expect(message.textContent).toBe("Update stopped");
  expect(root.querySelector("#banner").hidden).toBe(true);
});

test("a failed Update does not later say Updated", async () => {
  const transport = mockTransport();
  const call = transport.call;
  transport.call = async (cmd, args) => {
    if (cmd === "sync.now") {
      throw new Error("offline");
    }
    return call(cmd, args);
  };
  const el = document.createElement("div");
  document.body.append(el);
  await mount(el, createApi(transport), { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  const message = root.querySelector("#tabs-message");
  root.querySelector("#update").click();
  await Promise.resolve();
  await Promise.resolve();
  expect(message.textContent).toBe("Update Started");
  expect(root.querySelector("#banner").textContent).toBe("offline");
  transport.emit("activity.changed", { activity: running("sync.mail") });
  transport.emit("activity.changed", { activity: { ...running("sync.mail"), state: "done" } });
  expect(message.textContent).toBe("Update Started");
});

test("the activity page uses action and account menus instead of the log filters", async () => {
  const transport = mockTransport();
  const el = document.createElement("div");
  document.body.append(el);
  await mount(el, createApi(transport), { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  const severity = root.querySelector('[aria-label="Severity"]');
  const subsystem = root.querySelector('[aria-label="Subsystem"]');
  const search = root.querySelector('.log-topbar input[type="search"]');
  const gear = root.querySelector(".log-gear");
  const action = root.querySelector('[aria-label="Action"]');
  const account = root.querySelector('[aria-label="Account"]');
  const options = root.querySelector('[aria-label="Activity options"]');
  expect(severity.closest(".log-menu").hidden).toBe(false);
  expect(severity.textContent).toBe("3 severities");
  expect(action.closest(".log-menu").hidden).toBe(true);
  expect(root.querySelector(".log-topbar").getAttribute("aria-label")).toBe("Log tools");

  root.querySelector('[data-activity-view="activity"]').click();
  expect(severity.closest(".log-menu").hidden).toBe(true);
  expect(subsystem.closest(".log-menu").hidden).toBe(true);
  expect(search.hidden).toBe(true);
  expect(gear.closest(".log-menu").hidden).toBe(true);
  expect(action.closest(".log-menu").hidden).toBe(false);
  expect(action.textContent).toBe("All actions");
  expect(account.closest(".log-menu").hidden).toBe(false);
  expect(account.textContent).toBe("All accounts");
  expect(options.textContent).toBe("Options");
  expect([...root.querySelectorAll(".log-menu-action")].map((button) => button.textContent)).toEqual([
    "Stop all",
    "Clear all",
    "Expand all",
    "Collapse all",
  ]);
  expect(root.querySelector(".log-topbar").getAttribute("aria-label")).toBe("Activity tools");
  expect(root.querySelector(".activity-bar")).toBeNull();

  root.querySelector('[data-activity-view="log"]').click();
  expect(severity.closest(".log-menu").hidden).toBe(false);
  expect(search.hidden).toBe(false);
  expect(action.closest(".log-menu").hidden).toBe(true);
});

test("reduced motion shows the sync count and debug Help can run the demo", async () => {
  window.matchMedia = () => ({
    matches: true,
    addEventListener() {},
    removeEventListener() {},
  });
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
  const demo = root.querySelector("#demo-activity");
  expect(demo.hidden).toBe(false);
  demo.click();
  await Promise.resolve();
  expect(transport.seen.some((item) => item.cmd === "debug.activity_demo" && item.args.steps === 8 && item.args.ms === 400)).toBe(true);

  transport.emit("activity.changed", { activity: running("sync.calendar") });
  const update = root.querySelector("#update");
  expect(update.classList.contains("is-busy")).toBe(true);
  expect(root.querySelector(".update-badge").hidden).toBe(false);
  expect(root.querySelector(".update-badge").textContent).toBe("1");
  expect(update.getAttribute("aria-label")).toBe("1 syncing, 25%");
  expect(update.querySelector(".update-pie").hidden).toBe(false);
});

test("Update again cancels the running sync and hides the pie", async () => {
  const transport = mockTransport();
  const el = document.createElement("div");
  document.body.append(el);
  await mount(el, createApi(transport), { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  const update = root.querySelector("#update");
  const pie = update.querySelector(".update-pie");

  update.click();
  await Promise.resolve();
  expect(pie.hidden).toBe(false);
  update.click();
  expect(pie.hidden).toBe(true);
  expect(transport.seen.some((item) => item.cmd === "activity.cancel")).toBe(false);

  transport.emit("activity.changed", { activity: running("sync.mail") });
  await Promise.resolve();
  await Promise.resolve();
  expect(transport.seen.some((item) => item.cmd === "activity.cancel" && item.args.id === "act-9")).toBe(true);
  expect(pie.hidden).toBe(true);
  expect(root.querySelector("#banner").hidden).toBe(true);
  expect(update.getAttribute("aria-label")).toBe("1 syncing, 25%");
});
