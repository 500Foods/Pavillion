/* Coloris (MIT). Accounts color and the Icon Wizard share one picker.
   jsdom tests keep the plain input. The account field stores #rrggbb only.
   Ctrl+wheel zooms the open picker. Swatches are the theme's list, then
   colors saved on the profile. */

import { colorTip } from "./colornames.js";
import { zoomStatus } from "./status.js";

const ZOOM_MIN = 1;
const ZOOM_MAX = 3;
const ZOOM_STEP_PCT = 5;
const WHEEL_NOTCH_PX = 100;
export const SWATCH_CAP = 24;

let pickerZoom = 2;
let onZoom = null;
let wheelToward = null;
let wheelRemainder = 0;
let userSwatches = [];
let swatchesBound = false;
let onSwatches = null;
let colorisApi = null;
let extrasBound = false;

/* 100% to 300%, steps of 5%. A missing value is 200%. */
export function colorZoomOf(raw) {
  const n = Number(raw);
  if (!Number.isFinite(n)) {
    return 2;
  }
  const pct = Math.min(ZOOM_MAX * 100, Math.max(ZOOM_MIN * 100, Math.round(n * 100)));
  return (Math.round(pct / ZOOM_STEP_PCT) * ZOOM_STEP_PCT) / 100;
}

/* One notch of the picker wheel. `steps` is positive when the wheel rolls toward you. */
export function pickerZoomAfter(current, steps, toward) {
  const direction = toward ? 1 : -1;
  return colorZoomOf(Number(current) + (direction * steps * ZOOM_STEP_PCT) / 100);
}

/* Hex colors from a theme list or a saved list. Duplicates drop. The cap is 24. */
export function swatchList(raw) {
  const parts = Array.isArray(raw) ? raw : String(raw || "").split(/\s+/);
  const out = [];
  for (const part of parts) {
    if (typeof part !== "string") {
      continue;
    }
    const hex = hexColor(part);
    if (!hex || out.includes(hex)) {
      continue;
    }
    out.push(hex);
    if (out.length >= SWATCH_CAP) {
      break;
    }
  }
  return out;
}

/* Theme colors first, then saved colors that are not already in the theme. */
export function mergeSwatches(theme, user) {
  const out = swatchList(theme);
  for (const hex of swatchList(user)) {
    if (out.includes(hex)) {
      continue;
    }
    if (out.length >= SWATCH_CAP) {
      break;
    }
    out.push(hex);
  }
  return out;
}

export function bindColorPicker(hooks) {
  if (hooks && typeof hooks.onZoom === "function") {
    onZoom = hooks.onZoom;
  }
  if (hooks && typeof hooks.toward === "function") {
    wheelToward = hooks.toward;
  }
  if (hooks && typeof hooks.onSwatches === "function") {
    onSwatches = hooks.onSwatches;
  }
  if (hooks && Array.isArray(hooks.swatches)) {
    userSwatches = swatchList(hooks.swatches);
    swatchesBound = true;
  }
  if (hooks && hooks.zoom !== undefined) {
    pickerZoom = colorZoomOf(hooks.zoom);
    applyPickerZoom();
  }
}

function clampByte(n) {
  return Math.min(255, Math.max(0, Math.round(Number(n) || 0)));
}

function hexByte(r, g, b) {
  return `#${[r, g, b].map((n) => clampByte(n).toString(16).padStart(2, "0")).join("")}`;
}

function hslToHex(h, s, l) {
  const sat = Math.min(100, Math.max(0, s)) / 100;
  const light = Math.min(100, Math.max(0, l)) / 100;
  const hue = ((h % 360) + 360) % 360;
  const c = (1 - Math.abs(2 * light - 1)) * sat;
  const x = c * (1 - Math.abs(((hue / 60) % 2) - 1));
  const m = light - c / 2;
  let r = 0;
  let g = 0;
  let b = 0;
  if (hue < 60) {
    r = c;
    g = x;
  } else if (hue < 120) {
    r = x;
    g = c;
  } else if (hue < 180) {
    g = c;
    b = x;
  } else if (hue < 240) {
    g = x;
    b = c;
  } else if (hue < 300) {
    r = x;
    b = c;
  } else {
    r = c;
    b = x;
  }
  return hexByte((r + m) * 255, (g + m) * 255, (b + m) * 255);
}

/* #rgb, #rrggbb, and the rgb()/hsl() strings Coloris writes. Empty when it is not a color. */
export function hexColor(value) {
  const text = String(value || "").trim();
  const short = /^#([0-9a-f]{3})$/i.exec(text);
  if (short) {
    const [r, g, b] = short[1];
    return `#${r}${r}${g}${g}${b}${b}`.toLowerCase();
  }
  const long = /^#([0-9a-f]{6})([0-9a-f]{2})?$/i.exec(text);
  if (long) {
    return `#${long[1].toLowerCase()}`;
  }
  const rgb = /^rgba?\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})/i.exec(text);
  if (rgb) {
    return hexByte(rgb[1], rgb[2], rgb[3]);
  }
  const hsl = /^hsla?\(\s*([0-9.]+)\s*,\s*([0-9.]+)%\s*,\s*([0-9.]+)%/i.exec(text);
  if (hsl) {
    return hslToHex(Number(hsl[1]), Number(hsl[2]), Number(hsl[3]));
  }
  return "";
}

function applyPickerZoom() {
  const picker = document.getElementById("clr-picker");
  if (!picker) {
    return;
  }
  picker.style.setProperty("--pav-color-zoom", String(pickerZoom));
  if (picker.classList.contains("clr-open") && colorisApi && typeof colorisApi.updatePosition === "function") {
    requestAnimationFrame(() => colorisApi.updatePosition());
  }
}

function stepZoom(delta) {
  const next = colorZoomOf(pickerZoom + delta);
  if (next === pickerZoom) {
    return;
  }
  pickerZoom = next;
  applyPickerZoom();
  if (onZoom) {
    onZoom(pickerZoom);
  }
}

/* A mouse notch is about 100px. Smaller trackpad deltas add up. A line or a page is one step. */
function notchSteps(event) {
  const delta = event.deltaY;
  if (!Number.isFinite(delta) || delta === 0) {
    return 0;
  }
  if (event.deltaMode !== 0 || Math.abs(delta) >= WHEEL_NOTCH_PX) {
    wheelRemainder = 0;
    return delta > 0 ? 1 : -1;
  }
  wheelRemainder += delta;
  if (Math.abs(wheelRemainder) < WHEEL_NOTCH_PX) {
    return 0;
  }
  const steps = wheelRemainder > 0 ? 1 : -1;
  wheelRemainder -= steps * WHEEL_NOTCH_PX;
  return steps;
}

function onPickerWheel(event) {
  const picker = document.getElementById("clr-picker");
  if (!event.ctrlKey || !picker || !picker.classList.contains("clr-open") || !picker.contains(event.target)) {
    return;
  }
  event.preventDefault();
  const steps = notchSteps(event);
  if (!steps) {
    return;
  }
  const toward = wheelToward ? wheelToward() : false;
  stepZoom(pickerZoomAfter(pickerZoom, steps, toward) - pickerZoom);
  zoomStatus("Color picker", pickerZoom);
}

function themeRoot() {
  const picker = document.getElementById("clr-picker");
  return (picker && picker.closest(".pav-root")) || document.querySelector(".pav-root");
}

function shownSwatches() {
  const root = themeRoot();
  const view = root && root.ownerDocument.defaultView;
  const listed = view ? view.getComputedStyle(root).getPropertyValue("--pav-picker-swatches") : "";
  return mergeSwatches(listed, userSwatches);
}

function colorReader() {
  const root = themeRoot();
  const view = root && root.ownerDocument.defaultView;
  if (!view) {
    return null;
  }
  const style = view.getComputedStyle(root);
  return (name) => style.getPropertyValue(name);
}

function currentHex() {
  const field = document.getElementById("clr-color-value");
  return hexColor(field && field.value);
}

function labelSwatches(shown) {
  const picker = document.getElementById("clr-picker");
  if (!picker) {
    return;
  }
  const read = colorReader();
  const owned = new Set(userSwatches);
  for (const button of picker.querySelectorAll(".clr-swatches button")) {
    const hex = hexColor(button.textContent);
    const name = colorTip(hex, read);
    if (name) {
      button.setAttribute("data-pav-tip", name);
    } else {
      button.removeAttribute("data-pav-tip");
    }
    if (owned.has(hex)) {
      button.dataset.pavUser = "1";
    } else {
      delete button.dataset.pavUser;
    }
  }
  paintAdd(shown);
}

function actionIcon(doc, name) {
  const icon = doc.createElement("i");
  icon.className = `icon fa-sharp fa-thin fa-${name}`;
  icon.setAttribute("aria-hidden", "true");
  icon.draggable = false;
  return icon;
}

function actionButton(doc, name, label) {
  const button = doc.createElement("button");
  button.type = "button";
  button.className = "pav-color-action";
  button.setAttribute("aria-label", label);
  button.setAttribute("data-pav-tip", label);
  button.append(actionIcon(doc, name));
  return button;
}

/* Add, Remove, and Close stay on one row. Coloris parks Close in the preview. */
function ensureActions(picker) {
  const doc = picker.ownerDocument;
  let row = picker.querySelector(".pav-color-actions");
  if (!row) {
    row = doc.createElement("div");
    row.className = "pav-color-actions";
    const add = actionButton(doc, "plus", "Add this color");
    add.dataset.pavColorAction = "add";
    add.addEventListener("click", onAddClick);
    const remove = actionButton(doc, "minus", "Remove this color");
    remove.dataset.pavColorAction = "remove";
    remove.addEventListener("click", onRemoveClick);
    row.append(add, remove);
    picker.append(row);
  }
  const close = picker.querySelector("#clr-close");
  if (close && close.parentElement !== row) {
    close.classList.add("pav-color-action");
    if (!close.querySelector(".icon")) {
      close.textContent = "";
      close.append(actionIcon(doc, "xmark"));
    }
    close.setAttribute("aria-label", "Close");
    close.setAttribute("data-pav-tip", "Close");
    row.append(close);
  }
  return row;
}

function paintAdd(shown) {
  const picker = document.getElementById("clr-picker");
  if (!picker) {
    return;
  }
  const row = ensureActions(picker);
  const hex = currentHex();
  const owned = Boolean(hex && userSwatches.includes(hex));
  const add = row.querySelector("[data-pav-color-action='add']");
  const remove = row.querySelector("[data-pav-color-action='remove']");
  if (add) {
    add.disabled = !hex || shown.includes(hex) || shown.length >= SWATCH_CAP;
  }
  if (remove) {
    remove.disabled = !owned;
  }
}

function commitSwatches() {
  if (onSwatches) {
    onSwatches(userSwatches.slice());
  }
  applySwatches();
}

function onAddClick() {
  const hex = currentHex();
  const shown = shownSwatches();
  if (!hex || shown.includes(hex) || shown.length >= SWATCH_CAP) {
    return;
  }
  userSwatches = swatchList([...userSwatches, hex]);
  commitSwatches();
}

function onRemoveClick() {
  const hex = currentHex();
  if (!hex || !userSwatches.includes(hex)) {
    return;
  }
  userSwatches = userSwatches.filter((item) => item !== hex);
  commitSwatches();
}

function applySwatches() {
  if (!colorisApi) {
    return;
  }
  const shown = shownSwatches();
  colorisApi({ swatches: shown });
  labelSwatches(shown);
}

function ensurePickerExtras(doc) {
  const picker = doc.getElementById("clr-picker");
  if (!picker) {
    return;
  }
  if (!extrasBound) {
    extrasBound = true;
    picker.dataset.pavExtras = "1";
    picker.addEventListener("wheel", onPickerWheel, { capture: true, passive: false });
    picker.addEventListener("input", () => paintAdd(shownSwatches()));
    doc.addEventListener("coloris:pick", () => paintAdd(shownSwatches()));
  }
  applyPickerZoom();
  applySwatches();
}

/* One picker serves every field. Move it into the open dialog, or the root
   layer, before Coloris measures the field. A closed dialog would hide it. */
function placePicker(input) {
  const picker = input.ownerDocument.getElementById("clr-picker");
  const host = pickerHost(input);
  if (!picker || !host || !colorisApi || picker.parentElement === host) {
    return;
  }
  colorisApi({ parent: host });
}

/* A fixed layer over the viewport. A field inside a dialog gets its own
   layer there, so the picker stays in the top layer while that dialog is open. */
function pickerHost(input) {
  const root = input.closest(".pav-root");
  if (!root) {
    return null;
  }
  const dialog = input.closest("dialog");
  const parent = dialog || root;
  let host = parent.querySelector(":scope > .pav-color-host");
  if (!host) {
    host = input.ownerDocument.createElement("div");
    host.className = "pav-color-host";
    parent.append(host);
  }
  return host;
}

function keepHex(input) {
  if (input.dataset.pavHex === "1") {
    return;
  }
  input.dataset.pavHex = "1";
  input.addEventListener("pointerdown", () => {
    placePicker(input);
    applySwatches();
  }, true);
  input.addEventListener("input", () => {
    const raw = String(input.value || "").trim();
    /* #rgb and #rrggbb already store. Rewriting #2a7 on Escape would dirty the form. */
    if (/^#[0-9a-f]{3}$/i.test(raw) || /^#[0-9a-f]{6}$/i.test(raw)) {
      return;
    }
    const hex = hexColor(raw);
    if (!hex) {
      return;
    }
    input.value = hex;
    const field = input.parentElement;
    if (field && field.classList.contains("clr-field")) {
      field.style.color = hex;
    }
  });
}

export async function mountColoris(input, swatches) {
  if (!input || (typeof navigator !== "undefined" && /jsdom/i.test(navigator.userAgent))) {
    return;
  }
  const { default: Coloris } = await import("@melloware/coloris");
  await import("@melloware/coloris/dist/coloris.css");
  colorisApi = Coloris;
  if (!swatchesBound && Array.isArray(swatches)) {
    userSwatches = swatchList(swatches);
  }
  Coloris.init();
  Coloris({
    el: input,
    parent: pickerHost(input),
    themeMode: "dark",
    /* Opacity and rgb()/hsl() do not fit the account color, which is #rgb or #rrggbb.
       The Hex/RGB/HSL buttons stay so a value can be read in another form; the field
       is written back as hex. */
    alpha: false,
    format: "hex",
    formatToggle: true,
    closeButton: true,
    closeLabel: "Close",
    focusInput: false,
    swatches: shownSwatches(),
  });
  keepHex(input);
  const doc = input.ownerDocument;
  ensurePickerExtras(doc);
  requestAnimationFrame(() => ensurePickerExtras(doc));
}
