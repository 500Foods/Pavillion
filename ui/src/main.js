import { createApi } from "./api.js";
import { mount } from "./index.js";
import { tauriHost, tauriTransport } from "./transport/tauri.js";
import { clampUi } from "./prefs.js";
import { installLogHooks } from "./log.js";

const api = createApi(tauriTransport());
const hostServices = tauriHost();
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
  const isHtml = kitHtml.trim().startsWith('<');
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
  if (isFontAwesomeLoaded()) {
    return;
  }
  const script = document.createElement("script");
  script.src = `https://kit.fontawesome.com/${kitHtml}.js`;
  script.setAttribute("data-auto-replace-svg", "svg");
  script.setAttribute("data-kit-id", kitHtml);
  script.setAttribute("crossorigin", "anonymous");
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

window.addEventListener("load", () => {
  const kitScript = document.querySelector('script[src*="kit.fontawesome.com"]');
  if (kitScript) {
    if (kitScript.dataset.fontawesome) {
      log("ui", 2, `FA kit: ${kitScript.dataset.fontawesome}`);
    } else {
      log("ui", 0, "FA kit: dataset absent, kit may not have executed");
      const svgs = document.querySelectorAll(".nav-buttons svg");
      log("ui", 2, `FA kit: ${svgs.length} SVG(s) found in nav (expected 8)`);
      if (svgs.length === 0) {
        log("ui", 0, "FA kit: no SVGs in nav — kit JS did not replace <i> elements");
      }
    }
  } else {
    log("ui", 0, "FA kit: script tag not found in DOM");
  }
});

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
