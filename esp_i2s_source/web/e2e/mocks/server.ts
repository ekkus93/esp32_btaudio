// 10.12 — Mock device server for Playwright e2e tests
// Provides a local HTTP server that simulates the ESP32 device API.
// Run: node e2e/mocks/server.ts

import http from "node:http";
import { URL } from "node:url";

const deviceState = {
  device: "ESP32-S3 Audio",
  version: "0.1.0",
  uptime_s: 123,
  heap_free: 100000,
  wifi: {
    mode: "STA",
    state: "CONNECTED",
    ssid: "MyNetwork",
    ip: "192.168.1.100",
    rssi: -60,
    ap: {
      on: true,
      enabled: true,
      ssid: "ESP32-Audio",
      secured: true,
      ip: "192.168.4.1",
      clients: 1,
    },
  },
  wroom: { reachable: true, version: "1.0.0" },
  tone: { on: false, hz: 440 },
  radio: {
    playing: false,
    codec: "mp3",
    station: "Radio 1",
    title: "Now Playing",
    url: "http://stream.example.com/radio1",
    bitrate: 128,
    bytes_in: 0,
    ring_used: 0,
    ring_cap: 100000,
    reconnects: 0,
  },
  i2s: { gain: 50 },
};

const stations = [
  { id: 1, name: "Radio 1", url: "http://stream.example.com/radio1" },
  { id: 2, name: "Radio 2", url: "http://stream.example.com/radio2" },
];

const btState = {
  connected: false,
  scanning: false,
  paired: [],
  discovered: [],
};

function handleRequest(req: http.IncomingMessage, res: http.ServerResponse) {
  const url = new URL(req.url ?? "", `http://${req.headers.host}`);
  const method = req.method ?? "GET";

  // Helper to send JSON response
  const send = (status: number, body: unknown) => {
    res.writeHead(status, { "Content-Type": "application/json" });
    res.end(JSON.stringify(body));
  };

  // Helper to read request body
  const readBody = (): Promise<string> => {
    return new Promise((resolve) => {
      const chunks: Buffer[] = [];
      req.on("data", (chunk) => chunks.push(chunk));
      req.on("end", () => resolve(Buffer.concat(chunks).toString()));
    });
  };

  try {
    if (url.pathname === "/api/status" && method === "GET") {
      send(200, deviceState);
    } else if (url.pathname === "/api/bt" && method === "GET") {
      send(200, btState);
    } else if (url.pathname === "/api/stations" && method === "GET") {
      send(200, stations);
    } else if (url.pathname === "/api/console" && method === "POST") {
      const body = await readBody();
      const { cmd } = JSON.parse(body);
      if (cmd === "HELP") {
        send(200, {
          status: "OK",
          result: "DONE",
          data: "",
          lines: ["3 commands available", "VOLUME 0-100 - Set volume", "STATUS - Show status"],
        });
      } else if (cmd === "STATUS") {
        send(200, {
          status: "OK",
          result: "DONE",
          data: "Connected: false",
          lines: [],
        });
      } else {
        send(200, { status: "OK", result: "UNKNOWN_CMD", data: "", lines: [] });
      }
    } else if (url.pathname === "/api/scan" && method === "POST") {
      send(200, { ok: true });
    } else if (url.pathname === "/api/bt" && method === "POST") {
      const body = await readBody();
      const { action, mac } = JSON.parse(body);
      if (action === "pair") {
        btState.paired.push({ mac, name: "Test Device" });
        send(200, { ok: true, result: "PAIRED" });
      } else if (action === "connect") {
        btState.connected = true;
        btState.connected_mac = mac;
        send(200, { ok: true, result: "INITIATED" });
      } else {
        send(200, { ok: true, result: "OK" });
      }
    } else if (url.pathname === "/api/tone" && method === "POST") {
      send(200, { ok: true, on: true, hz: 440 });
    } else if (url.pathname === "/api/tone" && method === "DELETE") {
      send(200, { ok: true, on: false });
    } else {
      send(404, { error: "Not found" });
    }
  } catch (error) {
    send(500, { error: "Internal server error" });
  }
}

// Start the server on a random port
const server = http.createServer(handleRequest);
server.listen(0, () => {
  const address = server.address();
  if (typeof address === "object" && address !== null) {
    console.log(`Mock device server running on http://localhost:${address.port}`);
  }
});
