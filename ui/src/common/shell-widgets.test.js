/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test } from "vitest";
import { createApi } from "../api.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";

afterEach(() => {
  unmount();
  document.body.replaceChildren();
});

test("the shell uses the menu, tooltip, toast, and link dialog", async () => {
  const transport = mockTransport();
  const call = transport.call;
  transport.call = async (cmd, args) => {
    const result = await call(cmd, args);
    if (cmd === "system.status" && result) {
      result.debug = true;
    }
    return result;
  };
  const el = document.createElement("div");
  document.body.append(el);
  const api = createApi(transport);
  await mount(el, api, { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  const buttons = [...root.querySelectorAll(".nav-buttons button")];
  expect(buttons.map((button) => button.getAttribute("aria-label"))).toEqual([
    "Mail",
    "Contacts",
    "Calendars",
    "Tasks",
    "Files",
    "Notes",
    "Argent",
    "Grok Bots",
    "GitHub",
    "Activity",
    "Settings",
    "Update",
    "Help",
  ]);

  api.prefs.ui.set = () => Promise.reject(new Error("disk full"));
  const activity = root.querySelector('[data-section="activity"]');
  activity.dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: 8, clientY: 16 }));
  const items = [...root.querySelectorAll("[role='menuitem']")];
  expect(items.map((item) => item.textContent)).toEqual(["Activity", "Logs"]);
  expect(items[1].getAttribute("aria-current")).toBe("true");
  items[0].click();
  expect(root.querySelector("#panel-activity").hidden).toBe(false);
  expect(root.querySelector("[data-activity-pane='activity']").hidden).toBe(false);
  expect(root.querySelector("[data-activity-pane='log']").hidden).toBe(true);
  await new Promise((resolve) => {
    setTimeout(resolve, 400);
  });
  const toast = root.querySelector(".pav-toast p");
  expect(toast.textContent).toBe("Could not save the profile. disk full");
  expect(root.querySelector("#banner").hidden).toBe(true);

  const mail = root.querySelector('[data-section="mail"]');
  mail.dispatchEvent(new KeyboardEvent("keydown", { key: "Tab", bubbles: true }));
  mail.dispatchEvent(new FocusEvent("focusin", { bubbles: true }));
  expect(root.querySelector(".pav-tooltip").textContent).toBe("Mail");
  expect(mail.getAttribute("aria-describedby")).toBe("pav-tooltip");

  transport.emit("link.clicked", { paneId: "isolation", n: 0 });
  expect(root.querySelector("#link-confirm")).toBeNull();
  expect(root.querySelector(".pav-dialog-title").textContent).toBe("This link wants to open:");
  expect(root.querySelector(".pav-dialog-text").textContent).toBe("https://example.com/pavillion-isolation-test");
  root.querySelector(".pav-dialog-ok").click();
  expect(root.querySelector("dialog")).toBeNull();
});
