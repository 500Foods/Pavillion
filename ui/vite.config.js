import { dirname, resolve } from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const root = dirname(fileURLToPath(import.meta.url));

const host = process.env.TAURI_DEV_HOST;

export default defineConfig(() => ({
  clearScreen: false,
  build: {
    /* Desktop UI ships in the Tauri binary (no network fetch). Dynamic
       import() splitting does not buy load time. Main ~669 kB / ~201 kB gzip;
       Tabulator stays its own chunk (~243 kB / ~58 kB gzip). */
    chunkSizeWarningLimit: 2000,
    rollupOptions: {
      input: resolve(root, "index.html"),
    },
  },
  server: {
    fs: {
      allow: [resolve(root, "..")],
    },
    port: 1420,
    strictPort: true,
    host: host || false,
    hmr: host
      ? {
          protocol: "ws",
          host,
          port: 1421,
        }
      : undefined,
    watch: {
      ignored: ["**/src-tauri/**"],
    },
  },
}));
