// Thin fetch layer over the device REST API (SPEC §5.2).

export interface WifiStatus {
  mode: string; // STA | AP
  state?: string; // CONNECTING | CONNECTED
  ssid?: string;
  ip?: string;
  rssi?: number;
}

export interface DeviceStatus {
  device: string;
  version: string;
  uptime_s: number;
  heap_free: number;
  wifi: WifiStatus;
  wroom?: { reachable: boolean; version?: string };
  tone?: { on: boolean; hz: number };
  radio?: RadioStatus;
  i2s?: { gain: number };
}

export interface RadioStatus {
  playing: boolean;
  codec: string;
  station: string;
  title: string;
  url: string;
  bitrate: number;
  bytes_in: number;
  ring_used: number;
  ring_cap: number;
  reconnects: number;
}

async function getJSON<T>(path: string): Promise<T> {
  const r = await fetch(path, { headers: { Accept: "application/json" } });
  if (!r.ok) throw new Error(`${path} → HTTP ${r.status}`);
  return r.json() as Promise<T>;
}

export const getStatus = () => getJSON<DeviceStatus>("/api/status");

export interface ProvisionResult {
  ok: boolean;
  host?: string;
  error?: string;
}

export async function setWifi(ssid: string, pass: string): Promise<ProvisionResult> {
  const r = await fetch("/api/wifi", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ ssid, pass }),
  });
  return r.json() as Promise<ProvisionResult>;
}

export interface ToneState {
  ok: boolean;
  on: boolean;
  hz?: number;
}

export async function setTone(hz: number): Promise<ToneState> {
  const r = await fetch("/api/tone", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ hz }),
  });
  return r.json() as Promise<ToneState>;
}

export async function toneOff(): Promise<ToneState> {
  const r = await fetch("/api/tone", { method: "DELETE" });
  return r.json() as Promise<ToneState>;
}

// Pre-I2S (ESP32-S3) software gain, 0..100 %.
export async function setS3Volume(pct: number): Promise<{ ok: boolean; pct?: number }> {
  const r = await fetch("/api/volume", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ pct }),
  });
  return r.json();
}

// Post-mix (ESP32-WROOM32) A2DP volume, 0..100. -1 = unknown.
export async function getBtVolume(): Promise<{ vol: number }> {
  return getJSON<{ vol: number }>("/api/btvolume");
}

export async function setBtVolume(vol: number): Promise<{ ok: boolean; vol?: number }> {
  const r = await fetch("/api/btvolume", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ vol }),
  });
  return r.json();
}

export async function playRadio(url: string): Promise<{ ok: boolean; error?: string }> {
  const r = await fetch("/api/radio", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ url }),
  });
  return r.json();
}

export async function stopRadio(): Promise<{ ok: boolean }> {
  const r = await fetch("/api/radio", { method: "DELETE" });
  return r.json();
}

export interface Station {
  id: number;
  name: string;
  url: string;
}

export const getStations = () => getJSON<Station[]>("/api/stations");

export async function addStation(name: string, url: string): Promise<{ ok: boolean; id?: number; error?: string }> {
  const r = await fetch("/api/stations", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ name, url }),
  });
  return r.json();
}

export async function updateStation(id: number, name: string, url: string): Promise<{ ok: boolean }> {
  const r = await fetch(`/api/stations?id=${id}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ name, url }),
  });
  return r.json();
}

export async function deleteStation(id: number): Promise<{ ok: boolean }> {
  const r = await fetch(`/api/stations?id=${id}`, { method: "DELETE" });
  return r.json();
}
