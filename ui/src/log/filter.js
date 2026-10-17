import { Text } from "@codemirror/state";
import { SearchQuery } from "@codemirror/search";
import { fileLine } from "./format.js";

/* The core's log.export uses this same predicate. levels is the checked
   set (empty shows nothing). subsystems null means every subsystem, and an
   empty array shows nothing. text is a literal, ASCII case-insensitive
   substring of the UTC file line. CodeMirror search is that substring:
   regexp is off, and literal is on so backslash sequences stay characters. */

export const LOG_LEVELS = ["ERROR", "WARN", "INFO", "DEBUG"];
const LEVEL_SET = new Set(LOG_LEVELS);
const OWN = ["levels", "subsystems", "text", "follow", "wrap"];

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function subsystemName(value) {
  if (typeof value !== "string" || value.length === 0 || value.length > 128) {
    return null;
  }
  if (value.includes("\n") || value.includes("\r")) {
    return null;
  }
  return value;
}

export function normalizeFilter(raw) {
  const src = isObject(raw) ? raw : {};
  let levels;
  if (Array.isArray(src.levels)) {
    levels = LOG_LEVELS.filter((level) => src.levels.includes(level));
  } else {
    levels = LOG_LEVELS.slice();
  }
  let subsystems = null;
  if (Array.isArray(src.subsystems)) {
    subsystems = [];
    for (const item of src.subsystems) {
      const name = subsystemName(item);
      if (name !== null && !subsystems.includes(name)) {
        subsystems.push(name);
      }
    }
  }
  let text = "";
  if (typeof src.text === "string") {
    text = src.text.length > 200 ? src.text.slice(0, 200) : src.text;
  }
  const out = {
    levels,
    subsystems,
    text,
    follow: src.follow !== false,
    wrap: src.wrap === true,
  };
  for (const key of Object.keys(src)) {
    if (!OWN.includes(key)) {
      out[key] = src[key];
    }
  }
  return out;
}

export function countMatches(text, needle) {
  if (typeof needle !== "string" || needle === "") {
    return 0;
  }
  const query = new SearchQuery({
    search: needle,
    caseSensitive: false,
    literal: true,
    regexp: false,
  });
  if (!query.valid) {
    return 0;
  }
  const cursor = query.getCursor(Text.of(String(text).split("\n")));
  let count = 0;
  let step = cursor.next();
  while (!step.done) {
    count += 1;
    step = cursor.next();
  }
  return count;
}

export function textMatches(line, needle) {
  if (typeof needle !== "string" || needle === "") {
    return true;
  }
  return countMatches(line, needle) > 0;
}

export function lineVisible(entry, filter) {
  const spec = normalizeFilter(filter);
  const level = entry && typeof entry.level === "string" ? entry.level : "";
  if (!LEVEL_SET.has(level) || !spec.levels.includes(level)) {
    return false;
  }
  const subsystem = entry && typeof entry.subsystem === "string" ? entry.subsystem : "";
  if (spec.subsystems !== null && !spec.subsystems.includes(subsystem)) {
    return false;
  }
  return textMatches(fileLine(entry), spec.text);
}
