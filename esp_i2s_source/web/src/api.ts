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

/** Centralised fetch wrapper — handles timeout, abort, auth, structured envelope. */
export async function apiRequest<T>(
  path: string,
  init: RequestInit = {},
  timeoutMs = 10_000,
): Promise<T> {
  // Device-token auth is disabled server-side (route_dispatch() no longer
  // checks it); still attach it if the user happens to have one set (e.g.
  // via AUTH ROTATE), but never block a request on its absence.
  const token = getAuthToken();
  const authHeader: Record<string, string> = token ? { Authorization: `Bearer ${token}` } : {};

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

    // The device API is NOT uniformly enveloped: mutating routes reply
    // {ok, ...} or {ok:false, error:{code,message,retryable}|"text"}, but
    // plain data endpoints (/api/status, /api/bt, /api/console, ...) return
    // bare objects with no `ok` field at all. Only treat a response as a
    // failure envelope when `ok` is explicitly false; only unwrap `.data`
    // when it actually exists. (Assuming the envelope everywhere crashed on
    // /api/status with "Cannot read properties of undefined".)
    const payload = (await response.json()) as Record<string, unknown> | null;
    const envelopeFailed =
      payload !== null && typeof payload === "object" && payload.ok === false;

    if (!response.ok || envelopeFailed) {
      let code = "HTTP_ERROR";
      let message = `HTTP ${response.status}`;
      let retryable = response.status >= 500;
      if (envelopeFailed) {
        const e = (payload as { error?: unknown }).error;
        if (typeof e === "string") {
          code = "ERROR";
          message = e;
          retryable = false;
        } else if (e && typeof e === "object") {
          const eo = e as { code?: string; message?: string; retryable?: boolean };
          code = eo.code ?? "ERROR";
          message = eo.message ?? "request failed";
          retryable = eo.retryable ?? false;
        }
      }
      if (response.status === 401 || code === "AUTH_REQUIRED") {
        notifyAuthRequired();
      }
      throw new ApiError(message, response.status, code, retryable);
    }

    if (payload !== null && typeof payload === "object" &&
        payload.ok === true && "data" in payload) {
      return (payload as { data: T }).data;
    }
    return payload as T;
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

// ---------------------------------------------------------------------------
// Station backup: export the list to a JSON file and merge-import it back.
// Pure functions (no fetch) so the export shape, parsing tolerance, and
// dedup logic are unit-testable independently of the DOM/network.

export interface StationsExport {
  format: "esp-i2s-source/stations";
  version: 1;
  exported_at: string;
  stations: { name: string; url: string }[];
}

/** Build the downloadable envelope (name+url only; ids are device-internal). */
export function buildStationsExport(
  stations: Station[],
  now: string = new Date().toISOString(),
): StationsExport {
  return {
    format: "esp-i2s-source/stations",
    version: 1,
    exported_at: now,
    stations: stations.map((s) => ({ name: s.name, url: s.url })),
  };
}

/** Parse an uploaded file. Accepts our export envelope OR a bare array of
 *  {name,url}. Entries without a usable url string are dropped. Throws on
 *  malformed JSON or a shape with no stations. */
export function parseStationsImport(text: string): { name: string; url: string }[] {
  let data: unknown;
  try {
    data = JSON.parse(text);
  } catch {
    throw new Error("file is not valid JSON");
  }
  const arr: unknown = Array.isArray(data)
    ? data
    : data && typeof data === "object" && Array.isArray((data as StationsExport).stations)
      ? (data as StationsExport).stations
      : null;
  if (!Array.isArray(arr)) throw new Error("no stations found in file");
  const out: { name: string; url: string }[] = [];
  for (const item of arr) {
    if (!item || typeof item !== "object") continue;
    const rawUrl = (item as { url?: unknown }).url;
    if (typeof rawUrl !== "string" || !rawUrl.trim()) continue;
    const rawName = (item as { name?: unknown }).name;
    out.push({
      url: rawUrl.trim(),
      name: typeof rawName === "string" ? rawName.trim() : "",
    });
  }
  if (out.length === 0) throw new Error("no valid stations in file");
  return out;
}

/** Merge plan: split imported stations into the ones to add vs. duplicates,
 *  deduping by exact URL against the current list AND within the file itself
 *  (first occurrence wins). Matches the device's own exact-URL dedup. */
export function planStationMerge(
  imported: { name: string; url: string }[],
  existing: Station[],
): { toAdd: { name: string; url: string }[]; duplicates: number } {
  const seen = new Set(existing.map((s) => s.url));
  const toAdd: { name: string; url: string }[] = [];
  let duplicates = 0;
  for (const s of imported) {
    if (seen.has(s.url)) {
      duplicates++;
      continue;
    }
    seen.add(s.url);
    toAdd.push(s);
  }
  return { toAdd, duplicates };
}
