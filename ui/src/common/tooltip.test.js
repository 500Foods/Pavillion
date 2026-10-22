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

test("D-UI Tooltips: delegation covers data-pav-tip and icon-only buttons added later", () => {
  vi.useFakeTimers();
  const root = document.createElement("div");
  document.body.append(root);
  const tips = createTooltip(root);
  const tip = root.querySelector(".pav-tooltip");
  const late = document.createElement("button");
  late.dataset.pavTip = "Refresh";
  late.append(document.createElement("i"));
  root.append(late);
  late.firstChild.dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
  vi.advanceTimersByTime(500);
  expect(tip.hidden).toBe(false);
  expect(tip.textContent).toBe("Refresh");
  late.dispatchEvent(new PointerEvent("pointerout", { bubbles: true }));
  const iconOnly = document.createElement("button");
  iconOnly.setAttribute("aria-label", "Close Inbox");
  root.append(iconOnly);
  iconOnly.dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
  vi.advanceTimersByTime(500);
  expect(tip.textContent).toBe("Close Inbox");
  iconOnly.dispatchEvent(new PointerEvent("pointerout", { bubbles: true }));
  const texted = document.createElement("button");
  texted.setAttribute("aria-label", "Save");
  texted.textContent = "Save";
  root.append(texted);
  texted.dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
  vi.advanceTimersByTime(500);
  expect(tip.hidden).toBe(true);
  tips.destroy();
});

test("an open tooltip follows a later data-pav-tip", async () => {
  vi.useFakeTimers();
  const root = document.createElement("div");
  document.body.append(root);
  const tips = createTooltip(root);
  const host = document.createElement("span");
  host.setAttribute("data-pav-tip", "25%");
  root.append(host);
  host.dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
  vi.advanceTimersByTime(500);
  const tip = root.querySelector(".pav-tooltip");
  expect(tip.textContent).toBe("25%");
  host.setAttribute("data-pav-tip", "40%");
  await Promise.resolve();
  expect(tip.textContent).toBe("40%");
  tips.destroy();
});
