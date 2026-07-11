import { useEffect, useState } from "react";
import { getStatus, type DeviceStatus } from "./api";

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

        <Placeholder title="Terminal" task="WEB-1c" />
        <Placeholder title="Tone" task="WEB-1d" />
        <Placeholder title="Radio" task="RADIO-1" />
        <Placeholder title="Bluetooth" task="BTUI-1" />
      </div>

      <footer className="muted">esp-i2s-source · {status?.version ?? "…"}</footer>
    </div>
  );
}
