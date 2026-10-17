export const DEFAULT_UI = {
  nav: { width: 320, min: 200, max: 500 },
  mail: { split: 0.5, min: 0.25, max: 0.75 },
  contacts: { split: 0.5, min: 0.25, max: 0.75 },
  files: { split: 0.5, min: 0.25, max: 0.75 },
  zoom: { shell: 1, navButtons: 1, navBody: 1, mailList: 1, mailDetail: 1, log: 1 },
  activity: { view: "log" },
  icons: {},
  fontawesome_kit: "45801522ef",
  fonts: {
    body: '"Vanadium Sans", system-ui, sans-serif',
    mono: "ui-monospace, monospace",
  },
};

const ZOOM_KEYS = ["shell", "navButtons", "navBody", "mailList", "mailDetail", "log"];
const KNOWN = new Set(["nav", "mail", "contacts", "files", "zoom", "icons", "fontawesome_kit", "fonts", "activity"]);

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function finite(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

/* 5% through 500%, in steps of 5%. A missing or non-numeric value is 100%. */
export function snapZoom(value) {
  const n = finite(value);
  if (n === null) {
    return 1;
  }
  const clamped = Math.min(5, Math.max(0.05, n));
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

function activityOf(src) {
  const raw = isObject(src) ? src : {};
  const out = { view: raw.view === "activity" ? "activity" : "log" };
  copyUnknown(out, raw, ["view"]);
  return out;
}

/* Fills defaults and clamps. Unknown keys are kept. */
export function clampUi(raw) {
  const src = isObject(raw) ? raw : {};
  const zoomSrc = isObject(src.zoom) ? src.zoom : {};
  const zoom = {};
  for (const key of ZOOM_KEYS) {
    zoom[key] = snapZoom(zoomSrc[key] === undefined ? DEFAULT_UI.zoom[key] : zoomSrc[key]);
  }
  copyUnknown(zoom, zoomSrc, ZOOM_KEYS);
  const out = {
    nav: panePx(src.nav, DEFAULT_UI.nav),
    mail: paneSplit(src.mail, DEFAULT_UI.mail),
    contacts: paneSplit(src.contacts, DEFAULT_UI.contacts),
    files: paneSplit(src.files, DEFAULT_UI.files),
    zoom,
    icons: isObject(src.icons) ? { ...src.icons } : {},
    fontawesome_kit: typeof src.fontawesome_kit === "string" ? src.fontawesome_kit : DEFAULT_UI.fontawesome_kit,
    fonts: fontsOf(src.fonts),
    activity: activityOf(src.activity),
  };
  for (const key of Object.keys(src)) {
    if (!KNOWN.has(key)) {
      out[key] = src[key];
    }
  }
  return out;
}
