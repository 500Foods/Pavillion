/* Screen text for one activity row. The core owns the record. */

export const MAIL_ICON = "fa-sharp-duotone fa-solid fa-paper-plane-top fa-rotate-270";

export const APP_ACCOUNT = {
  number: "000",
  name: "Pavillion",
  icon: MAIL_ICON,
};

const KIND_ROWS = {
  "sync.mail": ["fa-envelope", "Mail sync"],
  "sync.contacts": ["fa-address-book", "Contacts sync"],
  "sync.calendar": ["fa-calendar-days", "Calendar sync"],
  "sync.files": ["fa-folder", "Files sync"],
  "body.fetch": ["fa-file-lines", "Fetch body"],
  "attachment.download": ["fa-paperclip", "Attachment"],
  "files.upload": ["fa-arrow-up-from-bracket", "Upload"],
  "files.download": ["fa-arrow-down-to-bracket", "Download"],
  send: ["fa-paper-plane", "Send"],
  "index.rebuild": ["fa-magnifying-glass", "Index"],
  "rules.run": ["fa-filter", "Rules"],
  import: ["fa-file-import", "Import"],
  export: ["fa-file-export", "Export"],
  retention: ["fa-broom", "Retention"],
  "cache.evict": ["fa-box-archive", "Evict cache"],
  "search.server": ["fa-magnifying-glass", "Server search"],
  "queue.flush": ["fa-list-check", "Flush queue"],
  "account.discover": ["fa-wand-magic-sparkles", "Configure account"],
  "account.test": ["fa-plug", "Test account"],
};

const ICON_CLASS = /^[-_a-zA-Z0-9 ]+$/;

export function iconClass(value, fallback) {
  if (typeof value !== "string") {
    return fallback;
  }
  const clean = value.trim();
  if (!clean || !ICON_CLASS.test(clean)) {
    return fallback;
  }
  return clean;
}

export function kindFace(kind) {
  const row = KIND_ROWS[kind];
  if (!row) {
    return {
      icon: "fa-sharp fa-thin fa-circle",
      label: typeof kind === "string" && kind !== "" ? kind : "Activity",
    };
  }
  return { icon: `fa-sharp fa-thin ${row[0]}`, label: row[1] };
}

export function padNumber(value) {
  if (typeof value === "number" && Number.isFinite(value)) {
    return String(Math.trunc(Math.abs(value))).padStart(3, "0");
  }
  const text = typeof value === "string" ? value.trim() : "";
  if (/^\d+$/.test(text)) {
    return text.padStart(3, "0");
  }
  return text;
}

/* No accountId is the app itself: 000, the mail icon, and Pavillion.
   A known account shows its number, icon, and name. */
/* null kinds or accounts means every value. A missing accountId matches "". */
export function activityMatches(activity, kinds, accounts) {
  if (!activity) {
    return false;
  }
  const kind = typeof activity.kind === "string" ? activity.kind : "";
  if (Array.isArray(kinds) && !kinds.includes(kind)) {
    return false;
  }
  const accountId = typeof activity.accountId === "string" ? activity.accountId : "";
  if (Array.isArray(accounts) && !accounts.includes(accountId)) {
    return false;
  }
  return true;
}

export function accountFace(accountId, accounts) {
  if (typeof accountId !== "string" || accountId === "") {
    return APP_ACCOUNT;
  }
  const found = (Array.isArray(accounts) ? accounts : []).find((item) => item && item.id === accountId);
  if (!found) {
    return { number: "—", name: accountId, icon: "fa-sharp fa-thin fa-circle" };
  }
  const name = typeof found.name === "string" && found.name.trim() !== "" ? found.name.trim() : accountId;
  return {
    number: padNumber(found.number),
    name,
    icon: iconClass(found.icon, MAIL_ICON),
  };
}

export function groupInt(value) {
  const n = Math.trunc(value);
  if (!Number.isFinite(n)) {
    return "";
  }
  const sign = n < 0 ? "-" : "";
  const digits = String(Math.abs(n));
  return sign + digits.replace(/\B(?=(\d{3})+(?!\d))/g, ",");
}

export function progressText(progress) {
  if (!progress || !Number.isFinite(progress.done)) {
    return "";
  }
  if (Number.isFinite(progress.total) && progress.total >= 0) {
    return `${groupInt(progress.done)}/${groupInt(progress.total)}`;
  }
  return groupInt(progress.done);
}

/* Under a minute the core's example is "in 5s". From a minute it is "2m". */
export function formatEta(ms) {
  if (!Number.isFinite(ms) || ms < 0) {
    return "";
  }
  const sec = Math.round(ms / 1000);
  if (sec < 60) {
    return `in ${sec}s`;
  }
  const min = Math.round(sec / 60);
  if (min < 60) {
    return `${min}m`;
  }
  const hours = Math.floor(min / 60);
  const rest = min % 60;
  return rest === 0 ? `${hours}h` : `${hours}h ${rest}m`;
}

export function localClock(ms, timeZone) {
  const zone = typeof timeZone === "string" && timeZone !== "" ? timeZone : "America/Vancouver";
  const date = new Date(ms);
  if (!Number.isFinite(ms) || Number.isNaN(date.getTime())) {
    return "--:--:--";
  }
  let parts;
  try {
    parts = new Intl.DateTimeFormat("en-GB", {
      timeZone: zone,
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
      hourCycle: "h23",
    }).formatToParts(date);
  } catch {
    return "--:--:--";
  }
  const pick = (type) => {
    const part = parts.find((item) => item.type === type);
    return part ? part.value : "";
  };
  let hour = pick("hour");
  if (hour === "24") {
    hour = "00";
  }
  const minute = pick("minute");
  const second = pick("second");
  if (hour.length !== 2 || minute.length !== 2 || second.length !== 2) {
    return "--:--:--";
  }
  return `${hour}:${minute}:${second}`;
}

export function stateLabel(state) {
  if (state === "queued") return "Queued";
  if (state === "running") return "Running";
  if (state === "done") return "Done";
  if (state === "failed") return "Failed";
  if (state === "cancelled") return "Cancelled";
  return typeof state === "string" ? state : "";
}

const FINISHED = new Set(["done", "failed", "cancelled"]);

export function listedActivity(activity, history) {
  if (!activity) {
    return false;
  }
  if (activity.state === "running" || activity.state === "queued") {
    return true;
  }
  if (activity.state === "failed" && activity.dismissed !== true) {
    return true;
  }
  return history === true;
}

/* A delayed event must not walk a finished row back to running, or lower a count. */
export function preferActivity(prev, next) {
  if (!prev) {
    return next;
  }
  if (!next) {
    return prev;
  }
  if (FINISHED.has(prev.state) && !FINISHED.has(next.state)) {
    return prev;
  }
  if (!FINISHED.has(prev.state) && !FINISHED.has(next.state)) {
    const prevDone = prev.progress && Number.isFinite(prev.progress.done) ? prev.progress.done : -1;
    const nextDone = next.progress && Number.isFinite(next.progress.done) ? next.progress.done : -1;
    if (nextDone < prevDone) {
      return prev;
    }
  }
  return next;
}

function runningSync(activity) {
  return Boolean(activity && activity.state === "running" && typeof activity.kind === "string" && activity.kind.startsWith("sync."));
}

export function syncingCount(activities) {
  let n = 0;
  for (const activity of activities) {
    if (runningSync(activity)) {
      n += 1;
    }
  }
  return n;
}

/* count matches syncingCount. turn is sum(done) / sum(total) when any
   running sync has a positive total, otherwise null. */
export function syncSnapshot(activities) {
  const ids = [];
  let count = 0;
  let done = 0;
  let total = 0;
  let known = false;
  for (const activity of activities) {
    if (!runningSync(activity)) {
      continue;
    }
    count += 1;
    if (typeof activity.id === "string" && activity.id !== "") {
      ids.push(activity.id);
    }
    const progress = activity.progress;
    if (progress && Number.isFinite(progress.total) && progress.total > 0) {
      known = true;
      done += Number.isFinite(progress.done) ? progress.done : 0;
      total += progress.total;
    }
  }
  return {
    count,
    ids,
    turn: known && total > 0 ? (done / total) * 100 : null,
  };
}
