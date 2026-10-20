import { dirname, resolve } from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const root = dirname(fileURLToPath(import.meta.url));

const host = process.env.TAURI_DEV_HOST;

export default defineConfig(() => ({
  clearScreen: false,
  build: {
    /* The app loads from disk, not the network; the main chunk passed 500 kB
       with K-Q12 (503 kB, 153 kB gzip). Tabulator stays in its own chunk. */
    chunkSizeWarningLimit: 600,
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
