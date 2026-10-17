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
hostServices.loadKit = loadFontAwesomeKit;
installLogHooks(api);
if (import.meta.env.DEV) {
  window.pav = api;
}

function log(subsystem, level, message) {
  return api.log.client({ subsystem, level, message });
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
      document.head.appendChild(newScript);
    }
    return;
  }
  const existing = document.querySelector("script[data-kit-id]");
  if (existing && existing.dataset.kitId === raw) {
    return;
  }
  if (existing) {
    existing.remove();
  } else if (isFontAwesomeLoaded()) {
    return;
  }
  const script = document.createElement("script");
  script.src = `https://kit.fontawesome.com/${raw}.js`;
  script.setAttribute("data-auto-replace-svg", "svg");
  script.setAttribute("data-kit-id", raw);
  script.setAttribute("crossorigin", "anonymous");
  script.addEventListener("error", () => {
    log("ui", 0, "FA kit: script failed to load");
  });
  document.head.appendChild(script);
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
    loadFontAwesomeKit(stored.fontawesome_kit || FA_KIT_ID);
  } catch (err) {
    loadFontAwesomeKit(FA_KIT_ID);
    log("ui", 0, `FA kit init failed: ${err instanceof Error ? err.message : String(err)}`);
  }
}

hostServices.reload = () => reloadIcons();

await initIcons();
await mount(document.querySelector("#app"), api, { hostServices });
