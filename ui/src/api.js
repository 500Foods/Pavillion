import { invoke } from "@tauri-apps/api/core";

export function status() {
  return invoke("status");
}

export function uiGet() {
  return invoke("ui_get");
}

export function uiSet(json) {
  return invoke("ui_set", { json });
}

export function log(subsystem, level, message) {
  return invoke("ui_log", { subsystem, level, message });
}
