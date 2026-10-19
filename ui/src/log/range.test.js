import { expect, test } from "vitest";
import { inRange, normalizeRange, rangeBounds, rangeWidened } from "./range.js";

test("ranges normalize to a preset or a custom pair", () => {
  expect(normalizeRange(undefined)).toBe("12h");
  expect(normalizeRange("7d")).toBe("7d");
  expect(normalizeRange("1y")).toBe("12h");
  expect(normalizeRange({ since: 10, until: 20 })).toEqual({ since: 10, until: 20 });
  expect(normalizeRange({ since: 20, until: 10 })).toBe("12h");
  expect(normalizeRange({ since: -1 })).toBe("12h");
});

test("bounds, membership, and widening", () => {
  const now = 100 * 3600 * 1000;
  expect(rangeBounds("12h", now)).toEqual({ since: now - 12 * 3600 * 1000 });
  expect(rangeBounds("all", now)).toEqual({});
  const bounds = { since: Date.parse("2026-10-19T00:00:00.000Z"), until: Date.parse("2026-10-19T01:00:00.000Z") };
  expect(inRange({ ts: "2026-10-19T00:00:00.000Z" }, bounds)).toBe(true);
  expect(inRange({ ts: "2026-10-19T01:00:00.000Z" }, bounds)).toBe(false);
  expect(rangeWidened("12h", "24h", now)).toBe(true);
  expect(rangeWidened("24h", "12h", now)).toBe(false);
  expect(rangeWidened("7d", "all", now)).toBe(true);
});
