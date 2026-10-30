/**
 * @vitest-environment jsdom
 */
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";
import {
  defaultLibraries,
  importedLibraries,
  libraryUrl,
  libraryUrlError,
  librariesError,
  MATERIAL_URL,
  mergeShippedLibraries,
  phosphorUrl,
  singleFontAwesome,
} from "../src/common/iconhosts.js";
import { registryEntry } from "../src/common/icons/registry.js";
import { createIconSearch } from "../src/common/icons/search.js";
import { PNG_RENDER, faCatalog, faChoiceFromHtml, faFamilyList, faIconKnown, faIconPlace, faKitNames, iconFormat, iconFormatLabel, iconOverrideDocument, iconOverridesFromText, iconPackFromText, paintRender } from "../src/common/icons/snippet.js";
import { applyIconWizardZoom, openIconWizard } from "../src/common/icons/wizard.js";

const src = join(dirname(fileURLToPath(import.meta.url)), "../src");

function walk(dir) {
  const found = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) {
      found.push(...walk(path));
    } else if (entry.name.endsWith(".js") && !entry.name.endsWith(".test.js")) {
      found.push(path);
    }
  }
  return found;
}

function buttonNamed(root, text) {
  return [...root.querySelectorAll("button")].find((node) => node.textContent === text || node.getAttribute("aria-label") === text);
}

function waitSearch() {
  return new Promise((resolve) => setTimeout(resolve, 600));
}

test("a host outside the shipped list is refused", () => {
  expect(libraryUrlError("script", "https://kit.fontawesome.com/45801522ef.js")).toBe("");
  expect(libraryUrlError("stylesheet", "https://fonts.googleapis.com/css2?family=Material+Symbols+Sharp")).toBe("");
  expect(libraryUrlError("stylesheet", MATERIAL_URL)).toBe("");
  expect(libraryUrlError("script", "https://code.iconify.design/iconify-icon/3.0.0/iconify-icon.min.js")).toBe("");
  expect(libraryUrlError("stylesheet", "https://cdn.jsdelivr.net/npm/bootstrap-icons@1.13.1/font/bootstrap-icons.min.css")).toBe("");
  expect(libraryUrlError("stylesheet", phosphorUrl("regular"))).toBe("");
  expect(librariesError(defaultLibraries())).toBe("");
  expect(defaultLibraries().filter((entry) => entry.enabled).map((entry) => entry.id)).toEqual(["fontawesome"]);
  expect(libraryUrlError("stylesheet", "https://user:pass@fonts.googleapis.com/css2?family=x")).toMatch(/credentials/);
  expect(libraryUrlError("script", "https://api.iconify.design/all.js")).toMatch(/not shipped/);
  expect(libraryUrlError("script", "http://kit.fontawesome.com/a.js")).toMatch(/https/);
  expect(librariesError([{ ...defaultLibraries()[0], group: "nope" }])).toMatch(/group/);
  expect(librariesError([{ ...defaultLibraries()[0], style: "nope" }])).toMatch(/style/);
  const withIconify = defaultLibraries().map((entry) => (
    entry.id === "iconify" ? { ...entry, enabled: true } : entry
  ));
  expect(librariesError(withIconify)).toBe("");
  const secondKit = {
    ...defaultLibraries()[0],
    id: "font-awesome-2",
    url: libraryUrl("fontawesome", "classic-solid", "abc"),
    style: "classic-solid",
    enabled: true,
  };
  const twoKits = [...defaultLibraries(), secondKit];
  expect(librariesError(twoKits)).toMatch(/only one Font Awesome/);
  const firstKept = singleFontAwesome(twoKits);
  expect(firstKept.find((entry) => entry.id === "fontawesome").enabled).toBe(true);
  expect(firstKept.find((entry) => entry.id === "font-awesome-2").enabled).toBe(false);
  const savedKept = singleFontAwesome(twoKits, "font-awesome-2");
  expect(savedKept.find((entry) => entry.id === "fontawesome").enabled).toBe(false);
  expect(savedKept.find((entry) => entry.id === "font-awesome-2").enabled).toBe(true);
  expect(importedLibraries(twoKits).find((entry) => entry.enabled && entry.group === "fontawesome").id).toBe("fontawesome");
  const legacy = [{ id: "fontawesome", name: "Font Awesome", type: "script", url: "https://kit.fontawesome.com/45801522ef.js", enabled: true }];
  expect(mergeShippedLibraries(legacy).map((entry) => entry.id)).toEqual(["fontawesome", "material", "iconify", "bootstrap", "phosphor", "tabler", "remix"]);
  expect(mergeShippedLibraries([{ ...legacy[0], group: "fontawesome" }])).toHaveLength(1);
});

test("the family menu lists every Pro+ family when a kit object exists", () => {
  expect(faFamilyList(null).map((item) => item.id)).toEqual(["classic", "sharp", "duotone", "sharp-duotone", "brands"]);
  const loaded = faFamilyList({});
  expect(loaded.map((item) => item.id)).toContain("graphite");
  expect(loaded).toHaveLength(22);
});

test("an icon export is the saved overrides", () => {
  const text = iconOverrideDocument({
    "nav.contacts": '<i class="fa-solid fa-star"></i>',
    "no.such": '<i class="fa-solid fa-star"></i>',
    "nav.help": "   ",
  });
  const parsed = JSON.parse(text);
  expect(parsed.kind).toBe("pavillion.icons");
  expect(Object.keys(parsed.icons)).toEqual(["nav.contacts"]);
  expect(text.endsWith("\n")).toBe(true);
  expect(iconOverridesFromText(text)["nav.contacts"]).toContain("fa-star");
  expect(iconOverridesFromText('{"kind":"other","icons":{}}')).toBeNull();
  expect(iconOverridesFromText('{"kind":"pavillion.icons","icons":[]}')).toBeNull();
  expect(iconOverridesFromText('{"kind":"pavillion.icons","icons":null}')).toBeNull();
  expect(iconOverridesFromText("not json")).toBeNull();
  const mixed = iconOverridesFromText(JSON.stringify({
    kind: "pavillion.icons",
    icons: {
      "nav.contacts": '  <i class="fa-solid fa-star"></i>  ',
      "missing.key": '<i class="fa-solid fa-star"></i>',
      "nav.help": "<img src=x onerror=alert(1)>",
      "nav.mail": 12,
    },
  }));
  expect(Object.keys(mixed)).toEqual(["nav.contacts"]);
  expect(mixed["nav.contacts"]).toBe('<i class="fa-solid fa-star"></i>');
  expect(iconOverridesFromText('{"kind":"pavillion.icons","icons":{}}')).toEqual({});
  const shared = defaultLibraries().map((entry) => (
    entry.id === "iconify" ? { ...entry, enabled: true } : entry
  ));
  const withLibraries = iconOverrideDocument({
    "nav.contacts": '<i class="fa-solid fa-star"></i>',
  }, shared);
  const packed = iconPackFromText(withLibraries);
  expect(packed.libraries.find((entry) => entry.id === "iconify").enabled).toBe(true);
  expect(packed.libraries.find((entry) => entry.id === "fontawesome").enabled).toBe(true);
  expect(packed.icons["nav.contacts"]).toContain("fa-star");
  const older = iconPackFromText(iconOverrideDocument({
    "nav.contacts": '<i class="fa-solid fa-star"></i>',
  }));
  expect(older.libraries).toBeNull();
  const two = iconPackFromText(JSON.stringify({
    kind: "pavillion.icons",
    icons: {},
    libraries: twoFontAwesome(),
  }));
  expect(two.error).toBe("");
  expect(two.libraries.filter((entry) => entry.enabled && entry.group === "fontawesome")).toHaveLength(1);
  expect(two.libraries.find((entry) => entry.id === "fontawesome").enabled).toBe(true);
  const refused = iconPackFromText(JSON.stringify({
    kind: "pavillion.icons",
    icons: { "nav.contacts": '<i class="fa-solid fa-star"></i>' },
    libraries: [{ id: "extra", name: "Extra", type: "script", url: "https://evil.example/a.js", enabled: true }],
  }));
  expect(refused.error).toMatch(/not shipped/);
  expect(iconOverridesFromText(JSON.stringify({
    kind: "pavillion.icons",
    icons: { "nav.contacts": '<i class="fa-solid fa-star"></i>' },
    libraries: [{ id: "extra", name: "Extra", type: "script", url: "https://evil.example/a.js", enabled: true }],
  }))).toBeNull();
});

function twoFontAwesome() {
  const first = defaultLibraries()[0];
  return [
    first,
    {
      ...first,
      id: "font-awesome-2",
      url: libraryUrl("fontawesome", "classic-solid", "abc"),
      style: "classic-solid",
    },
  ];
}

test("a PNG render is 128 by 128", () => {
  const canvas = { width: 0, height: 0 };
  const ctx = {
    canvas,
    clearRect() {},
    save() {},
    restore() {},
    translate() {},
    rotate() {},
    scale() {},
    drawImage() {},
  };
  paintRender(ctx, { width: 8, height: 8 }, { scale: 1 });
  expect(canvas.width).toBe(128);
  expect(canvas.height).toBe(PNG_RENDER);
});

test("every painted icon key is in the registry", () => {
  const keys = new Set();
  const call = /(?:iconClass|iconNode|registryIcon)\(\s*["']([^"']+)["']/g;
  const attr = /data-icon-key="([^"]+)"/g;
  for (const path of walk(src)) {
    const text = readFileSync(path, "utf8");
    for (const match of text.matchAll(call)) {
      keys.add(match[1]);
    }
    for (const match of text.matchAll(attr)) {
      keys.add(match[1]);
    }
  }
  expect(keys.size).toBeGreaterThan(20);
  for (const key of keys) {
    expect(registryEntry(key), key).not.toBeNull();
  }
  expect(registryEntry("no.such.key")).toBeNull();
});

test("a Material library pick stores a ligature span", async () => {
  const parent = document.createElement("div");
  document.body.append(parent);
  const pending = openIconWizard(parent, {
    title: "Account icon",
    libraries: [
      { id: "fontawesome", name: "Font Awesome", enabled: true },
      { id: "material", name: "Material Symbols", enabled: true },
    ],
    search: async () => [{ name: "inbox", label: "Inbox" }],
  });
  const dialog = parent.querySelector("dialog");
  buttonNamed(dialog, "Other Library").click();
  const librarySelect = [...dialog.querySelectorAll("label")].find((node) => node.firstChild && node.firstChild.textContent === "Library").querySelector("select");
  librarySelect.value = "material";
  librarySelect.dispatchEvent(new Event("change"));
  const face = [...dialog.querySelectorAll("label")].find((node) => node.textContent.startsWith("Material style")).querySelector("select");
  face.value = "outlined";
  face.dispatchEvent(new Event("change"));
  const search = dialog.querySelector(".pav-module-search");
  expect(search.placeholder).toBe("🔍 Search Material Symbols");
  search.dispatchEvent(new Event("input"));
  await waitSearch();
  const hit = dialog.querySelector('.pav-icon-hit[aria-label="Inbox"]');
  expect(hit.querySelector("span").textContent).toBe("inbox");
  hit.click();
  buttonNamed(dialog, "Accept").click();
  await expect(pending).resolves.toEqual({ html: '<span class="material-symbols-outlined">inbox</span>' });
  parent.remove();
});

test("the preview stack is the theme, then dark and light", async () => {
  const css = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "../public/styles/widgets.css"), "utf8");
  expect(css).toMatch(/\.pav-icon-square\s*\{[^}]*width:\s*128px/);
  expect(css).toMatch(/\.pav-icon-chip\s*\{[^}]*width:\s*128px/);
  expect(css).toMatch(/\.pav-icon-preview-col\s*\{[^}]*border-right:/);
  expect(css).toMatch(/\.pav-icon-preview-col\s*\{[^}]*width:\s*calc\(128px \+ 1\.3rem\)/);
  expect(css).toMatch(/\.pav-icon-preview-col\s*\{[^}]*padding:\s*0\.65rem 0/);
  const acceptEdge = css.match(/\.pav-icon-bar button\.pav-dialog-ok,[\s\S]*?\n\}/);
  expect(acceptEdge[0]).toContain("border-left:");
  expect(acceptEdge[0]).not.toContain("border-right");
  expect(css).toMatch(/\.pav-icon-bar button\.pav-icon-close \{\s*border-right:/);
  expect(css).toMatch(/\.pav-icon-flags \{[^}]*border-right:/);
  expect(css).toMatch(/\.pav-icon-flags > button\.pav-icon-menu \{[^}]*border-right:\s*0/);
  expect(css).toMatch(/\.pav-icon-theme-label\s*\{[^}]*border-top:/);
  expect(css).toMatch(/\.pav-icon-theme-label\s*\{[^}]*border-bottom:/);
  expect(css).toMatch(/\.pav-icon-label\s*\{[^}]*color:\s*var\(--pav-accent\)/);
  expect(css).toMatch(/\.pav-icon-bar button\.pav-icon-menu\s*\{[^}]*border-right:/);
  expect(css).toMatch(/\.pav-icon-toolsbar > \.pav-icon-toolset\s*\{[^}]*min-width:\s*max-content/);
  expect(css).not.toMatch(/background:\s*(?:white|black)\b/);
  const parent = document.createElement("div");
  document.body.append(parent);
  let asked = 0;
  const pending = openIconWizard(parent, {
    title: "Sent",
    section: "Mail",
    current: '<i class="fa-solid fa-star"></i>',
    confirm: async () => {
      asked += 1;
      return true;
    },
  });
  const dialog = parent.querySelector("dialog");
  const square = dialog.querySelector(".pav-icon-square");
  const dark = dialog.querySelector(".pav-icon-chip.is-dark");
  const light = dialog.querySelector(".pav-icon-chip.is-light");
  expect(square.classList.contains("is-dark")).toBe(false);
  expect(square.classList.contains("is-light")).toBe(false);
  expect([...dialog.querySelectorAll(".pav-icon-theme-label")].map((node) => node.textContent)).toEqual(["Theme", "Dark", "Light"]);
  expect(dialog.querySelector(".pav-icon-ground")).toBeNull();
  expect(dark.tagName).toBe("DIV");
  expect(light.tagName).toBe("DIV");
  expect(square.querySelector("i").className).toContain("fa-star");
  expect(dark.querySelector("i").className).toContain("fa-star");
  expect(light.querySelector("i").className).toContain("fa-star");
  const bars = dialog.querySelectorAll(".pav-icon-bar");
  expect(bars).toHaveLength(4);
  expect(bars[0].classList.contains("pav-module-toolbar")).toBe(true);
  expect(bars[0].getAttribute("aria-label")).toBe("Icon");
  expect(bars[0].querySelector(".pav-icon-crumb")).not.toBeNull();
  expect(bars[0].querySelector(".pav-icon-close")).not.toBeNull();
  expect(bars[1].getAttribute("aria-label")).toBe("Icon presets");
  expect(bars[1].firstElementChild.classList.contains("pav-icon-preset-pin")).toBe(true);
  expect(bars[1].firstElementChild.textContent).toBe("Default");
  expect(bars[1].querySelector(".pav-icon-preset-scroll").textContent).not.toContain("Default");
  expect(bars[2].getAttribute("aria-label")).toBe("Icon Format");
  expect(bars[2].querySelector(".pav-module-title").textContent).toBe("Icon Format");
  expect(bars[2].querySelector(".pav-icon-label")).not.toBeNull();
  expect(bars[3].getAttribute("aria-label")).toBe("Icon tools");
  expect(bars[1].querySelector(".pav-module-spacer")).not.toBeNull();
  const accept = dialog.querySelector(".pav-dialog-ok");
  const cancel = dialog.querySelector(".pav-dialog-cancel");
  expect(accept.getAttribute("aria-label")).toBe("Accept");
  expect(accept.querySelector("[data-icon-key]").dataset.iconKey).toBe("ui.check");
  expect(cancel.getAttribute("aria-label")).toBe("Cancel");
  expect(cancel.querySelector("[data-icon-key]").dataset.iconKey).toBe("ui.cancel");
  expect([...bars[2].querySelectorAll("button")].map((node) => node.textContent)).toEqual([
    "Font Awesome", "Other Library", "PNG", "SVG", "Custom",
  ]);
  expect(bars[2].querySelector('[aria-pressed="true"]').textContent).toBe("Font Awesome");
  expect(dialog.querySelector(".pav-icon-work .pav-module-search")).toBeNull();
  const iconSearch = bars[3].querySelector(".pav-module-search");
  expect(iconSearch).not.toBeNull();
  expect(iconSearch.placeholder).toBe("🔍 Search Font Awesome");
  expect(iconSearch.previousElementSibling.classList.contains("pav-module-spacer")).toBe(true);
  expect(iconSearch.nextElementSibling.classList.contains("help-jump")).toBe(true);
  expect(iconSearch.nextElementSibling.getAttribute("data-help-section")).toBe("fontawesome");
  expect(buttonNamed(dialog, "Search")).toBeUndefined();
  expect([...dialog.querySelectorAll("label")].some((node) => node.childNodes[0].textContent === "Size")).toBe(false);
  expect(dialog.querySelector('[aria-label="Result size"]')).toBeNull();
  const flags = buttonNamed(dialog, "Flags");
  expect(flags.getAttribute("aria-expanded")).toBe("false");
  flags.click();
  const menu = dialog.querySelector(".pav-icon-flag-menu");
  expect(menu.hidden).toBe(false);
  expect([...menu.querySelectorAll(".pav-icon-flag-heading")].map((node) => node.textContent)).toEqual(["Size", "Rotate", "Flip", "Styling", "Width", "Animate"]);
  expect(menu.querySelector('[data-flag="fa-swap-opacity"]').dataset.exclusive).toBe("false");
  expect(menu.querySelectorAll(".pav-icon-flag-check").length).toBe(menu.querySelectorAll(".pav-icon-flag").length);
  expect(menu.querySelector('[data-flag="fa-2x"]').dataset.exclusive).toBe("true");
  expect(menu.querySelector('[data-flag="fa-spin"]').dataset.exclusive).toBe("false");
  expect(dialog.querySelectorAll(".pav-icon-row")).toHaveLength(0);
  expect(dialog.querySelector(".pav-icon-grid")).not.toBeNull();
  buttonNamed(dialog, "PNG").click();
  expect(dialog.querySelector(".pav-module-search").closest("[hidden]")).not.toBeNull();
  expect(dialog.querySelector(".help-jump").hidden).toBe(true);
  expect([...dialog.querySelectorAll("button")].some((node) => node.textContent === "Load" && !node.closest("[hidden]"))).toBe(true);
  expect([...dialog.querySelectorAll("button")].some((node) => node.textContent === "Save" && !node.closest("[hidden]"))).toBe(true);
  buttonNamed(dialog, "Font Awesome").click();
  expect(dialog.querySelector(".pav-module-search").closest("[hidden]")).toBeNull();
  expect(dialog.querySelector(".help-jump").hidden).toBe(false);
  dialog.querySelector(".pav-icon-close").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(asked).toBe(0);
  await expect(pending).resolves.toBeNull();
  parent.remove();
});

test("icon format follows the snippet, and the roof mark stays on PNG", async () => {
  expect(iconFormat('<img src="pav-system:mark" alt="">')).toBe("png");
  expect(iconFormat('<i class="fa-solid fa-star"></i>')).toBe("fa");
  expect(iconFormat('<span class="material-symbols-outlined">inbox</span>')).toBe("other");
  expect(iconFormat('<span class="fa-layers"><i class="fa-solid fa-star"></i></span>')).toBe("custom");
  expect(iconFormat("")).toBe("fa");
  expect(iconFormatLabel("other")).toBe("Other Library");
  const parent = document.createElement("div");
  document.body.append(parent);
  let asked = 0;
  const pending = openIconWizard(parent, {
    title: "Mail",
    current: '<img src="pav-system:mark" alt="">',
    confirm: async () => {
      asked += 1;
      return true;
    },
  });
  const dialog = parent.querySelector("dialog");
  expect(dialog.querySelector('.pav-icon-mode[aria-pressed="true"]').textContent).toBe("PNG");
  expect(dialog.querySelector(".pav-icon-crumb-format").textContent).toBe("PNG");
  expect(dialog.querySelector(".pav-icon-square img").getAttribute("src")).toContain("mark");
  buttonNamed(dialog, "Font Awesome").click();
  expect(dialog.querySelector(".pav-icon-crumb-format").textContent).toBe("Font Awesome");
  expect(dialog.querySelector(".pav-icon-square img")).toBeNull();
  expect(dialog.querySelector(".pav-icon-square i").className).toContain("fa-star");
  buttonNamed(dialog, "PNG").click();
  expect(dialog.querySelector(".pav-icon-square img").getAttribute("src")).toContain("mark");
  buttonNamed(dialog, "Cancel").click();
  await expect(pending).resolves.toBeNull();
  expect(asked).toBe(0);
  parent.remove();
});

test("a Font Awesome search paints hits with the third toolbar", async () => {
  const parent = document.createElement("div");
  document.body.append(parent);
  const seen = [];
  const pending = openIconWizard(parent, {
    title: "Clock",
    current: '<i class="fa-sharp-duotone fa-solid fa-star"></i>',
    search: async (body) => {
      seen.push(body.query);
      return { icons: [{ name: "clock", label: "Clock" }, { name: "alarm-clock", label: "Alarm Clock" }] };
    },
  });
  const dialog = parent.querySelector("dialog");
  const search = dialog.querySelector(".pav-module-search");
  search.value = "clock";
  search.dispatchEvent(new Event("input"));
  await waitSearch();
  expect(seen).toEqual(["clock"]);
  const hits = [...dialog.querySelectorAll(".pav-icon-hit")];
  expect(hits.map((node) => node.getAttribute("aria-label"))).toEqual(["Clock", "Alarm Clock"]);
  expect(hits.map((node) => node.querySelector(".pav-icon-hit-name").textContent)).toEqual(["Clock", "Alarm Clock"]);
  const grid = dialog.querySelector(".pav-icon-grid");
  expect(grid.style.getPropertyValue("--pav-icon-hit-size")).toBe("28px");
  grid.dispatchEvent(new WheelEvent("wheel", { deltaY: -120, ctrlKey: true, bubbles: true, cancelable: true }));
  expect(grid.style.getPropertyValue("--pav-icon-hit-size")).toBe("29px");
  expect(dialog.style.getPropertyValue("--pav-wizard-zoom")).toBe("1");
  expect(hits[0].querySelector("i").className).toContain("fa-clock");
  expect(hits[0].querySelector("i").className).toContain("fa-sharp-duotone");
  const style = [...dialog.querySelectorAll("label")].find((node) => node.firstChild && node.firstChild.textContent === "Style").querySelector("select");
  style.value = "thin";
  style.dispatchEvent(new Event("change"));
  const again = dialog.querySelector('.pav-icon-hit[aria-label="Clock"] i');
  expect(again.className).toContain("fa-thin");
  expect(again.className).toContain("fa-clock");
  expect(dialog.querySelector(".pav-icon-note").textContent).toBe("");
  buttonNamed(dialog, "Cancel").click();
  await expect(pending).resolves.toBeNull();
  parent.remove();
});

test("a kit that cannot draw an icon leaves that hit out", async () => {
  const lookup = {
    library: {
      definitions: {
        fasds: { book: { iconName: "book" } },
      },
    },
    findIconDefinition({ prefix, iconName }) {
      return prefix === "fasds" && iconName === "book" ? { iconName } : null;
    },
  };
  expect(faIconKnown("sharp-duotone", "solid", "book")).toBe(true);
  expect(faIconKnown("sharp-duotone", "solid", "book", lookup)).toBe(true);
  expect(faIconKnown("sharp-duotone", "solid", "not-a-book", lookup)).toBe(false);
  expect(faIconKnown("sharp-duotone", "semibold", "book", lookup)).toBe(false);
  const previous = globalThis.FontAwesome;
  globalThis.FontAwesome = lookup;
  const parent = document.createElement("div");
  document.body.append(parent);
  try {
    const pending = openIconWizard(parent, {
      title: "Contacts",
      section: "Nav",
      key: "nav.contacts",
      current: '<i class="fa-sharp-duotone fa-solid fa-address-book"></i>',
      search: async () => ({
        icons: [
          { name: "book", label: "Book" },
          { name: "not-a-book", label: "Not a book" },
        ],
      }),
    });
    const dialog = parent.querySelector("dialog");
    expect(dialog.querySelector(".pav-icon-crumb-group").textContent).toBe("Nav");
    expect(dialog.querySelector(".pav-icon-crumb-name").textContent).toBe("Contacts");
    expect(dialog.querySelector(".pav-icon-crumb-key").textContent).toBe("nav.contacts");
    expect(dialog.querySelector(".pav-icon-crumb-format").textContent).toBe("Font Awesome");
    dialog.querySelector(".pav-module-search").dispatchEvent(new Event("input"));
    await waitSearch();
    expect([...dialog.querySelectorAll(".pav-icon-hit-name")].map((node) => node.textContent)).toEqual(["Book"]);
    buttonNamed(dialog, "Cancel").click();
    await expect(pending).resolves.toBeNull();
  } finally {
    if (previous === undefined) {
      delete globalThis.FontAwesome;
    } else {
      globalThis.FontAwesome = previous;
    }
    parent.remove();
  }
});

test("a search shows an icon the kit draws in another family", async () => {
  const defs = {
    fas: { clock: { iconName: "clock" }, user: { iconName: "user" } },
    fasds: { star: { iconName: "star" } },
  };
  const fa = {
    library: { definitions: defs },
    findIconDefinition({ prefix, iconName }) {
      const bucket = defs[prefix];
      return bucket && bucket[iconName] ? bucket[iconName] : null;
    },
  };
  expect(faIconPlace("sharp-duotone", "solid", "clock", fa)).toEqual({ family: "classic", style: "solid" });
  expect(faIconPlace("sharp-duotone", "solid", "star", fa)).toEqual({ family: "sharp-duotone", style: "solid" });
  expect(faIconPlace("sharp-duotone", "solid", "missing", fa)).toBeNull();
  expect(faKitNames("user", fa)).toEqual(["user"]);
  expect(faKitNames("", {
    library: { definitions: new Map([["fab", new Map([["github", {}], ["git", {}]])]]) },
  })).toEqual(["github", "git"]);
  expect(faIconPlace("classic", "thin", "nope", { findIconDefinition() { return null; } })).toEqual({
    family: "classic",
    style: "thin",
  });
  const previous = globalThis.FontAwesome;
  globalThis.FontAwesome = fa;
  const parent = document.createElement("div");
  document.body.append(parent);
  const seen = [];
  let helped = 0;
  try {
    const pending = openIconWizard(parent, {
      title: "Star",
      current: '<i class="fa-sharp-duotone fa-solid fa-star"></i>',
      onHelp() {
        helped += 1;
      },
      search: async (body) => {
        seen.push(body);
        if (body.query === "user") {
          return { icons: [] };
        }
        return { icons: [{ name: "clock", label: "Clock" }] };
      },
    });
    const dialog = parent.querySelector("dialog");
    const family = [...dialog.querySelectorAll("label")].find((node) => node.firstChild && node.firstChild.textContent === "Family").querySelector("select");
    expect(family.value).toBe("sharp-duotone");
    dialog.querySelector(".help-jump").click();
    expect(helped).toBe(1);
    const search = dialog.querySelector(".pav-module-search");
    search.value = "clock";
    search.dispatchEvent(new Event("input"));
    await waitSearch();
    expect(seen[0]).toMatchObject({ id: "fontawesome", query: "clock", limit: 200 });
    const clock = dialog.querySelector('.pav-icon-hit[aria-label="Clock"]');
    expect(clock.querySelector("i").className).toBe("fa-solid fa-clock");
    clock.click();
    expect(family.value).toBe("classic");
    expect(dialog.querySelector(".pav-icon-square i").className).toBe("fa-solid fa-clock");
    search.value = "user";
    search.dispatchEvent(new Event("input"));
    await waitSearch();
    const user = dialog.querySelector('.pav-icon-hit[aria-label="user"]');
    expect(user.querySelector("i").className).toBe("fa-solid fa-user");
    user.click();
    search.value = "clock";
    search.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
    expect(search.value).toBe("");
    expect(parent.querySelector("dialog")).not.toBeNull();
    buttonNamed(dialog, "Accept").click();
    await expect(pending).resolves.toEqual({ html: '<i class="fa-solid fa-user"></i>' });
  } finally {
    if (previous === undefined) {
      delete globalThis.FontAwesome;
    } else {
      globalThis.FontAwesome = previous;
    }
    parent.remove();
  }
});

test("a brand icon is drawn with fa-brands", async () => {
  const defs = {
    fasds: { star: { iconName: "star" } },
    fab: { github: { iconName: "github", prefix: "fab" } },
    fas: { clock: { iconName: "clock", prefix: "fas" } },
  };
  const fa = {
    library: { definitions: defs },
    findIconDefinition() {
      return { iconName: "loose" };
    },
  };
  expect(faIconPlace("sharp-duotone", "solid", "github", fa)).toEqual({ family: "brands", style: "" });
  expect(faIconPlace("sharp-duotone", "solid", "clock", fa)).toEqual({ family: "classic", style: "solid" });
  expect(faIconPlace("sharp-duotone", "solid", "star", fa)).toEqual({ family: "sharp-duotone", style: "solid" });
  const previous = globalThis.FontAwesome;
  globalThis.FontAwesome = fa;
  const parent = document.createElement("div");
  document.body.append(parent);
  try {
    const pending = openIconWizard(parent, {
      title: "Star",
      current: '<i class="fa-sharp-duotone fa-solid fa-star"></i>',
      search: async () => ({
        icons: [
          { name: "github", label: "GitHub" },
          { name: "clock", label: "Clock" },
        ],
      }),
    });
    const dialog = parent.querySelector("dialog");
    const search = dialog.querySelector(".pav-module-search");
    search.value = "git";
    search.dispatchEvent(new Event("input"));
    await waitSearch();
    const github = dialog.querySelector('.pav-icon-hit[aria-label="GitHub"]');
    const clock = dialog.querySelector('.pav-icon-hit[aria-label="Clock"]');
    expect(github.querySelector("i").className).toBe("fa-brands fa-github");
    expect(clock.querySelector("i").className).toBe("fa-solid fa-clock");
    github.click();
    const family = [...dialog.querySelectorAll("label")].find((node) => node.firstChild && node.firstChild.textContent === "Family").querySelector("select");
    expect(family.value).toBe("brands");
    const style = [...dialog.querySelectorAll("label")].find((node) => node.firstChild && node.firstChild.textContent === "Style");
    expect(style.hidden).toBe(true);
    expect(dialog.querySelector(".pav-icon-square i").className).toBe("fa-brands fa-github");
    buttonNamed(dialog, "Accept").click();
    await expect(pending).resolves.toEqual({ html: '<i class="fa-brands fa-github"></i>' });
  } finally {
    if (previous === undefined) {
      delete globalThis.FontAwesome;
    } else {
      globalThis.FontAwesome = previous;
    }
    parent.remove();
  }
});

test("a catalog row marked brands is drawn with fa-brands", async () => {
  const fa = {
    library: { definitions: { fasds: { star: { iconName: "star" } } } },
    findIconDefinition() {
      return { iconName: "loose" };
    },
  };
  const previous = globalThis.FontAwesome;
  globalThis.FontAwesome = fa;
  const parent = document.createElement("div");
  document.body.append(parent);
  try {
    const pending = openIconWizard(parent, {
      title: "Star",
      current: '<i class="fa-sharp-duotone fa-solid fa-star"></i>',
      search: async () => ({
        icons: [{ name: "facebook", label: "Facebook", family: "brands" }],
      }),
    });
    const dialog = parent.querySelector("dialog");
    const search = dialog.querySelector(".pav-module-search");
    search.value = "facebook";
    search.dispatchEvent(new Event("input"));
    await waitSearch();
    const hit = dialog.querySelector('.pav-icon-hit[aria-label="Facebook"]');
    expect(hit.querySelector("i").className).toBe("fa-brands fa-facebook");
    hit.click();
    const family = [...dialog.querySelectorAll("label")].find((node) => node.firstChild && node.firstChild.textContent === "Family").querySelector("select");
    expect(family.value).toBe("brands");
    buttonNamed(dialog, "Cancel").click();
    await expect(pending).resolves.toBeNull();
  } finally {
    if (previous === undefined) {
      delete globalThis.FontAwesome;
    } else {
      globalThis.FontAwesome = previous;
    }
    parent.remove();
  }
});

test("a search waits half a second and does not overlap", async () => {
  const parent = document.createElement("div");
  document.body.append(parent);
  const seen = [];
  let release = () => {};
  const pending = openIconWizard(parent, {
    title: "Book",
    current: '<i class="fa-solid fa-star"></i>',
    search: async (body) => {
      seen.push(body.query);
      if (body.query === "book") {
        await new Promise((resolve) => {
          release = resolve;
        });
      }
      return { icons: [] };
    },
  });
  const dialog = parent.querySelector("dialog");
  const search = dialog.querySelector(".pav-module-search");
  search.value = "bo";
  search.dispatchEvent(new Event("input"));
  await new Promise((resolve) => setTimeout(resolve, 200));
  expect(seen).toEqual([]);
  search.value = "book";
  search.dispatchEvent(new Event("input"));
  await waitSearch();
  expect(seen).toEqual(["book"]);
  search.value = "books";
  search.dispatchEvent(new Event("input"));
  await waitSearch();
  expect(seen).toEqual(["book"]);
  release();
  await new Promise((resolve) => setTimeout(resolve, 200));
  expect(seen).toEqual(["book"]);
  await new Promise((resolve) => setTimeout(resolve, 450));
  expect(seen).toEqual(["book", "books"]);
  buttonNamed(dialog, "Cancel").click();
  await expect(pending).resolves.toBeNull();
  parent.remove();
});

test("family and style open the same list as flags", async () => {
  const parent = document.createElement("div");
  document.body.append(parent);
  openIconWizard(parent, {
    title: "Star",
    current: '<i class="fa-sharp-duotone fa-solid fa-star"></i>',
  });
  const dialog = parent.querySelector("dialog");
  const familyLabel = [...dialog.querySelectorAll("label")].find((node) => node.firstChild && node.firstChild.textContent === "Family");
  const family = familyLabel.querySelector("select");
  const trigger = familyLabel.querySelector("button.pav-icon-menu");
  expect(family.hidden).toBe(true);
  expect(trigger.querySelector(".pav-icon-caret")).not.toBeNull();
  expect(trigger.textContent).toBe([...family.options].find((option) => option.value === family.value).textContent);
  trigger.click();
  const open = [...dialog.querySelectorAll(".pav-icon-choice-menu")].filter((menu) => !menu.hidden);
  expect(open).toHaveLength(1);
  const row = [...open[0].querySelectorAll(".pav-icon-flag")].find((item) => item.dataset.value !== family.value);
  row.click();
  expect(family.value).toBe(row.dataset.value);
  expect(open[0].hidden).toBe(true);
  expect(trigger.textContent).toBe(row.querySelector(".pav-icon-flag-label").textContent);
  const flags = buttonNamed(dialog, "Flags");
  expect(flags.classList.contains("pav-icon-menu")).toBe(true);
  expect(flags.querySelector(".pav-icon-caret")).not.toBeNull();
  flags.click();
  expect(dialog.querySelector(".pav-icon-flag-menu").hidden).toBe(false);
  expect([...dialog.querySelectorAll(".pav-icon-choice-menu")].every((menu) => menu.hidden)).toBe(true);
  trigger.click();
  expect(dialog.querySelector(".pav-icon-flag-menu").hidden).toBe(true);
  expect([...dialog.querySelectorAll(".pav-icon-choice-menu")].filter((menu) => !menu.hidden)).toHaveLength(1);
  parent.remove();
});

test("flags keep fa-2x and fa-rotate-90 on the icon", async () => {
  const parent = document.createElement("div");
  document.body.append(parent);
  let asked = 0;
  const pending = openIconWizard(parent, {
    title: "Star",
    current: '<i class="fa-sharp-duotone fa-solid fa-star"></i>',
    confirm: async () => {
      asked += 1;
      return true;
    },
  });
  const dialog = parent.querySelector("dialog");
  buttonNamed(dialog, "Flags").click();
  const menu = dialog.querySelector(".pav-icon-flag-menu");
  menu.querySelector('[data-flag="fa-2x"]').click();
  menu.querySelector('[data-flag="fa-rotate-90"]').click();
  const icon = dialog.querySelector(".pav-icon-square i");
  expect(icon.className).toContain("fa-2x");
  expect(icon.className).toContain("fa-rotate-90");
  expect(icon.className).toContain("fa-sharp-duotone");
  const custom = [...dialog.querySelectorAll("button")].find((node) => node.textContent === "Open in Custom");
  expect(custom.hidden).toBe(true);
  menu.querySelector('[data-flag="fa-flip-horizontal"]').click();
  expect(dialog.querySelector(".pav-icon-square i").className).toContain("fa-flip-horizontal");
  expect(dialog.querySelector(".pav-icon-square i").className).toContain("fa-2x");
  expect(custom.hidden).toBe(false);
  custom.click();
  expect(dialog.querySelector(".pav-icon-square span").className).toContain("fa-rotate-90");
  expect(dialog.querySelector(".pav-icon-square i").className).toContain("fa-flip-horizontal");
  expect(dialog.querySelector(".pav-icon-square i").className).not.toContain("fa-rotate-90");
  buttonNamed(dialog, "Cancel").click();
  await expect(pending).resolves.toBeNull();
  expect(asked).toBe(1);
  parent.remove();
});

test("a flag keeps the open icon", async () => {
  expect(faChoiceFromHtml('<i class="fa-solid fa-clock"></i>')).toEqual({
    name: "clock",
    family: "classic",
    style: "solid",
    flags: [],
  });
  expect(faChoiceFromHtml('<i class="fa-solid fa-clock fa-2x fa-3x"></i>').flags).toEqual(["fa-3x"]);
  expect(faChoiceFromHtml('<i class="fa-sharp-duotone fa-solid fa-book fa-swap-opacity fa-border"></i>').flags).toEqual(["fa-swap-opacity", "fa-border"]);
  expect(faChoiceFromHtml('<i class="fa-brands fa-github"></i>')).toEqual({
    name: "github",
    family: "brands",
    flags: [],
  });
  expect(faChoiceFromHtml('<img src="pav-system:mark" alt="">')).toBeNull();
  expect(faChoiceFromHtml('<span class="fa-layers"><i class="fa-solid fa-star"></i></span>')).toBeNull();
  const parent = document.createElement("div");
  document.body.append(parent);
  let asked = 0;
  const boxes = [];
  const pending = openIconWizard(parent, {
    title: "Clock",
    current: '<i class="fa-sharp-duotone fa-solid fa-clock fa-lg" style="color: #800000"></i>',
    flagBox: { width: 240, height: 300 },
    onFlagBox(next) {
      boxes.push(next);
    },
    confirm: async () => {
      asked += 1;
      return true;
    },
  });
  const dialog = parent.querySelector("dialog");
  expect(dialog.querySelector(".pav-icon-square i").className).toContain("fa-clock");
  buttonNamed(dialog, "Flags").click();
  const menu = dialog.querySelector(".pav-icon-flag-menu");
  expect(menu.style.width).toBe("240px");
  expect(menu.style.height).toBe("300px");
  expect(menu.querySelector('[data-flag="fa-lg"]').getAttribute("aria-pressed")).toBe("true");
  expect(boxes).toEqual([]);
  menu.querySelector('[data-flag="fa-2x"]').click();
  menu.querySelector('[data-flag="fa-swap-opacity"]').click();
  const icon = dialog.querySelector(".pav-icon-square i");
  expect(icon.className).toContain("fa-clock");
  expect(icon.className).toContain("fa-swap-opacity");
  expect(icon.className).toContain("fa-sharp-duotone");
  expect(icon.className).toContain("fa-2x");
  expect(icon.className).not.toContain("fa-star");
  expect(icon.className).not.toContain("fa-lg");
  expect(icon.getAttribute("style") || "").toContain("#800000");
  expect(menu.querySelector('[data-flag="fa-lg"]').getAttribute("aria-pressed")).toBe("false");
  expect(menu.querySelector('[data-flag="fa-2x"]').getAttribute("aria-pressed")).toBe("true");
  const flagsBtn = buttonNamed(dialog, "Flags");
  expect(flagsBtn.querySelector(".pav-icon-menu-label").textContent).toBe("Flags (2)");
  menu.querySelector('[data-flag="fa-2x"]').click();
  expect(dialog.querySelector(".pav-icon-square i").className).toContain("fa-clock");
  expect(dialog.querySelector(".pav-icon-square i").className).toContain("fa-swap-opacity");
  expect(dialog.querySelector(".pav-icon-square i").className).not.toContain("fa-2x");
  expect(dialog.querySelector(".pav-icon-square i").className).not.toContain("fa-star");
  expect(flagsBtn.querySelector(".pav-icon-menu-label").textContent).toBe("Flags (1)");
  const clear = dialog.querySelector(".pav-icon-flag-clear");
  expect(clear.hidden).toBe(false);
  expect(clear.parentElement.classList.contains("pav-icon-flags")).toBe(true);
  expect(flagsBtn.nextElementSibling).toBe(clear);
  clear.click();
  expect(flagsBtn.querySelector(".pav-icon-menu-label").textContent).toBe("Flags");
  expect(dialog.querySelector(".pav-icon-flag-clear").hidden).toBe(true);
  expect(dialog.querySelector(".pav-icon-square i").className).not.toContain("fa-swap-opacity");
  expect(dialog.querySelector(".pav-icon-square i").className).not.toContain("fa-2x");
  buttonNamed(dialog, "Cancel").click();
  await expect(pending).resolves.toBeNull();
  expect(asked).toBe(1);
  parent.remove();
});

test("the wizard help icon is the nav Help icon", async () => {
  const parent = document.createElement("div");
  document.body.append(parent);
  const pending = openIconWizard(parent, {
    title: "Help",
    icons: { "nav.help": '<i class="fa-sharp-duotone fa-solid fa-book"></i>' },
    confirm: async () => false,
  });
  const mark = parent.querySelector(".help-jump .icon");
  expect(mark).not.toBeNull();
  expect(mark.className).toContain("fa-book");
  expect(mark.dataset.iconKey).toBe("nav.help");
  parent.querySelector(".pav-icon-close").click();
  await expect(pending).resolves.toBeNull();
  parent.remove();
});

test("the result grid zoom does not edit the icon", async () => {
  const parent = document.createElement("div");
  document.body.append(parent);
  let asked = 0;
  let seen = 0;
  const pending = openIconWizard(parent, {
    title: "Star",
    current: '<i class="fa-sharp-duotone fa-solid fa-star"></i>',
    resultZoom: 2,
    onResultZoom(next) {
      seen = next;
    },
    confirm: async () => {
      asked += 1;
      return false;
    },
  });
  const dialog = parent.querySelector("dialog");
  const grid = dialog.querySelector(".pav-icon-grid");
  expect(grid.style.getPropertyValue("--pav-icon-hit-size")).toBe("56px");
  grid.dispatchEvent(new WheelEvent("wheel", { deltaY: -120, bubbles: true, cancelable: true }));
  expect(grid.style.getPropertyValue("--pav-icon-hit-size")).toBe("56px");
  grid.dispatchEvent(new WheelEvent("wheel", { deltaY: 120, ctrlKey: true, bubbles: true, cancelable: true }));
  expect(grid.style.getPropertyValue("--pav-icon-hit-size")).toBe("55px");
  expect(seen).toBe(1.95);
  expect(dialog.style.getPropertyValue("--pav-wizard-zoom")).toBe("1");
  buttonNamed(dialog, "Cancel").click();
  await expect(pending).resolves.toBeNull();
  expect(asked).toBe(0);
  parent.remove();
});

test("family and style follow the loaded kit and the fallback is not cached as one", async () => {
  const previous = globalThis.FontAwesome;
  const kit = {
    library: {
      definitions: {
        fasds: { star: { iconName: "star" } },
        fajr: { star: { iconName: "star" } },
      },
    },
  };
  globalThis.FontAwesome = kit;
  const parent = document.createElement("div");
  document.body.append(parent);
  try {
    const first = faCatalog();
    expect(first.source).toBe("kit");
    expect(first.families.map((item) => item.id)).toEqual(["sharp-duotone", "jelly"]);
    expect(first.families[1].styles.map((item) => item.label)).toEqual(["Regular"]);
    kit.library.definitions.fas = { star: { iconName: "star" } };
    expect(faCatalog()).toBe(first);
    const pending = openIconWizard(parent, { title: "Star", current: '<i class="fa-solid fa-star"></i>' });
    const dialog = parent.querySelector("dialog");
    const family = [...dialog.querySelectorAll("label")].find((node) => node.firstChild && node.firstChild.textContent === "Family").querySelector("select");
    expect([...family.options].map((option) => option.textContent)).toEqual(faFamilyList(kit).map((item) => item.label));
    expect(family.value).toBe("classic");
    family.value = "jelly";
    family.dispatchEvent(new Event("change"));
    const style = [...dialog.querySelectorAll("label")].find((node) => node.firstChild && node.firstChild.textContent === "Style");
    expect(style.hidden).toBe(false);
    expect([...style.querySelector("select").options].map((option) => option.textContent)).toEqual(["Regular"]);
    family.value = "sharp-duotone";
    family.dispatchEvent(new Event("change"));
    const brands = faCatalog({ library: { definitions: new Map([["fab", new Map([["github", {}]])]]) } });
    expect(brands.families.map((item) => item.id)).toEqual(["brands"]);
    expect(faCatalog()).toBe(first);
    buttonNamed(dialog, "Cancel").click();
    await expect(pending).resolves.toBeNull();
  } finally {
    if (previous === undefined) {
      delete globalThis.FontAwesome;
    } else {
      globalThis.FontAwesome = previous;
    }
    parent.remove();
  }
  const fallback = faCatalog();
  expect(fallback.source).toBe("fontawesome");
  expect(fallback.families.map((item) => item.id)).toEqual(["classic", "sharp", "duotone", "sharp-duotone", "brands"]);
  expect(fallback.families[0].styles.map((item) => item.id)).toEqual(["solid", "regular", "light", "thin"]);
});

test("icon search refreshes an empty catalog once", async () => {
  const calls = [];
  const search = createIconSearch({
    iconlib: {
      async search(body) {
        calls.push(["search", body.id, body.query, body.limit]);
        if (body.query === "facebook") {
          return { icons: [{ name: "facebook", label: "Facebook", family: "" }] };
        }
        if (body.query === "") {
          return { icons: [] };
        }
        return { icons: [{ name: "clock", label: "Clock" }] };
      },
      async refresh(body) {
        calls.push(["refresh", body.id]);
        return {};
      },
    },
  });
  const first = await search({ id: "fontawesome", query: "clock", limit: 80 });
  expect(first.icons[0].name).toBe("clock");
  expect(calls).toEqual([
    ["search", "fontawesome", "facebook", 5],
    ["refresh", "fontawesome"],
    ["search", "fontawesome", "clock", 80],
  ]);
  calls.length = 0;
  await search({ id: "fontawesome", query: "alarm", limit: 80 });
  expect(calls).toEqual([["search", "fontawesome", "alarm", 80]]);

  const cached = [];
  const ready = createIconSearch({
    iconlib: {
      async search(body) {
        cached.push(body.query);
        if (body.query === "facebook") {
          return { icons: [{ name: "facebook", label: "Facebook", family: "brands" }] };
        }
        return { icons: [{ name: "house", label: "House" }] };
      },
      async refresh() {
        throw new Error("should not refresh");
      },
    },
  });
  await ready({ id: "fontawesome", query: "house", limit: 80 });
  await ready({ id: "material", query: "home", limit: 80 });
  expect(cached).toEqual(["facebook", "house", "", "home"]);

  const skipped = [];
  const generic = createIconSearch({
    iconlib: {
      async search(body) {
        skipped.push(body.id);
        return { icons: [] };
      },
      async refresh() {
        throw new Error("should not refresh");
      },
    },
  });
  await generic({ id: "iconify", query: "clock", limit: 80 });
  expect(skipped).toEqual(["iconify"]);
});

test("Foundry does not preview on a keystroke", async () => {
  const parent = document.createElement("div");
  document.body.append(parent);
  const pending = openIconWizard(parent, {
    title: "Mail",
    section: "Nav",
    key: "nav.mail",
    current: '<i class="fa-solid fa-star"></i>',
    defaultHtml: '<i class="fa-solid fa-star"></i>',
    smallChrome: true,
  });
  const dialog = parent.querySelector("dialog");
  buttonNamed(dialog, "Custom").click();
  await Promise.resolve();
  const square = dialog.querySelector(".pav-icon-square");
  const before = square.innerHTML;
  const area = dialog.querySelector("textarea");
  area.value = '<i class="fa-solid fa-gear"></i>';
  area.dispatchEvent(new Event("input", { bubbles: true }));
  expect(square.innerHTML).toBe(before);
  buttonNamed(dialog, "Preview").click();
  expect(square.querySelector("i").className).toContain("fa-gear");
  buttonNamed(dialog, "Cancel").click();
  await expect(pending).resolves.toBeNull();
  parent.remove();
});

test("the wizard zooms on ctrl wheel and asks only after an edit", async () => {
  const parent = document.createElement("div");
  document.body.append(parent);
  const zooms = [];
  let asked = 0;
  const pending = openIconWizard(parent, {
    title: "Update",
    current: '<img src="pav-system:mark" alt="">',
    defaultHtml: '<i class="fa-solid fa-star"></i>',
    zoom: 1,
    onZoom(next) {
      zooms.push(next);
    },
    confirm: async () => {
      asked += 1;
      return false;
    },
  });
  const dialog = parent.querySelector("dialog");
  expect(dialog.style.getPropertyValue("--pav-wizard-zoom")).toBe("1");
  dialog.querySelector(".pav-icon-crumb-bar").dispatchEvent(new WheelEvent("wheel", { deltaY: -120, ctrlKey: true, bubbles: true, cancelable: true }));
  expect(dialog.style.getPropertyValue("--pav-wizard-zoom")).toBe("1");
  expect(zooms).toEqual([]);
  dialog.dispatchEvent(new WheelEvent("wheel", { deltaY: -120, ctrlKey: true, bubbles: true, cancelable: true }));
  expect(dialog.style.getPropertyValue("--pav-wizard-zoom")).toBe("1.05");
  expect(zooms).toEqual([1.05]);
  applyIconWizardZoom(1.4);
  expect(dialog.style.getPropertyValue("--pav-wizard-zoom")).toBe("1.4");
  dialog.dispatchEvent(new WheelEvent("wheel", { deltaY: -120, ctrlKey: true, bubbles: true, cancelable: true }));
  expect(dialog.style.getPropertyValue("--pav-wizard-zoom")).toBe("1.45");
  expect(zooms).toEqual([1.05, 1.45]);
  dialog.querySelector(".pav-icon-close").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(asked).toBe(0);
  expect(parent.querySelector("dialog")).toBeNull();
  await expect(pending).resolves.toBeNull();

  const again = openIconWizard(parent, {
    title: "Update",
    current: '<img src="pav-system:mark" alt="">',
    defaultHtml: '<i class="fa-solid fa-star"></i>',
    confirm: async () => {
      asked += 1;
      return asked >= 3;
    },
  });
  const second = parent.querySelector("dialog");
  buttonNamed(second, "Default").click();
  second.querySelector(".pav-icon-close").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(asked).toBe(1);
  expect(parent.querySelector("dialog")).not.toBeNull();
  second.dispatchEvent(new MouseEvent("click", { bubbles: true, clientX: -1, clientY: -1 }));
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(asked).toBe(2);
  buttonNamed(second, "Cancel").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(asked).toBe(3);
  await expect(again).resolves.toBeNull();
  parent.remove();
});
