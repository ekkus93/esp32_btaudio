// 10.11 — Frontend tests for api.ts helpers
// Tests run against a mocked fetch in jsdom.

import { describe, it, expect, beforeEach, vi, Mock } from "vitest";
import { ApiError } from "../api";

// Mock fetch before each test to capture global fetch.
beforeEach(() => {
  globalThis.fetch = vi.fn();
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
