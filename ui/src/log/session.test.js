// @vitest-environment jsdom
import { expect, test } from "vitest";
import { createApi } from "../api.js";
import { mockTransport } from "../transport/mock.js";
import { LOG_LEVELS } from "./filter.js";
import { createLogView } from "./view.js";

if (typeof Range !== "undefined" && !Range.prototype.getClientRects) {
  Range.prototype.getClientRects = () => [];
  Range.prototype.getBoundingClientRect = () => ({ left: 0, right: 0, top: 0, bottom: 0, width: 0, height: 0 });
}

function line(seq, session) {
  return {
    seq,
    ts: new Date(Date.now() - (100 - seq) * 1000).toISOString(),
    level: "INFO",
    subsystem: "core",
    text: `run ${session} line ${seq}`,
    session,
  };
}

test("Last run reads only the previous session from log.db, This run and All start from the ring", async () => {
  const transport = mockTransport();
  transport.setLogHistory([line(1, 1), line(2, 1), line(3, 2), line(4, 2), line(5, 2)]);
  const parent = document.createElement("div");
  document.body.append(parent);
  const view = createLogView(parent, createApi(transport), { filter: { levels: LOG_LEVELS } });
  await view.open();
  const select = parent.querySelector("select.log-session");
  expect([...select.options].map((o) => o.textContent)).toEqual(["This run", "Last run", "All"]);
  expect(select.value).toBe("all");
  expect(view.loadedCount()).toBe(5);

  select.value = "previous";
  select.dispatchEvent(new Event("change"));
  await new Promise((resolve) => setTimeout(resolve, 0));
  const query = transport.seen.filter((item) => item.cmd === "log.query").pop();
  expect(query.args.session).toBe("previous");
  expect(view.loadedCount()).toBe(2);
  /* Last run is finished: live lines are not appended. */
  transport.emit("log.lines", { lines: [{ seq: 6, ts: new Date().toISOString(), level: "INFO", subsystem: "core", text: "live" }] });
  expect(view.loadedCount()).toBe(2);

  select.value = "current";
  select.dispatchEvent(new Event("change"));
  await new Promise((resolve) => setTimeout(resolve, 0));
  await view.loadOlder();
  const older = transport.seen.filter((item) => item.cmd === "log.query").pop();
  expect(older.args.session).toBe("current");
  view.destroy();
});

test("Save exports with the view's range, session, and filter", async () => {
  const transport = mockTransport();
  const parent = document.createElement("div");
  document.body.append(parent);
  const host = { dialog: { save: async () => "/tmp/out.log" } };
  const view = createLogView(parent, createApi(transport), {
    filter: { levels: ["ERROR"], text: "boom", range: "7d" },
    host,
  });
  await view.open();
  const select = parent.querySelector("select.log-session");
  select.value = "current";
  select.dispatchEvent(new Event("change"));
  [...parent.querySelectorAll("button")].find((b) => b.textContent === "Save").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  const exp = transport.seen.filter((item) => item.cmd === "log.query" && item.args.limit === 10000).pop();
  expect(exp.args.levels).toEqual(["ERROR"]);
  expect(exp.args.text).toBe("boom");
  expect(exp.args.session).toBe("current");
  expect(exp.args.since).toBeGreaterThan(Date.now() - 7 * 24 * 3600 * 1000 - 5000);
  expect(exp.args.until).toBeUndefined();
  const write = transport.seen.find((item) => item.cmd === "export.write_text");
  expect(write.args.path).toBe("/tmp/out.log");
  expect(write.args.text).toBe("");
  view.destroy();
});
