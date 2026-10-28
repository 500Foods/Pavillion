/**
 * @vitest-environment jsdom
 */
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";
import { defaultLibraries, libraryUrlError, librariesError, MATERIAL_URL } from "../src/common/iconhosts.js";
import { mountIconsPanel } from "../src/common/icons/panel.js";
import { registryEntry } from "../src/common/icons/registry.js";
import { PNG_RENDER, paintRender } from "../src/common/icons/snippet.js";
import { openIconWizard } from "../src/common/icons/wizard.js";

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
  return [...root.querySelectorAll("button")].find((node) => node.textContent === text);
}

test("an off-list library URL asks for a rebuild", () => {
  expect(libraryUrlError("script", "https://kit.fontawesome.com/45801522ef.js")).toBe("");
  expect(libraryUrlError("stylesheet", "https://fonts.googleapis.com/css2?family=Material+Symbols+Sharp")).toBe("");
  expect(libraryUrlError("stylesheet", MATERIAL_URL)).toBe("");
  expect(librariesError(defaultLibraries())).toBe("");
  expect(libraryUrlError("stylesheet", "https://user:pass@fonts.googleapis.com/css2?family=x")).toMatch(/credentials/);
  expect(libraryUrlError("script", "https://api.iconify.design/all.js")).toMatch(/needs a rebuild/);
  expect(libraryUrlError("script", "http://kit.fontawesome.com/a.js")).toMatch(/https/);
});

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
  const librarySelect = dialog.querySelector("select");
  librarySelect.value = "material";
  librarySelect.dispatchEvent(new Event("change"));
  const face = [...dialog.querySelectorAll("label")].find((node) => node.textContent.startsWith("Material style")).querySelector("select");
  face.value = "outlined";
  face.dispatchEvent(new Event("change"));
  buttonNamed(dialog, "Search").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  buttonNamed(dialog, "Inbox").click();
  buttonNamed(dialog, "Accept").click();
  await expect(pending).resolves.toEqual({ html: '<span class="material-symbols-outlined">inbox</span>' });
  parent.remove();
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
  buttonNamed(dialog, "Icon Foundry").click();
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

test("a section tab lists that section, and a clean close does not ask", async () => {
  const ui = {
    icons: { "nav.update": '<i class="fa-solid fa-star"></i>' },
    iconRecent: [],
    iconRecentMax: 12,
    iconLibraries: [],
  };
  const host = document.createElement("div");
  document.body.append(host);
  let asked = 0;
  mountIconsPanel(host, {
    ui,
    api: { asset: { list: async () => { throw new Error("none"); } } },
    saveNow() {
      return Promise.resolve();
    },
    confirm: async () => {
      asked += 1;
      return false;
    },
  });
  expect(host.querySelector('[role="tab"][aria-selected="true"]').textContent).toBe("Nav");
  expect(host.querySelector('tr[data-icon-key="nav.mail"] .pav-icon-default').dataset.default).toBe("1");
  expect(host.querySelector('tr[data-icon-key="nav.update"] .pav-icon-default').dataset.default).toBeUndefined();
  expect(host.querySelector('tr[data-icon-key="mail.list.flag"]')).toBeNull();
  expect(host.querySelector("tr[data-icon-key] .pav-icon-glyph").style.fontSize).toBe("");
  buttonNamed(host, "Mail list").click();
  expect(host.querySelector('tr[data-icon-key="mail.list.flag"] .pav-icon-name').textContent).toBe("Flag");
  expect(host.querySelector('tr[data-icon-key="nav.mail"]')).toBeNull();
  host.querySelector('tr[data-icon-key="mail.list.flag"]').click();
  const dialog = host.querySelector("dialog");
  buttonNamed(dialog, "Cancel").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(asked).toBe(0);
  expect(host.querySelector("dialog")).toBeNull();
  host.remove();
});

test("Apply stores only that key, Cancel leaves it, and Revert all deletes it", async () => {
  const ui = {
    icons: {},
    iconRecent: [],
    iconRecentMax: 12,
    iconLibraries: [{ id: "fontawesome", name: "Font Awesome", enabled: true }],
    colorSwatches: [],
  };
  const saves = [];
  const host = document.createElement("div");
  document.body.append(host);
  mountIconsPanel(host, {
    ui,
    api: { asset: { list: async () => { throw new Error("none"); } } },
    saveNow() {
      saves.push(structuredClone(ui.icons));
      return Promise.resolve();
    },
    confirm: async () => true,
    search: async () => [{ name: "up-down", label: "Up down" }],
  });
  host.querySelector('[data-icon-key="nav.update"]').click();
  let dialog = host.querySelector("dialog");
  dialog.querySelector(".pav-icon-search").value = "up";
  buttonNamed(dialog, "Search").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  dialog.querySelector(".pav-icon-hit").click();
  buttonNamed(dialog, "Accept").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(Object.keys(ui.icons)).toEqual(["nav.update"]);
  expect(ui.icons["nav.update"]).toContain("fa-up-down");
  expect(saves.at(-1)["nav.update"]).toContain("fa-up-down");
  expect(saves.at(-1)["nav.help"]).toBeUndefined();

  host.querySelector('[data-icon-key="nav.help"]').click();
  dialog = host.querySelector("dialog");
  buttonNamed(dialog, "Cancel").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(ui.icons["nav.help"]).toBeUndefined();

  buttonNamed(host, "Revert all").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(ui.icons["nav.update"]).toBeUndefined();
  host.remove();
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
  dialog.dispatchEvent(new WheelEvent("wheel", { deltaY: -120, ctrlKey: true, bubbles: true, cancelable: true }));
  expect(dialog.style.getPropertyValue("--pav-wizard-zoom")).toBe("1.05");
  expect(zooms).toEqual([1.05]);
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
