/* Icon Wizard. Its own dialog. Foundry text is never inserted as HTML. */
import { colorZoomOf, mountColoris, pickerZoomAfter } from "../coloris.js";
import { paintIcon, removedText, sanitizeIconHtml } from "../iconsan.js";
import { SYSTEM_ICONS } from "./registry.js";
import {
  FA_FAMILIES,
  FA_SIZES,
  FA_STYLES,
  PNG_RENDER,
  addClassAt,
  assetSnippet,
  bakeSvg,
  foundryFromLibrary,
  guessMode,
  librarySnippet,
  needsFoundry,
  paintRender,
  wrapRange,
} from "./snippet.js";

const MODE_LABELS = {
  library: "Icon Library",
  foundry: "Icon Foundry",
  svg: "SVG",
  png: "PNG",
};

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
  label.append(doc.createTextNode(text));
  label.append(control);
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

export function openIconWizard(parent, options = {}) {
  if (!(parent instanceof Element)) {
    return Promise.resolve(null);
  }
  const doc = parent.ownerDocument;
  const modes = (Array.isArray(options.modes) && options.modes.length
    ? options.modes
    : ["library", "foundry", "svg", "png"]
  ).filter((mode) => MODE_LABELS[mode]);
  const maxBytes = Number.isInteger(options.maxBytes) ? options.maxBytes : 8192;
  const current = typeof options.current === "string" ? options.current : "";
  const defaultHtml = typeof options.defaultHtml === "string" ? options.defaultHtml : "";
  let mode = modes.includes(guessMode(current, options.assetKind))
    ? guessMode(current, options.assetKind)
    : modes[0];

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
  };
  const image = {
    svg: { tx: 0, ty: 0, scale: 1, rotate: 0, flipH: false, flipV: false, crop: true, background: "none", source: "" },
    png: { tx: 0, ty: 0, scale: 1, rotate: 0, flipH: false, flipV: false, crop: true, background: "none", source: "" },
  };
  let svgHtml = mode === "svg" ? current : "";
  let pngHtml = mode === "png" ? current : "";
  let foundryText = current;
  let settled = false;
  let edited = false;
  let wizardZoom = options.zoom === undefined || options.zoom === null || options.zoom === ""
    ? 1
    : colorZoomOf(options.zoom);
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
    dialog.style.setProperty("--pav-wizard-zoom", String(wizardZoom));
    const head = doc.createElement("div");
    head.className = "pav-icon-wizard-head";
    const title = doc.createElement("h2");
    title.className = "pav-dialog-title";
    const section = options.section ? ` — ${options.section}` : "";
    title.textContent = `${options.title || "Icon"}${section}`;
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
    head.append(title, closeBtn);
    dialog.append(head);
    if (options.key) {
      const key = doc.createElement("p");
      key.className = "pav-icon-key";
      key.textContent = options.key;
      dialog.append(key);
    }

    const layout = doc.createElement("div");
    layout.className = "pav-icon-layout";
    const previewCol = doc.createElement("div");
    previewCol.className = "pav-icon-preview-col";
    const square = doc.createElement("div");
    square.className = "pav-icon-square";
    square.setAttribute("aria-hidden", "true");
    previewCol.append(square);
    let chipLight = null;
    let chipDark = null;
    if (options.smallChrome) {
      const chips = doc.createElement("div");
      chips.className = "pav-icon-chips";
      chipLight = doc.createElement("div");
      chipLight.className = "pav-icon-chip is-light";
      chipDark = doc.createElement("div");
      chipDark.className = "pav-icon-chip is-dark";
      chips.append(chipLight, chipDark);
      previewCol.append(chips);
    }

    const main = doc.createElement("div");
    main.className = "pav-icon-main";
    const bar1 = doc.createElement("div");
    bar1.className = "button-bar pav-icon-bar";
    bar1.setAttribute("role", "toolbar");
    const modeButtons = new Map();
    for (const name of modes) {
      const tab = button(doc, MODE_LABELS[name], "pav-icon-mode");
      tab.dataset.mode = name;
      tab.setAttribute("aria-pressed", name === mode ? "true" : "false");
      modeButtons.set(name, tab);
      bar1.append(tab);
    }
    const acceptBtn = button(doc, "Accept", "pav-dialog-ok");
    const cancelBtn = button(doc, "Cancel", "pav-dialog-cancel");
    bar1.append(acceptBtn, cancelBtn);

    const bar2 = doc.createElement("div");
    bar2.className = "button-bar pav-icon-bar pav-icon-presets";
    bar2.setAttribute("role", "toolbar");
    bar2.append(button(doc, "Default", "pav-icon-preset"));
    for (const system of SYSTEM_ICONS) {
      const shelf = button(doc, system.label, "pav-icon-preset");
      shelf.dataset.system = system.id;
      bar2.append(shelf);
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
      bar2.append(chip);
    }

    const work = doc.createElement("div");
    work.className = "pav-icon-work";
    const note = doc.createElement("p");
    note.className = "pav-icon-note";
    main.append(bar1, bar2, work, note);
    layout.append(previewCol, main);
    dialog.append(layout);
    parent.append(dialog);

    let foundry = null;
    const pages = {};

    function activeHtml() {
      if (mode === "library") {
        return needsFoundry(library) ? "" : librarySnippet(library);
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
      const painted = paintIcon(html || "", doc);
      if (!html) {
        note.textContent = needsFoundry(library) && mode === "library" ? "Open in Foundry" : "";
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
        if (chipLight) {
          chipLight.replaceChildren(node.cloneNode(true));
          chipDark.replaceChildren(node.cloneNode(true));
        }
      }
    }

    function showPage(name) {
      mode = name;
      for (const [key, tab] of modeButtons) {
        tab.setAttribute("aria-pressed", key === name ? "true" : "false");
      }
      for (const [key, page] of Object.entries(pages)) {
        page.hidden = key !== name;
      }
      if (name !== "foundry") {
        paintSquare(activeHtml(), true);
      }
    }

    function loadSnippet(html) {
      edited = true;
      foundryText = html;
      if (foundry) {
        foundry.set(html);
      }
      const next = guessMode(html, options.assetKind);
      if (next === "svg") {
        svgHtml = html;
      } else if (next === "png") {
        pngHtml = html;
      } else if (html.includes("pav-system:mark")) {
        library.system = "mark";
      }
      if (modes.includes(next)) {
        showPage(next);
      }
      paintSquare(html, true);
    }

    function libraryPage() {
      const page = doc.createElement("div");
      const libs = (options.libraries || []).filter((item) => item && item.enabled);
      library.libraryId = (libs[0] && libs[0].id) || "fontawesome";
      const libSelect = selectOf(doc, libs.map((item) => ({ id: item.id, label: item.name || item.id })), library.libraryId);
      const family = selectOf(doc, FA_FAMILIES, library.family);
      const style = selectOf(doc, FA_STYLES, library.style);
      const materialFace = selectOf(doc, [
        { id: "sharp", label: "Sharp" },
        { id: "outlined", label: "Outlined" },
        { id: "rounded", label: "Rounded" },
      ], library.materialStyle);
      const familyField = fieldLabel(doc, "Family", family);
      const styleField = fieldLabel(doc, "Style", style);
      const materialField = fieldLabel(doc, "Material style", materialFace);
      const search = doc.createElement("input");
      search.type = "search";
      search.className = "pav-icon-search";
      search.setAttribute("aria-label", "Search icons");
      const grid = doc.createElement("div");
      grid.className = "pav-icon-grid";
      const color = doc.createElement("input");
      color.type = "text";
      color.placeholder = "#800000";
      color.setAttribute("aria-label", "Color");
      void mountColoris(color, options.swatches);
      const size = selectOf(doc, FA_SIZES.filter(Boolean).concat([""]).map((id) => ({ id, label: id || "Size" })), "");
      const rotate = selectOf(doc, [0, 90, 180, 270].map((n) => ({ id: String(n), label: n ? `Rotate ${n}` : "Rotate" })), "0");
      const flip = selectOf(doc, [
        { id: "", label: "Flip" },
        { id: "h", label: "Flip horizontal" },
        { id: "v", label: "Flip vertical" },
      ], "");
      const fixed = doc.createElement("input");
      fixed.type = "checkbox";
      const foundryBtn = button(doc, "Open in Foundry", "pav-icon-tool");
      foundryBtn.hidden = true;
      const refresh = () => {
        edited = true;
        library.libraryId = libSelect.value;
        library.family = family.value;
        library.style = style.value;
        library.materialStyle = materialFace.value;
        library.color = color.value.trim();
        library.size = size.value;
        library.rotate = Number(rotate.value) || 0;
        library.flip = flip.value;
        library.fixed = fixed.checked;
        const material = library.libraryId === "material";
        familyField.hidden = material;
        styleField.hidden = material;
        materialField.hidden = !material;
        foundryBtn.hidden = material || !needsFoundry(library);
        paintSquare(activeHtml(), true);
      };
      for (const control of [libSelect, family, style, materialFace, color, size, rotate, flip, fixed]) {
        control.addEventListener("input", refresh);
        control.addEventListener("change", refresh);
      }
      search.addEventListener("keydown", (event) => {
        if (event.key === "Enter") {
          event.preventDefault();
          runSearch();
        }
      });
      async function runSearch() {
        if (typeof options.search !== "function") {
          return;
        }
        library.query = search.value;
        let rows = [];
        try {
          const payload = await options.search({ id: library.libraryId, query: library.query, limit: 80 });
          rows = Array.isArray(payload) ? payload : (payload && payload.icons) || [];
        } catch (err) {
          note.textContent = err instanceof Error ? err.message : "Search failed.";
          return;
        }
        grid.replaceChildren();
        for (const row of rows || []) {
          const hit = button(doc, row.label || row.name, "pav-icon-hit");
          hit.addEventListener("click", () => {
            library.system = "";
            library.name = row.name;
            refresh();
          });
          hit.addEventListener("dblclick", () => {
            library.system = "";
            library.name = row.name;
            refresh();
            accept();
          });
          grid.append(hit);
        }
      }
      const searchBtn = button(doc, "Search", "pav-icon-tool");
      searchBtn.addEventListener("click", () => runSearch());
      foundryBtn.addEventListener("click", () => {
        edited = true;
        const nested = foundryFromLibrary(library);
        foundryText = nested;
        if (foundry) {
          foundry.set(nested);
        }
        showPage("foundry");
        paintSquare(nested, true);
      });
      materialField.hidden = library.libraryId !== "material";
      page.append(
        fieldLabel(doc, "Library", libSelect),
        familyField,
        styleField,
        materialField,
        search,
        searchBtn,
        grid,
        fieldLabel(doc, "Color", color),
        fieldLabel(doc, "Size", size),
        fieldLabel(doc, "Rotate", rotate),
        fieldLabel(doc, "Flip", flip),
        fieldLabel(doc, "Fixed width", fixed),
        foundryBtn,
      );
      return page;
    }

    function foundryPage() {
      const page = doc.createElement("div");
      const tools = doc.createElement("div");
      tools.className = "pav-icon-tools";
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
      page.append(tools, host);
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
      const pick = button(doc, kind === "svg" ? "Pick SVG" : "Pick PNG", "pav-icon-tool");
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
      const apply = button(doc, "Apply view", "pav-icon-tool");
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
      pick.addEventListener("click", () => importImage(kind));
      apply.addEventListener("click", () => {
        read();
        rebake(kind);
      });
      page.append(pick, fieldLabel(doc, "Scale", scale), fieldLabel(doc, "Rotate", rotate), flipH, flipV, turn, apply);
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
      if (!bytes && typeof options.renderPng !== "function") {
        note.textContent = "Pick a PNG.";
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

    function wizardNotches(event) {
      const delta = event.deltaY;
      if (!Number.isFinite(delta) || delta === 0) {
        return 0;
      }
      if (event.deltaMode !== 0 || Math.abs(delta) >= 100) {
        wheelRemainder = 0;
        return delta > 0 ? 1 : -1;
      }
      wheelRemainder += delta;
      if (Math.abs(wheelRemainder) < 100) {
        return 0;
      }
      const steps = wheelRemainder > 0 ? 1 : -1;
      wheelRemainder -= steps * 100;
      return steps;
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
    for (const [name, tab] of modeButtons) {
      tab.addEventListener("click", () => showPage(name));
    }
    bar2.addEventListener("click", (event) => {
      const target = event.target instanceof Element ? event.target.closest("button") : null;
      if (!target || !bar2.contains(target)) {
        return;
      }
      if (target.dataset.system) {
        library.system = target.dataset.system;
        loadSnippet(SYSTEM_ICONS.find((item) => item.id === target.dataset.system).html);
        return;
      }
      if (target.dataset.snippet) {
        loadSnippet(target.dataset.snippet);
        return;
      }
      if (target.textContent === "Default") {
        loadSnippet(defaultHtml);
      }
    });
    acceptBtn.addEventListener("click", accept);
    cancelBtn.addEventListener("click", () => requestClose());
    closeBtn.addEventListener("click", () => requestClose());
    if (!embed) {
      dialog.addEventListener("cancel", (event) => {
        event.preventDefault();
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
    dialog.addEventListener("wheel", (event) => {
      if (!event.ctrlKey) {
        return;
      }
      if (event.target instanceof Element && event.target.closest("#clr-picker")) {
        return;
      }
      event.preventDefault();
      const steps = wizardNotches(event);
      if (!steps) {
        return;
      }
      const toward = typeof options.toward === "function" && options.toward();
      const next = pickerZoomAfter(wizardZoom, steps, toward);
      if (next === wizardZoom) {
        return;
      }
      wizardZoom = next;
      dialog.style.setProperty("--pav-wizard-zoom", String(wizardZoom));
      if (typeof options.onZoom === "function") {
        options.onZoom(wizardZoom);
      }
    }, { capture: true, passive: false });
    showPage(mode);
    if (mode !== "foundry") {
      paintSquare(current || activeHtml(), true);
    }
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
