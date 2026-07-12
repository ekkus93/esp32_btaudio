import { useEffect, useState } from "react";
import {
  playRadio, stopRadio, getStations, addStation, updateStation, deleteStation, moveStation,
  type RadioStatus, type Station,
} from "./api";

function kb(n: number) {
  return n > 1024 * 1024 ? `${(n / 1024 / 1024).toFixed(1)} MB` : `${(n / 1024).toFixed(0)} KB`;
}

export function Radio({ radio, onChange }: { radio?: RadioStatus; onChange: () => void }) {
  const [stations, setStations] = useState<Station[]>([]);
  const [name, setName] = useState("");
  const [url, setUrl] = useState("");
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  // Inline row editor (accordion), independent of the add form below.
  const [editId, setEditId] = useState<number | null>(null);
  const [editName, setEditName] = useState("");
  const [editUrl, setEditUrl] = useState("");
  const [editErr, setEditErr] = useState<string | null>(null);

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

  // Add form (bottom) — add-only now that editing is inline.
  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!url) return;
    const r = await addStation(name, url);
    if (r.ok) { setName(""); setUrl(""); loadStations(); }
    else setErr((r as { error?: string }).error || "rejected");
  };

  // Inline row editor.
  const startEdit = (s: Station) => {
    setEditId(s.id); setEditName(s.name); setEditUrl(s.url); setEditErr(null);
  };
  const cancelEdit = () => { setEditId(null); setEditErr(null); };
  const saveEdit = async () => {
    if (editId == null || !editUrl) return;
    setBusy(true); setEditErr(null);
    try {
      const r = await updateStation(editId, editName, editUrl);
      if (r.ok) { setEditId(null); loadStations(); onChange(); }
      else setEditErr((r as { error?: string }).error || "rejected");
    } finally {
      setBusy(false);
    }
  };

  const bufPct = radio && radio.ring_cap ? Math.round((radio.ring_used / radio.ring_cap) * 100) : 0;

  return (
    <section className="card radio">
      <h2>
        Radio
        <span className={`dot ${radio?.playing ? "ok" : "bad"}`} title={radio?.playing ? "playing" : "stopped"} />
      </h2>

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

      <ul className="bt-list">
        {stations.map((s, i) => (
          <li key={s.id} className={editId === s.id ? "editing" : undefined}>
            {editId === s.id ? (
              <div className="radio-edit">
                <input value={editName} onChange={(e) => setEditName(e.target.value)} placeholder="name (optional)" disabled={busy} />
                <input value={editUrl} onChange={(e) => setEditUrl(e.target.value)} placeholder="stream or .pls/.m3u URL" spellCheck={false} disabled={busy} />
                <div className="radio-edit-btns">
                  <button className="primary" disabled={busy || !editUrl} onClick={saveEdit}>Save</button>
                  <button disabled={busy} onClick={cancelEdit}>Cancel</button>
                </div>
                {editErr && <div className="banner err">{editErr}</div>}
              </div>
            ) : (
              <>
                <button className="radio-play" disabled={busy} onClick={() => wrap(() => playRadio(s.url))} title={s.url}>▶</button>
                <span className="name">{s.name}</span>
                <span className="btns">
                  <span className="radio-move">
                    <button disabled={busy || i === 0} onClick={() => wrap(() => moveStation(s.id, "up"), true)} title="Move up" aria-label="Move up">↑</button>
                    <button disabled={busy || i === stations.length - 1} onClick={() => wrap(() => moveStation(s.id, "down"), true)} title="Move down" aria-label="Move down">↓</button>
                  </span>
                  <button disabled={busy} onClick={() => startEdit(s)}>Edit</button>
                  <button className="danger" disabled={busy} onClick={() => wrap(() => deleteStation(s.id), true)}>×</button>
                </span>
              </>
            )}
          </li>
        ))}
        {stations.length === 0 && <li className="muted">No stations. Add one below.</li>}
      </ul>

      <form className="radio-add" onSubmit={submit}>
        <input value={name} onChange={(e) => setName(e.target.value)} placeholder="name (optional)" disabled={busy} />
        <input value={url} onChange={(e) => setUrl(e.target.value)} placeholder="stream or .pls/.m3u URL" spellCheck={false} disabled={busy} />
        <button className="primary" type="submit" disabled={busy || !url}>Add</button>
        <button type="button" disabled={busy || !url} onClick={() => wrap(() => playRadio(url))} title="play without saving">Play</button>
      </form>
      {err && <div className="banner err">{err}</div>}
    </section>
  );
}
