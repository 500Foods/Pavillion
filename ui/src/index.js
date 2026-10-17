import { shellMarkup } from "./shell/template.js";
import { startShell, stopShell } from "./shell.js";

let current = null;

export async function mount(el, api, opts = {}) {
  if (!(el instanceof Element)) {
    throw new Error("mount requires an element");
  }
  unmount();
  const surface = document.createElement("div");
  surface.className = "pav-root";
  // html-ok: shellMarkup is the static template, not outside data.
  surface.innerHTML = shellMarkup;
  el.append(surface);
  current = { el, surface };
  const hostServices = opts && opts.hostServices ? opts.hostServices : {};
  let result;
  try {
    result = await api.system.status();
  } catch (err) {
    result = {
      state: "error",
      error: err instanceof Error ? err.message : String(err),
      profileName: "",
    };
  }
  if (!current || current.surface !== surface) {
    return;
  }
  try {
    await startShell(surface, api, hostServices, result);
  } catch (err) {
    if (current && current.surface === surface) {
      unmount();
    }
    throw err;
  }
}

export function unmount() {
  const mounted = current;
  current = null;
  stopShell();
  if (!mounted) {
    return;
  }
  mounted.surface.remove();
  mounted.el.replaceChildren();
}
