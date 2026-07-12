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

  test("Radio station Edit expands the row inline (accordion)", async ({ page }) => {
    await page.goto("/");
    const firstRow = page.locator(".card.radio ul.bt-list li").first();
    await expect(firstRow.getByRole("button", { name: "Edit" })).toBeVisible({ timeout: 10_000 });
    await firstRow.getByRole("button", { name: "Edit" }).click();
    // The row itself becomes the editor (inline fields + Save/Cancel), not a bottom form.
    await expect(firstRow.locator(".radio-edit input")).toHaveCount(2);
    await expect(firstRow.getByRole("button", { name: "Save" })).toBeVisible();
    await firstRow.getByRole("button", { name: "Cancel" }).click();
    await expect(firstRow.getByRole("button", { name: "Edit" })).toBeVisible();
  });

  test("Radio station rows have up/down reorder arrows (ends disabled)", async ({ page }) => {
    await page.goto("/");
    const rows = page.locator(".card.radio ul.bt-list li");
    await expect(rows.first().getByRole("button", { name: "Move up" })).toBeVisible({ timeout: 10_000 });
    const n = await rows.count();
    await expect(rows.first().getByRole("button", { name: "Move up" })).toBeDisabled();
    await expect(rows.nth(n - 1).getByRole("button", { name: "Move down" })).toBeDisabled();
  });

  test("Tone tab has the tone control + a volume card", async ({ page }) => {
    await page.goto("/");
    await page.locator(".tab", { hasText: "Tone" }).click();
    await expect(page.getByRole("heading", { name: "Tone" })).toBeVisible();
    await expect(page.getByRole("heading", { name: "Volume" })).toBeVisible();
  });

  test("Tone tab: Tone|Arpeggios row, then Piano (with sliders), then Volume", async ({ page }) => {
    await page.goto("/");
    await page.locator(".tab", { hasText: "Tone" }).click();

    const piano = page.locator(".card.piano");
    await expect(piano.getByRole("heading", { name: "Piano" })).toBeVisible();
    await expect(piano.locator(".pkey.white")).toHaveCount(15); // C3..C5
    await expect(piano.locator(".pkey.black")).toHaveCount(10);
    for (const c of ["C3", "C4", "C5"]) await expect(piano.getByText(c, { exact: true })).toBeVisible();
    // Note length + note volume sliders.
    await expect(piano.getByText("Note length")).toBeVisible();
    await expect(piano.getByText("Note volume")).toBeVisible();
    await expect(piano.locator('input[type="range"]')).toHaveCount(2);

    // Arpeggios card sits next to Tone with preset buttons.
    const arps = page.locator(".card.arps");
    await expect(arps.getByRole("heading", { name: "Arpeggios" })).toBeVisible();
    await expect(arps.locator(".arp-btns button")).not.toHaveCount(0);
    await expect(arps.getByRole("button", { name: "C Major" })).toBeVisible();

    // DOM order: Tone, Arpeggios, Piano, Volume.
    const cards = page.locator(".grid .card");
    await expect(cards.nth(0).locator("h2")).toContainText("Tone");
    await expect(cards.nth(1).locator("h2")).toHaveText("Arpeggios");
    await expect(cards.nth(2).locator("h2")).toHaveText("Piano");
    await expect(cards.nth(3).locator("h2")).toHaveText("Volume");
  });

  test("Piano note-length slider controls how long a key sounds", async ({ page }) => {
    // Intercept /api/tone so no real audio plays; measure play->stop timing.
    const events: { method: string; t: number }[] = [];
    await page.route("**/api/tone", async (route) => {
      events.push({ method: route.request().method(), t: Date.now() });
      await route.fulfill({ status: 200, contentType: "application/json", body: '{"ok":true,"on":true,"hz":440}' });
    });
    await page.goto("/");
    await page.locator(".tab", { hasText: "Tone" }).click();
    const piano = page.locator(".card.piano");
    // Set note length to the minimum (100 ms) and tap a key.
    await piano.locator('input[type="range"]').first().fill("100");
    await piano.locator(".pkey.white").first().click();
    await page.waitForTimeout(700);
    const post = events.find((e) => e.method === "POST");
    const del = events.find((e) => e.method === "DELETE");
    expect(post, "a POST /api/tone fired on key press").toBeTruthy();
    expect(del, "a DELETE /api/tone auto-stopped the note").toBeTruthy();
    const gap = del!.t - post!.t;
    // Auto-stop ~100 ms after play (generous bounds for CI jitter).
    expect(gap).toBeGreaterThan(50);
    expect(gap).toBeLessThan(450);
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

  test("Find & pair scans for a named device and pairs the match", async ({ page }) => {
    // Fully mocked — no real scan/pair hits the device.
    await page.route("**/api/scan", (r) =>
      r.fulfill({ status: 200, contentType: "application/json", body: '{"ok":true}' }));
    let pairBody: { action?: string; mac?: string } | null = null;
    await page.route("**/api/bt", async (route) => {
      if (route.request().method() === "POST") {
        pairBody = JSON.parse(route.request().postData() || "{}");
        return route.fulfill({ status: 200, contentType: "application/json", body: '{"ok":true,"result":"OK"}' });
      }
      return route.fulfill({
        status: 200,
        contentType: "application/json",
        body: JSON.stringify({
          connected: false, scanning: true, paired: [],
          discovered: [{ mac: "AA:BB:CC:DD:EE:FF", name: "Test Buds Pro" }],
        }),
      });
    });
    await page.goto("/");
    await page.locator(".tab", { hasText: "Settings" }).click();
    const bt = page.locator(".card.bt");
    await bt.locator(".bt-findpair input[type='text']").fill("test buds");
    await bt.getByRole("button", { name: "Find & pair" }).click();
    await expect(bt.locator(".bt-findmsg")).toContainText(/Paired with Test Buds Pro/i, { timeout: 15_000 });
    expect(pairBody?.action).toBe("pair");
    expect(pairBody?.mac).toBe("AA:BB:CC:DD:EE:FF");
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

  test("Bluetooth shows which device is currently connected", async ({ page }) => {
    await page.goto("/");
    await page.locator(".tab", { hasText: "Settings" }).click();
    const banner = page.locator(".card.bt .bt-connected");
    await expect(banner).toBeVisible();
    // The device is connected (autostart), so the banner names the sink and the
    // connected paired row carries a "connected" badge.
    await expect(banner).toContainText(/Connected to/i, { timeout: 15_000 });
    await expect(page.locator(".card.bt .bt-badge")).toHaveText("connected");
  });

  test("Scan button shows the pairing-mode hint", async ({ page }) => {
    await page.goto("/");
    await page.locator(".tab", { hasText: "Settings" }).click();
    // Before scanning, the tip is visible.
    await expect(page.getByText(/pairing mode/i).first()).toBeVisible();
  });
});
