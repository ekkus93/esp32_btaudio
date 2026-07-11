// Shared single WebSocket to /ws, multiplexing terminal + BT frames (SPEC §5.2).
// One connection is reused by every panel via useWs().
import { useEffect, useState } from "react";

export interface WsMessage {
  type: string; // term_out | event | info
  [k: string]: unknown;
}
type Listener = (m: WsMessage) => void;
type StateListener = (connected: boolean) => void;

let ws: WebSocket | null = null;
let connected = false;
const listeners = new Set<Listener>();
const stateListeners = new Set<StateListener>();

function ensure() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;
  const proto = location.protocol === "https:" ? "wss" : "ws";
  ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onopen = () => {
    connected = true;
    stateListeners.forEach((l) => l(true));
  };
  ws.onclose = () => {
    connected = false;
    stateListeners.forEach((l) => l(false));
    setTimeout(ensure, 1500);
  };
  ws.onmessage = (ev) => {
    try {
      const m = JSON.parse(ev.data as string) as WsMessage;
      listeners.forEach((l) => l(m));
    } catch {
      /* ignore malformed */
    }
  };
}

export function wsSend(obj: unknown): boolean {
  ensure();
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(obj));
    return true;
  }
  return false;
}

/** Send a raw command line to the WROOM32 (term_in). */
export const wsCommand = (data: string) => wsSend({ type: "term_in", data });

export function wsSubscribe(l: Listener): () => void {
  ensure();
  listeners.add(l);
  return () => listeners.delete(l);
}

/** React hook: connection state + subscribe helper. */
export function useWs() {
  const [isConnected, setConnected] = useState(connected);
  useEffect(() => {
    ensure();
    const l: StateListener = (c) => setConnected(c);
    stateListeners.add(l);
    setConnected(connected);
    return () => {
      stateListeners.delete(l);
    };
  }, []);
  return { connected: isConnected, send: wsSend, command: wsCommand, subscribe: wsSubscribe };
}
