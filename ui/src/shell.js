import { prefs, saveLayout, saveIcons, saveKit, log } from "./api.js";
import { reloadIcons } from "./main.js";

const ui = {
  nav: { width: 320, min: 200, max: 500 },
  mail: { split: 0.5, min: 0.25, max: 0.75 },
  contacts: { split: 0.5, min: 0.25, max: 0.75 },
  files: { split: 0.5, min: 0.25, max: 0.75 },
  zoom: { shell: 1, navButtons: 1, navBody: 1, mailList: 1, mailDetail: 1 },
};

const iconOverrides = {};
let fontawesomeKit = "45801522ef";

/* shell is the master scale on the root; each pane multiplies on top of it. */
const zoomPanes = {
  navButtons: ".nav-buttons",
  navBody: ".nav-body",
  mailList: "#panel-mail .pane-list",
  mailDetail: "#panel-mail .pane-detail",
};

function zoomClamp(value) {
  const scale = Number(value);
  return Number.isFinite(scale) ? clamp(scale, 0.05, 5) : 1;
}

function zoomLabel(scale) {
  return `${Math.round(scale * 100)}%`;
}

function applyZoom() {
  document.documentElement.style.fontSize = `${ui.zoom.shell * 100}%`;
  for (const [key, selector] of Object.entries(zoomPanes)) {
    for (const element of document.querySelectorAll(selector)) {
      const scale = ui.zoom.shell * ui.zoom[key];
      element.style.zoom = String(scale);
      for (const svg of element.querySelectorAll("svg")) {
        svg.style.width = `${1.15 * scale}em`;
        svg.style.height = `${1.15 * scale}em`;
      }
    }
  }
}

function applyIconOverride(key) {
  const navIcon = document.querySelector(`.nav [data-section="${key}"] .icon`);
  if (!navIcon) {
    return;
  }
  const override = iconOverrides[key];
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
  for (const input of document.querySelectorAll("#icon-list .icon-input")) {
    const key = input.dataset.iconKey;
    const preview = document.querySelector(`#icon-list .icon[data-icon-key="${key}"]`);
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

/* Surface JS errors in the core log so a missing DevTools doesn't leave us
   blind. Errors are logged as "ui" at PAV_ERROR. */
window.onerror = function (message, source, lineno, colno, error) {
  const text = error && error.stack
    ? String(error.stack)
    : `${message} (${source}:${lineno}:${colno})`;
  log("ui", 0, `onerror: ${text}`);
  return false;
};
window.onunhandledrejection = function (event) {
  const reason = event && event.reason
    ? String(event.reason.stack || event.reason)
    : "unhandled promise rejection";
  log("ui", 0, `onunhandledrejection: ${reason}`);
};

let canSave = false;

function messageOf(err) {
  if (err instanceof Error) {
    return err.message;
  }
  return String(err);
}

function showBanner(text) {
  const banner = document.querySelector("#banner");
  banner.hidden = false;
  banner.textContent = text;
}

function persistLayout() {
  if (!canSave) {
    return;
  }
  saveLayout({
    zoom: { ...ui.zoom },
    navWidth: ui.nav.width,
    mailSplit: ui.mail.split,
    contactsSplit: ui.contacts.split,
    filesSplit: ui.files.split,
  }).catch((err) => {
    showBanner(messageOf(err));
  });
}

async function loadPrefs() {
  const stored = await prefs();
  ui.nav = stored.ui.nav;
  ui.mail = stored.ui.mail;
  ui.contacts = stored.ui.contacts;
  ui.files = stored.ui.files;
  for (const key of Object.keys(ui.zoom)) {
    ui.zoom[key] = zoomClamp(stored.zoom[key]);
  }
  Object.assign(iconOverrides, stored.icon_overrides || {});
  fontawesomeKit = stored.fontawesome_kit || fontawesomeKit;
  const kitInput = document.querySelector("#fa-kit-html");
  if (kitInput) {
    kitInput.value = fontawesomeKit;
  }
  for (const label of document.querySelectorAll("[data-zoom]")) {
    const key = label.dataset.zoom;
    if (!(key in ui.zoom)) {
      continue;
    }
    label.querySelector("input").value = String(ui.zoom[key]);
    label.querySelector(".zoom-value").textContent = zoomLabel(ui.zoom[key]);
  }
  applyZoom();
  for (const key of Object.keys(iconOverrides)) {
    applyIconOverride(key);
  }
  for (const input of document.querySelectorAll("#icon-list .icon-input")) {
    const key = input.dataset.iconKey;
    if (iconOverrides[key]) {
      input.value = iconOverrides[key];
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
  const mini = document.querySelector("#mini-cal");
  const month = document.querySelector("#month");
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

function openSection(id) {
  const section = sections[id];
  if (section.nav) {
    navSection = id;
  }
  for (const button of document.querySelectorAll("[data-section]")) {
    button.setAttribute("aria-pressed", button.dataset.section === id ? "true" : "false");
  }
  for (const body of document.querySelectorAll("[data-nav]")) {
    body.hidden = body.dataset.nav !== navSection;
  }
  if (!tabs.has(id)) {
    const tab = document.createElement("button");
    tab.type = "button";
    tab.role = "tab";
    tab.textContent = section.title;
    tab.addEventListener("click", () => openSection(id));
    tabs.set(id, tab);
    document.querySelector("#tablist").append(tab);
  }
  for (const [key, tab] of tabs) {
    tab.setAttribute("aria-selected", key === id ? "true" : "false");
  }
  for (const panel of document.querySelectorAll(".panel")) {
    panel.hidden = panel.id !== `panel-${id}`;
  }
}

function bindNavResize() {
  const nav = document.querySelector("#nav");
  const handle = document.querySelector("#nav-resize");
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
  const handle = document.querySelector(`[data-split="${name}"]`);
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
  for (const label of document.querySelectorAll("[data-zoom]")) {
    const key = label.dataset.zoom;
    if (!(key in ui.zoom)) {
      continue;
    }
    const input = label.querySelector("input");
    const value = label.querySelector(".zoom-value");
    const show = () => {
      ui.zoom[key] = zoomClamp(input.value);
      value.textContent = zoomLabel(ui.zoom[key]);
      applyZoom();
    };
    input.addEventListener("input", show);
    input.addEventListener("change", () => {
      show();
      persistLayout();
    });
  }
  const bodyFont = document.querySelector("#body-font");
  if (bodyFont) {
    bodyFont.addEventListener("change", () => {
      document.documentElement.style.setProperty("--font", bodyFont.value);
    });
  }
  for (const button of document.querySelectorAll("[data-heading]")) {
    button.addEventListener("click", () => {
      for (const other of document.querySelectorAll("[data-heading]")) {
        other.removeAttribute("aria-current");
      }
      button.setAttribute("aria-current", "true");
      for (const pane of document.querySelectorAll("[data-settings]")) {
        pane.hidden = pane.dataset.settings !== button.dataset.heading;
      }
    });
  }
  const iconForm = document.querySelector("#icon-form");
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
        const preview = document.querySelector(`#icon-list .icon[data-icon-key="${key}"]`);
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
        iconOverrides[key] = input.value;
        const preview = document.querySelector(`#icon-list .icon[data-icon-key="${key}"]`);
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
        fontawesomeKit = kitInput.value;
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
        const overrides = {};
        for (const input of iconForm.querySelectorAll(".icon-input")) {
          overrides[input.dataset.iconKey] = input.value;
        }
        const kitVal = kitInput ? kitInput.value.trim() : "";
        const savePromises = [saveIcons(overrides)];
        if (kitVal) {
          savePromises.push(saveKit(kitVal));
        }
        Promise.all(savePromises).then(() => {
          fontawesomeKit = kitVal || fontawesomeKit;
          for (const key of Object.keys(iconOverrides)) {
            applyIconOverride(key);
          }
          setDirty(false);
        }).catch((err) => {
          showBanner(messageOf(err));
        });
      });
    }

    iconForm.addEventListener("submit", (event) => {
      event.preventDefault();
      const overrides = {};
      for (const input of iconForm.querySelectorAll(".icon-input")) {
        overrides[input.dataset.iconKey] = input.value;
      }
      const kitVal = kitInput ? kitInput.value.trim() : "";
      const savePromises = [saveIcons(overrides)];
      if (kitVal) {
        savePromises.push(saveKit(kitVal));
      }
      Promise.all(savePromises).then(() => {
        fontawesomeKit = kitVal || fontawesomeKit;
        reloadIcons();
      }).catch((err) => {
        showBanner(messageOf(err));
      });
    });

    setDirty(false);
  }
}

function bindUpdate() {
  const button = document.querySelector("#update");
  button.addEventListener("click", () => {
    button.classList.add("is-busy");
    button.setAttribute("aria-busy", "true");
    window.setTimeout(() => {
      button.classList.remove("is-busy");
      button.removeAttribute("aria-busy");
    }, 1600);
  });
}

export async function startShell(result) {
  const banner = document.querySelector("#banner");
  if (result.state === "error") {
    banner.hidden = false;
    banner.textContent = result.error || result.state;
  } else {
    document.querySelector("#profile-name").textContent = result.profileName || "";
    try {
      await loadPrefs();
    } catch (err) {
      canSave = false;
      showBanner(messageOf(err));
    }
  }
  letters(document.querySelector("#letters-nav"), true);
  letters(document.querySelector("#letters-main"), true);
  fillCalendar(new Date());
  for (const button of document.querySelectorAll("[data-section]")) {
    button.addEventListener("click", () => openSection(button.dataset.section));
  }
  bindNavResize();
  bindSplit("mail");
  bindSplit("contacts");
  bindSplit("files");
  bindSettings();
  bindUpdate();
  for (const button of document.querySelectorAll(".tree button")) {
    button.addEventListener("click", () => {
      for (const other of document.querySelectorAll(".tree button")) {
        other.removeAttribute("aria-current");
      }
      button.setAttribute("aria-current", "true");
    });
  }
  openSection("mail");
}
