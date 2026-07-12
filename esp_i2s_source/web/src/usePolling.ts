import { useEffect, useRef } from "react";

// Poll `fn` every `ms`, but pause while the browser tab is hidden (no wasted
// requests in the background) and refresh once immediately when it becomes
// visible again. `ms` may change over time (e.g. faster during a scan) — the
// interval is re-armed when it does. Fires once on mount.
export function usePolling(fn: () => void, ms: number) {
  const fnRef = useRef(fn);
  fnRef.current = fn;
  useEffect(() => {
    const tick = () => {
      if (!document.hidden) fnRef.current();
    };
    tick(); // initial fetch
    const id = setInterval(tick, ms);
    document.addEventListener("visibilitychange", tick);
    return () => {
      clearInterval(id);
      document.removeEventListener("visibilitychange", tick);
    };
  }, [ms]);
}
