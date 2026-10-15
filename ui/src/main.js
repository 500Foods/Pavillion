import { status } from "./api.js";
import { startShell } from "./shell.js";

try {
  await startShell(await status());
} catch (err) {
  await startShell({
    state: "error",
    error: err instanceof Error ? err.message : String(err),
    profileName: "",
  });
}
