export const SESSION_MS = 30000;
export const MAX_TABS_DEFAULT = 50;
export const MAX_TABS_MIN = 1;
export const MAX_TABS_MAX = 200;

/* A missing or unusable value is the default. The field cannot drop to zero or run away. */
export function clampMaxTabs(value) {
  const n = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(n)) {
    return MAX_TABS_DEFAULT;
  }
  const whole = Math.round(n);
  if (whole < MAX_TABS_MIN) {
    return MAX_TABS_MIN;
  }
  if (whole > MAX_TABS_MAX) {
    return MAX_TABS_MAX;
  }
  return whole;
}

const SECTION = /^[a-z][a-z0-9_-]{0,31}$/;
const KIND = /^[a-z][a-z0-9_-]{0,31}$/;

function isPlain(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function cloneContext(value) {
  if (!isPlain(value)) {
    return null;
  }
  try {
    const copy = JSON.parse(JSON.stringify(value));
    return isPlain(copy) ? copy : null;
  } catch {
    return null;
  }
}

function titleOf(raw, kind, section) {
  let title = typeof raw.title === "string" ? raw.title.trim() : "";
  if (title.length > 200) {
    title = title.slice(0, 200);
  }
  if (title === "") {
    return kind === "home" ? section : kind;
  }
  return title;
}

/* One saved tab, or null when the record cannot be stored. */
export function clampTab(raw, index) {
  if (!isPlain(raw)) {
    return null;
  }
  const section = raw.section;
  const kind = raw.kind;
  if (typeof section !== "string" || !SECTION.test(section)) {
    return null;
  }
  if (typeof kind !== "string" || !KIND.test(kind)) {
    return null;
  }
  let key = raw.key == null ? "" : String(raw.key);
  if (kind === "home") {
    key = "";
  } else if (key === "" || key.length > 200 || /[\u0000-\u001f]/.test(key)) {
    return null;
  }
  const id = kind === "home" ? `${section}:home` : `${section}:${kind}:${key}`;
  if (raw.id !== id) {
    return null;
  }
  let icon = typeof raw.icon === "string" ? raw.icon.trim() : "";
  if (icon.startsWith("<") || icon.length > 200) {
    icon = "";
  }
  const order = Number.isFinite(raw.order) ? raw.order : index;
  return {
    id,
    section,
    kind,
    key,
    title: titleOf(raw, kind, section),
    icon,
    context: cloneContext(raw.context),
    pinned: kind !== "home" && raw.pinned === true,
    muted: kind === "browse" && raw.muted === true,
    order,
    active: raw.active === true,
  };
}

/* tabs null means nothing has been saved. An empty array is an empty workspace.
   shutdown is clean only after a quit. Anything else is an open session. */
export function clampSession(raw) {
  if (!isPlain(raw)) {
    return { tabs: null, restore: true, shutdown: "open", maxTabs: MAX_TABS_DEFAULT };
  }
  const restore = raw.restore !== false;
  const shutdown = raw.shutdown === "clean" ? "clean" : "open";
  const maxTabs = clampMaxTabs(raw.maxTabs);
  if (!Array.isArray(raw.tabs)) {
    return { tabs: null, restore, shutdown, maxTabs };
  }
  const seen = new Set();
  const tabs = [];
  raw.tabs.forEach((item, index) => {
    const tab = clampTab(item, index);
    if (!tab || seen.has(tab.id)) {
      return;
    }
    seen.add(tab.id);
    tabs.push(tab);
  });
  return { tabs, restore, shutdown, maxTabs };
}

export function serializeTabs(list) {
  if (!Array.isArray(list)) {
    return [];
  }
  const tabs = [];
  list.forEach((entry, index) => {
    const tab = clampTab(entry, index);
    if (tab) {
      tabs.push(tab);
    }
  });
  return tabs;
}

function chooseActive(sorted, kept) {
  const alive = new Set(kept.map((tab) => tab.id));
  const origin = sorted.findIndex((tab) => tab.active === true);
  if (origin >= 0 && alive.has(sorted[origin].id)) {
    return sorted[origin].id;
  }
  const start = origin < 0 ? -1 : origin;
  for (let i = start - 1; i >= 0; i -= 1) {
    if (alive.has(sorted[i].id)) {
      return sorted[i].id;
    }
  }
  for (let i = start + 1; i < sorted.length; i += 1) {
    if (alive.has(sorted[i].id)) {
      return sorted[i].id;
    }
  }
  return kept[0].id;
}

function finish(tabs, resolve, max) {
  const sorted = [...tabs].sort((a, b) => a.order - b.order);
  const dropped = [];
  const kept = [];
  for (const tab of sorted) {
    if (typeof resolve === "function" && resolve(tab) === false) {
      dropped.push({ id: tab.id, title: tab.title });
      continue;
    }
    kept.push({
      ...tab,
      pinned: tab.kind !== "home" && tab.pinned === true,
      muted: tab.kind === "browse" && tab.muted === true,
    });
  }
  if (kept.length === 0) {
    return { mode: "empty", tabs: [], dropped, limited: 0 };
  }
  const activeId = chooseActive(sorted, kept);
  const ordered = [...kept.filter((tab) => tab.pinned), ...kept.filter((tab) => !tab.pinned)];
  ordered.forEach((tab, index) => {
    tab.order = index + 1;
    tab.active = tab.id === activeId;
  });
  const capped = capOrdered(ordered, max);
  return { mode: "tabs", tabs: capped.tabs, dropped, limited: capped.limited };
}

/* Homes always stay. They are one per section, so a sub-tab flood cannot hide Settings.
   The active sub-tab is next, then pins, then the rest, until the total reaches max. */
function capOrdered(ordered, max) {
  const limit = clampMaxTabs(max);
  const homes = ordered.filter((tab) => tab.kind === "home");
  const subs = ordered.filter((tab) => tab.kind !== "home");
  const room = Math.max(0, limit - homes.length);
  if (subs.length <= room) {
    return { tabs: ordered, limited: 0 };
  }
  const active = subs.find((tab) => tab.active);
  const chosen = [];
  if (active && room > 0) {
    chosen.push(active);
  }
  for (const tab of subs) {
    if (chosen.length >= room) {
      break;
    }
    if (active && tab.id === active.id) {
      continue;
    }
    chosen.push(tab);
  }
  const ids = new Set([...homes, ...chosen].map((tab) => tab.id));
  const packed = ordered.filter((tab) => ids.has(tab.id));
  const marked = ordered.find((tab) => tab.active);
  const activeId = marked && ids.has(marked.id) ? marked.id : packed[0].id;
  packed.forEach((tab, index) => {
    tab.order = index + 1;
    tab.active = tab.id === activeId;
  });
  return { tabs: packed, limited: subs.length - chosen.length };
}

function quick(unexpected) {
  return { mode: "quick", tabs: [], dropped: [], unexpected: unexpected === true, limited: 0 };
}

/* first: nothing saved, and restore is on, so open Mail.
   empty: a clean quit saved an empty strip.
   quick: open nothing. unexpected means the last run did not shut down cleanly.
   tabs: the records to open, active tab included.
   Restore off never opens a saved tab. A saved list whose shutdown is not clean
   does not open either, including a list written before shutdown existed. */
export function planRestore(session, resolve) {
  const src = isPlain(session) ? session : {};
  const restore = src.restore !== false;
  const clean = src.shutdown === "clean";
  if (!Array.isArray(src.tabs)) {
    if (!restore) {
      return quick(false);
    }
    return { mode: "first", tabs: [], dropped: [], unexpected: false, limited: 0 };
  }
  const seen = new Set();
  const cleaned = [];
  src.tabs.forEach((item, index) => {
    const tab = clampTab(item, index);
    if (!tab || seen.has(tab.id)) {
      return;
    }
    seen.add(tab.id);
    cleaned.push(tab);
  });
  if (!restore || !clean) {
    return quick(!clean);
  }
  if (cleaned.length === 0) {
    return { mode: "empty", tabs: [], dropped: [], unexpected: false, limited: 0 };
  }
  const planned = finish(cleaned, resolve, src.maxTabs);
  planned.unexpected = false;
  return planned;
}
