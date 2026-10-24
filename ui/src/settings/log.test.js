// @vitest-environment jsdom
import { expect, test } from "vitest";
import { createApi } from "../api.js";
import { mockTransport } from "../transport/mock.js";
import { createLogSettings, formatMb, statsText } from "./log.js";

function pane() {
  const root = document.createElement("div");
  root.innerHTML = `
    <input id="log-cap" type="range" min="1" max="100" step="1" value="10" />
    <span id="log-cap-value"></span>
    <p id="log-stats"></p>
    <p id="log-preview"></p>
    <button id="log-refresh">Refresh</button>
    <button id="log-apply">Apply</button>
    <button id="log-trim-now">Trim Now</button>`;
  document.body.append(root);
  return root;
}

function rows(count) {
  const out = [];
  for (let i = 0; i < count; i += 1) {
    out.push({ seq: i + 1, ts: new Date(Date.UTC(2026, 9, 19, 12, 0) + i).toISOString(), level: "INFO", subsystem: "core", text: "x" });
  }
  return out;
}

const tick = () => new Promise((resolve) => setTimeout(resolve, 0));

test("Refresh shows size, records, and the oldest local date and time", async () => {
  const transport = mockTransport();
  transport.setLogHistory(rows(3));
  const root = pane();
  const settings = createLogSettings(createApi(transport), root, { timeZone: "America/Vancouver" });
  root.querySelector("#log-refresh").click();
  await tick();
  const text = root.querySelector("#log-stats").textContent;
  expect(text).toContain("3 records");
  expect(text).toContain(formatMb(600));
  expect(text).toContain("2026-10-19 05:00");
  expect(root.querySelector("#log-cap").value).toBe("10");
  expect(root.querySelector("#log-cap-value").textContent).toBe("10 MB");
  settings.destroy();
  root.remove();
});

test("the slider previews and deletes nothing", async () => {
  const transport = mockTransport();
  transport.setLogHistory(rows(12000));
  const root = pane();
  const settings = createLogSettings(createApi(transport), root, { timeZone: "UTC" });
  await settings.refresh();
  const slider = root.querySelector("#log-cap");
  slider.value = "1";
  slider.dispatchEvent(new Event("input"));
  expect(root.querySelector("#log-cap-value").textContent).toBe("1 MB");
  await settings.preview();
  expect(transport.seen.find((item) => item.cmd === "log.trim_preview").args).toEqual({ mb: 1 });
  expect(root.querySelector("#log-preview").textContent).toContain("At 1 MB: about");
  expect(root.querySelector("#log-preview").textContent).toContain("5,242 records");
  await settings.refresh();
  expect(root.querySelector("#log-stats").textContent).toContain("12,000 records");
  settings.destroy();
  root.remove();
});

test("Apply saves the cap and trims, and shows the new stats", async () => {
  const transport = mockTransport();
  transport.setLogHistory(rows(12000));
  const root = pane();
  const settings = createLogSettings(createApi(transport), root, { timeZone: "UTC" });
  await settings.refresh();
  root.querySelector("#log-cap").value = "1";
  root.querySelector("#log-apply").click();
  await tick();
  expect(transport.seen.filter((item) => item.cmd === "log.trim").pop().args).toEqual({ mb: 1 });
  expect(root.querySelector("#log-stats").textContent).toContain("5,242 records");
  expect(root.querySelector("#log-cap-value").textContent).toBe("1 MB");
  settings.destroy();
  root.remove();
});

test("Trim Now trims to the saved cap and shows the new stats", async () => {
  const transport = mockTransport();
  transport.setLogHistory(rows(12000));
  const api = createApi(transport);
  await api.log.trim(2);
  transport.setLogHistory(rows(12000));
  const root = pane();
  const settings = createLogSettings(api, root, { timeZone: "UTC" });
  await settings.refresh();
  expect(root.querySelector("#log-cap").value).toBe("2");
  /* A moved slider is not saved by Trim Now. */
  root.querySelector("#log-cap").value = "1";
  root.querySelector("#log-trim-now").click();
  await tick();
  expect(transport.seen.filter((item) => item.cmd === "log.trim").pop().args).toEqual({});
  expect(root.querySelector("#log-stats").textContent).toContain("10,485 records");
  expect(root.querySelector("#log-cap").value).toBe("2");
  settings.destroy();
  root.remove();
});

test("statsText reads an empty log", () => {
  expect(statsText({ bytes: 0, records: 0, oldestTs: null }, "UTC")).toBe("0.00 MB, 0 records, oldest none");
});
