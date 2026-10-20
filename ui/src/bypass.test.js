/* H-4 Lock: nothing bypasses pav_log. This lists every allowed direct
   output call site and fails on any new one. Allowed: --help and --version
   output, the F-1 API version mismatch line, pav_log's own stderr copy, the
   second-launch line in lib.rs (O-COMPOSE Lock), and the console router in
   ui/src/log.js. */
import { readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

const ROOT = fileURLToPath(new URL("../..", import.meta.url));

const RULES = [
  { dirs: ["core/src", "core/platform", "core/include"], ext: [".c", ".h"],
    pattern: /\b(?:fprintf\s*\(\s*stderr|fputs\s*\([^;]*stderr|printf\s*\(|puts\s*\(|perror\s*\(|fprintf\s*\(\s*stdout)/g },
  { dirs: ["src-tauri/src"], ext: [".rs"], pattern: /\b(?:e?println!|e?print!|dbg!)/g },
  { dirs: ["ui/src"], ext: [".js"], skip: /\.test\.js$|\/help\/pages\.js$/, /* help prose names console calls */ pattern: /\bconsole\.\w+/g },
];

const ALLOWED = {
  "core/src/pavillion.c": ["fputs(line, stderr"],
  "src-tauri/src/lib.rs": ["eprintln!", "eprintln!"],
  "src-tauri/src/main.rs": ["print!", "println!", "eprint!"],
  "ui/src/log.js": ["console.warn", "console.error", "console.warn", "console.error"],
};

function walk(dir, out) {
  for (const name of readdirSync(dir)) {
    if (name === "target" || name === "node_modules" || name === "vendor" || name === "dist") {
      continue;
    }
    const path = join(dir, name);
    if (statSync(path).isDirectory()) {
      walk(path, out);
    } else {
      out.push(path);
    }
  }
  return out;
}

function stripComments(text, ext) {
  if (ext === ".js" || ext === ".rs" || ext === ".c" || ext === ".h") {
    return text.replace(/\/\*[\s\S]*?\*\//g, "").replace(/(^|[^:"])\/\/.*$/gm, "$1");
  }
  return text;
}

function scan() {
  const found = {};
  for (const rule of RULES) {
    for (const dir of rule.dirs) {
      for (const path of walk(join(ROOT, dir), [])) {
        const ext = rule.ext.find((e) => path.endsWith(e));
        if (!ext || (rule.skip && rule.skip.test(path))) {
          continue;
        }
        const text = stripComments(readFileSync(path, "utf8"), ext);
        const hits = [...text.matchAll(rule.pattern)].map((m) => m[0].replace(/\s+/g, " "));
        if (hits.length > 0) {
          found[relative(ROOT, path)] = hits;
        }
      }
    }
  }
  return found;
}

describe("log bypass", () => {
  it("every direct output call site is on the allowed list", () => {
    const found = scan();
    expect(Object.keys(found).sort()).toEqual(Object.keys(ALLOWED).sort());
    for (const [file, hits] of Object.entries(found)) {
      expect({ file, hits: hits.length }).toEqual({ file, hits: ALLOWED[file].length });
    }
  });
});
