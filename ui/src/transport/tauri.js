import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
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
    let closing = false;
    current.onCloseRequested(async () => {
      if (closing) {
        return;
      }
      closing = true;
      try {
        if (typeof host.onQuit === "function") {
          await host.onQuit();
        }
      } catch {
        /* The window still closes. A failed write leaves shutdown open. */
      }
    }).catch(() => {});
  }
  return host;
}

export function tauriTransport() {
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
    on(handler) {
      let closed = false;
      let unlisten = () => {};
      listen("pav", (event) => {
        if (closed) {
          return;
        }
        const body = event && event.payload ? event.payload : {};
        handler(body.name, body.payload);
      }).then((stop) => {
        if (closed) {
          stop();
          return;
        }
        unlisten = stop;
      });
      return () => {
        closed = true;
        unlisten();
      };
    },
  };
}
