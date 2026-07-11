import { useEffect, useRef, useState } from "react";

type Line = { kind: "sent" | "out" | "event" | "sys"; text: string };

// WEB-1c: terminal over /ws. term_in -> WROOM32 via bt_link -> term_out; the
// device also pushes async `event` frames (the live EVENT feed).
export function Terminal() {
  const [lines, setLines] = useState<Line[]>([]);
  const [input, setInput] = useState("");
  const [connected, setConnected] = useState(false);
  const wsRef = useRef<WebSocket | null>(null);
  const logRef = useRef<HTMLDivElement | null>(null);

  const push = (l: Line) => setLines((cur) => [...cur.slice(-200), l]);

  useEffect(() => {
    let closed = false;
    let ws: WebSocket;
    let retry: ReturnType<typeof setTimeout>;

    const connect = () => {
      const proto = location.protocol === "https:" ? "wss" : "ws";
      ws = new WebSocket(`${proto}://${location.host}/ws`);
      wsRef.current = ws;
      ws.onopen = () => setConnected(true);
      ws.onclose = () => {
        setConnected(false);
        if (!closed) retry = setTimeout(connect, 1500);
      };
      ws.onmessage = (ev) => {
        try {
          const m = JSON.parse(ev.data as string);
          if (m.type === "term_out") {
            push({ kind: "out", text: `${m.status} | ${m.result || ""}` });
          } else if (m.type === "event") {
            const parts = [m.command, m.result, m.data].filter(Boolean).join(" | ");
            push({ kind: "event", text: `EVENT ${parts}` });
          }
        } catch {
          /* ignore malformed */
        }
      };
    };
    connect();
    return () => {
      closed = true;
      clearTimeout(retry);
      ws?.close();
    };
  }, []);

  useEffect(() => {
    logRef.current?.scrollTo({ top: logRef.current.scrollHeight });
  }, [lines]);

  const send = (e: React.FormEvent) => {
    e.preventDefault();
    const cmd = input.trim();
    if (!cmd || wsRef.current?.readyState !== WebSocket.OPEN) return;
    wsRef.current.send(JSON.stringify({ type: "term_in", data: cmd }));
    push({ kind: "sent", text: `> ${cmd}` });
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
