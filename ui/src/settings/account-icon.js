/* K-Q9: the account icon is an HTML snippet; "" is the envelope. It is drawn
   through the shared icon sanitizer on the account color. A refused snippet
   draws the envelope. */
import { paintIcon } from "../common/iconsan.js";

export const DEFAULT_ICON = "envelope";

/* The interim chooser (K-Q9): icons already in the kit. */
export const ICON_CHOICES = [
  "envelope",
  "briefcase",
  "house",
  "user",
  "users",
  "building",
  "globe",
  "star",
  "heart",
  "cloud",
  "rocket",
  "ghost",
];

export function iconSnippet(name) {
  return !name || name === DEFAULT_ICON ? "" : `<i class="fa-solid fa-${name}"></i>`;
}

/* The chooser name for a stored snippet, or null when it is not one of the
   interim choices (a later Icon Wizard snippet). */
export function iconChoice(snippet) {
  if (!snippet) {
    return DEFAULT_ICON;
  }
  const m = /^<i class="fa-solid fa-([a-z0-9-]+)"><\/i>$/.exec(snippet);
  return m && ICON_CHOICES.includes(m[1]) ? m[1] : null;
}

/* A swatch of the account color with the icon drawn on it. */
export function accountSwatch(account, documentRef = globalThis.document) {
  const snippet = account && typeof account.icon === "string" ? account.icon : "";
  let icon = null;
  if (snippet) {
    const painted = paintIcon(snippet, documentRef);
    icon = painted.ok ? painted.fragment.firstElementChild : null;
  }
  if (!icon) {
    icon = documentRef.createElement("i");
    icon.className = `fa-solid fa-${DEFAULT_ICON}`;
  }
  icon.classList.add("account-icon");
  icon.setAttribute("aria-hidden", "true");
  const swatch = documentRef.createElement("span");
  swatch.className = "account-swatch";
  swatch.style.backgroundColor = (account && account.color) || "#2a7";
  swatch.append(icon);
  return swatch;
}
