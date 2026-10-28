import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";
import { cssColorName, colorTip } from "./colornames.js";
import { colorZoomOf, hexColor, mergeSwatches, pickerZoomAfter, swatchList } from "./coloris.js";

test("color picker zoom starts at 200% and steps by 5%", () => {
  expect(colorZoomOf(undefined)).toBe(2);
  expect(colorZoomOf("nope")).toBe(2);
  expect(colorZoomOf(1.3)).toBe(1.3);
  expect(colorZoomOf(1.32)).toBe(1.3);
  expect(colorZoomOf(1.33)).toBe(1.35);
  expect(colorZoomOf(9)).toBe(3);
  expect(colorZoomOf(0)).toBe(1);
});

test("account colors land on #rrggbb", () => {
  expect(hexColor("#2a7")).toBe("#22aa77");
  expect(hexColor("#AABBCC")).toBe("#aabbcc");
  expect(hexColor("#aabbccdd")).toBe("#aabbcc");
  expect(hexColor("rgb(1, 2, 3)")).toBe("#010203");
  expect(hexColor("rgba(255, 0, 0, 0.5)")).toBe("#ff0000");
  expect(hexColor("hsl(0, 100%, 50%)")).toBe("#ff0000");
  expect(hexColor("hsla(120, 100%, 50%, 0.2)")).toBe("#00ff00");
  expect(hexColor("")).toBe("");
  expect(hexColor("nope")).toBe("");
});

test("ctrl wheel steps the picker by 5% and the switch flips direction", () => {
  expect(pickerZoomAfter(2, 1, false)).toBe(1.95);
  expect(pickerZoomAfter(2, -1, false)).toBe(2.05);
  expect(pickerZoomAfter(2, 1, true)).toBe(2.05);
  expect(pickerZoomAfter(3, -1, false)).toBe(3);
  expect(pickerZoomAfter(1, 1, false)).toBe(1);
});

test("swatches keep the theme list first and cap the row at 24", () => {
  expect(swatchList("#2a7 #22aa77 nope #fff")).toEqual(["#22aa77", "#ffffff"]);
  expect(mergeSwatches("#800000 #ffffff", ["#ffffff", "#010203"])).toEqual(["#800000", "#ffffff", "#010203"]);
  const many = Array.from({ length: 30 }, (_, i) => `#${i.toString(16).padStart(6, "0")}`);
  expect(mergeSwatches(many, ["#abcdef"])).toHaveLength(24);
  expect(mergeSwatches(many, ["#abcdef"])).not.toContain("#abcdef");
});

test("a swatch is named when it is a CSS color or a theme role", () => {
  expect(cssColorName("#800000")).toBe("Maroon");
  expect(cssColorName("#fff")).toBe("White");
  expect(cssColorName("#cc9999")).toBe("");
  expect(colorTip("#22aa77", (name) => (name === "--pav-accent" ? "#2a7" : ""))).toBe("Accent");
  expect(colorTip("#800000", () => "#800000")).toBe("Maroon");
  expect(colorTip("#112233", () => "")).toBe("");
});

test("the picker size rule beats the library stylesheet", () => {
  const css = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "../../public/styles/theme.css"), "utf8");
  const block = css.slice(css.indexOf(".pav-root .pav-color-host .clr-picker {"));
  expect(block.startsWith(".pav-root .pav-color-host .clr-picker {")).toBe(true);
  expect(block.slice(0, 400)).toContain("width: calc(200px * var(--pav-color-zoom))");
  expect(css).not.toMatch(/^\.clr-picker \{/m);
  expect(css).not.toMatch(/pav-color-zoom button/);
  expect(css).toMatch(/\.pav-color-actions button/);
});

test("the account swatch is a text field, so the system color dialog stays closed", () => {
  const template = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "../shell/template.js"), "utf8");
  expect(template).toMatch(/id="account-color-input"[^>]*type="text"/);
  expect(template).not.toMatch(/type="color"/);
});
