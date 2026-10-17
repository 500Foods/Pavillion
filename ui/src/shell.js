import { installIsolationTest } from "./common/isolation.js";
import { createLogView } from "./log/view.js";
import { DEFAULT_UI, ZOOM_KEYS, clampUi, snapZoom } from "./prefs.js";
import { createZoomHistory } from "./zoom-history.js";

let api = null;
let host = null;
let mountRoot = null;
let session = null;

function log(subsystem, level, message) {
  return api.log.client({ subsystem, level, message });
}

let ui = clampUi({});
let zoomHistory = createZoomHistory(ZOOM_KEYS);
let zoomBefore = null;

/* shell is the master scale on the root; each pane multiplies on top of it.
   The zoom action bar is excluded so a large Button bars value cannot push it off. */
const zoomPanes = {
  navButtons: ".button-bar:not(.zoom-actions)",
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
}

function zoomAtDefault() {
  return ZOOM_KEYS.every((key) => ui.zoom[key] === DEFAULT_UI.zoom[key]);
}

function paintZoomActions() {
  const undo = mountRoot.querySelector("#zoom-undo");
  const redo = mountRoot.querySelector("#zoom-redo");
  const defaults = mountRoot.querySelector("#zoom-default");
  if (undo) {
    undo.disabled = !zoomHistory.canUndo;
  }
  if (redo) {
    redo.disabled = !zoomHistory.canRedo;
  }
  if (defaults) {
    defaults.disabled = zoomAtDefault();
  }
}

function rememberZoom() {
  if (zoomBefore) {
    return;
  }
  zoomBefore = {};
  for (const key of ZOOM_KEYS) {
    zoomBefore[key] = ui.zoom[key];
  }
}

function commitZoom() {
  if (!zoomBefore) {
    return;
  }
  const before = zoomBefore;
  zoomBefore = null;
  zoomHistory.push(before, ui.zoom);
  paintZoomActions();
}

function applyZoomState(next) {
  for (const key of ZOOM_KEYS) {
    ui.zoom[key] = next[key];
  }
  paintZoomSliders();
  applyZoom();
  paintZoomActions();
  persistLayout();
}

function applyFonts() {
  mountRoot.style.setProperty("--pav-font", ui.fonts.body);
  mountRoot.style.setProperty("--pav-font-mono", ui.fonts.mono);
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

function applyIconOverride(key) {
  const navIcon = mountRoot.querySelector(`.nav [data-section="${key}"] .icon`);
  if (!navIcon) {
    return;
  }
  const override = ui.icons[key];
  if (override) {
    if (override.trim().startsWith('<')) {
      navIcon.innerHTML = override;
    } else {
      navIcon.className = `icon ${override}`;
    }
  } else {
    navIcon.className = navIcon.dataset.iconDefinition || navIcon.className;
  }
}

function refreshIconPreviews() {
  for (const input of mountRoot.querySelectorAll("#icon-list .icon-input")) {
    const key = input.dataset.iconKey;
    const preview = mountRoot.querySelector(`#icon-list .icon[data-icon-key="${key}"]`);
    if (preview) {
      preview.innerHTML = "";
      const value = input.value || input.defaultValue;
      if (value.trim().startsWith('<')) {
        preview.innerHTML = value;
      } else {
        preview.className = `icon ${value}`;
      }
    }
  }
}

let canSave = false;

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

const SAVE_MS = 300;
let saveTimer = 0;
let dirty = false;

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
    return Promise.resolve();
  }
  dirty = false;
  return api.prefs.ui.set(clampUi(ui)).catch((err) => {
    showBanner(messageOf(err));
  });
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
  const kitInput = mountRoot.querySelector("#fa-kit-html");
  if (kitInput) {
    kitInput.value = ui.fontawesome_kit;
  }
  paintZoomSliders();
  applyZoom();
  paintZoomActions();
  applyFonts();
  showFont(mountRoot.querySelector("#body-font"), ui.fonts.body);
  showFont(mountRoot.querySelector("#mono-font"), ui.fonts.mono);
  for (const key of Object.keys(ui.icons)) {
    applyIconOverride(key);
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

const tabs = new Map();
let navSection = "mail";
let logView = null;

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function letters(root, pressed) {
  for (let code = 65; code <= 90; code += 1) {
    const letter = String.fromCharCode(code);
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = letter;
    button.setAttribute("aria-pressed", pressed && letter === "A" ? "true" : "false");
    button.addEventListener("click", () => {
      for (const other of root.querySelectorAll("button")) {
        other.setAttribute("aria-pressed", other === button ? "true" : "false");
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

function openSection(id) {
  const section = sections[id];
  if (!section) {
    return;
  }
  if (section.nav) {
    navSection = id;
  }
  for (const button of mountRoot.querySelectorAll("[data-section]")) {
    button.setAttribute("aria-pressed", button.dataset.section === id ? "true" : "false");
  }
  for (const body of mountRoot.querySelectorAll("[data-nav]")) {
    body.hidden = body.dataset.nav !== navSection;
  }
  if (!tabs.has(id)) {
    const tab = document.createElement("button");
    tab.type = "button";
    tab.role = "tab";
    tab.textContent = section.title;
    tab.addEventListener("click", () => openSection(id));
    tabs.set(id, tab);
    mountRoot.querySelector("#tablist").append(tab);
  }
  for (const [key, tab] of tabs) {
    tab.setAttribute("aria-selected", key === id ? "true" : "false");
  }
  for (const panel of mountRoot.querySelectorAll(".panel")) {
    panel.hidden = panel.id !== `panel-${id}`;
  }
  syncLog();
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
    input.addEventListener("input", () => {
      rememberZoom();
      show();
    });
    input.addEventListener("change", () => {
      rememberZoom();
      show();
      commitZoom();
      persistLayout();
    });
  }
  const defaults = mountRoot.querySelector("#zoom-default");
  const undo = mountRoot.querySelector("#zoom-undo");
  const redo = mountRoot.querySelector("#zoom-redo");
  if (defaults) {
    defaults.addEventListener("click", () => {
      zoomBefore = null;
      const before = {};
      const next = {};
      for (const key of ZOOM_KEYS) {
        before[key] = ui.zoom[key];
        next[key] = DEFAULT_UI.zoom[key];
      }
      if (!zoomHistory.push(before, next)) {
        return;
      }
      applyZoomState(next);
    });
  }
  if (undo) {
    undo.addEventListener("click", () => {
      zoomBefore = null;
      const next = zoomHistory.undo(ui.zoom);
      if (next) {
        applyZoomState(next);
      }
    });
  }
  if (redo) {
    redo.addEventListener("click", () => {
      zoomBefore = null;
      const next = zoomHistory.redo(ui.zoom);
      if (next) {
        applyZoomState(next);
      }
    });
  }
  paintZoomActions();
  for (const [id, key] of [["#body-font", "body"], ["#mono-font", "mono"]]) {
    const select = mountRoot.querySelector(id);
    if (!select) {
      continue;
    }
    showFont(select, ui.fonts[key]);
    select.addEventListener("change", () => {
      ui.fonts[key] = select.value;
      applyFonts();
      persistLayout();
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
    });
  }
  const iconForm = mountRoot.querySelector("#icon-form");
  if (iconForm) {
    const previewBtn = iconForm.querySelector("#icon-preview");
    const reloadBtn = iconForm.querySelector("#icon-reload");

    function setDirty(dirty) {
      if (previewBtn) {
        previewBtn.disabled = !dirty;
      }
      if (reloadBtn) {
        reloadBtn.disabled = !dirty;
      }
    }

    function updatePreviews() {
      for (const input of iconForm.querySelectorAll(".icon-input")) {
        const key = input.dataset.iconKey;
        const preview = mountRoot.querySelector(`#icon-list .icon[data-icon-key="${key}"]`);
        if (preview) {
          preview.innerHTML = "";
          const value = input.value || input.defaultValue;
          if (value.trim().startsWith('<')) {
            preview.innerHTML = value;
          } else {
            preview.className = `icon ${value}`;
          }
        }
      }
    }

    for (const input of iconForm.querySelectorAll(".icon-input")) {
      input.addEventListener("input", () => {
        const key = input.dataset.iconKey;
        ui.icons[key] = input.value;
        const preview = mountRoot.querySelector(`#icon-list .icon[data-icon-key="${key}"]`);
        if (preview) {
          preview.innerHTML = "";
          const value = input.value || input.defaultValue;
          if (value.trim().startsWith('<')) {
            preview.innerHTML = value;
          } else {
            preview.className = `icon ${value}`;
          }
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

function bindUpdate() {
  const button = mountRoot.querySelector("#update");
  button.addEventListener("click", () => {
    button.classList.add("is-busy");
    button.setAttribute("aria-busy", "true");
    const timer = window.setTimeout(() => {
      if (session) {
        session.updateTimer = 0;
      }
      button.classList.remove("is-busy");
      button.removeAttribute("aria-busy");
    }, 1600);
    if (session) {
      session.updateTimer = timer;
    }
  });
}

function alive(mine) {
  return session === mine;
}

export function stopShell() {
  const current = session;
  session = null;
  if (current) {
    window.removeEventListener("pagehide", onPageHide);
    if (current.updateTimer) {
      window.clearTimeout(current.updateTimer);
    }
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
  tabs.clear();
  navSection = "mail";
  canSave = false;
  dirty = false;
  zoomHistory = createZoomHistory(ZOOM_KEYS);
  zoomBefore = null;
  mountRoot = null;
  api = null;
  host = null;
  ui = clampUi({});
}

export async function startShell(surface, nextApi, nextHost, result) {
  stopShell();
  const mine = { updateTimer: 0, isolationStop: null };
  session = mine;
  mountRoot = surface;
  api = nextApi;
  host = nextHost || {};
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
      ui.activity.view = button.dataset.activityView === "activity" ? "activity" : "log";
      paintActivity();
      persistLayout();
      syncLog();
    });
  }
  letters(mountRoot.querySelector("#letters-nav"), true);
  letters(mountRoot.querySelector("#letters-main"), true);
  fillCalendar(new Date());
  for (const button of mountRoot.querySelectorAll("[data-section]")) {
    button.addEventListener("click", () => openSection(button.dataset.section));
  }
  bindNavResize();
  bindSplit("mail");
  bindSplit("contacts");
  bindSplit("files");
  bindSettings();
  bindUpdate();
  mine.isolationStop = installIsolationTest({
    api,
    root: mountRoot,
    enabled: result.debug === true || result.selftest === true,
    openMail() {
      openSection("mail");
    },
  });
  for (const button of mountRoot.querySelectorAll(".tree button")) {
    button.addEventListener("click", () => {
      for (const other of mountRoot.querySelectorAll(".tree button")) {
        other.removeAttribute("aria-current");
      }
      button.setAttribute("aria-current", "true");
    });
  }
  openSection("mail");
}
