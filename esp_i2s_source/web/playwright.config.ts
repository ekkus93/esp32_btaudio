import { defineConfig } from "@playwright/test";

// Playwright config — uses mock server by default for deterministic tests.
// Set LIVE_DEVICE=1 to test against a real device.
//   DEVICE_URL=http://10.1.2.52 npx playwright test

const isLive = process.env.LIVE_DEVICE === "1";

export default defineConfig({
  testDir: "./e2e",
  timeout: 45_000,
  expect: { timeout: 10_000 },
  reporter: [["list"]],
  use: {
    baseURL: isLive
      ? (process.env.DEVICE_URL || "http://10.1.2.52")
      : "http://localhost:5173",
    browserName: "chromium",
    viewport: { width: 1000, height: 820 },
    launchOptions: {
      executablePath: process.env.CHROME || "/usr/bin/chromium-browser",
      args: ["--no-sandbox", "--disable-dev-shm-usage"],
    },
  },
});
