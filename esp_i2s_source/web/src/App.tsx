import { useEffect, useRef, useState } from "react";
import {
  getStatus, setWifi, setTone, toneOff,
  setS3Volume, getBtVolume, setBtVolume, setApEnabled, setApConfig,
  type DeviceStatus, type ApStatus, type WifiStatus,
} from "./api";
import { Terminal } from "./Terminal";
import { Bluetooth } from "./Bluetooth";
import { Radio } from "./Radio";
import { Piano } from "./Piano";

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

function ProvisionForm({ wifi }: { wifi?: WifiStatus }) {
  const [ssid, setSsid] = useState("");
  const [pass, setPass] = useState("");
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const connected = wifi?.state === "CONNECTED";

  // Pre-fill the SSID with the current network once known (until the user edits).
  const didFill = useRef(false);
  useEffect(() => {
    if (!didFill.current && wifi?.ssid) {
      setSsid(wifi.ssid);
      didFill.current = true;
    }
  }, [wifi?.ssid]);

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!ssid) return;
    // Same network, blank password field -> nothing to change.
    if (connected && ssid === wifi?.ssid && !pass) {
      setMsg({ ok: true, text: `Already connected to "${ssid}".` });
      return;
    }
    setBusy(true);
    setMsg(null);
    const guidance =
      `Joining "${ssid}"… the device is reconnecting. Reach it at ` +
      `http://esp-i2s-source.local (once on your network) or via its own AP at http://192.168.4.1.`;
    try {
      const r = await setWifi(ssid, pass);
      if (r.ok || r.ok === undefined) {
        setMsg({ ok: true, text: guidance });
      } else {
        setMsg({ ok: false, text: r.error ?? "provisioning failed" });
      }
    } catch {
      // The reply may not arrive as the link switches — expected on success.
      setMsg({ ok: true, text: guidance });
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="card provision">
      <h2>{connected ? "WiFi" : "Set up WiFi"}</h2>
      <p className="muted">
        {connected
          ? `Connected to "${wifi?.ssid}". Enter a different network below to switch. The control AP stays up, so you won't lose access if the new network fails.`
          : "Enter your WiFi so the device can join your network."}
      </p>
      <form onSubmit={submit}>
        <label>
          Network (SSID)
          <input value={ssid} onChange={(e) => setSsid(e.target.value)} disabled={busy} />
        </label>
        <label>
          Password
          <PasswordField
            value={pass}
            onChange={setPass}
            placeholder={connected ? "••••••••" : "(leave blank if open)"}
            disabled={busy}
          />
        </label>
        <button type="submit" disabled={busy || !ssid}>
          {busy ? "Connecting…" : connected ? "Switch network" : "Connect"}
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

// Password input with a Show/Hide toggle. Reused by the WiFi and Control-AP
// forms (revealing what you type / the stored AP key is intentional here).
function PasswordField({
  value,
  onChange,
  placeholder,
  disabled,
}: {
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
  disabled?: boolean;
}) {
  const [show, setShow] = useState(false);
  return (
    <div className="pw-wrap">
      <input
        type={show ? "text" : "password"}
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder={placeholder}
        disabled={disabled}
        spellCheck={false}
        autoCapitalize="off"
        autoCorrect="off"
      />
      <button
        type="button"
        className="pw-toggle"
        onClick={() => setShow((s) => !s)}
        disabled={disabled}
        tabIndex={-1}
        aria-label={show ? "Hide password" : "Show password"}
      >
        {show ? "Hide" : "Show"}
      </button>
    </div>
  );
}

function ApControl({ ap, onChange }: { ap?: ApStatus; onChange: () => void }) {
  const [busy, setBusy] = useState(false);
  const [ssid, setSsid] = useState("");
  const [pass, setPass] = useState("");
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  // Pre-fill the editable fields from the device's current AP config, once.
  const didFill = useRef(false);
  useEffect(() => {
    if (!didFill.current && ap?.ssid) {
      setSsid(ap.ssid);
      setPass(ap.pass ?? "");
      didFill.current = true;
    }
  }, [ap?.ssid, ap?.pass]);

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

  const save = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!ssid) return;
    if (pass && (pass.length < 8 || pass.length > 64)) {
      setMsg({ ok: false, text: "Password must be blank (open) or 8-64 characters." });
      return;
    }
    setBusy(true);
    setMsg(null);
    try {
      const r = await setApConfig(ssid, pass);
      if (r.ok) {
        setMsg({
          ok: true,
          text: `Control AP is now "${ssid}". If you're connected to the AP, rejoin with the new name/password.`,
        });
      } else {
        setMsg({ ok: false, text: r.error ?? "update failed" });
      }
    } catch {
      // Renaming the AP can drop the very connection serving this request.
      setMsg({ ok: true, text: `Applying "${ssid}"… reconnect to the AP with the new name/password.` });
    } finally {
      setBusy(false);
      onChange();
    }
  };

  return (
    <section className="card provision">
      <h2>
        Control AP
        <span className={`dot ${ap.on ? "ok" : "bad"}`} title={ap.on ? "broadcasting" : "off"} />
      </h2>
      <p className="muted">
        The device's own WiFi. Connect to it to reach this page directly — even
        away from your home network.
      </p>
      <form onSubmit={save}>
        <label>
          Network (SSID)
          <input value={ssid} onChange={(e) => setSsid(e.target.value)} disabled={busy} />
        </label>
        <label>
          Password
          <PasswordField
            value={pass}
            onChange={setPass}
            placeholder="(blank = open network)"
            disabled={busy}
          />
        </label>
        <button type="submit" disabled={busy || !ssid}>
          {busy ? "Saving…" : "Save AP name/password"}
        </button>
      </form>
      {msg && <div className={`banner ${msg.ok ? "ok" : "err"}`}>{msg.text}</div>}
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
  const [pre, setPre] = useState(30); // ESP32-S3 pre-I2S gain % (device default)
  const [post, setPost] = useState(10); // WROOM32 post-mix volume (device default)
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
    <section className="card volume">
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
        <span className="vol-val">{postKnown ? `${post}%` : "?"}</span>
      </div>

      <p className="muted">
        Both are 0–100% of full scale. Pre-I2S trims the S3 source before it
        leaves over I2S; post-mix is the WROOM32's final A2DP level. Both are
        saved and restored on reboot — use post-mix as your main control.
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

  // First-time setup (AP mode): land on Settings where the WiFi form lives.
  const didInitTab = useRef(false);
  useEffect(() => {
    if (!didInitTab.current && status) {
      didInitTab.current = true;
      if (status.wifi?.mode === "AP") setTab("settings");
    }
  }, [status]);

  const online = !err && status != null;
  const w = status?.wifi;

  return (
    <div className="app">
      <header>
        <h1>ESP32-S3 Audio Source</h1>
        <span className={`dot ${online ? "ok" : "bad"}`} title={online ? "online" : "unreachable"} />
      </header>

      {err && <div className="banner err">Device unreachable: {err}</div>}

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
        <div className="grid settings">
          {/* Row 1: Network + System */}
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

          {/* Row 2: WiFi + Control AP */}
          <ProvisionForm wifi={w} />
          <ApControl ap={w?.ap} onChange={refresh} />

          {/* Row 3: Bluetooth (full width) */}
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
          <Piano />
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
