import { createDialogs } from "./common/dialog.js";
import { installIsolationTest } from "./common/isolation.js";
import { createKeys } from "./common/keys.js";
import { createMenu } from "./common/menu.js";
import { createToaster } from "./common/toast.js";
import { createTooltip } from "./common/tooltip.js";
import { createTabs, KIND_ICONS } from "./shell/tabs.js";
import { createActivityView } from "./activity/view.js";
import { createLogView } from "./log/view.js";
import { DEFAULT_UI, clampUi, snapZoom } from "./prefs.js";
import {
  THEME_TEMPLATES,
  buildCatalog,
  effectiveChoice,
  iconFromHtml,
  liveThemeCss,
  safeAssetUrl,
  schemeOf,
  themeMap,
  unquote,
} from "./theme-catalog.js";
import { normalizeTheme, schemeQuery } from "./theme.js";

let api = null;
let host = null;
let mountRoot = null;
let session = null;
let widgets = null;

function log(subsystem, level, message) {
  return api.log.client({ subsystem, level, message });
}

let ui = clampUi({});

/* shell is the master scale on the root; each pane multiplies on top of it. */
const zoomPanes = {
  navButtons: ".button-bar",
  navBody: ".nav-body",
  mailList: "#panel-mail .pane-list",
  mailDetail: "#panel-mail .pane-detail",
};

function zoomClamp(key, value) {
  return snapZoom(value, key);
}

function zoomLabel(scale) {
  return `${Math.round(scale * 100)}%`;
}

function paintZoomSliders() {
  for (const label of mountRoot.querySelectorAll("[data-zoom]")) {
    const key = label.dataset.zoom;
    if (!(key in ui.zoom)) {
      continue;
    }
    const input = label.querySelector("input");
    const reading = label.querySelector(".zoom-value");
    if (input) {
      input.value = String(ui.zoom[key]);
    }
    if (reading) {
      reading.textContent = zoomLabel(ui.zoom[key]);
    }
  }
  paintWheelSwitch();
}

function applyFonts() {
  if (!mountRoot) {
    return;
  }
  if (ui.fontSource === "override") {
    mountRoot.style.setProperty("--pav-font", ui.fonts.body);
    mountRoot.style.setProperty("--pav-font-mono", ui.fonts.mono);
  } else {
    mountRoot.style.removeProperty("--pav-font");
    mountRoot.style.removeProperty("--pav-font-mono");
  }
  paintFontFields();
}

function paintFontFields() {
  if (!mountRoot) {
    return;
  }
  const themeMode = ui.fontSource !== "override";
  const source = mountRoot.querySelector("#font-source");
  if (source) {
    source.value = themeMode ? "theme" : "override";
  }
  const computed = getComputedStyle(mountRoot);
  for (const [id, key, prop] of [
    ["#body-font", "body", "--pav-font"],
    ["#mono-font", "mono", "--pav-font-mono"],
  ]) {
    const select = mountRoot.querySelector(id);
    if (!select) {
      continue;
    }
    select.disabled = themeMode;
    const value = themeMode ? unquote(computed.getPropertyValue(prop)).trim() || ui.fonts[key] : ui.fonts[key];
    showFont(select, value);
  }
}

function showFont(select, value) {
  if (!select) {
    return;
  }
  if (![...select.options].some((option) => option.value === value)) {
    const option = document.createElement("option");
    option.value = value;
    option.textContent = value;
    select.append(option);
  }
  select.value = value;
}

function applyZoom() {
  if (host && typeof host.setRootFontSize === "function") {
    host.setRootFontSize(ui.zoom.shell);
  }
  for (const [key, selector] of Object.entries(zoomPanes)) {
    for (const element of mountRoot.querySelectorAll(selector)) {
      const scale = ui.zoom.shell * ui.zoom[key];
      element.style.zoom = String(scale);
      /* WebKit zoom scales the label and a px border, and it leaves a
         rem-sized svg at its unzoomed size. The variable grows the icon
         with the label, and divides the bar rules back to 1px. */
      if (key === "navButtons") {
        element.style.setProperty("--pav-bar-zoom", String(scale));
        continue;
      }
      for (const svg of element.querySelectorAll("svg")) {
        svg.style.width = `${1.15 * scale}em`;
        svg.style.height = `${1.15 * scale}em`;
      }
    }
  }
  if (logView) {
    logView.setZoom(ui.zoom.log);
  }
}

/* A mouse notch is about 100px. Smaller pixel deltas add up until then, so a trackpad flick is not one step per event.
   A line or a page is one step from its sign. deltaY > 0 rolls toward you. "away" increases zoom when deltaY is negative. */
const WHEEL_NOTCH_PX = 100;
let wheelRemainder = 0;
let onZoomWheel = null;

function paintWheelSwitch() {
  if (!mountRoot) {
    return;
  }
  const button = mountRoot.querySelector("#zoom-wheel");
  if (!button) {
    return;
  }
  const toward = ui.zoom.wheel === "toward";
  button.setAttribute("aria-checked", toward ? "true" : "false");
  button.textContent = toward ? "Toward zooms in" : "Away zooms in";
}

function zoomKeyAt(target) {
  if (!(target instanceof Element)) {
    return "";
  }
  if (target.closest(".button-bar")) {
    return "navButtons";
  }
  if (target.closest(".nav-body") || target.closest("#month")) {
    return "navBody";
  }
  if (target.closest("#panel-mail .pane-list")) {
    return "mailList";
  }
  if (target.closest("#panel-mail .pane-detail")) {
    return "mailDetail";
  }
  if (target.closest("[data-activity-pane='log']")) {
    return "log";
  }
  return "";
}

function wheelSteps(event) {
  const delta = event.deltaY;
  if (!Number.isFinite(delta) || delta === 0) {
    return 0;
  }
  if (event.deltaMode !== 0) {
    wheelRemainder = 0;
    return delta > 0 ? 1 : -1;
  }
  if (Math.abs(delta) >= WHEEL_NOTCH_PX) {
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

function stepZoom(key, steps) {
  if (!steps || key === "shell" || !(key in ui.zoom)) {
    return;
  }
  const direction = ui.zoom.wheel === "toward" ? 1 : -1;
  const next = zoomClamp(key, ui.zoom[key] + direction * steps * 0.05);
  if (next === ui.zoom[key]) {
    return;
  }
  ui.zoom[key] = next;
  paintZoomSliders();
  applyZoom();
  persistLayout();
}

function onWheelZoom(event) {
  if (!event.ctrlKey || !mountRoot) {
    return;
  }
  event.preventDefault();
  const key = zoomKeyAt(event.target);
  if (!key) {
    return;
  }
  stepZoom(key, wheelSteps(event));
}

const ICON_KEYS = ["mail", "contacts", "calendars", "files", "activity", "settings", "update", "help"];

function iconElement(key) {
  if (key === "update") {
    return mountRoot.querySelector("#update .icon");
  }
  return mountRoot.querySelector(`.nav [data-section="${key}"] .icon`);
}

/* WebKit's SVG className is read-only. Font Awesome swaps <i> for <svg>, so always replace the node. */
function paintNavIcon(el, value) {
  const definition = el.dataset.iconDefinition || "";
  const raw = unquote(value).trim();
  const fallback = definition ? `icon ${definition}` : "icon";
  if (!raw || raw === "none") {
    el.replaceWith(freshIcon(fallback, definition));
    return;
  }
  if (raw.startsWith("<")) {
    const parsed = iconFromHtml(raw);
    if (!parsed) {
      el.replaceWith(freshIcon(fallback, definition));
      return;
    }
    parsed.classList.add("icon");
    parsed.setAttribute("aria-hidden", "true");
    if (definition) {
      parsed.dataset.iconDefinition = definition;
    }
    el.replaceWith(parsed);
    return;
  }
  const asset = /\s/.test(raw) ? "" : safeAssetUrl(raw);
  if (asset) {
    const img = document.createElement("img");
    img.alt = "";
    img.className = "icon";
    img.src = asset;
    img.setAttribute("aria-hidden", "true");
    if (definition) {
      img.dataset.iconDefinition = definition;
    }
    el.replaceWith(img);
    return;
  }
  el.replaceWith(freshIcon(`icon ${raw}`, definition));
}

function freshIcon(className, definition) {
  const node = document.createElement("i");
  node.className = className;
  node.setAttribute("aria-hidden", "true");
  if (definition) {
    node.dataset.iconDefinition = definition;
  }
  return node;
}

function applyIconOverride(key) {
  const el = iconElement(key);
  if (!el) {
    return;
  }
  paintNavIcon(el, ui.icons[key] || "");
  if (tabCtl) {
    tabCtl.refreshIcons();
  }
}

function activeFile() {
  const shown = effectiveChoice(themeChoice, catalog.themes);
  if (typeof shown !== "string" || !shown.startsWith("file:")) {
    return null;
  }
  return catalog.themes.find((item) => `file:${item.name}` === shown) || null;
}

function fileDecl(name) {
  const theme = activeFile();
  if (!theme) {
    return "";
  }
  return unquote(themeMap(theme.text).get(name) || "").trim();
}

function applyIcons() {
  if (!mountRoot) {
    return;
  }
  const file = activeFile();
  const themeMode = Boolean(file) || ui.iconSource !== "override";
  const computed = getComputedStyle(mountRoot);
  for (const key of ICON_KEYS) {
    const el = iconElement(key);
    if (!el) {
      continue;
    }
    if (file) {
      paintNavIcon(el, fileDecl(`--pav-icon-${key}`));
      continue;
    }
    if (!themeMode) {
      paintNavIcon(el, ui.icons[key] || "");
      continue;
    }
    paintNavIcon(el, computed.getPropertyValue(`--pav-icon-${key}`));
  }
  paintIconFields();
  applyThemeKit();
  if (tabCtl) {
    tabCtl.refreshIcons();
  }
}

function paintIconFields() {
  if (!mountRoot) {
    return;
  }
  const themeMode = ui.iconSource !== "override";
  const source = mountRoot.querySelector("#icon-source");
  if (source) {
    source.value = themeMode ? "theme" : "override";
  }
  const computed = getComputedStyle(mountRoot);
  for (const input of mountRoot.querySelectorAll("#icon-list .icon-input")) {
    const key = input.dataset.iconKey;
    input.disabled = themeMode;
    if (themeMode) {
      const shown = unquote(computed.getPropertyValue(`--pav-icon-${key}`)).trim();
      if (shown) {
        input.value = shown;
      }
    } else if (ui.icons[key]) {
      input.value = ui.icons[key];
    }
  }
  const kit = mountRoot.querySelector("#fa-kit-html");
  if (kit) {
    kit.disabled = themeMode;
    if (themeMode) {
      const shown = unquote(computed.getPropertyValue("--pav-icon-kit")).trim();
      if (shown) {
        kit.value = shown;
      }
    } else {
      kit.value = ui.fontawesome_kit;
    }
  }
}

function applyThemeKit() {
  const file = activeFile();
  if (!mountRoot || (!file && ui.iconSource === "override") || !host || typeof host.loadKit !== "function") {
    return;
  }
  const kit = file
    ? fileDecl("--pav-icon-kit")
    : unquote(getComputedStyle(mountRoot).getPropertyValue("--pav-icon-kit")).trim();
  if (!kit || kit.startsWith("<")) {
    return;
  }
  host.loadKit(kit);
}

function applyMark() {
  if (!mountRoot) {
    return;
  }
  const img = mountRoot.querySelector(".nav-mark");
  if (!img) {
    return;
  }
  const file = activeFile();
  const declared = file ? themeMap(file.text).has("--pav-mark") : false;
  const raw = declared ? fileDecl("--pav-mark") : getComputedStyle(mountRoot).getPropertyValue("--pav-mark");
  const src = safeAssetUrl(raw);
  if (!src) {
    img.removeAttribute("src");
    img.classList.remove("is-shown");
    return;
  }
  img.src = src;
  img.classList.add("is-shown");
}

function paintPreview(preview, value) {
  const key = preview.dataset.iconKey || "";
  const raw = String(value || "").trim();
  let node = raw.startsWith("<") ? iconFromHtml(raw) : null;
  if (!node) {
    node = document.createElement("i");
    node.className = raw.startsWith("<") || raw === "" ? "icon" : `icon ${raw}`;
  }
  node.classList.add("icon");
  if (key) {
    node.dataset.iconKey = key;
  }
  preview.replaceWith(node);
}

function refreshIconPreviews() {
  for (const input of mountRoot.querySelectorAll("#icon-list .icon-input")) {
    const key = input.dataset.iconKey;
    const preview = mountRoot.querySelector(`#icon-list .icon[data-icon-key="${key}"]`);
    if (preview) {
      paintPreview(preview, input.value || input.defaultValue);
    }
  }
}

let canSave = false;
let themeChoice = "system";
let themePast = [];
let themeFuture = [];
let catalog = { folder: "", chosen: "", fallback: "", missing: false, themes: [] };
let folderField = "";
let listEpoch = 0;
let schemeList = null;
let onScheme = null;
let hostThemeSent = "";

function systemPrefersLight() {
  const query = schemeQuery();
  return Boolean(query && query.matches);
}

function detachScheme() {
  if (schemeList && onScheme) {
    schemeList.removeEventListener("change", onScheme);
  }
  schemeList = null;
  onScheme = null;
}

function watchScheme() {
  detachScheme();
  const query = schemeQuery();
  if (!query || typeof query.addEventListener !== "function") {
    return;
  }
  onScheme = () => {
    if (mountRoot) {
      paintTheme();
    }
  };
  schemeList = query;
  schemeList.addEventListener("change", onScheme);
}

function applyFileStyle(text) {
  let style = document.getElementById("pav-file-theme");
  if (!text) {
    if (style) {
      style.remove();
    }
    return;
  }
  if (!style) {
    style = document.createElement("style");
    style.id = "pav-file-theme";
    document.head.append(style);
  }
  style.textContent = liveThemeCss(text);
}

function renderThemeList(shown) {
  const list = mountRoot.querySelector("#theme-list");
  if (!list) {
    return;
  }
  const rows = buildCatalog(catalog.themes, systemPrefersLight());
  list.replaceChildren();
  rows.forEach((row, index) => {
    if (index === 3) {
      const divider = document.createElement("div");
      divider.className = "theme-divider";
      divider.setAttribute("role", "separator");
      list.append(divider);
    }
    const label = document.createElement("label");
    label.className = "theme-row";
    const input = document.createElement("input");
    input.type = "radio";
    input.name = "pav-theme";
    input.value = row.id;
    input.checked = row.id === shown;
    const preview = document.createElement("span");
    preview.className = "theme-preview";
    preview.setAttribute("aria-hidden", "true");
    const frame = document.createElement("iframe");
    frame.setAttribute("sandbox", "");
    frame.tabIndex = -1;
    try {
      frame.inert = true;
    } catch {
      /* The frame is already pointer-events: none. */
    }
    frame.srcdoc = row.srcdoc;
    frame.setAttribute("srcdoc", row.srcdoc);
    preview.append(frame);
    const copy = document.createElement("span");
    copy.className = "theme-copy";
    const title = document.createElement("span");
    title.className = "theme-title";
    title.textContent = row.label;
    title.title = row.titleFull;
    const author = document.createElement("span");
    author.className = "theme-author";
    author.textContent = row.author;
    author.title = row.authorFull;
    const meta = document.createElement("span");
    meta.className = "theme-meta";
    meta.textContent = row.meta;
    meta.title = row.metaFull;
    copy.append(title, author, meta);
    label.append(input, preview, copy);
    list.append(label);
  });
  if (rows.length === 3) {
    const divider = document.createElement("div");
    divider.className = "theme-divider";
    divider.setAttribute("role", "separator");
    list.append(divider);
  }
  for (const frame of list.querySelectorAll("iframe")) {
    const height = frame.parentElement.getBoundingClientRect().height;
    if (height > 0) {
      frame.style.transform = `scale(${height / 768})`;
    }
  }
}

function paintThemeActions() {
  if (!mountRoot) {
    return;
  }
  const undo = mountRoot.querySelector("#theme-undo");
  const redo = mountRoot.querySelector("#theme-redo");
  const defaults = mountRoot.querySelector("#theme-default");
  if (undo) {
    undo.disabled = themePast.length === 0;
  }
  if (redo) {
    redo.disabled = themeFuture.length === 0;
  }
  if (defaults) {
    defaults.disabled = effectiveChoice(themeChoice, catalog.themes) === "system";
  }
}

function paintFolder() {
  const input = mountRoot.querySelector("#theme-folder");
  const note = mountRoot.querySelector("#theme-folder-note");
  if (input && document.activeElement !== input) {
    const chosen = typeof catalog.chosen === "string" ? catalog.chosen : "";
    input.value = chosen;
    folderField = chosen;
    const fallback = catalog.fallback || catalog.folder || "";
    input.placeholder = fallback ? `Default:${fallback}` : "";
  }
  if (note) {
    note.hidden = catalog.missing !== true;
  }
}

function paintTheme() {
  if (!mountRoot) {
    return;
  }
  try {
    paintThemeBody();
  } catch (err) {
    showBanner(messageOf(err));
  }
  paintThemeActions();
}

function paintThemeBody() {
  const shown = effectiveChoice(themeChoice, catalog.themes);
  const light = systemPrefersLight();
  let dataset = "dark";
  let hostTheme = "dark";
  let noteText = "";
  if (shown === "system") {
    dataset = light ? "light" : "dark";
    hostTheme = dataset;
    noteText = `Following the system: ${hostTheme}.`;
    applyFileStyle("");
  } else if (shown === "light" || shown === "dark") {
    dataset = shown;
    hostTheme = shown;
    noteText = `${shown === "light" ? "Light" : "Dark"} is in use.`;
    applyFileStyle("");
  } else {
    const theme = catalog.themes.find((item) => `file:${item.name}` === shown);
    dataset = "file";
    hostTheme = theme ? schemeOf(theme.text) : "dark";
    noteText = `${theme ? theme.name : "Theme"} is in use.`;
    applyFileStyle(theme ? theme.text : "");
    const row = buildCatalog(catalog.themes, light).find((item) => item.id === shown);
    if (row) {
      noteText = `${row.label} is in use.`;
    }
  }
  mountRoot.dataset.theme = dataset;
  const note = mountRoot.querySelector("#theme-resolved");
  if (note) {
    note.textContent = noteText;
  }
  renderThemeList(shown);
  applyFonts();
  applyIcons();
  applyMark();
  if (host && typeof host.onTheme === "function" && hostThemeSent !== hostTheme) {
    hostThemeSent = hostTheme;
    host.onTheme(hostTheme);
  }
}

async function refreshThemes(mine) {
  const epoch = ++listEpoch;
  if (!api || typeof api.themes === "undefined") {
    paintTheme();
    watchScheme();
    return;
  }
  try {
    await api.themes.ensure({ files: THEME_TEMPLATES });
  } catch (err) {
    if (mountRoot) {
      showBanner(messageOf(err));
    }
  }
  if (epoch !== listEpoch || (mine && !alive(mine))) {
    return;
  }
  try {
    const listed = await api.themes.list();
    if (epoch !== listEpoch || (mine && !alive(mine))) {
      return;
    }
    catalog = {
      folder: typeof listed.folder === "string" ? listed.folder : "",
      chosen: typeof listed.chosen === "string" ? listed.chosen : "",
      fallback: typeof listed.fallback === "string" ? listed.fallback : "",
      missing: listed.missing === true,
      themes: Array.isArray(listed.themes)
        ? listed.themes.filter((item) => item && typeof item.name === "string" && typeof item.text === "string")
        : [],
    };
  } catch (err) {
    if (mountRoot) {
      showBanner(messageOf(err));
    }
  }
  if (epoch !== listEpoch || !mountRoot || (mine && !alive(mine))) {
    return;
  }
  paintFolder();
  paintTheme();
  watchScheme();
}

function messageOf(err) {
  if (err instanceof Error) {
    return err.message;
  }
  return String(err);
}

function showBanner(text) {
  const banner = mountRoot.querySelector("#banner");
  banner.hidden = false;
  banner.textContent = text;
}

function showSaveFailure(err) {
  const detail = messageOf(err);
  const text = detail ? `Could not save the profile. ${detail}` : "Could not save the profile.";
  if (widgets) {
    widgets.toaster.toast(text);
    return;
  }
  showBanner(text);
}

function destroyWidgets() {
  const set = widgets;
  widgets = null;
  if (!set) {
    return;
  }
  set.menus.destroy();
  set.tooltips.destroy();
  set.toaster.destroy();
  set.dialogs.destroy();
}

const SAVE_MS = 300;
let saveTimer = 0;
let dirty = false;
let saveChain = Promise.resolve();

function scheduleUiSave() {
  if (!canSave) {
    return;
  }
  dirty = true;
  if (saveTimer) {
    return;
  }
  saveTimer = window.setTimeout(() => {
    saveTimer = 0;
    flushUiSave();
  }, SAVE_MS);
}

function flushUiSave() {
  if (!canSave || !dirty) {
    return saveChain;
  }
  dirty = false;
  const snapshot = clampUi(ui);
  const writer = api;
  saveChain = saveChain.catch(() => {}).then(() => {
    if (!writer || writer !== api) {
      return undefined;
    }
    return writer.prefs.ui.set(snapshot);
  }).catch((err) => {
    if (mountRoot && writer === api) {
      showSaveFailure(err);
    }
  });
  return saveChain;
}

function onPageHide() {
  if (saveTimer) {
    window.clearTimeout(saveTimer);
    saveTimer = 0;
  }
  flushUiSave();
}

function persistLayout() {
  scheduleUiSave();
}

async function loadPrefs() {
  const stored = clampUi(await api.prefs.ui.get());
  ui.nav = stored.nav;
  ui.mail = stored.mail;
  ui.contacts = stored.contacts;
  ui.files = stored.files;
  ui.zoom = stored.zoom;
  ui.activity = stored.activity;
  ui.log = stored.log;
  ui.icons = stored.icons;
  ui.fontawesome_kit = stored.fontawesome_kit;
  ui.fonts = stored.fonts;
  if (stored.fontSource === "theme" || stored.fontSource === "override") {
    ui.fontSource = stored.fontSource;
  } else {
    ui.fontSource =
      stored.fonts.body !== DEFAULT_UI.fonts.body || stored.fonts.mono !== DEFAULT_UI.fonts.mono ? "override" : "theme";
  }
  if (stored.iconSource === "theme" || stored.iconSource === "override") {
    ui.iconSource = stored.iconSource;
  } else {
    ui.iconSource = stored.icons && Object.keys(stored.icons).length > 0 ? "override" : "theme";
  }
  if (stored.themeChoice) {
    ui.themeChoice = stored.themeChoice;
    themeChoice = stored.themeChoice;
  }
  const kitInput = mountRoot.querySelector("#fa-kit-html");
  if (kitInput) {
    kitInput.value = ui.fontawesome_kit;
  }
  paintZoomSliders();
  applyZoom();
  applyFonts();
  if (ui.iconSource === "override") {
    for (const key of Object.keys(ui.icons)) {
      applyIconOverride(key);
    }
  }
  for (const input of mountRoot.querySelectorAll("#icon-list .icon-input")) {
    const key = input.dataset.iconKey;
    if (ui.icons[key]) {
      input.value = ui.icons[key];
    }
  }
  refreshIconPreviews();
  canSave = true;
}

const sections = {
  mail: { title: "Mail", nav: true },
  contacts: { title: "Contacts", nav: true },
  calendars: { title: "Calendars", nav: true },
  files: { title: "Files", nav: true },
  settings: { title: "Settings", nav: true },
  activity: { title: "Activity", nav: false },
  help: { title: "Help", nav: false },
};

let tabCtl = null;
let keys = null;
let testSerial = 0;
let navSection = "mail";
let logView = null;
let activityView = null;
let syncing = 0;
let motionQuery = null;
let onMotion = null;

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function letters(root, pressed, onPick) {
  for (let code = 65; code <= 90; code += 1) {
    const letter = String.fromCharCode(code);
    const button = document.createElement("button");
    button.type = "button";
    button.dataset.letter = letter;
    button.textContent = letter;
    button.setAttribute("aria-pressed", pressed && letter === "A" ? "true" : "false");
    button.addEventListener("click", () => {
      for (const other of root.querySelectorAll("button")) {
        other.setAttribute("aria-pressed", other === button ? "true" : "false");
      }
      if (typeof onPick === "function") {
        onPick(letter);
      }
    });
    root.append(button);
  }
}

function mondayIndex(date) {
  return (date.getDay() + 6) % 7;
}

function addDays(date, count) {
  const next = new Date(date);
  next.setDate(next.getDate() + count);
  return next;
}

function fillCalendar(today) {
  const mini = mountRoot.querySelector("#mini-cal");
  const month = mountRoot.querySelector("#month");
  const names = document.createElement("div");
  names.className = "week week-names";
  for (const name of ["M", "T", "W", "T", "F", "S", "S"]) {
    const cell = document.createElement("span");
    cell.textContent = name;
    names.append(cell);
  }
  mini.append(names);

  const first = new Date(today.getFullYear(), today.getMonth(), 1);
  const last = new Date(today.getFullYear(), today.getMonth() + 1, 0);
  for (let pad = 0; pad < mondayIndex(first); pad += 1) {
    month.append(document.createElement("div"));
  }
  const start = addDays(first, -7 - mondayIndex(first));
  const end = addDays(last, 7 + (6 - mondayIndex(last)));
  const sameDay = (a, b) =>
    a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();

  let cursor = start;
  while (cursor <= end) {
    const week = document.createElement("div");
    week.className = "week";
    for (let day = 0; day < 7; day += 1) {
      const cell = document.createElement("span");
      cell.textContent = String(cursor.getDate());
      if (cursor.getMonth() !== today.getMonth()) {
        cell.dataset.outside = "true";
      }
      if (sameDay(cursor, today)) {
        cell.dataset.today = "true";
      }
      const block = document.createElement("div");
      block.textContent = String(cursor.getDate());
      if (sameDay(cursor, today)) {
        block.dataset.today = "true";
      }
      if (cursor >= first && cursor <= last) {
        month.append(block);
      }
      week.append(cell);
      cursor = addDays(cursor, 1);
    }
    mini.append(week);
  }
}

function setActivityView(name) {
  ui.activity.view = name === "activity" ? "activity" : "log";
  paintActivity();
  persistLayout();
  syncLog();
}

function bindNavAffordances() {
  for (const button of mountRoot.querySelectorAll(".nav-buttons button")) {
    widgets.tooltips.bind(button, button.getAttribute("aria-label") || "");
  }
  const activity = mountRoot.querySelector('.nav-buttons [data-section="activity"]');
  widgets.menus.bind(activity, () => {
    const view = ui.activity.view === "activity" ? "activity" : "log";
    return [
      {
        label: "Activity",
        current: view === "activity",
        onSelect() {
          setActivityView("activity");
          showNav("activity");
          openHome("activity");
        },
      },
      {
        label: "Logs",
        current: view === "log",
        onSelect() {
          setActivityView("log");
          showNav("activity");
          openHome("activity");
        },
      },
    ];
  });
}

function paintActivity() {
  const name = ui.activity.view === "activity" ? "activity" : "log";
  for (const button of mountRoot.querySelectorAll("[data-activity-view]")) {
    button.setAttribute("aria-selected", button.dataset.activityView === name ? "true" : "false");
  }
  for (const pane of mountRoot.querySelectorAll("[data-activity-pane]")) {
    pane.hidden = pane.dataset.activityPane !== name;
  }
  const bottom = mountRoot.querySelector(".log-bottombar");
  if (bottom) {
    bottom.hidden = name !== "log";
  }
  const top = mountRoot.querySelector(".log-topbar");
  if (top) {
    top.setAttribute("aria-label", name === "activity" ? "Activity tools" : "Log tools");
  }
  if (logView) {
    logView.setChrome(name === "log");
  }
  if (activityView) {
    activityView.setShown(name === "activity");
  }
}

function syncLog() {
  if (!logView) {
    return;
  }
  const panel = mountRoot.querySelector("#panel-activity");
  const show = panel && !panel.hidden && ui.activity.view !== "activity";
  if (show) {
    logView.setZoom(ui.zoom.log);
    logView.open().then(() => {
      logView.measure();
    }).catch((err) => {
      showBanner(messageOf(err));
    });
  } else {
    logView.close();
  }
}

function homeIconNode(section) {
  const icon = iconElement(section);
  if (!icon) {
    return null;
  }
  const clone = icon.cloneNode(true);
  clone.removeAttribute("id");
  return clone;
}

function homeIconLabel(section) {
  const icon = iconElement(section);
  if (!icon) {
    return "";
  }
  if (icon.tagName === "IMG") {
    return icon.getAttribute("src") || "";
  }
  const classes = [...icon.classList].filter((name) => name !== "icon");
  if (classes.length > 0) {
    return classes.join(" ");
  }
  return icon.dataset.iconDefinition || "";
}

function showNav(section) {
  if (!mountRoot) {
    return;
  }
  const meta = sections[section];
  if (!meta) {
    return;
  }
  if (meta.nav) {
    navSection = section;
  }
  for (const button of mountRoot.querySelectorAll(".nav-buttons [data-section]")) {
    button.setAttribute("aria-pressed", button.dataset.section === section ? "true" : "false");
  }
  for (const body of mountRoot.querySelectorAll("[data-nav]")) {
    body.hidden = body.dataset.nav !== navSection;
  }
}

function openHome(section, context) {
  if (!tabCtl) {
    return null;
  }
  return tabCtl.openHome(section, context);
}

function bindTabKeys() {
  if (!keys || !tabCtl) {
    return;
  }
  const tab = (id, chords, run) => {
    keys.register({ id, scope: "workspace", chords, run });
  };
  tab("tab.next", [["Ctrl+Tab"]], () => {
    tabCtl.cycle(1);
  });
  tab("tab.previous", [["Ctrl+Shift+Tab"]], () => {
    tabCtl.cycle(-1);
  });
  tab("tab.close", [["Ctrl+W"], ["Ctrl+F4"]], () => {
    void tabCtl.closeActive();
  });
  tab("tab.new", [["Ctrl+T"]], () => {});
  tab("tab.reopen", [["Ctrl+Shift+T"]], () => {
    tabCtl.reopenClosed();
  });
  tab("tab.reload", [["Ctrl+R"]], () => {
    tabCtl.reloadActive();
  });
  for (let n = 1; n <= 9; n += 1) {
    const index = n - 1;
    tab(`tab.goto.${n}`, [[`Alt+${n}`]], () => {
      tabCtl.activateAt(index);
    });
  }
  const switches = [
    ["1", "mail"],
    ["2", "contacts"],
    ["3", "calendars"],
    ["4", "files"],
  ];
  for (const [digit, section] of switches) {
    keys.register({
      id: `section.${section}`,
      scope: "global",
      chords: [[`Ctrl+${digit}`]],
      run() {
        showNav(section);
        openHome(section);
        tabCtl.focusActive();
      },
    });
  }
}

function bindNavResize() {
  const nav = mountRoot.querySelector("#nav");
  const handle = mountRoot.querySelector("#nav-resize");
  nav.style.width = `${ui.nav.width}px`;
  handle.addEventListener("pointerdown", (event) => {
    handle.setPointerCapture(event.pointerId);
    const move = (pointer) => {
      const width = clamp(pointer.clientX - nav.getBoundingClientRect().left, ui.nav.min, ui.nav.max);
      ui.nav.width = Math.round(width);
      nav.style.width = `${ui.nav.width}px`;
    };
    const stop = (pointer) => {
      move(pointer);
      handle.removeEventListener("pointermove", move);
      handle.removeEventListener("pointerup", stop);
      persistLayout();
    };
    handle.addEventListener("pointermove", move);
    handle.addEventListener("pointerup", stop);
  });
}

function bindSplit(name) {
  const handle = mountRoot.querySelector(`[data-split="${name}"]`);
  const pane = handle.previousElementSibling;
  const bounds = ui[name];
  pane.style.flexBasis = `${bounds.split * 100}%`;
  handle.addEventListener("pointerdown", (event) => {
    const parent = handle.parentElement;
    handle.setPointerCapture(event.pointerId);
    const move = (pointer) => {
      const rect = parent.getBoundingClientRect();
      const ratio = rect.width > 0
        ? clamp((pointer.clientX - rect.left) / rect.width, bounds.min, bounds.max)
        : bounds.split;
      bounds.split = ratio;
      pane.style.flexBasis = `${ratio * 100}%`;
    };
    const stop = (pointer) => {
      move(pointer);
      handle.removeEventListener("pointermove", move);
      handle.removeEventListener("pointerup", stop);
      persistLayout();
    };
    handle.addEventListener("pointermove", move);
    handle.addEventListener("pointerup", stop);
  });
}

function chooseTheme(next, record, onFail) {
  if (!mountRoot) {
    return;
  }
  const previous = themeChoice;
  const previousUi = ui.themeChoice;
  const pushed = Boolean(record) && next !== previous;
  if (pushed) {
    themePast.push(previous);
    themeFuture = [];
  }
  themeChoice = next;
  ui.themeChoice = next;
  paintTheme();
  watchScheme();
  const core = next === "light" || next === "system" ? next : "dark";
  dirty = true;
  if (saveTimer) {
    window.clearTimeout(saveTimer);
    saveTimer = 0;
  }
  flushUiSave();
  if (!api || typeof api.prefs === "undefined") {
    refreshThemes(session);
    return;
  }
  api.prefs.set({ theme: core }).then(() => {
    if (mountRoot && themeChoice === next) {
      refreshThemes(session);
    }
  }).catch((err) => {
    if (!mountRoot) {
      return;
    }
    if (pushed) {
      themePast.pop();
    }
    if (typeof onFail === "function") {
      onFail();
    }
    themeChoice = previous;
    ui.themeChoice = previousUi;
    paintTheme();
    watchScheme();
    showSaveFailure(err);
  });
}

function bindSettings() {
  for (const label of mountRoot.querySelectorAll("[data-zoom]")) {
    const key = label.dataset.zoom;
    if (!(key in ui.zoom)) {
      continue;
    }
    const input = label.querySelector("input");
    const value = label.querySelector(".zoom-value");
    const show = () => {
      ui.zoom[key] = zoomClamp(key, input.value);
      for (const other of mountRoot.querySelectorAll(`[data-zoom="${key}"]`)) {
        const field = other.querySelector("input");
        const reading = other.querySelector(".zoom-value");
        if (field && field !== input) {
          field.value = String(ui.zoom[key]);
        }
        if (reading) {
          reading.textContent = zoomLabel(ui.zoom[key]);
        }
      }
      value.textContent = zoomLabel(ui.zoom[key]);
      applyZoom();
    };
    input.addEventListener("input", show);
    input.addEventListener("change", () => {
      show();
      persistLayout();
    });
  }
  const wheelSwitch = mountRoot.querySelector("#zoom-wheel");
  if (wheelSwitch) {
    wheelSwitch.addEventListener("click", () => {
      ui.zoom.wheel = ui.zoom.wheel === "toward" ? "away" : "toward";
      paintWheelSwitch();
      persistLayout();
    });
  }
  const themeDefault = mountRoot.querySelector("#theme-default");
  const themeUndo = mountRoot.querySelector("#theme-undo");
  const themeRedo = mountRoot.querySelector("#theme-redo");
  if (themeDefault) {
    themeDefault.addEventListener("click", () => {
      if (effectiveChoice(themeChoice, catalog.themes) === "system") {
        return;
      }
      chooseTheme("system", true);
    });
  }
  if (themeUndo) {
    themeUndo.addEventListener("click", () => {
      if (themePast.length === 0) {
        return;
      }
      const current = themeChoice;
      const next = themePast.pop();
      themeFuture.push(current);
      chooseTheme(next, false, () => {
        themeFuture.pop();
        themePast.push(next);
      });
    });
  }
  if (themeRedo) {
    themeRedo.addEventListener("click", () => {
      if (themeFuture.length === 0) {
        return;
      }
      const current = themeChoice;
      const next = themeFuture.pop();
      themePast.push(current);
      chooseTheme(next, false, () => {
        themePast.pop();
        themeFuture.push(next);
      });
    });
  }
  for (const [id, key] of [["#body-font", "body"], ["#mono-font", "mono"]]) {
    const select = mountRoot.querySelector(id);
    if (!select) {
      continue;
    }
    showFont(select, ui.fonts[key]);
    select.addEventListener("change", () => {
      if (ui.fontSource !== "override") {
        return;
      }
      ui.fonts[key] = select.value;
      applyFonts();
      persistLayout();
    });
  }
  const fontSource = mountRoot.querySelector("#font-source");
  if (fontSource) {
    fontSource.addEventListener("change", () => {
      const next = fontSource.value === "override" ? "override" : "theme";
      if (next === "override" && ui.fontSource !== "override") {
        const computed = getComputedStyle(mountRoot);
        const body = unquote(computed.getPropertyValue("--pav-font")).trim();
        const mono = unquote(computed.getPropertyValue("--pav-font-mono")).trim();
        if (body) {
          ui.fonts.body = body;
        }
        if (mono) {
          ui.fonts.mono = mono;
        }
      }
      ui.fontSource = next;
      applyFonts();
      persistLayout();
    });
  }
  const iconSource = mountRoot.querySelector("#icon-source");
  if (iconSource) {
    iconSource.addEventListener("change", () => {
      const next = iconSource.value === "override" ? "override" : "theme";
      if (next === "override" && ui.iconSource !== "override") {
        const computed = getComputedStyle(mountRoot);
        ui.icons = { ...ui.icons };
        for (const key of ICON_KEYS) {
          const shown = unquote(computed.getPropertyValue(`--pav-icon-${key}`)).trim();
          if (shown) {
            ui.icons[key] = shown;
          }
        }
        const kit = unquote(computed.getPropertyValue("--pav-icon-kit")).trim();
        if (kit) {
          ui.fontawesome_kit = kit;
        }
      }
      ui.iconSource = next;
      applyIcons();
      persistLayout();
    });
  }
  const themeList = mountRoot.querySelector("#theme-list");
  if (themeList) {
    themeList.addEventListener("change", (event) => {
      const input = event.target;
      if (!(input instanceof HTMLInputElement) || input.name !== "pav-theme") {
        return;
      }
      chooseTheme(input.value, true);
    });
  }
  const themeFolder = mountRoot.querySelector("#theme-folder");
  if (themeFolder) {
    const commitFolder = () => {
      const next = themeFolder.value.trim();
      if (next === folderField) {
        return;
      }
      api.themes.folder.set(next).then(() => refreshThemes(session)).catch((err) => {
        if (!mountRoot) {
          return;
        }
        themeFolder.value = folderField;
        showBanner(messageOf(err));
      });
    };
    themeFolder.addEventListener("change", commitFolder);
    themeFolder.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        commitFolder();
      }
    });
    const pick = mountRoot.querySelector("#theme-folder-pick");
    if (pick) {
      pick.addEventListener("click", () => {
        const dialog = host && host.dialog;
        if (!dialog || typeof dialog.open !== "function") {
          showBanner("Folder picker is not available.");
          return;
        }
        dialog.open({ directory: true, title: "Themes folder" }).then((picked) => {
          if (!mountRoot || picked == null || picked === "" || Array.isArray(picked)) {
            return;
          }
          themeFolder.value = String(picked);
          commitFolder();
        }).catch((err) => {
          if (mountRoot) {
            showBanner(messageOf(err));
          }
        });
      });
    }
  }
  const themeRefresh = mountRoot.querySelector("#theme-refresh");
  if (themeRefresh) {
    themeRefresh.addEventListener("click", () => {
      refreshThemes(session);
    });
  }
  for (const button of mountRoot.querySelectorAll("[data-heading]")) {
    button.addEventListener("click", () => {
      for (const other of mountRoot.querySelectorAll("[data-heading]")) {
        other.removeAttribute("aria-current");
      }
      button.setAttribute("aria-current", "true");
      for (const pane of mountRoot.querySelectorAll("[data-settings]")) {
        pane.hidden = pane.dataset.settings !== button.dataset.heading;
      }
      if (button.dataset.heading === "theme") {
        refreshThemes(session);
      }
      showNav("settings");
      openHome("settings", { heading: button.dataset.heading });
    });
  }
  const iconForm = mountRoot.querySelector("#icon-form");
  if (iconForm) {
    const previewBtn = iconForm.querySelector("#icon-preview");
    const reloadBtn = iconForm.querySelector("#icon-reload");

    function setDirty(isDirty) {
      const locked = ui.iconSource !== "override";
      if (previewBtn) {
        previewBtn.disabled = locked || !isDirty;
      }
      if (reloadBtn) {
        reloadBtn.disabled = locked || !isDirty;
      }
    }

    function updatePreviews() {
      for (const input of iconForm.querySelectorAll(".icon-input")) {
        const key = input.dataset.iconKey;
        const preview = mountRoot.querySelector(`#icon-list .icon[data-icon-key="${key}"]`);
        if (preview) {
          paintPreview(preview, input.value || input.defaultValue);
        }
      }
    }

    for (const input of iconForm.querySelectorAll(".icon-input")) {
      input.addEventListener("input", () => {
        if (ui.iconSource !== "override") {
          return;
        }
        const key = input.dataset.iconKey;
        ui.icons[key] = input.value;
        const preview = mountRoot.querySelector(`#icon-list .icon[data-icon-key="${key}"]`);
        if (preview) {
          paintPreview(preview, input.value || input.defaultValue);
        }
        for (const other of iconForm.querySelectorAll(".icon-input")) {
          if (other.value !== other.defaultValue) {
            setDirty(true);
            return;
          }
        }
        const kitInput = iconForm.querySelector("#fa-kit-html");
        if (kitInput && kitInput.value !== kitInput.defaultValue) {
          setDirty(true);
          return;
        }
        setDirty(false);
      });
    }

    const kitInput = iconForm.querySelector("#fa-kit-html");
    if (kitInput) {
      kitInput.addEventListener("input", () => {
        if (ui.iconSource !== "override") {
          return;
        }
        ui.fontawesome_kit = kitInput.value;
        let dirty = false;
        for (const input of iconForm.querySelectorAll(".icon-input")) {
          if (input.value !== input.defaultValue) {
            dirty = true;
            break;
          }
        }
        if (!dirty && kitInput.value === kitInput.defaultValue) {
          setDirty(false);
        } else {
          setDirty(true);
        }
      });
    }

    if (previewBtn) {
      previewBtn.addEventListener("click", (event) => {
        event.preventDefault();
        for (const input of iconForm.querySelectorAll(".icon-input")) {
          ui.icons[input.dataset.iconKey] = input.value;
        }
        if (kitInput) {
          ui.fontawesome_kit = kitInput.value.trim();
        }
        dirty = true;
        flushUiSave().then(() => {
          for (const key of Object.keys(ui.icons)) {
            applyIconOverride(key);
          }
          setDirty(false);
        });
      });
    }

    iconForm.addEventListener("submit", (event) => {
      event.preventDefault();
      for (const input of iconForm.querySelectorAll(".icon-input")) {
        ui.icons[input.dataset.iconKey] = input.value;
      }
      if (kitInput) {
        ui.fontawesome_kit = kitInput.value.trim();
      }
      dirty = true;
      flushUiSave().then(() => {
        if (host && typeof host.reload === "function") {
          host.reload();
        }
      });
    });

    setDirty(false);
  }
}

function reducedMotion() {
  return Boolean(motionQuery && motionQuery.matches);
}

function paintUpdate() {
  if (!mountRoot) {
    return;
  }
  const button = mountRoot.querySelector("#update");
  if (!button) {
    return;
  }
  const n = syncing > 0 ? syncing : 0;
  const label = n === 0 ? "Update" : `${n} syncing`;
  button.setAttribute("aria-label", label);
  if (widgets) {
    widgets.tooltips.bind(button, label);
  }
  const badge = button.querySelector(".update-badge");
  if (n > 0) {
    button.classList.add("is-busy");
    button.setAttribute("aria-busy", "true");
  } else {
    button.classList.remove("is-busy");
    button.removeAttribute("aria-busy");
  }
  if (badge) {
    badge.textContent = n > 0 ? String(n) : "";
    badge.hidden = !(reducedMotion() && n > 0);
  }
}

function bindUpdate() {
  const button = mountRoot.querySelector("#update");
  button.addEventListener("click", () => {
    api.sync.now({}).catch((err) => showBanner(messageOf(err)));
  });
  const view = mountRoot.ownerDocument.defaultView;
  if (view && typeof view.matchMedia === "function") {
    motionQuery = view.matchMedia("(prefers-reduced-motion: reduce)");
    if (motionQuery && typeof motionQuery.addEventListener === "function") {
      onMotion = () => paintUpdate();
      motionQuery.addEventListener("change", onMotion);
    }
  }
  paintUpdate();
}

function bindDemo(enabled) {
  const shown = enabled === true;
  const button = mountRoot.querySelector("#demo-activity");
  if (button) {
    button.hidden = !shown;
    button.addEventListener("click", () => {
      api.debug.activityDemo({ steps: 8, ms: 400 }).catch((err) => showBanner(messageOf(err)));
    });
  }
  const opener = mountRoot.querySelector("#open-test-tab");
  if (!opener) {
    return;
  }
  opener.hidden = !shown;
  opener.addEventListener("click", () => {
    if (!tabCtl) {
      return;
    }
    testSerial += 1;
    const title = `Test ${testSerial}`;
    tabCtl.openTab({
      section: "help",
      kind: "test",
      key: String(testSerial),
      title,
      icon: KIND_ICONS.file,
      context: { n: testSerial },
    });
  });
}

function alive(mine) {
  return session === mine;
}

export function stopShell() {
  destroyWidgets();
  const current = session;
  session = null;
  detachScheme();
  themeChoice = "system";
  themePast = [];
  themeFuture = [];
  hostThemeSent = "";
  listEpoch += 1;
  catalog = { folder: "", chosen: "", fallback: "", missing: false, themes: [] };
  folderField = "";
  const fileTheme = document.getElementById("pav-file-theme");
  if (fileTheme) {
    fileTheme.remove();
  }
  if (current) {
    window.removeEventListener("pagehide", onPageHide);
    if (current.isolationStop) {
      current.isolationStop();
    }
  }
  if (saveTimer) {
    window.clearTimeout(saveTimer);
    saveTimer = 0;
  }
  if (logView) {
    logView.destroy();
    logView = null;
  }
  if (activityView) {
    activityView.destroy();
    activityView = null;
  }
  if (motionQuery && onMotion && typeof motionQuery.removeEventListener === "function") {
    motionQuery.removeEventListener("change", onMotion);
  }
  motionQuery = null;
  onMotion = null;
  syncing = 0;
  if (mountRoot && onZoomWheel) {
    mountRoot.removeEventListener("wheel", onZoomWheel, true);
  }
  onZoomWheel = null;
  wheelRemainder = 0;
  if (keys) {
    keys.destroy();
    keys = null;
  }
  if (tabCtl) {
    tabCtl.destroy();
    tabCtl = null;
  }
  testSerial = 0;
  navSection = "mail";
  canSave = false;
  dirty = false;
  saveChain = Promise.resolve();
  mountRoot = null;
  api = null;
  host = null;
  ui = clampUi({});
}

export async function startShell(surface, nextApi, nextHost, result) {
  stopShell();
  const mine = { isolationStop: null };
  session = mine;
  mountRoot = surface;
  onZoomWheel = onWheelZoom;
  mountRoot.addEventListener("wheel", onZoomWheel, { capture: true, passive: false });
  api = nextApi;
  host = nextHost || {};
  widgets = {
    menus: createMenu(mountRoot),
    tooltips: createTooltip(mountRoot),
    toaster: createToaster(mountRoot),
    dialogs: createDialogs(mountRoot),
  };
  tabCtl = createTabs(mountRoot.querySelector("#tablist"), {
    workspace: mountRoot.querySelector(".workspace"),
    sections,
    homeIcon: homeIconNode,
    iconLabel: homeIconLabel,
    onReveal(section) {
      showNav(section);
      openHome(section);
    },
    menu: widgets.menus,
    strip: mountRoot.querySelector(".tabs"),
    confirm(entry) {
      if (!widgets) {
        return Promise.resolve(false);
      }
      return widgets.dialogs.confirm({
        title: `Close ${entry.title}?`,
        text: "This tab has unsaved work.",
        okLabel: "Close",
        danger: true,
      });
    },
  });
  keys = createKeys(mountRoot);
  bindTabKeys();
  tabCtl.register("test", {
    create(el, ctx) {
      const note = document.createElement("p");
      note.className = "empty";
      note.textContent = `${ctx.title}.`;
      el.append(note);
    },
  });
  tabCtl.subscribe(() => {
    syncLog();
  });
  bindNavAffordances();
  window.addEventListener("pagehide", onPageHide);
  const banner = mountRoot.querySelector("#banner");
  if (result.state === "error") {
    banner.hidden = false;
    banner.textContent = result.error || result.state;
  } else {
    mountRoot.querySelector("#profile-name").textContent = result.profileName || "";
    try {
      await loadPrefs();
    } catch (err) {
      if (!alive(mine)) {
        return;
      }
      canSave = false;
      showBanner(messageOf(err));
    }
  }
  if (!alive(mine)) {
    return;
  }
  let timeZone = "America/Vancouver";
  let logLevel = "info";
  if (banner.hidden) {
    try {
      const prefs = await api.prefs.get();
      if (!alive(mine)) {
        return;
      }
      if (typeof prefs.timezone === "string" && prefs.timezone !== "") {
        timeZone = prefs.timezone;
      }
      if (typeof prefs.logLevel === "string" && prefs.logLevel !== "") {
        logLevel = prefs.logLevel;
      }
      themeChoice = ui.themeChoice || normalizeTheme(prefs.theme);
      await refreshThemes(mine);
    } catch (err) {
      if (!alive(mine)) {
        return;
      }
      showBanner(messageOf(err));
    }
  }
  if (!alive(mine)) {
    return;
  }
  activityView = createActivityView(mountRoot.querySelector("[data-activity-pane='activity']"), api, {
    timeZone,
    history: ui.activity.history === true,
    onHistory(value) {
      ui.activity.history = value === true;
      persistLayout();
    },
    onSync(count) {
      syncing = count;
      paintUpdate();
    },
    onError(text) {
      const banner = mountRoot.querySelector("#banner");
      if (banner && !banner.hidden) {
        return;
      }
      showBanner(text);
    },
    topbar: mountRoot.querySelector(".log-topbar"),
    root: mountRoot,
  });
  logView = createLogView(mountRoot.querySelector("[data-activity-pane='log']"), api, {
    timeZone,
    zoom: ui.zoom.log,
    filter: ui.log,
    logLevel,
    root: mountRoot,
    topbar: mountRoot.querySelector(".log-topbar"),
    bottombar: mountRoot.querySelector(".log-bottombar"),
    host,
    onFilter(next) {
      ui.log = next;
      persistLayout();
    },
    onLogLevel(level) {
      return api.prefs.set({ logLevel: level });
    },
    onError(text) {
      showBanner(text);
    },
  });
  paintActivity();
  for (const button of mountRoot.querySelectorAll("[data-activity-view]")) {
    button.addEventListener("click", () => {
      setActivityView(button.dataset.activityView);
    });
  }
  letters(mountRoot.querySelector("#letters-nav"), true, (letter) => {
    showNav("contacts");
    openHome("contacts", { letter });
  });
  letters(mountRoot.querySelector("#letters-main"), true);
  fillCalendar(new Date());
  for (const button of mountRoot.querySelectorAll(".nav-buttons [data-section]")) {
    button.addEventListener("click", () => {
      showNav(button.dataset.section);
      openHome(button.dataset.section);
    });
  }
  bindNavResize();
  bindSplit("mail");
  bindSplit("contacts");
  bindSplit("files");
  bindSettings();
  bindUpdate();
  bindDemo(result.debug === true);
  mine.isolationStop = installIsolationTest({
    api,
    root: mountRoot,
    enabled: result.debug === true || result.selftest === true,
    openMail() {
      showNav("mail");
      openHome("mail");
    },
    confirm(spec) {
      if (!widgets) {
        return Promise.resolve(false);
      }
      return widgets.dialogs.confirm(spec);
    },
  });
  for (const button of mountRoot.querySelectorAll(".tree button")) {
    button.addEventListener("click", () => {
      for (const other of mountRoot.querySelectorAll(".tree button")) {
        other.removeAttribute("aria-current");
      }
      button.setAttribute("aria-current", "true");
      if (button.dataset.mailbox) {
        showNav("mail");
        openHome("mail", { mailboxId: button.dataset.mailbox });
      }
    });
  }
  showNav("mail");
  openHome("mail");
  if (activityView) {
    await activityView.ready();
  }
}
