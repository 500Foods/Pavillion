import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";

const here = dirname(fileURLToPath(import.meta.url));
const read = (path) => readFileSync(join(here, path), "utf8");
const html = read("../index.html");
const main = read("main.js");
const theme = read("../public/styles/theme.css");
const shell = read("../../src-tauri/src/lib.rs");
const conf = JSON.parse(read("../../src-tauri/tauri.conf.json"));

test("the splash is native: no cover in the page or the stylesheet", () => {
  expect(html).not.toContain('id="splash"');
  expect(html).not.toContain("pav-splash-painted");
  expect(theme).not.toContain("#splash");
  expect(html).toContain("background: #000");
  const window = conf.app.windows[0];
  expect(window.visible).toBe(false);
  expect(window.backgroundColor).toBe("#000000");
});

test("the page tells the shell when it is ready, and the shell waits for it", () => {
  expect(main).toContain("hostServices.appReady()");
  expect(read("transport/tauri.js")).toContain('emit("pav-app-ready")');
  expect(shell).toContain('"pav-app-ready"');
  expect(shell).toContain("splash::show(");
  expect(shell).toContain("splash::close(");
  expect(shell).not.toContain("on_page_load");
});

test("the Help mark sits above its line and takes its width", () => {
  const rule = theme.slice(theme.indexOf(".pav-mark-lockup img {"));
  expect(theme).toMatch(/\.pav-mark-lockup \{[^}]*width: max-content;/);
  expect(rule.slice(0, rule.indexOf("}"))).toMatch(/width: 0;\s*min-width: 100%;\s*height: auto;/);
});
