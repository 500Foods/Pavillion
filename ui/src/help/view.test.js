/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createApi } from "../api.js";
import { filterHelp } from "./view.js";
import { helpPages } from "./pages.js";
import { APP_VERSION } from "../version.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";

test("help search keeps a title hit whole and a text hit narrow", () => {
  expect(helpPages[0].id).toBe("app");
  expect(helpPages[0].title).toBe(`Pavillion ${APP_VERSION}`);
  expect(helpPages[1].id).toBe("introduction");
  expect(filterHelp(helpPages, "").map((hit) => hit.page.id)).toContain("developer");
  const titled = filterHelp(helpPages, "Mail");
  const mail = titled.find((hit) => hit.page.id === "mail");
  expect(mail.sectionIds).toEqual(helpPages.find((page) => page.id === "mail").sections.map((section) => section.id));
  const zoom = filterHelp(helpPages, "toward");
  expect(zoom.map((hit) => hit.page.id)).toEqual(["settings"]);
  expect(zoom[0].sectionIds).toEqual(["zoom"]);
  expect(filterHelp(helpPages, "no-such-token")).toEqual([]);
  const schema = filterHelp(helpPages, "pavillion-index");
  expect(schema.map((hit) => hit.page.id)).toEqual(["developer"]);
  expect(schema[0].sectionIds).toEqual(["index"]);
});

test("the help button opens the index and search opens a page", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    await mount(el, createApi(mockTransport()));
    document.querySelector('[data-section="help"]').click();
    const panel = document.querySelector("#panel-help");
    expect(panel.hidden).toBe(false);
    expect(document.querySelector("#help-article h2").textContent).toBe(`Pavillion ${APP_VERSION}`);
    expect(document.querySelector("#help-index").textContent).toContain("Developer");
    expect(document.querySelector("#open-test-tab").hidden).toBe(true);

    const search = document.querySelector("#help-search");
    search.value = "toward";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    expect(document.querySelector("#help-index").textContent).toContain("Zoom");
    expect(document.querySelector("#help-index").textContent).not.toContain("Introduction");

    document.querySelector('#help-index button[data-help-section="zoom"]').click();
    expect(document.querySelector("#help-article h2").textContent).toBe("Settings");
    expect(document.querySelector("#help-article").textContent).toContain("Away zooms in");
    expect(document.querySelector("#help-sec-settings-zoom").textContent).toContain("toward");

    search.value = "no-such-token";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    expect(document.querySelector("#help-article").textContent).toBe("No matching pages.");

    search.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    expect(search.value).toBe("");
    expect(document.querySelector("#help-article h2").textContent).toBe("Settings");
    expect(document.querySelector("#help-index").textContent).toContain("Introduction");
  } finally {
    unmount();
    el.remove();
  }
});

test("dragging the help bar resizes the index and saves the ratio", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const api = createApi(mockTransport());
  try {
    await mount(el, api);
    const handle = document.querySelector('[data-split="help"]');
    const index = document.querySelector("#help-index");
    const article = document.querySelector("#help-article");
    expect(handle.getAttribute("aria-label")).toBe("Resize help panes");
    expect(handle.previousElementSibling).toBe(index);
    expect(handle.nextElementSibling).toBe(article);
    expect(parseFloat(index.style.flexBasis)).toBeCloseTo(25);

    const parent = handle.parentElement;
    parent.getBoundingClientRect = () => ({
      left: 0,
      width: 1000,
      top: 0,
      height: 400,
      right: 1000,
      bottom: 400,
      x: 0,
      y: 0,
      toJSON() {
        return {};
      },
    });
    handle.setPointerCapture = () => {};
    const pointer = (type, clientX) => new PointerEvent(type, {
      bubbles: true,
      cancelable: true,
      clientX,
      button: 0,
      pointerId: 1,
    });
    handle.dispatchEvent(pointer("pointerdown", 400));
    handle.dispatchEvent(pointer("pointermove", 400));
    expect(parseFloat(index.style.flexBasis)).toBeCloseTo(40);
    handle.dispatchEvent(pointer("pointerup", 40));
    expect(parseFloat(index.style.flexBasis)).toBeCloseTo(15);
    handle.dispatchEvent(pointer("pointerdown", 900));
    handle.dispatchEvent(pointer("pointerup", 900));
    expect(parseFloat(index.style.flexBasis)).toBeCloseTo(60);

    await new Promise((resolve) => {
      setTimeout(resolve, 400);
    });
    const stored = await api.prefs.ui.get();
    expect(stored.help.split).toBeCloseTo(0.6);
    expect(stored.help.min).toBe(0.15);
    expect(stored.help.max).toBe(0.6);
  } finally {
    unmount();
    el.remove();
  }
});

test("D-10: the Help toolbar has the title, search, then Back to the opener", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    await mount(el, createApi(mockTransport()));
    const bar = document.querySelector("#help-toolbar");
    expect(bar.querySelector(".pav-module-title").textContent).toBe("Help");
    const order = [...bar.children].filter((c) => !c.hidden).map((c) => c.id || c.className);
    expect(order.indexOf("help-search")).toBeLessThan(order.indexOf("help-back"));
    document.querySelector('[data-section="settings"]').click();
    document.querySelector('[data-heading="accounts"]').click();
    document.querySelector('.help-jump[data-help-section="accounts"]').click();
    expect(document.querySelector("#panel-help").hidden).toBe(false);
    const back = document.querySelector("#help-back");
    expect(back.disabled).toBe(false);
    back.click();
    expect(document.querySelector("#panel-help").hidden).toBe(true);
    expect(document.querySelector('[data-settings="accounts"]').hidden).toBe(false);
  } finally {
    unmount();
    el.remove();
  }
});
