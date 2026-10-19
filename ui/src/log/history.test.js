// @vitest-environment jsdom
import { expect, test } from "vitest";
import { createApi } from "../api.js";
import { mockTransport } from "../transport/mock.js";
import { LOG_LEVELS } from "./filter.js";
import { HISTORY_PAGE, createLogView } from "./view.js";

/* jsdom has no layout; CodeMirror measures text ranges. */
if (typeof Range !== "undefined" && !Range.prototype.getClientRects) {
  Range.prototype.getClientRects = () => [];
  Range.prototype.getBoundingClientRect = () => ({ left: 0, right: 0, top: 0, bottom: 0, width: 0, height: 0 });
}

function history(count, startMs, stepMs, session = 1) {
  const lines = [];
  for (let i = 0; i < count; i += 1) {
    lines.push({
      seq: i + 1,
      ts: new Date(startMs + i * stepMs).toISOString(),
      level: "INFO",
      subsystem: "core",
      text: `line ${i + 1}`,
      session,
    });
  }
  return lines;
}

function mount(transport, filter = {}) {
  const parent = document.createElement("div");
  document.body.append(parent);
  return createLogView(parent, createApi(transport), { filter: { levels: LOG_LEVELS, ...filter } });
}

test("paging past the 10,000-line ring reads older lines from log.db", async () => {
  const transport = mockTransport();
  const now = Date.now();
  transport.setLogHistory(history(15000, now - 15000 * 1000, 1000));
  /* The lines are INFO and the view shows ERROR only, so the editor stays
     small in jsdom; paging counts what is loaded, not what is drawn. */
  const view = mount(transport, { levels: ["ERROR"] });
  await view.open();
  expect(view.loadedCount()).toBe(10000);
  await view.loadOlder();
  const first = transport.seen.filter((item) => item.cmd === "log.query");
  expect(first).toHaveLength(1);
  expect(first[0].args.beforeSeq).toBe(5001);
  expect(first[0].args.limit).toBe(HISTORY_PAGE);
  expect(first[0].args.session).toBe("all");
  expect(first[0].args.since).toBeGreaterThan(now - 12 * 3600 * 1000 - 1000);
  expect(view.loadedCount()).toBe(10000 + HISTORY_PAGE);
  await view.loadOlder();
  await view.loadOlder();
  expect(view.loadedCount()).toBe(15000);
  const before = transport.seen.length;
  await view.loadOlder();
  /* A short page means nothing older: no further query. */
  expect(transport.seen.length).toBe(before);
  view.destroy();
}, 60000);

test("the range defaults to 12 hours and widening reads older lines", async () => {
  const transport = mockTransport();
  const now = Date.now();
  const old = history(50, now - 20 * 3600 * 1000, 1000);
  const recent = history(30, now - 3600 * 1000, 1000).map((line, i) => ({ ...line, seq: 51 + i }));
  transport.setLogHistory(old.concat(recent));
  let saved = null;
  const parent = document.createElement("div");
  document.body.append(parent);
  const view = createLogView(parent, createApi(transport), {
    filter: { levels: LOG_LEVELS },
    onFilter(next) {
      saved = next;
    },
  });
  await view.open();
  expect(view.loadedCount()).toBe(30);
  const select = parent.querySelector("select.log-range");
  expect(select.value).toBe("12h");
  select.value = "24h";
  select.dispatchEvent(new Event("change"));
  expect(saved.range).toBe("24h");
  await new Promise((resolve) => setTimeout(resolve, 0));
  await view.loadOlder();
  expect(view.loadedCount()).toBe(80);
  const query = transport.seen.filter((item) => item.cmd === "log.query").pop();
  expect(query.args.since).toBeLessThan(now - 20 * 3600 * 1000);
  view.destroy();
});

test("a stored custom range is restored as Custom with both fields", async () => {
  const transport = mockTransport();
  const parent = document.createElement("div");
  document.body.append(parent);
  const since = Date.UTC(2026, 9, 1, 12, 0);
  const view = createLogView(parent, createApi(transport), {
    filter: { range: { since, until: since + 3600 * 1000 } },
  });
  expect(parent.querySelector("select.log-range").value).toBe("custom");
  expect(parent.querySelector(".log-range-custom").hidden).toBe(false);
  expect(parent.querySelectorAll(".log-range-custom input")[0].value).not.toBe("");
  view.destroy();
});
