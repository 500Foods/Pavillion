/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test, vi } from "vitest";
import { createTooltip } from "./tooltip.js";

afterEach(() => {
  vi.useRealTimers();
  document.body.replaceChildren();
});

function setup() {
  const root = document.createElement("div");
  document.body.append(root);
  const button = document.createElement("button");
  button.type = "button";
  button.textContent = "Mail";
  root.append(button);
  const tips = createTooltip(root);
  tips.bind(button, "Mail");
  return { root, button, tips };
}

test("a hover waits 500 ms and a pointer leave cancels it", () => {
  vi.useFakeTimers();
  const { root, button, tips } = setup();
  button.dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
  vi.advanceTimersByTime(499);
  expect(root.querySelector(".pav-tooltip").hidden).toBe(true);
  button.dispatchEvent(new PointerEvent("pointerout", { bubbles: true }));
  vi.advanceTimersByTime(500);
  expect(root.querySelector(".pav-tooltip").hidden).toBe(true);
  button.dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
  vi.advanceTimersByTime(500);
  const tip = root.querySelector(".pav-tooltip");
  expect(tip.hidden).toBe(false);
  expect(tip.textContent).toBe("Mail");
  expect(tip.getAttribute("role")).toBe("tooltip");
  expect(tip.dataset.side).toBe("bottom");
  expect(tip.style.getPropertyValue("--pav-tooltip-shift")).not.toBe("");
  expect(button.getAttribute("aria-describedby")).toBe("pav-tooltip");
  tips.destroy();
});

test("keyboard focus shows the tooltip and a click does not", () => {
  const { root, button, tips } = setup();
  button.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true }));
  button.dispatchEvent(new FocusEvent("focusin", { bubbles: true }));
  expect(root.querySelector(".pav-tooltip").hidden).toBe(true);
  button.dispatchEvent(new KeyboardEvent("keydown", { key: "Tab", bubbles: true }));
  button.dispatchEvent(new FocusEvent("focusin", { bubbles: true }));
  const tip = root.querySelector(".pav-tooltip");
  expect(tip.hidden).toBe(false);
  expect(tip.textContent).toBe("Mail");
  expect(tip.dataset.side).toBe("bottom");
  expect(button.getAttribute("aria-describedby")).toBe("pav-tooltip");
  tips.destroy();
  expect(root.querySelector(".pav-tooltip")).toBeNull();
});
