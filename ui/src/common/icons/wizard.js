/* Icon Wizard. Its own dialog. Foundry text is never inserted as HTML. */
import { colorZoomOf, mountColoris, pickerZoomAfter } from "../coloris.js";
import { zoomStatus } from "../status.js";
import { paintIcon, removedText, sanitizeIconHtml } from "../iconsan.js";
import { SYSTEM_ICONS, iconNode } from "./registry.js";
import {
  FA_FLAG_GROUPS,
  PNG_RENDER,
  addClassAt,
  assetSnippet,
  bakeSvg,
  faCatalog,
  faChoiceFromHtml,
  faFamilyList,
  faIconPlace,
  faKitNames,
  foundryFromLibrary,
  iconFormat,
  iconFormatLabel,
  librarySnippet,
  needsFoundry,
  paintRender,
  wrapRange,
} from "./snippet.js";

function searchPlaceholder(text) {
  return `🔍 ${text}`;
}

const MODE_LABELS = {
  library: "Icon Library",
  foundry: "Icon Foundry",
  svg: "SVG",
  png: "PNG",
};

const HIT_BASE = 28;

function hitSize(scale) {
  return `${Math.round(HIT_BASE * scale)}px`;
}

function resultZoomOf(raw) {
  if (raw === undefined || raw === null || raw === "" || !Number.isFinite(Number(raw))) {
    return 1;
  }
  return colorZoomOf(raw);
}

function flagBoxPx(raw) {
  if (!raw || typeof raw !== "object") {
    return null;
  }
  const width = Math.round(Number(raw.width));
  const height = Math.round(Number(raw.height));
  if (!Number.isFinite(width) || !Number.isFinite(height)) {
    return null;
  }
  return { width, height };
}

function show(dialog) {
  if (typeof dialog.showModal === "function") {
    try {
      dialog.showModal();
      return;
    } catch {
      /* jsdom has no showModal. The open attribute still shows it. */
    }
  }
  dialog.setAttribute("open", "");
}

function button(doc, label, className) {
  const el = doc.createElement("button");
  el.type = "button";
  el.className = className;
  el.textContent = label;
  return el;
}

function fieldLabel(doc, text, control) {
  const label = doc.createElement("label");
  label.className = "pav-icon-field";
  const caption = doc.createElement("span");
  caption.className = "pav-icon-label";
  caption.textContent = text;
  label.append(caption, control);
  return label;
}

function selectOf(doc, options, value) {
  const select = doc.createElement("select");
  for (const item of options) {
    const option = doc.createElement("option");
    option.value = item.id;
    option.textContent = item.label;
    select.append(option);
  }
  select.value = value;
  return select;
}

async function mountFoundry(parent, text, onInput) {
  const doc = parent.ownerDocument;
  const area = doc.createElement("textarea");
  area.className = "pav-icon-foundry";
  area.value = text;
  area.spellcheck = false;
  area.setAttribute("aria-label", "Icon HTML");
  parent.append(area);
  const editor = {
    value: () => area.value,
    cursor: () => area.selectionStart || 0,
    range: () => [area.selectionStart || 0, area.selectionEnd || 0],
    set(next) {
      area.value = next;
    },
  };
  area.addEventListener("input", () => onInput(area.value));
  if (typeof navigator !== "undefined" && /jsdom/i.test(navigator.userAgent)) {
    return editor;
  }
  try {
    const [{ EditorState }, { EditorView }, { html }] = await Promise.all([
      import("@codemirror/state"),
      import("@codemirror/view"),
      import("@codemirror/lang-html"),
    ]);
    const view = new EditorView({
      state: EditorState.create({
        doc: text,
        extensions: [
          html(),
          EditorView.lineWrapping,
          EditorView.updateListener.of((update) => {
            if (update.docChanged) {
              area.value = update.state.doc.toString();
              onInput(area.value);
            }
          }),
        ],
      }),
      parent,
    });
    area.hidden = true;
    editor.value = () => view.state.doc.toString();
    editor.cursor = () => view.state.selection.main.head;
    editor.range = () => {
      const main = view.state.selection.main;
      return [main.from, main.to];
    };
    editor.set = (next) => {
      view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: next } });
      area.value = next;
    };
  } catch {
    /* The textarea remains the document. */
  }
  return editor;
}

function bytesOf(text) {
  return new TextEncoder().encode(text).length;
}

export function utf8Base64(text) {
  const bytes = new TextEncoder().encode(text);
  let bin = "";
  for (const byte of bytes) {
    bin += String.fromCharCode(byte);
  }
  return btoa(bin);
}

const liveWizards = new Set();

/* The Zooms row and a wheel over an open Wizard share this value. */
export function applyIconWizardZoom(scale) {
  const next = colorZoomOf(scale);
  for (const slot of liveWizards) {
    slot.zoom = next;
    if (slot.dialog) {
      slot.dialog.style.setProperty("--pav-wizard-zoom", String(next));
    }
  }
}

/* The search grid has its own zoom. Shell does not multiply it. */
export function applyIconResultZoom(scale) {
  const next = resultZoomOf(scale);
  for (const slot of liveWizards) {
    slot.resultZoom = next;
    if (slot.grid) {
      slot.grid.style.setProperty("--pav-icon-hit-size", hitSize(next));
    }
  }
}

export function openIconWizard(parent, options = {}) {
  if (!(parent instanceof Element)) {
    return Promise.resolve(null);
  }
  const doc = parent.ownerDocument;
  const modes = (Array.isArray(options.modes) && options.modes.length
    ? options.modes
    : ["library", "foundry", "svg", "png"]
  ).filter((mode) => MODE_LABELS[mode]);
  const shelves = [
    { id: "fa", label: iconFormatLabel("fa"), mode: "library" },
    { id: "other", label: iconFormatLabel("other"), mode: "library" },
    { id: "png", label: iconFormatLabel("png"), mode: "png" },
    { id: "svg", label: iconFormatLabel("svg"), mode: "svg" },
    { id: "custom", label: iconFormatLabel("custom"), mode: "foundry" },
  ].filter((item) => modes.includes(item.mode));
  function shelfFor(html, kind) {
    return iconFormat(html, kind);
  }
  const maxBytes = Number.isInteger(options.maxBytes) ? options.maxBytes : 8192;
  const current = typeof options.current === "string" ? options.current : "";
  const defaultHtml = typeof options.defaultHtml === "string" ? options.defaultHtml : "";
  let shelf = shelfFor(current, options.assetKind);
  if (!shelves.some((item) => item.id === shelf)) {
    shelf = shelves.length ? shelves[0].id : "fa";
  }
  let mode = (shelves.find((item) => item.id === shelf) || { mode: "library" }).mode;

  const library = {
    libraryId: "",
    family: "sharp-duotone",
    style: "solid",
    name: "star",
    color: "",
    rotate: 0,
    flip: "",
    fixed: false,
    size: "",
    materialStyle: "sharp",
    system: "",
    query: "",
    flags: [],
  };
  if (shelf === "fa") {
    const opened = faChoiceFromHtml(current);
    if (opened) {
      library.name = opened.name;
      library.family = opened.family;
      if (opened.style) {
        library.style = opened.style;
      }
      if (opened.color) {
        library.color = opened.color;
      }
      library.flags = opened.flags;
    }
  }
  const image = {
    svg: { tx: 0, ty: 0, scale: 1, rotate: 0, flipH: false, flipV: false, crop: true, background: "none", source: "" },
    png: { tx: 0, ty: 0, scale: 1, rotate: 0, flipH: false, flipV: false, crop: true, background: "none", source: "" },
  };
  let pngBytes = null;
  let svgHtml = mode === "svg" ? current : "";
  let pngHtml = mode === "png" ? current : "";
  let foundryText = current;
  let pinned = shelf === "fa" || shelf === "other" ? current : "";
  let settled = false;
  let edited = false;
  const slot = {
    zoom: options.zoom === undefined || options.zoom === null || options.zoom === ""
      ? 1
      : colorZoomOf(options.zoom),
    resultZoom: resultZoomOf(options.resultZoom),
    dialog: null,
    grid: null,
  };
  liveWizards.add(slot);
  let wheelRemainder = 0;
  const embed = options.embed === true;
  const control = {
    isDirty() {
      return false;
    },
    abandon() {},
  };

  const pending = new Promise((resolve) => {
    const dialog = doc.createElement(embed ? "div" : "dialog");
    dialog.className = embed ? "pav-icon-wizard is-embedded" : "pav-dialog pav-icon-wizard";
    slot.dialog = dialog;
    dialog.style.setProperty("--pav-wizard-zoom", String(slot.zoom));
    const title = doc.createElement("h2");
    title.className = "pav-dialog-title pav-icon-crumb";
    function crumbPart(className, text, strong) {
      const el = doc.createElement(strong ? "strong" : "span");
      el.className = className;
      el.textContent = text;
      return el;
    }
    function crumbSep() {
      const sep = doc.createElement("span");
      sep.className = "pav-icon-crumb-sep";
      sep.setAttribute("aria-hidden", "true");
      sep.textContent = ">";
      title.append(sep);
    }
    if (options.section) {
      title.append(crumbPart("pav-icon-crumb-group pav-icon-label", options.section, true));
      crumbSep();
    }
    title.append(crumbPart("pav-icon-crumb-name", options.title || "Icon", false));
    if (options.key) {
      crumbSep();
      title.append(crumbPart("pav-icon-crumb-key", options.key, false));
    }
    crumbSep();
    const formatCrumb = crumbPart("pav-icon-crumb-format", iconFormatLabel(shelf), false);
    title.append(formatCrumb);
    const closeBtn = doc.createElement("button");
    closeBtn.type = "button";
    closeBtn.className = "pav-icon-close";
    closeBtn.setAttribute("aria-label", "Close");
    closeBtn.setAttribute("data-pav-tip", "Close");
    const closeIcon = doc.createElement("i");
    closeIcon.className = "icon fa-sharp fa-thin fa-xmark";
    closeIcon.setAttribute("aria-hidden", "true");
    closeIcon.draggable = false;
    closeBtn.append(closeIcon);
    const barCrumb = doc.createElement("div");
    barCrumb.className = "button-bar pav-module-toolbar pav-icon-bar pav-icon-crumb-bar";
    barCrumb.setAttribute("role", "toolbar");
    barCrumb.setAttribute("aria-label", "Icon");
    const crumbSpacer = doc.createElement("div");
    crumbSpacer.className = "pav-module-spacer";
    if (embed) {
      barCrumb.append(title, crumbSpacer);
    } else {
      barCrumb.append(title, crumbSpacer, closeBtn);
    }

    const layout = doc.createElement("div");
    layout.className = "pav-icon-layout";
    const previewCol = doc.createElement("div");
    previewCol.className = "pav-icon-preview-col";
    const square = doc.createElement("div");
    square.className = "pav-icon-square";
    square.setAttribute("aria-hidden", "true");
    const themeLabel = doc.createElement("div");
    themeLabel.className = "pav-icon-theme-label";
    themeLabel.textContent = "Theme";
    function themeCaption(text) {
      const cap = doc.createElement("div");
      cap.className = "pav-icon-theme-label";
      cap.textContent = text;
      return cap;
    }
    function themeChip(kind) {
      const chip = doc.createElement("div");
      chip.className = `pav-icon-chip is-${kind}`;
      chip.setAttribute("aria-hidden", "true");
      return chip;
    }
    const chipDark = themeChip("dark");
    const chipLight = themeChip("light");
    previewCol.append(square, themeLabel, chipDark, themeCaption("Dark"), chipLight, themeCaption("Light"));

    const barPresets = doc.createElement("div");
    barPresets.className = "button-bar pav-module-toolbar pav-icon-bar pav-icon-presets";
    barPresets.setAttribute("role", "toolbar");
    barPresets.setAttribute("aria-label", "Icon presets");
    const presetDefault = button(doc, "Default", "pav-icon-preset pav-icon-preset-pin");
    const presetScroll = doc.createElement("div");
    presetScroll.className = "pav-icon-preset-scroll";
    for (const system of SYSTEM_ICONS) {
      const preset = button(doc, system.label, "pav-icon-preset");
      preset.dataset.system = system.id;
      presetScroll.append(preset);
    }
    for (const recent of options.recent || []) {
      if (typeof recent !== "string" || recent === "") {
        continue;
      }
      const chip = button(doc, "Recent", "pav-icon-preset pav-icon-recent");
      chip.dataset.snippet = recent;
      const painted = paintIcon(recent, doc);
      chip.replaceChildren();
      if (painted.ok && painted.fragment.firstElementChild) {
        chip.append(painted.fragment.firstElementChild);
      } else {
        chip.textContent = "Recent";
      }
      presetScroll.append(chip);
    }
    function actionIcon(key, label, className) {
      const el = doc.createElement("button");
      el.type = "button";
      el.className = className;
      el.setAttribute("aria-label", label);
      el.setAttribute("data-pav-tip", label);
      const icon = iconNode(key, { icons: options.icons, documentRef: doc });
      if (icon) {
        icon.draggable = false;
        el.append(icon);
      }
      return el;
    }
    const acceptBtn = actionIcon("ui.check", "Accept", "pav-dialog-ok");
    const cancelBtn = actionIcon("ui.cancel", "Cancel", "pav-dialog-cancel");
    const spacer = doc.createElement("div");
    spacer.className = "pav-module-spacer";
    barPresets.append(presetDefault, presetScroll, spacer, acceptBtn, cancelBtn);

    const barSource = doc.createElement("div");
    barSource.className = "button-bar pav-module-toolbar pav-icon-bar";
    barSource.setAttribute("role", "toolbar");
    barSource.setAttribute("aria-label", "Icon Format");
    const formatTitle = doc.createElement("span");
    formatTitle.className = "pav-module-title pav-icon-label";
    formatTitle.textContent = "Icon Format";
    barSource.append(formatTitle);
    const shelfButtons = new Map();
    for (const item of shelves) {
      const tab = button(doc, item.label, "pav-icon-mode");
      tab.dataset.shelf = item.id;
      tab.setAttribute("aria-pressed", item.id === shelf ? "true" : "false");
      shelfButtons.set(item.id, tab);
      barSource.append(tab);
    }

    const barTools = doc.createElement("div");
    barTools.className = "button-bar pav-module-toolbar pav-icon-bar pav-icon-toolsbar";
    barTools.setAttribute("role", "toolbar");
    barTools.setAttribute("aria-label", "Icon tools");

    const bars = doc.createElement("div");
    bars.className = "pav-icon-bars";
    bars.append(barCrumb, barPresets, barSource, barTools);
    const main = doc.createElement("div");
    main.className = "pav-icon-main";
    const work = doc.createElement("div");
    work.className = "pav-icon-work";
    const note = doc.createElement("p");
    note.className = "pav-icon-note";
    main.append(bars, work, note);
    layout.append(previewCol, main);
    dialog.append(layout);
    parent.append(dialog);

    let foundry = null;
    const pages = {};
    let grid = null;
    let syncLibrary = () => {};
    let syncSearchChrome = () => {};
    let flagObserver = null;
    const menuObservers = [];
    let escapeClearsSearch = false;
    let cancelSearch = () => {};

    function closePops() {
      for (const menu of dialog.querySelectorAll(".pav-icon-pop")) {
        menu.hidden = true;
      }
      for (const trigger of dialog.querySelectorAll("button.pav-icon-menu")) {
        trigger.setAttribute("aria-expanded", "false");
      }
    }

    function placePop(trigger, menu) {
      const rect = trigger.getBoundingClientRect();
      const z = slot.zoom || 1;
      menu.hidden = false;
      menu.style.left = `${rect.left / z}px`;
      menu.style.top = `${rect.bottom / z}px`;
      trigger.setAttribute("aria-expanded", "true");
    }

    function openPop(trigger, menu) {
      const wasClosed = menu.hidden;
      closePops();
      if (wasClosed) {
        placePop(trigger, menu);
      }
    }

    /* The select stays in the label so tests and refillStyles keep .value.
       The button is what you see. A label click on that button must not open
       the native list. */
    function attachMenu(select, caption) {
      select.hidden = true;
      select.tabIndex = -1;
      select.setAttribute("aria-hidden", "true");
      const trigger = doc.createElement("button");
      trigger.type = "button";
      trigger.className = "pav-icon-menu";
      trigger.setAttribute("aria-haspopup", "listbox");
      trigger.setAttribute("aria-expanded", "false");
      const name = doc.createElement("span");
      name.className = "pav-icon-menu-label";
      const caret = doc.createElement("span");
      caret.className = "pav-icon-caret";
      caret.setAttribute("aria-hidden", "true");
      trigger.append(name, caret);
      select.after(trigger);
      const menu = doc.createElement("div");
      menu.className = "pav-icon-pop pav-icon-choice-menu";
      menu.hidden = true;
      menu.setAttribute("role", "listbox");
      menu.setAttribute("aria-label", caption);
      dialog.append(menu);
      function paint() {
        const picked = [...select.options].find((option) => option.value === select.value) || select.options[0] || null;
        const text = picked && picked.textContent ? picked.textContent : "";
        name.textContent = text;
        trigger.setAttribute("aria-label", text ? `${caption}: ${text}` : caption);
        for (const row of menu.querySelectorAll(".pav-icon-flag")) {
          const on = row.dataset.value === select.value && select.value !== "";
          row.setAttribute("aria-pressed", on ? "true" : "false");
          row.setAttribute("aria-selected", on ? "true" : "false");
        }
      }
      function rebuild() {
        menu.replaceChildren();
        for (const option of select.options) {
          if (!option.textContent) {
            continue;
          }
          const row = doc.createElement("button");
          row.type = "button";
          row.className = "pav-icon-flag";
          row.dataset.value = option.value;
          row.setAttribute("role", "option");
          const mark = doc.createElement("span");
          mark.className = "pav-icon-flag-check";
          mark.setAttribute("aria-hidden", "true");
          const label = doc.createElement("span");
          label.className = "pav-icon-flag-label";
          label.textContent = option.textContent;
          row.append(mark, label);
          menu.append(row);
        }
        paint();
      }
      const observer = new MutationObserver(() => rebuild());
      observer.observe(select, { childList: true });
      menuObservers.push(() => observer.disconnect());
      rebuild();
      select.addEventListener("change", paint);
      trigger.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        openPop(trigger, menu);
      });
      menu.addEventListener("click", (event) => {
        const row = event.target instanceof Element ? event.target.closest(".pav-icon-flag") : null;
        if (!row) {
          return;
        }
        event.stopPropagation();
        if (select.value !== row.dataset.value) {
          select.value = row.dataset.value;
          select.dispatchEvent(new Event("input", { bubbles: true }));
          select.dispatchEvent(new Event("change", { bubbles: true }));
        }
        closePops();
      });
      return rebuild;
    }

    function activeHtml() {
      if (mode === "library") {
        if (pinned) {
          return pinned;
        }
        if (!Array.isArray(library.flags) && needsFoundry(library)) {
          return "";
        }
        return librarySnippet(library);
      }
      if (mode === "foundry") {
        return foundry ? foundry.value() : foundryText;
      }
      if (mode === "svg") {
        return svgHtml;
      }
      return pngHtml;
    }

    function paintSquare(html, live) {
      if (mode === "foundry" && !live) {
        return;
      }
      square.replaceChildren();
      chipDark.replaceChildren();
      chipLight.replaceChildren();
      const painted = paintIcon(html || "", doc);
      if (!html) {
        note.textContent = needsFoundry(library) && mode === "library" ? "Open in Custom" : "";
        acceptBtn.disabled = true;
        return;
      }
      if (!painted.ok || bytesOf(html) > maxBytes) {
        note.textContent = painted.ok ? "The icon is over the size limit." : removedText(painted.removed);
        acceptBtn.disabled = true;
        return;
      }
      note.textContent = "";
      acceptBtn.disabled = false;
      const node = painted.fragment.firstElementChild;
      if (node) {
        square.append(node);
        chipDark.append(node.cloneNode(true));
        chipLight.append(node.cloneNode(true));
      }
    }

    function toolsetOn(which) {
      if (which === "tone") {
        return shelf === "fa" || shelf === "other";
      }
      return which === shelf;
    }

    function showShelf(id) {
      const item = shelves.find((entry) => entry.id === id);
      if (!item) {
        return;
      }
      const previous = shelf;
      shelf = item.id;
      closePops();
      mode = item.mode;
      formatCrumb.textContent = item.label;
      for (const [key, tab] of shelfButtons) {
        tab.setAttribute("aria-pressed", key === shelf ? "true" : "false");
      }
      for (const group of barTools.querySelectorAll(".pav-icon-toolset")) {
        group.hidden = !toolsetOn(group.dataset.toolset);
      }
      for (const [key, page] of Object.entries(pages)) {
        const on = key === "library"
          ? shelf === "fa" || shelf === "other"
          : key === "foundry"
            ? shelf === "custom"
            : key === shelf;
        page.hidden = !on;
      }
      if ((previous === "fa" || previous === "other") && previous !== shelf && grid) {
        grid.replaceChildren();
        if (note.textContent === "No icons match.") {
          note.textContent = "";
        }
      }
      syncLibrary();
      syncSearchChrome();
      if (mode !== "foundry") {
        paintSquare(activeHtml(), true);
      }
    }

    function loadSnippet(html) {
      edited = true;
      foundryText = html;
      if (foundry) {
        foundry.set(html);
      }
      const next = shelfFor(html, options.assetKind);
      if (next === "svg") {
        svgHtml = html;
        pinned = "";
      } else if (next === "png") {
        pngHtml = html;
        pinned = "";
      } else {
        pinned = html;
        library.system = String(html).includes("pav-system:mark") ? "mark" : "";
      }
      if (shelves.some((item) => item.id === next)) {
        showShelf(next);
      }
      paintSquare(html, true);
    }

    function libraryPage() {
      const menuSync = [];
      function syncMenus() {
        for (const rebuild of menuSync) {
          rebuild();
        }
      }
      const page = doc.createElement("div");
      page.className = "pav-icon-page";
      const libs = (options.libraries || []).filter((item) => item && item.enabled);
      const others = libs.filter((item) => item.id !== "fontawesome");
      const libSelect = selectOf(
        doc,
        others.map((item) => ({ id: item.id, label: item.name || item.id })),
        (others[0] && others[0].id) || "",
      );
      const familyRows = faFamilyList();
      const family = selectOf(doc, familyRows, "");
      const style = doc.createElement("select");
      const materialFace = selectOf(doc, [
        { id: "sharp", label: "Sharp" },
        { id: "outlined", label: "Outlined" },
        { id: "rounded", label: "Rounded" },
      ], library.materialStyle);
      const familyField = fieldLabel(doc, "Family", family);
      const styleField = fieldLabel(doc, "Style", style);
      const materialField = fieldLabel(doc, "Material style", materialFace);
      function stylesFor(familyId) {
        const row = familyRows.find((item) => item.id === familyId);
        return row ? row.styles : [];
      }
      function refillStyles() {
        const styles = stylesFor(family.value);
        const hide = styles.length === 0;
        styleField.hidden = hide;
        const previous = style.value || library.style;
        style.replaceChildren();
        const items = hide ? [{ id: "", label: "" }] : styles;
        for (const item of items) {
          const option = doc.createElement("option");
          option.value = item.id;
          option.textContent = item.label;
          style.append(option);
        }
        const keep = items.some((item) => item.id === previous);
        style.value = keep ? previous : items[0].id;
        syncMenus();
      }
      if (!familyRows.some((item) => item.id === library.family) && familyRows[0]) {
        library.family = familyRows[0].id;
      }
      family.value = library.family;
      refillStyles();
      library.style = style.value;
      const search = doc.createElement("input");
      search.type = "search";
      search.className = "pav-module-search";
      search.placeholder = searchPlaceholder("Search Font Awesome");
      search.setAttribute("aria-label", "Search Font Awesome");
      search.spellcheck = false;
      search.autocomplete = "off";
      const searchSpacer = doc.createElement("span");
      searchSpacer.className = "pav-module-spacer";
      const helpBtn = doc.createElement("button");
      helpBtn.type = "button";
      helpBtn.className = "help-jump";
      helpBtn.dataset.helpPage = "settings";
      helpBtn.dataset.helpSection = "fontawesome";
      helpBtn.dataset.wizardHelp = "true";
      helpBtn.dataset.shelfOn = "true";
      helpBtn.setAttribute("aria-label", "Help for Font Awesome");
      const helpIcon = iconNode("nav.help", {
        icons: options.icons,
        themeHtml: typeof options.helpThemeHtml === "string" ? options.helpThemeHtml : "",
        documentRef: doc,
      });
      if (helpIcon) {
        helpIcon.draggable = false;
        helpBtn.append(helpIcon);
      }
      helpBtn.addEventListener("click", (event) => {
        if (typeof options.onHelp !== "function") {
          return;
        }
        event.stopPropagation();
        options.onHelp();
      });
      grid = doc.createElement("div");
      grid.className = "pav-icon-grid";
      slot.grid = grid;
      grid.style.setProperty("--pav-icon-hit-size", hitSize(slot.resultZoom));
      let resultsShelf = "";
      let resultRows = [];
      const color = doc.createElement("input");
      color.type = "text";
      color.placeholder = "#800000";
      color.setAttribute("aria-label", "Color");
      if (library.color) {
        color.value = library.color;
      }
      void mountColoris(color, options.swatches);
      const flagsBtn = button(doc, "", "pav-icon-tool pav-icon-menu");
      flagsBtn.setAttribute("aria-haspopup", "true");
      flagsBtn.setAttribute("aria-expanded", "false");
      flagsBtn.setAttribute("aria-label", "Flags");
      const flagsLabel = doc.createElement("span");
      flagsLabel.className = "pav-icon-menu-label";
      flagsLabel.textContent = "Flags";
      const flagsCaret = doc.createElement("span");
      flagsCaret.className = "pav-icon-caret";
      flagsCaret.setAttribute("aria-hidden", "true");
      flagsBtn.append(flagsLabel, flagsCaret);
      const clearFlags = actionIcon("ui.cancel", "Clear flags", "pav-icon-flag-clear");
      clearFlags.hidden = true;
      const flagsWrap = doc.createElement("div");
      flagsWrap.className = "pav-icon-flags";
      flagsWrap.append(flagsBtn, clearFlags);
      const flagMenu = doc.createElement("div");
      flagMenu.className = "pav-icon-pop pav-icon-flag-menu";
      flagMenu.hidden = true;
      for (const group of FA_FLAG_GROUPS) {
        const section = doc.createElement("div");
        section.className = "pav-icon-flag-section";
        section.setAttribute("role", "group");
        section.setAttribute("aria-label", group.label);
        const heading = doc.createElement("div");
        heading.className = "pav-icon-flag-heading";
        heading.setAttribute("aria-hidden", "true");
        heading.textContent = group.label;
        section.append(heading);
        for (const item of group.items) {
          const choice = button(doc, "", "pav-icon-flag");
          choice.dataset.flag = item.id;
          choice.dataset.exclusive = group.exclusive ? "true" : "false";
          choice.setAttribute("aria-pressed", "false");
          const mark = doc.createElement("span");
          mark.className = "pav-icon-flag-check";
          mark.setAttribute("aria-hidden", "true");
          const name = doc.createElement("span");
          name.className = "pav-icon-flag-label";
          name.textContent = item.label;
          choice.append(mark, name);
          section.append(choice);
        }
        flagMenu.append(section);
      }
      dialog.append(flagMenu);
      const openedBox = flagBoxPx(options.flagBox);
      if (openedBox) {
        flagMenu.style.width = `${openedBox.width}px`;
        flagMenu.style.height = `${openedBox.height}px`;
      }
      let flagBoxWidth = 0;
      let flagBoxHeight = 0;
      let flagBoxSeen = false;
      if (typeof ResizeObserver === "function") {
        flagObserver = new ResizeObserver(() => {
          if (flagMenu.hidden) {
            return;
          }
          const width = Math.round(flagMenu.offsetWidth);
          const height = Math.round(flagMenu.offsetHeight);
          if (width < 160 || height < 120) {
            return;
          }
          if (!flagBoxSeen) {
            flagBoxSeen = true;
            flagBoxWidth = width;
            flagBoxHeight = height;
            return;
          }
          if (width === flagBoxWidth && height === flagBoxHeight) {
            return;
          }
          flagBoxWidth = width;
          flagBoxHeight = height;
          if (typeof options.onFlagBox === "function") {
            options.onFlagBox({ width, height });
          }
        });
        flagObserver.observe(flagMenu);
      }
      function paintFlags() {
        const on = new Set(library.flags);
        const count = library.flags.length;
        flagsLabel.textContent = count > 0 ? `Flags (${count})` : "Flags";
        clearFlags.hidden = count === 0;
        for (const choice of flagMenu.querySelectorAll(".pav-icon-flag")) {
          choice.setAttribute("aria-pressed", on.has(choice.dataset.flag) ? "true" : "false");
        }
      }
      flagsBtn.addEventListener("click", (event) => {
        event.stopPropagation();
        openPop(flagsBtn, flagMenu);
      });
      flagMenu.addEventListener("click", (event) => {
        const choice = event.target instanceof Element ? event.target.closest(".pav-icon-flag") : null;
        if (!choice) {
          return;
        }
        event.stopPropagation();
        const cls = choice.dataset.flag;
        const flags = new Set(library.flags);
        if (flags.has(cls)) {
          flags.delete(cls);
        } else {
          if (choice.dataset.exclusive === "true") {
            for (const other of choice.parentElement.querySelectorAll(".pav-icon-flag")) {
              flags.delete(other.dataset.flag);
            }
          }
          flags.add(cls);
        }
        library.flags = FA_FLAG_GROUPS.flatMap((group) => group.items.map((item) => item.id)).filter((id) => flags.has(id));
        paintFlags();
        onField();
      });
      paintFlags();
      const foundryBtn = button(doc, "Open in Custom", "pav-icon-tool");
      foundryBtn.hidden = true;
      function markHits() {
        for (const hit of grid.querySelectorAll(".pav-icon-hit")) {
          const on = hit.dataset.name === library.name && !library.system && resultsShelf === shelf;
          hit.setAttribute("aria-pressed", on ? "true" : "false");
        }
      }
      function noHitNote() {
        const fa = globalThis.FontAwesome;
        if (library.libraryId === "fontawesome" && fa && typeof fa.findIconDefinition === "function" && faCatalog().source === "kit") {
          return "No icons in the loaded kit match.";
        }
        return "No icons in this family and style match.";
      }
      function chooseHit(row, hit) {
        const placeFamily = hit.dataset.family || "";
        const placeStyle = hit.dataset.style || "";
        if (placeFamily && placeFamily !== family.value) {
          family.value = placeFamily;
          refillStyles();
        }
        if (placeStyle && [...style.options].some((opt) => opt.value === placeStyle)) {
          style.value = placeStyle;
        }
        syncMenus();
        library.system = "";
        library.name = row.name;
        refresh();
      }
      function paintHits(rows) {
        grid.replaceChildren();
        let shown = 0;
        for (const row of rows) {
          if (shown >= 80) {
            break;
          }
          if (!row || typeof row.name !== "string" || row.name === "") {
            continue;
          }
          let drawFamily = library.family;
          let drawStyle = library.style;
          if (library.libraryId === "fontawesome") {
            if (row.family === "brands") {
              drawFamily = "brands";
              drawStyle = "";
            } else {
              const place = faIconPlace(library.family, library.style, row.name);
              if (!place) {
                continue;
              }
              drawFamily = place.family;
              drawStyle = place.style;
            }
          }
          const label = row.label || row.name;
          const hit = button(doc, "", "pav-icon-hit");
          hit.dataset.name = row.name;
          if (library.libraryId === "fontawesome") {
            hit.dataset.family = drawFamily;
            hit.dataset.style = drawStyle;
          }
          hit.setAttribute("aria-label", label);
          hit.setAttribute("data-pav-tip", label);
          const snippet = librarySnippet({
            ...library,
            family: drawFamily,
            style: drawStyle,
            system: "",
            name: row.name,
          });
          const painted = paintIcon(snippet, doc);
          if (painted.ok && painted.fragment.firstElementChild) {
            hit.append(painted.fragment.firstElementChild);
          }
          const caption = doc.createElement("span");
          caption.className = "pav-icon-hit-name";
          caption.textContent = label;
          hit.append(caption);
          hit.addEventListener("click", () => chooseHit(row, hit));
          hit.addEventListener("dblclick", () => {
            chooseHit(row, hit);
            accept();
          });
          grid.append(hit);
          shown += 1;
        }
        markHits();
        return shown;
      }
      syncLibrary = () => {
        if (shelf === "fa") {
          library.libraryId = "fontawesome";
        } else if (shelf === "other") {
          library.libraryId = libSelect.value;
        }
        library.family = family.value;
        library.style = style.value;
        library.materialStyle = materialFace.value;
        library.color = color.value.trim();
        materialField.hidden = !(shelf === "other" && library.libraryId === "material");
        foundryBtn.hidden = shelf !== "fa" || !needsFoundry(library);
        if (resultsShelf === shelf && resultRows.length) {
          const shown = paintHits(resultRows);
          if (!shown) {
            note.textContent = noHitNote();
          } else if (note.textContent === "No icons in this family and style match." || note.textContent === "No icons in the loaded kit match.") {
            note.textContent = "";
          }
        } else {
          markHits();
        }
        syncSearchChrome();
      };
      const refresh = () => {
        edited = true;
        pinned = "";
        syncLibrary();
        paintSquare(activeHtml(), true);
      };
      const onField = () => {
        library.system = "";
        refresh();
      };
      clearFlags.addEventListener("click", (event) => {
        event.stopPropagation();
        event.preventDefault();
        if (!library.flags.length) {
          return;
        }
        library.flags = [];
        paintFlags();
        onField();
      });
      family.addEventListener("change", () => {
        refillStyles();
      });
      for (const control of [libSelect, family, style, materialFace, color]) {
        control.addEventListener("input", onField);
        control.addEventListener("change", onField);
      }
      let searchGen = 0;
      let searchTimer = 0;
      let searchBusy = false;
      let searchWait = false;
      let searchClosed = false;
      cancelSearch = () => {
        searchClosed = true;
        if (searchTimer) {
          clearTimeout(searchTimer);
          searchTimer = 0;
        }
        searchGen += 1;
      };
      function scheduleSearch(delay) {
        if (searchClosed) {
          return;
        }
        if (searchBusy) {
          searchWait = true;
          return;
        }
        if (searchTimer) {
          clearTimeout(searchTimer);
        }
        searchTimer = setTimeout(() => {
          searchTimer = 0;
          void runSearch();
        }, delay);
      }
      search.addEventListener("input", () => {
        scheduleSearch(500);
      });
      search.addEventListener("keydown", (event) => {
        if (event.key === "Enter") {
          event.preventDefault();
          scheduleSearch(0);
        }
      });
      async function runSearch() {
        if (typeof options.search !== "function") {
          return;
        }
        if (shelf === "other" && !libSelect.value) {
          note.textContent = "Turn on a library under Settings → Libraries.";
          return;
        }
        if (searchBusy) {
          searchWait = true;
          return;
        }
        searchBusy = true;
        const gen = ++searchGen;
        try {
          syncLibrary();
          note.textContent = "Searching.";
          library.query = search.value;
          let rows = [];
          try {
            const payload = await options.search({ id: library.libraryId, query: library.query, limit: 200 });
            if (gen !== searchGen) {
              return;
            }
            rows = Array.isArray(payload) ? payload : (payload && payload.icons) || [];
          } catch (err) {
            if (gen !== searchGen) {
              return;
            }
            note.textContent = err instanceof Error ? err.message : "Search failed.";
            return;
          }
          if (library.libraryId === "fontawesome") {
            const have = new Set(rows.map((row) => row && row.name).filter(Boolean));
            for (const name of faKitNames(library.query)) {
              if (!have.has(name)) {
                rows.push({ name, label: name });
                have.add(name);
              }
            }
          }
          resultsShelf = shelf;
          if (!rows.length) {
            resultRows = [];
            grid.replaceChildren();
            note.textContent = "No icons match.";
            return;
          }
          resultRows = rows;
          const shown = paintHits(rows);
          note.textContent = shown ? "" : noHitNote();
        } finally {
          searchBusy = false;
          if (!searchClosed && searchWait) {
            searchWait = false;
            scheduleSearch(500);
          }
        }
      }
      let resultRemainder = 0;
      grid.addEventListener("wheel", (event) => {
        if (!event.ctrlKey) {
          return;
        }
        event.preventDefault();
        event.stopPropagation();
        const steps = notchesFrom(event, () => resultRemainder, (value) => {
          resultRemainder = value;
        });
        if (!steps) {
          return;
        }
        const toward = typeof options.toward === "function" && options.toward();
        const next = pickerZoomAfter(slot.resultZoom, steps, toward);
        if (next !== slot.resultZoom) {
          slot.resultZoom = next;
          grid.style.setProperty("--pav-icon-hit-size", hitSize(next));
          if (typeof options.onResultZoom === "function") {
            options.onResultZoom(next);
          }
        }
        zoomStatus("Icon results", next);
      }, { passive: false });
      foundryBtn.addEventListener("click", () => {
        edited = true;
        const nested = foundryFromLibrary(library);
        foundryText = nested;
        if (foundry) {
          foundry.set(nested);
        }
        showShelf("custom");
        paintSquare(nested, true);
      });
      function addToolset(id, ...nodes) {
        const group = doc.createElement("div");
        group.className = "pav-icon-toolset";
        group.dataset.toolset = id;
        group.append(...nodes);
        barTools.append(group);
      }
      const libraryField = fieldLabel(doc, "Library", libSelect);
      menuSync.push(attachMenu(family, "Family"), attachMenu(style, "Style"), attachMenu(libSelect, "Library"), attachMenu(materialFace, "Material style"));
      addToolset("fa", familyField, styleField, flagsWrap, foundryBtn);
      addToolset("other", libraryField, materialField);
      addToolset("tone", fieldLabel(doc, "Color", color));
      barTools.append(searchSpacer, search, helpBtn);
      syncSearchChrome = () => {
        const libraryOn = shelf === "fa" || shelf === "other";
        searchSpacer.hidden = !libraryOn;
        search.hidden = !libraryOn;
        const picked = libSelect.options[libSelect.selectedIndex];
        const prompt = shelf === "fa"
          ? "Search Font Awesome"
          : (picked && picked.textContent ? `Search ${picked.textContent}` : "Search icons");
        search.placeholder = searchPlaceholder(prompt);
        search.setAttribute("aria-label", prompt);
        helpBtn.dataset.shelfOn = libraryOn ? "true" : "false";
        helpBtn.hidden = !libraryOn || options.showHelp === false;
      };
      page.append(grid);
      return page;
    }

    function foundryPage() {
      const page = doc.createElement("div");
      page.className = "pav-icon-page";
      const tools = doc.createElement("div");
      tools.className = "pav-icon-toolset";
      tools.dataset.toolset = "custom";
      const host = doc.createElement("div");
      host.className = "pav-icon-editor";
      const preview = button(doc, "Preview", "pav-icon-tool");
      preview.addEventListener("click", () => {
        edited = true;
        foundryText = foundry ? foundry.value() : foundryText;
        paintSquare(foundryText, true);
      });
      const actions = [
        ["Layers", () => edit((text, range) => wrapRange(text, range[0], range[1], '<span class="fa-layers">', "</span>"))],
        ["Stack", () => edit((text, range) => wrapRange(text, range[0], range[1], '<span class="fa-stack">', "</span>"))],
        ["Flip H", () => edit((text, _range, cursor) => addClassAt(text, cursor, "fa-flip-horizontal"))],
        ["Flip V", () => edit((text, _range, cursor) => addClassAt(text, cursor, "fa-flip-vertical"))],
        ["Rotate", () => edit((text, _range, cursor) => addClassAt(text, cursor, "fa-rotate-90"))],
        ["Spin", () => edit((text, _range, cursor) => addClassAt(text, cursor, "fa-spin"))],
      ];
      for (const [label, fn] of actions) {
        const tool = button(doc, label, "pav-icon-tool");
        tool.addEventListener("click", fn);
        tools.append(tool);
      }
      tools.append(preview);
      barTools.append(tools);
      page.append(host);
      mountFoundry(host, foundryText, (value) => {
        edited = true;
        foundryText = value;
      }).then((editor) => {
        foundry = editor;
      });
      function edit(fn) {
        if (!foundry) {
          return;
        }
        const text = foundry.value();
        const next = fn(text, foundry.range(), foundry.cursor());
        foundry.set(next);
        foundryText = next;
        edited = true;
      }
      return page;
    }

    function imagePage(kind) {
      const page = doc.createElement("div");
      page.className = "pav-icon-page";
      const tools = doc.createElement("div");
      tools.className = "pav-icon-toolset";
      tools.dataset.toolset = kind;
      const load = button(doc, "Load", "pav-icon-tool");
      const save = button(doc, "Save", "pav-icon-tool");
      const scale = doc.createElement("input");
      scale.type = "range";
      scale.min = "0.2";
      scale.max = "4";
      scale.step = "0.1";
      scale.value = "1";
      const rotate = doc.createElement("input");
      rotate.type = "number";
      rotate.value = "0";
      rotate.setAttribute("aria-label", "Rotate degrees");
      const flipH = button(doc, "Flip H", "pav-icon-tool");
      const flipV = button(doc, "Flip V", "pav-icon-tool");
      const turn = button(doc, "Rotate 90", "pav-icon-tool");
      const transform = image[kind];
      function read() {
        edited = true;
        transform.scale = Number(scale.value) || 1;
        transform.rotate = Number(rotate.value) || 0;
      }
      flipH.addEventListener("click", () => {
        edited = true;
        transform.flipH = !transform.flipH;
      });
      flipV.addEventListener("click", () => {
        edited = true;
        transform.flipV = !transform.flipV;
      });
      turn.addEventListener("click", () => {
        edited = true;
        transform.rotate = ((Number(rotate.value) || 0) + 90) % 360;
        rotate.value = String(transform.rotate);
      });
      scale.addEventListener("input", read);
      rotate.addEventListener("input", read);
      load.addEventListener("click", () => importImage(kind));
      save.addEventListener("click", () => {
        read();
        if (kind === "svg") {
          rebake("svg");
          return;
        }
        if (!pngBytes && typeof options.renderPng !== "function") {
          note.textContent = "Load a PNG.";
          return;
        }
        void storePng(pngBytes);
      });
      tools.append(load, save, fieldLabel(doc, "Scale", scale), fieldLabel(doc, "Rotate", rotate), flipH, flipV, turn);
      barTools.append(tools);
      return page;
    }

    async function importImage(kind) {
      let text = "";
      let bytes = null;
      if (typeof options.pickFile === "function") {
        let picked;
        try {
          picked = await options.pickFile(kind);
        } catch (err) {
          note.textContent = err instanceof Error ? err.message : "Could not import the file.";
          acceptBtn.disabled = true;
          return;
        }
        if (!picked) {
          return;
        }
        if (kind === "svg") {
          text = picked.text || "";
        } else {
          bytes = picked.bytes || null;
          image.png.originalId = picked.id || "";
        }
      }
      if (kind === "svg") {
        image.svg.source = text;
        rebake("svg");
        return;
      }
      if (bytes) {
        pngBytes = bytes;
      }
      if (!bytes && typeof options.renderPng !== "function") {
        note.textContent = "Load a PNG.";
        return;
      }
      await storePng(bytes);
    }

    function rebake(kind) {
      if (kind !== "svg") {
        return;
      }
      const baked = bakeSvg(image.svg.source, { ...image.svg, crop: true }, doc);
      if (!baked.ok) {
        note.textContent = removedText(baked.removed);
        acceptBtn.disabled = true;
        return;
      }
      if (typeof options.storeSvg !== "function") {
        note.textContent = "Cannot store the SVG.";
        return;
      }
      options.storeSvg(baked.svg).then((stored) => {
        if (!stored || !stored.id) {
          return;
        }
        svgHtml = assetSnippet(stored.id);
        paintSquare(svgHtml, true);
      }).catch((err) => {
        note.textContent = err instanceof Error ? err.message : "Could not store the SVG.";
      });
    }

    async function storePng(bytes) {
      if (typeof options.storePng !== "function") {
        note.textContent = "Cannot store the PNG.";
        return;
      }
      const stored = await options.storePng(bytes, image.png);
      if (stored && stored.id) {
        pngHtml = assetSnippet(stored.id);
        paintSquare(pngHtml, true);
      }
    }

    function accept() {
      const html = activeHtml();
      const checked = sanitizeIconHtml(html, doc);
      if (!html || !checked.ok || bytesOf(html) > maxBytes) {
        note.textContent = !checked.ok ? removedText(checked.removed) : "The icon is over the size limit.";
        acceptBtn.disabled = true;
        paintSquare(html, true);
        return;
      }
      finish({ html });
    }

    function notchesFrom(event, read, write) {
      const delta = event.deltaY;
      if (!Number.isFinite(delta) || delta === 0) {
        return 0;
      }
      if (event.deltaMode !== 0 || Math.abs(delta) >= 100) {
        write(0);
        return delta > 0 ? 1 : -1;
      }
      let remainder = read() + delta;
      if (Math.abs(remainder) < 100) {
        write(remainder);
        return 0;
      }
      const steps = remainder > 0 ? 1 : -1;
      remainder -= steps * 100;
      write(remainder);
      return steps;
    }

    function wizardNotches(event) {
      return notchesFrom(event, () => wheelRemainder, (value) => {
        wheelRemainder = value;
      });
    }

    async function requestClose() {
      if (edited && activeHtml() !== current && typeof options.confirm === "function") {
        const ok = await options.confirm({
          title: "Discard icon changes?",
          text: "Closing leaves the saved icon unchanged.",
          okLabel: "Discard",
        });
        if (!ok) {
          return;
        }
      }
      finish(null);
    }

    function finish(value) {
      if (settled) {
        return;
      }
      settled = true;
      cancelSearch();
      liveWizards.delete(slot);
      if (flagObserver) {
        flagObserver.disconnect();
      }
      for (const disconnect of menuObservers) {
        disconnect();
      }
      dialog.remove();
      resolve(value);
    }

    control.isDirty = () => edited && activeHtml() !== current;
    control.abandon = () => finish(null);

    if (modes.includes("library")) {
      pages.library = libraryPage();
    }
    if (modes.includes("foundry")) {
      pages.foundry = foundryPage();
    }
    if (modes.includes("svg")) {
      pages.svg = imagePage("svg");
    }
    if (modes.includes("png")) {
      pages.png = imagePage("png");
    }
    for (const page of Object.values(pages)) {
      work.append(page);
    }
    for (const [name, tab] of shelfButtons) {
      tab.addEventListener("click", () => showShelf(name));
    }
    barPresets.addEventListener("click", (event) => {
      const target = event.target instanceof Element ? event.target.closest("button") : null;
      if (!target || target === acceptBtn || target === cancelBtn) {
        return;
      }
      if (target.dataset.suppressClick === "true") {
        delete target.dataset.suppressClick;
        return;
      }
      if (target.classList.contains("pav-icon-preset-pin")) {
        loadSnippet(defaultHtml);
        return;
      }
      if (!presetScroll.contains(target)) {
        return;
      }
      if (target.dataset.system) {
        library.system = target.dataset.system;
        loadSnippet(SYSTEM_ICONS.find((item) => item.id === target.dataset.system).html);
        return;
      }
      if (target.dataset.snippet) {
        loadSnippet(target.dataset.snippet);
      }
    });
    let presetDrag = null;
    presetScroll.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) {
        return;
      }
      presetDrag = { x: event.clientX, left: presetScroll.scrollLeft, moved: false, pointer: event.pointerId };
    });
    presetScroll.addEventListener("pointermove", (event) => {
      if (!presetDrag || event.pointerId !== presetDrag.pointer) {
        return;
      }
      const dx = event.clientX - presetDrag.x;
      if (Math.abs(dx) > 3) {
        presetDrag.moved = true;
      }
      if (!presetDrag.moved) {
        return;
      }
      presetScroll.scrollLeft = presetDrag.left - dx;
    });
    function endPresetDrag(event) {
      if (!presetDrag || event.pointerId !== presetDrag.pointer) {
        return;
      }
      if (presetDrag.moved && event.target instanceof Element) {
        const dragged = event.target.closest("button");
        if (dragged) {
          dragged.dataset.suppressClick = "true";
        }
      }
      presetDrag = null;
    }
    presetScroll.addEventListener("pointerup", endPresetDrag);
    presetScroll.addEventListener("pointercancel", endPresetDrag);
    presetScroll.addEventListener("wheel", (event) => {
      if (event.ctrlKey) {
        return;
      }
      const delta = Math.abs(event.deltaX) > Math.abs(event.deltaY) ? event.deltaX : event.deltaY;
      if (!delta) {
        return;
      }
      presetScroll.scrollLeft += delta;
      event.preventDefault();
      event.stopPropagation();
    }, { passive: false });
    acceptBtn.addEventListener("click", accept);
    cancelBtn.addEventListener("click", () => requestClose());
    if (!embed) {
      closeBtn.addEventListener("click", () => requestClose());
    }
    if (!embed) {
      dialog.addEventListener("cancel", (event) => {
        event.preventDefault();
        if (escapeClearsSearch) {
          escapeClearsSearch = false;
          return;
        }
        requestClose();
      });
      dialog.addEventListener("click", (event) => {
        const rect = dialog.getBoundingClientRect();
        const outside = event.clientX < rect.left || event.clientX > rect.right
          || event.clientY < rect.top || event.clientY > rect.bottom;
        if (outside) {
          requestClose();
        }
      });
    }
    dialog.addEventListener("keydown", (event) => {
      if (event.key !== "Escape") {
        return;
      }
      const field = event.target instanceof Element ? event.target.closest(".pav-module-search") : null;
      if (field && field.value !== "") {
        event.preventDefault();
        event.stopPropagation();
        escapeClearsSearch = true;
        field.value = "";
        field.dispatchEvent(new Event("input"));
        return;
      }
      const open = [...dialog.querySelectorAll(".pav-icon-pop")].some((menu) => !menu.hidden);
      if (!open) {
        return;
      }
      event.preventDefault();
      event.stopPropagation();
      closePops();
    }, true);
    dialog.addEventListener("click", (event) => {
      const open = [...dialog.querySelectorAll(".pav-icon-pop")].some((menu) => !menu.hidden);
      if (!open) {
        return;
      }
      if (event.target instanceof Element && (event.target.closest(".pav-icon-pop") || event.target.closest("button.pav-icon-menu"))) {
        return;
      }
      closePops();
    });
    dialog.addEventListener("wheel", (event) => {
      if (!event.ctrlKey) {
        return;
      }
      if (event.target instanceof Element && event.target.closest(".pav-icon-bar")) {
        return;
      }
      if (event.target instanceof Element && (event.target.closest("#clr-picker") || event.target.closest(".pav-icon-grid") || event.target.closest(".pav-icon-pop"))) {
        return;
      }
      event.preventDefault();
      const steps = wizardNotches(event);
      if (!steps) {
        return;
      }
      const toward = typeof options.toward === "function" && options.toward();
      const next = pickerZoomAfter(slot.zoom, steps, toward);
      if (next !== slot.zoom) {
        slot.zoom = next;
        dialog.style.setProperty("--pav-wizard-zoom", String(slot.zoom));
        if (typeof options.onZoom === "function") {
          options.onZoom(slot.zoom);
        }
      }
      zoomStatus("Icon Wizard", next);
    }, { capture: true, passive: false });
    showShelf(shelf);
    paintSquare(current || activeHtml(), true);
    if (!embed) {
      show(dialog);
    }
    dialog.dataset.renderSize = String(PNG_RENDER);
  });
  if (embed) {
    pending.isDirty = () => control.isDirty();
    pending.abandon = () => control.abandon();
  }
  return pending;
}

export { paintRender, PNG_RENDER };
