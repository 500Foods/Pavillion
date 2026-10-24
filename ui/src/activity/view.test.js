/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createActivityView } from "./view.js";

function row(extra) {
  return {
    id: "act-1",
    kind: "sync.mail",
    accountId: null,
    title: "Demo activity",
    detail: null,
    state: "running",
    startedMs: Date.parse("2026-10-14T17:04:01.123Z"),
    endedMs: null,
    progress: { done: 1, total: 8, unit: "steps" },
    etaMs: 5000,
    cancellable: true,
    error: null,
    parentId: null,
    dismissed: false,
    ...extra,
  };
}

function harness(initial, options) {
  const store = initial.map((item) => ({ ...item }));
  let listener = null;
  const calls = [];
  const api = {
    activity: {
      list({ includeHistory }) {
        calls.push(includeHistory);
        const activities = store.filter((item) => {
          if (item.state === "running" || item.state === "queued") return true;
          if (item.state === "failed" && item.dismissed !== true) return true;
          return includeHistory === true;
        });
        return Promise.resolve({ activities });
      },
      cancel(id) {
        calls.push(["cancel", id]);
        return Promise.resolve({ id });
      },
      dismiss(id) {
        calls.push(["dismiss", id]);
        const item = store.find((entry) => entry.id === id);
        if (item) item.dismissed = true;
        return Promise.resolve({ ...item });
      },
    },
    on(name, fn) {
      if (name === "activity.changed") listener = fn;
      return () => {
        listener = null;
      };
    },
  };
  const pane = document.createElement("div");
  document.body.append(pane);
  const view = createActivityView(pane, api, { timeZone: "America/Vancouver", ...(options || {}) });
  return {
    pane,
    view,
    calls,
    store,
    emit(activity) {
      const index = store.findIndex((item) => item.id === activity.id);
      if (index >= 0) {
        store[index] = { ...activity };
      } else {
        store.push({ ...activity });
      }
      listener({ activity });
    },
  };
}

test("a demo row keeps its element, cancels, and shows the app account", async () => {
  const { pane, view, calls, emit } = harness([row({})]);
  await view.ready();
  expect([...pane.querySelectorAll("th")].map((cell) => cell.textContent)[1]).toBe("Action");
  const tr = pane.querySelector('[data-activity-id="act-1"]');
  expect(tr.querySelector('[data-field="started"]').textContent).toBe("10:04:01.123");
  expect(tr.querySelector(".activity-kind-label").textContent).toBe("Mail sync");
  expect(tr.querySelector(".activity-account-num").textContent).toBe("000");
  expect(tr.querySelector(".activity-account-name").textContent).toBe("Pavillion");
  expect(tr.querySelector(".activity-account .icon").className).toContain("fa-paper-plane-top");
  expect(tr.querySelector(".activity-progress-text").textContent).toBe("1/8");
  expect(tr.querySelector('[data-field="eta"]').textContent).toBe("in 5s");
  expect(tr.querySelector(".activity-cancel").getAttribute("aria-label")).toBe("Cancel Demo activity");
  expect(tr.querySelector("progress").value).toBe(1);

  emit(row({ progress: { done: 4, total: 8, unit: "steps" }, detail: "step 4", etaMs: 120000 }));
  expect(pane.querySelector('[data-activity-id="act-1"]')).toBe(tr);
  expect(tr.querySelector(".activity-progress-text").textContent).toBe("4/8");
  expect(tr.querySelector(".activity-detail").textContent).toBe("step 4");
  expect(tr.querySelector('[data-field="eta"]').textContent).toBe("2m");

  tr.querySelector(".activity-cancel").click();
  expect(calls).toContainEqual(["cancel", "act-1"]);
  expect(pane.querySelector('[data-activity-id="act-1"]')).toBe(tr);

  emit(row({ state: "cancelled", endedMs: Date.parse("2026-10-14T17:04:05.000Z"), etaMs: null }));
  expect(pane.querySelector('[data-activity-id="act-1"]')).toBeNull();
  expect(pane.querySelector(".empty").hidden).toBe(false);

  pane.querySelector(".activity-history input").click();
  await view.ready();
  const history = pane.querySelector('[data-activity-id="act-1"]');
  expect(history.querySelector('[data-field="state"]').textContent).toBe("Cancelled");
  expect(history.querySelector('[data-field="state"]').className).toContain("activity-state-cancelled");
  view.destroy();
  expect(pane.childNodes).toHaveLength(0);
});

test("children stay folded, a failure can be dismissed, and an account shows all three parts", async () => {
  const parent = row({ id: "act-1", title: "Sync", accountId: "a-500foods-andrew" });
  const child = row({
    id: "act-2",
    kind: "body.fetch",
    title: "Fetch body",
    parentId: "act-1",
    cancellable: false,
    accountId: null,
  });
  const failed = row({
    id: "act-3",
    kind: "import",
    title: "Import",
    state: "failed",
    error: "disk full",
    cancellable: false,
    progress: null,
    etaMs: null,
  });
  const { pane, view, calls } = harness([parent, child, failed]);
  await view.ready();
  view.setAccounts([
    { id: "a-500foods-andrew", number: "001", name: "Andrew", icon: "fa-sharp fa-thin fa-envelope" },
  ]);
  const parentRow = pane.querySelector('[data-activity-id="act-1"]');
  const childRow = pane.querySelector('[data-activity-id="act-2"]');
  expect(parentRow.querySelector(".activity-account-num").textContent).toBe("001");
  expect(parentRow.querySelector(".activity-account-name").textContent).toBe("Andrew");
  expect(parentRow.querySelector(".activity-account .icon").className).toContain("fa-envelope");
  expect(parentRow.querySelector(".activity-fold").getAttribute("aria-label")).toBe("Show Sync");
  expect(parentRow.querySelector(".activity-fold").textContent).toBe("▸");
  expect(childRow.hidden).toBe(true);
  expect(childRow.dataset.child).toBe("true");
  parentRow.querySelector(".activity-fold").click();
  expect(childRow.hidden).toBe(false);
  expect(pane.querySelector('[data-activity-id="act-2"]').querySelector(".activity-kind-label").textContent).toBe("Fetch body");
  const collapse = [...pane.querySelectorAll(".log-menu-action")].find((button) => button.textContent === "Collapse all");
  collapse.click();
  expect(childRow.hidden).toBe(true);
  const expand = [...pane.querySelectorAll(".log-menu-action")].find((button) => button.textContent === "Expand all");
  expand.click();
  expect(childRow.hidden).toBe(false);

  const failedRow = pane.querySelector('[data-activity-id="act-3"]');
  expect(failedRow.querySelector(".activity-error").textContent).toBe("disk full");
  expect(failedRow.querySelector(".activity-dismiss").getAttribute("aria-label")).toBe("Dismiss Import");
  failedRow.querySelector(".activity-dismiss").click();
  await Promise.resolve();
  expect(calls).toContainEqual(["dismiss", "act-3"]);
  expect(pane.querySelector('[data-activity-id="act-3"]')).toBeNull();
  view.destroy();
});

test("action and account menus filter rows, and options stop or clear them", async () => {
  const topbar = document.createElement("div");
  document.body.append(topbar);
  const saved = [];
  const parent = row({ id: "act-1", title: "Sync", accountId: "a-1" });
  const child = row({
    id: "act-2",
    kind: "body.fetch",
    title: "Body",
    parentId: "act-1",
    cancellable: false,
    accountId: null,
  });
  const failed = row({
    id: "act-3",
    kind: "import",
    title: "Import",
    state: "failed",
    error: "disk full",
    cancellable: false,
    progress: null,
    etaMs: null,
    accountId: null,
  });
  const done = row({
    id: "act-4",
    kind: "export",
    title: "Export",
    state: "done",
    cancellable: false,
    progress: null,
    etaMs: null,
    accountId: null,
  });
  const { pane, view, calls } = harness([parent, child, failed, done], {
    topbar,
    onHistory(value) {
      saved.push(value);
    },
  });
  await view.ready();
  view.setShown(true);
  const action = topbar.querySelector('[aria-label="Action"]');
  const account = topbar.querySelector('[aria-label="Account"]');
  expect(action.textContent).toBe("All actions");
  expect(account.textContent).toBe("All accounts");
  expect(action.closest(".log-menu").hidden).toBe(false);
  view.setShown(false);
  expect(action.closest(".log-menu").hidden).toBe(true);
  view.setShown(true);

  const mail = [...topbar.querySelectorAll(".log-check")].find((label) => label.textContent.includes("Mail sync"));
  mail.querySelector("input").click();
  expect(pane.querySelector('[data-activity-id="act-1"]').hidden).toBe(true);
  expect(pane.querySelector('[data-activity-id="act-2"]').hidden).toBe(false);
  expect(action.textContent).toBe("2 actions");

  const pavillion = [...topbar.querySelectorAll(".log-check")].find((label) => label.textContent.includes("000 Pavillion"));
  pavillion.querySelector("input").click();
  expect(pane.querySelector('[data-activity-id="act-2"]').hidden).toBe(true);
  expect(account.textContent).toBe("— a-1");
  expect(pane.querySelector(".empty").textContent).toBe("No matching activity.");

  function allIn(button) {
    const panel = button.parentElement.querySelector(".log-menu-panel");
    const label = [...panel.querySelectorAll(".log-check")].find((item) => item.textContent === " All");
    return label.querySelector("input");
  }
  allIn(action).click();
  allIn(account).click();
  expect(action.textContent).toBe("All actions");
  expect(account.textContent).toBe("All accounts");
  expect(pane.querySelector('[data-activity-id="act-1"]').hidden).toBe(false);
  expect(pane.querySelector('[data-activity-id="act-2"]').hidden).toBe(true);

  topbar.querySelector(".activity-history input").click();
  await view.ready();
  expect(pane.querySelector('[data-activity-id="act-4"]').querySelector('[data-field="state"]').textContent).toBe("Done");
  expect(saved).toEqual([true]);

  const stop = [...topbar.querySelectorAll(".log-menu-action")].find((button) => button.textContent === "Stop all");
  expect(stop.disabled).toBe(false);
  stop.click();
  expect(calls).toContainEqual(["cancel", "act-1"]);

  const clear = [...topbar.querySelectorAll(".log-menu-action")].find((button) => button.textContent === "Clear all");
  clear.click();
  for (let i = 0; i < 6; i += 1) {
    await Promise.resolve();
  }
  await view.ready();
  expect(calls).toContainEqual(["dismiss", "act-3"]);
  expect(saved).toEqual([true, false]);
  expect(pane.querySelector('[data-activity-id="act-3"]')).toBeNull();
  expect(pane.querySelector('[data-activity-id="act-4"]')).toBeNull();
  expect(pane.querySelector('[data-activity-id="act-1"]')).not.toBeNull();
  view.destroy();
  expect(topbar.childNodes).toHaveLength(0);
  topbar.remove();
});
