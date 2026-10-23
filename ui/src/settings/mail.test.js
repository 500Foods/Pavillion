// @vitest-environment jsdom
import { afterEach, expect, test } from "vitest";
import { createApi } from "../api.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";
import { createMailSettings, namesFromText, textFromNames } from "./mail.js";

afterEach(() => {
  unmount();
  document.body.replaceChildren();
});

function tick() {
  return new Promise((resolve) => setTimeout(resolve, 0));
}

async function flush() {
  for (let i = 0; i < 8; i += 1) {
    await tick();
  }
}

test("names keep blanks out and keep a repeated name", () => {
  expect(namesFromText(" gap \n\ncolor\ngap ")).toEqual(["gap", "color", "gap"]);
  expect(textFromNames(["gap", "color"])).toBe("gap\ncolor");
  expect(textFromNames(null)).toBe("");
});

test("Settings → Mail saves extra properties and drops an unsaved edit on Refresh", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const transport = mockTransport();
  const api = createApi(transport);
  await mount(el, api, { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  root.querySelector('[data-heading="mail"]').click();
  await flush();
  const pane = root.querySelector('[data-settings="mail"]');
  expect(pane.hidden).toBe(false);
  expect(root.querySelector('[data-settings="features"]').hidden).toBe(true);
  expect(pane.querySelector(".pav-module-title").textContent).toBe("Mail");
  expect(pane.querySelector("#mail-toolbar").lastElementChild.classList.contains("help-jump")).toBe(true);
  expect(pane.querySelector(".help-jump").getAttribute("data-help-section")).toBe("mail");
  const field = pane.querySelector("#mail-css-extra");
  expect(field.value).toBe("");
  field.value = "gap\ncolor";
  pane.querySelector('[data-module-action="apply"]').click();
  await flush();
  expect(transport.seen.filter((item) => item.cmd === "prefs.set").pop().args).toEqual({
    mail: { cssExtra: ["gap", "color"] },
  });
  expect((await api.prefs.get()).mail).toEqual({ cssExtra: ["gap", "color"] });
  field.value = "position";
  pane.querySelector('[data-module-action="refresh"]').click();
  await flush();
  expect(field.value).toBe("gap\ncolor");
});

test("Apply reports a refused property and leaves the field", async () => {
  const root = document.createElement("div");
  // html-ok: static test fixture, not outside data.
  root.innerHTML = `
    <div id="mail-toolbar" class="button-bar"></div>
    <textarea id="mail-css-extra"></textarea>
    <div id="mail-footer" class="button-bar"></div>`;
  document.body.append(root);
  const errors = [];
  const transport = mockTransport();
  const settings = createMailSettings(createApi(transport), root, {
    onError(text) {
      errors.push(text);
    },
  });
  await settings.refresh();
  const field = root.querySelector("#mail-css-extra");
  field.value = "position";
  await settings.apply();
  expect(errors).toEqual(["cssExtra has a name outside the rule"]);
  expect(field.value).toBe("position");
  expect((await createApi(transport).prefs.get()).mail.cssExtra).toEqual([]);
  field.value = "";
  await settings.apply();
  expect(transport.seen.filter((item) => item.cmd === "prefs.set").pop().args).toEqual({
    mail: { cssExtra: [] },
  });
  settings.destroy();
});
