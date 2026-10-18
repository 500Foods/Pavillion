import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";
import { APP_BUILD, APP_PHASE, APP_VERSION } from "./version.js";

const root = join(dirname(fileURLToPath(import.meta.url)), "../..");
const script = join(root, "scripts/bump-build.sh");

test("the app version is 0.phase.build and the copies match", () => {
  const stored = readFileSync(join(root, "PAVILLION_BUILD"), "utf8").trim();
  expect(stored).toMatch(/^[1-9]\d*$/);
  expect(APP_PHASE).toMatch(/^0\.\d+$/);
  expect(APP_BUILD).toBe(stored);
  expect(APP_VERSION).toBe(`${APP_PHASE}.${stored}`);
  const cargo = readFileSync(join(root, "src-tauri/Cargo.toml"), "utf8");
  const conf = JSON.parse(readFileSync(join(root, "src-tauri/tauri.conf.json"), "utf8"));
  expect(cargo).toMatch(new RegExp(`^version = "${APP_PHASE}.0"`, "m"));
  expect(conf.version).toBe(`${APP_PHASE}.0`);
  expect(conf.app.windows[0].title).toBe("Pavillion");
});

test("the build count steps by one and leaves the phase stamp alone", () => {
  const dir = mkdtempSync(join(tmpdir(), "pav-bump-"));
  const file = join(dir, "PAVILLION_BUILD");
  const decoy = join(dir, "Cargo.toml");
  writeFileSync(file, "143\n");
  writeFileSync(decoy, 'version = "0.1.0"\ntauri = { version = "2" }\n');
  try {
    const first = spawnSync("bash", [script, dir], { encoding: "utf8" });
    expect(first.status).toBe(0);
    expect(first.stdout).toBe("PAVILLION_BUILD 144\n");
    expect(readFileSync(file, "utf8")).toBe("144\n");
    expect(readFileSync(decoy, "utf8")).toBe('version = "0.1.0"\ntauri = { version = "2" }\n');
    const second = spawnSync("bash", [script, dir], { encoding: "utf8" });
    expect(second.status).toBe(0);
    expect(readFileSync(file, "utf8")).toBe("145\n");
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("a build count that is not a positive integer is left unchanged", () => {
  const dir = mkdtempSync(join(tmpdir(), "pav-bump-"));
  const file = join(dir, "PAVILLION_BUILD");
  writeFileSync(file, "nope\n");
  try {
    const run = spawnSync("bash", [script, dir], { encoding: "utf8" });
    expect(run.status).not.toBe(0);
    expect(readFileSync(file, "utf8")).toBe("nope\n");
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
