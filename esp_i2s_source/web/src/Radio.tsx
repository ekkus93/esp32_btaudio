import { useEffect, useState } from "react";
import {
  playRadio, stopRadio, getStations, addStation, updateStation, deleteStation,
  type RadioStatus, type Station,
} from "./api";

function kb(n: number) {
  return n > 1024 * 1024 ? `${(n / 1024 / 1024).toFixed(1)} MB` : `${(n / 1024).toFixed(0)} KB`;
}

export function Radio({ radio, onChange }: { radio?: RadioStatus; onChange: () => void }) {
  const [stations, setStations] = useState<Station[]>([]);
  const [name, setName] = useState("");
  const [url, setUrl] = useState("");
  const [editId, setEditId] = useState<number | null>(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  const loadStations = () => getStations().then(setStations).catch(() => {});
  useEffect(() => { loadStations(); }, []);

  // tick telemetry while playing
  useEffect(() => {
    if (!radio?.playing) return;
    const id = setInterval(onChange, 2000);
    return () => clearInterval(id);
  }, [radio?.playing, onChange]);

  const wrap = async (fn: () => Promise<unknown>, reload = false) => {
    setBusy(true);
    setErr(null);
    try {
      await fn();
    } finally {
      setBusy(false);
      if (reload) loadStations();
      onChange();
    }
  };

  const resetForm = () => { setName(""); setUrl(""); setEditId(null); };

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!url) return;
    const r = editId != null ? await updateStation(editId, name, url) : await addStation(name, url);
    if (r.ok) { resetForm(); loadStations(); }
    else setErr((r as { error?: string }).error || "rejected");
  };

  const bufPct = radio && radio.ring_cap ? Math.round((radio.ring_used / radio.ring_cap) * 100) : 0;

  return (
    <section className="card radio">
      <h2>
        Radio
        <span className={`dot ${radio?.playing ? "ok" : "bad"}`} title={radio?.playing ? "playing" : "stopped"} />
      </h2>

      <ul className="bt-list">
        {stations.map((s) => (
          <li key={s.id}>
            <button className="radio-play" disabled={busy} onClick={() => wrap(() => playRadio(s.url))} title={s.url}>▶</button>
            <span className="name">{s.name}</span>
            <span className="btns">
              <button disabled={busy} onClick={() => { setEditId(s.id); setName(s.name); setUrl(s.url); }}>Edit</button>
              <button className="danger" disabled={busy} onClick={() => wrap(() => deleteStation(s.id), true)}>×</button>
            </span>
          </li>
        ))}
        {stations.length === 0 && <li className="muted">No stations. Add one below.</li>}
      </ul>

      <form className="radio-add" onSubmit={submit}>
        <input value={name} onChange={(e) => setName(e.target.value)} placeholder="name (optional)" disabled={busy} />
        <input value={url} onChange={(e) => setUrl(e.target.value)} placeholder="stream or .pls/.m3u URL" spellCheck={false} disabled={busy} />
        <button className="primary" type="submit" disabled={busy || !url}>{editId != null ? "Update" : "Add"}</button>
        {editId != null && <button type="button" disabled={busy} onClick={resetForm}>Cancel</button>}
        {editId == null && <button type="button" disabled={busy || !url} onClick={() => wrap(() => playRadio(url))} title="play without saving">Play</button>}
      </form>
      {err && <div className="banner err">{err}</div>}

      {radio?.playing ? (
        <div className="radio-now">
          <div className="np">{radio.title || radio.station || "…"}</div>
          <div className="fields">
            <span>{radio.codec}{radio.bitrate ? ` ${radio.bitrate}k` : ""}</span>
            <span>buffer {bufPct}%</span>
            <span>{kb(radio.bytes_in)} in</span>
            {radio.reconnects > 0 && <span>{radio.reconnects} reconnects</span>}
            <button className="stop" disabled={busy} onClick={() => wrap(stopRadio)}>Stop</button>
          </div>
          {radio.station && <div className="muted">{radio.station}</div>}
        </div>
      ) : (
        <p className="muted">
          Play a station. (Audio starts once the decoder lands in RADIO-2 — for now this
          validates streaming: codec, buffer, and “now playing”.)
        </p>
      )}
    </section>
  );
}
