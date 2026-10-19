import { expect, test } from "vitest";
import { fileLine } from "./format.js";
import { LOG_LEVELS, levelsAtOrAbove, lineVisible, normalizeFilter, textMatches } from "./filter.js";

const warn = {
  ts: "2026-10-14T17:04:01.123Z",
  level: "WARN",
  subsystem: "config",
  text: "prefs saved",
};
const info = {
  ts: "2026-10-14T17:04:02.123Z",
  level: "INFO",
  subsystem: "config",
  text: "prefs saved",
};
const other = {
  ts: "2026-10-14T17:04:03.123Z",
  level: "WARN",
  subsystem: "core",
  text: "prefs saved",
};

function asciiIncludes(hay, needle) {
  if (needle === "") {
    return true;
  }
  const fold = (value) => value.replace(/[A-Z]/g, (ch) => String.fromCharCode(ch.charCodeAt(0) + 32));
  return fold(hay).includes(fold(needle));
}

test("level, subsystem, and text narrow the loaded lines", () => {
  const filter = { levels: ["WARN"], subsystems: ["config"], text: "saved" };
  expect(lineVisible(warn, filter)).toBe(true);
  expect(lineVisible(info, filter)).toBe(false);
  expect(lineVisible(other, filter)).toBe(false);
  expect(lineVisible(warn, { ...filter, text: "missing" })).toBe(false);
});

test("a cleared filter starts at the info default", () => {
  const open = normalizeFilter({});
  expect(open.levels).toEqual(["ERROR", "WARN", "INFO"]);
  expect(open.subsystems).toBeNull();
  expect(open.text).toBe("");
  expect(open.follow).toBe(true);
  expect(open.wrap).toBe(false);
  expect(lineVisible(warn, open)).toBe(true);
  expect(lineVisible(info, open)).toBe(true);
  expect(lineVisible(other, open)).toBe(true);
});

test("an empty level or subsystem list shows nothing", () => {
  expect(lineVisible(warn, { levels: [], subsystems: null, text: "" })).toBe(false);
  expect(lineVisible(warn, { levels: ["WARN"], subsystems: [], text: "" })).toBe(false);
});

test("text is a literal case-insensitive substring of the UTC line", () => {
  const line = fileLine({ ...warn, text: "a.b kept" });
  const dotted = fileLine({ ...warn, text: "axb dropped" });
  expect(textMatches(line, "A.B")).toBe(true);
  expect(textMatches(dotted, "a.b")).toBe(false);
  expect(textMatches(line, "A.B")).toBe(asciiIncludes(line, "A.B"));
  expect(textMatches(line, "saved")).toBe(asciiIncludes(line, "saved"));
  expect(textMatches(fileLine(warn), "CONFIG")).toBe(true);
});

test("a TRACE line is accepted and hidden at the info default", () => {
  const trace = { ts: "2026-10-14T17:04:04.123Z", level: "TRACE", subsystem: "http", text: "GET /jmap/ 200" };
  expect(LOG_LEVELS).toContain("TRACE");
  expect(normalizeFilter({ levels: ["TRACE"] }).levels).toEqual(["TRACE"]);
  expect(lineVisible(trace, normalizeFilter({}))).toBe(false);
  expect(lineVisible(trace, { levels: levelsAtOrAbove("info"), subsystems: null, text: "" })).toBe(false);
  expect(lineVisible(trace, { levels: levelsAtOrAbove("trace"), subsystems: null, text: "" })).toBe(true);
  expect(levelsAtOrAbove("debug")).toEqual(["ERROR", "WARN", "INFO", "DEBUG"]);
  expect(levelsAtOrAbove("nope")).toEqual(["ERROR", "WARN", "INFO"]);
});

test("queryArgs is the view filter as log.query and log.export take it", async () => {
  const { queryArgs } = await import("./filter.js");
  const now = Date.UTC(2026, 9, 19, 12, 0);
  expect(queryArgs({ levels: ["WARN"], subsystems: ["core"], text: "x", range: "24h" }, "previous", now)).toEqual({
    levels: ["WARN"],
    subsystems: ["core"],
    text: "x",
    since: now - 24 * 3600 * 1000,
    session: "previous",
  });
  expect(queryArgs({ range: "all" }, "bogus", now)).toEqual({
    levels: ["ERROR", "WARN", "INFO"],
    subsystems: null,
    session: "all",
  });
  expect(normalizeFilter({}).range).toBe("12h");
});
