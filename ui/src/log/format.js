/* The file line is the C format "%s %-5s %s  %s": UTC stamp, level
   padded to five characters, subsystem, two spaces, message.
   The screen line is separate. Time is local HH:mm:ss.mmm (12). Level
   is 5: ERROR and DEBUG are the longest severity names. Subsystem is 8:
   activity is the longest name the docs assign. Two spaces sit between
   the fields. The message is the last field and is not padded. */

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

/* Local HH:mm:ss.mmm. Milliseconds come from the UTC stamp; the zone
   only moves the clock. An unknown zone or stamp becomes question marks. */
export function localStamp(ts, timeZone) {
  const zone = typeof timeZone === "string" && timeZone !== "" ? timeZone : "America/Vancouver";
  const date = new Date(ts);
  if (typeof ts !== "string" || ts.length < 23 || Number.isNaN(date.getTime())) {
    return "??:??:??.???";
  }
  let parts;
  try {
    parts = new Intl.DateTimeFormat("en-GB", {
      timeZone: zone,
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
      hourCycle: "h23",
    }).formatToParts(date);
  } catch {
    return "??:??:??.???";
  }
  const pick = (type) => {
    const part = parts.find((item) => item.type === type);
    return part ? part.value : "";
  };
  let hour = pick("hour");
  if (hour === "24") {
    hour = "00";
  }
  const minute = pick("minute");
  const second = pick("second");
  if (hour.length !== 2 || minute.length !== 2 || second.length !== 2) {
    return "??:??:??.???";
  }
  return `${hour}:${minute}:${second}.${ts.slice(20, 23)}`;
}

export function screenLine(entry, timeZone) {
  const ts = typeof entry.ts === "string" ? entry.ts : "";
  const level = typeof entry.level === "string" ? entry.level : "";
  const subsystem = typeof entry.subsystem === "string" ? entry.subsystem : "";
  const text = typeof entry.text === "string" ? entry.text : "";
  const time = localStamp(ts, timeZone).padEnd(SCREEN_TIME, " ");
  return `${time}  ${level.padEnd(SCREEN_LEVEL, " ")}  ${subsystem.padEnd(SCREEN_SUBSYSTEM, " ")}  ${text}`;
}
