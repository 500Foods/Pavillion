import { beforeEach, describe, expect, it, vi } from "vitest";

const state = vi.hoisted(() => ({ listens: 0, unlistens: 0, cb: null }));

vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn(() => Promise.resolve({})) }));
vi.mock("@tauri-apps/api/event", () => ({
  emit: vi.fn(),
  listen: vi.fn((name, cb) => {
    state.listens += 1;
    state.cb = cb;
    return Promise.resolve(() => {
      state.unlistens += 1;
    });
  }),
}));
vi.mock("@tauri-apps/api/window", () => ({ getCurrentWindow: vi.fn() }));
vi.mock("@tauri-apps/plugin-dialog", () => ({ open: vi.fn(), save: vi.fn() }));

const { tauriTransport } = await import("./tauri.js");

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

describe("tauri transport events", () => {
  beforeEach(() => {
    state.listens = 0;
    state.unlistens = 0;
    state.cb = null;
  });

  it("keeps one app-wide listen across many on/off cycles", async () => {
    const t = tauriTransport();
    const seen = [];
    for (let i = 0; i < 5; i++) {
      const off = t.on((name) => seen.push(`${i}:${name}`));
      await flush();
      off();
    }
    const keep = t.on((name) => seen.push(`keep:${name}`));
    await flush();
    expect(state.listens).toBe(1);
    expect(state.unlistens).toBe(0);
    state.cb({ payload: { name: "log.lines", payload: {} } });
    expect(seen).toEqual(["keep:log.lines"]);
    keep();
    /* A late event after every view closed is ignored, not an error. */
    state.cb({ payload: { name: "log.lines", payload: {} } });
    expect(seen).toEqual(["keep:log.lines"]);
  });

  it("releases the listener only at stop, and listens again after", async () => {
    const t = tauriTransport();
    t.on(() => {});
    t.stop();
    await flush();
    expect(state.unlistens).toBe(1);
    t.on(() => {});
    await flush();
    expect(state.listens).toBe(2);
    t.stop();
    expect(state.unlistens).toBe(2);
  });
});
