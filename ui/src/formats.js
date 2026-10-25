/* D-8: the only module that turns a stored instant into display text.
   Strings live in preferences.ui.formats. The core does not read them.
   A missing time string follows time_format. A saved string that differs
   from that default wins. dateShort is the message-list rule. */

import { DateTime, Duration, IANAZone } from "luxon";

export const FORMAT_KINDS = [
  "date",
  "time",
  "timeSeconds",
  "datetime",
  "dateShort",
  "weekday",
  "relative",
  "duration",
];

export const RELATIVE_STYLES = ["long", "short", "narrow"];

/* 2026-10-14 15:04:05.006 in America/Vancouver. */
export const SAMPLE_INSTANT = Date.parse("2026-10-14T22:04:05.006Z");
/* 3:04:05.006 */
export const SAMPLE_DURATION_MS = (3 * 3600 + 4 * 60 + 5) * 1000 + 6;

const MAX_PATTERN = 64;

const DATE_TOKENS = new Set([
  "y", "yy", "yyyy",
  "M", "MM", "MMM", "MMMM", "MMMMM",
  "d", "dd",
  "E", "EEE", "EEEE", "EEEEE",
  "H", "HH",
  "h", "hh",
  "m", "mm",
  "s", "ss",
  "S", "SSS",
  "a",
  "Z", "ZZ", "ZZZ", "ZZZZ", "ZZZZZ",
  "z",
]);

const DURATION_TOKENS = new Set([
  "y", "yy", "yyyy",
  "M", "MM",
  "w", "ww",
  "d", "dd",
  "h", "hh",
  "m", "mm",
  "s", "ss",
  "S", "SS", "SSS",
]);

const DEFAULTS = {
  date: "yyyy-MM-dd",
  time: "HH:mm",
  timeSeconds: "HH:mm:ss.SSS",
  datetime: "yyyy-MM-dd HH:mm",
  dateShort: "MMM d",
  weekday: "EEEE",
  relative: "long",
  duration: "h:mm:ss",
};

const DEFAULTS_12 = {
  time: "h:mm a",
  timeSeconds: "h:mm:ss.SSS a",
  datetime: "yyyy-MM-dd h:mm a",
};

const TIME_KINDS = new Set(["time", "timeSeconds", "datetime"]);

const listeners = new Set();

let state = {
  zone: "America/Vancouver",
  locale: "en",
  timeFormat: "24h",
  formats: {},
};

export function defaultPattern(kind, timeFormat) {
  if (TIME_KINDS.has(kind) && timeFormat === "12h" && DEFAULTS_12[kind]) {
    return DEFAULTS_12[kind];
  }
  return DEFAULTS[kind] || "";
}

export function zoneError(name) {
  if (typeof name !== "string" || name === "" || !IANAZone.isValidZone(name)) {
    return "That time zone is not available";
  }
  return "";
}

export function localeError(locale) {
  if (typeof locale !== "string" || locale.trim() === "") {
    return "";
  }
  const name = locale.trim();
  if (name.length > 35) {
    return "That locale is not available";
  }
  try {
    if (Intl.DateTimeFormat.supportedLocalesOf([name]).length === 0) {
      return "That locale is not available";
    }
  } catch {
    return "That locale is not available";
  }
  return "";
}

/* Letter runs outside quotes must be tokens Help lists. An empty pattern,
   a pattern with no token, and an unclosed quote are refused. */
export function patternError(kind, pattern) {
  if (kind === "relative") {
    return RELATIVE_STYLES.includes(pattern) ? "" : "Relative is long, short, or narrow";
  }
  if (typeof pattern !== "string" || pattern.trim() === "") {
    return "Enter a format";
  }
  if (pattern.length > MAX_PATTERN) {
    return "That format is too long";
  }
  const allowed = kind === "duration" ? DURATION_TOKENS : DATE_TOKENS;
  const scanned = scanFormat(pattern);
  if (scanned.error !== "") {
    return scanned.error;
  }
  if (scanned.tokens.length === 0) {
    return "Enter a format token";
  }
  for (const token of scanned.tokens) {
    if (!allowed.has(token)) {
      return `Unknown token ${token}`;
    }
  }
  return "";
}

function scanFormat(pattern) {
  const tokens = [];
  let i = 0;
  let quoted = false;
  while (i < pattern.length) {
    const c = pattern[i];
    if (c === "'") {
      if (quoted && pattern[i + 1] === "'") {
        i += 2;
        continue;
      }
      quoted = !quoted;
      i += 1;
      continue;
    }
    if (quoted) {
      i += 1;
      continue;
    }
    if (/[A-Za-z]/.test(c)) {
      let j = i + 1;
      while (j < pattern.length && pattern[j] === c) {
        j += 1;
      }
      tokens.push(pattern.slice(i, j));
      i = j;
      continue;
    }
    i += 1;
  }
  if (quoted) {
    return { error: "A quote is not closed", tokens };
  }
  return { error: "", tokens };
}

export function normalizeFormats(raw) {
  const src = raw !== null && typeof raw === "object" && !Array.isArray(raw) ? raw : {};
  const out = {};
  if (typeof src.locale === "string" && localeError(src.locale) === "" && src.locale.trim() !== "") {
    out.locale = src.locale.trim();
  }
  for (const kind of FORMAT_KINDS) {
    if (typeof src[kind] !== "string") {
      continue;
    }
    const value = src[kind].trim();
    if (value !== "" && patternError(kind, value) === "") {
      out[kind] = value;
    }
  }
  return out;
}

export function zoneFallback(timezone, systemZone) {
  if (zoneError(timezone) === "") {
    return timezone;
  }
  if (zoneError(systemZone) === "") {
    return systemZone;
  }
  return "America/Vancouver";
}

function systemZoneName() {
  const name = DateTime.local().zoneName;
  return zoneError(name) === "" ? name : "";
}

function systemLocale() {
  const name = typeof navigator !== "undefined" && typeof navigator.language === "string"
    ? navigator.language
    : "";
  return localeError(name) === "" && name.trim() !== "" ? name : "en";
}

export function effectivePattern(kind, formats, timeFormat) {
  const stored = formats && typeof formats[kind] === "string" ? formats[kind] : "";
  const fallback = defaultPattern(kind, timeFormat === "12h" ? "12h" : "24h");
  if (stored !== "" && stored !== fallback && patternError(kind, stored) === "") {
    return stored;
  }
  return fallback;
}

export function configureFormats(input = {}) {
  const timeFormat = input.timeFormat === "12h" ? "12h" : "24h";
  const formats = normalizeFormats(input.formats);
  const system = typeof input.systemZone === "string" ? input.systemZone : systemZoneName();
  const requested = typeof input.timezone === "string" ? input.timezone : "";
  const locale = formats.locale || systemLocale();
  state = {
    zone: zoneFallback(requested, system),
    locale,
    timeFormat,
    formats,
  };
  for (const fn of listeners) {
    fn();
  }
  return currentFormats();
}

export function currentZone() {
  return state.zone;
}

export function currentFormats() {
  return {
    zone: state.zone,
    locale: state.locale,
    timeFormat: state.timeFormat,
    formats: { ...state.formats },
  };
}

export function subscribeFormats(fn) {
  listeners.add(fn);
  return () => listeners.delete(fn);
}

function toMillis(value) {
  if (typeof value === "number" && Number.isFinite(value)) {
    return value;
  }
  if (typeof value === "string" && value !== "") {
    const parsed = DateTime.fromISO(value, { setZone: true });
    if (parsed.isValid) {
      return parsed.toMillis();
    }
    const ms = Date.parse(value);
    if (Number.isFinite(ms)) {
      return ms;
    }
  }
  return null;
}

function resolveZone(override) {
  if (override === undefined) {
    return state.zone;
  }
  return zoneError(override) === "" ? override : "";
}

function resolveLocale(override) {
  if (override === undefined || override === "") {
    return state.locale;
  }
  return localeError(override) === "" ? override : state.locale;
}

function patternFor(kind, options) {
  const formats = options.patterns || state.formats;
  const timeFormat = options.timeFormat === "12h" || options.timeFormat === "24h"
    ? options.timeFormat
    : state.timeFormat;
  return effectivePattern(kind, formats, timeFormat);
}

/* value is epoch milliseconds or an ISO instant. duration also takes a
   millisecond count or an ISO duration (a string that starts with P). */
export function formatInstant(value, kind, options = {}) {
  if (!FORMAT_KINDS.includes(kind)) {
    return "";
  }
  const zone = resolveZone(options.zone);
  if (zone === "") {
    return "";
  }
  const locale = resolveLocale(options.locale);
  if (kind === "duration") {
    return formatDuration(value, patternFor(kind, options));
  }
  const ms = toMillis(value);
  if (ms === null) {
    return "";
  }
  const dt = DateTime.fromMillis(ms, { zone, locale });
  if (!dt.isValid) {
    return "";
  }
  if (kind === "relative") {
    const base = options.now === undefined ? DateTime.now().setZone(zone).setLocale(locale) : null;
    const nowMs = base ? null : toMillis(options.now);
    const now = base || (nowMs === null ? null : DateTime.fromMillis(nowMs, { zone, locale }));
    if (!now || !now.isValid) {
      return "";
    }
    return dt.toRelative({ base: now, style: patternFor("relative", options), locale }) || "";
  }
  if (kind === "dateShort") {
    const nowMs = options.now === undefined ? Date.now() : toMillis(options.now);
    if (nowMs === null) {
      return "";
    }
    const now = DateTime.fromMillis(nowMs, { zone, locale });
    if (!now.isValid) {
      return "";
    }
    if (dt.hasSame(now, "day")) {
      return normalizeAmpm(dt.toFormat(patternFor("time", options)), patternFor("time", options));
    }
    if (dt.hasSame(now, "year")) {
      return normalizeAmpm(dt.toFormat(patternFor("dateShort", options)), patternFor("dateShort", options));
    }
    return normalizeAmpm(dt.toFormat(patternFor("date", options)), patternFor("date", options));
  }
  return normalizeAmpm(dt.toFormat(patternFor(kind, options)), patternFor(kind, options));
}

/* ICU locale data varies: "en" can render "a.m." or "AM." or "am" depending
   on the platform and ICU data version. Normalize to uppercase AM/PM. */
function normalizeAmpm(text, pattern) {
  if (!pattern.includes("a")) {
    return text;
  }
  return text.replace(/\b(a\.?m\.?|p\.?m\.?)/gi, (m) => (m.toLowerCase().startsWith("a") ? "AM" : "PM"));
}function formatDuration(value, pattern) {
  let dur = null;
  if (typeof value === "number" && Number.isFinite(value)) {
    dur = Duration.fromMillis(value);
  } else if (typeof value === "string" && value.startsWith("P")) {
    dur = Duration.fromISO(value);
  }
  if (!dur || !dur.isValid) {
    return "";
  }
  return dur.toFormat(pattern);
}

export function timeZoneNames() {
  try {
    if (typeof Intl.supportedValuesOf === "function") {
      const names = Intl.supportedValuesOf("timeZone");
      if (Array.isArray(names) && names.length > 0) {
        return names;
      }
    }
  } catch {
    /* The text field is the fallback when the list is missing. */
  }
  return ["America/Vancouver", "UTC"];
}
