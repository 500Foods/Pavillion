/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { shortDuration } from "./status.js";

test("shortDuration keeps at most two units and skips a zero in the middle", () => {
  expect(shortDuration(0)).toBe("0s");
  expect(shortDuration(-40)).toBe("0s");
  expect(shortDuration(Number.NaN)).toBe("0s");
  expect(shortDuration(400)).toBe("1s");
  expect(shortDuration(1000)).toBe("1s");
  expect(shortDuration(90000)).toBe("1m30s");
  expect(shortDuration(184000)).toBe("3m4s");
  expect(shortDuration(60 * 60 * 1000)).toBe("1h");
  expect(shortDuration((60 * 60 + 3 * 60 + 4) * 1000)).toBe("1h3m");
  expect(shortDuration((2 * 60 * 60 + 4) * 1000)).toBe("2h4s");
  expect(shortDuration((24 * 60 * 60 + 4) * 1000)).toBe("1d4s");
  expect(shortDuration((26 * 60 * 60) * 1000)).toBe("1d2h");
});
