// @vitest-environment jsdom
import { afterEach, expect, test } from "vitest";
import { createApi } from "../api.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";
import { createLimitsSettings } from "./limits.js";

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

function pane() {
  const root = document.createElement("div");
  // html-ok: static test fixture, not outside data.
  root.innerHTML = `
    <div id="limits-toolbar" class="button-bar" role="toolbar"></div>
    <input id="limits-bodies" type="number" min="1" max="1048576" step="1" value="2048" />
    <input id="limits-blobs" type="number" min="1" max="1048576" step="1" value="4096" />
    <div id="limits-footer" class="button-bar" role="toolbar"></div>`;
  document.body.append(root);
  return root;
}

test("Refresh loads both limits and drops an unsaved edit", async () => {
  const transport = mockTransport();
  const root = pane();
  const settings = createLimitsSettings(createApi(transport), root);
  await settings.refresh();
  expect(root.querySelector("#limits-bodies").value).toBe("2048");
  expect(root.querySelector("#limits-blobs").value).toBe("4096");
  root.querySelector("#limits-bodies").value = "10";
  root.querySelector('[data-module-action="refresh"]').click();
  await tick();
  expect(root.querySelector("#limits-bodies").value).toBe("2048");
  expect(transport.seen.some((item) => item.cmd === "prefs.set")).toBe(false);
  settings.destroy();
});

test("Apply writes both cache fields and clamps the page", async () => {
  const transport = mockTransport();
  const root = pane();
  const settings = createLimitsSettings(createApi(transport), root);
  const bodies = root.querySelector("#limits-bodies");
  const blobs = root.querySelector("#limits-blobs");
  bodies.value = "0";
  blobs.value = "2000000";
  root.querySelector('[data-module-action="apply"]').click();
  await tick();
  expect(transport.seen.filter((item) => item.cmd === "prefs.set").pop().args).toEqual({
    cache: { bodiesMb: 1, blobsMb: 1048576 },
  });
  expect(bodies.value).toBe("1");
  expect(blobs.value).toBe("1048576");
  bodies.value = "100";
  await settings.apply();
  expect(transport.seen.filter((item) => item.cmd === "prefs.set").pop().args).toEqual({
    cache: { bodiesMb: 100, blobsMb: 1048576 },
  });
  expect((await createApi(transport).prefs.get()).cache).toEqual({ bodiesMb: 100, blobsMb: 1048576 });
  settings.destroy();
});

test("a blank field is restored and a failed save is reported", async () => {
  const transport = mockTransport();
  const inner = transport.call.bind(transport);
  transport.call = (cmd, args) => {
    if (cmd === "prefs.set") {
      return Promise.reject(new Error("disk full"));
    }
    return inner(cmd, args);
  };
  const root = pane();
  const errors = [];
  const settings = createLimitsSettings(createApi(transport), root, {
    onError(text) {
      errors.push(text);
    },
  });
  const bodies = root.querySelector("#limits-bodies");
  bodies.value = "nope";
  bodies.dispatchEvent(new Event("blur"));
  expect(bodies.value).toBe("2048");
  bodies.value = "300";
  await settings.apply();
  expect(errors).toEqual(["disk full"]);
  expect(bodies.value).toBe("300");
  await settings.refresh();
  expect(bodies.value).toBe("2048");
  settings.destroy();
});

test("Settings → Limits shows the defaults, saves them, and keeps the help jump last", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const transport = mockTransport();
  const api = createApi(transport);
  await mount(el, api, { hostServices: { setRootFontSize() {} } });
  const root = el.querySelector(".pav-root");
  root.querySelector('[data-heading="limits"]').click();
  await flush();
  const pane = root.querySelector('[data-settings="limits"]');
  expect(pane.hidden).toBe(false);
  expect(root.querySelector('[data-settings="features"]').hidden).toBe(true);
  expect(pane.querySelector(".pav-module-title").textContent).toBe("Limits");
  expect(pane.querySelector("#limits-toolbar").lastElementChild.classList.contains("help-jump")).toBe(true);
  expect(pane.querySelector(".help-jump").getAttribute("data-help-section")).toBe("limits");
  const bodies = pane.querySelector("#limits-bodies");
  const blobs = pane.querySelector("#limits-blobs");
  expect(bodies.value).toBe("2048");
  expect(blobs.value).toBe("4096");
  bodies.value = "100";
  blobs.value = "50";
  pane.querySelector('[data-module-action="apply"]').click();
  await flush();
  expect(transport.seen.filter((item) => item.cmd === "prefs.set").pop().args).toEqual({
    cache: { bodiesMb: 100, blobsMb: 50 },
  });
  expect((await api.prefs.get()).cache).toEqual({ bodiesMb: 100, blobsMb: 50 });
  bodies.value = "12";
  pane.querySelector('[data-module-action="refresh"]').click();
  await flush();
  expect(bodies.value).toBe("100");
  expect(blobs.value).toBe("50");
});
