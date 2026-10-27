import { createApi } from "./api.js";
import { mount } from "./index.js";
import { tauriHost, tauriTransport } from "./transport/tauri.js";
import { clampUi } from "./prefs.js";
import { installLogHooks } from "./log.js";

const api = createApi(tauriTransport());
const hostServices = tauriHost();
hostServices.onTheme = (resolved) => {
  document.documentElement.dataset.pavTheme = resolved === "light" ? "light" : "dark";
};
/* The kit loader fetches its bundle and then runs that bundle on the main
   thread. Insert the script after the first paint so that work is outside
   the first layout. The shell does not wait on the load. */
let kitQueued = null;
let kitLive = false;
/* Settles when the kit script loads or fails, so the splash can wait for it. */
let kitSettled;
const kitDone = new Promise((resolve) => {
  kitSettled = resolve;
});

function requestKit(kitHtml) {
  if (kitLive) {
    loadFontAwesomeKit(kitHtml);
    return;
  }
  kitQueued = kitHtml;
  markKit("FA kit: held for first paint");
}

function releaseKit() {
  kitLive = true;
  if (kitQueued === null) {
    return;
  }
  const kitHtml = kitQueued;
  kitQueued = null;
  loadFontAwesomeKit(kitHtml);
}

hostServices.loadKit = requestKit;
installLogHooks(api);
if (import.meta.env.DEV) {
  window.pav = api;
}

function log(subsystem, level, message) {
  return api.log.client({ subsystem, level, message });
}

function markKit(message) {
  void Promise.resolve(log("ui", "debug", message)).catch(() => {});
}

function watchKit(script) {
  script.addEventListener("load", () => {
    markKit("FA kit: script loaded");
    kitSettled();
  });
  script.addEventListener("error", () => {
    kitSettled();
    void Promise.resolve(log("ui", "error", "FA kit: script failed to load")).catch(() => {});
  });
}

const FA_KIT_ID = "45801522ef";
const FA_KIT_URL = `https://kit.fontawesome.com/${FA_KIT_ID}.js`;

function isFontAwesomeLoaded() {
  return !!document.querySelector('script[src*="kit.fontawesome.com"]');
}

export function loadFontAwesomeKit(kitHtml) {
  const raw = String(kitHtml || "").trim();
  const isHtml = raw.startsWith("<");
  if (isHtml) {
    const temp = document.createElement('div');
    temp.innerHTML = kitHtml;
    const script = temp.querySelector('script');
    if (script) {
      const newScript = document.createElement('script');
      if (script.src) {
        newScript.src = script.src;
      } else if (script.textContent) {
        newScript.textContent = script.textContent;
      }
      for (const attr of script.attributes) {
        newScript.setAttribute(attr.name, attr.value);
      }
      watchKit(newScript);
      document.head.appendChild(newScript);
      markKit(script.src ? "FA kit: script requested" : "FA kit: inline script");
    }
    return;
  }
  const existing = document.querySelector("script[data-kit-id]");
  if (existing && existing.dataset.kitId === raw) {
    markKit("FA kit: already present");
    return;
  }
  if (existing) {
    existing.remove();
  } else if (isFontAwesomeLoaded()) {
    markKit("FA kit: already present");
    return;
  }
  const script = document.createElement("script");
  script.src = `https://kit.fontawesome.com/${raw}.js`;
  script.setAttribute("data-auto-replace-svg", "svg");
  script.setAttribute("data-kit-id", raw);
  script.setAttribute("crossorigin", "anonymous");
  watchKit(script);
  document.head.appendChild(script);
  markKit("FA kit: script requested");
  return script;
}

export function reloadIcons() {
  const kitInput = document.querySelector("#fa-kit-html");
  if (kitInput) {
    loadFontAwesomeKit(kitInput.value || FA_KIT_ID);
  }
  window.location.reload();
}

async function initIcons() {
  try {
    const stored = clampUi(await api.prefs.ui.get());
    requestKit(stored.fontawesome_kit || FA_KIT_ID);
  } catch (err) {
    requestKit(FA_KIT_ID);
    log("ui", 0, `FA kit init failed: ${err instanceof Error ? err.message : String(err)}`);
  }
}

function afterFirstPaint() {
  return new Promise((resolve) => {
    requestAnimationFrame(() => requestAnimationFrame(resolve));
  });
}

hostServices.reload = () => reloadIcons();

/* The shell keeps the window hidden behind its native splash until this
   event, then shows the window and fades the splash out (D-UI.md Native
   splash). Sent after the mounted shell has laid out and the FA kit has
   loaded (or KIT_WAIT_MS passed), so its icon swap happens behind the
   splash; the shell also gives up after 5 s. Or on failure. A hidden
   WebKit view may not run animation frames, so a short timer stands in for
   the first paint. Rust ignores a second send. */
/* Longest the splash waits for the kit script (network). */
const KIT_WAIT_MS = 3000;

function appReady() {
  hostServices.appReady();
}

try {
  await initIcons();
  await mount(document.querySelector("#app"), api, { hostServices });
  await Promise.race([afterFirstPaint(), new Promise((resolve) => setTimeout(resolve, 32))]);
  releaseKit();
  await Promise.race([kitDone, new Promise((resolve) => setTimeout(resolve, KIT_WAIT_MS))]);
  appReady();
} finally {
  appReady();
}
