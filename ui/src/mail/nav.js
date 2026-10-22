/* Mail nav: the mailbox tree from mailbox.list. The shell owns clicks,
   menus, and the toolbar. This module loads nodes, decorates them, and
   patches rows when the core reports a change. */

import { createTreeController } from "../tree/tree.js";

const ACCOUNT_ICON = "fa-sharp-duotone fa-solid fa-user";
const FOLDER_ICON = "fa-sharp-duotone fa-solid fa-folder";
const ERROR_ICON = "fa-sharp-duotone fa-solid fa-circle-exclamation";
const TIP_PERIOD_MS = 1000;

const ROLE_ICONS = {
  inbox: "fa-sharp-duotone fa-solid fa-inbox",
  drafts: "fa-sharp-duotone fa-solid fa-file-pen",
  sent: "fa-sharp-duotone fa-solid fa-paper-plane",
  archive: "fa-sharp-duotone fa-solid fa-box-archive",
  junk: "fa-sharp-duotone fa-solid fa-ban",
  trash: "fa-sharp-duotone fa-solid fa-trash",
};

const DEFAULT_REGIONS = ["leading", "label", "status", "count", "hover"];

export function mapSyncState(state) {
  if (state === "syncing") {
    return { state: "syncing", stateIcon: "" };
  }
  if (
    state === "error" ||
    state === "offline" ||
    state === "locked" ||
    (typeof state === "string" && state.startsWith("error:"))
  ) {
    return { state, stateIcon: ERROR_ICON };
  }
  return { state: "idle", stateIcon: "" };
}

function parentKey(node) {
  if (!node || node.parentId === null || node.parentId === undefined || node.parentId === "") {
    return null;
  }
  return node.parentId;
}

function accountIdOf(id, byId) {
  let current = byId.get(id);
  const seen = new Set();
  while (current && !seen.has(current.id)) {
    seen.add(current.id);
    if (current.kind === "account") {
      return current.id;
    }
    const parentId = parentKey(current);
    if (!parentId) {
      return null;
    }
    current = byId.get(parentId);
  }
  return null;
}

export function createMailNav(api, container, options = {}) {
  const tree = createTreeController(container, {
    id: options.id || "mail",
    label: options.label || "Mail",
    nodes: [],
    state: options.state || {},
    regions: options.regions || DEFAULT_REGIONS,
    onPersist: options.onPersist || null,
  });

  let numbers = new Map();
  let syncByAccount = new Map();
  let progressByAccount = new Map();
  let stopped = false;
  /* The shell defers until the Mail nav is visible. A Contacts-only restore
     must not build this tree during the first paint: the icon kit then
     replaces every row icon on the main thread. */
  let armed = options.defer !== true;
  let generation = 0;
  let readySettled = false;
  let resolveReady = () => {};
  const ready = new Promise((resolve) => {
    resolveReady = resolve;
  });
  const stops = [];

  function settleReady() {
    if (readySettled) {
      return;
    }
    readySettled = true;
    resolveReady();
  }

  /* Boot timing. The core already times the commands. These lines say when
     the page sent them, when it resumed, and when the tree was drawn. */
  function mark(message) {
    const write = api.log && api.log.client;
    if (typeof write !== "function") {
      return;
    }
    try {
      void Promise.resolve(write.call(api.log, { subsystem: "mail", level: "debug", message })).catch(() => {});
    } catch {
      /* A mark must not change the reload. */
    }
  }

  function nodeMap() {
    const byId = new Map();
    for (const node of tree.model.toArray()) {
      if (node && typeof node.id === "string") {
        byId.set(node.id, node);
      }
    }
    return byId;
  }

  function decorate(node) {
    const next = { ...node };
    next.parentId = parentKey(next);
    next.hasChildren = Boolean(next.hasChildren);
    if (typeof next.order !== "number") {
      next.order = 0;
    }
    if (next.kind === "account") {
      const name = typeof next.label === "string" && next.label !== "" ? next.label : next.id;
      const number = numbers.get(next.id);
      next.label = typeof number === "string" && number !== "" ? `${number} ${name}` : name;
      next.icon = ACCOUNT_ICON;
      const prev = tree.getNode(next.id);
      const pending = syncByAccount.get(next.id);
      if (prev && typeof prev.state === "string") {
        next.state = prev.state;
        if (typeof prev.stateIcon === "string") {
          next.stateIcon = prev.stateIcon;
        }
        if (prev.state === "syncing") {
          next.syncTurn = prev.syncTurn;
          next.syncTip = prev.syncTip;
        }
      } else if (pending) {
        next.state = pending.state;
        next.stateIcon = pending.stateIcon;
        next.syncTurn = pending.syncTurn;
        next.syncTip = pending.syncTip;
      }
    } else if (next.kind === "mailbox") {
      next.icon = ROLE_ICONS[next.role] || FOLDER_ICON;
    }
    return next;
  }

  function changed(old, node) {
    return (
      old.label !== node.label ||
      old.unread !== node.unread ||
      old.total !== node.total ||
      old.role !== node.role ||
      old.icon !== node.icon ||
      old.swatch !== node.swatch ||
      old.state !== node.state ||
      old.stateIcon !== node.stateIcon ||
      old.syncTurn !== node.syncTurn ||
      old.syncTip !== node.syncTip
    );
  }

  function apply(incoming, scopeAccountId) {
    const decorated = [];
    for (const node of incoming) {
      if (node && typeof node.id === "string" && node.id !== "") {
        decorated.push(decorate(node));
      }
    }
    const ids = new Set(decorated.map((node) => node.id));
    const byId = nodeMap();
    const remove = [];
    for (const old of byId.values()) {
      if (ids.has(old.id)) {
        continue;
      }
      if (!scopeAccountId || old.id === scopeAccountId || accountIdOf(old.id, byId) === scopeAccountId) {
        remove.push(old.id);
      }
    }
    let structure = remove.length > 0;
    if (!structure) {
      for (const node of decorated) {
        const old = byId.get(node.id);
        if (
          !old ||
          parentKey(old) !== parentKey(node) ||
          Boolean(old.hasChildren) !== node.hasChildren ||
          (typeof old.order === "number" ? old.order : 0) !== node.order
        ) {
          structure = true;
          break;
        }
      }
    }
    if (structure) {
      tree.model.remove(remove);
      tree.model.patch(decorated);
      tree.render();
      return;
    }
    for (const node of decorated) {
      const old = byId.get(node.id);
      if (old && changed(old, node)) {
        tree.patchNode(node.id, node);
      }
    }
  }

  async function reload(accountId) {
    const gen = ++generation;
    const scope = typeof accountId === "string" && accountId !== "" ? accountId : "";
    const label = scope || "all";
    try {
      mark(`reload ${gen} account.list sent ${label}`);
      const listed = await api.account.list();
      mark(`reload ${gen} account.list returned ${label}`);
      if (stopped || gen !== generation) {
        mark(`reload ${gen} dropped after account.list`);
        return;
      }
      const nextNumbers = new Map();
      const accounts = listed && Array.isArray(listed.accounts) ? listed.accounts : [];
      for (const account of accounts) {
        if (account && typeof account.id === "string" && typeof account.number === "string") {
          nextNumbers.set(account.id, account.number);
        }
      }
      numbers = nextNumbers;
      mark(`reload ${gen} mailbox.list sent ${label}`);
      const result = await api.mailbox.list(scope ? { accountId: scope } : {});
      mark(`reload ${gen} mailbox.list returned ${label}`);
      if (stopped || gen !== generation) {
        mark(`reload ${gen} dropped after mailbox.list`);
        return;
      }
      const nodes = result && Array.isArray(result.nodes) ? result.nodes : [];
      mark(`reload ${gen} render started ${nodes.length}`);
      apply(nodes, scope);
      mark(`reload ${gen} render finished ${nodes.length}`);
    } catch (err) {
      if (stopped || gen !== generation) {
        return;
      }
      const code = err && typeof err.code === "string" && err.code !== "" ? err.code : "error";
      try {
        await api.log.client({ subsystem: "mail", level: "warn", message: `mailbox tree failed: ${code}` });
      } catch {
        /* Stopping can reject the log command. The tree stays as it was. */
      }
    } finally {
      /* A newer reload owns the first paint. This one settles it when it is
         still the latest, including a failed list. */
      if (stopped || gen === generation) {
        mark(`reload ${gen} ready`);
        settleReady();
      }
    }
  }

  function onMailboxes(payload) {
    if (!armed) {
      return;
    }
    const accountId = payload && typeof payload.accountId === "string" ? payload.accountId : "";
    mark(`mailboxes changed ${accountId || "all"}`);
    reload(accountId);
  }

  function fractionTurn(progress) {
    if (!progress || !Number.isFinite(progress.done)) {
      return null;
    }
    if (!Number.isFinite(progress.total) || progress.total <= 0) {
      return null;
    }
    const raw = (progress.done / progress.total) * 100;
    if (!Number.isFinite(raw)) {
      return null;
    }
    return Math.max(0, Math.min(100, raw));
  }

  function tipFor(turn) {
    if (turn === null) {
      return "Syncing";
    }
    return `${Math.round(turn)}%`;
  }

  function remember(accountId) {
    const current = syncByAccount.get(accountId);
    if (!current || current.state !== "syncing") {
      return;
    }
    const progress = progressByAccount.get(accountId);
    syncByAccount.set(accountId, {
      state: "syncing",
      stateIcon: "",
      syncTurn: progress ? progress.turn : null,
      syncTip: progress ? progress.shown : "Syncing",
    });
  }

  function noteProgress(accountId, progress) {
    const turn = fractionTurn(progress);
    const tip = tipFor(turn);
    const now = Date.now();
    const prev = progressByAccount.get(accountId);
    const shown = prev ? prev.shown : "";
    const tipAt = prev ? prev.tipAt : 0;
    const percent = turn === null ? null : Math.round(turn);
    const becameKnown = shown === "Syncing" && tip !== "Syncing";
    const due =
      tip !== shown &&
      (tipAt === 0 || becameKnown || percent === 100 || now - tipAt >= TIP_PERIOD_MS);
    progressByAccount.set(accountId, {
      turn,
      shown: due ? tip : shown || tip,
      tipAt: due ? now : tipAt,
    });
    remember(accountId);
  }

  function paintProgress(accountId) {
    if (!tree.getNode(accountId) || tree.getNode(accountId).state !== "syncing") {
      return;
    }
    const progress = progressByAccount.get(accountId);
    if (!progress) {
      return;
    }
    tree.patchNode(accountId, {
      id: accountId,
      state: "syncing",
      stateIcon: "",
      syncTurn: progress.turn,
      syncTip: progress.shown,
    });
  }

  function onSync(payload) {
    const accountId = payload && typeof payload.accountId === "string" ? payload.accountId : "";
    if (!accountId) {
      return;
    }
    const state = payload && typeof payload.state === "string" ? payload.state : "idle";
    const prev = syncByAccount.get(accountId);
    if (state === "syncing") {
      if (!prev || prev.state !== "syncing") {
        progressByAccount.delete(accountId);
      }
    } else {
      progressByAccount.delete(accountId);
    }
    const mapped = mapSyncState(state);
    syncByAccount.set(accountId, {
      state: mapped.state,
      stateIcon: mapped.stateIcon,
      syncTurn: null,
      syncTip: state === "syncing" ? "Syncing" : "",
    });
    remember(accountId);
    const view = syncByAccount.get(accountId);
    if (!tree.getNode(accountId)) {
      return;
    }
    tree.patchNode(accountId, { id: accountId, ...view });
  }

  function onActivity(payload) {
    const activity = payload && payload.activity;
    if (!activity || typeof activity.accountId !== "string" || activity.accountId === "") {
      return;
    }
    if (typeof activity.kind !== "string" || !activity.kind.startsWith("sync.")) {
      return;
    }
    const state = activity.state;
    if (state !== "running" && state !== "queued" && state !== "done" && state !== "failed" && state !== "cancelled") {
      return;
    }
    noteProgress(activity.accountId, activity.progress);
    paintProgress(activity.accountId);
  }

  stops.push(api.on("mail.mailboxes_changed", onMailboxes));
  stops.push(api.on("sync.state", onSync));
  stops.push(api.on("activity.changed", onActivity));
  stops.push(api.on("accounts.changed", () => {
    if (!armed) {
      return;
    }
    reload();
  }));
  if (armed) {
    reload();
  }

  return {
    tree,
    reload,
    start() {
      if (!armed) {
        armed = true;
        reload();
      }
      return ready;
    },
    ready,
    stop() {
      stopped = true;
      generation += 1;
      settleReady();
      while (stops.length > 0) {
        const stop = stops.pop();
        stop();
      }
      tree.destroy();
    },
  };
}
