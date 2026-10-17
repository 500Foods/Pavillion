import { expect, test } from "vitest";
import { clampUi } from "./prefs.js";

test("log zoom snaps and the activity view defaults to log", () => {
  const blank = clampUi({});
  expect(blank.zoom.log).toBe(1);
  expect(blank.zoom.wheel).toBe("away");
  expect(blank.activity.view).toBe("log");
  expect(blank.activity.history).toBe(false);

  const stored = clampUi({
    zoom: { log: 1.53, extra: 2 },
    activity: { view: "activity", note: "kept" },
    later: true,
  });
  expect(stored.zoom.log).toBe(1.55);
  expect(stored.zoom.extra).toBe(2);
  expect(clampUi({ zoom: { shell: 0.2, log: 0.2 } }).zoom.shell).toBe(0.5);
  expect(clampUi({ zoom: { shell: 0.2, log: 0.2 } }).zoom.log).toBe(0.2);
  expect(clampUi({ zoom: { shell: 3, log: 3 } }).zoom.shell).toBe(2);
  expect(clampUi({ zoom: { shell: 3, log: 3 } }).zoom.log).toBe(3);
  expect(clampUi({ zoom: { shell: 1.53 } }).zoom.shell).toBe(1.55);
  expect(clampUi({ zoom: { shell: "nope" } }).zoom.shell).toBe(1);
  expect(clampUi({ zoom: { wheel: "toward", extra: 3 } }).zoom.wheel).toBe("toward");
  expect(clampUi({ zoom: { wheel: "toward", extra: 3 } }).zoom.extra).toBe(3);
  expect(clampUi({ zoom: { wheel: "natural" } }).zoom.wheel).toBe("away");
  expect(clampUi({ zoom: { wheel: 1 } }).zoom.wheel).toBe("away");
  expect(stored.activity.view).toBe("activity");
  expect(stored.activity.note).toBe("kept");
  expect(stored.later).toBe(true);
  expect(clampUi({ themeChoice: "file:Warm.theme" }).themeChoice).toBe("file:Warm.theme");
  expect(clampUi({ themeChoice: "file:../x.theme" }).themeChoice).toBeUndefined();
  expect(clampUi({ fontSource: "override", iconSource: "theme" }).fontSource).toBe("override");
  expect(clampUi({}).fontSource).toBeUndefined();
  expect(clampUi({ activity: { view: "nope" } }).activity.view).toBe("log");
  expect(clampUi({ activity: { history: true } }).activity.history).toBe(true);
  expect(clampUi({ activity: { history: "yes" } }).activity.history).toBe(false);
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
