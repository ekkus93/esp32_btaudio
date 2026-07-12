import { useRef, useState } from "react";
import { setTone, toneOff } from "./api";

const midiToFreq = (m: number) => 440 * Math.pow(2, (m - 69) / 12);

// Arpeggios as MIDI notes rooted at middle C (C4 = 60), played ascending.
const ARPS: { name: string; notes: number[] }[] = [
  { name: "C Major", notes: [60, 64, 67, 72] },
  { name: "C Minor", notes: [60, 63, 67, 72] },
  { name: "Major 7", notes: [60, 64, 67, 71] },
  { name: "Dom 7", notes: [60, 64, 67, 70] },
  { name: "Dim", notes: [60, 63, 66, 69] },
  { name: "Octaves", notes: [48, 60, 72] },
];

const STEP_MS = 220; // per-note length
const AMP = 30;      // amplitude %

export function Arpeggios() {
  const [playing, setPlaying] = useState<string | null>(null);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const stop = () => {
    if (timerRef.current) { clearTimeout(timerRef.current); timerRef.current = null; }
    toneOff().catch(() => {});
    setPlaying(null);
  };

  const play = (arp: { name: string; notes: number[] }) => {
    if (timerRef.current) clearTimeout(timerRef.current);
    setPlaying(arp.name);
    // Ascending then back down (without repeating the top), for a fuller run.
    const seq = [...arp.notes, ...arp.notes.slice(0, -1).reverse()];
    let i = 0;
    const tick = () => {
      if (i >= seq.length) { stop(); return; }
      setTone(Math.round(midiToFreq(seq[i])), AMP).catch(() => {});
      i++;
      timerRef.current = setTimeout(tick, STEP_MS);
    };
    tick();
  };

  return (
    <section className="card arps">
      <h2>Arpeggios</h2>
      <div className="arp-btns">
        {ARPS.map((a) => (
          <button
            key={a.name}
            className={playing === a.name ? "primary" : ""}
            onClick={() => play(a)}
          >
            {a.name}
          </button>
        ))}
      </div>
      <p className="muted">Plays the chord up and back down over Bluetooth.</p>
    </section>
  );
}
