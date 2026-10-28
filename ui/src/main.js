import { createApi } from "./api.js";
import { mount } from "./index.js";
import { tauriHost, tauriTransport } from "./transport/tauri.js";
import { clampUi } from "./prefs.js";
import { installLogHooks } from "./log.js";
import { defaultLibraries } from "./common/iconhosts.js";
import { librariesFromKit, loadIconLibraries } from "./common/icons/libraries.js";

const api = createApi(tauriTransport());
const hostServices = tauriHost();
hostServices.onTheme = (resolved) => {
  document.documentElement.dataset.pavTheme = resolved === "light" ? "light" : "dark";
};
/* Enabled libraries load after the first paint. The shell does not wait. */
let librariesQueued = null;
let librariesLive = false;

function requestLibraries(list) {
  if (librariesLive) {
    loadIconLibraries(list);
    return;
  }
  librariesQueued = list;
  markKit("icon libraries: held for first paint");
}

function releaseLibraries() {
  librariesLive = true;
  if (librariesQueued === null) {
    return;
  }
  const list = librariesQueued;
  librariesQueued = null;
  loadIconLibraries(list);
}

hostServices.loadKit = (kit) => {
  requestLibraries(librariesFromKit(kit, defaultLibraries()));
};
hostServices.loadLibraries = requestLibraries;
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

export function reloadIcons() {
  window.location.reload();
}

async function initIcons() {
  try {
    const stored = clampUi(await api.prefs.ui.get());
    const list = Array.isArray(stored.iconLibraries)
      ? stored.iconLibraries
      : librariesFromKit(stored.fontawesome_kit, defaultLibraries());
    requestLibraries(list);
  } catch (err) {
    requestLibraries(defaultLibraries());
    log("ui", "error", `icon libraries failed: ${err instanceof Error ? err.message : String(err)}`);
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
/* Longest the splash waits for the icons (network). Under the shell's 5 s. */
const KIT_WAIT_MS = 4000;

/* The kit script only loads a loader: it then fetches the Font Awesome
   bundle, which swaps <i class="fa-..."> for <svg> in a later animation
   frame, so the script load event comes before any icon is drawn. While it
   converts, Font Awesome puts fontawesome-i2svg-pending on <html> and then
   swaps it for fontawesome-i2svg-complete (docs: SVG Async Loading). Wait
   for that class on <html>, then let one frame paint. */
function faComplete() {
  const html = document.documentElement;
  return new Promise((resolve) => {
    if (html.classList.contains("fontawesome-i2svg-complete")) {
      resolve();
      return;
    }
    const observer = new MutationObserver(() => {
      if (html.classList.contains("fontawesome-i2svg-complete")) {
        observer.disconnect();
        resolve();
      }
    });
    observer.observe(html, { attributes: true, attributeFilter: ["class"] });
  });
}

async function iconsSwapped() {
  await faComplete();
  await afterFirstPaint();
}

function appReady() {
  hostServices.appReady();
}

try {
  await initIcons();
  await mount(document.querySelector("#app"), api, { hostServices });
  await Promise.race([afterFirstPaint(), new Promise((resolve) => setTimeout(resolve, 32))]);
  releaseLibraries();
  await Promise.race([iconsSwapped(), new Promise((resolve) => setTimeout(resolve, KIT_WAIT_MS))]);
  appReady();
} finally {
  appReady();
}
