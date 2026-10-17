import { expect, test } from "vitest";
import { clampUi } from "./prefs.js";

test("log zoom snaps and the activity view defaults to log", () => {
  const blank = clampUi({});
  expect(blank.zoom.log).toBe(1);
  expect(blank.activity.view).toBe("log");

  const stored = clampUi({
    zoom: { log: 1.53, extra: 2 },
    activity: { view: "activity", note: "kept" },
    later: true,
  });
  expect(stored.zoom.log).toBe(1.55);
  expect(stored.zoom.extra).toBe(2);
  expect(stored.activity.view).toBe("activity");
  expect(stored.activity.note).toBe("kept");
  expect(stored.later).toBe(true);
  expect(clampUi({ activity: { view: "nope" } }).activity.view).toBe("log");
  expect(blank.log).toEqual({
    levels: ["ERROR", "WARN", "INFO", "DEBUG"],
    subsystems: null,
    text: "",
    follow: true,
    wrap: false,
  });
  expect(clampUi({
    log: { levels: ["WARN", "nope"], subsystems: ["config", "config"], text: " saved", follow: false, wrap: true, note: 1 },
  }).log).toEqual({
    levels: ["WARN"],
    subsystems: ["config"],
    text: " saved",
    follow: false,
    wrap: true,
    note: 1,
  });
});
