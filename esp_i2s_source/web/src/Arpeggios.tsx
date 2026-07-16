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

/** Delay that is abortable via an AbortSignal. */
async function delay(ms: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve) => {
    if (signal.aborted) return resolve();
    const id = setTimeout(resolve, ms);
    signal.addEventListener("abort", () => clearTimeout(id), { once: true });
  });
}

/** Play a sequence of MIDI notes serially, aborting on cleanup. */
async function playSequence(
  notes: number[],
  amp: number,
  signal: AbortSignal,
) {
  for (const midi of notes) {
    if (signal.aborted) return;
    await setTone(Math.round(midiToFreq(midi)), amp, "piano");
    await delay(STEP_MS, signal);
  }
  await toneOff();
}

export function Arpeggios() {
  const [playing, setPlaying] = useState<string | null>(null);
  const abortRef = useRef<AbortController | null>(null);

  const stop = () => {
    if (abortRef.current) abortRef.current.abort();
    abortRef.current = null;
    setPlaying(null);
    toneOff().catch(() => {});
  };

  const play = (arp: { name: string; notes: number[] }) => {
    // Cancel any in-flight sequence.
    stop();
    const controller = new AbortController();
    abortRef.current = controller;
    setPlaying(arp.name);

    // Ascending then back down (without repeating the top), for a fuller run.
    const seq = [...arp.notes, ...arp.notes.slice(0, -1).reverse()];

    const run = async () => {
      try {
        await playSequence(seq, AMP, controller.signal);
      } catch {
        // AbortError during delay — expected on stop/cleanup.
        if (!controller.signal.aborted) {
          console.error("arpeggio playback failed");
        }
      } finally {
        if (!controller.signal.aborted) {
          setPlaying(null);
        }
      }
    };

    void run();
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
