import { colorZoomOf, swatchList } from "./common/coloris.js";
import { libraryExtras } from "./common/iconhosts.js";
import { normalizeFormats } from "./formats.js";
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
  accounts: { split: 0.35, min: 0.15, max: 0.85 },
  iconPane: { split: 0.4, min: 0.2, max: 0.75 },
  libraryPane: { split: 0.4, min: 0.2, max: 0.75 },
  assetPane: { split: 0.4, min: 0.2, max: 0.75 },
  zoom: {
    shell: 1,
    navButtons: 1,
    navBody: 1,
    mailList: 1,
    mailDetail: 1,
    log: 1,
    helpIndex: 1,
    helpContent: 1,
    filters: 1,
    tables: 1,
    details: 1,
    settings: 1,
    wheel: "away",
  },
  activity: { view: "log", history: false },
  session: { tabs: null, restore: true, shutdown: "open", maxTabs: 50 },
  log: {
    levels: ["ERROR", "WARN", "INFO"],
    subsystems: null,
    text: "",
    follow: true,
    wrap: false,
  },
  icons: {},
  trees: {},
  features: defaultFeatures(),
  fontawesome_kit: "4a6b47ce22",
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
  "filters",
  "tables",
  "details",
  "settings",
];
const KNOWN = new Set([
  "nav",
  "mail",
  "contacts",
  "files",
  "help",
  "accounts",
  "iconPane",
  "libraryPane",
  "assetPane",
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
   "trees",
  "themeChoice",
  "fontSource",
  "iconSource",
  "formats",
  "iconLibraries",
  "iconRecent",
  "iconRecentMax",
  "iconsResetD9",
  "colorSwatches",
  "colorPickerZoom",
  "iconWizardZoom",
  "iconResultZoom",
  "iconFlagBox",
]);

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

/* Tree view state is opaque to the core. Keep one plain object per tree id
   so expansion and hidden roots survive a save. Drop anything else. */
function treesOf(raw) {
  if (!isObject(raw)) {
    return {};
  }
  const out = {};
  for (const key of Object.keys(raw)) {
    if (isObject(raw[key])) {
      out[key] = { ...raw[key] };
    }
  }
  return out;
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

function paneSplit(src, fallback, fixedBounds = false) {
  const raw = isObject(src) ? src : {};
  let min = finite(raw.min);
  let max = finite(raw.max);
  /* K-Q12 (2026-10-20): Accounts bounds come from code, so a stored 0.6 cap is lifted. */
  if (fixedBounds || min === null || max === null || min > max || min < 0 || max > 1) {
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

function iconsOf(raw) {
  if (!isObject(raw)) {
    return {};
  }
  const out = {};
  for (const [key, value] of Object.entries(raw)) {
    if (typeof value !== "string" || value.length > 8192 || key.length > 80) {
      continue;
    }
    out[key] = value;
  }
  return out;
}

function librariesOf(raw) {
  if (!Array.isArray(raw)) {
    return undefined;
  }
  return raw.slice(0, 16).map((entry) => ({
    id: entry && typeof entry.id === "string" ? entry.id : "",
    name: entry && typeof entry.name === "string" ? entry.name : "",
    type: entry && entry.type === "stylesheet" ? "stylesheet" : "script",
    url: entry && typeof entry.url === "string" ? entry.url : "",
    enabled: Boolean(entry && entry.enabled),
    ...libraryExtras(entry),
  }));
}

function recentMaxOf(raw) {
  const number = typeof raw === "number" ? raw : Number.parseInt(raw, 10);
  if (!Number.isFinite(number)) {
    return 12;
  }
  return Math.min(48, Math.max(1, Math.round(number)));
}

function recentOf(raw, max) {
  if (!Array.isArray(raw)) {
    return [];
  }
  const out = [];
  const seen = new Set();
  for (const item of raw) {
    if (typeof item !== "string") {
      continue;
    }
    const text = item.trim();
    if (!text || text.length > 8192 || seen.has(text)) {
      continue;
    }
    seen.add(text);
    out.push(text);
    if (out.length >= max) {
      break;
    }
  }
  return out;
}

function swatchesOf(raw) {
  if (!Array.isArray(raw)) {
    return [];
  }
  return swatchList(raw);
}

/* Flags menu size. Missing or invalid stays omitted so the CSS size is used. */
function flagBoxOf(raw) {
  if (!isObject(raw)) {
    return null;
  }
  const width = Math.round(Number(raw.width));
  const height = Math.round(Number(raw.height));
  if (!Number.isFinite(width) || !Number.isFinite(height)) {
    return null;
  }
  return {
    width: Math.min(360, Math.max(160, width)),
    height: Math.min(880, Math.max(120, height)),
  };
}

/* A saved Accounts table/form zoom moves once onto the shared keys. */
function zoomOf(raw) {
  const src = isObject(raw) ? { ...raw } : {};
  if (src.tables === undefined && src.accountsTable !== undefined) {
    src.tables = src.accountsTable;
  }
  if (src.details === undefined && src.accountsForm !== undefined) {
    src.details = src.accountsForm;
  }
  if (src.filters === undefined && src.helpFilter !== undefined) {
    src.filters = src.helpFilter;
  }
  delete src.accountsTable;
  delete src.accountsForm;
  delete src.helpFilter;
  const zoom = {};
  for (const key of ZOOM_KEYS) {
    zoom[key] = snapZoom(src[key] === undefined ? DEFAULT_UI.zoom[key] : src[key], key);
  }
  /* Which roll increases a pane. Not a scale, so it stays out of ZOOM_KEYS. */
  zoom.wheel = src.wheel === "toward" ? "toward" : "away";
  copyUnknown(zoom, src, [...ZOOM_KEYS, "wheel"]);
  return zoom;
}

/* Fills defaults and clamps. Unknown keys are kept. */
export function clampUi(raw) {
  const src = isObject(raw) ? raw : {};
  const rail = railOf(src.rail);
  const zoom = zoomOf(src.zoom);
  const out = {
    nav: panePx(src.nav, DEFAULT_UI.nav),
    trees: treesOf(src.trees),
    mail: paneSplit(src.mail, DEFAULT_UI.mail),
    contacts: paneSplit(src.contacts, DEFAULT_UI.contacts),
    files: paneSplit(src.files, DEFAULT_UI.files),
    help: paneSplit(src.help, DEFAULT_UI.help),
    accounts: paneSplit(src.accounts, DEFAULT_UI.accounts, true),
    iconPane: paneSplit(src.iconPane, DEFAULT_UI.iconPane, true),
    libraryPane: paneSplit(src.libraryPane, DEFAULT_UI.libraryPane, true),
    assetPane: paneSplit(src.assetPane, DEFAULT_UI.assetPane, true),
    zoom,
    icons: iconsOf(src.icons),
    iconRecentMax: recentMaxOf(src.iconRecentMax),
    iconRecent: recentOf(src.iconRecent, recentMaxOf(src.iconRecentMax)),
    colorSwatches: swatchesOf(src.colorSwatches),
    colorPickerZoom: colorZoomOf(src.colorPickerZoom),
    iconWizardZoom: src.iconWizardZoom === undefined || src.iconWizardZoom === null || src.iconWizardZoom === ""
      ? 1
      : colorZoomOf(src.iconWizardZoom),
    iconResultZoom: src.iconResultZoom === undefined || src.iconResultZoom === null || src.iconResultZoom === "" || !Number.isFinite(Number(src.iconResultZoom))
      ? 1
      : colorZoomOf(src.iconResultZoom),
    fontawesome_kit: typeof src.fontawesome_kit === "string" ? src.fontawesome_kit : DEFAULT_UI.fontawesome_kit,
    fonts: fontsOf(src.fonts),
    activity: activityOf(src.activity),
    log: normalizeFilter(src.log === undefined ? DEFAULT_UI.log : src.log),
    session: clampSession(src.session),
    features: featuresOf(src.features),
    rail,
    chords: chordsOf(src.chords, rail),
    formats: normalizeFormats(src.formats),
  };
  const libraries = librariesOf(src.iconLibraries);
  if (libraries) {
    out.iconLibraries = libraries;
  }
  const flagBox = flagBoxOf(src.iconFlagBox);
  if (flagBox) {
    out.iconFlagBox = flagBox;
  }
  if (src.iconsResetD9 === true) {
    out.iconsResetD9 = true;
  }
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
