// 10.11 / FIX3 11.4 — Frontend tests for api.ts helpers
// Tests run against a mocked fetch in jsdom.

import { describe, it, expect, beforeEach, vi, Mock } from "vitest";
import { ApiError } from "../api";

// Mock fetch before each test to capture global fetch.
beforeEach(() => {
  globalThis.fetch = vi.fn();
  sessionStorage.clear();
  localStorage.clear();
});

describe("apiRequest error handling", () => {
  it("throws ApiError on 4xx/5xx with structured error body", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: false,
      status: 401,
      headers: { get: () => "application/json" },
      json: async () => ({ ok: false, error: { code: "UNAUTHORIZED", message: "Bad token", retryable: false } }),
    });

    const { apiRequest } = await import("../api");
    await expect(async () => {
      await apiRequest("/api/status");
    }).rejects.toThrow("Bad token");
  });

  it("throws ApiError on non-JSON content", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: true,
      status: 200,
      headers: { get: () => "text/plain" },
    });

    const { apiRequest } = await import("../api");
    await expect(async () => {
      await apiRequest("/api/status");
    }).rejects.toThrow("returned non-JSON content");
  });

  it("throws ApiError on timeout", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: false,
      status: 504,
      headers: { get: () => "text/plain" },
    });

    const { apiRequest } = await import("../api");
    await expect(async () => {
      await apiRequest("/api/status");
    }).rejects.toThrow();
  });
});

describe("ApStatus type", () => {
  it("ApStatus uses secured boolean, not pass", () => {
    const sample = {
      on: true,
      enabled: true,
      ssid: "ESP32-Audio",
      secured: true,
      ip: "192.168.4.1",
      clients: 1,
    };
    expect(sample.secured).toBe(true);
    expect((sample as any).pass).toBeUndefined();
  });
});

describe("ApiError class", () => {
  it("has status, code, and retryable fields", () => {
    const err = new ApiError("test", 400, "TEST_ERROR", true);
    expect(err.message).toBe("test");
    expect(err.status).toBe(400);
    expect(err.code).toBe("TEST_ERROR");
    expect(err.retryable).toBe(true);
  });
});

describe("apiRequest sends no auth (device-token auth removed)", () => {
  it("a mutating request reaches the network with no Authorization header", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: true,
      status: 200,
      headers: { get: () => "application/json" },
      json: async () => ({ ok: true }),
    });
    const { apiRequest } = await import("../api");
    await apiRequest("/api/radio", { method: "POST" });

    expect(globalThis.fetch).toHaveBeenCalledTimes(1);
    const [, options] = (globalThis.fetch as Mock).mock.calls[0];
    const headers = options.headers as Record<string, string>;
    expect(headers.Authorization).toBeUndefined();
  });

  it("a GET request does not require a token", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: true,
      status: 200,
      headers: { get: () => "application/json" },
      json: async () => ({ ok: true, data: { device: "esp-i2s-source" } }),
    });
    const { apiRequest } = await import("../api");
    const result = await apiRequest("/api/status");
    expect(result).toEqual({ device: "esp-i2s-source" });
    expect(globalThis.fetch).toHaveBeenCalledTimes(1);
  });
});

describe("non-enveloped device responses (the /api/status crash)", () => {
  it("returns a bare object (no ok field) directly instead of crashing", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: true,
      status: 200,
      headers: { get: () => "application/json" },
      json: async () => ({ device: "esp-i2s-source", uptime_s: 42 }),
    });
    const { apiRequest } = await import("../api");
    const r = await apiRequest<{ device: string; uptime_s: number }>("/api/status");
    expect(r.device).toBe("esp-i2s-source");
    expect(r.uptime_s).toBe(42);
  });

  it("returns an inline ok:true payload (no data wrapper) whole", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: true,
      status: 200,
      headers: { get: () => "application/json" },
      json: async () => ({ ok: true, on: true, hz: 440 }),
    });
    const { apiRequest } = await import("../api");
    const r = await apiRequest<{ ok: boolean; on: boolean; hz: number }>(
      "/api/tone", { method: "POST", body: "{}" });
    expect(r).toEqual({ ok: true, on: true, hz: 440 });
  });

  it("handles ok:false with a plain-string error field", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: true,
      status: 200,
      headers: { get: () => "application/json" },
      json: async () => ({ ok: false, error: "scan already running" }),
    });
    const { apiRequest } = await import("../api");
    await expect(
      apiRequest("/api/scan", { method: "POST" }),
    ).rejects.toThrow("scan already running");
  });

  it("handles ok:false with a missing error field without crashing", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: false,
      status: 500,
      headers: { get: () => "application/json" },
      json: async () => ({ ok: false }),
    });
    const { apiRequest } = await import("../api");
    await expect(
      apiRequest("/api/radio", { method: "POST" }),
    ).rejects.toThrow("HTTP 500");
  });
});

describe("station export/import helpers", () => {
  const stations = [
    { id: 1, name: "Groove Salad", url: "http://somafm.com/groovesalad.pls" },
    { id: 2, name: "Drone Zone", url: "http://somafm.com/dronezone.pls" },
  ];

  it("buildStationsExport emits the envelope with name+url only (no ids)", async () => {
    const { buildStationsExport } = await import("../api");
    const e = buildStationsExport(stations, "2026-07-25T00:00:00Z");
    expect(e.format).toBe("esp-i2s-source/stations");
    expect(e.version).toBe(1);
    expect(e.exported_at).toBe("2026-07-25T00:00:00Z");
    expect(e.stations).toEqual([
      { name: "Groove Salad", url: "http://somafm.com/groovesalad.pls" },
      { name: "Drone Zone", url: "http://somafm.com/dronezone.pls" },
    ]);
    expect((e.stations[0] as { id?: number }).id).toBeUndefined();
  });

  it("parseStationsImport accepts our envelope", async () => {
    const { parseStationsImport } = await import("../api");
    const text = JSON.stringify({
      format: "esp-i2s-source/stations", version: 1,
      stations: [{ name: "A", url: "http://a/s" }, { name: "B", url: "http://b/s" }],
    });
    expect(parseStationsImport(text)).toEqual([
      { name: "A", url: "http://a/s" }, { name: "B", url: "http://b/s" },
    ]);
  });

  it("parseStationsImport also accepts a bare array and trims / drops url-less entries", async () => {
    const { parseStationsImport } = await import("../api");
    const text = JSON.stringify([
      { name: " A ", url: " http://a/s " },
      { name: "no url" },              // dropped: no url
      { url: "http://c/s" },           // name defaults to ""
      "garbage",                        // dropped: not an object
    ]);
    expect(parseStationsImport(text)).toEqual([
      { name: "A", url: "http://a/s" },
      { name: "", url: "http://c/s" },
    ]);
  });

  it("parseStationsImport throws on invalid JSON and on empty/shapeless input", async () => {
    const { parseStationsImport } = await import("../api");
    expect(() => parseStationsImport("{not json")).toThrow(/not valid JSON/);
    expect(() => parseStationsImport("{}")).toThrow(/no stations/);
    expect(() => parseStationsImport("[]")).toThrow(/no valid stations/);
    expect(() => parseStationsImport(JSON.stringify([{ name: "x" }]))).toThrow(/no valid stations/);
  });

  it("planStationMerge dedups by exact URL against existing and within the file", async () => {
    const { planStationMerge } = await import("../api");
    const imported = [
      { name: "Drone Zone dup", url: "http://somafm.com/dronezone.pls" }, // dup of existing
      { name: "Star 90s", url: "http://x/star90s" },                       // new
      { name: "Star 90s again", url: "http://x/star90s" },                 // dup within file
      { name: "Lite 90s", url: "http://x/lite90s" },                       // new
    ];
    const { toAdd, duplicates } = planStationMerge(imported, stations);
    expect(toAdd).toEqual([
      { name: "Star 90s", url: "http://x/star90s" },
      { name: "Lite 90s", url: "http://x/lite90s" },
    ]);
    expect(duplicates).toBe(2);
  });

  it("planStationMerge treats http vs https as distinct (exact-URL policy)", async () => {
    const { planStationMerge } = await import("../api");
    const { toAdd, duplicates } = planStationMerge(
      [{ name: "GS https", url: "https://somafm.com/groovesalad.pls" }], stations);
    expect(toAdd).toHaveLength(1);
    expect(duplicates).toBe(0);
  });
});
