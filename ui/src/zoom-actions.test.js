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

test("Tables, Details, Settings, Color picker, and Icon Wizard each scale their own surface", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const api = createApi(mockTransport());
  try {
    await mount(el, api, { hostServices: {} });
    release(document.querySelector('[data-zoom="details"] input'), "1.5");
    expect(document.querySelector(".accounts-form-pane").style.zoom).toBe("1.5");
    expect(document.querySelector("#libraries-form").style.zoom).toBe("1.5");
    expect(document.querySelector("#assets-form").style.zoom).toBe("1.5");
    expect(document.querySelector("#icons-form").style.zoom).toBe("");
    expect(document.querySelector("#accounts-tree").style.zoom).toBe("");

    release(document.querySelector('[data-zoom="settings"] input'), "1.25");
    expect(document.querySelector("[data-settings='features'] .settings-body").style.zoom).toBe("1.25");
    expect(document.querySelector("[data-settings='accounts'] .settings-body").style.zoom).toBe("");
    expect(document.querySelector("#theme-list").style.zoom).toBe("1.25");
    expect(document.querySelector(".theme-bar").style.zoom).toBe("1");
    const features = document.querySelector("[data-settings='features'] .settings-body");
    features.dispatchEvent(new WheelEvent("wheel", { deltaY: -120, ctrlKey: true, bubbles: true, cancelable: true }));
    expect(document.querySelector('[data-zoom="settings"] input').value).toBe("1.3");

    release(document.querySelector('[data-zoom="mailList"] input'), "1.4");
    expect(document.querySelector("#panel-mail .pane-list").style.zoom).toBe("");
    expect(document.querySelector("#help-filter-panel").style.getPropertyValue("--pav-filter-zoom")).toBe("1");
    release(document.querySelector('[data-zoom="filters"] input'), "1.6");
    expect(document.querySelector("#help-filter-panel").style.getPropertyValue("--pav-filter-zoom")).toBe("1.6");
    expect(document.querySelector(".help-bar").style.getPropertyValue("--pav-bar-zoom")).toBe("1");

    release(document.querySelector('[data-zoom="colorPickerZoom"] input'), "2.5");
    release(document.querySelector('[data-zoom="iconWizardZoom"] input'), "1.5");
    release(document.querySelector('[data-zoom="iconResultZoom"] input'), "1.25");
    await new Promise((resolve) => {
      setTimeout(resolve, 400);
    });
    const stored = await api.prefs.ui.get();
    expect(stored.zoom.details).toBe(1.5);
    expect(stored.zoom.settings).toBe(1.3);
    expect(stored.zoom.mailList).toBe(1.4);
    expect(stored.zoom.filters).toBe(1.6);
    expect(stored.zoom.tables).toBe(1);
    expect(stored.zoom.colorPickerZoom).toBeUndefined();
    expect(stored.colorPickerZoom).toBe(2.5);
    expect(stored.iconWizardZoom).toBe(1.5);
    expect(stored.iconResultZoom).toBe(1.25);
    expect(stored.zoom.iconResultZoom).toBeUndefined();
  } finally {
    unmount();
    el.remove();
  }
}, 20000);
