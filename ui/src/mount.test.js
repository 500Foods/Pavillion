/**
 * @vitest-environment jsdom
 */
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";
import { createApi } from "./api.js";
import { mount, unmount } from "./index.js";
import { mockTransport } from "./transport/mock.js";

const here = dirname(fileURLToPath(import.meta.url));

const darkCss = readFileSync(join(here, "../public/styles/dark.css"), "utf8");

function tokenValue(name) {
  const match = darkCss.match(new RegExp(`${name}\\s*:\\s*([^;]+)`));
  if (!match) {
    throw new Error(`missing ${name}`);
  }
  return match[1].trim();
}

function cssText() {
  return [
    darkCss,
    readFileSync(join(here, "../public/styles/light.css"), "utf8"),
    readFileSync(join(here, "../public/styles/theme.css"), "utf8"),
    ":root { --bg: lime; --pav-surface-canvas: lime; }",
    "button { background-color: lime; color: purple; border: 4px solid purple; }",
    "body { background: lime; font-family: serif; }",
  ].join("\n");
}

function outsideAdds() {
  const adds = [];
  const removes = [];
  const origAdd = EventTarget.prototype.addEventListener;
  const origRemove = EventTarget.prototype.removeEventListener;

  function outside(target) {
    return target === window || target === document || target === document.documentElement || target === document.body;
  }

  EventTarget.prototype.addEventListener = function (type, fn, opts) {
    if (outside(this)) {
      adds.push({ type, fn });
    }
    return origAdd.call(this, type, fn, opts);
  };
  EventTarget.prototype.removeEventListener = function (type, fn, opts) {
    if (outside(this)) {
      removes.push({ type, fn });
    }
    return origRemove.call(this, type, fn, opts);
  };

  return {
    restore() {
      EventTarget.prototype.addEventListener = origAdd;
      EventTarget.prototype.removeEventListener = origRemove;
    },
    leftover() {
      const pending = removes.slice();
      return adds.filter((add) => {
        const index = pending.findIndex((item) => item.type === add.type && item.fn === add.fn);
        if (index === -1) {
          return true;
        }
        pending.splice(index, 1);
        return false;
      });
    },
  };
}

test("mount renders inside the element and unmount leaves the page", async () => {
  const style = document.createElement("style");
  style.textContent = cssText();
  document.head.append(style);

  const foreign = document.createElement("button");
  foreign.type = "button";
  foreign.textContent = "Foreign";
  document.body.append(foreign);

  const el = document.createElement("div");
  document.body.append(el);
  const tracker = outsideAdds();
  const scales = [];

  try {
    const api = createApi(mockTransport());
    await mount(el, api, {
      hostServices: {
        setRootFontSize(scale) {
          scales.push(scale);
        },
      },
    });

    const root = el.querySelector(":scope > .pav-root");
    expect(root).not.toBeNull();
    expect(root.querySelector(".shell")).not.toBeNull();
    expect(document.querySelectorAll(".pav-root")).toHaveLength(1);
    const chain = [];
    for (let node = root; node; node = node.parentNode) {
      chain.push(node.nodeName);
    }
    expect(chain).toEqual(["DIV", "DIV", "BODY", "HTML", "#document"]);
    expect([...document.body.childNodes].map((node) => node.nodeName)).toEqual(["BUTTON", "DIV"]);
    expect(document.documentElement.style.fontSize).toBe("");
    expect(scales).toEqual([1]);
    expect(getComputedStyle(root).getPropertyValue("--pav-surface-canvas").trim()).toBe(tokenValue("--pav-surface-canvas"));
    expect(getComputedStyle(document.documentElement).getPropertyValue("--pav-surface-canvas").trim()).toBe("lime");
    expect(getComputedStyle(foreign).backgroundColor).toMatch(/lime|rgb\(0,\s*255,\s*0\)/);
    expect(getComputedStyle(foreign).color).toMatch(/purple|rgb\(128,\s*0,\s*128\)/);
    const shellZoom = root.querySelector('[data-zoom="shell"] input');
    expect(shellZoom.min).toBe("0.5");
    expect(shellZoom.max).toBe("2");
    expect(shellZoom.step).toBe("0.05");
    for (const key of ["navButtons", "navBody", "mailList", "mailDetail", "log", "helpIndex", "helpContent", "filters", "tables", "details", "settings"]) {
      const input = root.querySelector(`[data-zoom="${key}"] input`);
      expect(input.min).toBe("0.05");
      expect(input.max).toBe("5");
    }
    const pickerZoom = root.querySelector('[data-zoom="colorPickerZoom"] input');
    const wizardZoom = root.querySelector('[data-zoom="iconWizardZoom"] input');
    const resultZoom = root.querySelector('[data-zoom="iconResultZoom"] input');
    expect(pickerZoom.min).toBe("1");
    expect(pickerZoom.max).toBe("3");
    expect(pickerZoom.value).toBe("2");
    expect(wizardZoom.min).toBe("1");
    expect(wizardZoom.max).toBe("3");
    expect(wizardZoom.value).toBe("1");
    expect(resultZoom.min).toBe("1");
    expect(resultZoom.max).toBe("3");
    expect(resultZoom.value).toBe("1");
    expect(resultZoom.closest("[data-zoom]").querySelector(".zoom-label").textContent).toBe("Icon results");
    expect(root.querySelector('[data-zoom="filters"] .zoom-label').textContent).toBe("Filters");
    const topNav = root.querySelector(".nav > .nav-buttons");
    const bottomNav = root.querySelectorAll(".nav > .nav-buttons")[1];
    expect(topNav.matches(".nav > .nav-buttons:first-of-type")).toBe(true);
    expect(bottomNav.matches(".nav > .nav-buttons:last-of-type")).toBe(true);
    const declared = new Map();
    for (const sheet of document.styleSheets) {
      for (const rule of sheet.cssRules) {
        if (rule.selectorText) {
          declared.set(rule.selectorText, rule.style);
        }
      }
    }
    const tabBorder = declared.get(".pav-root .tabs").getPropertyValue("border-bottom");
    const logBorder = declared.get(".pav-root .log-bottombar").getPropertyValue("border-top");
    expect(declared.get(".pav-root .nav > .nav-buttons:first-of-type").getPropertyValue("border-bottom")).toBe(tabBorder);
    expect(declared.get(".pav-root .nav > .nav-buttons:last-of-type").getPropertyValue("border-top")).toBe(logBorder);
    expect(tabBorder).toBe("calc(1px / var(--pav-bar-zoom)) solid var(--pav-border)");
    const strut = declared.get(".pav-root .button-bar::before");
    expect(strut.getPropertyValue("content")).toBe('"Mg"');
    expect(strut.getPropertyValue("padding")).toBe("var(--pav-bar-pad-y) 0");
    expect(declared.get(".pav-root .pav-module-toolbar .pav-module-search").getPropertyValue("font-size")).toBe("var(--pav-bar-font-size)");
    expect(declared.get(".pav-root .button-bar").getPropertyValue("max-height")).toBe("");
    expect(declared.get(".pav-root .button-bar").getPropertyValue("min-height")).toBe("");
    const shellButton = root.querySelector(".nav-buttons button");
    expect(getComputedStyle(shellButton).borderTopWidth).toBe("0px");
    expect(getComputedStyle(shellButton).color).not.toMatch(/purple|rgb\(128,\s*0,\s*128\)/);
    expect(getComputedStyle(document.body).fontFamily.toLowerCase()).not.toContain("vanadium");

    unmount();

    expect(el.childNodes).toHaveLength(0);
    expect(el.className).toBe("");
    expect(document.querySelector(".pav-root")).toBeNull();
    expect(document.body.contains(foreign)).toBe(true);
    expect([...document.body.childNodes]).toEqual([foreign, el]);
    expect(tracker.leftover().map((item) => item.type)).toEqual([]);
    expect(document.documentElement.style.fontSize).toBe("");
  } finally {
    tracker.restore();
    unmount();
    foreign.remove();
    el.remove();
    style.remove();
  }
});
