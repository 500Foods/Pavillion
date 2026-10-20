/* K-Q8: Settings → Accounts list, the first Tabulator table. Columns: the
   color swatch with the account icon, Name, then one read-only checkbox per
   feature with the main nav's icon as its header. A box is on when the
   stored capabilities (K-Q10) include it and the account syncs it.
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

export function accountRows(accounts, timeZone) {
  return accounts.map((account) => {
    const caps = Array.isArray(account.capabilities) ? account.capabilities : null;
    const row = {
      id: account.id,
      name: account.name || account.id,
      number: account.number || "",
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

export function accountColumns(documentRef = globalThis.document) {
  const columns = [
    {
      title: "",
      field: "account",
      width: 44,
      headerSort: false,
      resizable: false,
      formatter: (cell) => accountSwatch(cell.getValue(), documentRef),
    },
    {
      title: "Name",
      field: "name",
      widthGrow: 1,
      minWidth: 96,
      formatter: (cell) => {
        const span = documentRef.createElement("span");
        span.className = "accounts-name-cell";
        span.textContent = cell.getValue();
        const num = documentRef.createElement("span");
        num.className = "accounts-tree-number";
        num.textContent = cell.getRow().getData().number;
        span.append(" ", num);
        return span;
      },
    },
  ];
  for (const feature of FEATURES) {
    columns.push({
      title: feature.title,
      titleFormatter: () => headerIcon(feature, documentRef),
      field: feature.key,
      hozAlign: "center",
      headerHozAlign: "center",
      headerSort: false,
      width: feature.key === "mail" ? 96 : 52,
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
  return columns;
}
