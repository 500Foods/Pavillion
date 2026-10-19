const MAX_BYTES = 2000;

function clip(message) {
  const text = String(message).replace(/\r\n|\n|\r/g, " ");
  let out = "";
  let bytes = 0;
  for (const ch of text) {
    const size = new TextEncoder().encode(ch).length;
    if (bytes + size > MAX_BYTES) {
      break;
    }
    out += ch;
    bytes += size;
  }
  return out;
}

function location(event) {
  const filename = event.filename;
  if (!filename || filename === "undefined") {
    return "";
  }
  return ` (${filename}:${event.lineno || 0}:${event.colno || 0})`;
}

/* WebKit's stack for an anonymous throw is "@", which drops the message.
   A stack is used only when it still contains that message. */
function errorText(event) {
  const error = event.error;
  const message = (error instanceof Error && error.message) || event.message || "error";
  const stack = error instanceof Error ? error.stack || "" : "";
  if (stack.includes(message)) {
    return stack;
  }
  return `${event.message || message}${location(event)}`;
}

function reasonText(reason) {
  if (reason instanceof Error) {
    if (reason.stack && reason.stack.includes(reason.message || "error")) {
      return reason.stack;
    }
    return reason.message || "error";
  }
  return String(reason);
}

/* window.onerror and unhandledrejection become one ui error line each.
   A failure to log must not raise another rejection. */
export function installLogHooks(api) {
  let reporting = false;

  function report(message) {
    if (reporting) {
      return;
    }
    reporting = true;
    try {
      Promise.resolve(api.log.client({ subsystem: "ui", level: "error", message: clip(message) }))
        .catch(() => {})
        .finally(() => {
          reporting = false;
        });
    } catch {
      reporting = false;
    }
  }

  window.addEventListener("error", (event) => {
    report(errorText(event));
  });
  window.addEventListener("unhandledrejection", (event) => {
    report(`unhandledrejection: ${reasonText(event.reason)}`);
  });
  routeConsole(api);
}

/* H-4: console.warn and console.error also go to the core log through
   log.client (the browser console keeps its copy for the inspector). This
   file is the only allowed console call site in ui/src (bypass.test.js). */
let consoleOriginals = null;

function consoleText(args) {
  return args.map((arg) => (arg instanceof Error ? reasonText(arg) : typeof arg === "string" ? arg : (() => {
    try {
      return JSON.stringify(arg);
    } catch {
      return String(arg);
    }
  })())).join(" ");
}

/* Known framework messages, reworded so users never see the framework name
   at WARN (H-4 Lock). Any other [TAURI] message stays as it was. */
const FRAMEWORK_NOTES = [
  { pattern: /^\[TAURI\] Couldn't find callback id/, level: "debug", message: "late reply ignored after a view closed" },
];

export function frameworkNote(text) {
  return FRAMEWORK_NOTES.find((note) => note.pattern.test(text)) || null;
}

export function routeConsole(api) {
  if (typeof console === "undefined") {
    return;
  }
  if (!consoleOriginals) {
    consoleOriginals = { warn: console.warn, error: console.error };
  }
  let busy = false;
  for (const level of ["warn", "error"]) {
    const original = consoleOriginals[level];
    console[level] = (...args) => {
      original.apply(console, args);
      if (busy) {
        return;
      }
      busy = true;
      try {
        const text = consoleText(args);
        const known = frameworkNote(text);
        Promise.resolve(api.log.client(known
          ? { subsystem: "ui", level: known.level, message: known.message }
          : { subsystem: "ui", level, message: clip(text) }))
          .catch(() => {})
          .finally(() => {
            busy = false;
          });
      } catch {
        busy = false;
      }
    };
  }
}

export function restoreConsole() {
  if (consoleOriginals) {
    console.warn = consoleOriginals.warn;
    console.error = consoleOriginals.error;
    consoleOriginals = null;
  }
}
