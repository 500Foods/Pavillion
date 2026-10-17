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
}
