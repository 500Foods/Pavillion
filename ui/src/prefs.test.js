import { expect, test } from "vitest";
import { clampUi } from "./prefs.js";

test("a missing session restores, and an empty tab list stays empty", () => {
  expect(clampUi({}).session).toEqual({ tabs: null, restore: true, shutdown: "open", maxTabs: 50 });
  expect(clampUi({ session: { restore: false, tabs: [] } }).session).toEqual({
    tabs: [],
    restore: false,
    shutdown: "open",
    maxTabs: 50,
  });
  expect(clampUi({ session: { maxTabs: 0, tabs: [] } }).session.maxTabs).toBe(1);
  expect(clampUi({ session: { maxTabs: 999, tabs: [] } }).session.maxTabs).toBe(200);
  expect(clampUi({ session: { maxTabs: 12, tabs: [] } }).session.maxTabs).toBe(12);
  expect(clampUi({ session: { restore: true, shutdown: "clean", tabs: [] } }).session.shutdown).toBe("clean");
  expect(clampUi({ session: { shutdown: "closed", tabs: [] } }).session.shutdown).toBe("open");
  const session = clampUi({
    session: {
      restore: "yes",
      tabs: [
        { id: "mail:home", section: "mail", kind: "home", title: "Mail", order: 1, active: true, pinned: true },
        { id: "bad" },
      ],
    },
  }).session;
  expect(session.restore).toBe(true);
  expect(session.tabs).toHaveLength(1);
  expect(session.tabs[0]).toMatchObject({
    id: "mail:home",
    kind: "home",
    key: "",
    title: "Mail",
    pinned: false,
    active: true,
    context: null,
  });
});

test("log zoom snaps and the activity view defaults to log", () => {
  const blank = clampUi({});
  expect(blank.zoom.log).toBe(1);
  expect(blank.zoom.helpIndex).toBe(1);
  expect(blank.zoom.helpContent).toBe(1);
  expect(blank.help).toEqual({ split: 0.25, min: 0.15, max: 0.6 });
  expect(clampUi({ help: { split: 0.9, min: 0.15, max: 0.6 } }).help.split).toBe(0.6);
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
