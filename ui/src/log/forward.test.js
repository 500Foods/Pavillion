// @vitest-environment jsdom
import { describe, expect, it, vi } from "vitest";
import { createForwarder, forwardEvents, FORWARD_PER_SECOND } from "./forward.js";
import { restoreConsole, routeConsole } from "../log.js";

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

describe("UI log forwarding", () => {
  it("sends actions at info and navigation at debug, 50 per second", async () => {
    const sent = [];
    let now = 5000;
    const f = createForwarder((sub, level, message) => sent.push([sub, level, message]), () => now);
    for (let i = 0; i < FORWARD_PER_SECOND + 10; i++) {
      f.action("click save");
    }
    expect(f.navigate("heading log")).toBe(false);
    now += 1000;
    expect(f.navigate("heading log")).toBe(true);
    await flush();
    expect(sent.length).toBe(FORWARD_PER_SECOND + 1);
    expect(sent[0]).toEqual(["ui", "info", "action click save"]);
    expect(sent.at(-1)).toEqual(["ui", "debug", "navigate heading log"]);
  });

  it("logs stable names, never field values or text", async () => {
    const sent = [];
    const root = document.createElement("div");
    root.innerHTML = `<button data-heading="log">Logs</button>
      <button id="save-account">Save me@example.com</button>
      <input id="acct-email" value="me@example.com">
      <input type="password" id="pw" value="hunter2">`;
    document.body.append(root);
    const stop = forwardEvents(root, createForwarder((s, l, m) => sent.push([l, m])));
    root.querySelector("[data-heading]").click();
    root.querySelector("#save-account").click();
    for (const id of ["acct-email", "pw"]) {
      root.querySelector(`#${id}`).dispatchEvent(new Event("change", { bubbles: true }));
    }
    await flush();
    stop();
    root.remove();
    expect(sent).toEqual([
      ["debug", "navigate heading log"],
      ["info", "action click save-account"],
      ["info", "action change acct-email"],
    ]);
    expect(JSON.stringify(sent)).not.toMatch(/example\.com|hunter2/);
  });

  it("routes console.warn and console.error through log.client", async () => {
    const calls = [];
    const quiet = vi.spyOn(console, "warn").mockImplementation(() => {});
    const quietErr = vi.spyOn(console, "error").mockImplementation(() => {});
    routeConsole({ log: { client: (args) => calls.push(args) } });
    console.warn("careful", 3);
    await flush();
    console.error(new Error("boom"));
    await flush();
    restoreConsole();
    quiet.mockRestore();
    quietErr.mockRestore();
    expect(calls[0]).toEqual({ subsystem: "ui", level: "warn", message: "careful 3" });
    expect(calls[1].level).toBe("error");
    expect(calls[1].message).toContain("boom");
  });
});

describe("framework console notes", () => {
  it("maps the Tauri late-callback warning to debug, other [TAURI] lines stay warn", async () => {
    const calls = [];
    const quiet = vi.spyOn(console, "warn").mockImplementation(() => {});
    routeConsole({ log: { client: (args) => calls.push(args) } });
    console.warn("[TAURI] Couldn't find callback id 1502767015. This might happen when the app is reloaded while Rust is running an asynchronous operation.");
    await flush();
    console.warn("[TAURI] something else");
    await flush();
    restoreConsole();
    quiet.mockRestore();
    expect(calls[0]).toEqual({ subsystem: "ui", level: "debug", message: "late reply ignored after a view closed" });
    expect(calls[1]).toEqual({ subsystem: "ui", level: "warn", message: "[TAURI] something else" });
  });
});
