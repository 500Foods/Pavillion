/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createApi } from "./api.js";
import { mount, unmount } from "./index.js";
import { mockTransport } from "./transport/mock.js";

function wheel(target, { deltaY = -120, deltaMode = 0, ctrlKey = true } = {}) {
  const event = new WheelEvent("wheel", {
    deltaY,
    deltaMode,
    ctrlKey,
    bubbles: true,
    cancelable: true,
  });
  target.dispatchEvent(event);
  return event;
}

function scale(key) {
  const label = document.querySelector(`[data-zoom="${key}"]`);
  return {
    value: label.querySelector("input").value,
    text: label.querySelector(".zoom-value").textContent,
  };
}

function release(input, value) {
  input.value = value;
  input.dispatchEvent(new Event("input", { bubbles: true }));
  input.dispatchEvent(new Event("change", { bubbles: true }));
}

async function settle() {
  await new Promise((resolve) => {
    setTimeout(resolve, 0);
  });
}

test("ctrl wheel steps the pane under the pointer and the switch flips direction", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const scales = [];
  const api = createApi(mockTransport());
  try {
    await mount(el, api, {
      hostServices: {
        setRootFontSize(value) {
          scales.push(value);
        },
      },
    });

    const direction = document.querySelector("#zoom-wheel");
    expect(direction.getAttribute("role")).toBe("switch");
    expect(direction.getAttribute("aria-checked")).toBe("false");
    expect(direction.textContent).toBe("Away zooms in");
    expect(scale("shell").value).toBe("1");
    expect(scales.every((value) => value === 1)).toBe(true);

    const month = document.querySelector("#month");
    expect(wheel(month, { deltaY: -40 }).defaultPrevented).toBe(true);
    expect(scale("navBody").value).toBe("1");
    wheel(month, { deltaY: -30 });
    expect(scale("navBody").value).toBe("1");
    wheel(month, { deltaY: -30 });
    expect(scale("navBody")).toEqual({ value: "1.05", text: "105%" });

    const bars = document.querySelector(".nav-buttons");
    expect(wheel(bars).defaultPrevented).toBe(true);
    expect(scale("navButtons")).toEqual({ value: "1.05", text: "105%" });
    expect(scale("shell").value).toBe("1");
    expect(bars.style.zoom).toBe("1.05");
    expect(document.querySelector(".tabs").style.zoom).toBe("1.05");

    wheel(document.querySelector(".tabs"));
    expect(scale("navButtons").value).toBe("1.1");
    expect(document.querySelector(".log-topbar").style.getPropertyValue("--pav-bar-zoom")).toBe("1.1");

    const plain = wheel(bars, { ctrlKey: false });
    expect(plain.defaultPrevented).toBe(false);
    expect(scale("navButtons").value).toBe("1.1");

    wheel(document.querySelector("#mini-cal"));
    expect(scale("navBody").value).toBe("1.1");

    const mailList = document.querySelector("#panel-mail .pane-list");
    const mailDetail = document.querySelector("#panel-mail .pane-detail");
    wheel(mailList);
    expect(scale("mailList").value).toBe("1.05");
    expect(wheel(mailDetail, { deltaY: 3, deltaMode: 1 }).defaultPrevented).toBe(true);
    expect(scale("mailDetail").value).toBe("0.95");
    wheel(mailDetail, { deltaY: 4, deltaMode: 2 });
    expect(scale("mailDetail").value).toBe("0.9");

    const contacts = document.querySelector("#panel-contacts .pane-list");
    expect(wheel(contacts).defaultPrevented).toBe(true);
    expect(scale("mailList").value).toBe("1.05");
    expect(wheel(document.querySelector("#zoom")).defaultPrevented).toBe(true);
    expect(scale("shell").value).toBe("1");
    expect(wheel(direction).defaultPrevented).toBe(true);
    expect(scale("navButtons").value).toBe("1.1");

    const logBar = document.querySelector(".log-topbar");
    const editor = document.querySelector(".log-editor");
    expect(editor).not.toBeNull();
    wheel(logBar);
    expect(scale("navButtons").value).toBe("1.15");
    expect(scale("log").value).toBe("1");
    wheel(editor);
    expect(scale("log")).toEqual({ value: "1.05", text: "105%" });
    expect(scale("navButtons").value).toBe("1.15");

    const log = document.querySelector('[data-zoom="log"] input');
    release(log, "5");
    wheel(editor);
    expect(scale("log").value).toBe("5");
    release(log, "0.05");
    wheel(editor, { deltaY: 120 });
    expect(scale("log")).toEqual({ value: "0.05", text: "5%" });

    const helpIndex = document.querySelector("#help-index");
    const helpArticle = document.querySelector("#help-article");
    wheel(helpIndex);
    expect(scale("helpIndex")).toEqual({ value: "1.05", text: "105%" });
    expect(helpIndex.style.zoom).toBe("1.05");
    expect(scale("navButtons").value).toBe("1.15");
    expect(wheel(helpArticle, { deltaY: 3, deltaMode: 1 }).defaultPrevented).toBe(true);
    expect(scale("helpContent").value).toBe("0.95");
    expect(helpArticle.style.zoom).toBe("0.95");
    wheel(document.querySelector(".help-bar"));
    expect(scale("navButtons").value).toBe("1.2");
    expect(scale("helpIndex").value).toBe("1.05");
    expect(scale("helpContent").value).toBe("0.95");

    direction.click();
    expect(direction.getAttribute("aria-checked")).toBe("true");
    expect(direction.textContent).toBe("Toward zooms in");
    wheel(mailList);
    expect(scale("mailList").value).toBe("1");
    expect(scale("shell").value).toBe("1");

    document.querySelector('input[value="dark"]').click();
    await settle();
    document.querySelector("#theme-undo").click();
    await settle();
    expect(document.querySelector('input[name="pav-theme"]:checked').value).toBe("system");
    expect(scale("mailList").value).toBe("1");
    expect(scale("navButtons").value).toBe("1.2");
    expect(scale("log").value).toBe("0.05");
    expect(scale("helpIndex").value).toBe("1.05");
    expect(scale("helpContent").value).toBe("0.95");
    expect(scales.every((value) => value === 1)).toBe(true);

    await new Promise((resolve) => {
      setTimeout(resolve, 400);
    });
    const stored = await api.prefs.ui.get();
    expect(stored.zoom.wheel).toBe("toward");
    expect(stored.zoom.shell).toBe(1);
    expect(stored.zoom.navButtons).toBe(1.2);
    expect(stored.zoom.navBody).toBe(1.1);
    expect(stored.zoom.mailList).toBe(1);
    expect(stored.zoom.mailDetail).toBe(0.9);
    expect(stored.zoom.log).toBe(0.05);
    expect(stored.zoom.helpIndex).toBe(1.05);
    expect(stored.zoom.helpContent).toBe(0.95);
  } finally {
    unmount();
    el.remove();
  }
}, 20000);
