import { normalizeFilter } from "./log/filter.js";
import { clampSession } from "./shell/session.js";
import { themeNameOk } from "./theme-catalog.js";

/* bar is the default side of the divider. toggle false is a row with no checkbox.
   chord is the default digit, or "" for none. Settings cannot be stored off. */
export const RAIL_ITEMS = [
  { key: "mail", label: "Mail", bar: "top", toggle: true, on: true, chord: "1" },
  { key: "contacts", label: "Contacts", bar: "top", toggle: true, on: true, chord: "2" },
  { key: "calendars", label: "Calendars", bar: "top", toggle: true, on: true, chord: "3" },
  { key: "tasks", label: "Tasks", bar: "top", toggle: true, on: false, chord: "4" },
  { key: "files", label: "Files", bar: "top", toggle: true, on: true, chord: "5" },
  { key: "notes", label: "Notes", bar: "top", toggle: true, on: false, chord: "6" },
  { key: "argent", label: "Argent", bar: "top", toggle: true, on: false, chord: "7" },
  { key: "bots", label: "Grok Bots", bar: "bottom", toggle: true, on: false, chord: "" },
  { key: "github", label: "GitHub", bar: "bottom", toggle: true, on: false, chord: "" },
  { key: "activity", label: "Activity", bar: "bottom", toggle: true, on: true, chord: "" },
  { key: "settings", label: "Settings", bar: "bottom", toggle: false, on: true, chord: "" },
  { key: "update", label: "Update", bar: "bottom", toggle: true, on: true, chord: "" },
  { key: "help", label: "Help", bar: "bottom", toggle: true, on: true, chord: "" },
];

export const RAIL_KEYS = RAIL_ITEMS.map((item) => item.key);
export const FEATURE_KEYS = RAIL_ITEMS.filter((item) => item.toggle).map((item) => item.key);
export const CHORD_DIGITS = ["1", "2", "3", "4", "5", "6", "7", "8", "9"];

const RAIL_BY_KEY = new Map(RAIL_ITEMS.map((item) => [item.key, item]));

function defaultFeatures() {
  const out = {};
  for (const item of RAIL_ITEMS) {
    if (item.toggle) {
      out[item.key] = item.on;
    }
  }
  return out;
}

export const DEFAULT_UI = {
  nav: { width: 320, min: 200, max: 500 },
  mail: { split: 0.5, min: 0.25, max: 0.75 },
  contacts: { split: 0.5, min: 0.25, max: 0.75 },
  files: { split: 0.5, min: 0.25, max: 0.75 },
  help: { split: 0.25, min: 0.15, max: 0.6 },
  zoom: {
    shell: 1,
    navButtons: 1,
    navBody: 1,
    mailList: 1,
    mailDetail: 1,
    log: 1,
    helpIndex: 1,
    helpContent: 1,
    wheel: "away",
  },
  activity: { view: "log", history: false },
  session: { tabs: null, restore: true, shutdown: "open", maxTabs: 50 },
  log: {
    levels: ["ERROR", "WARN", "INFO", "DEBUG"],
    subsystems: null,
    text: "",
    follow: true,
    wrap: false,
  },
  icons: {},
  features: defaultFeatures(),
  fontawesome_kit: "45801522ef",
  fonts: {
    body: '"Vanadium Sans", system-ui, sans-serif',
    mono: "ui-monospace, monospace",
  },
};

export const ZOOM_KEYS = [
  "shell",
  "navButtons",
  "navBody",
  "mailList",
  "mailDetail",
  "log",
  "helpIndex",
  "helpContent",
];
const KNOWN = new Set([
  "nav",
  "mail",
  "contacts",
  "files",
  "help",
  "zoom",
  "icons",
  "fontawesome_kit",
  "fonts",
  "activity",
  "log",
  "session",
  "features",
  "rail",
  "chords",
  "themeChoice",
  "fontSource",
  "iconSource",
]);

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function finite(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

/* Pane zoom is 5% through 500%. Shell, the master scale, is 50% through 200%.
   Steps of 5% on the same grid. A missing or non-numeric value is 100%. */
export function snapZoom(value, key) {
  const n = finite(value);
  if (n === null) {
    return 1;
  }
  const min = key === "shell" ? 0.5 : 0.05;
  const max = key === "shell" ? 2 : 5;
  const clamped = Math.min(max, Math.max(min, n));
  const steps = Math.round((clamped - 0.05) / 0.05);
  return Math.round((0.05 + steps * 0.05) * 1000) / 1000;
}

function copyUnknown(out, src, known) {
  if (!isObject(src)) {
    return;
  }
  for (const key of Object.keys(src)) {
    if (!known.includes(key)) {
      out[key] = src[key];
    }
  }
}

function panePx(src, fallback) {
  const raw = isObject(src) ? src : {};
  let min = finite(raw.min);
  let max = finite(raw.max);
  if (min === null || max === null || min > max || min < 0 || max > 10000) {
    min = fallback.min;
    max = fallback.max;
  }
  let width = finite(raw.width);
  if (width === null) {
    width = fallback.width;
  }
  width = Math.round(Math.min(max, Math.max(min, width)));
  const out = { width, min, max };
  copyUnknown(out, raw, ["width", "min", "max"]);
  return out;
}

function paneSplit(src, fallback) {
  const raw = isObject(src) ? src : {};
  let min = finite(raw.min);
  let max = finite(raw.max);
  if (min === null || max === null || min > max || min < 0 || max > 1) {
    min = fallback.min;
    max = fallback.max;
  }
  let split = finite(raw.split);
  if (split === null) {
    split = fallback.split;
  }
  split = Math.min(max, Math.max(min, split));
  const out = { split, min, max };
  copyUnknown(out, raw, ["split", "min", "max"]);
  return out;
}

function fontStack(value, fallback) {
  if (typeof value !== "string" || value.trim() === "") {
    return fallback;
  }
  return value.trim();
}

function fontsOf(src) {
  const raw = isObject(src) ? src : {};
  const out = {
    body: fontStack(raw.body, DEFAULT_UI.fonts.body),
    mono: fontStack(raw.mono, DEFAULT_UI.fonts.mono),
  };
  copyUnknown(out, raw, ["body", "mono"]);
  return out;
}

/* A missing or non-boolean value uses the default. Tasks, Notes, Argent, Grok Bots, and GitHub start off.
   Activity, Update, and Help start on. Settings is not a stored switch. */
function featuresOf(src) {
  const raw = isObject(src) ? src : {};
  const out = {};
  for (const key of FEATURE_KEYS) {
    out[key] = typeof raw[key] === "boolean" ? raw[key] : DEFAULT_UI.features[key];
  }
  copyUnknown(out, raw, FEATURE_KEYS);
  return out;
}

function knownRailKeys(list) {
  const out = [];
  if (!Array.isArray(list)) {
    return out;
  }
  for (const key of list) {
    if (typeof key === "string" && RAIL_BY_KEY.has(key) && !out.includes(key)) {
      out.push(key);
    }
  }
  return out;
}

/* No saved lists means the catalog order. A saved list keeps its order, the top side wins a
   duplicate, and a catalog key that is missing is appended to its default side. */
export function railOf(src) {
  const raw = isObject(src) ? src : {};
  let top;
  let bottom;
  if (!Array.isArray(raw.top) && !Array.isArray(raw.bottom)) {
    top = RAIL_ITEMS.filter((item) => item.bar === "top").map((item) => item.key);
    bottom = RAIL_ITEMS.filter((item) => item.bar === "bottom").map((item) => item.key);
  } else {
    top = knownRailKeys(raw.top);
    const seen = new Set(top);
    bottom = knownRailKeys(raw.bottom).filter((key) => !seen.has(key));
    for (const key of bottom) {
      seen.add(key);
    }
    for (const item of RAIL_ITEMS) {
      if (seen.has(item.key)) {
        continue;
      }
      if (item.bar === "top") {
        top.push(item.key);
      } else {
        bottom.push(item.key);
      }
      seen.add(item.key);
    }
  }
  const out = { top, bottom };
  copyUnknown(out, raw, ["top", "bottom"]);
  return out;
}

/* An absent chords value uses the catalog digits. A present object treats a missing or invalid
   key as none, so a cleared shortcut stays cleared. The earlier button, top then bottom, keeps
   a repeated digit. */
export function chordsOf(src, rail) {
  const order = [...rail.top, ...rail.bottom];
  const out = {};
  const used = new Set();
  const present = isObject(src);
  for (const key of order) {
    const item = RAIL_BY_KEY.get(key);
    let digit = "";
    if (!present) {
      digit = item && CHORD_DIGITS.includes(item.chord) ? item.chord : "";
    } else if (typeof src[key] === "string" && CHORD_DIGITS.includes(src[key])) {
      digit = src[key];
    }
    if (digit !== "" && used.has(digit)) {
      digit = "";
    }
    if (digit !== "") {
      used.add(digit);
    }
    out[key] = digit;
  }
  if (present) {
    copyUnknown(out, src, RAIL_KEYS);
  }
  return out;
}

function activityOf(src) {
  const raw = isObject(src) ? src : {};
  const out = {
    view: raw.view === "activity" ? "activity" : "log",
    history: raw.history === true,
  };
  copyUnknown(out, raw, ["view", "history"]);
  return out;
}

function choiceOf(value) {
  if (value === "dark" || value === "light" || value === "system") {
    return value;
  }
  if (typeof value !== "string" || !value.startsWith("file:")) {
    return "";
  }
  const name = value.slice(5);
  return themeNameOk(name) ? value : "";
}

function sourceOf(value) {
  return value === "theme" || value === "override" ? value : "";
}

/* Fills defaults and clamps. Unknown keys are kept. */
export function clampUi(raw) {
  const src = isObject(raw) ? raw : {};
  const rail = railOf(src.rail);
  const zoomSrc = isObject(src.zoom) ? src.zoom : {};
  const zoom = {};
  for (const key of ZOOM_KEYS) {
    zoom[key] = snapZoom(zoomSrc[key] === undefined ? DEFAULT_UI.zoom[key] : zoomSrc[key], key);
  }
  /* Which roll increases a pane. Not a scale, so it stays out of ZOOM_KEYS. */
  zoom.wheel = zoomSrc.wheel === "toward" ? "toward" : "away";
  copyUnknown(zoom, zoomSrc, [...ZOOM_KEYS, "wheel"]);
  const out = {
    nav: panePx(src.nav, DEFAULT_UI.nav),
    mail: paneSplit(src.mail, DEFAULT_UI.mail),
    contacts: paneSplit(src.contacts, DEFAULT_UI.contacts),
    files: paneSplit(src.files, DEFAULT_UI.files),
    help: paneSplit(src.help, DEFAULT_UI.help),
    zoom,
    icons: isObject(src.icons) ? { ...src.icons } : {},
    fontawesome_kit: typeof src.fontawesome_kit === "string" ? src.fontawesome_kit : DEFAULT_UI.fontawesome_kit,
    fonts: fontsOf(src.fonts),
    activity: activityOf(src.activity),
    log: normalizeFilter(src.log === undefined ? DEFAULT_UI.log : src.log),
    session: clampSession(src.session),
    features: featuresOf(src.features),
    rail,
    chords: chordsOf(src.chords, rail),
  };
  const themeChoice = choiceOf(src.themeChoice);
  if (themeChoice) {
    out.themeChoice = themeChoice;
  }
  const fontSource = sourceOf(src.fontSource);
  if (fontSource) {
    out.fontSource = fontSource;
  }
  const iconSource = sourceOf(src.iconSource);
  if (iconSource) {
    out.iconSource = iconSource;
  }
  for (const key of Object.keys(src)) {
    if (!KNOWN.has(key)) {
      out[key] = src[key];
    }
  }
  return out;
}
