/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { accountSwatch, iconChoice, iconSnippet } from "./account-icon.js";
import { featureOn, FEATURES } from "./accounts-table.js";

test("interim icons are FA snippets; envelope is empty", () => {
  expect(iconSnippet("envelope")).toBe("");
  expect(iconSnippet("star")).toBe('<i class="fa-solid fa-star"></i>');
  expect(iconChoice("")).toBe("envelope");
  expect(iconChoice('<i class="fa-solid fa-star"></i>')).toBe("star");
  expect(iconChoice('<span class="fa-layers"></span>')).toBeNull();
});

test("the swatch draws on the account color and refuses unsafe markup", () => {
  const ok = accountSwatch({ icon: '<i class="fa-solid fa-star"></i>', color: "#123456" });
  expect(ok.style.backgroundColor).toBe("rgb(18, 52, 86)");
  expect(ok.querySelector("i").className).toContain("fa-star");
  const bad = accountSwatch({ icon: '<img src="x" onerror="alert(1)">', color: "#123456" });
  expect(bad.querySelector("img")).toBeNull();
  expect(bad.querySelector("i").className).toContain("fa-envelope");
});

test("a feature box needs the tested capability and the sync flag", () => {
  const mail = FEATURES.find((f) => f.key === "mail");
  const files = FEATURES.find((f) => f.key === "files");
  expect(featureOn({ syncMail: true }, mail, null)).toBe(false);
  expect(featureOn({ syncMail: true }, mail, ["mail"])).toBe(true);
  expect(featureOn({ syncMail: false }, mail, ["mail"])).toBe(false);
  expect(featureOn({}, files, ["files"])).toBe(true);
});

test("checkedTitle shows 24-hour local time in the profile zone (K-Q10)", async () => {
  const { checkedTitle } = await import("./accounts-table.js");
  expect(checkedTitle("2026-10-19T17:05:09.000Z", "America/Vancouver")).toBe("Checked 2026-10-19 10:05:09");
  expect(checkedTitle("2026-10-19T17:05:09Z", "Europe/Berlin")).toBe("Checked 2026-10-19 19:05:09");
  expect(checkedTitle(null, "America/Vancouver")).toBe("Not checked yet");
});

test("serverGroup labels by host only, else (no server) (K-Q11)", async () => {
  const { serverGroup, accountCountText, NO_SERVER } = await import("./accounts-table.js");
  expect(serverGroup("https://mail.500foods.com/jmap/session")).toBe("mail.500foods.com");
  expect(serverGroup(" https://h.example:8443/x ")).toBe("h.example");
  expect(serverGroup("")).toBe(NO_SERVER);
  expect(serverGroup(undefined)).toBe("(no server)");
  expect(serverGroup("not a url")).toBe("(no server)");
  expect(accountCountText(0)).toBe("0 accounts");
  expect(accountCountText(1)).toBe("1 account");
  expect(accountCountText(3)).toBe("3 accounts");
});

test("navigator state and targets at the ends (K-Q12)", async () => {
  const { navState, navTarget } = await import("./accounts-table.js");
  const ids = ["a", "b", "c"];
  expect(navState(ids, "a")).toEqual({ first: false, prev: false, next: true, last: true });
  expect(navState(ids, "b")).toEqual({ first: true, prev: true, next: true, last: true });
  expect(navState(ids, "c")).toEqual({ first: true, prev: true, next: false, last: false });
  expect(navState([], null)).toEqual({ first: false, prev: false, next: false, last: false });
  expect(navState(["a"], "a")).toEqual({ first: false, prev: false, next: false, last: false });
  expect(navTarget(ids, "b", "prev")).toBe("a");
  expect(navTarget(ids, "c", "next")).toBe("c");
  expect(navTarget(ids, "a", "last")).toBe("c");
  expect(navTarget(ids, null, "next")).toBe("a");
  expect(navTarget([], null, "first")).toBeNull();
});

test("CSV columns are number, name, server, features, email only (K-Q12)", async () => {
  const { accountColumns, exportFields, EXPORT_FIELDS, tableMinWidth, accountRows } = await import("./accounts-table.js");
  const columns = accountColumns(document);
  expect(exportFields(columns)).toEqual(EXPORT_FIELDS);
  expect(EXPORT_FIELDS).toEqual(["number", "name", "server", "mail", "calendars", "contacts", "files", "email"]);
  expect(columns.find((c) => c.field === "mail").accessorDownload(true)).toBe("yes");
  expect(columns.find((c) => c.field === "files").accessorDownload(false)).toBe("no");
  const row = accountRows([{ id: "x", name: "X", sessionUrl: "https://h.example/j", username: "u", email: "e@h.example", secret: "s" }], "UTC")[0];
  for (const field of Object.keys(row)) {
    expect(["secret", "username", "password"]).not.toContain(field);
  }
  expect(tableMinWidth(columns)).toBe(60 + 44 + 120 + 104 + 60 * 3);
  /* Every visible column sorts; feature columns by checked state. */
  for (const column of columns.filter((c) => c.visible !== false)) {
    expect(column.headerSort).not.toBe(false);
    expect(column.sorter).toBeDefined();
  }
  expect(columns.find((c) => c.field === "contacts").sorter).toBe("boolean");
  expect(columns.find((c) => c.field === "calendars").bottomCalc([true, false, true])).toBe(2);
  expect(columns.find((c) => c.field === "name").bottomCalc(["a"])).toBe("1 account");
});

test("chooser columns lock Name and cleanHidden keeps known unlocked fields", async () => {
  const { CHOOSER_COLUMNS, cleanHidden, accountColumns } = await import("./accounts-table.js");
  const name = CHOOSER_COLUMNS.find((c) => c.field === "name");
  expect(name.locked).toBe(true);
  expect(cleanHidden(["name", "number", "number", "bogus", 7])).toEqual(["number"]);
  expect(cleanHidden(null)).toEqual([]);
  const cols = accountColumns(document, ["number"]);
  expect(cols.find((c) => c.field === "number").visible).toBe(false);
  expect(cols.find((c) => c.field === "name").visible).not.toBe(false);
});
