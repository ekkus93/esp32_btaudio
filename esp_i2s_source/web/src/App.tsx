import { useEffect, useState } from "react";
import { getStatus, setWifi, type DeviceStatus } from "./api";
import { Terminal } from "./Terminal";

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

function Placeholder({ title, task }: { title: string; task: string }) {
  return (
    <section className="card stub">
      <h2>{title}</h2>
      <p className="muted">Coming in {task}.</p>
    </section>
  );
}

export function App() {
  const [status, setStatus] = useState<DeviceStatus | null>(null);
  const [err, setErr] = useState<string | null>(null);

  useEffect(() => {
    let alive = true;
    const tick = () =>
      getStatus()
        .then((s) => alive && (setStatus(s), setErr(null)))
        .catch((e) => alive && setErr(String(e.message ?? e)));
    tick();
    const id = setInterval(tick, 3000);
    return () => {
      alive = false;
      clearInterval(id);
    };
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

        <Terminal />
        <Placeholder title="Tone" task="WEB-1d" />
        <Placeholder title="Radio" task="RADIO-1" />
        <Placeholder title="Bluetooth" task="BTUI-1" />
      </div>

      <footer className="muted">esp-i2s-source · {status?.version ?? "…"}</footer>
    </div>
  );
}
