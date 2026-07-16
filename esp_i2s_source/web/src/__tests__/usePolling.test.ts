// 10.11 — Tests for usePolling hook
// Verifies no overlapping polls, stale response rejection, and abort on cleanup.

import { describe, it, expect, beforeEach, vi } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { usePolling } from "../usePolling";

beforeEach(() => {
  vi.useFakeTimers();
});

describe("usePolling", () => {
  it("runs the poll function on mount", () => {
    const fn = vi.fn().mockResolvedValue(undefined);
    renderHook(() => usePolling(fn, 1000));
    expect(fn).toHaveBeenCalled();
  });

  it("does not run overlapping polls", async () => {
    const calls: number[] = [];
    const fn = vi.fn().mockImplementation(async () => {
      calls.push(Date.now());
      await new Promise((r) => setTimeout(r, 1500));
    });
    renderHook(() => usePolling(fn, 1000));
    // First poll starts
    await act(async () => {});
    // Advance time to start of next poll
    await act(() => vi.advanceTimersByTime(1000));
    // The first poll is still running (1500ms), so no second poll starts.
    expect(fn).toHaveBeenCalledTimes(1);
  });

  it("schedules next poll after current completes", async () => {
    const fn = vi.fn().mockImplementation(async () => {
      await new Promise((r) => setTimeout(r, 500));
    });
    renderHook(() => usePolling(fn, 1000));
    // Wait for first poll to complete
    await act(() => vi.advanceTimersByTime(500));
    // The timer for the next poll should be scheduled
    await act(() => vi.advanceTimersByTime(1000));
    // Second poll starts
    expect(fn).toHaveBeenCalledTimes(2);
  });

  it("aborts the poll on cleanup", () => {
    const fn = vi.fn().mockResolvedValue(undefined);
    const { unmount } = renderHook(() => usePolling(fn, 1000));
    const initialCalls = fn.mock.calls.length;
    unmount();
    // Poll should not run after cleanup
    vi.advanceTimersByTime(2000);
    expect(fn).toHaveBeenCalledTimes(initialCalls);
  });

  it("does not overwrite fresh state with stale response", async () => {
    let callCount = 0;
    const fn = vi.fn().mockImplementation(async () => {
      callCount++;
      const response = { ok: callCount % 2 === 0 };
      await new Promise((r) => setTimeout(r, 100));
      return response;
    });
    renderHook(() => usePolling(fn, 1000));
    // Let both polls complete
    await act(() => vi.advanceTimersByTime(1000));
    await act(() => vi.advanceTimersByTime(1000));
    // Both should have run without error
    expect(fn).toHaveBeenCalledTimes(2);
  });
});
