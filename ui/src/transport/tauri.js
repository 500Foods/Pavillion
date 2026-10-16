import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

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
