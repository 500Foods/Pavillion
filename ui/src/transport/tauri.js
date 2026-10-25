import { invoke } from "@tauri-apps/api/core";
import { emit, listen } from "@tauri-apps/api/event";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { open, save } from "@tauri-apps/plugin-dialog";
import { APP_VERSION } from "../version.js";

function asError(err) {
  if (err instanceof Error && err.code) {
    return err;
  }
  let body = err;
  if (typeof err === "string") {
    try {
      body = JSON.parse(err);
    } catch {
      body = { message: err };
    }
  }
  const message = body && typeof body === "object" && body.message
    ? body.message
    : "core call failed";
  const error = new Error(message);
  if (body && typeof body === "object") {
    if (body.code) {
      error.code = body.code;
    }
    error.retryable = body.retryable === true;
  }
  return error;
}

export function tauriHost() {
  const host = {
    setRootFontSize(scale) {
      const value = Number(scale);
      if (!Number.isFinite(value)) {
        return;
      }
      document.documentElement.style.fontSize = `${value * 100}%`;
    },
    dialog: {
      save(options) {
        const opts = options && typeof options === "object" ? options : {};
        return save({
          title: typeof opts.title === "string" ? opts.title : "Save log",
          defaultPath: typeof opts.defaultPath === "string" ? opts.defaultPath : "pavillion.log",
          filters: Array.isArray(opts.filters) ? opts.filters : [{ name: "Log", extensions: ["log", "txt"] }],
        });
      },
      open(options) {
        const opts = options && typeof options === "object" ? options : {};
        return open({
          directory: opts.directory === true,
          multiple: false,
          title: typeof opts.title === "string" ? opts.title : "Open",
        });
      },
    },
  };
  /* Rust keeps the window up once this listener exists, and destroys it only
     after the handler returns. pagehide does not finish the profile write. */
  if (typeof window !== "undefined" && window.__TAURI_INTERNALS__) {
    const current = getCurrentWindow();
    current.setTitle(`Pavillion ${APP_VERSION}`).catch(() => {});
    let quitting = null;
    const beginQuit = () => {
      if (quitting) {
        return quitting;
      }
      if (typeof host.onQuit !== "function") {
        return null;
      }
      quitting = Promise.resolve(host.onQuit());
      return quitting;
    };
     /* Ctrl+C calls this. The shell treats prefs.ui.set of shutdown clean as
        the save finishing. These events only name a step that did not. */
     window.__pavQuit = () => {
       const pending = beginQuit();
       if (!pending) {
         emit("pav-quit-stuck").catch(() => {});
         return;
       }
       emit("pav-quit-began").catch(() => {});
       pending.then((ok) => {
         emit(ok === false ? "pav-quit-failed" : "pav-quit-done").catch(() => {});
       }).catch(() => {
         emit("pav-quit-failed").catch(() => {});
       });
    };
    current.onCloseRequested(async () => {
      const pending = beginQuit();
      if (!pending) {
        return;
      }
      try {
        await pending;
      } catch {
        /* The window still closes. A failed write leaves shutdown open. */
      }
    }).catch(() => {});
  }
  return host;
}

export function tauriTransport() {
  const handlers = new Set();
  let unlisten = null;
  let listening = null;
  let generation = 0;

  function ensureListening() {
    if (listening) {
      return;
    }
    generation += 1;
    const mine = generation;
    listening = listen("pav", (event) => {
      const body = event && event.payload ? event.payload : {};
      for (const handler of [...handlers]) {
        handler(body.name, body.payload);
      }
    }).then((stop) => {
      /* A stop() before this resolved (or a newer listen) owns the slot. */
      if (mine !== generation) {
        stop();
        return;
      }
      unlisten = stop;
    }).catch(() => {
      if (mine === generation) {
        listening = null;
      }
    });
  }
  if (typeof window !== "undefined") {
    window.addEventListener("keydown", (event) => {
      if ((event.ctrlKey || event.metaKey) && event.shiftKey && event.code === "KeyI") {
        event.preventDefault();
        invoke("internal_toggle_devtools").catch(() => {});
      }
    });
  }
  return {
    call(cmd, args) {
      return invoke("core", { cmd, args: args ?? {} }).catch((err) => {
        throw asError(err);
      });
    },
    /* One app-wide listen("pav") fans out to every handler (G-3 Lock). An
       unsubscribe only removes its handler, so a view closing never drops the
       Tauri callback while Rust is emitting. stop() releases the listener. */
    on(handler) {
      handlers.add(handler);
      ensureListening();
      return () => {
        handlers.delete(handler);
      };
    },
    stop() {
      handlers.clear();
      const release = unlisten;
      unlisten = null;
      listening = null;
      generation += 1;
      if (release) {
        release();
      }
    },
  };
}
