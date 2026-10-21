import { describe, expect, test } from "vitest";
import { createApi } from "./api.js";
import { mockTransport } from "./transport/mock.js";

function apiWith(transport) {
  return { api: createApi(transport), transport };
}

describe("createApi over the mock transport", () => {
  test("system.status returns the ready profile", async () => {
    const { api } = apiWith(mockTransport());
    const status = await api.system.status();
    expect(status.profileName).toBe("Default");
    expect(status.profileId).toBe("default");
    expect(status.state).toBe("ready");
    expect(status.error).toBe("");
    expect(status.debug).toBe(false);
    expect(status.selftest).toBe(false);
  });

  test("prefs.set keeps keys it does not send", async () => {
    const { api } = apiWith(mockTransport());
    const saved = await api.prefs.set({ theme: "light", windowWidth: 900 });
    expect(saved.theme).toBe("light");
    expect(saved.windowWidth).toBe(900);
    expect(saved.timezone).toBe("America/Vancouver");
    expect((await api.prefs.get()).chrome).toBe("system");
  });

  test("prefs.set of an empty object is bad_args", async () => {
    const { api } = apiWith(mockTransport());
    await expect(api.prefs.set({})).rejects.toMatchObject({
      code: "bad_args",
      message: "nothing to save",
    });
  });

  test("prefs.ui.set replaces the blob", async () => {
    const { api } = apiWith(mockTransport());
    expect((await api.prefs.ui.get()).nav.width).toBe(320);
    const stored = await api.prefs.ui.set({ zoom: { shell: 1.5 } });
    expect(stored.zoom.shell).toBe(1.5);
    expect(stored.nav).toBeUndefined();
    expect((await api.prefs.ui.get()).nav).toBeUndefined();
  });

  test("log.tail sends the query object", async () => {
    const transport = mockTransport();
    const api = createApi(transport);
    await expect(api.log.tail({ limit: 5 })).resolves.toEqual({ lines: [] });
    expect(transport.seen.at(-1)).toEqual({ cmd: "log.tail", args: { limit: 5 } });
    await api.log.tail();
    expect(transport.seen.at(-1).args).toEqual({});
  });

  test("log.export sends the path and the filter", async () => {
    const transport = mockTransport();
    const api = createApi(transport);
    const filter = { levels: ["WARN"], subsystems: ["config"], text: "saved" };
    await expect(api.log.export({ path: "/tmp/pav.log", filter })).resolves.toEqual({ lines: 0 });
    expect(transport.seen.at(-1)).toEqual({
      cmd: "log.export",
      args: { path: "/tmp/pav.log", filter },
    });
  });

  test("prefs.set accepts logLevel", async () => {
    const { api } = apiWith(mockTransport());
    const saved = await api.prefs.set({ logLevel: "debug" });
    expect(saved.logLevel).toBe("debug");
    expect(saved.theme).toBe("system");
  });

  test("log.client maps the old numeric levels and returns an empty object", async () => {
    const transport = mockTransport();
    const api = createApi(transport);
    await expect(api.log.client({ subsystem: "ui", level: 0, message: "line" })).resolves.toEqual({});
    const sent = transport.seen.at(-1);
    expect(sent.cmd).toBe("log.client");
    expect(sent.args.level).toBe("error");
  });

  test("mailbox.list sends the account id only when one is given", async () => {
    const transport = mockTransport();
    const api = createApi(transport);
    const listed = await api.mailbox.list();
    expect(listed.nodes.map((node) => node.id)).toEqual(["inbox", "drafts", "sent", "archive"]);
    expect(transport.seen.at(-1)).toEqual({ cmd: "mailbox.list", args: {} });
    await expect(api.mailbox.list({ accountId: "foods" })).resolves.toEqual({ nodes: [] });
    expect(transport.seen.at(-1)).toEqual({ cmd: "mailbox.list", args: { accountId: "foods" } });
  });

  test("an unknown command is bad_args", async () => {
    const transport = mockTransport();
    await expect(transport.call("no.such", {})).rejects.toMatchObject({
      code: "bad_args",
    });
  });

  test("api.on receives one event name and unsubscribe stops it", async () => {
    const transport = mockTransport();
    const api = createApi(transport);
    const seen = [];
    const stop = api.on("core.state", (payload) => {
      seen.push(payload);
    });
    transport.emit("core.state", { state: "ready" });
    transport.emit("log.lines", { lines: [] });
    stop();
    transport.emit("core.state", { state: "error" });
    expect(seen).toEqual([{ state: "ready" }]);
  });
});
