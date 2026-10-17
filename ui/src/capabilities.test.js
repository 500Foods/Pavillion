import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";

const file = join(dirname(fileURLToPath(import.meta.url)), "../../src-tauri/capabilities/default.json");

test("capabilities grant IPC to the main window only", () => {
  const cap = JSON.parse(readFileSync(file, "utf8"));
  expect(cap.windows).toEqual(["main"]);
  expect(Object.hasOwn(cap, "remote")).toBe(false);
  expect(JSON.stringify(cap)).not.toContain("://");
});
