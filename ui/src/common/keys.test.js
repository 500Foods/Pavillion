/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test, vi } from "vitest";
import { createKeys } from "./keys.js";

const open = [];

afterEach(() => {
  vi.useRealTimers();
  while (open.length > 0) {
    open.pop().destroy();
  }
  document.body.replaceChildren();
});

function setup() {
  const root = document.createElement("div");
  document.body.append(root);
  const keys = createKeys(root);
  open.push(keys);
  const press = (key, extra = {}) => {
    const event = new KeyboardEvent("keydown", {
      key,
      bubbles: true,
      cancelable: true,
      ...extra,
    });
    root.dispatchEvent(event);
    return event;
  };
  return { root, keys, press };
}

test("the active scope owns its key and falls through when it has none", () => {
  const { keys, press } = setup();
  const seen = [];
  keys.register({ id: "tree.star", scope: "tree", chords: [["*"]], run() { seen.push("tree"); } });
  keys.register({ id: "list.star", scope: "list", chords: [["*"]], run() { seen.push("list"); } });
  keys.register({ id: "tab.close", scope: "workspace", chords: [["Ctrl+W"]], run() { seen.push("close"); } });
  keys.register({ id: "section.mail", scope: "global", chords: [["Ctrl+1"]], run() { seen.push("mail"); } });
  keys.register({ id: "tree.mail", scope: "tree", chords: [["Ctrl+1"]], run() { seen.push("tree-mail"); } });

  keys.setScope("list");
  press("*");
  press("w", { ctrlKey: true });
  press("1", { ctrlKey: true });
  expect(seen).toEqual(["list", "close", "mail"]);

  keys.setScope("tree");
  press("*");
  press("1", { ctrlKey: true });
  expect(seen).toEqual(["list", "close", "mail", "tree", "tree-mail"]);
  expect(keys.list().map((binding) => binding.id)).toEqual([
    "tree.star",
    "list.star",
    "tab.close",
    "section.mail",
    "tree.mail",
  ]);
});

test("a chord fires on the second key and a different second key does not", () => {
  vi.useFakeTimers();
  const { keys, press } = setup();
  const seen = [];
  keys.register({
    id: "go.inbox",
    scope: "workspace",
    chords: [["g", "i"]],
    run() { seen.push("gi"); },
  });
  keys.register({
    id: "go",
    scope: "list",
    chords: [["g"]],
    run() { seen.push("g"); },
  });

  press("g");
  press("x");
  expect(seen).toEqual([]);
  press("g");
  press("i");
  expect(seen).toEqual(["gi"]);

  keys.setScope("list");
  press("g");
  vi.advanceTimersByTime(1000);
  press("i");
  expect(seen).toEqual(["gi", "g"]);
});

test("a short binding waits out a longer chord and then fires", () => {
  vi.useFakeTimers();
  const { keys, press } = setup();
  const seen = [];
  keys.register({ id: "go", scope: "workspace", chords: [["g"]], run() { seen.push("g"); } });
  keys.register({ id: "go.inbox", scope: "workspace", chords: [["g", "i"]], run() { seen.push("gi"); } });
  press("g");
  expect(seen).toEqual([]);
  vi.advanceTimersByTime(1000);
  expect(seen).toEqual(["g"]);
  press("g");
  press("i");
  expect(seen).toEqual(["g", "gi"]);
});

test("the same id replaces a binding", () => {
  const { keys, press } = setup();
  const seen = [];
  keys.register({ id: "tab.close", scope: "workspace", chords: [["Ctrl+W"]], run() { seen.push("w"); } });
  keys.register({ id: "tab.close", scope: "workspace", chords: [["Ctrl+Q"]], run() { seen.push("q"); } });
  press("w", { ctrlKey: true });
  press("q", { ctrlKey: true });
  expect(seen).toEqual(["q"]);
  expect(keys.list()).toEqual([{ id: "tab.close", scope: "workspace", chords: [["Ctrl+q"]] }]);
});

test("a text field, a dialog, and a menu do not take shortcuts", () => {
  const { root, keys, press } = setup();
  let ran = 0;
  keys.register({ id: "tab.close", scope: "workspace", chords: [["Ctrl+W"], ["Ctrl+F4"]], run() { ran += 1; } });
  const input = document.createElement("input");
  root.append(input);
  input.focus();
  const typed = press("w", { ctrlKey: true });
  expect(typed.defaultPrevented).toBe(false);
  expect(ran).toBe(0);

  input.blur();
  const dialog = document.createElement("dialog");
  dialog.open = true;
  root.append(dialog);
  press("F4", { ctrlKey: true });
  expect(ran).toBe(0);
  dialog.remove();

  const menu = document.createElement("div");
  menu.className = "pav-menu";
  root.append(menu);
  press("w", { ctrlKey: true });
  expect(ran).toBe(0);
  menu.remove();

  const field = document.createElement("div");
  field.setAttribute("contenteditable", "true");
  root.append(field);
  field.focus();
  press("w", { ctrlKey: true });
  expect(ran).toBe(0);
  field.remove();

  const allowed = press("w", { ctrlKey: true });
  expect(allowed.defaultPrevented).toBe(true);
  expect(ran).toBe(1);
});

test("a shortcut still fires when focus has dropped to the page", () => {
  const { root, keys } = setup();
  let ran = 0;
  keys.register({ id: "tab.close", scope: "workspace", chords: [["Ctrl+W"]], run() { ran += 1; } });
  const button = document.createElement("button");
  button.type = "button";
  button.textContent = "17";
  root.append(button);
  button.focus();
  button.blur();
  expect(document.activeElement).toBe(document.body);
  const fromPage = new KeyboardEvent("keydown", {
    key: "w",
    ctrlKey: true,
    bubbles: true,
    cancelable: true,
  });
  document.body.dispatchEvent(fromPage);
  expect(fromPage.defaultPrevented).toBe(true);
  expect(ran).toBe(1);

  const outside = document.createElement("button");
  outside.type = "button";
  document.body.append(outside);
  outside.focus();
  const foreign = new KeyboardEvent("keydown", {
    key: "w",
    ctrlKey: true,
    bubbles: true,
    cancelable: true,
  });
  outside.dispatchEvent(foreign);
  expect(foreign.defaultPrevented).toBe(false);
  expect(ran).toBe(1);
});
