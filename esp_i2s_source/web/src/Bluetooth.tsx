import { useEffect, useRef, useState } from "react";
import { useWs, type WsMessage } from "./ws";

interface Dev {
  mac: string;
  name: string;
}
interface PairPrompt {
  mac: string;
  info: string;
}

function splitMacName(data: string): Dev {
  const i = data.indexOf(",");
  return i < 0 ? { mac: data, name: "" } : { mac: data.slice(0, i), name: data.slice(i + 1) };
}

// BTUI-1: Bluetooth management over the shared /ws. Commands go out as term_in;
// scan results / paired items arrive as `info` frames, pairing prompts as
// `event` frames (SPEC §5.2, protocol per esp_bt_audio_source commands.c).
export function Bluetooth() {
  const { connected, command, subscribe } = useWs();
  const [devices, setDevices] = useState<Dev[]>([]);
  const [paired, setPaired] = useState<Dev[]>([]);
  const [scanning, setScanning] = useState(false);
  const [scanned, setScanned] = useState(false);   // a scan has completed at least once
  const [prompt, setPrompt] = useState<PairPrompt | null>(null);
  const [note, setNote] = useState<string>("");
  const scanTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const refreshPaired = () => {
    setPaired([]);
    command("PAIRED");
  };

  useEffect(() => {
    const unsub = subscribe((m: WsMessage) => {
      const cmd = m.command as string;
      const result = m.result as string;
      const data = (m.data as string) || "";
      if (m.type === "info" && cmd === "SCAN" && result === "RESULT") {
        const d = splitMacName(data);
        setDevices((cur) => (cur.some((x) => x.mac === d.mac) ? cur : [...cur, d]));
      } else if (m.type === "info" && cmd === "PAIRED" && result === "ITEM") {
        const d = splitMacName(data);
        setPaired((cur) => (cur.some((x) => x.mac === d.mac) ? cur : [...cur, d]));
      } else if (m.type === "event" && cmd === "PAIR" && result === "CONFIRM") {
        const d = splitMacName(data);
        setPrompt({ mac: d.mac, info: data });
      } else if (m.type === "event" && cmd === "PAIR" && (result === "SUCCESS" || result === "FAILED")) {
        setPrompt(null);
        setNote(`Pairing ${result.toLowerCase()}`);
        refreshPaired();
      } else if (m.type === "term_out") {
        if (["CONNECT", "DISCONNECT", "PAIR", "UNPAIR"].includes(cmd)) {
          setNote(`${cmd}: ${m.status} ${result || ""}`.trim());
          if (cmd === "UNPAIR" || cmd === "PAIR") refreshPaired();
        }
      }
    });
    refreshPaired();
    return unsub;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [subscribe]);

  const scan = () => {
    setDevices([]);
    setScanned(false);
    setScanning(true);
    command("SCAN");
    if (scanTimer.current) clearTimeout(scanTimer.current);
    // The WROOM32 inquiry runs ~13s and reports at the end, so wait past it.
    scanTimer.current = setTimeout(() => {
      setScanning(false);
      setScanned(true);
    }, 15000);
  };

  const isPaired = (mac: string) => paired.some((p) => p.mac === mac);

  return (
    <section className="card bt">
      <h2>
        Bluetooth
        <span className={`dot ${connected ? "ok" : "bad"}`} />
      </h2>

      <div className="bt-actions">
        <button className="primary" disabled={!connected || scanning} onClick={scan}>
          {scanning ? "Scanning…" : "Scan"}
        </button>
        <button disabled={!connected} onClick={() => command("DISCONNECT")}>Disconnect</button>
      </div>

      {scanning ? (
        <p className="muted bt-hint">
          Scanning… put the speaker/headphones you want in <strong>pairing mode</strong> now —
          only devices actively in pairing mode are discoverable.
        </p>
      ) : scanned && devices.length === 0 ? (
        <p className="muted bt-hint">
          No devices found. Bluetooth speakers/earbuds only appear while they're in
          <strong> pairing mode</strong> — put yours in pairing mode and scan again.
        </p>
      ) : devices.length === 0 ? (
        <p className="muted bt-hint">
          Tip: put your Bluetooth speaker/headphones in <strong>pairing mode</strong> before scanning.
        </p>
      ) : null}

      {devices.length > 0 && (
        <>
          <h3>Discovered</h3>
          <ul className="bt-list">
            {devices.map((d) => (
              <li key={d.mac}>
                <span className="name">{d.name || "(no name)"}</span>
                <span className="mac">{d.mac}</span>
                <span className="btns">
                  {!isPaired(d.mac) && <button onClick={() => command(`PAIR ${d.mac}`)}>Pair</button>}
                  <button onClick={() => command(`CONNECT ${d.mac}`)}>Connect</button>
                </span>
              </li>
            ))}
          </ul>
        </>
      )}

      <h3>Paired</h3>
      {paired.length === 0 ? (
        <p className="muted">None paired yet.</p>
      ) : (
        <ul className="bt-list">
          {paired.map((d) => (
            <li key={d.mac}>
              <span className="name">{d.name || "(no name)"}</span>
              <span className="mac">{d.mac}</span>
              <span className="btns">
                <button onClick={() => command(`CONNECT ${d.mac}`)}>Connect</button>
                <button className="danger" onClick={() => command(`UNPAIR ${d.mac}`)}>Unpair</button>
              </span>
            </li>
          ))}
        </ul>
      )}

      {note && <div className="banner ok">{note}</div>}

      {prompt && (
        <div className="bt-prompt">
          <p>Pairing request from <b>{prompt.mac}</b></p>
          <p className="muted">{prompt.info}</p>
          <div className="bt-actions">
            <button className="primary" onClick={() => { command("CONFIRM_PIN ACCEPT"); setPrompt(null); }}>Accept</button>
            <button className="danger" onClick={() => { command("CONFIRM_PIN REJECT"); setPrompt(null); }}>Reject</button>
          </div>
        </div>
      )}
    </section>
  );
}
