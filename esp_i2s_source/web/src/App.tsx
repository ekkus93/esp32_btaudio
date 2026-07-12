import { useEffect, useState } from "react";
import {
  getStatus, setWifi, setTone, toneOff,
  setS3Volume, getBtVolume, setBtVolume, setApEnabled,
  type DeviceStatus, type ApStatus,
} from "./api";
import { Terminal } from "./Terminal";
import { Bluetooth } from "./Bluetooth";
import { Radio } from "./Radio";

function fmtUptime(s: number): string {
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  return h > 0 ? `${h}h ${m}m` : m > 0 ? `${m}m ${sec}s` : `${sec}s`;
}

function Field({ k, v }: { k: string; v: React.ReactNode }) {
  return (
    <div className="field">
      <span className="k">{k}</span>
      <span className="v">{v}</span>
    </div>
  );
}

function Card({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <section className="card">
      <h2>{title}</h2>
      {children}
    </section>
  );
}

function ProvisionForm() {
  const [ssid, setSsid] = useState("");
  const [pass, setPass] = useState("");
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!ssid) return;
    setBusy(true);
    setMsg(null);
    try {
      const r = await setWifi(ssid, pass);
      if (r.ok) {
        setMsg({
          ok: true,
          text: `Joining "${ssid}"… reconnect this device to your network, then open http://${r.host ?? "esp-i2s-source.local"}`,
        });
      } else {
        setMsg({ ok: false, text: r.error ?? "provisioning failed" });
        setBusy(false);
      }
    } catch (err) {
      // The AP tears down as STA comes up, so the reply may not arrive — that's
      // expected on success. Show the same guidance.
      setMsg({
        ok: true,
        text: `Joining "${ssid}"… reconnect this device to your network, then open http://esp-i2s-source.local`,
      });
    }
  };

  return (
    <section className="card provision">
      <h2>Set up WiFi</h2>
      <p className="muted">
        This device is in setup mode. Enter your WiFi so it can join your network.
      </p>
      <form onSubmit={submit}>
        <label>
          Network (SSID)
          <input value={ssid} onChange={(e) => setSsid(e.target.value)} autoFocus disabled={busy} />
        </label>
        <label>
          Password
          <input
            type="password"
            value={pass}
            onChange={(e) => setPass(e.target.value)}
            placeholder="(leave blank if open)"
            disabled={busy}
          />
        </label>
        <button type="submit" disabled={busy || !ssid}>
          {busy ? "Connecting…" : "Connect"}
        </button>
      </form>
      {msg && <div className={`banner ${msg.ok ? "ok" : "err"}`}>{msg.text}</div>}
    </section>
  );
}

const TONE_PRESETS = [220, 440, 1000, 4000];

function ToneControl({ tone, onChange }: { tone?: { on: boolean; hz: number }; onChange: () => void }) {
  const [hz, setHz] = useState(440);
  const [busy, setBusy] = useState(false);

  // Sync the slider/input from device state when not mid-edit.
  useEffect(() => {
    if (tone?.hz) setHz(tone.hz);
  }, [tone?.hz]);

  const on = tone?.on ?? false;

  const act = async (fn: () => Promise<unknown>) => {
    setBusy(true);
    try {
      await fn();
    } finally {
      setBusy(false);
      onChange();
    }
  };

  return (
    <section className="card tone">
      <h2>
        Tone
        <span className={`dot ${on ? "ok" : "bad"}`} title={on ? "on" : "off"} />
      </h2>
      <div className="tone-row">
        <input
          type="number"
          min={20}
          max={20000}
          value={hz}
          onChange={(e) => setHz(Number(e.target.value))}
          disabled={busy}
        />
        <span className="muted">Hz</span>
        <button className="primary" disabled={busy} onClick={() => act(() => setTone(hz))}>
          {on ? "Set" : "Play"}
        </button>
        <button disabled={busy || !on} onClick={() => act(toneOff)}>
          Stop
        </button>
      </div>
      <div className="tone-presets">
        {TONE_PRESETS.map((p) => (
          <button key={p} disabled={busy} onClick={() => { setHz(p); act(() => setTone(p)); }}>
            {p >= 1000 ? `${p / 1000}k` : p}
          </button>
        ))}
      </div>
      <p className="muted">Streams over Bluetooth to the connected speaker.</p>
    </section>
  );
}

function ApControl({ ap, onChange }: { ap?: ApStatus; onChange: () => void }) {
  const [busy, setBusy] = useState(false);
  if (!ap) return null;

  const toggle = async () => {
    setBusy(true);
    try {
      await setApEnabled(!ap.enabled);
    } finally {
      setBusy(false);
      onChange();
    }
  };

  return (
    <section className="card">
      <h2>
        Control AP
        <span className={`dot ${ap.on ? "ok" : "bad"}`} title={ap.on ? "broadcasting" : "off"} />
      </h2>
      <p className="muted">
        The device's own WiFi. Connect to it to reach this page directly — even
        away from your home network.
      </p>
      <Field k="Network" v={ap.ssid} />
      <Field k="Password" v={ap.pass} />
      <Field k="Address" v={ap.on ? `http://${ap.ip ?? "192.168.4.1"}` : "—"} />
      {ap.on && typeof ap.clients === "number" && <Field k="Clients" v={ap.clients} />}
      <label className="ap-toggle">
        <input type="checkbox" checked={ap.enabled} onChange={toggle} disabled={busy} />
        Keep AP on alongside WiFi
      </label>
    </section>
  );
}

function VolumeControl({ s3gain, onChange }: { s3gain?: number; onChange: () => void }) {
  const [pre, setPre] = useState(100); // ESP32-S3 pre-I2S gain %
  const [post, setPost] = useState(40); // WROOM32 post-mix volume
  const [postKnown, setPostKnown] = useState(false);

  // Sync the pre-I2S slider from device status (polled).
  useEffect(() => {
    if (typeof s3gain === "number") setPre(s3gain);
  }, [s3gain]);

  // Fetch the WROOM32 volume once on mount (a bt_link round-trip — not polled).
  useEffect(() => {
    getBtVolume()
      .then((v) => {
        if (v.vol >= 0) {
          setPost(v.vol);
          setPostKnown(true);
        }
      })
      .catch(() => {});
  }, []);

  return (
    <section className="card">
      <h2>Volume</h2>

      <div className="vol-row">
        <label>
          Pre-I2S <span className="muted">(ESP32-S3 source)</span>
        </label>
        <input
          type="range"
          min={0}
          max={100}
          value={pre}
          onChange={(e) => setPre(Number(e.target.value))}
          onPointerUp={(e) => setS3Volume(Number((e.target as HTMLInputElement).value)).then(onChange)}
        />
        <span className="vol-val">{pre}%</span>
      </div>

      <div className="vol-row">
        <label>
          Post-mix <span className="muted">(WROOM32 → A2DP)</span>
        </label>
        <input
          type="range"
          min={0}
          max={100}
          value={post}
          onChange={(e) => setPost(Number(e.target.value))}
          onPointerUp={(e) => setBtVolume(Number((e.target as HTMLInputElement).value)).then(() => setPostKnown(true))}
        />
        <span className="vol-val">{postKnown ? post : "?"}</span>
      </div>

      <p className="muted">
        Pre-I2S trims the S3 source before it leaves over I2S; post-mix is the
        WROOM32's final A2DP level (persisted). Use post-mix as your main control.
      </p>
    </section>
  );
}

const TABS = [
  { id: "radio", label: "Radio" },
  { id: "tone", label: "Tone" },
  { id: "terminal", label: "Terminal" },
  { id: "settings", label: "Settings" },
] as const;

export function App() {
  const [status, setStatus] = useState<DeviceStatus | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [tab, setTab] = useState<string>("radio");

  const refresh = () =>
    getStatus()
      .then((s) => (setStatus(s), setErr(null)))
      .catch((e) => setErr(String(e.message ?? e)));

  useEffect(() => {
    refresh();
    const id = setInterval(refresh, 3000);
    return () => clearInterval(id);
  }, []);

  const online = !err && status != null;
  const w = status?.wifi;

  return (
    <div className="app">
      <header>
        <h1>ESP32-S3 Audio Source</h1>
        <span className={`dot ${online ? "ok" : "bad"}`} title={online ? "online" : "unreachable"} />
      </header>

      {err && <div className="banner err">Device unreachable: {err}</div>}

      {w?.mode === "AP" && (
        <div className="grid">
          <ProvisionForm />
        </div>
      )}

      <nav className="tabs">
        {TABS.map((t) => (
          <button
            key={t.id}
            className={`tab ${tab === t.id ? "active" : ""}`}
            onClick={() => setTab(t.id)}
          >
            {t.label}
          </button>
        ))}
      </nav>

      {tab === "settings" && (
        <div className="grid">
          <Card title="Network">
            {w ? (
              <>
                <Field k="Mode" v={w.mode} />
                {w.state && <Field k="State" v={w.state} />}
                {w.ssid && <Field k="SSID" v={w.ssid} />}
                <Field k="IP" v={w.ip || "—"} />
                {typeof w.rssi === "number" && w.rssi !== 0 && <Field k="RSSI" v={`${w.rssi} dBm`} />}
              </>
            ) : (
              <p className="muted">…</p>
            )}
          </Card>

          <Card title="System">
            <Field k="Device" v={status?.device ?? "—"} />
            <Field k="Firmware" v={status?.version ?? "—"} />
            <Field k="Uptime" v={status ? fmtUptime(status.uptime_s) : "—"} />
            <Field k="Free heap" v={status ? `${(status.heap_free / 1024).toFixed(0)} KB` : "—"} />
            <Field
              k="BT bridge"
              v={status?.wroom?.reachable ? (status.wroom.version ?? "reachable") : "unreachable"}
            />
          </Card>

          <ApControl ap={w?.ap} onChange={refresh} />
          <Bluetooth />
        </div>
      )}

      {tab === "radio" && (
        <div className="grid">
          {status && w?.state !== "CONNECTED" ? (
            <section className="card wifi-gate">
              <h2>WiFi required</h2>
              <p className="muted">
                Internet radio needs a WiFi connection, and this device isn't on your
                network yet. Set up WiFi first, then come back to play a station.
              </p>
              <button className="primary" onClick={() => setTab("settings")}>
                Go to Settings
              </button>
            </section>
          ) : (
            <>
              <Radio radio={status?.radio} onChange={refresh} />
              <VolumeControl s3gain={status?.i2s?.gain} onChange={refresh} />
            </>
          )}
        </div>
      )}

      {tab === "tone" && (
        <div className="grid">
          <ToneControl tone={status?.tone} onChange={refresh} />
          <VolumeControl s3gain={status?.i2s?.gain} onChange={refresh} />
        </div>
      )}

      {tab === "terminal" && (
        <div className="grid">
          <Terminal />
        </div>
      )}

      <footer className="muted">esp-i2s-source · {status?.version ?? "…"}</footer>
    </div>
  );
}
