import { createApi } from "./api.js";
import { mount } from "./index.js";
import { tauriHost, tauriTransport } from "./transport/tauri.js";
import { clampUi } from "./prefs.js";
import { installLogHooks } from "./log.js";
import { APP_VERSION } from "./version.js";

const api = createApi(tauriTransport());
const hostServices = tauriHost();
hostServices.onTheme = (resolved) => {
  document.documentElement.dataset.pavTheme = resolved === "light" ? "light" : "dark";
  matchSplash();
};
/* The kit loader fetches its bundle and then runs that bundle on the main
   thread. Insert the script after the first paint so that work is outside
   the first layout. The shell does not wait on the load. */
let kitQueued = null;
let kitLive = false;

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
  });
  script.addEventListener("error", () => {
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

function splashNode() {
  return document.getElementById("splash");
}

function paintSplashVersion() {
  const line = document.querySelector("#splash p");
  if (line) {
    line.textContent = `Pavillion ${APP_VERSION}`;
  }
}

/* The cover inherits the dark defaults. Once the shell has a theme, match
   its canvas, text, and font so the fade reveals the same colors. */
function matchSplash() {
  const splash = splashNode();
  const root = document.querySelector(".pav-root");
  if (!splash || !root) {
    return;
  }
  const style = getComputedStyle(root);
  const bg = style.backgroundColor;
  if (bg && bg !== "transparent" && bg !== "rgba(0, 0, 0, 0)") {
    splash.style.background = bg;
  }
  splash.style.color = style.color;
  splash.style.fontFamily = style.fontFamily;
}

function dismissSplash() {
  const splash = splashNode();
  if (!splash) {
    return;
  }
  matchSplash();
  const reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  if (reduce) {
    splash.remove();
    return;
  }
  splash.setAttribute("aria-hidden", "true");
  splash.classList.add("is-done");
  const remove = () => {
    if (splash.isConnected) {
      splash.remove();
    }
  };
  splash.addEventListener("transitionend", (event) => {
    if (event.propertyName === "opacity") {
      remove();
    }
  });
  window.setTimeout(remove, 600);
}

/* Two seconds from when this module starts, which is when the cover is
   already on screen. The shell may finish sooner. Fade-out waits for both. */
const SPLASH_DWELL_MS = 2000;
const splashShownAt = performance.now();

function finishSplash() {
  if (!splashNode()) {
    return;
  }
  const wait = Math.max(0, SPLASH_DWELL_MS - (performance.now() - splashShownAt));
  window.setTimeout(dismissSplash, wait);
}

paintSplashVersion();
try {
  await initIcons();
  await mount(document.querySelector("#app"), api, { hostServices });
  await afterFirstPaint();
  releaseKit();
} finally {
  finishSplash();
}
