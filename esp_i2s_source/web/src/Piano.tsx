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
  const heldRef = useRef(false);

  const press = (midi: number) => {
    heldRef.current = true;
    setActive(midi);
    setTone(Math.round(midiToFreq(midi))).catch(() => {});
  };
  const release = () => {
    if (!heldRef.current) return;
    heldRef.current = false;
    setActive(null);
    toneOff().catch(() => {});
  };

  return (
    <section className="card piano">
      <h2>Piano</h2>
      <div className="piano-keys" onPointerUp={release} onPointerLeave={release} onPointerCancel={release}>
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
        Press a key to play its note as a test tone over Bluetooth (hold to sustain). C4 is middle C.
      </p>
    </section>
  );
}
