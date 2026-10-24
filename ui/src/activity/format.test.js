import { expect, test } from "vitest";
import {
  accountFace,
  activityMatches,
  formatEta,
  kindFace,
  localClock,
  preferActivity,
  progressText,
  syncingCount,
  syncSnapshot,
} from "./format.js";

const STAMP = Date.parse("2026-10-14T17:04:01.123Z");

test("the clock is local hours and the eta follows the lock", () => {
  expect(localClock(STAMP, "America/Vancouver")).toBe("10:04:01.123");
  expect(localClock(Number.NaN, "America/Vancouver")).toBe("--:--:--");
  expect(formatEta(null)).toBe("");
  expect(formatEta(5000)).toBe("in 5s");
  expect(formatEta(120000)).toBe("2m");
  expect(formatEta(90 * 60 * 1000)).toBe("1h 30m");
  expect(progressText({ done: 300, total: 1200 })).toBe("300/1,200");
  expect(progressText({ done: 300, total: null })).toBe("300");
  expect(progressText(null)).toBe("");
});

test("action and account filters match the row's kind and account", () => {
  const mail = { kind: "sync.mail", accountId: null };
  expect(activityMatches(mail, null, null)).toBe(true);
  expect(activityMatches(mail, ["sync.mail"], null)).toBe(true);
  expect(activityMatches(mail, ["import"], null)).toBe(false);
  expect(activityMatches(mail, null, [""])).toBe(true);
  expect(activityMatches({ kind: "send", accountId: "a-1" }, null, [""])).toBe(false);
  expect(activityMatches({ kind: "send", accountId: "a-1" }, null, ["a-1"])).toBe(true);
  expect(activityMatches(null, null, null)).toBe(false);
});

test("an activity with no account is 000, the mail icon, and Pavillion", () => {
  expect(accountFace(null, [])).toEqual({
    number: "000",
    name: "Pavillion",
    icon: "fa-sharp-duotone fa-solid fa-paper-plane-top fa-rotate-270",
  });
  expect(accountFace("a-500foods-andrew", [
    { id: "a-500foods-andrew", number: 2, name: "Andrew", icon: "fa-sharp fa-thin fa-envelope" },
  ])).toEqual({
    number: "002",
    name: "Andrew",
    icon: "fa-sharp fa-thin fa-envelope",
  });
  expect(accountFace("missing", []).name).toBe("missing");
  expect(accountFace("missing", []).number).toBe("—");
  expect(kindFace("sync.mail").label).toBe("Mail sync");
  expect(kindFace("later.kind").label).toBe("later.kind");
});

test("a late event does not rewind progress or a finished row", () => {
  const running = { id: "act-1", state: "running", progress: { done: 4, total: 10 } };
  const older = { id: "act-1", state: "running", progress: { done: 1, total: 10 } };
  const done = { id: "act-1", state: "done", progress: { done: 10, total: 10 } };
  expect(preferActivity(running, older)).toBe(running);
  expect(preferActivity(done, running)).toBe(done);
  expect(preferActivity(running, done)).toBe(done);
  expect(syncingCount([
    { state: "running", kind: "sync.mail" },
    { state: "queued", kind: "sync.contacts" },
    { state: "running", kind: "body.fetch" },
  ])).toBe(1);
});

test("a sync snapshot sums running sync progress and ignores other work", () => {
  expect(syncSnapshot([
    { id: "a", state: "running", kind: "sync.mail", progress: { done: 1, total: 4 } },
    { id: "b", state: "running", kind: "sync.contacts", progress: { done: 1, total: 4 } },
    { id: "c", state: "queued", kind: "sync.files", progress: { done: 0, total: 4 } },
    { id: "d", state: "running", kind: "body.fetch", progress: { done: 3, total: 3 } },
  ])).toEqual({ count: 2, ids: ["a", "b"], turn: 25 });
  expect(syncSnapshot([
    { id: "a", state: "running", kind: "sync.mail", progress: { done: 2, total: -1 } },
  ])).toEqual({ count: 1, ids: ["a"], turn: null });
});
