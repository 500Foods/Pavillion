/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test, vi } from "vitest";
import { createToaster } from "./toast.js";

afterEach(() => {
  vi.useRealTimers();
  document.body.replaceChildren();
});

function setup() {
  const root = document.createElement("div");
  document.body.append(root);
  return { root, toaster: createToaster(root) };
}

test("the action runs once", () => {
  const { root, toaster } = setup();
  let runs = 0;
  toaster.toast("Removed", { action: "Undo", onAction() { runs += 1; } });
  const button = root.querySelector(".pav-toast button");
  expect(button.textContent).toBe("Undo");
  expect(root.querySelector(".pav-toasts").getAttribute("aria-live")).toBe("polite");
  button.click();
  button.click();
  expect(runs).toBe(1);
  expect(root.querySelector(".pav-toast")).toBeNull();
  toaster.destroy();
});

test("a toast leaves after the timeout", () => {
  vi.useFakeTimers();
  const { root, toaster } = setup();
  toaster.toast("Saved");
  expect(root.querySelector(".pav-toast")).not.toBeNull();
  vi.advanceTimersByTime(4999);
  expect(root.querySelector(".pav-toast")).not.toBeNull();
  vi.advanceTimersByTime(1);
  expect(root.querySelector(".pav-toast")).toBeNull();
  toaster.destroy();
});

test("text is not parsed as HTML", () => {
  const { root, toaster } = setup();
  toaster.toast("<b>no</b>", { timeoutMs: 0 });
  expect(root.querySelector("b")).toBeNull();
  expect(root.querySelector(".pav-toast p").textContent).toBe("<b>no</b>");
  toaster.destroy();
});
