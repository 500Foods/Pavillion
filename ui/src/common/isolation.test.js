import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";
import { ISOLATION_LINKS, isolationLinkUrl } from "./isolation.js";

const fixture = join(dirname(fileURLToPath(import.meta.url)), "../../../tests/corpus/hostile.html");

test("the hostile fixture carries the locked constructs and one pav-link", () => {
  const html = readFileSync(fixture, "utf8");
  for (const token of [
    "<script",
    "onerror",
    "javascript:",
    "http-equiv=\"refresh\"",
    "<base ",
    "https://example.invalid/",
    "url(",
    "<form",
    "<svg",
    "<iframe",
    "<object",
    "pav-link://isolation/0",
  ]) {
    expect(html).toContain(token);
  }
});

test("the fixture link index maps to the real URL", () => {
  expect(isolationLinkUrl("isolation", 0)).toBe(ISOLATION_LINKS[0]);
  expect(isolationLinkUrl("isolation", 0)).toBe("https://example.com/pavillion-isolation-test");
  expect(isolationLinkUrl("other", 0)).toBeNull();
  expect(isolationLinkUrl("isolation", 1)).toBeNull();
  expect(isolationLinkUrl("isolation", 1.5)).toBeNull();
});
