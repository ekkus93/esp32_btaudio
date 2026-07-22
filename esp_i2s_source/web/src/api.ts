// Thin fetch layer over the device REST API (SPEC §5.2).

export interface ApStatus {
  on: boolean;          // AP currently broadcasting
  enabled: boolean;     // user setting (keep AP up alongside STA)
  ssid: string;
  secured: boolean;     // true when the AP has a password set
  ip?: string;
  clients?: number;
}

export interface WifiStatus {
  mode: string; // STA | AP
  state?: string; // CONNECTING | CONNECTED
  ssid?: string;
  ip?: string;
  rssi?: number;
  ap?: ApStatus;
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
  prebuffer_ms?: number;
}

/** Structured API error thrown by apiRequest(). */
export class ApiError extends Error {
  public readonly retryable: boolean;

  constructor(
    message: string,
    public readonly status: number,
    public readonly code: string,
    retryable = false,
  ) {
    super(message);
    this.name = "ApiError";
    this.retryable = retryable;
  }
}

type ApiEnvelope<T> =
  | { ok: true; data: T }
  | { ok: false; error: { code: string; message: string; retryable?: boolean } };

// -----------------------------------------------------------------------
// FIX3 11.2: auth token storage. Session-only by default (cleared when the
// tab closes) — "remember on this browser" is an explicit opt-in that
// additionally mirrors into localStorage.
const TOKEN_KEY = "esp_i2s_auth_token";
const TOKEN_RE = /^[0-9a-f]{64}$/;

export function getAuthToken(): string {
  return sessionStorage.getItem(TOKEN_KEY) ?? localStorage.getItem(TOKEN_KEY) ?? "";
}

/** Exact 64-lowercase-hex validation — throws if malformed. */
export function setAuthToken(token: string, remember = false): void {
  if (!TOKEN_RE.test(token)) {
    throw new Error("Token must be exactly 64 lowercase hexadecimal characters");
  }
  sessionStorage.setItem(TOKEN_KEY, token);
  if (remember) {
    localStorage.setItem(TOKEN_KEY, token);
  } else {
    localStorage.removeItem(TOKEN_KEY);
  }
}

export function clearAuthToken(): void {
  sessionStorage.removeItem(TOKEN_KEY);
  localStorage.removeItem(TOKEN_KEY);
}

// FIX3 11.3: centralized "the UI needs a (new) token" signal — apiRequest()
// fires this on a missing/rejected token so a single auth panel (mounted
// once in App) can react, instead of every caller re-implementing it.
type AuthListener = () => void;
const authListeners = new Set<AuthListener>();
export function onAuthRequired(cb: AuthListener): () => void {
  authListeners.add(cb);
  return () => authListeners.delete(cb);
}
function notifyAuthRequired(): void {
  for (const cb of authListeners) cb();
}

const MUTATING_METHODS = new Set(["POST", "PUT", "DELETE", "PATCH"]);

/** Centralised fetch wrapper — handles timeout, abort, auth, structured envelope. */
export async function apiRequest<T>(
  path: string,
  init: RequestInit = {},
  timeoutMs = 10_000,
): Promise<T> {
  const method = (init.method ?? "GET").toUpperCase();
  const mutating = MUTATING_METHODS.has(method);

  // 11.1/11.4: a mutation without a token never reaches the network — fail
  // closed before fetch(), not after a round trip.
  let authHeader: Record<string, string> = {};
  if (mutating) {
    const token = getAuthToken();
    if (!token) {
      notifyAuthRequired();
      throw new ApiError("Enter the device token", 401, "AUTH_REQUIRED", false);
    }
    authHeader = { Authorization: `Bearer ${token}` };
  }

  const controller = new AbortController();
  const timerId = window.setTimeout(() => controller.abort(), timeoutMs);

  try {
    const response = await fetch(path, {
      ...init,
      signal: init.signal ?? controller.signal,
      headers: {
        Accept: "application/json",
        ...(init.body ? { "Content-Type": "application/json" } : {}),
        ...authHeader,
        ...init.headers,
      },
    });

    const contentType = response.headers.get("content-type") ?? "";
    if (!contentType.includes("application/json")) {
      throw new ApiError(
        `${path} returned non-JSON content`,
        response.status,
        "NON_JSON_RESPONSE",
        response.status >= 500,
      );
    }

    const payload = (await response.json()) as ApiEnvelope<T>;
    if (!response.ok || !payload.ok) {
      const error = !payload.ok
        ? payload.error
        : { code: "HTTP_ERROR", message: `HTTP ${response.status}`, retryable: true };
      if (response.status === 401) {
        notifyAuthRequired();
      }
      throw new ApiError(
        error.message,
        response.status,
        error.code,
        error.retryable ?? false,
      );
    }
    return payload.data;
  } finally {
    window.clearTimeout(timerId);
  }
}

// -----------------------------------------------------------------------
// API helpers — all wrapped through apiRequest()

/** Radio jitter-cushion prebuffer depth (ms), NVS-persisted; clamped device-side. */
export async function setPrebuffer(ms: number): Promise<{ ms?: number }> {
  return apiRequest<{ ms?: number }>("/api/prebuffer", {
    method: "POST",
    body: JSON.stringify({ ms }),
  });
}

export const getStatus = () =>
  apiRequest<DeviceStatus>("/api/status", { method: "GET" });

export interface ProvisionResult {
  ok: boolean;
  host?: string;
  error?: string;
}

// Start a Bluetooth scan: the device suspends A2DP for a clean inquiry, then
// restores. Discovered devices appear in getBt().discovered.
export async function triggerScan(): Promise<{ ok: boolean; error?: string }> {
  return apiRequest<{ ok: boolean; error?: string }>(
    "/api/scan",
    { method: "POST" },
  );
}

export interface BtDev {
  mac: string;
  name: string;
}
export interface BtState {
  connected: boolean;
  connected_mac?: string; // MAC of the currently-connected A2DP sink, if any
  scanning: boolean;
  prompt?: string; // pairing confirm prompt, if any
  paired: BtDev[];
  discovered: BtDev[];
}
export const getBt = () => apiRequest<BtState>("/api/bt", { method: "GET" });

// Bluetooth actions: connect | disconnect | pair | unpair | pin_accept |
// pin_reject | refresh.
export async function btAction(
  action: string,
  mac?: string,
): Promise<{ ok: boolean; result?: string }> {
  return apiRequest<{ ok: boolean; result?: string }>("/api/bt", {
    method: "POST",
    body: JSON.stringify({ action, mac }),
  });
}

// Run a raw WROOM32 command and get its response (replaces the WS terminal).
export async function consoleCmd(
  cmd: string,
): Promise<{ status: string; result: string; data: string; lines?: string[] }> {
  return apiRequest<{ status: string; result: string; data: string; lines?: string[] }>(
    "/api/console",
    { method: "POST", body: JSON.stringify({ cmd }) },
  );
}

// Toggle the concurrent control AP (keep it up alongside STA, or STA-only).
export async function setApEnabled(
  enabled: boolean,
): Promise<{ enabled?: boolean }> {
  return apiRequest<{ enabled?: boolean }>("/api/apmode", {
    method: "POST",
    body: JSON.stringify({ enabled }),
  });
}

// Change the control-AP name/password. pass "" = open AP; else 8-64 chars.
export async function setApConfig(
  ssid: string,
  pass: string,
): Promise<{ ok: boolean; error?: string }> {
  return apiRequest<{ ok: boolean; error?: string }>("/api/apmode", {
    method: "POST",
    body: JSON.stringify({ ssid, pass }),
  });
}

export async function setWifi(
  ssid: string,
  pass: string,
): Promise<ProvisionResult> {
  return apiRequest<ProvisionResult>("/api/wifi", {
    method: "POST",
    body: JSON.stringify({ ssid, pass }),
  });
}

export interface ToneState {
  ok: boolean;
  on: boolean;
  hz?: number;
}

// amp (0..100 %) optional; voice "sine" (default) or "piano" (harmonics + decay).
export async function setTone(
  hz: number,
  amp?: number,
  voice?: "sine" | "piano",
): Promise<ToneState> {
  const body: Record<string, unknown> = { hz };
  if (amp != null) body.amp = amp;
  if (voice) body.voice = voice;
  return apiRequest<ToneState>("/api/tone", {
    method: "POST",
    body: JSON.stringify(body),
  });
}

export async function toneOff(): Promise<ToneState> {
  return apiRequest<ToneState>("/api/tone", { method: "DELETE" });
}

// Pre-I2S (ESP32-S3) software gain, 0..100 %.
export async function setS3Volume(
  pct: number,
): Promise<{ pct?: number }> {
  return apiRequest<{ pct?: number }>("/api/volume", {
    method: "POST",
    body: JSON.stringify({ pct }),
  });
}

// Post-mix (ESP32-WROOM32) A2DP volume, 0..100. -1 = unknown.
export async function getBtVolume(): Promise<{ vol: number }> {
  return apiRequest<{ vol: number }>("/api/btvolume", { method: "GET" });
}

export async function setBtVolume(
  vol: number,
): Promise<{ ok: boolean; vol?: number }> {
  return apiRequest<{ ok: boolean; vol?: number }>("/api/btvolume", {
    method: "POST",
    body: JSON.stringify({ vol }),
  });
}

export async function playRadio(
  url: string,
): Promise<{ ok: boolean; error?: string }> {
  return apiRequest<{ ok: boolean; error?: string }>("/api/radio", {
    method: "POST",
    body: JSON.stringify({ url }),
  });
}

export async function stopRadio(): Promise<{ ok: boolean }> {
  return apiRequest<{ ok: boolean }>("/api/radio", { method: "DELETE" });
}

export interface Station {
  id: number;
  name: string;
  url: string;
}

export const getStations = () =>
  apiRequest<Station[]>("/api/stations", { method: "GET" });

export async function addStation(
  name: string,
  url: string,
): Promise<{ ok: boolean; id?: number; error?: string }> {
  return apiRequest<{ ok: boolean; id?: number; error?: string }>(
    "/api/stations",
    { method: "POST", body: JSON.stringify({ name, url }) },
  );
}

export async function updateStation(
  id: number,
  name: string,
  url: string,
): Promise<{ ok: boolean }> {
  return apiRequest<{ ok: boolean }>(`/api/stations?id=${id}`, {
    method: "PUT",
    body: JSON.stringify({ name, url }),
  });
}

export async function deleteStation(id: number): Promise<{ ok: boolean }> {
  return apiRequest<{ ok: boolean }>(`/api/stations?id=${id}`, {
    method: "DELETE",
  });
}

// Reorder a station by swapping it with its neighbour (up = earlier, down = later).
export async function moveStation(
  id: number,
  dir: "up" | "down",
): Promise<{ ok: boolean }> {
  return apiRequest<{ ok: boolean }>(`/api/stations?id=${id}&move=${dir}`, {
    method: "PUT",
  });
}
