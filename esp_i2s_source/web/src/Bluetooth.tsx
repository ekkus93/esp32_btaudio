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

  // Scan-until-found, then pair (by name).
  const [findName, setFindName] = useState("");
  const [findSecs, setFindSecs] = useState(60);
  const [finding, setFinding] = useState(false);
  const [findMsg, setFindMsg] = useState("");
  const cancelFind = useRef(false);

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

  const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

  // Repeatedly scan for up to `findSecs`, and pair with the first discovered
  // device whose name contains the entered text (case-insensitive).
  const findAndPair = async () => {
    const target = findName.trim().toLowerCase();
    if (!target || finding) return;
    cancelFind.current = false;
    setFinding(true);
    setFindMsg(`Scanning for "${findName.trim()}" — put it in pairing mode now.`);
    const deadline = Date.now() + findSecs * 1000;
    let done = false;
    try {
      while (Date.now() < deadline && !cancelFind.current && !done) {
        await triggerScan().catch(() => {});
        await sleep(2000); // let the inquiry actually start
        while (Date.now() < deadline && !cancelFind.current) {
          const st = await getBt().catch(() => null);
          if (st) {
            setBt(st);
            const hit = st.discovered.find((d) => (d.name || "").toLowerCase().includes(target));
            if (hit) {
              const label = hit.name || hit.mac;
              setFindMsg(`Found ${label} — pairing…`);
              await btAction("pair", hit.mac).catch(() => {});
              // Auto-accept the pairing confirmation if the device prompts, then
              // verify it actually landed in the paired list.
              const pairDeadline = Date.now() + 15000;
              let ok = false;
              while (Date.now() < pairDeadline && !cancelFind.current) {
                await sleep(1200);
                const s2 = await getBt().catch(() => null);
                if (!s2) continue;
                setBt(s2);
                if (s2.prompt) await btAction("pin_accept").catch(() => {});
                if (s2.paired.some((p) => p.mac.toLowerCase() === hit.mac.toLowerCase())) {
                  ok = true;
                  break;
                }
              }
              setFindMsg(
                ok
                  ? `Paired with ${label}. You can connect it below.`
                  : `Reached ${label} but pairing didn't confirm — try again.`
              );
              done = true;
              break;
            }
            if (!st.scanning) break; // this inquiry finished — loop to re-scan
          }
          await sleep(1500);
        }
      }
      if (!done) {
        setFindMsg(
          cancelFind.current
            ? "Stopped."
            : `"${findName.trim()}" not found. Make sure it's in pairing mode and try again.`
        );
      }
    } finally {
      setFinding(false);
      setTimeout(refresh, 800);
    }
  };
  const stopFind = () => { cancelFind.current = true; };

  const paired: BtDev[] = bt?.paired ?? [];
  const discovered: BtDev[] = bt?.discovered ?? [];
  const isPaired = (mac: string) => paired.some((p) => p.mac.toLowerCase() === mac.toLowerCase());
  const online = bt != null;

  // Which device is on the A2DP link right now (from STATUS CONN_MAC).
  const connMac = bt?.connected_mac?.toLowerCase();
  const isConn = (mac: string) => !!connMac && mac.toLowerCase() === connMac;
  const connectedName =
    [...paired, ...discovered].find((d) => isConn(d.mac))?.name || bt?.connected_mac || "a device";

  return (
    <section className="card bt">
      <h2>
        Bluetooth
        <span className={`dot ${bt?.connected ? "ok" : "bad"}`} title={bt?.connected ? "connected" : "not connected"} />
      </h2>

      <p className="bt-connected">
        {bt?.connected ? (
          <>
            Connected to <strong>{connectedName}</strong>
            {bt.connected_mac && <span className="mac"> {bt.connected_mac}</span>}
          </>
        ) : (
          <span className="muted">No device connected.</span>
        )}
      </p>

      <div className="bt-actions">
        <button className="primary" disabled={!online || scanning || finding} onClick={scan}>
          {scanning ? "Scanning…" : "Scan"}
        </button>
        <button disabled={!online || !bt?.connected || finding} onClick={() => act("disconnect")}>
          Disconnect
        </button>
      </div>

      <div className="bt-findpair">
        <input
          type="text"
          value={findName}
          onChange={(e) => setFindName(e.target.value)}
          placeholder="device name to find (e.g. Echo Buds)"
          disabled={finding}
          spellCheck={false}
          autoCapitalize="off"
          autoCorrect="off"
        />
        <label className="bt-secs">
          for
          <input
            type="number"
            min={15}
            max={300}
            value={findSecs}
            onChange={(e) => setFindSecs(Math.max(15, Math.min(300, Number(e.target.value) || 60)))}
            disabled={finding}
          />
          s
        </label>
        {finding ? (
          <button className="danger" onClick={stopFind}>Stop</button>
        ) : (
          <button className="primary" disabled={!online || !findName.trim()} onClick={findAndPair}>
            Find &amp; pair
          </button>
        )}
      </div>
      {(finding || findMsg) && (
        <p className="muted bt-hint bt-findmsg">
          {finding && <span className="bt-spin">⟳ </span>}
          {findMsg}
        </p>
      )}

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
            <li key={d.mac} className={isConn(d.mac) ? "connected" : undefined}>
              <span className="name">
                {d.name || "(no name)"}
                {isConn(d.mac) && <span className="bt-badge">connected</span>}
              </span>
              <span className="mac">{d.mac}</span>
              <span className="btns">
                {isConn(d.mac) ? (
                  <button onClick={() => act("disconnect")}>Disconnect</button>
                ) : (
                  <button onClick={() => act("connect", d.mac)}>Connect</button>
                )}
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
