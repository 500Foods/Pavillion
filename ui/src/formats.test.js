import { afterEach, expect, test } from "vitest";
import { helpPages } from "./help/pages.js";
import {
  SAMPLE_DURATION_MS,
  SAMPLE_INSTANT,
  configureFormats,
  currentZone,
  defaultPattern,
  formatInstant,
  localeError,
  patternError,
  zoneFallback,
} from "./formats.js";

const OCT = "2026-10-14T17:04:01.123Z";
const SAMPLE = "2026-10-14T22:04:05.006Z";

afterEach(() => {
  configureFormats({ timezone: "America/Vancouver", timeFormat: "24h", formats: {}, systemZone: "" });
});

test("defaults are 24 h in America/Vancouver and in UTC", () => {
  configureFormats({ timezone: "America/Vancouver", timeFormat: "24h", systemZone: "UTC" });
  expect(formatInstant(OCT, "timeSeconds")).toBe("10:04:01.123");
  expect(formatInstant(OCT, "time")).toBe("10:04");
  expect(formatInstant(OCT, "date")).toBe("2026-10-14");
  expect(formatInstant(OCT, "datetime")).toBe("2026-10-14 10:04");
  expect(formatInstant(SAMPLE, "weekday")).toBe("Wednesday");
  expect(formatInstant(SAMPLE_DURATION_MS, "duration")).toBe("3:04:05");
  configureFormats({ timezone: "UTC", timeFormat: "24h", systemZone: "America/Vancouver" });
  expect(formatInstant(OCT, "timeSeconds")).toBe("17:04:01.123");
  expect(formatInstant(OCT, "datetime")).toBe("2026-10-14 17:04");
});

test("a Vancouver spring-forward skips the missing hour", () => {
  configureFormats({ timezone: "America/Vancouver", timeFormat: "24h", systemZone: "" });
  const clock = (iso) => formatInstant(iso, "time", { patterns: { time: "HH:mm ZZZZ" } });
  expect(clock("2026-03-08T09:30:00.000Z")).toBe("01:30 PST");
  expect(clock("2026-03-08T10:30:00.000Z")).toBe("03:30 PDT");
  expect(clock("2026-11-01T08:30:00.000Z")).toBe("01:30 PDT");
  /* 09:30Z on 2026-11-01. tzdata before 2026b falls back to 01:30 PST.
     tzdata 2026b keeps British Columbia on -07: 02:30 MST there, and
     02:30 PDT where ICU has the offset and still says PDT. */
  expect(["01:30 PST", "02:30 PDT", "02:30 MST"]).toContain(clock("2026-11-01T09:30:00.000Z"));
});

test("12 h is the default until a saved time string differs from it", () => {
  configureFormats({ timezone: "America/Vancouver", timeFormat: "12h", systemZone: "" });
  expect(defaultPattern("time", "12h")).toBe("h:mm a");
  expect(formatInstant(OCT, "time")).toBe("10:04 AM");
  expect(formatInstant(OCT, "timeSeconds")).toBe("10:04:01.123 AM");
  expect(formatInstant(OCT, "datetime")).toBe("2026-10-14 10:04 AM");
  configureFormats({
    timezone: "America/Vancouver",
    timeFormat: "12h",
    formats: { time: "HH:mm" },
    systemZone: "",
  });
  expect(formatInstant(OCT, "time")).toBe("10:04");
  expect(formatInstant(OCT, "datetime")).toBe("2026-10-14 10:04 AM");
});

test("dateShort uses the time today, the short date this year, and the date after that", () => {
  configureFormats({ timezone: "America/Vancouver", timeFormat: "24h", systemZone: "" });
  expect(formatInstant(SAMPLE, "dateShort", { now: SAMPLE_INSTANT })).toBe("15:04");
  expect(formatInstant(SAMPLE, "dateShort", { now: Date.parse("2026-01-02T20:00:00.000Z") })).toBe("Oct 14");
  expect(formatInstant(SAMPLE, "dateShort", { now: Date.parse("2027-06-01T19:00:00.000Z") })).toBe("2026-10-14");
});

test("an invalid format string is refused, and a locale or zone falls back", () => {
  expect(patternError("time", "nope")).toBe("Unknown token n");
  expect(patternError("date", "")).toBe("Enter a format");
  expect(patternError("date", "'yyyy")).toBe("A quote is not closed");
  expect(patternError("relative", "soon")).toBe("Relative is long, short, or narrow");
  expect(patternError("duration", "h:mm:ss")).toBe("");
  expect(localeError("not a locale !!!")).not.toBe("");
  expect(zoneFallback("Not/AZone", "UTC")).toBe("UTC");
  expect(zoneFallback("Not/AZone", "Also/Bad")).toBe("America/Vancouver");
  expect(zoneFallback("America/Vancouver", "UTC")).toBe("America/Vancouver");
  configureFormats({ timezone: "Not/AZone", systemZone: "UTC", timeFormat: "24h" });
  expect(currentZone()).toBe("UTC");
  configureFormats({ timezone: "Missing/Zone", systemZone: "Missing/Zone", timeFormat: "24h" });
  expect(currentZone()).toBe("America/Vancouver");
});

test("relative and a French month follow the locale", () => {
  configureFormats({
    timezone: "America/Vancouver",
    timeFormat: "24h",
    formats: { locale: "fr", date: "MMMM", relative: "long" },
    systemZone: "",
  });
  expect(formatInstant(SAMPLE, "date")).toMatch(/octobre/i);
  expect(formatInstant(SAMPLE, "relative", { now: Date.parse("2026-10-23T22:04:05.006Z") })).toMatch(/jour/);
});

test("Help lists the format tokens and the export split", () => {
  const settings = helpPages.find((page) => page.id === "settings");
  const text = settings.sections.find((section) => section.id === "formats").paragraphs.join("\n");
  for (const token of ["yyyy", "yy", "MM", "M", "MMM", "MMMM", "dd", "d", "EEE", "EEEE", "HH", "H", "hh", "h", "mm", "ss", "SSS", "a", "ZZ", "ZZZZ", "z"]) {
    expect(text).toContain(token);
  }
  expect(text).toContain("https://moment.github.io/luxon/#/formatting?id=table-of-tokens");
  const activity = helpPages.find((page) => page.id === "activity").sections[0].paragraphs.join("\n");
  expect(activity).toContain("display lines");
  const logging = helpPages.find((page) => page.id === "developer").sections.find((section) => section.id === "logging").paragraphs.join("\n");
  expect(logging).toContain("export.write_text");
  expect(logging).toContain("log.export still writes UTC lines");
});
