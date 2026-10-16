import { invoke } from "@tauri-apps/api/core";

export function status() {
  return invoke("status");
}

export function prefs() {
  return invoke("prefs");
}

export function saveLayout(layout) {
  return invoke("save_layout", { layout });
}

export function saveIcons(overrides) {
  return invoke("save_icons", { icons: overrides });
}

export function saveKit(kit) {
  return invoke("save_kit", { kit: { kit } });
}

export function log(subsystem, level, message) {
  return invoke("ui_log", { subsystem, level, message });
}
