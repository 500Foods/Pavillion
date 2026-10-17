import { expect, test } from "vitest";
import { fileLine, localStamp, parseFileLine, screenLine } from "./format.js";

const entry = {
  seq: 1,
  ts: "2026-10-14T17:04:01.123Z",
  level: "INFO",
  subsystem: "core",
  text: "core ready",
};

test("the document line is the UTC file line", () => {
  const line = fileLine(entry);
  expect(line).toBe("2026-10-14T17:04:01.123Z INFO  core  core ready");
  expect(parseFileLine(line)).toEqual({
    ts: entry.ts,
    level: "INFO",
    subsystem: "core",
    text: "core ready",
  });
  expect(fileLine({ ...entry, level: "ERROR", text: "boom" })).toBe(
    "2026-10-14T17:04:01.123Z ERROR core  boom",
  );
});

test("the on-screen stamp is local time in the profile zone", () => {
  expect(localStamp(entry.ts, "America/Vancouver")).toBe("10:04:01.123");
  expect(localStamp("2026-01-15T08:00:00.000Z", "America/Vancouver")).toBe("00:00:00.000");
  expect(localStamp(entry.ts)).toBe("10:04:01.123");
  expect(localStamp("not-a-stamp", "America/Vancouver")).toBe("??:??:??.???");
  expect(localStamp(entry.ts, "Not/AZone")).toBe("??:??:??.???");
});

test("the screen line uses fixed fields and two spaces", () => {
  const line = screenLine(entry, "America/Vancouver");
  expect(line.slice(0, 12)).toBe("10:04:01.123");
  expect(line.slice(12, 14)).toBe("  ");
  expect(line.slice(14, 19)).toBe("INFO ");
  expect(line.slice(19, 21)).toBe("  ");
  expect(line.slice(21, 29)).toBe("core".padEnd(8, " "));
  expect(line.slice(29, 31)).toBe("  ");
  expect(line.slice(31)).toBe("core ready");
  const error = screenLine({ ...entry, level: "ERROR", subsystem: "ui", text: "boom" }, "America/Vancouver");
  expect(error.slice(14, 19)).toBe("ERROR");
  expect(error.slice(19, 21)).toBe("  ");
  expect(error.slice(21, 29)).toBe("ui".padEnd(8, " "));
  expect(error.slice(31)).toBe("boom");
  const activity = screenLine({ ...entry, subsystem: "activity", text: "begin" }, "America/Vancouver");
  expect(activity.slice(21, 29)).toBe("activity");
  expect(activity.slice(29, 31)).toBe("  ");
  expect(activity.slice(31)).toBe("begin");
});
