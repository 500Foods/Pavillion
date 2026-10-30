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
  expect(css).toMatch(/\.pav-icon-hit \{[^}]*gap:\s*8px/);
  expect(css).toMatch(/\.pav-icon-hit \{[^}]*margin:\s*10px/);
  expect(css).toMatch(/\.pav-icon-hit \{[^}]*padding:\s*20px/);
  expect(css).toMatch(/\.pav-icon-grid \{[^}]*gap:\s*0;/);
  expect(css).toMatch(/\.pav-icon-flag-menu \{[^}]*resize:\s*both/);
  expect(css).toMatch(/\.pav-icon-bar button\.help-jump \{[^}]*border-left:/);
  expect(css).toMatch(/\.pav-icon-bar button\.help-jump \{[^}]*padding:\s*0/);
  expect(css).toMatch(/\.pav-icon-bar button\.help-jump \{[^}]*box-sizing:\s*border-box/);
  expect(css).toMatch(/\.pav-icon-bar > \.pav-module-search \{[^}]*flex:\s*0 0 16rem/);
  expect(css).toMatch(/\.pav-icon-wizard \.button-bar \{[^}]*zoom:\s*calc\(var\(--pav-bar-zoom, 1\) \/ var\(--pav-wizard-zoom, 1\)\)/);
  expect(css).toMatch(/\.pav-icon-wizard\.is-embedded \{[^}]*margin:\s*0;/);
  expect(css).toMatch(/\.pav-icon-square \{[^}]*font-size:\s*96px/);
  const preview = css.match(/\.pav-icon-square > \*,[\s\S]*?\n\}/);
  expect(preview[0]).toContain("height: 1em");
  expect(preview[0]).not.toContain("font-size");
  expect(css).toMatch(/\.pav-icon-pop \{[^}]*background:\s*var\(--pav-surface-canvas\)/);
  expect(css).toMatch(/\.pav-icon-pop \{[^}]*color:\s*var\(--pav-text\)/);
  expect(css).toMatch(/\.pav-icon-bar button\.pav-icon-menu \{[^}]*border-left:/);
  expect(css).toMatch(/\.pav-icon-flag:hover,\n\.pav-root \.pav-icon-flag:focus-visible \{[^}]*background:/);
  expect(css).not.toMatch(/\.pav-icon-flag\[aria-pressed="true"\] \{[^}]*background:/);
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
  expect(css).not.toMatch(/#icons-search \{[^}]*border-right:/);
  expect(css).toMatch(/\.pav-module-toolbar > button\.pav-labeled-action:has\(> :is\(\.icon, svg, i\)\),/);
  expect(css).toMatch(/\.pav-module-footer button\.pav-labeled-action:has\(> :is\(\.icon, svg, i\)\) \{\s*font-size: var\(--pav-bar-font-size\);\s*border-left:/);
  expect(css).toMatch(/\.pav-module-footer button\.pav-labeled-action :is\(\.icon, svg, i\) \{\s*position: static;/);
  expect(css).toMatch(/\.pav-module-footer button\.pav-labeled-action:has\(> :is\(\.icon, svg, i\)\)::after \{\s*content: none;/);
  expect(css).toMatch(/var\(--pav-surface-canvas\)/);
  expect(css).toMatch(/var\(--pav-status-error\)/);
  expect(css).toMatch(/var\(--pav-surface-scrim\)/);
  expect(css).toMatch(/var\(--pav-help-index-bg\)/);
  expect(css).toMatch(/var\(--pav-help-index-current\)/);
  expect(css).toMatch(/var\(--pav-help-content-bg\)/);
  expect(css).toMatch(/var\(--pav-help-content-heading\)/);
  expect(css).toMatch(/var\(--pav-help-code-bg\)/);
  expect(css).toMatch(/\.pav-color-host \.clr-picker \{[^}]*width: calc\(200px \* var\(--pav-color-zoom\)\)/);
  expect(css).toMatch(/\.nav-buttons button::after \{[^}]*height:\s*1em/);
  expect(css).toMatch(/\.help-filter-chip::after \{\s*content: "";\s*width: 1em;\s*height: 1em;/);
  expect(css).toMatch(/--pav-filter-zoom:\s*1/);
  const slot = css.match(/\.nav-buttons :is\(\.icon, svg, img\):not\(\.nav-mark\),[\s\S]*?\n\}/);
  expect(slot[0]).toContain("height: 1em");
  expect(slot[0]).not.toContain("font-size");
});

test("K-Q14: the table never wraps its calc row, hides the horizontal bar, and pads the header by variable", () => {
  const css = readFileSync(join(styles, "theme.css"), "utf8");
  expect(css).toMatch(/\.tabulator-calcs-holder \{\n  overflow: hidden;\n  white-space: nowrap;/);
  expect(css).toMatch(/\.tabulator-tableholder \{[^}]*overflow-x: auto;/);
  expect(css).toMatch(/\.tabulator-col \{[^}]*padding: var\(--pav-table-header-pad-y\) var\(--pav-table-header-pad-x\);/);
});

test("resize handles sit after their cell like Tabulator expects, and arrows pin right", () => {
  const css = readFileSync(join(styles, "theme.css"), "utf8");
  const handle = css.match(/\.pav-table \.tabulator-col-resize-handle \{[^}]*\}/)[0];
  expect(handle).toContain("position: relative;");
  expect(handle).toContain("display: inline-block;");
  expect(handle).not.toContain("absolute");
  expect(css).toMatch(/\.tabulator-col-sorter \{[^}]*margin-left: auto;/);
  expect(css).toMatch(/\.tabulator-cell\.pav-col-center,\n\.pav-root \.pav-table \.tabulator-cell\.pav-col-check \{\n  justify-content: center;/);
  expect(css).not.toMatch(/\.tabulator-cell \{[^}]*padding: [0-9.]+rem/);
});
