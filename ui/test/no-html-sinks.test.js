import { readdirSync, readFileSync } from "node:fs";
import { dirname, join, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";

const src = join(dirname(fileURLToPath(import.meta.url)), "../src");
const SINK = /(?:\.innerHTML\s*(?:=|\+=)|\.outerHTML\s*=|insertAdjacentHTML\s*\(|document\.write\s*\()/;

function files(dir) {
  const found = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) {
      found.push(...files(path));
    } else if (entry.name.endsWith(".js")) {
      found.push(path);
    }
  }
  return found;
}

test("an HTML sink names the static string it inserts", () => {
  const misses = [];
  for (const path of files(src)) {
    const lines = readFileSync(path, "utf8").split("\n");
    lines.forEach((line, index) => {
      if (!SINK.test(line)) {
        return;
      }
      const window = lines.slice(Math.max(0, index - 3), index + 1).join("\n");
      if (!window.includes("html-ok:")) {
        misses.push(`${relative(src, path).split(sep).join("/")}:${index + 1}`);
      }
    });
  }
  expect(misses).toEqual([]);
});
