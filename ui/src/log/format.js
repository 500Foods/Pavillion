/* The file line is the C format "%s %-5s %s  %s": UTC stamp, level
   padded to five characters, subsystem, two spaces, message.
   The screen line is separate. Its time is formats.js timeSeconds.
   Level is 5: ERROR and DEBUG are the longest severity names. Subsystem
   is 8: activity is the longest name the docs assign. Two spaces sit
   between the fields. The message is the last field and is not padded. */

import { formatInstant } from "../formats.js";

const STAMP = 24;
const SCREEN_TIME = 12;
const SCREEN_LEVEL = 5;
const SCREEN_SUBSYSTEM = 8;

export function fileLine(entry) {
  const ts = typeof entry.ts === "string" ? entry.ts : "";
  const level = typeof entry.level === "string" ? entry.level : "";
  const subsystem = typeof entry.subsystem === "string" ? entry.subsystem : "";
  const text = typeof entry.text === "string" ? entry.text : "";
  return `${ts} ${level.padEnd(5, " ")} ${subsystem}  ${text}`;
}

export function parseFileLine(line) {
  if (typeof line !== "string" || line.length < 33 || line[STAMP] !== " " || line[30] !== " ") {
    return null;
  }
  const level = line.slice(25, 30).replace(/ +$/, "");
  if (level === "") {
    return null;
  }
  const body = line.slice(31);
  const gap = body.indexOf("  ");
  if (gap <= 0) {
    return null;
  }
  return {
    ts: line.slice(0, STAMP),
    level,
    subsystem: body.slice(0, gap),
    text: body.slice(gap + 2),
  };
}

/* Display time for a log line. An unknown zone or stamp becomes question marks. */
export function localStamp(ts, timeZone) {
  const zone = typeof timeZone === "string" && timeZone !== "" ? timeZone : undefined;
  const text = formatInstant(ts, "timeSeconds", zone ? { zone } : {});
  return text === "" ? "??:??:??.???" : text;
}

export function screenLine(entry, timeZone) {
  const ts = typeof entry.ts === "string" ? entry.ts : "";
  const level = typeof entry.level === "string" ? entry.level : "";
  const subsystem = typeof entry.subsystem === "string" ? entry.subsystem : "";
  const text = typeof entry.text === "string" ? entry.text : "";
  const time = localStamp(ts, timeZone).padEnd(SCREEN_TIME, " ");
  return `${time}  ${level.padEnd(SCREEN_LEVEL, " ")}  ${subsystem.padEnd(SCREEN_SUBSYSTEM, " ")}  ${text}`;
}
