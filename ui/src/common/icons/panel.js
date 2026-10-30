/* Settings → Icons. The registry table, with the Wizard in the right pane.
   Overrides save on Accept. Cancel never writes the blob. */
import { dropIconPaint } from "../iconsan.js";
import { REGISTRY, iconClass, iconNode, registryEntry, resolveIcon } from "./registry.js";
import { libraryLoadSignature } from "../iconhosts.js";
import { dropOverride, iconFormat, iconFormatLabel, iconOverrideDocument, iconPackFromText, pushRecent, putOverride } from "./snippet.js";
import { openIconWizard, utf8Base64 } from "./wizard.js";
import { growWeights } from "../column-widths.js";
import { ensureSplit, mountModuleTable } from "../../settings/split-table.js";

const CHOOSER = [
  { field: "fallback", title: "Default" },
  { field: "glyph", title: "Icon", locked: true },
  { field: "name", title: "Name", locked: true },
  { field: "format", title: "Icon Format" },
  { field: "key", title: "Key" },
];

const WIDTH_FIELDS = ["fallback", "glyph", "format"];

export async function storeSvgText(api, svg) {
  return api.asset.put({ mime: "image/svg+xml", kind: "svg", base64: utf8Base64(svg) });
}

function sectionOrder(rows) {
  const order = [];
  for (const row of rows) {
    if (!order.includes(row.set)) {
      order.push(row.set);
    }
  }
  return order;
}

export function mountIconsPanel(root, ctx) {
  const doc = root.ownerDocument;
  ensureSplit(root, {
    toolbar: "icons-toolbar",
    footer: "icons-footer",
    tree: "icons-tree",
    pane: "icons-tree-pane",
    form: "icons-form",
    split: "iconPane",
    splitLabel: "Resize icon panes",
  });
  const ui = () => ctx.ui;
  let selectedId = null;
  let editor = null;
  let generation = 0;
  let view = null;

  function themeHtml(entry) {
    return typeof ctx.themeHtml === "function" ? ctx.themeHtml(entry) : "";
  }

  function rows() {
    return REGISTRY.map((entry) => {
      const resolved = resolveIcon(entry.key, { icons: ui().icons, themeHtml: themeHtml(entry) });
      return {
        id: entry.key,
        key: entry.key,
        name: entry.label,
        set: entry.section,
        fallback: resolved.source !== "override",
        format: iconFormatLabel(iconFormat(resolved.html)),
      };
    });
  }

  function entryOf(id) {
    return REGISTRY.find((entry) => entry.key === id) || null;
  }

  async function applyResult(entry, html) {
    const had = Object.prototype.hasOwnProperty.call(ui().icons || {}, entry.key);
    const previous = had ? ui().icons[entry.key] : "";
    ui().icons = putOverride(ui().icons, entry.key, html);
    ui().iconRecent = pushRecent(ui().iconRecent, html, ui().iconRecentMax);
    dropIconPaint(previous);
    dropIconPaint(html);
    await ctx.saveNow();
    if (typeof ctx.repaint === "function") {
      ctx.repaint();
    }
    if (typeof ctx.toast === "function") {
      ctx.toast(`${entry.label} updated.`, {
        action: "Undo",
        onAction: () => {
          ui().icons = had ? putOverride(ui().icons, entry.key, previous) : dropOverride(ui().icons, entry.key);
          dropIconPaint(html);
          ctx.saveNow();
          if (typeof ctx.repaint === "function") {
            ctx.repaint();
          }
          paintRows();
          if (selectedId === entry.key) {
            mountEditor(entry);
          }
        },
      });
    }
  }

  function mountEditor(entry) {
    const gen = ++generation;
    const pane = root.querySelector("#icons-form");
    if (!pane) {
      return;
    }
    pane.replaceChildren();
    editor = null;
    if (!entry) {
      const empty = doc.createElement("p");
      empty.className = "settings-empty";
      empty.textContent = "Select an icon.";
      pane.append(empty);
      return;
    }
    const resolved = resolveIcon(entry.key, { icons: ui().icons, themeHtml: themeHtml(entry) });
    const pending = openIconWizard(pane, {
      embed: true,
      title: entry.label,
      section: entry.section,
      key: entry.key,
      current: resolved.html,
      defaultHtml: entry.default,
      smallChrome: entry.smallChrome,
      recent: ui().iconRecent || [],
      libraries: ui().iconLibraries || [],
      zoom: typeof ctx.wizardZoom === "function" ? ctx.wizardZoom() : undefined,
      onZoom: ctx.onWizardZoom,
      resultZoom: typeof ctx.resultZoom === "function" ? ctx.resultZoom() : undefined,
      onResultZoom: ctx.onResultZoom,
      flagBox: typeof ctx.flagBox === "function" ? ctx.flagBox() : ctx.flagBox,
      onFlagBox: ctx.onFlagBox,
      showHelp: ctx.showHelp,
      onHelp: ctx.onHelp,
      icons: ui().icons,
      helpThemeHtml: themeHtml({ cssVar: "--pav-icon-help" }),
      toward: ctx.toward,
      maxBytes: 8192,
      search: ctx.search,
      confirm: ctx.confirm,
      swatches: typeof ctx.swatches === "function" ? ctx.swatches() : ctx.swatches,
      storeSvg: (svg) => storeSvgText(ctx.api, svg),
      storePng: ctx.storePng,
      pickFile: ctx.pickFile,
    });
    editor = pending;
    pending.then(async (result) => {
      if (gen !== generation) {
        return;
      }
      editor = null;
      if (result && result.html) {
        await applyResult(entry, result.html);
      }
      if (gen !== generation) {
        return;
      }
      mountEditor(entryOf(selectedId));
    });
  }

  async function choose(id, choice) {
    const reveal = !choice || choice.reveal !== false;
    if (id === selectedId && editor) {
      if (view) {
        view.reselect(reveal);
      }
      return;
    }
    if (editor && typeof editor.isDirty === "function" && editor.isDirty()) {
      const ask = typeof ctx.confirm === "function" ? ctx.confirm : () => Promise.resolve(true);
      const ok = await ask({
        title: "Discard icon changes?",
        text: "Closing leaves the saved icon unchanged.",
        okLabel: "Discard",
      });
      if (!ok) {
        if (view) {
          view.reselect();
        }
        return;
      }
    }
    if (editor && typeof editor.abandon === "function") {
      editor.abandon();
    }
    selectedId = id;
    if (typeof ctx.saveTableSelected === "function") {
      ctx.saveTableSelected("icons", id);
    }
    if (view) {
      view.reselect(reveal);
    }
    mountEditor(entryOf(id));
  }

  function checkCell(documentRef, on) {
    const span = documentRef.createElement("span");
    span.className = "pav-icon-default";
    if (!on) {
      span.setAttribute("aria-label", "Changed");
      return span;
    }
    span.dataset.default = "1";
    span.setAttribute("aria-label", "Default");
    const mark = documentRef.createElement("i");
    mark.className = "icon fa-sharp fa-thin fa-check";
    mark.setAttribute("aria-hidden", "true");
    mark.draggable = false;
    span.append(mark);
    return span;
  }

  function columns(documentRef, hidden, widths) {
    const off = new Set(hidden || []);
    const saved = widths || {};
    const share = growWeights(saved, ["name", "key"], { name: 1, key: 1 });
    const defs = [
      {
        title: "Default",
        field: "fallback",
        width: saved.fallback || 72,
        ...(saved.fallback ? { lockWidth: true } : {}),
        minWidth: 64,
        resizable: true,
        hozAlign: "center",
        headerSort: false,
        formatter: (cell) => checkCell(documentRef, cell.getValue() === true),
      },
      {
        title: "Icon",
        field: "glyph",
        width: saved.glyph || 56,
        ...(saved.glyph ? { lockWidth: true } : {}),
        minWidth: 40,
        resizable: true,
        headerSort: false,
        formatter: (cell) => {
          const data = cell.getRow().getData();
          const entry = entryOf(data.id);
          const wrap = documentRef.createElement("span");
          wrap.className = "pav-icon-cell";
          const node = iconNode(data.id, {
            icons: ui().icons,
            themeHtml: entry ? themeHtml(entry) : "",
            documentRef,
          });
          if (node) {
            node.draggable = false;
            wrap.append(node);
          }
          return wrap;
        },
      },
      {
        title: "Name",
        field: "name",
        minWidth: 120,
        widthGrow: share.name,
        resizable: true,
        formatter: (cell) => {
          const span = documentRef.createElement("span");
          span.className = "pav-icon-name";
          span.textContent = cell.getValue();
          return span;
        },
      },
      {
        title: "Icon Format",
        field: "format",
        width: saved.format || 160,
        ...(saved.format ? { lockWidth: true } : {}),
        minWidth: 96,
        resizable: true,
        formatter: (cell) => {
          const span = documentRef.createElement("span");
          span.className = "pav-icon-format";
          span.textContent = cell.getValue();
          return span;
        },
      },
      {
        title: "Key",
        field: "key",
        minWidth: 80,
        widthGrow: share.key,
        resizable: true,
        formatter: (cell) => {
          const span = documentRef.createElement("span");
          span.textContent = cell.getValue();
          return span;
        },
      },
    ];
    for (const column of defs) {
      if (off.has(column.field)) {
        column.visible = false;
      }
    }
    return defs;
  }

  function toast(text) {
    if (typeof ctx.toast === "function") {
      ctx.toast(text);
    }
  }

  async function exportIcons() {
    const dialog = ctx.host && ctx.host.dialog;
    const write = ctx.api && ctx.api.exports && ctx.api.exports.writeText;
    if (!dialog || typeof dialog.save !== "function" || typeof write !== "function") {
      toast("Export is not available.");
      return;
    }
    let path;
    try {
      path = await dialog.save({
        title: "Export icons",
        defaultPath: "pavillion-icons.json",
        filters: [{ name: "JSON", extensions: ["json"] }],
      });
    } catch {
      toast("Export is not available.");
      return;
    }
    if (path == null || path === "" || Array.isArray(path)) {
      return;
    }
    try {
      const libraries = Array.isArray(ui().iconLibraries) ? ui().iconLibraries : [];
      await write({ path: String(path), text: iconOverrideDocument(ui().icons, libraries) });
    } catch {
      toast("Could not export icons.");
    }
  }

  async function importIcons() {
    const dialog = ctx.host && ctx.host.dialog;
    const read = ctx.api && ctx.api.exports && ctx.api.exports.readText;
    if (!dialog || typeof dialog.open !== "function" || typeof read !== "function") {
      toast("Import is not available.");
      return;
    }
    let path;
    try {
      path = await dialog.open({
        title: "Import icons",
        filters: [{ name: "JSON", extensions: ["json"] }],
      });
    } catch {
      toast("Import is not available.");
      return;
    }
    if (path == null || path === "" || Array.isArray(path)) {
      return;
    }
    let payload;
    try {
      payload = await read({ path: String(path) });
    } catch {
      toast("Could not import icons.");
      return;
    }
    const pack = iconPackFromText(payload && payload.text);
    if (pack == null) {
      toast("That file is not an icon export.");
      return;
    }
    if (pack.error) {
      toast(pack.error);
      return;
    }
    const ask = typeof ctx.confirm === "function" ? ctx.confirm : () => Promise.resolve(true);
    const withLibraries = pack.libraries !== null;
    const ok = await ask({
      title: "Import icons?",
      text: withLibraries
        ? "Replace saved icon changes and the libraries with this file?"
        : "Replace saved icon changes with this file?",
      okLabel: "Import",
    });
    if (!ok) {
      return;
    }
    if (editor && typeof editor.abandon === "function") {
      editor.abandon();
    }
    const previousIcons = ui().icons;
    const previousLibraries = ui().iconLibraries;
    ui().icons = pack.icons;
    if (withLibraries) {
      ui().iconLibraries = pack.libraries;
    }
    dropIconPaint();
    const saved = await ctx.saveNow();
    if (saved === false) {
      ui().icons = previousIcons;
      ui().iconLibraries = previousLibraries;
      return;
    }
    const reloadLibraries = withLibraries
      && libraryLoadSignature(previousLibraries) !== libraryLoadSignature(pack.libraries);
    if (withLibraries && typeof ctx.onLibraries === "function") {
      ctx.onLibraries();
    }
    if (reloadLibraries && typeof ctx.reload === "function") {
      ctx.reload();
    }
    if (typeof ctx.repaint === "function") {
      ctx.repaint();
    }
    paintRows();
    mountEditor(entryOf(selectedId));
  }

  async function revertAll() {
    const ask = typeof ctx.confirm === "function" ? ctx.confirm : () => Promise.resolve(true);
    const ok = await ask({
      title: "Revert all icons?",
      text: "Every saved icon returns to its theme or shipped default.",
      okLabel: "Revert all",
      danger: true,
    });
    if (!ok) {
      return;
    }
    if (editor && typeof editor.abandon === "function") {
      editor.abandon();
    }
    ui().icons = {};
    dropIconPaint();
    await ctx.saveNow();
    if (typeof ctx.repaint === "function") {
      ctx.repaint();
    }
    paintRows();
    mountEditor(entryOf(selectedId));
  }

  const known = rows();
  const savedId = typeof ctx.tableSelected === "function" ? ctx.tableSelected("icons") : "";
  if (savedId && known.some((row) => row.id === savedId)) {
    selectedId = savedId;
  }

  view = mountModuleTable(root, {
    ids: {
      toolbar: "icons-toolbar",
      footer: "icons-footer",
      tree: "icons-tree",
      pane: "icons-tree-pane",
    },
    tableId: "icons",
    title: "Icons",
    noun: "icon",
    searchId: "icons-search",
    searchLabel: "Search icons",
    searchPlaceholder: "🔍 Search icons",
    empty: "No icons.",
    chooserLabel: "Icon columns",
    chooser: CHOOSER,
    widthFields: WIDTH_FIELDS,
    growFields: ["name", "key"],
    columns,
    groupBy: "set",
    groupOrder: sectionOrder,
    getRows: rows,
    match(row, needle) {
      const q = needle.toLowerCase();
      return row.name.toLowerCase().includes(q)
        || row.key.toLowerCase().includes(q)
        || row.set.toLowerCase().includes(q)
        || String(row.format || "").toLowerCase().includes(q);
    },
    getSelected: () => selectedId,
    onSelect: (id) => {
      void choose(id);
    },
    footerEnd: [
      {
        action: "import",
        icon: iconClass("ui.import"),
        text: "Import",
        label: "Import",
        className: "pav-labeled-action",
        onClick: () => {
          void importIcons();
        },
      },
      {
        action: "export",
        icon: iconClass("ui.export"),
        text: "Export",
        label: "Export",
        className: "pav-labeled-action",
        onClick: () => {
          void exportIcons();
        },
      },
      {
        action: "revert",
        icon: iconClass("ui.revert"),
        text: "Revert all",
        label: "Revert all",
        className: "pav-labeled-action",
        onClick: () => {
          void revertAll();
        },
      },
    ],
    tableHidden: ctx.tableHidden,
    saveTableHidden: ctx.saveTableHidden,
    tableWidths: ctx.tableWidths,
    saveTableWidths: ctx.saveTableWidths,
    tableOrder: ctx.tableOrder,
    saveTableOrder: ctx.saveTableOrder,
    tableSelected: ctx.tableSelected,
    saveTableSelected: ctx.saveTableSelected,
    tableScroll: ctx.tableScroll,
    saveTableScroll: ctx.saveTableScroll,
  });

  const openingScroll = typeof ctx.tableScroll === "function" ? ctx.tableScroll("icons") : null;
  const pinned = Boolean(openingScroll && (openingScroll.top > 0 || openingScroll.left > 0));
  const openId = selectedId || (known[0] && known[0].id);
  if (openId) {
    void choose(openId, { reveal: !pinned });
  }

  function paintRows() {
    if (view) {
      view.paint();
    }
  }

  return {
    paintRows,
    registryEntry,
    setZoom(width, font) {
      if (view && typeof view.setZoom === "function") {
        view.setZoom(width, font);
      }
    },
  };
}
