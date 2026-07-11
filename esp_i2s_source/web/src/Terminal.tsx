import { useEffect, useRef, useState } from "react";
import { useWs, type WsMessage } from "./ws";

type Line = { kind: "sent" | "out" | "event" | "info"; text: string };

// WEB-1c: terminal over the shared /ws. term_in -> WROOM32 via bt_link ->
// term_out; async `event`/`info` frames are shown inline too.
export function Terminal() {
  const { connected, command, subscribe } = useWs();
  const [lines, setLines] = useState<Line[]>([]);
  const [input, setInput] = useState("");
  const logRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    const push = (l: Line) => setLines((cur) => [...cur.slice(-200), l]);
    return subscribe((m: WsMessage) => {
      if (m.type === "term_out") {
        push({ kind: "out", text: `${m.status} | ${(m.result as string) || ""}` });
      } else if (m.type === "event" || m.type === "info") {
        const parts = [m.command, m.result, m.data].filter(Boolean).join(" | ");
        push({ kind: m.type, text: `${m.type.toUpperCase()} ${parts}` });
      }
    });
  }, [subscribe]);

  useEffect(() => {
    logRef.current?.scrollTo({ top: logRef.current.scrollHeight });
  }, [lines]);

  const send = (e: React.FormEvent) => {
    e.preventDefault();
    const cmd = input.trim();
    if (!cmd || !command(cmd)) return;
    setLines((cur) => [...cur.slice(-200), { kind: "sent", text: `> ${cmd}` }]);
    setInput("");
  };

  return (
    <section className="card terminal">
      <h2>
        Terminal
        <span className={`dot ${connected ? "ok" : "bad"}`} title={connected ? "ws connected" : "ws down"} />
      </h2>
      <div className="term-log" ref={logRef}>
        {lines.length === 0 && <div className="muted">Send a command to the Bluetooth board (e.g. VERSION, STATUS, SCAN).</div>}
        {lines.map((l, i) => (
          <div key={i} className={`term-line ${l.kind}`}>{l.text}</div>
        ))}
      </div>
      <form onSubmit={send}>
        <input
          value={input}
          onChange={(e) => setInput(e.target.value)}
          placeholder={connected ? "command…" : "connecting…"}
          disabled={!connected}
          spellCheck={false}
          autoCapitalize="off"
          autoCorrect="off"
        />
        <button type="submit" disabled={!connected || !input.trim()}>Send</button>
      </form>
    </section>
  );
}
