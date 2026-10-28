import { expect, test } from "vitest";
import { clampUi } from "./prefs.js";

test("features default to mail, contacts, calendars, and files on", () => {
  expect(clampUi({}).features).toEqual({
    mail: true,
    contacts: true,
    calendars: true,
    tasks: false,
    files: true,
    notes: false,
    argent: false,
    bots: false,
    github: false,
    activity: true,
    update: true,
    help: true,
  });
  expect(clampUi({
    features: { mail: false, bots: true, files: "yes", extra: 1, settings: false },
  }).features).toEqual({
    mail: false,
    contacts: true,
    calendars: true,
    tasks: false,
    files: true,
    notes: false,
    argent: false,
    bots: true,
    github: false,
    activity: true,
    update: true,
    help: true,
    extra: 1,
    settings: false,
  });
});

test("the rail order and the chords clamp", () => {
  expect(clampUi({}).rail).toEqual({
    top: ["mail", "contacts", "calendars", "tasks", "files", "notes", "argent"],
    bottom: ["bots", "github", "activity", "settings", "update", "help"],
  });
  expect(clampUi({}).chords).toEqual({
    mail: "1",
    contacts: "2",
    calendars: "3",
    tasks: "4",
    files: "5",
    notes: "6",
    argent: "7",
    bots: "",
    github: "",
    activity: "",
    settings: "",
    update: "",
    help: "",
  });
  const moved = clampUi({
    rail: { top: ["files", "mail"], bottom: ["help"], note: 1 },
  }).rail;
  expect(moved.top).toEqual(["files", "mail", "contacts", "calendars", "tasks", "notes", "argent"]);
  expect(moved.bottom).toEqual(["help", "bots", "github", "activity", "settings", "update"]);
  expect(moved.note).toBe(1);
  const parked = clampUi({
    rail: { top: ["settings", "mail"], bottom: ["mail", "calendars"] },
  }).rail;
  expect(parked.top.slice(0, 2)).toEqual(["settings", "mail"]);
  expect(parked.bottom.includes("mail")).toBe(false);
  expect(parked.bottom.includes("settings")).toBe(false);
  expect(clampUi({ chords: {} }).chords.mail).toBe("");
  expect(clampUi({ chords: {} }).chords.argent).toBe("");
  const stolen = clampUi({
    chords: { mail: "1", contacts: "1", files: "5", calendars: "8", tasks: "0", notes: 6, extra: "z" },
  }).chords;
  expect(stolen.mail).toBe("1");
  expect(stolen.contacts).toBe("");
  expect(stolen.files).toBe("5");
  expect(stolen.calendars).toBe("8");
  expect(stolen.tasks).toBe("");
  expect(stolen.notes).toBe("");
  expect(stolen.extra).toBe("z");
  const earlier = clampUi({
    rail: { top: ["contacts", "mail"], bottom: [] },
    chords: { contacts: "1", mail: "1" },
  }).chords;
  expect(earlier.contacts).toBe("1");
  expect(earlier.mail).toBe("");
});

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
    levels: ["ERROR", "WARN", "INFO"],
    subsystems: null,
    text: "",
    follow: true,
    wrap: false,
    range: "12h",
    session: "all",
  });
  expect(clampUi({
    log: { levels: ["WARN", "nope"], subsystems: ["config", "config"], text: " saved", follow: false, wrap: true, note: 1 },
  }).log).toEqual({
    levels: ["WARN"],
    subsystems: ["config"],
    text: " saved",
    follow: false,
    wrap: true,
    range: "12h",
    session: "all",
    note: 1,
  });
});

test("K-Q12 resize round: the accounts split bounds come from code (0.15 to 0.85)", () => {
  expect(clampUi({}).accounts).toEqual({ split: 0.35, min: 0.15, max: 0.85 });
  const old = clampUi({ accounts: { split: 0.8, min: 0.15, max: 0.6 } }).accounts;
  expect(old).toEqual({ split: 0.8, min: 0.15, max: 0.85 });
  expect(clampUi({ accounts: { split: 0.99 } }).accounts.split).toBe(0.85);
});

test("tree view state survives the clamp, and a non-object tree is dropped", () => {
  const stored = clampUi({
    trees: {
      mail: { expanded: ["foods"], hiddenRoots: ["courses"], selected: "mbx:1" },
      bad: "nope",
    },
  });
  expect(stored.trees.mail).toEqual({ expanded: ["foods"], hiddenRoots: ["courses"], selected: "mbx:1" });
  expect(stored.trees.bad).toBeUndefined();
  expect(clampUi({}).trees).toEqual({});
});

test("formats keeps valid kind strings and drops the rest", () => {
  expect(clampUi({ formats: { time: "HH:mm:ss", nope: "x", locale: "fr" } }).formats).toEqual({
    time: "HH:mm:ss",
    locale: "fr",
  });
  expect(clampUi({ formats: { time: "nope" } }).formats).toEqual({});
  expect(clampUi({}).formats).toEqual({});
});

test("the color picker zoom starts at 200% and steps by 5%", () => {
  expect(clampUi({}).colorPickerZoom).toBe(2);
  expect(clampUi({ colorPickerZoom: 9 }).colorPickerZoom).toBe(3);
  expect(clampUi({ colorPickerZoom: 1.3 }).colorPickerZoom).toBe(1.3);
  expect(clampUi({ colorPickerZoom: 1.33 }).colorPickerZoom).toBe(1.35);
  expect(clampUi({ colorPickerZoom: "nope" }).colorPickerZoom).toBe(2);
  expect(clampUi({}).colorSwatches).toEqual([]);
  expect(clampUi({ colorSwatches: ["#ABC", "#aabbcc", "nope", "#112233"] }).colorSwatches).toEqual(["#aabbcc", "#112233"]);
  const many = Array.from({ length: 30 }, (_, i) => `#${i.toString(16).padStart(6, "0")}`);
  expect(clampUi({ colorSwatches: many }).colorSwatches).toHaveLength(24);
  expect(clampUi({}).iconWizardZoom).toBe(1);
  expect(clampUi({ iconWizardZoom: 1.33 }).iconWizardZoom).toBe(1.35);
  expect(clampUi({ iconWizardZoom: 9 }).iconWizardZoom).toBe(3);
});

test("K-Q14: Accounts table and form zoom keys default to 1 and clamp like panes", () => {
  const z = clampUi({}).zoom;
  expect(z.accountsTable).toBe(1);
  expect(z.accountsForm).toBe(1);
  expect(clampUi({ zoom: { accountsTable: 9, accountsForm: 0.01 } }).zoom).toMatchObject({ accountsTable: 5, accountsForm: 0.05 });
});
