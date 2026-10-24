/* D-8: a date is formatted only in formats.js. Counts and sizes may
   still call toLocaleString. */
import { readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";

const ROOT = fileURLToPath(new URL(".", import.meta.url));
const PATTERN = /\b(?:toLocaleDateString|toLocaleTimeString|Intl\.DateTimeFormat)\b/g;

function walk(dir, out) {
  for (const name of readdirSync(dir)) {
    if (name === "node_modules" || name === "dist") {
      continue;
    }
    const path = join(dir, name);
    if (statSync(path).isDirectory()) {
      walk(path, out);
    } else if (name.endsWith(".js") && !name.endsWith(".test.js")) {
      out.push(path);
    }
  }
  return out;
}

test("date formatting stays in formats.js", () => {
  const hits = [];
  for (const path of walk(ROOT, [])) {
    const text = readFileSync(path, "utf8").replace(/\/\*[\s\S]*?\*\//g, "").replace(/(^|[^:"])\/\/.*$/gm, "$1");
    const found = [...text.matchAll(PATTERN)].map((match) => match[0]);
    if (found.length === 0) {
      continue;
    }
    const rel = relative(ROOT, path).split("\\").join("/");
    if (rel !== "formats.js") {
      hits.push(rel);
    }
  }
  expect(hits).toEqual([]);
});
