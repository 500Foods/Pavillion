import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";

const here = dirname(fileURLToPath(import.meta.url));
const html = readFileSync(join(here, "../index.html"), "utf8");
const conf = JSON.parse(readFileSync(join(here, "../../src-tauri/tauri.conf.json"), "utf8"));

test("the window stays hidden until the splash cover is laid out", () => {
  const theme = html.indexOf('href="/styles/theme.css"');
  const splash = html.indexOf('id="splash"');
  const reveal = html.indexOf("pav-splash-painted");
  const module = html.indexOf('type="module"');
  expect(theme).toBeGreaterThan(-1);
  expect(splash).toBeGreaterThan(theme);
  expect(reveal).toBeGreaterThan(splash);
  expect(module).toBeGreaterThan(reveal);
  expect(html).toContain("background: #000");
  const window = conf.app.windows[0];
  expect(window.visible).toBe(false);
  expect(window.backgroundColor).toBe("#000000");
});
