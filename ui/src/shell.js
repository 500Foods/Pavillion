import { createForwarder, forwardEvents } from "./log/forward.js";
import { createDialogs } from "./common/dialog.js";
import { installIsolationTest } from "./common/isolation.js";
import { createKeys } from "./common/keys.js";
import { createMenu } from "./common/menu.js";
import { createToaster } from "./common/toast.js";
import { createTooltip } from "./common/tooltip.js";
import { clampMaxTabs, planRestore, serializeTabs, SESSION_MS } from "./shell/session.js";
import { createTabs, KIND_ICONS } from "./shell/tabs.js";
import { createFeatureEditor } from "./shell/features.js";
import { createRailDrag } from "./shell/rail-drag.js";
import { createActivityView } from "./activity/view.js";
import { createUpdatePie } from "./activity/update-pie.js";
import { createAccountsView } from "./settings/accounts.js";
import { createLogSettings } from "./settings/log.js";
import { createLimitsSettings } from "./settings/limits.js";
import { createFormatsSettings } from "./settings/formats.js";
import { createMailSettings } from "./settings/mail.js";
import { configureFormats, currentZone, subscribeFormats } from "./formats.js";
import { createHelpView } from "./help/view.js";
import { createBackTracker } from "./help/back.js";
import { createLogView } from "./log/view.js";
import { createMailList } from "./mail/list.js";
import { createMailNav } from "./mail/nav.js";
import { renderRow } from "./tree/render.js";
import { DEFAULT_UI, FEATURE_KEYS, RAIL_KEYS, chordsOf, clampUi, railOf, snapZoom } from "./prefs.js";
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
let forwarder = null;
let unforward = null;

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
  helpIndex: "#help-index",
  helpContent: "#help-article",
  accountsForm: ".accounts-form-pane",
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
  if (accountsView && typeof accountsView.setZoom === "function") {
    /* K-Q14: in place, by font and widths; never CSS zoom on a table. */
    accountsView.setZoom(ui.zoom.shell * ui.zoom.accountsTable, ui.zoom.accountsTable);
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
  if (target.closest("#help-index")) {
    return "helpIndex";
  }
  if (target.closest("#help-article")) {
    return "helpContent";
  }
  if (target.closest("#accounts-tree")) {
    return "accountsTable";
  }
  if (target.closest(".accounts-form-pane")) {
    return "accountsForm";
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

const ICON_KEYS = ["mail", "contacts", "calendars", "tasks", "files", "notes", "argent", "bots", "github", "activity", "settings", "update", "help"];

function iconElement(key) {
  return mountRoot.querySelector(`.nav [data-rail="${key}"] .icon`);
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
  if (featureEditor) {
    featureEditor.refreshIcons();
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
  if (featureEditor) {
    featureEditor.refreshIcons();
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
  /* A late rejection after the shell stopped has nowhere to show. */
  if (!mountRoot) {
    return;
  }
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
let sessionTimer = 0;
let sessionDirty = false;
let sessionSnapshot = "";
let quitNoted = false;
let restoring = false;
let launching = false;
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
    return saveChain.then(() => true);
  }
  dirty = false;
  sessionDirty = false;
  sessionSnapshot = JSON.stringify(ui.session);
  const snapshot = clampUi(ui);
  const writer = api;
  const pending = saveChain.catch(() => {}).then(() => {
    if (!writer || writer !== api) {
      return false;
    }
    return writer.prefs.ui.set(snapshot).then(() => true);
  }).catch((err) => {
    if (mountRoot && writer === api) {
      showSaveFailure(err);
    }
    return false;
  });
  saveChain = pending.then(() => {});
  return pending;
}

function sessionRecord(shutdown) {
  return {
    tabs: serializeTabs(tabCtl.list()),
    restore: ui.session.restore !== false,
    shutdown: shutdown === "clean" ? "clean" : "open",
    maxTabs: clampMaxTabs(ui.session.maxTabs),
  };
}

function noteSession(shutdown) {
  if (!tabCtl || !ui.session) {
    return;
  }
  const next = sessionRecord(shutdown);
  const encoded = JSON.stringify(next);
  ui.session = next;
  if (encoded === sessionSnapshot) {
    return;
  }
  sessionSnapshot = encoded;
  sessionDirty = true;
  if (shutdown === "clean" || sessionTimer || !canSave) {
    return;
  }
  sessionTimer = window.setTimeout(() => {
    sessionTimer = 0;
    if (!sessionDirty) {
      return;
    }
    dirty = true;
    flushUiSave();
  }, SESSION_MS);
}

/* Write shutdown open before any tab loads. The live list is whatever is already
   open, which is empty at launch. A crash in the first page is then not a clean quit. */
async function markSessionOpen() {
  if (!canSave || !tabCtl || !ui.session) {
    return true;
  }
  ui.session = sessionRecord("open");
  sessionDirty = true;
  dirty = true;
  if (sessionTimer) {
    window.clearTimeout(sessionTimer);
    sessionTimer = 0;
  }
  return flushUiSave();
}

function finishSession(shutdown) {
  noteSession(shutdown);
  if (sessionDirty) {
    dirty = true;
  }
  if (saveTimer) {
    window.clearTimeout(saveTimer);
    saveTimer = 0;
  }
  if (sessionTimer) {
    window.clearTimeout(sessionTimer);
    sessionTimer = 0;
  }
  return flushUiSave();
}

function onPageHide() {
  if (quitNoted) {
    return;
  }
  quitNoted = true;
  finishSession("clean");
}

function persistLayout() {
  scheduleUiSave();
}

function applyFormatPrefs(prefs) {
  profileTimeFormat = prefs && prefs.timeFormat === "12h" ? "12h" : "24h";
  profileZone = prefs && typeof prefs.timezone === "string" ? prefs.timezone : "";
  configureFormats({
    timezone: profileZone,
    timeFormat: profileTimeFormat,
    formats: ui.formats,
  });
  logTimeZone = currentZone();
  return logTimeZone;
}

function repaintFormatSurfaces() {
  const zone = currentZone();
  logTimeZone = zone;
  if (logView) {
    logView.setTimeZone(zone);
  }
  if (activityView) {
    activityView.setTimeZone(zone);
  }
  if (logSettings && typeof logSettings.repaintTimes === "function") {
    logSettings.repaintTimes();
  }
  if (limitsSettings && typeof limitsSettings.repaintTimes === "function") {
    limitsSettings.repaintTimes();
  }
  if (accountsView && typeof accountsView.repaintTimes === "function") {
    accountsView.repaintTimes();
  }
}

async function loadPrefs() {
  const stored = clampUi(await api.prefs.ui.get());
  ui.nav = stored.nav;
  ui.mail = stored.mail;
  ui.contacts = stored.contacts;
  ui.files = stored.files;
  ui.help = stored.help;
  ui.accounts = stored.accounts; /* K-Q14: the split ratio was never restored */
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
   ui.features = stored.features;
  ui.rail = stored.rail;
  ui.chords = stored.chords;
  ui.trees = stored.trees || {};
  ui.tables = stored.tables && typeof stored.tables === "object" && !Array.isArray(stored.tables) ? stored.tables : {};
  paintFeatures();
  ui.formats = stored.formats;
  ui.session = stored.session;
  sessionSnapshot = JSON.stringify(ui.session);
  sessionDirty = false;
  paintSessionRestore();
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
  tasks: { title: "Tasks", nav: true },
  files: { title: "Files", nav: true },
  notes: { title: "Notes", nav: true },
  argent: { title: "Argent", nav: true },
  bots: { title: "Grok Bots", nav: false },
  github: { title: "GitHub", nav: false },
  settings: { title: "Settings", nav: true },
  activity: { title: "Activity", nav: false },
  help: { title: "Help", nav: false },
};

function featureVisible(section) {
  if (!FEATURE_KEYS.includes(section)) {
    return true;
  }
  return Boolean(ui.features && ui.features[section] === true);
}

function placeRailButtons() {
  if (!mountRoot || !ui.rail) {
    return;
  }
  const bars = mountRoot.querySelectorAll(".nav > .nav-buttons");
  const topBar = bars[0];
  const bottomBar = bars[1];
  if (!topBar || !bottomBar) {
    return;
  }
  for (const jump of mountRoot.querySelectorAll(".help-jump")) {
    jump.hidden = !featureVisible("help");
  }
  for (const key of ui.rail.top) {
    const button = mountRoot.querySelector(`.nav [data-rail="${key}"]`);
    if (button) {
      topBar.append(button);
    }
  }
  for (const key of ui.rail.bottom) {
    const button = mountRoot.querySelector(`.nav [data-rail="${key}"]`);
    if (button) {
      bottomBar.append(button);
    }
  }
  for (const key of RAIL_KEYS) {
    const button = mountRoot.querySelector(`.nav [data-rail="${key}"]`);
    if (!button) {
      continue;
    }
    button.hidden = key === "settings" ? false : !featureVisible(key);
  }
}

/* The current section is hidden. The first visible section in the top group that owns
   a nav takes over. Grok, GitHub, Activity, and Update do not. Settings always can. */
function fallThroughNav() {
  if (!mountRoot || featureVisible(navSection)) {
    return;
  }
  const top = ui.rail && Array.isArray(ui.rail.top) ? ui.rail.top : [];
  const next = top.find((key) => featureVisible(key) && sections[key] && sections[key].nav) || "settings";
  showNav(next);
}

function clearChords() {
  for (const stop of chordStops.values()) {
    stop();
  }
  chordStops = new Map();
}

function applyChords() {
  if (!keys) {
    return;
  }
  for (const key of RAIL_KEYS) {
    const prev = chordStops.get(key);
    if (prev) {
      prev();
      chordStops.delete(key);
    }
    const digit = ui.chords && ui.chords[key];
    if (typeof digit !== "string" || !/^[1-9]$/.test(digit)) {
      continue;
    }
    const stop = keys.register({
      id: `section.${key}`,
      scope: "global",
      chords: [[`Ctrl+${digit}`]],
      run() {
        if (!featureVisible(key)) {
          return;
        }
        if (key === "update") {
          toggleUpdate();
          return;
        }
        showNav(key);
        openHome(key);
        if (tabCtl) {
          tabCtl.focusActive();
        }
      },
    });
    chordStops.set(key, stop);
  }
}

function onFeatureToggle(key, on) {
  if (!FEATURE_KEYS.includes(key)) {
    return;
  }
  ui.features[key] = on === true;
  placeRailButtons();
  fallThroughNav();
  if (!on && sections[key]) {
    hideFeatureHome(key);
  }
  persistLayout();
}

function onFeatureChord(key, digit) {
  if (!RAIL_KEYS.includes(key) || !ui.chords) {
    return;
  }
  const next = typeof digit === "string" && /^[1-9]$/.test(digit) ? digit : "";
  if (next !== "") {
    for (const other of RAIL_KEYS) {
      if (other !== key && ui.chords[other] === next) {
        ui.chords[other] = "";
      }
    }
  }
  ui.chords[key] = next;
  if (featureEditor) {
    featureEditor.paint(ui.features, ui.chords);
  }
  applyChords();
  persistLayout();
}

function onFeatureArrange(next) {
  ui.rail = railOf({ ...ui.rail, top: next.top, bottom: next.bottom });
  ui.chords = chordsOf(ui.chords, ui.rail);
  placeRailButtons();
  if (featureEditor && !featureEditor.dragging()) {
    featureEditor.arrange(ui.rail);
    featureEditor.paint(ui.features, ui.chords);
  }
  applyChords();
  persistLayout();
}

function ensureRailDrag() {
  if (railDrag || !mountRoot) {
    return;
  }
  railDrag = createRailDrag(mountRoot, {
    rail() {
      return ui.rail;
    },
    onArrange: onFeatureArrange,
  });
}

function ensureFeatureEditor() {
  if (featureEditor || !mountRoot) {
    return;
  }
  const list = mountRoot.querySelector("#feature-list");
  if (!list) {
    return;
  }
  featureEditor = createFeatureEditor(list, {
    onToggle: onFeatureToggle,
    onChord: onFeatureChord,
    onArrange: onFeatureArrange,
  });
}

function paintFeatures() {
  if (!mountRoot) {
    return;
  }
  ensureFeatureEditor();
  ensureRailDrag();
  placeRailButtons();
  if (featureEditor && !featureEditor.dragging()) {
    featureEditor.arrange(ui.rail);
    featureEditor.paint(ui.features, ui.chords);
  }
  applyChords();
  fallThroughNav();
}

function hideFeatureHome(section) {
  if (!tabCtl) {
    return;
  }
  const id = `${section}:home`;
  const active = tabCtl.list().find((item) => item.active);
  const stay = !active || active.id !== id;
  void tabCtl.closeTab(id, { stay });
}

/* A hidden top section's home is not offered again. Leave it out before planning
   so the restore notice is reserved for a tab whose item is actually gone. */
function sessionForLaunch(saved) {
  if (!saved || !Array.isArray(saved.tabs)) {
    return saved;
  }
  const tabs = saved.tabs.filter((item) => {
    if (!item || item.kind !== "home") {
      return true;
    }
    return featureVisible(item.section);
  });
  if (tabs.length === saved.tabs.length) {
    return saved;
  }
  return { ...saved, tabs };
}

let tabCtl = null;
let keys = null;
let featureEditor = null;
let railDrag = null;
let chordStops = new Map();
let testSerial = 0;
let navSection = "mail";
let logView = null;
let activityView = null;
let helpView = null;
let helpBack = createBackTracker();
let accountsView = null;
let logSettings = null;
let limitsSettings = null;
let mailSettings = null;
let formatsSettings = null;
let unlistenFormats = null;
let profileTimeFormat = "24h";
let profileZone = "";
let logTimeZone = "America/Vancouver";
let mailTree = null;
let mailNav = null;
let mailList = null;
let syncing = 0;
let syncTurn = null;
let syncIds = [];
let stopRequested = false;
let updatePie = null;
const cancelSent = new Set();
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
  forwarder?.navigate(`activity view ${name === "activity" ? "activity" : "log"}`);
  ui.activity.view = name === "activity" ? "activity" : "log";
  paintActivity();
  persistLayout();
  syncLog();
}

function bindNavAffordances() {
  for (const button of mountRoot.querySelectorAll(".nav-buttons button")) {
    widgets.tooltips.bind(button, button.getAttribute("aria-label") || "");
  }
  const addHost = mountRoot.querySelector("#bots-add-host");
  if (addHost) {
    widgets.tooltips.bind(addHost, "A bot cannot be connected yet.");
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

function onHelpJump(event) {
  const button = event.target instanceof Element ? event.target.closest("[data-help-page]") : null;
  if (!button || !mountRoot || !mountRoot.contains(button) || !featureVisible("help") || !helpView) {
    return;
  }
  helpView.open(button.dataset.helpPage || "", button.dataset.helpSection || "");
  showNav("help");
  openHome("help");
}

/* D-10 (D-Q18): Back on the Help toolbar. */
function paintHelpBack() {
  if (helpView && tabCtl) {
    helpView.setBackEnabled(helpBack.target(tabCtl.list()) !== null);
  }
}

function helpGoBack() {
  if (!tabCtl) {
    return;
  }
  const target = helpBack.target(tabCtl.list());
  if (!target) {
    paintHelpBack();
    return;
  }
  showNav(target.section);
  tabCtl.activate(target.id);
}

function showNav(section) {
  if (!mountRoot) {
    return;
  }
  const meta = sections[section];
  if (!meta || !featureVisible(section)) {
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
  if (navSection === "mail" && mailNav) {
    void mailNav.start();
  }
}

function paintSessionRestore() {
  const box = mountRoot && mountRoot.querySelector("#session-restore");
  if (box) {
    box.checked = !ui.session || ui.session.restore !== false;
  }
  const max = mountRoot && mountRoot.querySelector("#session-max");
  if (max) {
    max.value = String(clampMaxTabs(ui.session && ui.session.maxTabs));
  }
}

function applyHomeContext(entry) {
  if (!mountRoot || !entry || entry.kind !== "home") {
    return;
  }
  const ctx = entry.context;
  if (entry.section === "settings" && ctx && typeof ctx.heading === "string") {
    const buttons = [...mountRoot.querySelectorAll("[data-heading]")];
    const button = buttons.find((item) => item.dataset.heading === ctx.heading);
    if (button) {
      for (const other of buttons) {
        if (other === button) {
          other.setAttribute("aria-current", "true");
        } else {
          other.removeAttribute("aria-current");
        }
      }
      for (const pane of mountRoot.querySelectorAll("[data-settings]")) {
        pane.hidden = pane.dataset.settings !== ctx.heading;
      }
      if (ctx.heading === "log" && logSettings) {
        logSettings.refresh();
      }
      if (ctx.heading === "limits" && limitsSettings) {
        limitsSettings.refresh();
      }
      if (ctx.heading === "mail" && mailSettings) {
        mailSettings.refresh();
      }
      if (ctx.heading === "formats" && formatsSettings) {
        formatsSettings.refresh();
      }
    }
  }
  if (entry.section === "mail") {
    const mailboxId = ctx && typeof ctx.mailboxId === "string" ? ctx.mailboxId : "";
    if (mailboxId !== "") {
      for (const button of mountRoot.querySelectorAll('[data-nav="mail"] .tree [role="treeitem"]')) {
        if (button.dataset.nodeId === mailboxId) {
          button.setAttribute("aria-current", "true");
        } else {
          button.removeAttribute("aria-current");
        }
      }
    }
    if (mailList) {
      void mailList.open(mailboxId);
    }
  }
  if (entry.section === "contacts" && ctx && typeof ctx.letter === "string") {
    for (const button of mountRoot.querySelectorAll("#letters-nav button")) {
      button.setAttribute("aria-pressed", button.dataset.letter === ctx.letter ? "true" : "false");
    }
  }
}

async function openInitialTabs() {
  if (!tabCtl) {
    return;
  }
  const saved = sessionForLaunch(ui.session);
  launching = true;
  restoring = true;
  tabCtl.restore([]);
  restoring = false;
  let marked = true;
  try {
    marked = await markSessionOpen();
    if (!tabCtl || !mountRoot) {
      return;
    }
    const plan = planRestore(saved, marked ? (tab) => tabCtl.resolves(tab) : undefined);
    const use = marked ? plan : {
      mode: "quick",
      tabs: [],
      dropped: [],
      unexpected: plan.unexpected === true,
      limited: 0,
    };
    restoring = true;
    launching = false;
    let dropped = [];
    try {
      if (use.mode === "first") {
        if (featureVisible("mail")) {
          showNav("mail");
          openHome("mail");
        }
      } else if (use.mode === "quick") {
        dropped = tabCtl.restore([]);
      } else if (use.mode === "empty") {
        if (featureVisible("mail")) {
          showNav("mail");
        }
        dropped = tabCtl.restore([]);
      } else {
        dropped = tabCtl.restore(use.tabs);
        const active = tabCtl.list().find((item) => item.active);
        if (active && active.kind === "home" && featureVisible(active.section)) {
          showNav(active.section);
        } else if (featureVisible("mail")) {
          showNav("mail");
        } else {
          navSection = "mail";
          fallThroughNav();
        }
        applyHomeContext(active);
      }
    } finally {
      restoring = false;
    }
    if (use.unexpected && widgets) {
      widgets.toaster.toast("Unexpected shutdown, performing quick start.");
    }
    if (use.limited > 0 && widgets) {
      const max = clampMaxTabs(saved && saved.maxTabs);
      widgets.toaster.toast(`Max tabs is ${max}. Some tabs stayed closed.`);
    }
    const gone = [...use.dropped, ...dropped];
    if (widgets) {
      for (const item of gone) {
        const title = item && typeof item.title === "string" && item.title !== "" ? item.title : "Tab";
        widgets.toaster.toast(`${title} could not be restored.`);
      }
    }
    noteSession();
  } finally {
    launching = false;
    restoring = false;
  }
}

function openHome(section, context) {
  if (!tabCtl || launching) {
    return null;
  }
  forwarder?.navigate(`home ${section}`);
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
  const sessionRestore = mountRoot.querySelector("#session-restore");
  if (sessionRestore) {
    sessionRestore.addEventListener("change", () => {
      ui.session.restore = sessionRestore.checked === true;
      noteSession();
      persistLayout();
    });
  }
  const sessionMax = mountRoot.querySelector("#session-max");
  if (sessionMax) {
    sessionMax.addEventListener("change", () => {
      const next = clampMaxTabs(sessionMax.value);
      sessionMax.value = String(next);
      ui.session.maxTabs = next;
      noteSession();
      persistLayout();
    });
  }
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
      if (button.dataset.heading === "log" && logSettings) {
        logSettings.refresh();
      }
      if (button.dataset.heading === "limits" && limitsSettings) {
        limitsSettings.refresh();
      }
      if (button.dataset.heading === "mail" && mailSettings) {
        mailSettings.refresh();
      }
      if (button.dataset.heading === "formats" && formatsSettings) {
        formatsSettings.refresh();
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

function syncPayload(payload) {
  if (typeof payload === "number") {
    return { count: payload > 0 ? payload : 0, ids: [], turn: null };
  }
  const count = payload && Number.isFinite(payload.count) && payload.count > 0 ? payload.count : 0;
  const ids = payload && Array.isArray(payload.ids)
    ? payload.ids.filter((id) => typeof id === "string" && id !== "")
    : [];
  const turn = payload && Number.isFinite(payload.turn) ? payload.turn : null;
  return { count, ids, turn };
}

function cancelRunningSync() {
  if (!api) {
    return;
  }
  for (const id of syncIds) {
    if (cancelSent.has(id)) {
      continue;
    }
    cancelSent.add(id);
    api.activity.cancel(id).catch((err) => {
      if (err && err.code === "not_found") {
        return;
      }
      showBanner(messageOf(err));
    });
  }
}

function toggleUpdate() {
  if (!api || !mountRoot) {
    return;
  }
  const pieOn = updatePie && updatePie.shown();
  if (syncing > 0 || pieOn) {
    stopRequested = true;
    if (updatePie) {
      updatePie.hide();
    }
    cancelRunningSync();
    paintUpdate();
    return;
  }
  stopRequested = false;
  cancelSent.clear();
  if (updatePie) {
    updatePie.start();
  }
  api.sync.now({}).catch((err) => {
    if (!stopRequested && updatePie) {
      updatePie.hide();
    }
    showBanner(messageOf(err));
  });
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
  const pieOn = updatePie && updatePie.shown();
  const pct = n > 0 && Number.isFinite(syncTurn) ? `${Math.round(syncTurn)}%` : "";
  const label = n === 0 ? "Update" : (pct ? `${n} syncing, ${pct}` : `${n} syncing`);
  button.setAttribute("aria-label", label);
  if (widgets) {
    const tip = pieOn || n > 0
      ? (n > 0 ? `${label}. Click again to stop.` : "Click again to stop.")
      : label;
    widgets.tooltips.bind(button, tip);
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
  const pie = button.querySelector(".update-pie");
  updatePie = pie ? createUpdatePie(pie, { reducedMotion, onChange: paintUpdate }) : null;
  button.addEventListener("click", () => {
    toggleUpdate();
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
    if (!tabCtl || launching) {
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
  if (api && typeof api.stop === "function") {
    api.stop();
  }
  if (unforward) {
    unforward();
    unforward = null;
  }
  forwarder = null;
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
    window.removeEventListener("beforeunload", onPageHide);
    if (current.isolationStop) {
      current.isolationStop();
    }
  }
  if (saveTimer) {
    window.clearTimeout(saveTimer);
    saveTimer = 0;
  }
  if (sessionTimer) {
    window.clearTimeout(sessionTimer);
    sessionTimer = 0;
  }
  sessionDirty = false;
  sessionSnapshot = "";
  quitNoted = false;
  restoring = false;
  launching = false;
  if (host) {
    host.onQuit = null;
  }
  if (unlistenFormats) {
    unlistenFormats();
    unlistenFormats = null;
  }
  if (formatsSettings) {
    formatsSettings.destroy();
    formatsSettings = null;
  }
  if (logView) {
    logView.destroy();
    logView = null;
  }
  if (helpView) {
    helpView.destroy();
    helpView = null;
    helpBack = createBackTracker();
  }
  if (accountsView) {
    accountsView.destroy();
    accountsView = null;
  }
  if (logSettings) {
    logSettings.destroy();
    logSettings = null;
  }
  if (limitsSettings) {
    limitsSettings.destroy();
    limitsSettings = null;
  }
  if (mailSettings) {
    mailSettings.destroy();
    mailSettings = null;
  }
  if (mailList) {
    mailList.stop();
    mailList = null;
  }
  if (mailNav) {
    mailNav.stop();
    mailNav = null;
    mailTree = null;
  } else if (mailTree) {
    mailTree.destroy();
    mailTree = null;
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
  syncTurn = null;
  syncIds = [];
  stopRequested = false;
  cancelSent.clear();
  if (updatePie) {
    updatePie.destroy();
    updatePie = null;
  }
  if (mountRoot && onZoomWheel) {
    mountRoot.removeEventListener("wheel", onZoomWheel, true);
  }
  onZoomWheel = null;
  if (mountRoot && onHelpJump) {
    mountRoot.removeEventListener("click", onHelpJump);
  }
  wheelRemainder = 0;
  if (featureEditor) {
    featureEditor.destroy();
    featureEditor = null;
  }
  if (railDrag) {
    railDrag.destroy();
    railDrag = null;
  }
  clearChords();
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
  host.onQuit = () => {
    quitNoted = true;
    return finishSession("clean");
  };
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
    offer(section) {
      return featureVisible(section);
    },
    onReveal(section) {
      if (!featureVisible(section)) {
        return;
      }
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
    maxTabs() {
      return clampMaxTabs(ui.session && ui.session.maxTabs);
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
  tabCtl.subscribe((change) => {
    syncLog();
    if (change.type === "limit" && widgets) {
      widgets.toaster.toast(`Max tabs is ${change.max}.`);
    }
    if (change.type === "activate" || change.type === "open" || change.type === "context") {
      const active = tabCtl.list().find((item) => item.active);
      applyHomeContext(active);
      helpBack.note(active);
    }
    paintHelpBack();
    if (!restoring && change.type !== "limit") {
      noteSession();
    }
  });
  bindNavAffordances();
  window.addEventListener("pagehide", onPageHide);
  window.addEventListener("beforeunload", onPageHide);
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
  if (unlistenFormats) {
    unlistenFormats();
    unlistenFormats = null;
  }
  let timeZone = applyFormatPrefs(null);
  let logLevel = "info";
  if (banner.hidden) {
    try {
      const prefs = await api.prefs.get();
      if (!alive(mine)) {
        return;
      }
      timeZone = applyFormatPrefs(prefs);
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
    onSync(payload) {
      const snap = syncPayload(payload);
      const prev = syncing;
      syncing = snap.count;
      syncTurn = snap.turn;
      syncIds = snap.ids;
      if (snap.count === 0) {
        if (prev > 0 && !stopRequested && updatePie) {
          updatePie.finish();
        }
        stopRequested = false;
        cancelSent.clear();
      } else if (stopRequested) {
        if (updatePie) {
          updatePie.hide();
        }
        cancelRunningSync();
      } else if (updatePie) {
        updatePie.follow(snap.turn);
      }
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
      if (!featureVisible(button.dataset.section)) {
        return;
      }
      showNav(button.dataset.section);
      openHome(button.dataset.section);
    });
  }
  if (!featureEditor) {
    paintFeatures();
  }
  bindNavResize();
  bindSplit("mail");
  bindSplit("contacts");
  bindSplit("files");
  bindSplit("help");
  bindSplit("accounts");
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

  const mailNavEl = mountRoot.querySelector('[data-nav="mail"]');
  if (mailNavEl) {
    const mailTreeContainer = mailNavEl.querySelector("#mail-tree");
    if (mailTreeContainer) {
      /* defer: the tree starts after openInitialTabs, and only when this
         pane is showing. Contacts-only startup skips mailbox.list. */
      mailNav = createMailNav(api, mailTreeContainer, {
        id: "mail",
        label: "Mail",
        defer: true,
        state: ui.trees && ui.trees.mail,
        regions: ["leading", "label", "status", "count", "hover"],
        onPersist(treeId, patch) {
          ui.trees = ui.trees || {};
          ui.trees[treeId] = { ...ui.trees[treeId], ...patch };
          if (treeId === "mail") {
            ui.trees[treeId].showFavorites = mailTree.viewState.showFavorites;
            ui.trees[treeId].showHeart = mailTree.viewState.showHeart;
            ui.trees[treeId].hideInfrequent = mailTree.viewState.hideInfrequent;
          }
          persistLayout();
        },
      });
      mailTree = mailNav.tree;
    }
  }
  function bindMailTree() {
    if (!mailTree || !mountRoot) {
      return;
    }
    const treeEl = mailTree.container;
    if (keys) {
      treeEl.addEventListener("focusin", () => {
        keys.setScope("tree");
      });
      treeEl.addEventListener("focusout", (event) => {
        const next = event.relatedTarget;
        if (!(next instanceof Node) || !treeEl.contains(next)) {
          keys.setScope("workspace");
        }
      });
    }
    treeEl.addEventListener("click", (event) => {
      const button = event.target instanceof Element ? event.target.closest('[role="treeitem"]') : null;
      if (!button || !treeEl.contains(button)) {
        return;
      }
      const nodeId = button.dataset.nodeId;
      if (!nodeId) {
        return;
      }
      if (event.target.closest("[data-region='trailing']")) {
        if (mailTree.isExpanded(nodeId)) {
          mailTree.collapseBranch(nodeId);
        } else {
          mailTree.expand(nodeId);
        }
        return;
      }
      /* The arrow opens the branch. It has pointer-events: none so a drag
         cannot grab the icon, and the hit is the row underneath. */
      const disclosure = button.querySelector(".tree-row-disclosure");
      if (disclosure) {
        const box = disclosure.getBoundingClientRect();
        const onArrow = event.clientX >= box.left && event.clientX <= box.right
          && event.clientY >= box.top && event.clientY <= box.bottom;
        if (onArrow) {
          if (mailTree.isExpanded(nodeId)) {
            mailTree.collapse(nodeId);
          } else {
            mailTree.expand(nodeId);
          }
          return;
        }
      }
      const node = mailTree.getNode(nodeId);
      mailTree.select(nodeId);
      if (node && node.kind === "account") {
        if (mailTree.isExpanded(nodeId)) {
          mailTree.collapse(nodeId);
        } else {
          mailTree.expand(nodeId);
        }
        return;
      }
      showNav("mail");
      openHome("mail", { mailboxId: nodeId });
    });

    function rememberHiddenRoots() {
      ui.trees = ui.trees || {};
      ui.trees.mail = { ...(ui.trees.mail || {}), hiddenRoots: [...mailTree.viewState.hiddenRoots] };
      persistLayout();
    }

    function mailTreeMenu(nodeId) {
      const node = mailTree.getNode(nodeId);
      if (!node) {
        return [];
      }
      const items = [
        {
          label: "Collapse this branch",
          onSelect() {
            mailTree.collapseBranch(nodeId);
          },
        },
      ];
      if (node.kind === "account") {
        items.unshift(
          { label: "Account settings", onSelect() { openHome("settings", { heading: "accounts" }); } },
          {
            label: "Hide account",
            onSelect() {
              mailTree.viewState.hideRoot(nodeId);
              mailTree.render();
              rememberHiddenRoots();
            },
          },
        );
      }
      if (node.kind === "mailbox" || node.kind === "folder") {
        const isFav = mailTree.viewState.isFavorite(nodeId);
        items.push({
          label: isFav ? "Unmark favorite" : "Mark favorite",
          onSelect() {
            mailTree.viewState.toggleFavorite(nodeId);
            mailTree.patchNode(nodeId, { favorite: !isFav });
          },
        });
        const isInfreq = mailTree.viewState.isInfrequent(nodeId);
        items.push({
          label: isInfreq ? "Mark frequent" : "Mark infrequent (hide)",
          onSelect() {
            mailTree.viewState.toggleInfrequent(nodeId);
            mailTree.viewState.hideRoot(nodeId);
            mailTree.render();
            rememberHiddenRoots();
          },
        });
      }
      items.push({ label: "Properties", onSelect() {} });
      return items;
    }

    if (widgets && widgets.menus) {
      mailTree.setRowMenu((nodeId) => mailTreeMenu(nodeId));
      widgets.menus.bind(treeEl, (origin) => {
        const row = origin instanceof Element ? origin.closest("[role='treeitem']") : null;
        if (!row || !treeEl.contains(row)) {
          return [];
        }
        const nodeId = row.dataset.nodeId;
        return nodeId ? mailTreeMenu(nodeId) : [];
      });
    }

    const toolbar = mountRoot.querySelector("#mail-tree-toolbar");
    if (toolbar) {
      toolbar.addEventListener("click", (event) => {
        const button = event.target instanceof Element ? event.target.closest("[data-tree-filter]") : null;
        if (!button || !toolbar.contains(button)) {
          return;
        }
        const filter = button.dataset.treeFilter;
        const pressed = button.getAttribute("aria-pressed") === "true";
        button.setAttribute("aria-pressed", !pressed ? "true" : "false");
        const on = !pressed;
        if (filter === "star") {
          ui.trees = ui.trees || {};
          ui.trees.mail = ui.trees.mail || {};
          ui.trees.mail.showFavorites = on;
          mailTree.viewState.setShowFavorites(on);
        } else if (filter === "heart") {
          ui.trees = ui.trees || {};
          ui.trees.mail = ui.trees.mail || {};
          ui.trees.mail.showHeart = on;
          mailTree.viewState.setShowHeart(on);
        } else if (filter === "ghost") {
          ui.trees = ui.trees || {};
          ui.trees.mail = ui.trees.mail || {};
          ui.trees.mail.hideInfrequent = on;
          mailTree.viewState.setHideInfrequent(on);
        }
        mailTree.render();
        persistLayout();
      });
    }

    if (keys) {
      keys.register({
        id: "tree.expand",
        scope: "tree",
        chords: [["+"], ["ArrowRight"]],
        run() {
          const nodeId = treeEl.querySelector('[role="treeitem"][aria-current="true"]')?.dataset.nodeId;
          if (nodeId) {
            mailTree.expand(nodeId);
          }
        },
      });
      keys.register({
        id: "tree.collapse",
        scope: "tree",
        chords: [["-"], ["ArrowLeft"]],
        run() {
          const nodeId = treeEl.querySelector('[role="treeitem"][aria-current="true"]')?.dataset.nodeId;
          if (nodeId) {
            if (mailTree.isExpanded(nodeId)) {
              mailTree.collapse(nodeId);
            } else {
              const parent = mailTree.getNode(nodeId)?.parentId;
              if (parent) {
                mailTree.collapseBranch(parent);
                const parentButton = treeEl.querySelector(`[data-node-id="${parent}"]`);
                if (parentButton) {
                  parentButton.focus();
                }
              }
            }
          }
        },
      });
      keys.register({
        id: "tree.toggle",
        scope: "tree",
        chords: [["Enter"], [" "]],
        run() {
          const nodeId = treeEl.querySelector('[role="treeitem"][aria-current="true"]')?.dataset.nodeId;
          if (nodeId) {
            const isOpen = mailTree.isExpanded(nodeId);
            if (isOpen) {
              mailTree.collapse(nodeId);
            } else {
              mailTree.expand(nodeId);
            }
          }
        },
      });
      keys.register({
        id: "tree.expandSiblings",
        scope: "tree",
        chords: [["*"]],
        run() {
          const nodeId = treeEl.querySelector('[role="treeitem"][aria-current="true"]')?.dataset.nodeId;
          if (!nodeId) {
            return;
          }
          const node = mailTree.getNode(nodeId);
          if (!node || !node.parentId) {
            return;
          }
          for (const child of mailTree.model.children(node.parentId)) {
            mailTree.expand(child.id);
          }
        },
      });
      keys.register({
        id: "tree.selectParent",
        scope: "tree",
        chords: [["ArrowUp"]],
        run() {
          const nodeId = treeEl.querySelector('[role="treeitem"][aria-current="true"]')?.dataset.nodeId;
          if (nodeId) {
            const node = mailTree.getNode(nodeId);
            if (node && node.parentId && !mailTree.getNode(node.parentId)?.parentId) {
              mailTree.select(node.parentId);
              const btn = treeEl.querySelector(`[data-node-id="${node.parentId}"]`);
              if (btn) {
                btn.focus();
              }
            } else if (node && node.parentId) {
              mailTree.select(node.parentId);
              const btn = treeEl.querySelector(`[data-node-id="${node.parentId}"]`);
              if (btn) {
                btn.focus();
              }
            }
          }
        },
      });
      keys.register({
        id: "tree.selectChild",
        scope: "tree",
        chords: [["ArrowDown"]],
        run() {
          const nodeId = treeEl.querySelector('[role="treeitem"][aria-current="true"]')?.dataset.nodeId;
          if (nodeId) {
            const node = mailTree.getNode(nodeId);
            if (node && node.hasChildren && mailTree.isExpanded(nodeId)) {
              const firstChild = mailTree.model.children(nodeId)[0];
              if (firstChild) {
                mailTree.select(firstChild.id);
                const btn = treeEl.querySelector(`[data-node-id="${firstChild.id}"]`);
                if (btn) {
                  btn.focus();
                }
              }
            }
          }
        },
      });
    }
  }

  forwarder = createForwarder(log);
  unforward = forwardEvents(mountRoot, forwarder);
  bindMailTree();
  helpView = createHelpView(mountRoot, undefined, { onBack: helpGoBack });
  paintHelpBack();
  mountRoot.addEventListener("click", onHelpJump);
  accountsView = createAccountsView(api, mountRoot, {
    host,
    tableHidden(id) {
      const entry = ui.tables && ui.tables[id];
      return entry && Array.isArray(entry.hidden) ? entry.hidden : [];
    },
    saveTableHidden(id, hidden) {
      ui.tables = { ...(ui.tables || {}), [id]: { ...((ui.tables && ui.tables[id]) || {}), hidden } };
      persistLayout();
    },
    tableWidths(id) {
      const entry = ui.tables && ui.tables[id];
      return entry && entry.widths && typeof entry.widths === "object" ? entry.widths : {};
    },
    saveTableWidths(id, widths) {
      ui.tables = { ...(ui.tables || {}), [id]: { ...((ui.tables && ui.tables[id]) || {}), widths } };
      persistLayout();
    },
    timeZone: () => logTimeZone,
    confirm(text, name) {
      if (!widgets) {
        return Promise.resolve(false);
      }
      return widgets.dialogs.confirm({ title: `Delete ${name}?`, text, okLabel: "Delete", danger: true });
    },
  });
  accountsView.setZoom(ui.zoom.shell * ui.zoom.accountsTable, ui.zoom.accountsTable);
  logSettings = createLogSettings(api, mountRoot, {
    timeZone: () => logTimeZone,
    onError(text) {
      showBanner(text);
    },
  });
  limitsSettings = createLimitsSettings(api, mountRoot, {
    timeZone: () => logTimeZone,
    onError(text) {
      showBanner(text);
    },
  });
  limitsSettings.refresh();
  mailSettings = createMailSettings(api, mountRoot, {
    onError(text) {
      showBanner(text);
    },
  });
  mailSettings.refresh();
  formatsSettings = createFormatsSettings(api, mountRoot, {
    timeFormat: () => profileTimeFormat,
    getUi: () => ui,
    onSave(next) {
      ui.formats = next.formats;
      persistLayout();
      const previousZone = profileZone;
      const zonePromise = next.timezone !== profileZone
        ? api.prefs.set({ timezone: next.timezone }).then((prefs) => {
          profileZone = next.timezone;
          if (prefs && (prefs.timeFormat === "12h" || prefs.timeFormat === "24h")) {
            profileTimeFormat = prefs.timeFormat;
          }
        })
        : Promise.resolve();
      return zonePromise.then(() => {
        applyFormatPrefs({ timezone: profileZone, timeFormat: profileTimeFormat });
      }, (err) => {
        applyFormatPrefs({ timezone: previousZone, timeFormat: profileTimeFormat });
        throw err;
      });
    },
    onError(text) {
      showBanner(text);
    },
  });
  formatsSettings.refresh();
  if (unlistenFormats) {
    unlistenFormats();
  }
  unlistenFormats = subscribeFormats(repaintFormatSurfaces);
  if (mountRoot.querySelector("[data-settings='accounts']")) {
    accountsView.refresh();
  }
  const mailListEl = mountRoot.querySelector("#mail-list");
  if (mailListEl) {
    mailList = createMailList(api, mailListEl);
  }
  await openInitialTabs();
  const mailPane = mountRoot && mountRoot.querySelector('[data-nav="mail"]');
  if (mailNav && mailPane && !mailPane.hidden) {
    await mailNav.start();
    if (alive(mine)) {
      try {
        void Promise.resolve(log("ui", "debug", "mail nav ready")).catch(() => {});
      } catch {
        /* The mark must not stop the shell. */
      }
    }
  }
  if (activityView) {
    await activityView.ready();
  }
}
