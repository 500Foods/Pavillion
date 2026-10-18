/* App version, 0.phase.build. APP_PHASE is the middle number: the count of
   completed plan steps. The build count is the PAVILLION_BUILD file, inlined
   by the bundler. make dev, make build, and make mini add one to that file
   before they compile. Cargo.toml and tauri.conf.json keep the phase stamp
   0.phase.0. */
import buildText from "../../PAVILLION_BUILD?raw";

export const APP_PHASE = "0.46";
export const APP_BUILD = buildText.trim();
export const APP_VERSION = `${APP_PHASE}.${APP_BUILD}`;
