import { useEffect, useRef, useState } from "react";
import { getBt, btAction, triggerScan, type BtDev, type BtState } from "./api";

// Bluetooth management via polled REST (GET /api/bt) — no WebSocket. Scan
// results / paired items / pairing prompts are buffered on the device and
// returned by the poll; actions go through POST /api/bt.
export function Bluetooth() {
  const [bt, setBt] = useState<BtState | null>(null);
  const [scanning, setScanning] = useState(false);
  const [scanned, setScanned] = useState(false);
  const [note, setNote] = useState<string>("");
  const scanTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const refresh = () => getBt().then(setBt).catch(() => {});

  useEffect(() => {
    refresh();
    const id = setInterval(refresh, 2000);
    return () => {
      clearInterval(id);
      if (scanTimer.current) clearTimeout(scanTimer.current);
    };
  }, []);

  const act = async (action: string, mac?: string) => {
    const r = await btAction(action, mac).catch(() => ({ ok: false }));
    setNote(`${action.replace("_", " ")}: ${r.ok ? "ok" : "failed"}`);
    setTimeout(refresh, 600);
  };

  const scan = () => {
    setScanned(false);
    setScanning(true);
    triggerScan().catch(() => {});
    if (scanTimer.current) clearTimeout(scanTimer.current);
    // Device: ~1.5s disconnect + ~15s inquiry + reconnect. Poll shows results live.
    scanTimer.current = setTimeout(() => {
      setScanning(false);
      setScanned(true);
    }, 20000);
  };

  const paired: BtDev[] = bt?.paired ?? [];
  const discovered: BtDev[] = bt?.discovered ?? [];
  const isPaired = (mac: string) => paired.some((p) => p.mac.toLowerCase() === mac.toLowerCase());
  const online = bt != null;

  return (
    <section className="card bt">
      <h2>
        Bluetooth
        <span className={`dot ${bt?.connected ? "ok" : "bad"}`} title={bt?.connected ? "connected" : "not connected"} />
      </h2>

      <div className="bt-actions">
        <button className="primary" disabled={!online || scanning} onClick={scan}>
          {scanning ? "Scanning…" : "Scan"}
        </button>
        <button disabled={!online || !bt?.connected} onClick={() => act("disconnect")}>
          Disconnect
        </button>
      </div>

      {scanning ? (
        <p className="muted bt-hint">
          Scanning… <strong>audio pauses</strong> for ~20s while the radio scans. Put the
          speaker/headphones you want in <strong>pairing mode</strong> now — only devices
          actively in pairing mode are discoverable.
        </p>
      ) : scanned && discovered.length === 0 ? (
        <p className="muted bt-hint">
          No devices found. Bluetooth speakers/earbuds only appear while they're in
          <strong> pairing mode</strong> — put yours in pairing mode and scan again.
        </p>
      ) : discovered.length === 0 ? (
        <p className="muted bt-hint">
          Tip: put your Bluetooth speaker/headphones in <strong>pairing mode</strong> before scanning.
        </p>
      ) : null}

      {discovered.length > 0 && (
        <>
          <h3>Discovered</h3>
          <ul className="bt-list">
            {discovered.map((d) => (
              <li key={d.mac}>
                <span className="name">{d.name || "(no name)"}</span>
                <span className="mac">{d.mac}</span>
                <span className="btns">
                  {!isPaired(d.mac) && <button onClick={() => act("pair", d.mac)}>Pair</button>}
                  <button onClick={() => act("connect", d.mac)}>Connect</button>
                </span>
              </li>
            ))}
          </ul>
        </>
      )}

      <h3>
        Paired
        <button className="bt-refresh" disabled={!online} onClick={() => act("refresh")} title="Refresh">
          ↻
        </button>
      </h3>
      {paired.length === 0 ? (
        <p className="muted">{online ? "None paired yet." : "…"}</p>
      ) : (
        <ul className="bt-list">
          {paired.map((d) => (
            <li key={d.mac}>
              <span className="name">{d.name || "(no name)"}</span>
              <span className="mac">{d.mac}</span>
              <span className="btns">
                <button onClick={() => act("connect", d.mac)}>Connect</button>
                <button className="danger" onClick={() => act("unpair", d.mac)}>Unpair</button>
              </span>
            </li>
          ))}
        </ul>
      )}

      {note && <div className="banner ok">{note}</div>}

      {bt?.prompt && (
        <div className="bt-prompt">
          <p>Pairing request: {bt.prompt}</p>
          <button className="primary" onClick={() => act("pin_accept")}>Accept</button>
          <button className="danger" onClick={() => act("pin_reject")}>Reject</button>
        </div>
      )}
    </section>
  );
}
