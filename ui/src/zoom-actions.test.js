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

async function settle() {
  await new Promise((resolve) => {
    setTimeout(resolve, 0);
  });
}

test("zoom sliders scale the panes, and theme history leaves zoom alone", async () => {
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

    const themeDefault = document.querySelector("#theme-default");
    const themeUndo = document.querySelector("#theme-undo");
    const themeRedo = document.querySelector("#theme-redo");
    const shell = document.querySelector('[data-zoom="shell"] input');
    const buttons = document.querySelector('[data-zoom="navButtons"] input');
    const log = document.querySelector('[data-zoom="log"] input');
    const checked = () => document.querySelector('input[name="pav-theme"]:checked').value;
    expect(document.querySelector(".zoom-actions")).toBeNull();
    expect(document.querySelector("#zoom-default")).toBeNull();
    expect(themeDefault.closest("[data-settings='theme']")).not.toBeNull();
    expect(themeDefault.disabled).toBe(true);
    expect(themeUndo.disabled).toBe(true);
    expect(themeRedo.disabled).toBe(true);
    expect(document.querySelector(".nav-buttons").style.zoom).toBe("1");
    expect(document.querySelector(".nav-buttons").style.getPropertyValue("--pav-bar-zoom")).toBe("1");

    release(buttons, "2");
    expect(document.querySelector(".nav-buttons").style.zoom).toBe("2");
    expect(document.querySelector(".nav-buttons").style.getPropertyValue("--pav-bar-zoom")).toBe("2");
    expect(document.querySelector(".tabs").style.zoom).toBe("2");
    expect(document.querySelector(".log-topbar").style.getPropertyValue("--pav-bar-zoom")).toBe("2");
    expect(document.querySelector(".theme-bar").style.zoom).toBe("2");
    expect(document.querySelector(".theme-actions").style.zoom).toBe("2");
    expect(themeUndo.disabled).toBe(true);
    expect(buttons.value).toBe("2");

    slide(shell, "1.2");
    slide(shell, "1.5");
    release(shell);
    expect(scales.at(-1)).toBe(1.5);
    release(log, "2");
    expect(shell.value).toBe("1.5");
    expect(log.value).toBe("2");

    document.querySelector('input[value="dark"]').click();
    await settle();
    expect(checked()).toBe("dark");
    expect(themeUndo.disabled).toBe(false);
    expect(themeDefault.disabled).toBe(false);
    expect(shell.value).toBe("1.5");
    expect(buttons.value).toBe("2");
    expect(log.value).toBe("2");

    themeUndo.click();
    await settle();
    expect(checked()).toBe("system");
    expect(themeRedo.disabled).toBe(false);
    expect(buttons.value).toBe("2");
    expect(shell.value).toBe("1.5");

    themeRedo.click();
    await settle();
    expect(checked()).toBe("dark");
    expect(log.value).toBe("2");

    themeDefault.click();
    await settle();
    expect(checked()).toBe("system");
    expect(themeDefault.disabled).toBe(true);
    expect(shell.value).toBe("1.5");
    expect(buttons.value).toBe("2");
    expect(log.value).toBe("2");
    expect(document.querySelector(".nav-buttons").style.zoom).toBe("3");

    themeUndo.click();
    await settle();
    expect(checked()).toBe("dark");
    expect(shell.value).toBe("1.5");
    expect(buttons.value).toBe("2");
    expect(log.value).toBe("2");
  } finally {
    unmount();
    el.remove();
  }
}, 20000);
