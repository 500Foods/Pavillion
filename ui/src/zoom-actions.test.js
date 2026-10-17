/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createApi } from "./api.js";
import { mount, unmount } from "./index.js";
import { mockTransport } from "./transport/mock.js";

function slide(input, value) {
  input.value = value;
  input.dispatchEvent(new Event("input", { bubbles: true }));
}

function release(input, value) {
  if (value !== undefined) {
    input.value = value;
    input.dispatchEvent(new Event("input", { bubbles: true }));
  }
  input.dispatchEvent(new Event("change", { bubbles: true }));
}

test("Default, Undo, and Redo walk the zoom sliders one gesture at a time", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const scales = [];
  try {
    await mount(el, createApi(mockTransport()), {
      hostServices: {
        setRootFontSize(scale) {
          scales.push(scale);
        },
      },
    });

    const bar = document.querySelector(".zoom-actions");
    const defaults = document.querySelector("#zoom-default");
    const undo = document.querySelector("#zoom-undo");
    const redo = document.querySelector("#zoom-redo");
    const shell = document.querySelector('[data-zoom="shell"] input');
    const buttons = document.querySelector('[data-zoom="navButtons"] input');
    const log = document.querySelector('[data-zoom="log"] input');
    expect(bar.closest("#panel-settings")).not.toBeNull();
    expect(defaults.disabled).toBe(true);
    expect(undo.disabled).toBe(true);
    expect(redo.disabled).toBe(true);
    expect(document.querySelector(".nav-buttons").style.zoom).toBe("1");
    expect(bar.style.zoom).toBe("");

    release(buttons, "2");
    expect(document.querySelector(".nav-buttons").style.zoom).toBe("2");
    expect(document.querySelector(".tabs").style.zoom).toBe("2");
    expect(bar.style.zoom).toBe("");
    expect(undo.disabled).toBe(false);
    undo.click();
    expect(buttons.value).toBe("1");
    expect(document.querySelector(".nav-buttons").style.zoom).toBe("1");
    expect(redo.disabled).toBe(false);
    redo.click();
    expect(buttons.value).toBe("2");

    slide(shell, "1.2");
    slide(shell, "1.5");
    release(shell);
    expect(scales.at(-1)).toBe(1.5);
    undo.click();
    expect(shell.value).toBe("1");
    expect(buttons.value).toBe("2");
    expect(scales.at(-1)).toBe(1);
    redo.click();
    expect(shell.value).toBe("1.5");

    release(log, "2");
    defaults.click();
    expect(defaults.disabled).toBe(true);
    expect(shell.value).toBe("1");
    expect(buttons.value).toBe("1");
    expect(log.value).toBe("1");
    expect(document.querySelector(".nav-buttons").style.zoom).toBe("1");
    undo.click();
    expect(shell.value).toBe("1.5");
    expect(buttons.value).toBe("2");
    expect(log.value).toBe("2");
    expect(bar.style.zoom).toBe("");
  } finally {
    unmount();
    el.remove();
  }
});
