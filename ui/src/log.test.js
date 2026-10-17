import { expect, test } from "vitest";
import { installLogHooks } from "./log.js";

function hooks() {
  const calls = [];
  const listeners = {};
  const previous = globalThis.window;
  globalThis.window = {
    addEventListener(name, fn) {
      listeners[name] = fn;
    },
  };
  const api = {
    log: {
      client(args) {
        calls.push(args);
        return Promise.resolve({});
      },
    },
  };
  installLogHooks(api);
  return {
    calls,
    listeners,
    restore() {
      if (previous === undefined) {
        delete globalThis.window;
      } else {
        globalThis.window = previous;
      }
    },
  };
}

test("an uncaught error is a ui error line", async () => {
  const hook = hooks();
  hook.listeners.error({ message: "boom", filename: "a.js", lineno: 4, colno: 2, error: null });
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(hook.calls).toEqual([{ subsystem: "ui", level: "error", message: "boom (a.js:4:2)" }]);
  hook.restore();
});

test("a stack of @ keeps the error message", async () => {
  const hook = hooks();
  const error = new Error("boom-h1");
  Object.defineProperty(error, "stack", { value: "@" });
  hook.listeners.error({
    message: "Error: boom-h1",
    filename: "undefined",
    lineno: 1,
    colno: 4,
    error,
  });
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(hook.calls).toEqual([{ subsystem: "ui", level: "error", message: "Error: boom-h1" }]);
  hook.restore();
});

test("an unhandled rejection is a ui error line", async () => {
  const hook = hooks();
  hook.listeners.unhandledrejection({ reason: new Error("nope") });
  await new Promise((resolve) => setTimeout(resolve, 0));
  expect(hook.calls).toHaveLength(1);
  expect(hook.calls[0].subsystem).toBe("ui");
  expect(hook.calls[0].level).toBe("error");
  expect(hook.calls[0].message).toContain("unhandledrejection: ");
  expect(hook.calls[0].message).toContain("nope");
  hook.restore();
});
