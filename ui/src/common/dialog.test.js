/**
 * @vitest-environment jsdom
 */
import { afterEach, expect, test } from "vitest";
import { createDialogs } from "./dialog.js";
import { installIsolationTest } from "./isolation.js";

afterEach(() => {
  document.body.replaceChildren();
});

function setup() {
  const root = document.createElement("div");
  document.body.append(root);
  return { root, dialogs: createDialogs(root) };
}

test("confirm resolves true, false, and escape", async () => {
  const { root, dialogs } = setup();
  const accepted = dialogs.confirm({ title: "Keep", text: "Save it", okLabel: "Keep" });
  expect(root.querySelector(".pav-dialog-title").textContent).toBe("Keep");
  expect(root.querySelector(".pav-dialog-text").textContent).toBe("Save it");
  expect(root.querySelector(".pav-dialog-ok").textContent).toBe("Keep");
  expect(root.querySelector("b")).toBeNull();
  root.querySelector(".pav-dialog-ok").click();
  await expect(accepted).resolves.toBe(true);

  const declined = dialogs.confirm({ title: "<b>no</b>", text: "<i>x</i>" });
  expect(root.querySelector("b")).toBeNull();
  expect(root.querySelector("i")).toBeNull();
  expect(root.querySelector(".pav-dialog-title").textContent).toBe("<b>no</b>");
  root.querySelector(".pav-dialog-cancel").click();
  await expect(declined).resolves.toBe(false);

  const escaped = dialogs.confirm({ title: "Leave", text: "Now" });
  root.querySelector("dialog").dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
  await expect(escaped).resolves.toBe(false);
  expect(root.querySelector("dialog")).toBeNull();
  dialogs.destroy();
});

test("prompt resolves the typed value or null", async () => {
  const { root, dialogs } = setup();
  const named = dialogs.prompt({ title: "Name", text: "Who", value: "Ada" });
  const input = root.querySelector(".pav-dialog-input");
  expect(input.value).toBe("Ada");
  expect(document.activeElement).toBe(input);
  input.value = "Bea";
  root.querySelector(".pav-dialog-ok").click();
  await expect(named).resolves.toBe("Bea");

  const cancelled = dialogs.prompt({ title: "Name", value: "Ada" });
  root.querySelector(".pav-dialog-cancel").click();
  await expect(cancelled).resolves.toBeNull();
  dialogs.destroy();
});

test("Tab stays inside the dialog and danger focuses Cancel", async () => {
  const { root, dialogs } = setup();
  const pending = dialogs.confirm({ title: "Delete", text: "Gone", danger: true });
  const cancel = root.querySelector(".pav-dialog-cancel");
  const ok = root.querySelector(".pav-dialog-ok");
  expect(ok.classList.contains("is-danger")).toBe(true);
  expect(document.activeElement).toBe(cancel);
  ok.focus();
  ok.dispatchEvent(new KeyboardEvent("keydown", { key: "Tab", bubbles: true, cancelable: true }));
  expect(document.activeElement).toBe(cancel);
  cancel.click();
  await expect(pending).resolves.toBe(false);
  dialogs.destroy();
});

test("a link click uses confirm and does not build the old dialog", () => {
  const { root, dialogs } = setup();
  root.insertAdjacentHTML("beforeend", '<button type="button" id="isolation-test" hidden></button><div id="panel-mail"><div class="pane-detail"></div></div>');
  let handler = null;
  const stop = installIsolationTest({
    api: {
      on(_name, fn) {
        handler = fn;
        return () => {};
      },
    },
    enabled: true,
    root,
    openMail() {},
    confirm: (spec) => dialogs.confirm(spec),
  });
  handler({ paneId: "isolation", n: 0 });
  expect(root.querySelector("#link-confirm")).toBeNull();
  expect(root.querySelector(".pav-dialog-title").textContent).toBe("This link wants to open:");
  expect(root.querySelector(".pav-dialog-text").textContent).toBe("https://example.com/pavillion-isolation-test");
  expect(root.querySelector(".pav-dialog-ok").textContent).toBe("Close");
  root.querySelector(".pav-dialog-ok").click();
  stop();
  dialogs.destroy();
});
