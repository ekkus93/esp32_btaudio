import { useEffect, useState } from "react";
import { playRadio, stopRadio, type RadioStatus } from "./api";

// Quick-play presets — SomaFM (reliably reachable, well-behaved SHOUTcast/ICY),
// used to demo/validate RADIO-1b. RADIO-1c seeds the SPEC §5.4 list into NVS and
// makes them web-editable; the internet-radio.com snapshot stations rotate and
// several had unreachable stream ports, so these are the working defaults here.
const PRESETS = [
  { name: "Groove Salad", url: "http://somafm.com/groovesalad.pls" },
  { name: "Drone Zone", url: "http://somafm.com/dronezone.pls" },
  { name: "DEF CON", url: "http://somafm.com/defcon.pls" },
  { name: "Indie Pop Rocks", url: "http://somafm.com/indiepop.pls" },
  { name: "Beat Blender", url: "http://somafm.com/beatblender.pls" },
];

function kb(n: number) {
  return n > 1024 * 1024 ? `${(n / 1024 / 1024).toFixed(1)} MB` : `${(n / 1024).toFixed(0)} KB`;
}

export function Radio({ radio, onChange }: { radio?: RadioStatus; onChange: () => void }) {
  const [url, setUrl] = useState("");
  const [busy, setBusy] = useState(false);

  const play = async (u: string) => {
    if (!u) return;
    setBusy(true);
    try {
      await playRadio(u);
    } finally {
      setBusy(false);
      onChange();
    }
  };
  const stop = async () => {
    setBusy(true);
    try {
      await stopRadio();
    } finally {
      setBusy(false);
      onChange();
    }
  };

  // periodic refresh while playing so telemetry ticks
  useEffect(() => {
    if (!radio?.playing) return;
    const id = setInterval(onChange, 2000);
    return () => clearInterval(id);
  }, [radio?.playing, onChange]);

  const bufPct = radio && radio.ring_cap ? Math.round((radio.ring_used / radio.ring_cap) * 100) : 0;

  return (
    <section className="card radio">
      <h2>
        Radio
        <span className={`dot ${radio?.playing ? "ok" : "bad"}`} title={radio?.playing ? "playing" : "stopped"} />
      </h2>

      <div className="radio-presets">
        {PRESETS.map((p) => (
          <button key={p.url} disabled={busy} onClick={() => play(p.url)} title={p.url}>{p.name}</button>
        ))}
      </div>

      <form className="radio-url" onSubmit={(e) => { e.preventDefault(); play(url); }}>
        <input
          value={url}
          onChange={(e) => setUrl(e.target.value)}
          placeholder="stream or .pls/.m3u URL"
          spellCheck={false}
          disabled={busy}
        />
        <button className="primary" type="submit" disabled={busy || !url}>Play</button>
        <button type="button" disabled={busy || !radio?.playing} onClick={stop}>Stop</button>
      </form>

      {radio?.playing ? (
        <div className="radio-now">
          <div className="np">{radio.title || radio.station || "…"}</div>
          <div className="fields">
            <span>{radio.codec}{radio.bitrate ? ` ${radio.bitrate}k` : ""}</span>
            <span>buffer {bufPct}%</span>
            <span>{kb(radio.bytes_in)} in</span>
            {radio.reconnects > 0 && <span>{radio.reconnects} reconnects</span>}
          </div>
          {radio.station && <div className="muted">{radio.station}</div>}
        </div>
      ) : (
        <p className="muted">
          Pick a station or paste a URL. (Audio starts once the decoder lands in RADIO-2 —
          for now this validates streaming: codec, buffer, and “now playing”.)
        </p>
      )}
    </section>
  );
}
