/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test } from "vitest";
import { createMenu } from "./menu.js";

afterEach(() => {
  document.body.replaceChildren();
});

function setup() {
  const root = document.createElement("div");
  document.body.append(root);
  const button = document.createElement("button");
  button.type = "button";
  button.textContent = "Activity";
  root.append(button);
  const menu = createMenu(root);
  return { root, button, menu };
}

test("arrow keys move through items and wrap", () => {
  const { root, button, menu } = setup();
  menu.open(button, [
    { label: "Activity" },
    { label: "Logs" },
    { label: "Skip", disabled: true },
  ]);
  const items = [...root.querySelectorAll("[role='menuitem']")];
  expect(document.activeElement).toBe(items[0]);
  items[0].dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowDown", bubbles: true }));
  expect(document.activeElement).toBe(items[1]);
  items[1].dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowDown", bubbles: true }));
  expect(document.activeElement).toBe(items[0]);
  items[0].dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true }));
  expect(document.activeElement).toBe(items[1]);
  menu.destroy();
});

test("Escape closes the menu and returns focus", () => {
  const { root, button, menu } = setup();
  menu.bind(button, () => [{ label: "Logs" }]);
  button.focus();
  const openKeys = new KeyboardEvent("keydown", { key: "F10", shiftKey: true, bubbles: true, cancelable: true });
  button.dispatchEvent(openKeys);
  expect(openKeys.defaultPrevented).toBe(true);
  expect(root.querySelector("[role='menu']")).not.toBeNull();
  const item = root.querySelector("[role='menuitem']");
  const escape = new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true });
  item.dispatchEvent(escape);
  expect(escape.defaultPrevented).toBe(true);
  expect(root.querySelector("[role='menu']")).toBeNull();
  expect(document.activeElement).toBe(button);
  menu.destroy();
});

test("Enter runs the item once", () => {
  const { root, button, menu } = setup();
  let runs = 0;
  menu.open(button, [{ label: "Logs", onSelect() { runs += 1; } }]);
  const item = root.querySelector("[role='menuitem']");
  item.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true, cancelable: true }));
  expect(runs).toBe(1);
  expect(root.querySelector("[role='menu']")).toBeNull();
  menu.destroy();
});

test("a context menu prevents the browser menu", () => {
  const { root, button, menu } = setup();
  menu.bind(button, () => [{ label: "Activity", current: true }]);
  const event = new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: 12, clientY: 20 });
  button.dispatchEvent(event);
  expect(event.defaultPrevented).toBe(true);
  const item = root.querySelector("[role='menuitem']");
  expect(item.textContent).toBe("Activity");
  expect(item.getAttribute("aria-current")).toBe("true");
  menu.destroy();
});
