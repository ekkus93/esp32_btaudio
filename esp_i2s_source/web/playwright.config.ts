import { defineConfig } from "@playwright/test";

// UI tests run against the live device (or a vite-dev proxy). Uses the system
// Chromium so no Playwright browser download is needed.
//   DEVICE_URL=http://10.1.2.52 npx playwright test
export default defineConfig({
  testDir: "./e2e",
  timeout: 45_000,
  expect: { timeout: 10_000 },
  reporter: [["list"]],
  use: {
    baseURL: process.env.DEVICE_URL || "http://10.1.2.52",
    browserName: "chromium",
    viewport: { width: 1000, height: 820 },
    launchOptions: {
      executablePath: process.env.CHROME || "/usr/bin/chromium-browser",
      args: ["--no-sandbox", "--disable-dev-shm-usage"],
    },
  },
});
