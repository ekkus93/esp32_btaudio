import { test, expect } from "@playwright/test";

test.describe("ESP32-S3 Audio Source UI", () => {
  test("loads and shows the tab bar", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator("header h1")).toContainText("ESP32-S3");
    for (const t of ["Radio", "Tone", "Terminal", "Settings"]) {
      await expect(page.locator(".tab", { hasText: t })).toBeVisible();
    }
    // default tab is Radio
    await expect(page.locator(".tab.active")).toHaveText("Radio");
  });

  test("Radio tab shows the station + two volume sliders (WiFi connected)", async ({ page }) => {
    await page.goto("/");
    // WiFi is up, so no gate — the Radio + Volume cards render.
    await expect(page.getByText("WiFi required")).toHaveCount(0);
    await expect(page.getByRole("heading", { name: "Volume" })).toBeVisible();
    await expect(page.locator(".vol-row label", { hasText: "Pre-I2S" })).toBeVisible();
    await expect(page.locator(".vol-row label", { hasText: "Post-mix" })).toBeVisible();
    await expect(page.locator('input[type="range"]')).toHaveCount(2);
  });

  test("Tone tab has the tone control + a volume card", async ({ page }) => {
    await page.goto("/");
    await page.locator(".tab", { hasText: "Tone" }).click();
    await expect(page.getByRole("heading", { name: "Tone" })).toBeVisible();
    await expect(page.getByRole("heading", { name: "Volume" })).toBeVisible();
  });

  test("Settings tab shows WiFi setup, Network, Control AP, Bluetooth", async ({ page }) => {
    await page.goto("/");
    await page.locator(".tab", { hasText: "Settings" }).click();
    // WiFi setup form must be present so users can set/change the network.
    await expect(page.locator(".card.provision")).toBeVisible();
    await expect(page.getByText("Network (SSID)")).toBeVisible();
    // When connected, SSID is pre-filled and the password shows masked dots.
    // The WiFi provision card is the first .card.provision (before Control AP).
    const wifiCard = page.locator(".card.provision").first();
    await expect(wifiCard.locator("input").first()).not.toHaveValue("");
    await expect(wifiCard.locator('input[type="password"]')).toHaveAttribute("placeholder", /•/);
    await expect(page.getByRole("heading", { name: "Network" })).toBeVisible();
    await expect(page.getByRole("heading", { name: "Control AP" })).toBeVisible();
    // Control-AP name/password are editable (pre-filled) with a Save button.
    const apCard = page
      .locator(".card.provision")
      .filter({ has: page.getByRole("heading", { name: "Control AP" }) });
    await expect(apCard.locator("input").first()).not.toHaveValue("");
    await expect(apCard.getByRole("button", { name: /Save AP/ })).toBeVisible();
    // Password has a Show/Hide toggle that flips the input type.
    const apPw = apCard.locator(".pw-wrap input");
    await expect(apPw).toHaveAttribute("type", "password");
    await apCard.getByRole("button", { name: "Show" }).click();
    await expect(apPw).toHaveAttribute("type", "text");
    await expect(page.getByRole("heading", { name: "Bluetooth" })).toBeVisible();
  });

  // THE bug the user hit: paired devices don't show up.
  test("Bluetooth card lists paired devices", async ({ page }) => {
    await page.goto("/");
    await page.locator(".tab", { hasText: "Settings" }).click();
    await expect(page.getByRole("heading", { name: "Paired" })).toBeVisible();
    // The earbuds (and laptop) are paired on the WROOM32 — the list must populate,
    // NOT stay "None paired yet."
    const pairedList = page.locator(".card.bt ul.bt-list").last();
    await expect(pairedList.locator("li")).not.toHaveCount(0, { timeout: 15_000 });
    await expect(page.locator(".card.bt")).toContainText(/48:78:5e:d9:35:a3/i);
  });

  test("Scan button shows the pairing-mode hint", async ({ page }) => {
    await page.goto("/");
    await page.locator(".tab", { hasText: "Settings" }).click();
    // Before scanning, the tip is visible.
    await expect(page.getByText(/pairing mode/i).first()).toBeVisible();
  });
});
