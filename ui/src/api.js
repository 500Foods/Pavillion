const LEVEL_NAMES = ["error", "warn", "info", "debug"];

function levelName(level) {
  if (typeof level === "number") {
    return LEVEL_NAMES[level] || "info";
  }
  return level;
}

/* A transport is { call(cmd, args) -> Promise, on(handler) -> unsubscribe }.
   Screens use the methods below and api.on(name, fn). They never see the transport. */
export function createApi(transport) {
  function call(cmd, args) {
    return transport.call(cmd, args ?? {});
  }

  return {
    system: {
      status() {
        return call("system.status", {});
      },
    },
    prefs: {
      get() {
        return call("prefs.get", {});
      },
      set(patch) {
        return call("prefs.set", patch);
      },
      ui: {
        get() {
          return call("prefs.ui.get", {});
        },
        set(blob) {
          return call("prefs.ui.set", blob);
        },
      },
    },
    themes: {
      list() {
        return call("themes.list", {});
      },
      ensure(body) {
        return call("themes.ensure", body ?? {});
      },
      folder: {
        set(folder) {
          return call("themes.folder.set", { folder });
        },
      },
    },
    log: {
      client({ subsystem, level, message }) {
        return call("log.client", { subsystem, level: levelName(level), message });
      },
      tail(query) {
        return call("log.tail", query ?? {});
      },
      export(body) {
        return call("log.export", body ?? {});
      },
    },
    on(name, fn) {
      return transport.on((eventName, payload) => {
        if (eventName === name) {
          fn(payload);
        }
      });
    },
  };
}
