import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { viteSingleFile } from "vite-plugin-singlefile";

// Single self-contained index.html (JS + CSS inlined) so the firmware embeds
// exactly one gzipped blob and httpd serves it from one route (SPEC §5.5).
export default defineConfig({
  plugins: [react(), viteSingleFile()],
  build: {
    target: "es2020",
    cssCodeSplit: false,
    assetsInlineLimit: 100_000_000,
    chunkSizeWarningLimit: 4096,
    reportCompressedSize: true,
    rollupOptions: {
      output: { inlineDynamicImports: true },
    },
  },
  // Dev: proxy API/WS to a device on the LAN for reflash-free iteration.
  // Override the target with VITE_DEVICE (e.g. VITE_DEVICE=http://esp-i2s-source.local).
  server: {
    proxy: {
      "/api": { target: process.env.VITE_DEVICE || "http://10.1.2.52", changeOrigin: true },
      "/ws": { target: (process.env.VITE_DEVICE || "http://10.1.2.52").replace(/^http/, "ws"), ws: true },
    },
  },
});
