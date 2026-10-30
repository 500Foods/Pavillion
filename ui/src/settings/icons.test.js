/**
 * @vitest-environment jsdom
 */
import { expect, test, vi } from "vitest";
import { defaultLibraries, FA_KIT_URL, MATERIAL_URL } from "../common/iconhosts.js";
import { cleanOrder, orderColumns } from "./split-table.js";

/* Tabulator does not lay out rows under jsdom. This fake draws one button per
   row, runs the real formatters, and filters the way the settings tables do. */
vi.mock("../common/table.js", async (importOriginal) => {
  const actual = await importOriginal();
  return {
  INDICATOR_WIDTH: actual.INDICATOR_WIDTH,
  createTable(el, options) {
    el.pavTableOptions = options;
    let rowsNow = options.data;
    let allRows = options.data;
    let filterFn = null;
    const rowKey = options.rowId || "id";
    const columns = options.columns.filter((column) => column.visible !== false);
    const grouped = (data) => {
      const groups = new Map();
      const seen = [];
      for (const row of data) {
        const key = options.groupBy ? row[options.groupBy] : "";
        if (!groups.has(key)) {
          groups.set(key, []);
          seen.push(key);
        }
        groups.get(key).push(row);
      }
      let keys = seen;
      if (typeof options.groupOrder === "function") {
        const wanted = options.groupOrder(data);
        if (Array.isArray(wanted)) {
          keys = wanted.filter((key) => groups.has(key));
          for (const key of seen) {
            if (!keys.includes(key)) {
              keys.push(key);
            }
          }
        }
      }
      return keys.map((key) => [key, groups.get(key)]);
    };
    const drawRow = (row) => {
      const button = document.createElement("button");
      button.type = "button";
      button.dataset.rowId = row[rowKey];
      for (const column of columns) {
        const cell = { getValue: () => row[column.field], getRow: () => ({ getData: () => row }) };
        const out = column.formatter ? column.formatter(cell) : String(row[column.field] ?? "");
        const span = document.createElement("span");
        span.dataset.field = column.field;
        if (out instanceof Node) {
          span.append(out);
        } else if (out !== "" && out != null) {
          span.textContent = String(out);
        }
        button.append(span);
      }
      button.addEventListener("click", () => options.onRowClick(row));
      return button;
    };
    const draw = (input) => {
      allRows = input;
      const data = filterFn ? input.filter(filterFn) : input;
      rowsNow = data;
      el.replaceChildren();
      for (const [key, rows] of grouped(data)) {
        if (options.groupBy) {
          const header = document.createElement("div");
          header.className = "tabulator-group";
          header.dataset.group = key;
          header.append(options.groupHeader ? options.groupHeader(key, rows.length, rows) : key);
          el.append(header);
        }
        for (const row of rows) {
          el.append(drawRow(row));
        }
      }
      if (options.onRender) {
        options.onRender();
      }
    };
    draw(options.data);
    return {
      setData: draw,
      setFilter(fn) {
        filterFn = fn;
        draw(allRows);
      },
      select() {},
      ids: () => grouped(rowsNow).flatMap((entry) => entry[1].map((row) => row[rowKey])),
      setColumnVisible() {},
      setColumnWidth() {},
      destroy() {
        el.replaceChildren();
      },
    };
  },
  };
});

const { mountIconsPanel } = await import("../common/icons/panel.js");
const { mountLibrariesPanel } = await import("./libraries.js");
const { mountAssetsPanel } = await import("./assets.js");

function tick() {
  return new Promise((resolve) => setTimeout(resolve, 0));
}

async function flush() {
  for (let i = 0; i < 6; i += 1) {
    await tick();
  }
}

function buttonNamed(root, text) {
  return [...root.querySelectorAll("button")].find((node) => node.textContent === text || node.getAttribute("aria-label") === text);
}

function iconCtx(ui, extra = {}) {
  return {
    ui,
    api: { asset: { list: async () => { throw new Error("none"); } } },
    saveNow() {
      return Promise.resolve(true);
    },
    confirm: async () => true,
    tableHidden: () => [],
    tableWidths: () => ({}),
    saveTableHidden() {},
    saveTableWidths() {},
    ...extra,
  };
}

test("Icons groups the registry, checks defaults, and searches by name", () => {
  const ui = {
    icons: { "nav.update": '<i class="fa-solid fa-star"></i>' },
    iconRecent: [],
    iconRecentMax: 12,
    iconLibraries: [],
  };
  const host = document.createElement("div");
  document.body.append(host);
  mountIconsPanel(host, iconCtx(ui, { confirm: async () => false }));
  expect(host.querySelector('[role="tab"]')).toBeNull();
  expect(host.querySelector('[data-group="Nav"]')).not.toBeNull();
  expect(host.querySelector('[data-group="Mail list"]')).not.toBeNull();
  expect(host.querySelector('[data-row-id="nav.mail"] .pav-icon-default').dataset.default).toBe("1");
  expect(host.querySelector('[data-row-id="nav.update"] .pav-icon-default').dataset.default).toBeUndefined();
  expect(host.querySelector('[data-row-id="nav.mail"] .pav-icon-format').textContent).toBe("PNG");
  expect(host.querySelector('[data-row-id="nav.update"] .pav-icon-format').textContent).toBe("Font Awesome");
  expect(host.querySelector('.pav-icon-wizard .pav-icon-mode[aria-pressed="true"]').textContent).toBe("PNG");
  expect(host.querySelector(".pav-icon-crumb-group").textContent).toBe("Nav");
  expect(host.querySelector(".pav-icon-crumb-name").textContent).toBe("Mail");
  expect(host.querySelector(".pav-icon-crumb-key").textContent).toBe("nav.mail");
  expect(host.querySelector(".pav-icon-crumb-format").textContent).toBe("PNG");
  expect(host.querySelector(".pav-icon-wizard.is-embedded")).not.toBeNull();
  expect(host.querySelector(".pav-icon-close")).toBeNull();
  const search = host.querySelector("#icons-search");
  expect(search.placeholder).toBe("🔍 Search icons");
  expect(search.getAttribute("aria-label")).toBe("Search icons");
  search.value = "PNG";
  search.dispatchEvent(new Event("input", { bubbles: true }));
  expect(host.querySelector('[data-row-id="nav.mail"]')).not.toBeNull();
  expect(host.querySelector('[data-row-id="nav.update"]')).toBeNull();
  search.value = "flag";
  search.dispatchEvent(new Event("input", { bubbles: true }));
  expect(host.querySelector('[data-row-id="nav.mail"]')).toBeNull();
  expect(host.querySelector('[data-row-id="mail.list.flag"] .pav-icon-name').textContent).toBe("Flag");
  host.remove();
});

test("Accept stores only that key, a clean Cancel does not ask, and Revert all clears it", async () => {
  const ui = {
    icons: {},
    iconRecent: [],
    iconRecentMax: 12,
    iconLibraries: [{ id: "fontawesome", name: "Font Awesome", type: "script", url: FA_KIT_URL, enabled: true }],
    colorSwatches: [],
  };
  const saves = [];
  let asked = 0;
  const host = document.createElement("div");
  document.body.append(host);
  mountIconsPanel(host, iconCtx(ui, {
    saveNow() {
      saves.push(structuredClone(ui.icons));
      return Promise.resolve(true);
    },
    confirm: async () => {
      asked += 1;
      return true;
    },
    search: async () => [{ name: "up-down", label: "Up down" }],
  }));
  host.querySelector('[data-row-id="nav.update"]').click();
  let wizard = host.querySelector(".pav-icon-wizard");
  const iconSearch = wizard.querySelector(".pav-module-search");
  iconSearch.value = "up";
  iconSearch.dispatchEvent(new Event("input"));
  await new Promise((resolve) => setTimeout(resolve, 600));
  wizard.querySelector(".pav-icon-hit").click();
  buttonNamed(wizard, "Accept").click();
  await flush();
  expect(Object.keys(ui.icons)).toEqual(["nav.update"]);
  expect(ui.icons["nav.update"]).toContain("fa-up-down");
  expect(saves.at(-1)["nav.update"]).toContain("fa-up-down");
  expect(saves.at(-1)["nav.help"]).toBeUndefined();

  asked = 0;
  host.querySelector('[data-row-id="nav.help"]').click();
  wizard = host.querySelector(".pav-icon-wizard");
  buttonNamed(wizard, "Cancel").click();
  await flush();
  expect(asked).toBe(0);
  expect(ui.icons["nav.help"]).toBeUndefined();
  expect(host.querySelector(".pav-icon-wizard")).not.toBeNull();

  buttonNamed(host, "Revert all").click();
  await flush();
  expect(asked).toBe(1);
  expect(ui.icons["nav.update"]).toBeUndefined();
  expect(buttonNamed(host, "Revert all").querySelector("i").className).toContain("fa-rotate-left");
  expect(buttonNamed(host, "Import").querySelector("i").className).toContain("fa-upload");
  expect(buttonNamed(host, "Export").querySelector("i").className).toContain("fa-download");
  for (const name of ["Import", "Export", "Revert all"]) {
    expect(buttonNamed(host, name).classList.contains("pav-labeled-action")).toBe(true);
  }
  host.remove();
});

test("Import and Export move the saved icon changes", async () => {
  const ui = {
    icons: { "nav.contacts": '<i class="fa-solid fa-star"></i>' },
    iconRecent: ['<i class="fa-solid fa-gear"></i>'],
    iconRecentMax: 12,
    iconLibraries: [],
  };
  const files = new Map();
  const confirms = [];
  const saves = [];
  const host = document.createElement("div");
  document.body.append(host);
  mountIconsPanel(host, iconCtx(ui, {
    saveNow() {
      saves.push(structuredClone(ui.icons));
      return Promise.resolve(true);
    },
    confirm: async (ask) => {
      confirms.push(ask);
      return true;
    },
    api: {
      asset: { list: async () => { throw new Error("none"); } },
      exports: {
        async writeText({ path, text }) {
          files.set(path, text);
          return { bytes: text.length };
        },
        async readText({ path }) {
          return { text: files.get(path) };
        },
      },
    },
    host: {
      dialog: {
        async save() {
          return "/tmp/pavillion-icons.json";
        },
        async open() {
          return "/tmp/pavillion-icons.json";
        },
      },
    },
  }));
  buttonNamed(host, "Export").click();
  await flush();
  const text = files.get("/tmp/pavillion-icons.json");
  expect(text).toContain('"kind": "pavillion.icons"');
  expect(text).toContain("nav.contacts");
  expect(text).not.toContain("fa-gear");
  ui.icons = {};
  buttonNamed(host, "Import").click();
  await flush();
  expect(confirms.at(-1).text).toBe("Replace saved icon changes with this file?");
  expect(ui.icons["nav.contacts"]).toContain("fa-star");
  expect(saves.at(-1)["nav.contacts"]).toContain("fa-star");
  host.remove();
});

test("switching icons asks once after an edit", async () => {
  const ui = { icons: {}, iconRecent: [], iconRecentMax: 12, iconLibraries: [] };
  let asked = 0;
  const host = document.createElement("div");
  document.body.append(host);
  mountIconsPanel(host, iconCtx(ui, {
    confirm: async () => {
      asked += 1;
      return false;
    },
  }));
  host.querySelector('[data-row-id="nav.update"]').click();
  await flush();
  expect(host.querySelector('.pav-icon-wizard .pav-icon-mode[aria-pressed="true"]').textContent).toBe("Font Awesome");
  const family = [...host.querySelectorAll("label")].find((node) => node.firstChild && node.firstChild.textContent === "Family").querySelector("select");
  family.value = family.options[1].value;
  family.dispatchEvent(new Event("change", { bubbles: true }));
  host.querySelector('[data-row-id="nav.contacts"]').click();
  await flush();
  expect(asked).toBe(1);
  expect(host.querySelector(".pav-icon-crumb-group").textContent).toBe("Nav");
  expect(host.querySelector(".pav-icon-crumb-name").textContent).toBe("Update");
  expect(host.querySelector(".pav-icon-crumb-key").textContent).toBe("nav.update");
  expect(host.querySelector(".pav-icon-crumb-format").textContent).toBe("Font Awesome");
  host.remove();
});

test("a library name or Font Awesome style does not reload, and a new kit does", async () => {
  const ui = {
    iconLibraries: [
      { id: "fontawesome", name: "Font Awesome", type: "script", url: FA_KIT_URL, enabled: true },
      { id: "material", name: "Material Symbols", type: "stylesheet", url: MATERIAL_URL, enabled: false },
    ],
  };
  let reloads = 0;
  const host = document.createElement("div");
  document.body.append(host);
  mountLibrariesPanel(host, iconCtx(ui, {
    reload() {
      reloads += 1;
    },
  }));
  expect(host.querySelector("#library-fontawesome")).toBeNull();
  expect(host.querySelector("#library-material")).toBeNull();
  expect(host.querySelector('[aria-label="Library URL"]')).toBeNull();
  host.querySelector('[data-row-id="fontawesome"]').click();
  host.querySelector('[aria-label="Library name"]').value = "FA kit";
  buttonNamed(host, "Save").click();
  await flush();
  expect(reloads).toBe(0);
  expect(ui.iconLibraries[0].name).toBe("FA kit");
  expect(ui.iconLibraries[0].group).toBe("fontawesome");
  expect(ui.iconLibraries[0].url).toBe(FA_KIT_URL);

  host.querySelector('[aria-label="Library style"]').value = "sharp-thin";
  buttonNamed(host, "Save").click();
  await flush();
  expect(reloads).toBe(0);
  expect(ui.iconLibraries[0].style).toBe("sharp-thin");
  expect(ui.iconLibraries[0].url).toBe(FA_KIT_URL);

  host.querySelector('[data-row-id="material"]').click();
  host.querySelector('[aria-label="Library style"]').value = "outlined";
  buttonNamed(host, "Save").click();
  await flush();
  expect(reloads).toBe(1);
  expect(ui.iconLibraries.find((entry) => entry.id === "material").url).toContain("Material+Symbols+Outlined");

  host.querySelector("#library-add").click();
  expect(host.querySelector('[aria-label="Font Awesome kit id"]').closest("label").hidden).toBe(false);
  buttonNamed(host, "Save").click();
  await flush();
  expect(reloads).toBe(1);
  expect(host.querySelector(".pav-icon-note").textContent).toMatch(/kit id/);

  host.querySelector('[aria-label="Font Awesome kit id"]').value = "abc";
  host.querySelector('[aria-label="Library style"]').value = "classic-solid";
  buttonNamed(host, "Save").click();
  await flush();
  expect(reloads).toBe(2);
  const added = ui.iconLibraries.find((entry) => entry.url.endsWith("/abc.js"));
  expect(added.group).toBe("fontawesome");
  expect(added.style).toBe("classic-solid");
  expect(added.enabled).toBe(true);
  expect(added.id).not.toBe("fontawesome");
  host.remove();
});

test("Assets lists on refresh and delete calls the asset command", async () => {
  let listed = 0;
  const deleted = [];
  const host = document.createElement("div");
  document.body.append(host);
  const panel = mountAssetsPanel(host, iconCtx({}, {
    api: {
      asset: {
        async list() {
          listed += 1;
          return { assets: [{ id: "a1", kind: "svg", bytes: 20 }], warn: true };
        },
        async delete(args) {
          deleted.push(args.id);
          return {};
        },
      },
    },
  }));
  expect(listed).toBe(0);
  expect(host.textContent).toContain("No icon assets.");
  await panel.refresh();
  expect(listed).toBe(1);
  expect(host.querySelector('[data-group="SVG"]')).not.toBeNull();
  expect(host.textContent).toContain("Icon assets are over 128 MB.");
  host.querySelector('[data-row-id="a1"]').click();
  expect(host.querySelector("img.settings-asset-preview").getAttribute("src")).toBe("pav-asset://a1");
  buttonNamed(host.querySelector("#assets-form"), "Delete").click();
  await flush();
  expect(deleted).toEqual(["a1"]);
  host.remove();
});

test("cleanOrder keeps known fields and appends the shipped ones", () => {
  expect(cleanOrder(["set", "on", "set", "bogus"], ["on", "name", "set"])).toEqual(["set", "on", "name"]);
  expect(cleanOrder(null, ["on", "name"])).toEqual(["on", "name"]);
  expect(orderColumns([{ field: "on" }, { field: "name" }, { field: "set" }], ["set", "on", "name"]).map((column) => column.field)).toEqual(["set", "on", "name"]);
});

test("the column chooser names columns, and every data column resizes and can move", async () => {
  const saved = [];
  const ui = { icons: {}, iconRecent: [], iconRecentMax: 12, iconLibraries: defaultLibraries() };
  const host = document.createElement("div");
  host.className = "pav-root";
  document.body.append(host);
  mountLibrariesPanel(host, iconCtx(ui, {
    tableOrder: () => ["url", "on"],
    tableWidths: () => ({ on: 70 }),
    saveTableOrder: (id, order) => saved.push([id, order]),
  }));
  const libraries = host.querySelector("#libraries-tree").pavTableOptions;
  expect(libraries.movable).toBe(true);
  expect(libraries.columns.every((column) => column.resizable === true)).toBe(true);
  expect(libraries.columns.map((column) => column.field)).toEqual(["url", "on", "name", "set", "styleLabel"]);
  expect(libraries.columns.find((column) => column.field === "on").width).toBe(70);
  const anchor = document.createElement("button");
  host.append(anchor);
  libraries.onIndicatorHeader(anchor);
  const box = document.querySelector(".pav-column-chooser");
  expect(box.textContent).toContain("On");
  expect(box.textContent).toContain("Group");
  expect(box.textContent).toContain("Style");
  expect(box.textContent).not.toContain("undefined");
  libraries.onColumnMoved(["name", "set"]);
  expect(saved.at(-1)).toEqual(["libraries", ["name", "set", "on", "styleLabel", "url"]]);
  box.querySelector("[data-reset]").click();
  expect(saved.at(-1)).toEqual(["libraries", []]);
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));

  const iconsHost = document.createElement("div");
  iconsHost.className = "pav-root";
  document.body.append(iconsHost);
  mountIconsPanel(iconsHost, iconCtx(ui, { tableWidths: () => ({ glyph: 80, fallback: 90 }) }));
  const icons = iconsHost.querySelector("#icons-tree").pavTableOptions;
  expect(icons.movable).toBe(true);
  expect(icons.columns.every((column) => column.resizable === true)).toBe(true);
  expect(icons.columns.map((column) => column.field)).toEqual(["fallback", "glyph", "name", "format", "key"]);
  expect(icons.columns.find((column) => column.field === "format").width).toBe(160);
  expect(icons.columns.find((column) => column.field === "glyph").width).toBe(80);
  const iconAnchor = document.createElement("button");
  iconsHost.append(iconAnchor);
  icons.onIndicatorHeader(iconAnchor);
  const iconBox = document.querySelector(".pav-column-chooser");
  expect(iconBox.textContent).toContain("Default");
  expect(iconBox.textContent).toContain("Icon");
  expect(iconBox.textContent).toContain("Icon Format");
  expect(iconBox.textContent).toContain("Key");
  expect(iconBox.textContent).not.toContain("undefined");
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));

  const assetsHost = document.createElement("div");
  assetsHost.className = "pav-root";
  document.body.append(assetsHost);
  const panel = mountAssetsPanel(assetsHost, iconCtx(ui, {
    api: { asset: { list: async () => ({ assets: [{ id: "a1", kind: "svg", bytes: 12 }], warn: false }) } },
  }));
  await panel.refresh();
  const assets = assetsHost.querySelector("#assets-tree").pavTableOptions;
  expect(assets.movable).toBe(true);
  expect(assets.columns.every((column) => column.resizable === true)).toBe(true);
  expect(assets.columns.map((column) => column.field)).toEqual(["kindLabel", "bytes", "id"]);
  const assetAnchor = document.createElement("button");
  assetsHost.append(assetAnchor);
  assets.onIndicatorHeader(assetAnchor);
  const assetBox = document.querySelector(".pav-column-chooser");
  expect(assetBox.textContent).toContain("Kind");
  expect(assetBox.textContent).toContain("Size");
  expect(assetBox.textContent).not.toContain("undefined");
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
  host.remove();
  iconsHost.remove();
  assetsHost.remove();
});
