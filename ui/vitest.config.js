import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vitest/config";

const root = dirname(fileURLToPath(import.meta.url));

export default defineConfig({
  server: {
    fs: {
      allow: [resolve(root, "..")],
    },
  },
  test: {
    environment: "node",
    include: ["src/**/*.test.js", "test/**/*.test.js"],
  },
});
