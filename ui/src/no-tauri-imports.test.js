import { readdirSync, readFileSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";

const src = dirname(fileURLToPath(import.meta.url));
const needle = `@${"tauri-apps"}`;

function sourceFiles(dir) {
  const found = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) {
      found.push(...sourceFiles(path));
    } else if (entry.name.endsWith(".js") && !entry.name.endsWith(".test.js")) {
      found.push(path);
    }
  }
  return found;
}

test("only transport/tauri.js imports the Tauri package", () => {
  const hits = sourceFiles(src).filter((path) => readFileSync(path, "utf8").includes(needle));
  expect(hits.map((path) => relative(src, path))).toEqual(["transport/tauri.js"]);
});
