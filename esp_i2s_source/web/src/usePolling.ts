import { useEffect, useRef } from "react";

/**
 * Generation-based polling — schedules the next poll only after the current
 * one completes, so overlapping polls never occur.  Uses a generation counter
 * so stale callbacks don't overwrite fresher state.
 *
 * @param fn - async function to run on each poll (receives an AbortSignal)
 * @param ms - minimum delay between successive polls
 */
export function usePolling(
  fn: (signal: AbortSignal) => Promise<void>,
  ms: number,
) {
  const latest = useRef(fn);
  latest.current = fn;

  useEffect(() => {
    let stopped = false;
    let timer: number | undefined;
    let generation = 0;
    let controller: AbortController | undefined;

    const run = async () => {
      const mine = ++generation;
      controller = new AbortController();
      try {
        await latest.current(controller.signal);
      } catch (error) {
        if (!controller.signal.aborted) {
          console.error("poll failed", error);
        }
      } finally {
        if (!stopped && mine === generation) {
          timer = window.setTimeout(run, ms);
        }
      }
    };

    void run();
    return () => {
      stopped = true;
      if (timer !== undefined) window.clearTimeout(timer);
      controller?.abort();
    };
  }, [ms]);
}
