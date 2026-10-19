/* The Log view's time range (H-4 Lock, Time range). Presets are 12h (the
   default), 24h, 7d, and all; Custom is {since, until} in UTC ms. The range
   is kept in preferences.ui.log.range. */

export const RANGE_PRESETS = ["12h", "24h", "7d", "all"];
export const DEFAULT_RANGE = "12h";

const HOUR = 60 * 60 * 1000;
const SPAN = { "12h": 12 * HOUR, "24h": 24 * HOUR, "7d": 7 * 24 * HOUR };

function msOrNull(value) {
  return Number.isInteger(value) && value >= 0 ? value : null;
}

export function normalizeRange(raw) {
  if (typeof raw === "string" && RANGE_PRESETS.includes(raw)) {
    return raw;
  }
  if (raw !== null && typeof raw === "object" && !Array.isArray(raw)) {
    const since = msOrNull(raw.since);
    const until = msOrNull(raw.until);
    if (since !== null || until !== null) {
      if (since !== null && until !== null && until <= since) {
        return DEFAULT_RANGE;
      }
      const out = {};
      if (since !== null) {
        out.since = since;
      }
      if (until !== null) {
        out.until = until;
      }
      return out;
    }
  }
  return DEFAULT_RANGE;
}

/* {since?, until?} in UTC ms for log.query; an absent key is open. */
export function rangeBounds(raw, now = Date.now()) {
  const range = normalizeRange(raw);
  if (typeof range === "string") {
    return range === "all" ? {} : { since: now - SPAN[range] };
  }
  return { ...range };
}

export function lineTime(entry) {
  const ms = entry && typeof entry.ts === "string" ? Date.parse(entry.ts) : NaN;
  return Number.isFinite(ms) ? ms : null;
}

export function inRange(entry, bounds) {
  const ms = lineTime(entry);
  if (ms === null) {
    return bounds.since === undefined && bounds.until === undefined;
  }
  if (bounds.since !== undefined && ms < bounds.since) {
    return false;
  }
  return !(bounds.until !== undefined && ms >= bounds.until);
}

/* True when next reaches further back than prev, so older lines are read. */
export function rangeWidened(prev, next, now = Date.now()) {
  const a = rangeBounds(prev, now);
  const b = rangeBounds(next, now);
  if (b.since === undefined) {
    return a.since !== undefined;
  }
  return a.since !== undefined && b.since < a.since;
}

/* datetime-local text in the browser's zone, and back. */
export function toLocalInput(ms) {
  if (!Number.isFinite(ms)) {
    return "";
  }
  const d = new Date(ms);
  const pad = (n) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

export function fromLocalInput(text) {
  if (typeof text !== "string" || text === "") {
    return null;
  }
  const ms = new Date(text).getTime();
  return Number.isFinite(ms) ? ms : null;
}
