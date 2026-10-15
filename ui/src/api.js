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
