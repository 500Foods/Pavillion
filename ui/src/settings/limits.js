/* Settings → Limits (L Lock, J Lock): preferences.cache bodiesMb,
   blobsMb, and listRows, labeled Database, Document, and Message list.
   Refresh reloads them. Apply writes them through prefs.set. The page
   clamps the megabyte values to 1..1048576 and the message list to
   1..10000. Recent icons is preferences.ui.iconRecentMax (1..48). Apply
   writes it through onRecent, which saves the ui blob, and a fixture
   without that field leaves it alone. The log size cap stays on Logs.
   A fixture without the message-list field omits listRows. */

import { iconClass } from "../common/icons/registry.js";
import { moduleFooter, moduleToolbar, mountBar } from "../common/module-bar.js";
import { formatInstant } from "../formats.js";

const MIN_MB = 1;
const MAX_MB = 1048576;
const MIN_ROWS = 1;
const MAX_ROWS = 10000;
const MIN_RECENT = 1;
const MAX_RECENT = 48;
const DEFAULT_RECENT = 12;
const DEFAULT_BODIES = 2048;
const DEFAULT_BLOBS = 4096;
const DEFAULT_ROWS = 10000;
const MB = 1024 * 1024;
const GB = 1024 * 1024 * 1024;

/* Megabytes with one decimal, matching the limit unit. suffix is
   "on disk" beside the limits. A missing figure is blank. */
export function formatCacheMb(bytes, suffix) {
  if (!Number.isInteger(bytes) || bytes < 0) {
    return "";
  }
  const shown = (Math.round((bytes / MB) * 10) / 10).toLocaleString("en-US", {
    minimumFractionDigits: 1,
    maximumFractionDigits: 1,
  });
  const text = `${shown} MB`;
  return suffix ? `${text} ${suffix}` : text;
}

/* Free space on the storage volume. Gigabytes once it reaches 1 GB,
   still one decimal. A missing figure is blank. The word is added by
   the line that places this beside the on-disk figure. */
export function formatAvailable(bytes) {
  if (!Number.isInteger(bytes) || bytes < 0) {
    return "";
  }
  if (bytes >= GB) {
    const shown = (Math.round((bytes / GB) * 10) / 10).toLocaleString("en-US", {
      minimumFractionDigits: 1,
      maximumFractionDigits: 1,
    });
    return `${shown} GB`;
  }
  return formatCacheMb(bytes);
}

/* "0.0 MB on disk, 16.1 GB available". The available half is left off
   when the volume cannot be read. */
export function formatDiskLine(used, free) {
  const disk = formatCacheMb(used, "on disk");
  const avail = formatAvailable(free);
  if (disk === "") {
    return avail === "" ? "" : `${avail} available`;
  }
  if (avail === "") {
    return disk;
  }
  return `${disk}, ${avail} available`;
}

export function formatEntries(count) {
  if (!Number.isInteger(count) || count < 0) {
    return "";
  }
  const word = count === 1 ? "entry" : "entries";
  return `${count.toLocaleString("en-US")} ${word}`;
}

/* "54,070 entries, 4,632.0 MB". Either half is left off when unknown. */
export function formatPoolCell(count, bytes) {
  const entries = formatEntries(count);
  const size = formatCacheMb(bytes);
  if (entries !== "" && size !== "") {
    return `${entries}, ${size}`;
  }
  return entries !== "" ? entries : size;
}

function knownBytes(value) {
  return Number.isInteger(value) && value >= 0 ? value : null;
}

function addCount(sum, figure) {
  const n = knownBytes(figure && figure.count);
  if (n === null) {
    return sum;
  }
  if (sum === null) {
    return n;
  }
  if (sum > Number.MAX_SAFE_INTEGER - n) {
    return sum;
  }
  return sum + n;
}

function addKnown(sum, value) {
  const n = knownBytes(value);
  if (n === null) {
    return sum;
  }
  if (sum > Number.MAX_SAFE_INTEGER - n) {
    return sum;
  }
  return sum + n;
}

/* One account's pool. count and bytes are null when that half is not
   known. A missing contact, calendar, or file size is left out. Document
   stays blank until the attachment split is known, so a file count of 0
   is not the whole Document figure. */
export function poolFigure(account, pool) {
  const row = account || {};
  const mail = row.mail;
  if (pool === "database") {
    let count = null;
    count = addCount(count, mail);
    count = addCount(count, row.contacts);
    count = addCount(count, row.calendars);
    const split = knownBytes(mail && mail.bodiesBytes);
    if (split === null) {
      return { count, bytes: null };
    }
    let bytes = split;
    bytes = addKnown(bytes, row.contacts && row.contacts.bytes);
    bytes = addKnown(bytes, row.calendars && row.calendars.bytes);
    return { count, bytes };
  }
  const split = knownBytes(mail && mail.attachmentsBytes);
  if (split === null) {
    return { count: null, bytes: null };
  }
  const attachCount = knownBytes(mail && mail.attachmentsCount);
  let count = attachCount;
  if (count === null && split === 0) {
    count = 0;
  }
  if (attachCount !== null || split === 0) {
    count = addCount(count, row.files);
  }
  let bytes = split;
  bytes = addKnown(bytes, row.files && row.files.bytes);
  return { count, bytes };
}

/* Sum of one column across accounts. A missing count or size is left
   out. Database and Document are totaled apart. */
export function poolTotal(accounts, pool) {
  const rows = Array.isArray(accounts) ? accounts : [];
  let count = null;
  let bytes = null;
  for (const account of rows) {
    const figure = poolFigure(account, pool);
    count = addCount(count, figure);
    bytes = addKnown(bytes, figure.bytes);
  }
  return { count, bytes };
}

export function formatCount(count) {
  if (!Number.isInteger(count) || count < 0) {
    return "";
  }
  return count.toLocaleString("en-US");
}

/* Formats datetime in the profile zone. A missing stamp is blank. */
export function formatChecked(ms, timeZone) {
  if (!Number.isInteger(ms) || ms < 0) {
    return "";
  }
  const zone = typeof timeZone === "string" && timeZone !== "" ? timeZone : "America/Vancouver";
  return formatInstant(ms, "datetime", { zone });
}

function messageOf(err) {
  return err instanceof Error ? err.message : String(err);
}

function storedMb(cache, key, fallback) {
  const value = cache && cache[key];
  return Number.isInteger(value) && value >= MIN_MB && value <= MAX_MB ? value : fallback;
}

/* A blank or non-integer field goes back to the saved value. A number
   outside the range snaps to that end. The field shows what will be saved. */
function readBounded(input, saved, min, max) {
  const raw = input.value.trim();
  if (!/^[0-9]+$/.test(raw)) {
    input.value = String(saved);
    return saved;
  }
  let n = Number(raw);
  if (n < min) {
    n = min;
  }
  if (n > max) {
    n = max;
  }
  input.value = String(n);
  return n;
}

function readMb(input, saved) {
  return readBounded(input, saved, MIN_MB, MAX_MB);
}

function storedRows(cache) {
  const value = cache && cache.listRows;
  return Number.isInteger(value) && value >= MIN_ROWS && value <= MAX_ROWS ? value : DEFAULT_ROWS;
}

export function createLimitsSettings(api, root, options = {}) {
  const bodies = root.querySelector("#limits-bodies");
  const blobs = root.querySelector("#limits-blobs");
  const listRowsInput = root.querySelector("#limits-list-rows");
  const recentInput = root.querySelector("#limits-recent");
  if (!bodies || !blobs) {
    return {
      refresh: () => Promise.resolve(),
      apply: () => Promise.resolve(),
      measure: () => Promise.resolve(),
      repaintTimes() {},
      destroy() {},
    };
  }

  const bodiesUsed = root.querySelector("#limits-bodies-used");
  const blobsUsed = root.querySelector("#limits-blobs-used");
  const accountTable = root.querySelector("#limits-accounts");
  const accountRows = root.querySelector("#limits-account-rows");
  const totalDatabase = root.querySelector("#limits-total-database");
  const totalDocument = root.querySelector("#limits-total-document");
  const measureButton = root.querySelector("#limits-measure");
  const cancelButton = root.querySelector("#limits-cancel");
  const progress = root.querySelector("#limits-progress");
  const checked = root.querySelector("#limits-checked");
  let savedBodies = DEFAULT_BODIES;
  let savedBlobs = DEFAULT_BLOBS;
  let savedRows = DEFAULT_ROWS;
  let savedRecent = DEFAULT_RECENT;
  if (typeof options.recentMax === "function" && Number.isInteger(options.recentMax())) {
    savedRecent = options.recentMax();
  }
  let ticket = 0;
  let measuring = false;
  let stopEvents = null;

  function zone() {
    const value = typeof options.timeZone === "function" ? options.timeZone() : options.timeZone;
    return typeof value === "string" && value !== "" ? value : "America/Vancouver";
  }

  function settleButton() {
    measuring = false;
    if (measureButton) {
      measureButton.disabled = false;
      measureButton.textContent = "Check sizes";
    }
    if (cancelButton) {
      cancelButton.hidden = true;
      cancelButton.disabled = false;
    }
    if (progress) {
      progress.hidden = true;
      progress.value = 0;
    }
  }

  function report(err) {
    if (typeof options.onError === "function") {
      options.onError(messageOf(err));
    }
  }

  function cell(text, numeric) {
    const node = document.createElement("td");
    node.textContent = text;
    if (numeric) {
      node.className = "limits-num";
    }
    return node;
  }

  function poolCell(account, pool) {
    const figure = poolFigure(account, pool);
    return cell(formatPoolCell(figure.count, figure.bytes), true);
  }

  let lastUsage = null;

  function paintUsage(usage) {
    lastUsage = usage;
    const accounts = usage && Array.isArray(usage.accounts) ? usage.accounts : [];
    const when = formatChecked(usage && usage.sizesCheckedAt, zone());
    const free = usage && usage.volumeFreeBytes;
    if (bodiesUsed) {
      bodiesUsed.textContent = formatDiskLine(usage && usage.bodiesBytes, free);
    }
    if (blobsUsed) {
      blobsUsed.textContent = formatDiskLine(usage && usage.blobsBytes, free);
    }
    if (checked) {
      checked.textContent = when === "" ? "" : `Last checked ${when}`;
    }
    if (!accountTable || !accountRows) {
      return;
    }
    accountRows.replaceChildren();
    accountTable.hidden = accounts.length === 0;
    for (const account of accounts) {
      const row = document.createElement("tr");
      const name = document.createElement("td");
      const label = account && typeof account.name === "string" && account.name !== ""
        ? account.name
        : (account && account.id) || "";
      name.textContent = label;
      if (account && typeof account.id === "string") {
        row.dataset.accountId = account.id;
      }
      row.append(name);
      row.append(poolCell(account, "database"));
      row.append(poolCell(account, "document"));
      accountRows.append(row);
    }
    if (totalDatabase) {
      const figure = poolTotal(accounts, "database");
      totalDatabase.textContent = formatPoolCell(figure.count, figure.bytes);
    }
    if (totalDocument) {
      const figure = poolTotal(accounts, "document");
      totalDocument.textContent = formatPoolCell(figure.count, figure.bytes);
    }
  }

  function paint() {
    bodies.value = String(savedBodies);
    blobs.value = String(savedBlobs);
    if (listRowsInput) {
      listRowsInput.value = String(savedRows);
    }
    if (recentInput) {
      recentInput.value = String(savedRecent);
    }
  }

  function onProgress(payload) {
    const store = payload && payload.store;
    const label = store === "files" ? "files" : store === "mail" ? "mail" : "";
    let row = null;

    if (!measuring || label === "") {
      return;
    }
    if (progress) {
      if (Number.isInteger(payload.scanTotal) && payload.scanTotal > 0 &&
          Number.isInteger(payload.scanned) && payload.scanned >= 0) {
        progress.max = payload.scanTotal;
        progress.value = Math.min(payload.scanned, payload.scanTotal);
      } else {
        progress.removeAttribute("value");
      }
    }
    if (!checked || !accountRows) {
      return;
    }
    for (const item of accountRows.children) {
      if (item.dataset.accountId === payload.accountId) {
        row = item;
        break;
      }
    }
    const who = row && row.cells[0] ? row.cells[0].textContent : "";
    const doneText = formatCount(payload.done);
    const totalText = formatCount(payload.total);
    if (who !== "" && doneText !== "" && totalText !== "") {
      checked.textContent = `Checking ${who} ${label}, ${doneText} of ${totalText}`;
    } else if (who !== "") {
      checked.textContent = `Checking ${who} ${label}`;
    }
  }

  function repaintTimes() {
    if (lastUsage) {
      paintUsage(lastUsage);
    }
  }

  function refresh() {
    if (measuring) {
      return Promise.resolve(null);
    }
    const mine = ticket + 1;
    ticket = mine;
    const usageCall = api.cache && typeof api.cache.usage === "function"
      ? api.cache.usage().catch((err) => {
        if (mine === ticket) {
          report(err);
        }
        return null;
      })
      : Promise.resolve(null);
    return Promise.all([api.prefs.get(), usageCall]).then(([prefs, usage]) => {
      if (mine !== ticket) {
        return null;
      }
      const cache = prefs && prefs.cache;
      savedBodies = storedMb(cache, "bodiesMb", DEFAULT_BODIES);
      savedBlobs = storedMb(cache, "blobsMb", DEFAULT_BLOBS);
      savedRows = storedRows(cache);
      if (typeof options.recentMax === "function" && Number.isInteger(options.recentMax())) {
        savedRecent = options.recentMax();
      }
      paint();
      if (usage) {
        paintUsage(usage);
      }
      settleButton();
      return prefs;
    }).catch((err) => {
      if (mine === ticket) {
        report(err);
        settleButton();
      }
      return null;
    });
  }

  function measure() {
    const mine = ticket + 1;
    ticket = mine;
    if (!api.cache || typeof api.cache.usage !== "function") {
      return Promise.resolve(null);
    }
    measuring = true;
    if (measureButton) {
      measureButton.disabled = true;
      measureButton.textContent = "Checking…";
    }
    if (cancelButton) {
      cancelButton.hidden = false;
      cancelButton.disabled = false;
    }
    if (progress) {
      progress.hidden = false;
      progress.removeAttribute("value");
    }
    return api.cache.usage({ measure: true }).then((usage) => {
      if (mine !== ticket) {
        return null;
      }
      if (usage) {
        paintUsage(usage);
        if (usage.cancelled && checked && checked.textContent === "") {
          checked.textContent = "Stopped";
        }
      }
      settleButton();
      return usage;
    }).catch((err) => {
      if (mine !== ticket) {
        return null;
      }
      report(err);
      settleButton();
      return null;
    });
  }

  async function apply() {
    const mine = ticket + 1;
    ticket = mine;
    const bodiesMb = readMb(bodies, savedBodies);
    const blobsMb = readMb(blobs, savedBlobs);
    const cachePatch = { bodiesMb, blobsMb };
    if (listRowsInput) {
      cachePatch.listRows = readBounded(listRowsInput, savedRows, MIN_ROWS, MAX_ROWS);
    }
    if (recentInput && typeof options.onRecent === "function") {
      const next = readBounded(recentInput, savedRecent, MIN_RECENT, MAX_RECENT);
      savedRecent = next;
      const pending = options.onRecent(next);
      if (pending && typeof pending.then === "function") {
        await pending;
      }
    }
    return api.prefs.set({ cache: cachePatch }).then((prefs) => {
      if (mine !== ticket) {
        return null;
      }
      const cache = prefs && prefs.cache;
      savedBodies = storedMb(cache, "bodiesMb", bodiesMb);
      savedBlobs = storedMb(cache, "blobsMb", blobsMb);
      savedRows = storedRows(cache);
      paint();
      settleButton();
      return prefs;
    }).catch((err) => {
      if (mine === ticket) {
        report(err);
        settleButton();
      }
      return null;
    });
  }

  function onBlur(event) {
    const input = event.currentTarget;
    if (input === listRowsInput) {
      readBounded(input, savedRows, MIN_ROWS, MAX_ROWS);
      return;
    }
    if (input === recentInput) {
      readBounded(input, savedRecent, MIN_RECENT, MAX_RECENT);
      return;
    }
    readMb(input, input === bodies ? savedBodies : savedBlobs);
  }

  function onKey(event) {
    if (event.key === "Enter") {
      event.preventDefault();
      apply();
    }
  }

  mountBar(root.querySelector("#limits-toolbar"), moduleToolbar({
    title: "Limits",
  }));
  mountBar(root.querySelector("#limits-footer"), moduleFooter({
    label: "Limits actions",
    onRefresh: () => {
      refresh();
    },
    end: [{
      action: "apply",
      icon: iconClass("settings.apply"),
      text: "Apply",
      label: "Save limits",
      onClick: () => {
        apply();
      },
    }],
  }));

  function onMeasure() {
    measure();
  }

  function onCancel() {
    if (!measuring || !api.cache || typeof api.cache.usage !== "function") {
      return;
    }
    if (cancelButton) {
      cancelButton.disabled = true;
    }
    const stop = api.cache.usage({ cancel: true });
    if (stop && typeof stop.catch === "function") {
      stop.catch(report);
    }
  }

  bodies.addEventListener("blur", onBlur);
  blobs.addEventListener("blur", onBlur);
  bodies.addEventListener("keydown", onKey);
  blobs.addEventListener("keydown", onKey);
  if (listRowsInput) {
    listRowsInput.addEventListener("blur", onBlur);
    listRowsInput.addEventListener("keydown", onKey);
  }
  if (recentInput) {
    recentInput.addEventListener("blur", onBlur);
    recentInput.addEventListener("keydown", onKey);
  }
  if (measureButton) {
    measureButton.addEventListener("click", onMeasure);
  }
  if (cancelButton) {
    cancelButton.addEventListener("click", onCancel);
  }
  if (typeof api.on === "function") {
    stopEvents = api.on("cache.change", onProgress);
  }

  return {
    refresh,
    apply,
    measure,
    repaintTimes,
    destroy() {
      ticket += 1;
      measuring = false;
      if (typeof stopEvents === "function") {
        stopEvents();
      }
      bodies.removeEventListener("blur", onBlur);
      blobs.removeEventListener("blur", onBlur);
      bodies.removeEventListener("keydown", onKey);
      blobs.removeEventListener("keydown", onKey);
      if (listRowsInput) {
        listRowsInput.removeEventListener("blur", onBlur);
        listRowsInput.removeEventListener("keydown", onKey);
      }
      if (recentInput) {
        recentInput.removeEventListener("blur", onBlur);
        recentInput.removeEventListener("keydown", onKey);
      }
      if (measureButton) {
        measureButton.removeEventListener("click", onMeasure);
      }
      if (cancelButton) {
        cancelButton.removeEventListener("click", onCancel);
      }
    },
  };
}
