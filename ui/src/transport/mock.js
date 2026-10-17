function fail(code, message) {
  const error = new Error(message);
  error.code = code;
  error.retryable = false;
  return error;
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

const PREF_KEYS = [
  "timezone",
  "weekStart",
  "timeFormat",
  "theme",
  "density",
  "windowMaximized",
  "windowWidth",
  "windowHeight",
  "windowX",
  "windowY",
  "chrome",
  "logLevel",
];

function defaultPrefs() {
  return {
    timezone: "America/Vancouver",
    weekStart: "monday",
    timeFormat: "24h",
    theme: "system",
    density: "comfortable",
    windowMaximized: false,
    windowWidth: 1200,
    windowHeight: 800,
    windowX: null,
    windowY: null,
    chrome: "system",
    logLevel: "info",
  };
}

function defaultUi() {
  return {
    nav: { width: 320 },
    zoom: { shell: 1 },
    fontawesome_kit: "45801522ef",
  };
}

/* In-memory stand-in for the real commands. emit() is for Vitest and the
   G-4 harness. It is not part of the transport the screens call. */
export function mockTransport() {
  const prefs = defaultPrefs();
  let ui = defaultUi();
  const listeners = new Set();
  const seen = [];

  async function call(cmd, args) {
    const input = args ?? {};
    seen.push({ cmd, args: input });
    if (!isObject(input)) {
      throw fail("bad_args", "arguments are not an object");
    }
    if (cmd === "system.status") {
      return {
        profileName: "Default",
        profileId: "default",
        state: "ready",
        error: "",
      };
    }
    if (cmd === "prefs.get") {
      return structuredClone(prefs);
    }
    if (cmd === "prefs.set") {
      const keys = Object.keys(input);
      if (keys.length === 0) {
        throw fail("bad_args", "nothing to save");
      }
      for (const key of keys) {
        if (!PREF_KEYS.includes(key)) {
          throw fail("bad_args", `unknown preference ${key}`);
        }
      }
      Object.assign(prefs, structuredClone(input));
      return structuredClone(prefs);
    }
    if (cmd === "prefs.ui.get") {
      return structuredClone(ui);
    }
    if (cmd === "prefs.ui.set") {
      ui = structuredClone(input);
      return structuredClone(ui);
    }
    if (cmd === "log.client") {
      if (typeof input.subsystem !== "string" || typeof input.level !== "string" || typeof input.message !== "string") {
        throw fail("bad_args", "log.client requires subsystem, level, and message");
      }
      return {};
    }
    if (cmd === "log.tail") {
      return { lines: [] };
    }
    if (cmd === "log.export") {
      if (typeof input.path !== "string" || !isObject(input.filter)) {
        throw fail("bad_args", "log.export requires path and filter");
      }
      return { lines: 0 };
    }
    throw fail("bad_args", `unknown command ${cmd}`);
  }

  return {
    seen,
    call,
    on(handler) {
      listeners.add(handler);
      return () => listeners.delete(handler);
    },
    emit(name, payload) {
      for (const handler of listeners) {
        handler(name, payload);
      }
    },
  };
}
