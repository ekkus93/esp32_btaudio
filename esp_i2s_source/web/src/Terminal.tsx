import { useEffect, useRef, useState } from "react";
import { consoleCmd } from "./api";

type Line = { kind: "sent" | "out"; text: string };

// Terminal via REST: POST /api/console runs a raw WROOM32 command and returns
// its response (no WebSocket).
export function Terminal() {
  const [lines, setLines] = useState<Line[]>([]);
  const [input, setInput] = useState("");
  const [busy, setBusy] = useState(false);
  const logRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    logRef.current?.scrollTo({ top: logRef.current.scrollHeight });
  }, [lines]);

  const push = (l: Line) => setLines((cur) => [...cur.slice(-200), l]);

  const send = async (e: React.FormEvent) => {
    e.preventDefault();
    const cmd = input.trim();
    if (!cmd || busy) return;
    push({ kind: "sent", text: `> ${cmd}` });
    setInput("");
    setBusy(true);
    try {
      const r = await consoleCmd(cmd);
      // Multi-line commands (HELP, PAIRED, ...) stream INFO lines first.
      (r.lines ?? []).forEach((l) => push({ kind: "out", text: l }));
      const parts = [r.result, r.data].filter(Boolean).join(" | ");
      push({ kind: "out", text: `${r.status}${parts ? " | " + parts : ""}` });
    } catch {
      push({ kind: "out", text: "(request failed)" });
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="card terminal">
      <h2>Terminal</h2>
      <div className="term-log" ref={logRef}>
        {lines.length === 0 && (
          <div className="muted">Send a command to the Bluetooth board (e.g. VERSION, STATUS, PAIRED).</div>
        )}
        {lines.map((l, i) => (
          <div key={i} className={`term-line ${l.kind}`}>
            {l.text}
          </div>
        ))}
      </div>
      <form onSubmit={send}>
        <input
          value={input}
          onChange={(e) => setInput(e.target.value)}
          placeholder="command…"
          disabled={busy}
          spellCheck={false}
          autoCapitalize="off"
          autoCorrect="off"
        />
        <button type="submit" disabled={busy || !input.trim()}>
          Send
        </button>
      </form>
    </section>
  );
}
