/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createApi } from "../api.js";
import { filterHelp } from "./view.js";
import { helpPages } from "./pages.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";

test("help search keeps a title hit whole and a text hit narrow", () => {
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
    expect(document.querySelector("#help-article h2").textContent).toBe("Introduction");
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
