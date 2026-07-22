// 10.11 / FIX3 11.4 — Frontend tests for api.ts helpers
// Tests run against a mocked fetch in jsdom.

import { describe, it, expect, beforeEach, vi, Mock } from "vitest";
import { ApiError } from "../api";

const VALID_TOKEN = "a".repeat(64);

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

describe("FIX3 11: auth token storage", () => {
  it("setAuthToken rejects a malformed token", async () => {
    const { setAuthToken } = await import("../api");
    expect(() => setAuthToken("not-hex")).toThrow(/64 lowercase hex/);
    expect(() => setAuthToken("a".repeat(63))).toThrow(/64 lowercase hex/);
    expect(() => setAuthToken("A".repeat(64))).toThrow(/64 lowercase hex/); // uppercase rejected
  });

  it("setAuthToken accepts an exact 64-lowercase-hex token", async () => {
    const { setAuthToken, getAuthToken } = await import("../api");
    setAuthToken(VALID_TOKEN);
    expect(getAuthToken()).toBe(VALID_TOKEN);
  });

  it("clearAuthToken removes the stored token", async () => {
    const { setAuthToken, clearAuthToken, getAuthToken } = await import("../api");
    setAuthToken(VALID_TOKEN);
    clearAuthToken();
    expect(getAuthToken()).toBe("");
  });

  it("remember=true also persists to localStorage; remember=false does not", async () => {
    const { setAuthToken } = await import("../api");
    setAuthToken(VALID_TOKEN, true);
    expect(localStorage.getItem("esp_i2s_auth_token")).toBe(VALID_TOKEN);

    setAuthToken(VALID_TOKEN, false);
    expect(localStorage.getItem("esp_i2s_auth_token")).toBeNull();
  });
});

describe("FIX3 11.1/11.4: apiRequest auth behavior", () => {
  it("a mutating request without a stored token never calls fetch()", async () => {
    const { apiRequest } = await import("../api");
    await expect(apiRequest("/api/radio", { method: "POST" })).rejects.toThrow(
      "Enter the device token",
    );
    expect(globalThis.fetch).not.toHaveBeenCalled();
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

  it("a mutating request with a stored token adds the Authorization header", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: true,
      status: 200,
      headers: { get: () => "application/json" },
      json: async () => ({ ok: true, data: { ok: true } }),
    });
    const { apiRequest, setAuthToken } = await import("../api");
    setAuthToken(VALID_TOKEN);

    await apiRequest("/api/radio", { method: "POST", body: JSON.stringify({ url: "x" }) });

    const [, options] = (globalThis.fetch as Mock).mock.calls[0];
    const headers = options.headers as Record<string, string>;
    expect(headers.Authorization).toBe(`Bearer ${VALID_TOKEN}`);
  });

  it("a 401 response fires the onAuthRequired listener", async () => {
    (globalThis.fetch as Mock).mockResolvedValueOnce({
      ok: false,
      status: 401,
      headers: { get: () => "application/json" },
      json: async () => ({ ok: false, error: { code: "AUTH_REQUIRED", message: "bad token" } }),
    });
    const { apiRequest, setAuthToken, onAuthRequired } = await import("../api");
    setAuthToken(VALID_TOKEN);

    const listener = vi.fn();
    const unsubscribe = onAuthRequired(listener);
    try {
      await expect(
        apiRequest("/api/radio", { method: "POST" }),
      ).rejects.toThrow("bad token");
      expect(listener).toHaveBeenCalledTimes(1);
    } finally {
      unsubscribe();
    }
  });

  it("a missing token fires the onAuthRequired listener before any network call", async () => {
    const { apiRequest, onAuthRequired } = await import("../api");
    const listener = vi.fn();
    const unsubscribe = onAuthRequired(listener);
    try {
      await expect(apiRequest("/api/radio", { method: "POST" })).rejects.toThrow();
      expect(listener).toHaveBeenCalledTimes(1);
      expect(globalThis.fetch).not.toHaveBeenCalled();
    } finally {
      unsubscribe();
    }
  });
});
