// @vitest-environment jsdom
import { afterEach, expect, test } from "vitest";
import { createApi } from "../api.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";
import { createLimitsSettings, formatCacheMb, formatChecked, formatCount, formatDiskLine, formatPoolCell, poolFigure, poolTotal } from "./limits.js";

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

test("formatCacheMb uses the limit unit and leaves a missing figure blank", () => {
  const gb = 1024 * 1024 * 1024;
  expect(formatCacheMb(0, "on disk")).toBe("0.0 MB on disk");
  expect(formatCacheMb(2621440)).toBe("2.5 MB");
  expect(formatCacheMb(100 * 1024 * 1024)).toBe("100.0 MB");
  expect(formatCacheMb(4632 * 1024 * 1024)).toBe("4,632.0 MB");
  expect(formatCacheMb(null)).toBe("");
  expect(formatCount(1200)).toBe("1,200");
  expect(formatCount(0)).toBe("0");
  expect(formatCount(null)).toBe("");
  expect(formatChecked(Date.UTC(2026, 9, 21, 21, 4), "America/Vancouver")).toBe("2026-10-21 14:04");
  expect(formatChecked(null, "America/Vancouver")).toBe("");
  expect(formatDiskLine(0, Math.round(16.1 * gb))).toBe("0.0 MB on disk, 16.1 GB available");
  expect(formatDiskLine(5 * 1024 * 1024, 500 * 1024 * 1024)).toBe("5.0 MB on disk, 500.0 MB available");
  expect(formatDiskLine(0, null)).toBe("0.0 MB on disk");
  expect(formatPoolCell(1, 0)).toBe("1 entry, 0.0 MB");
  expect(formatPoolCell(54070, 4632 * 1024 * 1024)).toBe("54,070 entries, 4,632.0 MB");
  expect(formatPoolCell(15, null)).toBe("15 entries");
  expect(formatPoolCell(null, null)).toBe("");
  expect(poolFigure({
    mail: { count: 12, bodiesBytes: 30, attachmentsBytes: 10, attachmentsCount: 1 },
    contacts: { count: 3, bytes: 2 },
    calendars: { count: 0, bytes: 4 },
    files: { count: 0, bytes: 0 },
  }, "database")).toEqual({ count: 15, bytes: 36 });
  expect(poolFigure({
    mail: { count: 4, bodiesBytes: null },
    contacts: { count: 2, bytes: 2 },
  }, "database")).toEqual({ count: 6, bytes: null });
  expect(poolFigure({
    mail: { attachmentsBytes: null },
    files: { count: 0, bytes: 0 },
  }, "document")).toEqual({ count: null, bytes: null });
  expect(poolFigure({
    mail: { attachmentsBytes: 10, attachmentsCount: 1 },
    files: { count: 0, bytes: 0 },
  }, "document")).toEqual({ count: 1, bytes: 10 });
  expect(poolFigure({
    mail: { attachmentsBytes: 10 },
    files: { count: 2, bytes: 1 },
  }, "document")).toEqual({ count: null, bytes: 11 });
  expect(poolFigure({
    mail: { attachmentsBytes: 0 },
    files: { count: 0, bytes: 0 },
  }, "document")).toEqual({ count: 0, bytes: 0 });
  const accounts = [
    {
      mail: { count: 12, bodiesBytes: null, attachmentsBytes: null },
      contacts: { count: 3 },
      calendars: { count: 0 },
    },
    {
      mail: { count: 4, bodiesBytes: 1024 * 1024, attachmentsBytes: 1024 * 1024, attachmentsCount: 1 },
      files: { count: 8 },
    },
  ];
  expect(poolTotal(accounts, "database")).toEqual({ count: 19, bytes: 1024 * 1024 });
  expect(poolTotal(accounts, "document")).toEqual({ count: 9, bytes: 1024 * 1024 });
  expect(poolTotal([], "database")).toEqual({ count: null, bytes: null });
});

test("usage shows disk totals and one row of counts and sizes per account", async () => {
  const root = document.createElement("div");
  // html-ok: static test fixture, not outside data.
  root.innerHTML = `
    <div id="limits-toolbar" class="button-bar"></div>
    <input id="limits-bodies" type="number" value="2048" />
    <span id="limits-bodies-used"></span>
    <input id="limits-blobs" type="number" value="4096" />
    <span id="limits-blobs-used"></span>
    <button type="button" id="limits-measure">Check sizes</button>
    <progress id="limits-progress" hidden value="0" max="1"></progress>
    <button type="button" id="limits-cancel" hidden>Cancel</button>
    <p id="limits-checked"></p>
    <table id="limits-accounts" hidden>
      <tbody id="limits-account-rows"></tbody>
      <tfoot>
        <tr>
          <th scope="row">Total</th>
          <td id="limits-total-database" class="limits-num"></td>
          <td id="limits-total-document" class="limits-num"></td>
        </tr>
      </tfoot>
    </table>
    <div id="limits-footer" class="button-bar"></div>`;
  document.body.append(root);
  const calls = [];
  let release;
  let onChange = null;
  const gate = new Promise((resolve) => {
    release = resolve;
  });
  const quick = {
    bodiesBytes: 5 * 1024 * 1024,
    blobsBytes: 20 * 1024 * 1024,
    volumeFreeBytes: 180 * 1024 * 1024 * 1024,
    sizesCheckedAt: null,
    accounts: [
      {
        id: "foods",
        name: "Foods",
        serverBytes: 100 * 1024 * 1024,
        mail: { count: 12, bytes: null },
        contacts: { count: 3, bytes: null },
        calendars: { count: 0, bytes: null },
        files: { count: null, bytes: null },
      },
      {
        id: "courses",
        name: "Courses",
        serverBytes: null,
        mail: {
          count: 4,
          bytes: 2 * 1024 * 1024,
          bodiesBytes: 1024 * 1024,
          attachmentsBytes: 1024 * 1024,
          attachmentsCount: 1,
        },
        contacts: { count: null, bytes: null },
        calendars: { count: null, bytes: null },
        files: { count: 8, bytes: null },
      },
    ],
  };
  const api = {
    on(name, fn) {
      if (name === "cache.change") {
        onChange = fn;
      }
      return () => {
        onChange = null;
      };
    },
    prefs: {
      get: async () => ({ cache: { bodiesMb: 2048, blobsMb: 4096 } }),
    },
    cache: {
      usage: (args) => {
        calls.push(args ?? {});
        if (args && args.cancel) {
          return Promise.resolve({ cancelled: true });
        }
        if (args && args.measure) {
          return gate.then(() => ({
            ...quick,
            sizesCheckedAt: Date.UTC(2026, 9, 21, 21, 4),
            accounts: [
              {
                ...quick.accounts[0],
                mail: {
                  count: 12,
                  bytes: 40 * 1024 * 1024,
                  bodiesBytes: 30 * 1024 * 1024,
                  attachmentsBytes: 10 * 1024 * 1024,
                  attachmentsCount: 4,
                },
                files: { count: 2, bytes: 0 },
              },
              quick.accounts[1],
            ],
          }));
        }
        return Promise.resolve(quick);
      },
    },
  };
  const settings = createLimitsSettings(api, root, { timeZone: "America/Vancouver" });
  await settings.refresh();
  expect(root.querySelector("#limits-bodies-used").textContent).toBe("5.0 MB on disk, 180.0 GB available");
  expect(root.querySelector("#limits-blobs-used").textContent).toBe("20.0 MB on disk, 180.0 GB available");
  expect(root.querySelector("#limits-accounts").hidden).toBe(false);
  expect(root.querySelector("#limits-checked").textContent).toBe("");
  const rows = [...root.querySelectorAll("#limits-account-rows tr")];
  expect([...rows[0].children].map((cell) => cell.textContent)).toEqual(["Foods", "15 entries", ""]);
  expect([...rows[1].children].map((cell) => cell.textContent)).toEqual(["Courses", "4 entries, 1.0 MB", "9 entries, 1.0 MB"]);
  expect(root.querySelector("#limits-total-database").textContent).toBe("19 entries, 1.0 MB");
  expect(root.querySelector("#limits-total-document").textContent).toBe("9 entries, 1.0 MB");
  expect(rows[0].dataset.accountId).toBe("foods");
  const button = root.querySelector("#limits-measure");
  const cancel = root.querySelector("#limits-cancel");
  const bar = root.querySelector("#limits-progress");
  button.click();
  expect(button.disabled).toBe(true);
  expect(button.textContent).toBe("Checking…");
  expect(calls.at(-1)).toEqual({ measure: true });
  expect(cancel.hidden).toBe(false);
  expect(bar.hidden).toBe(false);
  onChange({
    accountId: "foods",
    store: "mail",
    done: 12,
    total: 12,
    bytes: 40 * 1024 * 1024,
    scanned: 12,
    scanTotal: 12,
  });
  expect(bar.max).toBe(12);
  expect(bar.value).toBe(12);
  expect(root.querySelector("#limits-account-rows tr").children[1].textContent).toBe("15 entries");
  expect(root.querySelector("#limits-checked").textContent).toBe("Checking Foods mail, 12 of 12");
  const before = calls.length;
  await settings.refresh();
  expect(calls.length).toBe(before);
  cancel.click();
  expect(calls.at(-1)).toEqual({ cancel: true });
  release();
  await flush();
  expect(button.disabled).toBe(false);
  expect(button.textContent).toBe("Check sizes");
  expect(cancel.hidden).toBe(true);
  expect(bar.hidden).toBe(true);
  expect(root.querySelector("#limits-checked").textContent).toBe("Last checked 2026-10-21 14:04");
  const measured = root.querySelector("#limits-account-rows tr");
  expect(measured.children[1].textContent).toBe("15 entries, 30.0 MB");
  expect(measured.children[2].textContent).toBe("6 entries, 10.0 MB");
  expect(root.querySelector("#limits-total-database").textContent).toBe("19 entries, 31.0 MB");
  expect(root.querySelector("#limits-total-document").textContent).toBe("15 entries, 11.0 MB");
  settings.destroy();
});

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
  expect(pane.querySelector("#limits-bodies-used").textContent).toBe("0.0 MB on disk");
  expect(pane.querySelector("#limits-blobs-used").textContent).toBe("0.0 MB on disk");
  expect([...pane.querySelectorAll(".limits-list .zoom-label")].map((node) => node.textContent)).toEqual([
    "Database",
    "Document",
    "Message list",
  ]);
  expect(pane.querySelector("#limits-list-rows").value).toBe("10000");
  expect([...pane.querySelectorAll(".limits-kind")].map((node) => node.textContent)).toEqual([
    "(Mail, Contacts, Calendars)",
    "(Attachments, Files)",
  ]);
  expect(pane.querySelector("#limits-accounts").hidden).toBe(true);
  expect(pane.querySelector("#limits-accounts tfoot th").textContent).toBe("Total");
  expect(pane.querySelector("#limits-measure").textContent).toBe("Check sizes");
  expect(transport.seen.some((item) => item.cmd === "cache.usage")).toBe(true);
  pane.querySelector("#limits-measure").click();
  await flush();
  expect(transport.seen.filter((item) => item.cmd === "cache.usage").pop().args).toEqual({ measure: true });
  bodies.value = "100";
  blobs.value = "50";
  pane.querySelector('[data-module-action="apply"]').click();
  await flush();
  expect(transport.seen.filter((item) => item.cmd === "prefs.set").pop().args).toEqual({
    cache: { bodiesMb: 100, blobsMb: 50, listRows: 10000 },
  });
  expect((await api.prefs.get()).cache).toEqual({ bodiesMb: 100, blobsMb: 50, listRows: 10000 });
  bodies.value = "12";
  pane.querySelector("#limits-list-rows").value = "20";
  pane.querySelector('[data-module-action="refresh"]').click();
  await flush();
  expect(bodies.value).toBe("100");
  expect(blobs.value).toBe("50");
  expect(pane.querySelector("#limits-list-rows").value).toBe("10000");
});

test("Apply writes the message list cap and clamps it", async () => {
  const transport = mockTransport();
  const root = document.createElement("div");
  // html-ok: static test fixture, not outside data.
  root.innerHTML = `
    <div id="limits-toolbar" class="button-bar"></div>
    <input id="limits-bodies" type="number" value="2048" />
    <input id="limits-blobs" type="number" value="4096" />
    <input id="limits-list-rows" type="number" min="1" max="10000" value="10000" />
    <div id="limits-footer" class="button-bar"></div>`;
  document.body.append(root);
  const settings = createLimitsSettings(createApi(transport), root);
  await settings.refresh();
  const rows = root.querySelector("#limits-list-rows");
  rows.value = "0";
  await settings.apply();
  expect(transport.seen.filter((item) => item.cmd === "prefs.set").pop().args).toEqual({
    cache: { bodiesMb: 2048, blobsMb: 4096, listRows: 1 },
  });
  expect(rows.value).toBe("1");
  rows.value = "20000";
  await settings.apply();
  expect(rows.value).toBe("10000");
  expect((await createApi(transport).prefs.get()).cache.listRows).toBe(10000);
  rows.value = "nope";
  rows.dispatchEvent(new Event("blur"));
  expect(rows.value).toBe("10000");
  settings.destroy();
});
