import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";
import { htmlPaneAfterLoad, htmlPaneSrcdoc } from "./htmlpane.js";

const BLOCKED =
  "default-src 'none'; img-src pav-cid: data:; style-src 'unsafe-inline'; font-src data:; frame-src pav-link:";
const REMOTE =
  "default-src 'none'; img-src pav-cid: data: https:; style-src 'unsafe-inline'; font-src data:; frame-src pav-link:";

test("the srcdoc carries the locked sandbox policy and the base style", () => {
  const doc = htmlPaneSrcdoc("<p>Hello</p><script>alert(1)</script>", {});
  expect(doc).toContain(`content="${BLOCKED}"`);
  expect(doc).not.toContain("https:");
  expect(doc).toContain("color-scheme: dark");
  expect(doc).toContain("max-width: 100%");
  expect(doc).toContain("<p>Hello</p><script>alert(1)</script>");
  expect(doc).not.toContain("allow-scripts");
  expect(doc).not.toContain("fontawesome");
});

test("allowRemote adds https to img-src only", () => {
  const doc = htmlPaneSrcdoc("<p>x</p>", { allowRemote: true });
  expect(doc).toContain(`content="${REMOTE}"`);
  expect(doc).toContain("frame-src pav-link:");
  expect(doc).not.toContain("navigate-to");
  expect(doc).not.toContain("script-src");
});

test("a load this pane did not write asks for the same srcdoc again", () => {
  const own = htmlPaneAfterLoad(1);
  expect(own).toEqual({ pending: 0, restore: false });
  const navigated = htmlPaneAfterLoad(own.pending);
  expect(navigated).toEqual({ pending: 1, restore: true });
  expect(htmlPaneAfterLoad(navigated.pending)).toEqual({ pending: 0, restore: false });
});

test("the release frame-src allows pav-link and no remote host", () => {
  const file = join(dirname(fileURLToPath(import.meta.url)), "../../../src-tauri/tauri.conf.json");
  const csp = JSON.parse(readFileSync(file, "utf8")).app.security.csp;
  const frame = csp.split(";").map((part) => part.trim()).find((part) => part.startsWith("frame-src "));
  expect(frame).toBe("frame-src 'self' pav-link:");
});
