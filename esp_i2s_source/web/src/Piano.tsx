import { useRef, useState } from "react";
import { setTone, toneOff } from "./api";

// Two octaves: C3 (MIDI 48) through C5 (72) — one octave below middle C (C4=60)
// and one above. Each key plays its note as a test tone over Bluetooth.
const LOW = 48;
const HIGH = 72;
const midiToFreq = (m: number) => 440 * Math.pow(2, (m - 69) / 12);
const isBlack = (m: number) => [1, 3, 6, 8, 10].includes(((m % 12) + 12) % 12);

type White = { midi: number; label?: string; sharp?: number };

// White keys in order; each carries the black key that sits over its right edge.
const WHITES: White[] = (() => {
  const out: White[] = [];
  for (let m = LOW; m <= HIGH; m++) {
    if (isBlack(m)) continue;
    const octave = Math.floor(m / 12) - 1; // MIDI: C4 = 60 => octave 4
    out.push({
      midi: m,
      label: m % 12 === 0 ? `C${octave}` : undefined,
      sharp: m + 1 <= HIGH && isBlack(m + 1) ? m + 1 : undefined,
    });
  }
  return out;
})();

export function Piano() {
  const [active, setActive] = useState<number | null>(null);
  const [durMs, setDurMs] = useState(500); // note length (ms)
  const [vol, setVol] = useState(30); // note amplitude (% of full scale)
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const pressIdRef = useRef(0);

  const stop = () => {
    if (timerRef.current) { clearTimeout(timerRef.current); timerRef.current = null; }
    setActive(null);
    toneOff().catch(() => {});
  };

  // Each key press plays the note for the full set length, then auto-stops.
  // We AWAIT note-on before scheduling note-off so the DELETE can never overtake
  // the POST over WiFi (which would leave a stuck tone on short notes). The
  // press-id guard keeps a superseded note's timer from cutting a newer one.
  const press = async (midi: number) => {
    const id = ++pressIdRef.current;
    if (timerRef.current) { clearTimeout(timerRef.current); timerRef.current = null; }
    setActive(midi);
    await setTone(Math.round(midiToFreq(midi)), vol, "piano").catch(() => {});
    if (pressIdRef.current !== id) return; // a newer key took over
    timerRef.current = setTimeout(() => {
      if (pressIdRef.current === id) stop();
    }, durMs);
  };

  return (
    <section className="card piano">
      <h2>Piano</h2>

      <div className="vol-row">
        <label>Note length</label>
        <input type="range" min={100} max={2000} step={50} value={durMs} onChange={(e) => setDurMs(Number(e.target.value))} />
        <span className="vol-val">{(durMs / 1000).toFixed(2)}s</span>
      </div>
      <div className="vol-row">
        <label>Note volume</label>
        <input type="range" min={0} max={100} value={vol} onChange={(e) => setVol(Number(e.target.value))} />
        <span className="vol-val">{vol}%</span>
      </div>

      <div className="piano-keys">
        {WHITES.map((w) => (
          <div
            key={w.midi}
            className={`pkey white${active === w.midi ? " on" : ""}${w.midi === 60 ? " mid" : ""}`}
            onPointerDown={(e) => { e.preventDefault(); press(w.midi); }}
          >
            {w.sharp !== undefined && (
              <div
                className={`pkey black${active === w.sharp ? " on" : ""}`}
                onPointerDown={(e) => { e.preventDefault(); e.stopPropagation(); press(w.sharp!); }}
              />
            )}
            {w.label && <span className="klabel">{w.label}</span>}
          </div>
        ))}
      </div>
      <p className="muted">
        Each key plays its note (over Bluetooth) for the set length at the set volume. C4 is middle C.
      </p>
    </section>
  );
}
