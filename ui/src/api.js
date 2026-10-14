import { invoke } from "@tauri-apps/api/core";

export function status() {
  return invoke("status");
}
