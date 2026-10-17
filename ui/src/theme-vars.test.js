import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";

const styles = join(dirname(fileURLToPath(import.meta.url)), "../public/styles");

function variableNames(file) {
  const css = readFileSync(join(styles, file), "utf8");
  const found = new Set();
  for (const match of css.matchAll(/(--pav-[a-z0-9-]+)\s*:/g)) {
    found.add(match[1]);
  }
  return [...found].sort();
}

test("dark.css and light.css define the same variables", () => {
  const dark = variableNames("dark.css");
  const light = variableNames("light.css");
  expect(dark.length).toBeGreaterThan(0);
  expect(light).toEqual(dark);
});

test("widgets.css only references variables", () => {
  const css = readFileSync(join(styles, "widgets.css"), "utf8");
  expect(css).not.toMatch(/#[0-9a-fA-F]{3,8}\b/);
  expect(css).not.toMatch(/rgba?\(/);
  expect(css).toMatch(/var\(--pav-tooltip-bg\)/);
  expect(css).toMatch(/var\(--pav-tooltip-border\)/);
  expect(css).toMatch(/filter:\s*var\(--pav-tooltip-shadow\)/);
  expect(css).toMatch(/var\(--pav-dialog-bg\)/);
  expect(css).toMatch(/var\(--pav-dialog-border\)/);
  expect(css).toMatch(/box-shadow:\s*var\(--pav-dialog-shadow\)/);
  expect(css).toMatch(/var\(--pav-dialog-radius\)/);
  expect(css).toMatch(/var\(--pav-dialog-button-radius\)/);
});

test("theme.css only references variables", () => {
  const css = readFileSync(join(styles, "theme.css"), "utf8");
  expect(css).not.toMatch(/#[0-9a-fA-F]{3,8}\b/);
  expect(css).not.toMatch(/rgba?\(/);
  expect(css).not.toMatch(/hsla?\(/);
  expect(css).not.toMatch(/--pav-bg\b/);
  expect(css).not.toMatch(/--pav-fg\b/);
  expect(css).not.toMatch(/--pav-muted\b/);
  expect(css).toMatch(/var\(--pav-text\)/);
  expect(css).toMatch(/var\(--pav-surface-canvas\)/);
  expect(css).toMatch(/var\(--pav-status-error\)/);
  expect(css).toMatch(/var\(--pav-surface-scrim\)/);
  expect(css).toMatch(/var\(--pav-help-index-bg\)/);
  expect(css).toMatch(/var\(--pav-help-index-current\)/);
  expect(css).toMatch(/var\(--pav-help-content-bg\)/);
  expect(css).toMatch(/var\(--pav-help-content-heading\)/);
  expect(css).toMatch(/var\(--pav-help-code-bg\)/);
});
