/* Settings → Icons. Overrides save on Apply. Cancel never writes the blob. */
import { libraryUrlError } from "../iconhosts.js";
import { dropIconPaint } from "../iconsan.js";
import { REGISTRY, iconNode, registryEntry, resolveIcon } from "./registry.js";
import { dropOverride, pushRecent, putOverride } from "./snippet.js";
import { openIconWizard, utf8Base64 } from "./wizard.js";

function el(doc, tag, className, text) {
  const node = doc.createElement(tag);
  if (className) {
    node.className = className;
  }
  if (text) {
    node.textContent = text;
  }
  return node;
}

export async function storeSvgText(api, svg) {
  return api.asset.put({ mime: "image/svg+xml", kind: "svg", base64: utf8Base64(svg) });
}

export function mountIconsPanel(host, ctx) {
  const doc = host.ownerDocument;
  host.replaceChildren();
  const ui = () => ctx.ui;

  const libsTitle = el(doc, "h3", "pav-icon-heading", "Libraries");
  const libList = el(doc, "div", "pav-icon-libraries");
  const libError = el(doc, "p", "pav-icon-note", "");
  const addRow = el(doc, "div", "pav-icon-add");
  const name = doc.createElement("input");
  name.type = "text";
  name.placeholder = "Name";
  name.setAttribute("aria-label", "Library name");
  const type = doc.createElement("select");
  for (const [value, label] of [["script", "Script"], ["stylesheet", "Stylesheet"]]) {
    const option = doc.createElement("option");
    option.value = value;
    option.textContent = label;
    type.append(option);
  }
  const url = doc.createElement("input");
  url.type = "url";
  url.placeholder = "https://";
  url.setAttribute("aria-label", "Library URL");
  const add = el(doc, "button", "", "Add");
  add.type = "button";
  addRow.append(name, type, url, add);
  const rebuild = el(doc, "p", "muted", "A new host needs a rebuild. Only kit.fontawesome.com and fonts.googleapis.com can be added here.");

  const capLabel = el(doc, "label", "pav-icon-field", "Recent icons");
  const cap = doc.createElement("input");
  cap.type = "number";
  cap.min = "1";
  cap.max = "48";
  cap.value = String(ui().iconRecentMax || 12);
  capLabel.append(cap);

  const tabs = el(doc, "div", "pav-icon-tabs");
  tabs.setAttribute("role", "tablist");
  tabs.setAttribute("aria-label", "Icon sections");
  const filter = doc.createElement("input");
  filter.type = "search";
  filter.className = "pav-icon-search";
  filter.placeholder = "Search icons";
  filter.setAttribute("aria-label", "Search icons");
  const revertAll = el(doc, "button", "", "Revert all");
  revertAll.type = "button";
  const rows = el(doc, "div", "pav-icon-registry");
  const assetsTitle = el(doc, "h3", "pav-icon-heading", "Assets");
  const assetList = el(doc, "div", "pav-icon-assets");
  const assetNote = el(doc, "p", "pav-icon-note", "");
  const sectionNames = [];
  for (const entry of REGISTRY) {
    if (!sectionNames.includes(entry.section)) {
      sectionNames.push(entry.section);
    }
  }
  let currentSection = sectionNames[0] || "";

  host.append(libsTitle, libList, addRow, libError, rebuild, capLabel, tabs, filter, revertAll, rows, assetsTitle, assetList, assetNote);

  function paintTabs() {
    if (!sectionNames.includes(currentSection)) {
      currentSection = sectionNames[0] || "";
    }
    tabs.replaceChildren();
    for (const name of sectionNames) {
      const tab = el(doc, "button", "pav-icon-tab", name);
      tab.type = "button";
      tab.setAttribute("role", "tab");
      tab.setAttribute("aria-selected", name === currentSection ? "true" : "false");
      tab.addEventListener("click", () => {
        currentSection = name;
        paintTabs();
        paintRows();
      });
      tabs.append(tab);
    }
  }

  function paintLibraries() {
    libList.replaceChildren();
    const list = Array.isArray(ui().iconLibraries) ? ui().iconLibraries : [];
    list.forEach((entry, index) => {
      const row = el(doc, "div", "pav-icon-library");
      const enabled = doc.createElement("input");
      enabled.type = "checkbox";
      enabled.checked = entry.enabled === true;
      enabled.setAttribute("aria-label", `Enable ${entry.name || entry.id}`);
      const label = el(doc, "span", "", entry.name || entry.id);
      const address = el(doc, "span", "muted", entry.url || "");
      const problem = libraryUrlError(entry.type, entry.url);
      const err = el(doc, "span", "pav-icon-note", problem);
      const remove = el(doc, "button", "", "Remove");
      remove.type = "button";
      enabled.addEventListener("change", () => {
        const next = list.slice();
        next[index] = { ...entry, enabled: enabled.checked };
        const bad = libraryUrlError(next[index].type, next[index].url);
        if (bad) {
          libError.textContent = bad;
          enabled.checked = entry.enabled === true;
          return;
        }
        ui().iconLibraries = next;
        libError.textContent = "";
        ctx.saveNow();
        if (typeof ctx.loadLibraries === "function") {
          ctx.loadLibraries(next);
        }
      });
      remove.addEventListener("click", () => {
        ui().iconLibraries = list.filter((_, at) => at !== index);
        ctx.saveNow();
        paintLibraries();
        libError.textContent = entry.type === "script" ? "Reload the window to unload a script." : "";
      });
      row.append(enabled, label, address, err, remove);
      libList.append(row);
    });
  }

  add.addEventListener("click", () => {
    const problem = libraryUrlError(type.value, url.value.trim());
    if (problem) {
      libError.textContent = problem;
      return;
    }
    const id = (name.value.trim().toLowerCase().replace(/[^a-z0-9-]+/g, "-") || "library").slice(0, 32);
    const list = Array.isArray(ui().iconLibraries) ? ui().iconLibraries.slice() : [];
    if (list.some((entry) => entry.id === id)) {
      libError.textContent = "an icon library id is repeated";
      return;
    }
    list.push({ id, name: name.value.trim() || id, type: type.value, url: url.value.trim(), enabled: true });
    ui().iconLibraries = list;
    libError.textContent = "";
    name.value = "";
    url.value = "";
    ctx.saveNow();
    if (typeof ctx.loadLibraries === "function") {
      ctx.loadLibraries(list);
    }
    paintLibraries();
  });

  cap.addEventListener("change", () => {
    const next = Math.min(48, Math.max(1, Math.round(Number(cap.value) || 12)));
    cap.value = String(next);
    ui().iconRecentMax = next;
    ui().iconRecent = (ui().iconRecent || []).slice(0, next);
    ctx.saveNow();
  });

  function themeHtml(entry) {
    return typeof ctx.themeHtml === "function" ? ctx.themeHtml(entry) : "";
  }

  function paintRows() {
    const query = filter.value.trim().toLowerCase();
    rows.replaceChildren();
    const table = el(doc, "table", "pav-icon-table");
    const head = doc.createElement("thead");
    const headRow = doc.createElement("tr");
    for (const title of ["Default", "Icon", "Name"]) {
      headRow.append(el(doc, "th", "", title));
    }
    head.append(headRow);
    const body = doc.createElement("tbody");
    for (const entry of REGISTRY) {
      if (entry.section !== currentSection) {
        continue;
      }
      if (query && !entry.label.toLowerCase().includes(query) && !entry.key.toLowerCase().includes(query)) {
        continue;
      }
      const resolved = resolveIcon(entry.key, { icons: ui().icons, themeHtml: themeHtml(entry) });
      const row = doc.createElement("tr");
      row.dataset.iconKey = entry.key;
      row.tabIndex = 0;
      const markCell = el(doc, "td", "pav-icon-default");
      if (resolved.source !== "override") {
        markCell.dataset.default = "1";
        const mark = doc.createElement("i");
        mark.className = "icon fa-sharp fa-thin fa-check";
        mark.setAttribute("aria-hidden", "true");
        mark.draggable = false;
        markCell.append(mark);
        markCell.setAttribute("aria-label", "Default");
      } else {
        markCell.setAttribute("aria-label", "Changed");
      }
      const glyph = el(doc, "td", "pav-icon-glyph");
      const node = iconNode(entry.key, {
        icons: ui().icons,
        themeHtml: themeHtml(entry),
        documentRef: doc,
      });
      if (node) {
        node.draggable = false;
        glyph.append(node);
      }
      const name = el(doc, "td", "pav-icon-name", entry.label);
      const open = () => openRow(entry);
      row.addEventListener("click", open);
      row.addEventListener("keydown", (event) => {
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          open();
        }
      });
      row.append(markCell, glyph, name);
      body.append(row);
    }
    table.append(head, body);
    rows.append(table);
  }

  async function openRow(entry) {
    const resolved = resolveIcon(entry.key, { icons: ui().icons, themeHtml: themeHtml(entry) });
    const result = await openIconWizard(host, {
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
      toward: ctx.toward,
      maxBytes: 8192,
      search: ctx.search,
      confirm: ctx.confirm,
      swatches: ctx.swatches,
      storeSvg: (svg) => storeSvgText(ctx.api, svg),
      storePng: ctx.storePng,
      pickFile: ctx.pickFile,
    });
    if (!result) {
      return;
    }
    const had = Object.prototype.hasOwnProperty.call(ui().icons || {}, entry.key);
    const previous = had ? ui().icons[entry.key] : "";
    ui().icons = putOverride(ui().icons, entry.key, result.html);
    ui().iconRecent = pushRecent(ui().iconRecent, result.html, ui().iconRecentMax);
    dropIconPaint(previous);
    dropIconPaint(result.html);
    await ctx.saveNow();
    if (typeof ctx.repaint === "function") {
      ctx.repaint();
    }
    paintRows();
    if (typeof ctx.toast === "function") {
      ctx.toast(`${entry.label} updated.`, {
        action: "Undo",
        onAction: () => {
          ui().icons = had ? putOverride(ui().icons, entry.key, previous) : dropOverride(ui().icons, entry.key);
          dropIconPaint(result.html);
          ctx.saveNow();
          if (typeof ctx.repaint === "function") {
            ctx.repaint();
          }
          paintRows();
        },
      });
    }
  }

  revertAll.addEventListener("click", async () => {
    const ok = typeof ctx.confirm === "function"
      ? await ctx.confirm({ title: "Revert all icons?", text: "Every saved icon returns to its theme or shipped default.", okLabel: "Revert all", danger: true })
      : true;
    if (!ok) {
      return;
    }
    ui().icons = {};
    dropIconPaint();
    await ctx.saveNow();
    if (typeof ctx.repaint === "function") {
      ctx.repaint();
    }
    paintRows();
  });

  filter.addEventListener("input", paintRows);
  paintLibraries();
  paintTabs();
  paintRows();
  paintAssets();

  async function paintAssets() {
    if (!ctx.api || !ctx.api.asset || typeof ctx.api.asset.list !== "function") {
      return;
    }
    let listed;
    try {
      listed = await ctx.api.asset.list();
    } catch {
      assetNote.textContent = "";
      return;
    }
    assetList.replaceChildren();
    assetNote.textContent = listed && listed.warn ? "Icon assets are over 128 MB." : "";
    for (const asset of (listed && listed.assets) || []) {
      const row = el(doc, "div", "pav-icon-asset");
      row.append(el(doc, "span", "", `${asset.kind} · ${asset.bytes} bytes`));
      const del = el(doc, "button", "", "Delete");
      del.type = "button";
      del.addEventListener("click", async () => {
        await ctx.api.asset.delete({ id: asset.id });
        paintAssets();
      });
      row.append(del);
      assetList.append(row);
    }
  }

  return { paintRows, openRow, registryEntry };
}
