import {
  accountFace,
  activityMatches,
  formatEta,
  kindFace,
  listedActivity,
  localClock,
  preferActivity,
  progressText,
  stateLabel,
  syncingCount,
} from "./format.js";

function text(el, value) {
  el.textContent = value == null ? "" : String(value);
}

function paintIcon(slot, className) {
  const next = document.createElement("i");
  next.className = `icon ${className}`;
  next.setAttribute("aria-hidden", "true");
  slot.replaceWith(next);
  return next;
}

function idOrder(left, right) {
  if (left.id < right.id) return -1;
  if (left.id > right.id) return 1;
  return 0;
}

/* Roots in id order, then each parent's children, also in id order. */
export function layoutActivities(records) {
  const ids = new Set(records.keys());
  const kids = new Map();
  const roots = [];
  for (const activity of records.values()) {
    const parent = typeof activity.parentId === "string" && ids.has(activity.parentId) ? activity.parentId : null;
    if (!parent) {
      roots.push(activity);
      continue;
    }
    if (!kids.has(parent)) {
      kids.set(parent, []);
    }
    kids.get(parent).push(activity);
  }
  roots.sort(idOrder);
  const out = [];
  function walk(activity) {
    out.push(activity);
    const nested = kids.get(activity.id);
    if (!nested) {
      return;
    }
    nested.sort(idOrder);
    for (const child of nested) {
      walk(child);
    }
  }
  for (const root of roots) {
    walk(root);
  }
  return out;
}

export function createActivityView(pane, api, options) {
  const opts = options || {};
  let timeZone = opts.timeZone || "America/Vancouver";
  let history = opts.history === true;
  let accounts = Array.isArray(opts.accounts) ? opts.accounts : [];
  const records = new Map();
  const rowNodes = new Map();
  const open = new Set();
  const pending = [];
  let generation = 0;
  let loading = false;
  let ready = Promise.resolve();
  let stopped = false;
  let kindPick = null;
  let accountPick = null;
  let kindKey = null;
  let accountKey = null;

  pane.replaceChildren();
  const root = document.createElement("div");
  root.className = "activity-view";

  const scroll = document.createElement("div");
  scroll.className = "activity-scroll";
  const table = document.createElement("table");
  table.className = "activity-table";
  const caption = document.createElement("caption");
  caption.className = "activity-sr";
  text(caption, "Activity");
  const head = document.createElement("thead");
  const headRow = document.createElement("tr");
  for (const label of ["Started", "Action", "Account", "Title", "Progress", "ETA", "State", "Actions"]) {
    const cell = document.createElement("th");
    cell.scope = "col";
    text(cell, label);
    if (label === "Actions") {
      cell.className = "activity-sr";
    }
    headRow.append(cell);
  }
  head.append(headRow);
  const tbody = document.createElement("tbody");
  table.append(caption, head, tbody);
  const empty = document.createElement("p");
  empty.className = "empty";
  text(empty, "No activity.");
  scroll.append(table, empty);
  root.append(scroll);
  pane.append(root);

  const menus = [];
  const chromeNodes = [];

  function closeMenus() {
    for (const menu of menus) {
      menu.panel.hidden = true;
      menu.button.setAttribute("aria-expanded", "false");
    }
  }

  function addMenu(align) {
    const wrap = document.createElement("div");
    wrap.className = "log-menu";
    const button = document.createElement("button");
    button.type = "button";
    button.className = "log-menu-button";
    button.setAttribute("aria-haspopup", "true");
    button.setAttribute("aria-expanded", "false");
    const panel = document.createElement("div");
    panel.className = "log-menu-panel";
    panel.hidden = true;
    if (align === "end") {
      panel.dataset.align = "end";
    }
    button.addEventListener("click", (event) => {
      event.stopPropagation();
      const open = panel.hidden;
      closeMenus();
      if (open) {
        panel.hidden = false;
        button.setAttribute("aria-expanded", "true");
      }
    });
    wrap.append(button, panel);
    menus.push({ button, panel });
    chromeNodes.push(wrap);
    return { root: wrap, button, panel };
  }

  function checkRow(labelText, onChange) {
    const label = document.createElement("label");
    label.className = "log-check";
    const box = document.createElement("input");
    box.type = "checkbox";
    box.addEventListener("change", () => onChange(box));
    label.append(box, document.createTextNode(` ${labelText}`));
    return { label, box };
  }

  function rule() {
    const line = document.createElement("div");
    line.className = "log-menu-rule";
    return line;
  }

  function menuAction(labelText, onClick) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "log-menu-action";
    text(button, labelText);
    button.addEventListener("click", () => {
      closeMenus();
      onClick();
    });
    return button;
  }

  const kindMenu = addMenu();
  kindMenu.button.setAttribute("aria-label", "Action");
  text(kindMenu.button, "All actions");
  const kindAll = checkRow("All", (box) => {
    kindPick = box.checked ? null : [];
    paint();
  });
  const kindNone = checkRow("None", (box) => {
    kindPick = box.checked ? [] : null;
    paint();
  });
  const kindItems = document.createElement("div");
  kindMenu.panel.append(kindAll.label, kindNone.label, rule(), kindItems);

  const accountMenu = addMenu();
  accountMenu.button.setAttribute("aria-label", "Account");
  text(accountMenu.button, "All accounts");
  const accountAll = checkRow("All", (box) => {
    accountPick = box.checked ? null : [];
    paint();
  });
  const accountNone = checkRow("None", (box) => {
    accountPick = box.checked ? [] : null;
    paint();
  });
  const accountItems = document.createElement("div");
  accountMenu.panel.append(accountAll.label, accountNone.label, rule(), accountItems);

  const spacer = document.createElement("div");
  spacer.className = "log-bar-spacer";
  chromeNodes.push(spacer);

  const optionsMenu = addMenu("end");
  optionsMenu.button.setAttribute("aria-label", "Activity options");
  text(optionsMenu.button, "Options");
  const historyLabel = document.createElement("label");
  historyLabel.className = "log-check activity-history";
  const historyBox = document.createElement("input");
  historyBox.type = "checkbox";
  historyBox.checked = history;
  historyLabel.append(historyBox, document.createTextNode(" Show history"));
  const stopAll = menuAction("Stop all", () => {
    for (const id of cancellableIds()) {
      api.activity.cancel(id).catch((err) => fail(err));
    }
  });
  /* Clear all dismisses failures and, when history is open, hides finished
     rows. The profile keeps them; Show history brings them back. */
  const clearAll = menuAction("Clear all", () => {
    const ids = failedIds();
    const hideHistory = history === true;
    Promise.all(ids.map((id) => api.activity.dismiss(id).then((result) => {
      if (result && result.id) {
        accept(result);
      }
    }))).then(() => {
      if (!hideHistory || !history) {
        return undefined;
      }
      history = false;
      historyBox.checked = false;
      if (typeof opts.onHistory === "function") {
        opts.onHistory(false);
      }
      return reload();
    }).catch((err) => fail(err));
  });
  const expandAll = menuAction("Expand all", () => {
    for (const activity of records.values()) {
      if (hasChildren(activity.id)) {
        open.add(activity.id);
      }
    }
    paint();
  });
  const collapseAll = menuAction("Collapse all", () => {
    open.clear();
    paint();
  });
  optionsMenu.panel.append(historyLabel, rule(), stopAll, clearAll, rule(), expandAll, collapseAll);
  stopAll.disabled = true;
  clearAll.disabled = true;
  expandAll.disabled = true;
  collapseAll.disabled = true;

  const shared = opts.topbar instanceof Element;
  if (shared) {
    for (const el of chromeNodes) {
      el.hidden = true;
    }
    opts.topbar.append(...chromeNodes);
  } else {
    const bar = document.createElement("div");
    bar.className = "button-bar activity-bar";
    bar.setAttribute("role", "toolbar");
    bar.setAttribute("aria-label", "Activity");
    bar.append(...chromeNodes);
    root.insertBefore(bar, scroll);
  }

  function onPointerDown(event) {
    const target = event.target;
    if (target instanceof Element && target.closest(".log-menu")) {
      return;
    }
    closeMenus();
  }

  function onEscape(event) {
    if (event.key === "Escape") {
      closeMenus();
    }
  }

  const listenRoot = opts.root instanceof EventTarget ? opts.root : null;
  if (listenRoot) {
    listenRoot.addEventListener("pointerdown", onPointerDown);
    listenRoot.addEventListener("keydown", onEscape);
  }

  function setShown(shown) {
    if (!shared) {
      return;
    }
    for (const el of chromeNodes) {
      el.hidden = shown !== true;
    }
    if (shown !== true) {
      closeMenus();
    }
  }

  function report() {
    if (stopped || typeof opts.onSync !== "function") {
      return;
    }
    opts.onSync(syncingCount(records.values()));
  }

  function filterVisible(activity) {
    return activityMatches(activity, kindPick, accountPick);
  }

  function depthOf(activity) {
    let depth = 0;
    let parentId = activity.parentId;
    const seen = new Set();
    while (typeof parentId === "string" && records.has(parentId) && !seen.has(parentId)) {
      seen.add(parentId);
      const parent = records.get(parentId);
      if (filterVisible(parent)) {
        depth += 1;
      }
      parentId = parent.parentId;
    }
    return depth;
  }

  function rowShown(activity) {
    if (!filterVisible(activity)) {
      return false;
    }
    let parentId = activity.parentId;
    const seen = new Set();
    while (typeof parentId === "string" && records.has(parentId) && !seen.has(parentId)) {
      seen.add(parentId);
      const parent = records.get(parentId);
      if (filterVisible(parent) && !open.has(parentId)) {
        return false;
      }
      parentId = parent.parentId;
    }
    return true;
  }

  function hasChildren(id) {
    for (const activity of records.values()) {
      if (activity.parentId === id) {
        return true;
      }
    }
    return false;
  }

  function visibleChildren(id, seen) {
    if (seen.has(id)) {
      return false;
    }
    seen.add(id);
    for (const activity of records.values()) {
      if (activity.parentId !== id) {
        continue;
      }
      if (filterVisible(activity) || visibleChildren(activity.id, seen)) {
        return true;
      }
    }
    return false;
  }

  function cancellableIds() {
    const ids = [];
    for (const activity of records.values()) {
      if (activity.cancellable === true && (activity.state === "running" || activity.state === "queued")) {
        ids.push(activity.id);
      }
    }
    return ids;
  }

  function failedIds() {
    const ids = [];
    for (const activity of records.values()) {
      if (activity.state === "failed" && activity.dismissed !== true) {
        ids.push(activity.id);
      }
    }
    return ids;
  }

  function clearable() {
    if (failedIds().length > 0) {
      return true;
    }
    if (history !== true) {
      return false;
    }
    for (const activity of records.values()) {
      if (activity.state === "done" || activity.state === "cancelled" || activity.state === "failed") {
        return true;
      }
    }
    return false;
  }

  function accountKeyOf(activity) {
    return typeof activity.accountId === "string" ? activity.accountId : "";
  }

  function accountLabel(key) {
    const face = accountFace(key, accounts);
    return `${face.number} ${face.name}`;
  }

  function paintAccount(tr, activity) {
    const face = accountFace(activity.accountId, accounts);
    const cell = tr.querySelector('[data-field="account"]');
    text(cell.querySelector(".activity-account-num"), face.number);
    text(cell.querySelector(".activity-account-name"), face.name);
    paintIcon(cell.querySelector(".icon"), face.icon);
  }

  function fillRow(tr, activity) {
    text(tr.querySelector('[data-field="started"]'), localClock(activity.startedMs, timeZone));
    const kind = kindFace(activity.kind);
    const kindCell = tr.querySelector('[data-field="kind"]');
    text(kindCell.querySelector(".activity-kind-label"), kind.label);
    paintIcon(kindCell.querySelector(".icon"), kind.icon);
    paintAccount(tr, activity);
    text(tr.querySelector(".activity-title-text"), activity.title || "");
    const detail = tr.querySelector(".activity-detail");
    text(detail, activity.detail || "");
    detail.hidden = !activity.detail;
    const error = tr.querySelector(".activity-error");
    text(error, activity.error || "");
    error.hidden = !activity.error;
    const progress = tr.querySelector('[data-field="progress"]');
    const barEl = progress.querySelector("progress");
    const progressLabel = progress.querySelector(".activity-progress-text");
    const line = progressText(activity.progress);
    text(progressLabel, line);
    if (!activity.progress || !Number.isFinite(activity.progress.done)) {
      barEl.hidden = true;
      barEl.removeAttribute("value");
    } else if (Number.isFinite(activity.progress.total) && activity.progress.total >= 0) {
      barEl.hidden = false;
      barEl.max = activity.progress.total > 0 ? activity.progress.total : 1;
      barEl.value = activity.progress.done;
    } else {
      barEl.hidden = false;
      barEl.removeAttribute("value");
    }
    text(tr.querySelector('[data-field="eta"]'), formatEta(activity.etaMs));
    const state = tr.querySelector('[data-field="state"]');
    text(state, stateLabel(activity.state));
    state.className = `activity-state activity-state-${activity.state || "unknown"}`;
    const fold = tr.querySelector(".activity-fold");
    const nested = visibleChildren(activity.id, new Set());
    fold.hidden = !nested;
    const opened = open.has(activity.id);
    text(fold, opened ? "▾" : "▸");
    fold.setAttribute("aria-expanded", nested && opened ? "true" : "false");
    fold.setAttribute("aria-label", `${opened ? "Hide" : "Show"} ${activity.title || "activity"}`);
    const cancel = tr.querySelector(".activity-cancel");
    const showCancel = activity.cancellable === true && (activity.state === "running" || activity.state === "queued");
    cancel.hidden = !showCancel;
    cancel.setAttribute("aria-label", `Cancel ${activity.title || "activity"}`);
    const dismiss = tr.querySelector(".activity-dismiss");
    dismiss.hidden = !(activity.state === "failed" && activity.dismissed !== true);
    dismiss.setAttribute("aria-label", `Dismiss ${activity.title || "activity"}`);
    const depth = depthOf(activity);
    tr.dataset.child = depth > 0 ? "true" : "false";
    tr.style.setProperty("--activity-depth", String(depth));
    tr.hidden = !rowShown(activity);
  }

  function buildRow(activity) {
    const tr = document.createElement("tr");
    tr.dataset.activityId = activity.id;
    const started = document.createElement("td");
    started.dataset.field = "started";
    const kind = document.createElement("td");
    kind.dataset.field = "kind";
    const kindWrap = document.createElement("span");
    kindWrap.className = "activity-kind";
    const kindIcon = document.createElement("i");
    kindIcon.className = "icon";
    kindIcon.setAttribute("aria-hidden", "true");
    const kindLabel = document.createElement("span");
    kindLabel.className = "activity-kind-label";
    kindWrap.append(kindIcon, kindLabel);
    kind.append(kindWrap);
    const account = document.createElement("td");
    account.dataset.field = "account";
    const accountWrap = document.createElement("span");
    accountWrap.className = "activity-account";
    const number = document.createElement("span");
    number.className = "activity-account-num";
    const accountIcon = document.createElement("i");
    accountIcon.className = "icon";
    accountIcon.setAttribute("aria-hidden", "true");
    const name = document.createElement("span");
    name.className = "activity-account-name";
    accountWrap.append(number, accountIcon, name);
    account.append(accountWrap);
    const title = document.createElement("td");
    title.dataset.field = "title";
    const titleWrap = document.createElement("div");
    titleWrap.className = "activity-title";
    const fold = document.createElement("button");
    fold.type = "button";
    fold.className = "activity-fold";
    fold.hidden = true;
    const copy = document.createElement("div");
    const titleText = document.createElement("div");
    titleText.className = "activity-title-text";
    const detail = document.createElement("div");
    detail.className = "activity-detail";
    const error = document.createElement("div");
    error.className = "activity-error";
    copy.append(titleText, detail, error);
    titleWrap.append(fold, copy);
    title.append(titleWrap);
    const progress = document.createElement("td");
    progress.dataset.field = "progress";
    const barEl = document.createElement("progress");
    const progressLabel = document.createElement("span");
    progressLabel.className = "activity-progress-text";
    progress.append(barEl, progressLabel);
    const eta = document.createElement("td");
    eta.dataset.field = "eta";
    const state = document.createElement("td");
    state.dataset.field = "state";
    const actions = document.createElement("td");
    actions.dataset.field = "actions";
    const cancel = document.createElement("button");
    cancel.type = "button";
    cancel.className = "activity-action activity-cancel";
    text(cancel, "Cancel");
    const dismiss = document.createElement("button");
    dismiss.type = "button";
    dismiss.className = "activity-action activity-dismiss";
    text(dismiss, "Dismiss");
    actions.append(cancel, dismiss);
    tr.append(started, kind, account, title, progress, eta, state, actions);
    const id = activity.id;
    fold.addEventListener("click", () => {
      if (open.has(id)) {
        open.delete(id);
      } else {
        open.add(id);
      }
      paint();
    });
    cancel.addEventListener("click", () => {
      api.activity.cancel(id).catch((err) => fail(err));
    });
    dismiss.addEventListener("click", () => {
      api.activity.dismiss(id).then((result) => {
        if (result && result.id) {
          accept(result);
        }
      }).catch((err) => fail(err));
    });
    return tr;
  }

  function paint() {
    const ordered = layoutActivities(records);
    const seen = new Set();
    let previous = null;
    for (const activity of ordered) {
      let tr = rowNodes.get(activity.id);
      if (!tr) {
        tr = buildRow(activity);
        rowNodes.set(activity.id, tr);
      }
      fillRow(tr, activity);
      seen.add(activity.id);
      const next = previous ? previous.nextSibling : tbody.firstChild;
      if (tr !== next) {
        tbody.insertBefore(tr, previous ? previous.nextSibling : tbody.firstChild);
      }
      previous = tr;
    }
    for (const [id, tr] of rowNodes) {
      if (!seen.has(id)) {
        tr.remove();
        rowNodes.delete(id);
      }
    }
    let visible = 0;
    for (const activity of ordered) {
      const tr = rowNodes.get(activity.id);
      if (tr && !tr.hidden) {
        visible += 1;
      }
    }
    empty.hidden = visible > 0;
    table.hidden = visible === 0;
    text(empty, records.size > 0 && visible === 0 ? "No matching activity." : "No activity.");
    paintFilters();
  }

  function kindSummary() {
    if (kindPick === null) {
      return "All actions";
    }
    if (kindPick.length === 0) {
      return "No actions";
    }
    if (kindPick.length === 1) {
      return kindFace(kindPick[0]).label;
    }
    return `${kindPick.length} actions`;
  }

  function accountSummary() {
    if (accountPick === null) {
      return "All accounts";
    }
    if (accountPick.length === 0) {
      return "No accounts";
    }
    if (accountPick.length === 1) {
      return accountLabel(accountPick[0]);
    }
    return `${accountPick.length} accounts`;
  }

  function paintFilters() {
    const kinds = [];
    for (const activity of records.values()) {
      const kind = typeof activity.kind === "string" ? activity.kind : "";
      if (!kinds.includes(kind)) {
        kinds.push(kind);
      }
    }
    kinds.sort((a, b) => kindFace(a).label.localeCompare(kindFace(b).label, "en"));
    if (Array.isArray(kindPick)) {
      const kept = kindPick.filter((kind) => kinds.includes(kind));
      if (kept.length !== kindPick.length) {
        kindPick = kept.length === kinds.length ? null : kept;
      }
    }
    const nextKindKey = kinds.join("\0");
    if (nextKindKey !== kindKey) {
      kindKey = nextKindKey;
      kindItems.replaceChildren();
      for (const kind of kinds) {
        const row = checkRow(kindFace(kind).label, () => {
          const boxes = [...kindItems.querySelectorAll("input")];
          const checked = boxes.filter((item) => item.checked).map((item) => item.value);
          kindPick = checked.length === boxes.length ? null : checked;
          paint();
        });
        row.box.value = kind;
        kindItems.append(row.label);
      }
    }
    for (const box of kindItems.querySelectorAll("input")) {
      box.checked = kindPick === null || kindPick.includes(box.value);
    }
    kindAll.box.checked = kindPick === null;
    kindNone.box.checked = Array.isArray(kindPick) && kindPick.length === 0;
    text(kindMenu.button, kindSummary());

    const keys = [];
    for (const activity of records.values()) {
      const key = accountKeyOf(activity);
      if (!keys.includes(key)) {
        keys.push(key);
      }
    }
    keys.sort((a, b) => accountLabel(a).localeCompare(accountLabel(b), "en"));
    if (Array.isArray(accountPick)) {
      const kept = accountPick.filter((key) => keys.includes(key));
      if (kept.length !== accountPick.length) {
        accountPick = kept.length === keys.length ? null : kept;
      }
    }
    const nextAccountKey = keys.join("\0");
    if (nextAccountKey !== accountKey) {
      accountKey = nextAccountKey;
      accountItems.replaceChildren();
      for (const key of keys) {
        const row = checkRow(accountLabel(key), () => {
          const boxes = [...accountItems.querySelectorAll("input")];
          const checked = boxes.filter((item) => item.checked).map((item) => item.value);
          accountPick = checked.length === boxes.length ? null : checked;
          paint();
        });
        row.box.value = key;
        accountItems.append(row.label);
      }
    }
    for (const box of accountItems.querySelectorAll("input")) {
      box.checked = accountPick === null || accountPick.includes(box.value);
    }
    accountAll.box.checked = accountPick === null;
    accountNone.box.checked = Array.isArray(accountPick) && accountPick.length === 0;
    text(accountMenu.button, accountSummary());

    stopAll.disabled = cancellableIds().length === 0;
    clearAll.disabled = !clearable();
    let parents = 0;
    let closed = 0;
    for (const activity of records.values()) {
      if (!hasChildren(activity.id)) {
        continue;
      }
      parents += 1;
      if (!open.has(activity.id)) {
        closed += 1;
      }
    }
    expandAll.disabled = parents === 0 || closed === 0;
    collapseAll.disabled = open.size === 0;
  }

  function accept(activity) {
    if (!activity || typeof activity.id !== "string") {
      return;
    }
    if (loading) {
      pending.push(activity);
      return;
    }
    const chosen = preferActivity(records.get(activity.id), activity);
    if (!listedActivity(chosen, history)) {
      records.delete(activity.id);
    } else {
      records.set(activity.id, chosen);
    }
    paint();
    report();
  }

  function drain() {
    loading = false;
    let activity = pending.shift();
    while (activity) {
      accept(activity);
      activity = pending.shift();
    }
  }

  function fail(err) {
    if (stopped || typeof opts.onError !== "function") {
      return;
    }
    const message = err instanceof Error ? err.message : String(err);
    opts.onError(message);
  }

  function reload() {
    const ticket = ++generation;
    const includeHistory = history;
    loading = true;
    ready = api.activity.list({ includeHistory }).then((result) => {
      if (stopped || ticket !== generation) {
        return;
      }
      const rows = result && Array.isArray(result.activities) ? result.activities : [];
      records.clear();
      for (const activity of rows) {
        if (activity && typeof activity.id === "string" && listedActivity(activity, includeHistory)) {
          records.set(activity.id, activity);
        }
      }
      drain();
      paint();
      report();
    }).catch((err) => {
      if (!stopped && ticket === generation) {
        drain();
        fail(err);
      }
    });
    return ready;
  }

  historyBox.addEventListener("change", () => {
    history = historyBox.checked;
    if (typeof opts.onHistory === "function") {
      opts.onHistory(history);
    }
    reload();
  });

  const unsubscribe = api.on("activity.changed", (payload) => {
    const activity = payload && payload.activity ? payload.activity : payload;
    accept(activity);
  });

  reload();

  return {
    ready() {
      return ready;
    },
    setAccounts(list) {
      accounts = Array.isArray(list) ? list : [];
      accountKey = null;
      paint();
    },
    setTimeZone(zone) {
      timeZone = zone || "America/Vancouver";
      paint();
    },
    setShown,
    destroy() {
      stopped = true;
      generation += 1;
      if (typeof unsubscribe === "function") {
        unsubscribe();
      }
      if (listenRoot) {
        listenRoot.removeEventListener("pointerdown", onPointerDown);
        listenRoot.removeEventListener("keydown", onEscape);
      }
      for (const el of chromeNodes) {
        el.remove();
      }
      records.clear();
      rowNodes.clear();
      pane.replaceChildren();
    },
  };
}
