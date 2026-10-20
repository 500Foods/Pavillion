/* K-Q8, K-Q11: Settings → Accounts list, the first Tabulator table. Columns:
   the number, the color swatch with the account icon, Name, then one
   read-only checkbox per feature with the main nav's icon as its header.
   Rows are grouped by server (serverGroup) and the footer counts them.
   A box is on when the stored capabilities (K-Q10) include it and the
   account syncs it.
   Files has no sync flag yet, so it shows the capability only. */
import { accountSwatch } from "./account-icon.js";

export const FEATURES = [
  { key: "mail", section: "mail", title: "Mail", fallback: "fa-sharp-duotone fa-solid fa-paper-plane-top fa-rotate-270", cap: "mail", flag: "syncMail" },
  { key: "calendars", section: "calendars", title: "Calendar", fallback: "fa-sharp-duotone fa-solid fa-calendar-days", cap: "calendars", flag: "syncCalendars" },
  { key: "contacts", section: "contacts", title: "Contacts", fallback: "fa-sharp-duotone fa-solid fa-address-book", cap: "contacts", flag: "syncContacts" },
  { key: "files", section: "files", title: "Files", fallback: "fa-sharp-duotone fa-solid fa-folder", cap: "files", flag: null },
];

/* Mail protocols in the Mail cell: JMAP live; IMAP and SMTP not yet. */
export const MAIL_BADGES = [
  { letter: "J", title: "JMAP", enabled: true },
  { letter: "I", title: "IMAP (not available yet)", enabled: false },
  { letter: "S", title: "SMTP (not available yet)", enabled: false },
];

/* The nav button's live icon classes (user overrides included). */
export function navIconClass(feature, documentRef = globalThis.document) {
  const icon = documentRef.querySelector(`.nav-buttons [data-section="${feature.section}"] i[data-icon-definition], [data-rail="${feature.section}"] i[data-icon-definition]`);
  const value = icon ? icon.getAttribute("data-icon-definition") : "";
  return value || feature.fallback;
}

/* Whether a feature box is on: stored capability (K-Q10; null: unknown,
   treated as off) and the account's sync flag. */
export function featureOn(account, feature, caps) {
  const capable = Array.isArray(caps) && caps.includes(feature.cap);
  if (!capable) {
    return false;
  }
  return feature.flag ? account[feature.flag] !== false : true;
}

/* K-Q10: the feature tooltip, `Checked YYYY-MM-DD HH:MM:SS` in 24-hour time
   in the profile time zone, or `Not checked yet`. */
export function checkedTitle(iso, timeZone) {
  const ms = typeof iso === "string" ? Date.parse(iso) : Number.NaN;
  if (!Number.isFinite(ms)) {
    return "Not checked yet";
  }
  const zone = typeof timeZone === "string" && timeZone !== "" ? timeZone : "America/Vancouver";
  let parts;
  try {
    parts = new Intl.DateTimeFormat("en-GB", {
      timeZone: zone,
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
      hourCycle: "h23",
    }).formatToParts(new Date(ms));
  } catch {
    return "Not checked yet";
  }
  const pick = (type) => (parts.find((p) => p.type === type) || { value: "" }).value;
  const hour = pick("hour") === "24" ? "00" : pick("hour");
  return `Checked ${pick("year")}-${pick("month")}-${pick("day")} ${hour}:${pick("minute")}:${pick("second")}`;
}

export const NO_SERVER = "(no server)";

/* Group key for an account: the session URL's host only (TABULATOR.md
   Table look); "(no server)" when the URL is empty or does not parse. */
export function serverGroup(sessionUrl) {
  const text = typeof sessionUrl === "string" ? sessionUrl.trim() : "";
  if (text === "") {
    return NO_SERVER;
  }
  let url;
  try {
    url = new URL(text);
  } catch {
    return NO_SERVER;
  }
  if (url.hostname === "") {
    return NO_SERVER;
  }
  return url.hostname;
}

/* The footer: "1 account", "3 accounts". */
export function accountCountText(count) {
  return count === 1 ? "1 account" : `${count} accounts`;
}

/* A group header row: chevron toggle, the host, and a count badge, built
   with textContent. The chevron turns by CSS when the group closes. */
export function serverGroupHeader(value, count, documentRef = globalThis.document) {
  const toggle = documentRef.createElement("span");
  toggle.className = "pav-group-toggle";
  toggle.setAttribute("aria-hidden", "true");
  const chevron = documentRef.createElement("i");
  chevron.className = "icon fa-sharp-duotone fa-solid fa-chevron-down";
  toggle.append(chevron);
  const span = documentRef.createElement("span");
  span.className = "pav-group-label";
  span.textContent = value;
  const num = documentRef.createElement("span");
  num.className = "pav-group-count";
  num.textContent = String(count);
  num.title = accountCountText(count);
  const wrap = documentRef.createElement("span");
  wrap.className = "pav-group";
  wrap.append(toggle, span, num);
  return wrap;
}

export function accountRows(accounts, timeZone) {
  return accounts.map((account) => {
    const caps = Array.isArray(account.capabilities) ? account.capabilities : null;
    const row = {
      id: account.id,
      name: account.name || account.id,
      number: account.number || "",
      server: serverGroup(account.sessionUrl),
      email: typeof account.email === "string" ? account.email : "",
      account,
      checked: checkedTitle(account.capabilitiesChecked, timeZone),
    };
    for (const feature of FEATURES) {
      row[feature.key] = featureOn(account, feature, caps);
    }
    return row;
  });
}

function headerIcon(feature, documentRef) {
  const span = documentRef.createElement("span");
  const icon = documentRef.createElement("i");
  icon.className = `icon ${navIconClass(feature, documentRef)}`;
  icon.setAttribute("aria-hidden", "true");
  span.title = feature.title;
  span.setAttribute("aria-label", feature.title);
  span.append(icon);
  return span;
}

function checkbox(on, label, documentRef) {
  const box = documentRef.createElement("input");
  box.type = "checkbox";
  box.checked = on;
  box.disabled = true;
  box.setAttribute("aria-label", label);
  return box;
}

const FEATURE_WIDTH = 60;
const MAIL_WIDTH = 104;

/* K-Q12: the pane is never narrower than the fixed columns plus Name's
   minimum, so every column stays visible without a horizontal scrollbar. */
export function tableMinWidth(columns) {
  return columns
    .filter((c) => c.visible !== false)
    .reduce((sum, c) => sum + (Number.isFinite(c.width) ? c.width : c.minWidth || 0), 0);
}

/* K-Q12: the CSV columns, in order. Nothing else is exported. */
export const EXPORT_FIELDS = ["number", "name", "server", "mail", "calendars", "contacts", "files", "email"];

/* The fields a Tabulator CSV download takes from these columns. */
export function exportFields(columns) {
  return columns
    .filter((c) => (c.visible === false ? c.download === true : c.download !== false))
    .map((c) => c.field);
}

const yesNo = (value) => (value === true ? "yes" : "no");

/* Columns the chooser lists (TABULATOR.md Column chooser); Name is locked. */
export const CHOOSER_COLUMNS = [
  { field: "number", title: "Number" },
  { field: "account", title: "Icon" },
  { field: "name", title: "Name", locked: true },
  ...FEATURES.map((f) => ({ field: f.key, title: f.title })),
];

/* Stored hidden fields, keeping only known, unlocked chooser fields. */
export function cleanHidden(hidden) {
  const allowed = new Set(CHOOSER_COLUMNS.filter((c) => !c.locked).map((c) => c.field));
  return Array.isArray(hidden) ? [...new Set(hidden.filter((f) => allowed.has(f)))] : [];
}

export function accountColumns(documentRef = globalThis.document, hidden = []) {
  const off = new Set(cleanHidden(hidden));
  const columns = [
    {
      title: "#",
      titleDownload: "Number",
      field: "number",
      width: 60,
      resizable: false,
      sorter: "string",
      formatter: (cell) => {
        const num = documentRef.createElement("span");
        num.className = "accounts-tree-number";
        num.textContent = cell.getValue();
        return num;
      },
    },
    {
      title: "",
      field: "account",
      width: 44,
      resizable: false,
      download: false,
      sorter: (a, b) => String((a && a.color) || "").localeCompare(String((b && b.color) || "")),
      formatter: (cell) => accountSwatch(cell.getValue(), documentRef),
    },
    {
      title: "Name",
      field: "name",
      widthGrow: 1,
      minWidth: 120,
      sorter: "string",
      bottomCalc: (values) => accountCountText(values.length),
      formatter: (cell) => {
        const span = documentRef.createElement("span");
        span.className = "accounts-name-cell";
        span.textContent = cell.getValue();
        return span;
      },
    },
    { title: "Server", field: "server", visible: false, download: true },
  ];
  for (const feature of FEATURES) {
    columns.push({
      title: feature.title,
      titleFormatter: () => headerIcon(feature, documentRef),
      field: feature.key,
      hozAlign: "center",
      headerHozAlign: "center",
      sorter: "boolean",
      accessorDownload: yesNo,
      bottomCalc: (values) => values.filter((v) => v === true).length,
      width: feature.key === "mail" ? MAIL_WIDTH : FEATURE_WIDTH,
      resizable: false,
      formatter: (cell) => {
        const wrap = documentRef.createElement("span");
        wrap.className = "accounts-feature-cell";
        wrap.title = cell.getRow().getData().checked || "Not checked yet";
        wrap.append(checkbox(cell.getValue() === true, `${feature.title} for ${cell.getRow().getData().name}`, documentRef));
        if (feature.key === "mail") {
          for (const badge of MAIL_BADGES) {
            const b = documentRef.createElement("span");
            b.className = badge.enabled ? "accounts-badge" : "accounts-badge is-disabled";
            b.textContent = badge.letter;
            b.title = badge.title;
            if (!badge.enabled) {
              b.setAttribute("aria-disabled", "true");
            }
            wrap.append(b);
          }
        }
        return wrap;
      },
    });
  }
  for (const column of columns) {
    if (off.has(column.field)) {
      column.visible = false;
    }
  }
  columns.push({ title: "Email", field: "email", visible: false, download: true });
  return columns;
}

/* K-Q12 navigator state lives in the shared module footer (D-10). */
export { navState, navTarget } from "../common/module-bar.js";

/* D-10 Clone: every field except the username, the password, the id and
   number (Add assigns them), and the capabilities (they belong to the
   credentials). The account rows never carry a secret. */
export const CLONE_DROPPED = ["id", "number", "username", "secret", "password", "capabilities", "capabilitiesChecked"];

export function cloneAccount(account) {
  const copy = {};
  for (const [key, value] of Object.entries(account || {})) {
    if (!CLONE_DROPPED.includes(key)) {
      copy[key] = value;
    }
  }
  const name = typeof copy.name === "string" && copy.name !== "" ? copy.name : (account && account.id) || "";
  copy.name = name ? `${name} (copy)` : "";
  return copy;
}

/* D-10 search: case-insensitive substring over name, number, email,
   username, and the server label. A blank query keeps every row. */
export function accountMatches(row, query) {
  const q = String(query || "").trim().toLowerCase();
  if (q === "") {
    return true;
  }
  const account = row.account || {};
  return [row.name, row.number, row.email, account.username, row.server]
    .some((value) => typeof value === "string" && value.toLowerCase().includes(q));
}

/* D-10 Export: the CSV text for rows in display order, EXPORT_FIELDS only
   (no username, no secret). A cell starting with = + - @ gets a leading
   apostrophe so a spreadsheet does not run it as a formula. */
export const EXPORT_TITLES = ["Number", "Name", "Server", "Mail", "Calendar", "Contacts", "Files", "Email"];

function csvCell(value) {
  let text = typeof value === "boolean" ? yesNo(value) : String(value ?? "");
  if (/^[=+\-@]/.test(text)) {
    text = `'${text}`;
  }
  return /[",\r\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

export function accountsCsv(rows) {
  const lines = [EXPORT_TITLES.map(csvCell).join(",")];
  for (const row of rows) {
    lines.push(EXPORT_FIELDS.map((field) => csvCell(row[field])).join(","));
  }
  return `${lines.join("\r\n")}\r\n`;
}
