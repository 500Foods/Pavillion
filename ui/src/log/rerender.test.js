// @vitest-environment jsdom
import { expect, test } from "vitest";
import { createApi } from "../api.js";
import { configureFormats } from "../formats.js";
import { mockTransport } from "../transport/mock.js";
import { LOG_LEVELS } from "./filter.js";
import { createLogView } from "./view.js";

if (typeof Range !== "undefined" && !Range.prototype.getClientRects) {
  Range.prototype.getClientRects = () => [];
  Range.prototype.getBoundingClientRect = () => ({ left: 0, right: 0, top: 0, bottom: 0, width: 0, height: 0 });
}

const LINE = {
  seq: 1,
  ts: "2026-10-14T17:04:01.123Z",
  level: "INFO",
  subsystem: "core",
  text: "hello",
  session: 1,
};

test("a format change repaints the log, and Save writes that display line", async () => {
  configureFormats({ timezone: "America/Vancouver", timeFormat: "24h", formats: {}, systemZone: "" });
  const transport = mockTransport();
  transport.setLogHistory([LINE]);
  const parent = document.createElement("div");
  document.body.append(parent);
  const host = { dialog: { save: async () => "/tmp/out.log" } };
  const view = createLogView(parent, createApi(transport), {
    filter: { levels: LOG_LEVELS, range: "all" },
    timeZone: "America/Vancouver",
    host,
  });
  await view.open();
  expect(parent.textContent).toContain("10:04:01.123");
  configureFormats({
    timezone: "America/Vancouver",
    timeFormat: "24h",
    formats: { timeSeconds: "HH:mm" },
    systemZone: "",
  });
  view.setTimeZone("America/Vancouver");
  expect(parent.textContent).toContain("10:04");
  expect(parent.textContent).not.toContain("10:04:01.123");

  [...parent.querySelectorAll("button")].find((button) => button.textContent === "Save").click();
  await new Promise((resolve) => setTimeout(resolve, 0));
  const write = transport.seen.find((item) => item.cmd === "export.write_text");
  expect(write.args.path).toBe("/tmp/out.log");
  expect(write.args.text).toContain("10:04");
  expect(write.args.text).toContain("hello");
  expect(write.args.text).not.toContain("2026-10-14T17:04:01.123Z");
  view.destroy();
  parent.remove();
  configureFormats({ timezone: "America/Vancouver", timeFormat: "24h", formats: {}, systemZone: "" });
});
