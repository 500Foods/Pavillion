import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";
import { CONNECT_HOSTS, FONT_HOSTS, LOADER_HOSTS, SCRIPT_HOSTS, STYLESHEET_HOSTS } from "../src/common/iconhosts.js";
import hosts from "../src/common/iconhosts.json";

const root = join(dirname(fileURLToPath(import.meta.url)), "../..");

function directive(csp, name) {
  return csp.split(";").map((part) => part.trim()).find((part) => part.startsWith(name)) || "";
}

test("icon hosts match the core list and the CSP directive that loads them", () => {
  expect(SCRIPT_HOSTS).toEqual(hosts.scripts);
  expect(STYLESHEET_HOSTS).toEqual(hosts.stylesheets);
  expect(FONT_HOSTS).toEqual(hosts.fonts);
  expect(LOADER_HOSTS).toEqual(hosts.loader);
  expect(CONNECT_HOSTS).toEqual(hosts.connect);
  const core = readFileSync(join(root, "core/src/icon_hosts.c"), "utf8");
  for (const host of [...hosts.scripts, ...hosts.stylesheets]) {
    expect(core).toContain(`"${host}"`);
  }
  const csp = JSON.parse(readFileSync(join(root, "src-tauri/tauri.conf.json"), "utf8")).app.security.csp;
  for (const host of hosts.scripts) {
    expect(directive(csp, "script-src")).toContain(`https://${host}`);
  }
  for (const host of hosts.stylesheets) {
    expect(directive(csp, "style-src")).toContain(`https://${host}`);
  }
  for (const host of hosts.fonts) {
    expect(directive(csp, "font-src")).toContain(`https://${host}`);
  }
  for (const host of hosts.loader) {
    expect(directive(csp, "script-src")).toContain(`https://${host}`);
  }
  for (const host of hosts.connect) {
    expect(directive(csp, "connect-src")).toContain(`https://${host}`);
  }
});
