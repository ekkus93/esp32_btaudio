# ESP I2S Source — Functional Specification

**Document Version:** 1.0  
**Date:** February 6, 2026  
**Target Platform:** ESP32-S3 (512 KB SRAM, optional PSRAM)  
**ESP-IDF Version:** v5.4+  
**Status:** Implementation Ready

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Component Specifications](#2-component-specifications)
3. [Sequence Diagrams](#3-sequence-diagrams)
4. [State Machines](#4-state-machines)
5. [API Specifications](#5-api-specifications)
6. [Memory Layout and Budgets](#6-memory-layout-and-budgets)
7. [Error Handling](#7-error-handling)
8. [Configuration Management](#8-configuration-management)
9. [Implementation Guidelines](#9-implementation-guidelines)
10. [Testing Specifications](#10-testing-specifications)

---

## 1. Architecture Overview

### 1.1. System Context

```
┌─────────────────────────────────────────────────────────────────┐
│                        esp_i2s_source                            │
│                       (ESP32-S3 WiFi)                            │
│                                                                   │
│  ┌─────────────┐    ┌──────────────┐    ┌──────────────┐       │
│  │   Web UI    │───▶│ HTTP Server  │───▶│  WiFi Stack  │       │
│  │  (Browser)  │    │   (httpd)    │    │  (AP/STA)    │       │
│  └─────────────┘    └──────────────┘    └──────────────┘       │
│                            │                                     │
│                            ▼                                     │
│  ┌─────────────────────────────────────────────────────┐       │
│  │         Command Router & State Manager              │       │
│  └─────────────────────────────────────────────────────┘       │
│         │                    │                    │             │
│         ▼                    ▼                    ▼             │
│  ┌──────────┐       ┌──────────────┐      ┌──────────┐        │
│  │   UART   │       │ Radio Player │      │   Tone   │        │
│  │ Manager  │       │  (ESP-ADF)   │      │Generator │        │
│  └──────────┘       └──────────────┘      └──────────┘        │
│         │                    │                    │             │
│         │                    └─────────┬──────────┘             │
│         │                              ▼                        │
│         │                      ┌──────────────┐                │
│         │                      │ I2S Manager  │                │
│         │                      │   (Master)   │                │
│         │                      └──────────────┘                │
│         │                              │                        │
└─────────┼──────────────────────────────┼────────────────────────┘
          │ GPIO16/17                    │ GPIO18/23/19
          │ UART                         │ I2S (BCLK/WS/DOUT)
          ▼                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                   esp_bt_audio_source                            │
│                   (ESP32 WROOM32 BT)                             │
│                                                                   │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │ UART Handler │───▶│ I2S Manager  │───▶│  Bluetooth   │      │
│  │              │    │   (Slave)    │    │  A2DP Sink   │      │
│  └──────────────┘    └──────────────┘    └──────────────┘      │
│                                                  │               │
└──────────────────────────────────────────────────┼───────────────┘
                                                   │ Bluetooth
                                                   ▼
                                          [BT Speaker/Headphones]
```

### 1.2. Key Design Principles

1. **Separation of Concerns:** Clear boundaries between WiFi, UART, audio source, and I2S components
2. **Event-Driven Architecture:** Use ESP-IDF event loops for asynchronous operations
3. **Fail-Safe Operation:** Always maintain recovery path to AP mode with default credentials
4. **Resource Efficiency:** Target 30% CPU, 70+ KB free heap during internet radio streaming
5. **Test-Driven Development:** Follow Red-Green-Refactor cycle with Unity tests

### 1.3. Component Ownership

| Component | Responsibilities | Public APIs | Dependencies |
|-----------|-----------------|-------------|--------------|
| **WiFi Manager** | AP/STA mode switching, credential storage, auto-revert | `wifi_init()`, `wifi_set_mode()`, `wifi_get_status()` | NVS, esp_wifi, esp_event |
| **HTTP Server** | Web UI hosting, REST API endpoints, authentication | `webserver_init()`, `webserver_stop()` | httpd, WiFi Manager |
| **UART Manager** | Command TX/RX, response parsing, event handling | `uart_send_cmd()`, `uart_get_response()` | UART driver, FreeRTOS queues |
| **Radio Player** | HTTP streaming, MP3 decode, buffering | `radio_start()`, `radio_stop()`, `radio_set_url()` | ESP-ADF, http_stream, mp3_decoder |
| **Tone Generator** | Sine/square wave generation | `tone_start()`, `tone_stop()`, `tone_set_freq()` | Math library |
| **I2S Manager** | Master clock generation, DMA TX, format control | `i2s_init()`, `i2s_write_samples()` | I2S driver, DMA |
| **NVS Manager** | Persistent storage, schema migration | `nvs_load_config()`, `nvs_save_config()` | NVS driver |
| **State Manager** | Global state coordination, mode switching | `state_set_audio_source()`, `state_get()` | All components |

---

## 2. Component Specifications

### 2.1. WiFi Manager Component

**Header:** `components/wifi_manager/include/wifi_manager.h`

**Configuration Structure:**
```c
typedef enum {
    WIFI_MODE_AP = 0,
    WIFI_MODE_STA = 1
} wifi_mode_t;

typedef struct {
    wifi_mode_t mode;
    char ap_ssid[32];           // Default: "ESP_I2S_SOURCE"
    char ap_password[64];       // Default: "esp32audio"
    char sta_ssid[32];
    char sta_password[64];
    uint8_t ap_channel;         // Default: 1
    uint8_t ap_max_connections; // Default: 4
    uint32_t sta_timeout_ms;    // Default: 30000 (30 seconds)
} wifi_config_t;

typedef struct {
    wifi_mode_t current_mode;
    bool connected;
    char ip_address[16];
    int8_t rssi;                // Only for STA mode
    uint8_t connected_stations; // Only for AP mode
} wifi_status_t;
```

**Public API:**
```c
// Initialize WiFi subsystem (loads config from NVS)
esp_err_t wifi_manager_init(void);

// Start WiFi in configured mode
esp_err_t wifi_manager_start(void);

// Stop WiFi (for mode switching)
esp_err_t wifi_manager_stop(void);

// Switch mode (stops, saves to NVS, restarts)
esp_err_t wifi_manager_set_mode(wifi_mode_t mode);

// Update STA credentials (saves to NVS)
esp_err_t wifi_manager_set_sta_credentials(const char *ssid, const char *password);

// Get current status
esp_err_t wifi_manager_get_status(wifi_status_t *status);

// Factory reset (clear credentials, revert to AP)
esp_err_t wifi_manager_factory_reset(void);
```

**Event Callbacks:**
```c
typedef void (*wifi_event_cb_t)(wifi_event_t event, void *data);

typedef enum {
    WIFI_EVENT_AP_STARTED,
    WIFI_EVENT_AP_STOPPED,
    WIFI_EVENT_STA_CONNECTED,
    WIFI_EVENT_STA_DISCONNECTED,
    WIFI_EVENT_STA_GOT_IP,
    WIFI_EVENT_STA_TIMEOUT,      // Auto-revert to AP
    WIFI_EVENT_STA_STATION_JOINED,
    WIFI_EVENT_STA_STATION_LEFT
} wifi_event_t;

// Register callback for WiFi events
esp_err_t wifi_manager_register_callback(wifi_event_cb_t callback);
```

**Implementation Notes:**
- Use ESP-IDF `esp_wifi` and `esp_event` components
- STA join timeout triggers auto-revert to AP mode (30 seconds default)
- Preserve STA credentials in NVS even after revert (for manual retry)
- AP mode always uses WPA2-PSK authentication
- Single event loop for all WiFi events

---

### 2.2. HTTP Server Component

**Header:** `components/webserver/include/webserver.h`

**Configuration:**
```c
typedef struct {
    uint16_t port;              // Default: 80
    uint8_t max_connections;    // Default: 4 (1 active user + 3 keepalive)
    uint32_t session_timeout_s; // Default: 3600 (1 hour)
    bool require_auth;          // Default: true
} webserver_config_t;

typedef struct {
    char username[32];
    char password_hash[65];     // SHA256 hex string (64 chars + null)
    bool first_login;           // Force password change
    uint32_t session_token;     // Simple session token
    time_t session_expiry;
} webserver_auth_t;
```

**Public API:**
```c
// Initialize HTTP server (does not start)
esp_err_t webserver_init(const webserver_config_t *config);

// Start server (after WiFi ready)
esp_err_t webserver_start(void);

// Stop server
esp_err_t webserver_stop(void);

// Check authentication
bool webserver_is_authenticated(httpd_req_t *req);

// Set password (hash internally)
esp_err_t webserver_set_password(const char *new_password);
```

**REST Endpoint Handlers:**
```c
// Status and Info
esp_err_t handler_get_status(httpd_req_t *req);

// WiFi Configuration
esp_err_t handler_post_wifi_mode(httpd_req_t *req);
esp_err_t handler_post_wifi_sta_config(httpd_req_t *req);
esp_err_t handler_post_wifi_reset(httpd_req_t *req);

// Internet Radio
esp_err_t handler_post_radio_url(httpd_req_t *req);

// Bluetooth Control (relay to esp_bt_audio_source)
esp_err_t handler_post_bt_command(httpd_req_t *req);
esp_err_t handler_get_bt_status(httpd_req_t *req);

// Audio Control
esp_err_t handler_post_audio_volume(httpd_req_t *req);

// Factory Reset
esp_err_t handler_post_factory_reset(httpd_req_t *req);

// Authentication
esp_err_t handler_post_login(httpd_req_t *req);
esp_err_t handler_post_change_password(httpd_req_t *req);
esp_err_t handler_post_logout(httpd_req_t *req);
```

**Implementation Notes:**
- Use ESP-IDF `esp_http_server` component
- SHA256 hash password with salt (use `mbedtls_sha256`)
- Session token stored in HTTP cookie
- Single-user policy: HTTP 503 if concurrent sessions detected
- First login forces redirect to `/change_password` (cannot skip)
- All `/api/*` endpoints require authentication except `/api/status` (read-only)

---

### 2.3. UART Manager Component

**Header:** `components/uart_manager/include/uart_manager.h`

**Configuration:**
```c
typedef struct {
    uart_port_t port;           // Default: UART_NUM_1
    int tx_pin;                 // Default: GPIO16
    int rx_pin;                 // Default: GPIO17
    uint32_t baud_rate;         // Default: 115200
    uint32_t timeout_ms;        // Default: 5000
    uint8_t max_retries;        // Default: 1
} uart_config_t;

typedef enum {
    UART_RESPONSE_OK,
    UART_RESPONSE_ERROR,
    UART_RESPONSE_TIMEOUT,
    UART_RESPONSE_EVENT
} uart_response_type_t;

typedef struct {
    uart_response_type_t type;
    char command[64];           // Echo of command sent
    char result[256];           // Response data
    char error_code[32];        // Error code if type == ERROR
    char error_message[128];    // Human-readable error
} uart_response_t;
```

**Public API:**
```c
// Initialize UART (pins, baud, buffer)
esp_err_t uart_manager_init(const uart_config_t *config);

// Send command and wait for response (blocking with timeout)
esp_err_t uart_manager_send_command(const char *cmd, uart_response_t *response);

// Send command without waiting for response (fire-and-forget)
esp_err_t uart_manager_send_async(const char *cmd);

// Register callback for asynchronous EVENT| lines
typedef void (*uart_event_cb_t)(const char *event_type, const char *event_data);
esp_err_t uart_manager_register_event_callback(uart_event_cb_t callback);
```

**Implementation Notes:**
- Use ESP-IDF `driver/uart.h` component
- Dedicated FreeRTOS task for RX line parsing
- Line-buffered parsing: accumulate until `\n`, then tokenize on `|`
- Response matching: track pending command, match `OK|CMD` or `ERR|CMD`
- Event queue: `EVENT|...` lines bypass command/response flow
- Timeout handling: retry once if no response in 5 seconds (configurable)
- Thread-safe: use mutex for command sending (prevent interleaved commands)

---

### 2.4. Radio Player Component (ESP-ADF)

**Header:** `components/radio_player/include/radio_player.h`

**Configuration:**
```c
typedef struct {
    char url[256];
    uint32_t buffer_size;       // Default: 65536 (64 KB)
    uint8_t task_priority;      // Default: 5
    uint16_t task_stack_size;   // Default: 4096
} radio_config_t;

typedef enum {
    RADIO_STATE_IDLE,
    RADIO_STATE_CONNECTING,
    RADIO_STATE_BUFFERING,
    RADIO_STATE_PLAYING,
    RADIO_STATE_ERROR
} radio_state_t;

typedef struct {
    radio_state_t state;
    char current_url[256];
    uint32_t bytes_buffered;
    uint32_t buffer_fill_pct;   // 0-100
    uint32_t reconnect_attempts;
    char last_error[128];
} radio_status_t;
```

**Public API:**
```c
// Initialize radio player (create pipeline, do not start)
esp_err_t radio_player_init(void);

// Set stream URL (validates format, saves to NVS)
esp_err_t radio_player_set_url(const char *url);

// Start playback (connect, decode, stream to I2S)
esp_err_t radio_player_start(void);

// Stop playback (tear down pipeline)
esp_err_t radio_player_stop(void);

// Get current status
esp_err_t radio_player_get_status(radio_status_t *status);

// Register callback for state changes
typedef void (*radio_event_cb_t)(radio_state_t new_state, const char *message);
esp_err_t radio_player_register_callback(radio_event_cb_t callback);
```

**ESP-ADF Pipeline Configuration:**
```c
// Pipeline: http_stream → mp3_decoder → i2s_stream_writer

audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
audio_pipeline_handle_t pipeline = audio_pipeline_init(&pipeline_cfg);

// HTTP stream element
http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
http_cfg.event_handle = http_stream_event_handle;
http_cfg.type = AUDIO_STREAM_READER;
http_cfg.enable_playlist_parser = false;
audio_element_handle_t http_stream = http_stream_init(&http_cfg);

// MP3 decoder element
mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
audio_element_handle_t mp3_decoder = mp3_decoder_init(&mp3_cfg);

// I2S stream writer element
i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
i2s_cfg.type = AUDIO_STREAM_WRITER;
i2s_cfg.i2s_config.mode = I2S_MODE_MASTER | I2S_MODE_TX;
i2s_cfg.i2s_config.sample_rate = 48000;
i2s_cfg.i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
i2s_cfg.i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
audio_element_handle_t i2s_writer = i2s_stream_init(&i2s_cfg);

// Link pipeline
audio_pipeline_register(pipeline, http_stream, "http");
audio_pipeline_register(pipeline, mp3_decoder, "mp3");
audio_pipeline_register(pipeline, i2s_writer, "i2s");
audio_pipeline_link(pipeline, (const char *[]){"http", "mp3", "i2s"}, 3);
```

**Implementation Notes:**
- Use ESP-ADF v2.6+ audio pipeline
- Buffer size 64 KB circular buffer (configurable 32-128 KB)
- Exponential backoff retry: 1s, 2s, 4s (max 3 attempts)
- Mute I2S output during reconnection attempts (write silence)
- Error classification:
  - **Recoverable:** HTTP timeout, network glitch → retry
  - **Non-recoverable:** HTTP 404, 403, bad URL → fail immediately
- Event callbacks for state changes (connecting → buffering → playing → error)

---

### 2.5. Tone Generator Component

**Header:** `components/tone_generator/include/tone_generator.h`

**Configuration:**
```c
typedef enum {
    TONE_WAVEFORM_SINE,
    TONE_WAVEFORM_SQUARE,
    TONE_WAVEFORM_SAWTOOTH
} tone_waveform_t;

typedef struct {
    uint16_t frequency_hz;      // Default: 1000 Hz
    uint8_t gain_pct;           // 0-100, default: 50
    tone_waveform_t waveform;   // Default: SINE
    uint32_t sample_rate;       // Default: 48000
} tone_config_t;
```

**Public API:**
```c
// Initialize tone generator
esp_err_t tone_generator_init(void);

// Start tone generation (feeds I2S continuously)
esp_err_t tone_generator_start(const tone_config_t *config);

// Stop tone generation
esp_err_t tone_generator_stop(void);

// Update frequency on-the-fly
esp_err_t tone_generator_set_frequency(uint16_t frequency_hz);

// Update gain on-the-fly
esp_err_t tone_generator_set_gain(uint8_t gain_pct);
```

**Implementation Notes:**
- Dedicated FreeRTOS task (priority 4, stack 3072 bytes)
- Generate samples in 512-sample chunks (10.67 ms @ 48 kHz)
- Sine wave: use lookup table (1024 samples, linearly interpolated)
- Square wave: direct generation (50% duty cycle)
- Gain scaling: multiply samples by `gain_pct / 100.0`
- Feed I2S manager via `i2s_write_samples()` API

---

### 2.6. I2S Manager Component

**Header:** `components/i2s_manager/include/i2s_manager.h`

**Configuration:**
```c
typedef struct {
    i2s_port_t port;            // Default: I2S_NUM_0
    int bclk_pin;               // Default: GPIO18
    int ws_pin;                 // Default: GPIO23
    int dout_pin;               // Default: GPIO19
    uint32_t sample_rate;       // Default: 48000
    i2s_bits_per_sample_t bits; // Default: I2S_BITS_PER_SAMPLE_16BIT
    i2s_channel_fmt_t channels; // Default: I2S_CHANNEL_FMT_RIGHT_LEFT
    size_t dma_buf_count;       // Default: 8
    size_t dma_buf_len;         // Default: 512 samples
} i2s_config_t;
```

**Public API:**
```c
// Initialize I2S as master transmitter
esp_err_t i2s_manager_init(const i2s_config_t *config);

// Start I2S clock generation
esp_err_t i2s_manager_start(void);

// Stop I2S
esp_err_t i2s_manager_stop(void);

// Write PCM samples (blocking with timeout)
esp_err_t i2s_manager_write_samples(const int16_t *samples, size_t count, uint32_t timeout_ms);

// Get statistics (underruns, overruns)
typedef struct {
    uint32_t underruns;
    uint32_t total_samples_written;
} i2s_stats_t;
esp_err_t i2s_manager_get_stats(i2s_stats_t *stats);
```

**Implementation Notes:**
- Use ESP-IDF `driver/i2s_std.h` (I2S standard mode driver)
- Master mode: generate BCLK and WS clocks
- DMA buffer: 8 buffers × 512 samples = 8 KB total (configurable)
- Underrun handling: if no data available, write silence (zero-fill)
- Track underrun counter for telemetry

---

### 2.7. NVS Manager Component

**Header:** `components/nvs_manager/include/nvs_manager.h`

**Schema:**
```c
#define NVS_NAMESPACE "esp_i2s_src"
#define NVS_SCHEMA_VERSION 1

typedef struct {
    // WiFi
    uint8_t wifi_mode;          // 0=AP, 1=STA
    char sta_ssid[32];
    char sta_pass[64];
    
    // Web UI
    char web_password[65];      // SHA256 hash
    
    // Internet Radio
    char radio_url[256];
    
    // Audio
    uint8_t audio_source;       // 0=tone, 1=radio
    uint16_t tone_freq;
    uint8_t tone_gain;
    uint8_t last_volume;
    
    // System
    uint32_t boot_count;
    uint8_t nvs_schema_ver;
} nvs_config_t;
```

**Public API:**
```c
// Initialize NVS partition
esp_err_t nvs_manager_init(void);

// Load configuration (with schema migration)
esp_err_t nvs_manager_load_config(nvs_config_t *config);

// Save full configuration
esp_err_t nvs_manager_save_config(const nvs_config_t *config);

// Save individual key (for frequent updates like volume)
esp_err_t nvs_manager_save_wifi_mode(uint8_t mode);
esp_err_t nvs_manager_save_sta_credentials(const char *ssid, const char *pass);
esp_err_t nvs_manager_save_radio_url(const char *url);
esp_err_t nvs_manager_save_web_password(const char *hash);

// Factory reset (erase namespace)
esp_err_t nvs_manager_factory_reset(void);
```

**Schema Migration:**
```c
esp_err_t nvs_migrate_schema(nvs_config_t *config) {
    uint8_t stored_version = 0;
    nvs_get_u8(nvs_handle, "nvs_schema_ver", &stored_version);
    
    if (stored_version == 0) {
        // Migration from v0 (no schema) to v1
        // Set defaults for new keys
        config->nvs_schema_ver = 1;
        nvs_manager_save_config(config);
    }
    // Future migrations: v1 → v2, etc.
    
    return ESP_OK;
}
```

---

## 3. Sequence Diagrams

### 3.1. Boot Sequence

```
User            WiFi Mgr    NVS Mgr     HTTP Svr    UART Mgr    State Mgr   I2S Mgr
 │                │           │            │           │           │           │
 │  [Power On]    │           │            │           │           │           │
 ├───────────────▶│           │            │           │           │           │
 │                │ load cfg  │            │           │           │           │
 │                ├──────────▶│            │           │           │           │
 │                │◀──────────┤            │           │           │           │
 │                │ (wifi_mode=AP, ssid/pass)          │           │           │
 │                │           │            │           │           │           │
 │                │ start AP  │            │           │           │           │
 │                │───────────┼───────────▶│           │           │           │
 │                │           │  AP ready  │           │           │           │
 │                │           │◀───────────┤           │           │           │
 │                │           │            │           │           │           │
 │                │           │ start httpd│           │           │           │
 │                │           ├───────────▶│           │           │           │
 │                │           │            │ listen:80 │           │           │
 │                │           │            │           │           │           │
 │                │           │            │ init UART │           │           │
 │                │           │            ├──────────▶│           │           │
 │                │           │            │           │ TX/RX task│           │
 │                │           │            │           │           │           │
 │                │           │            │           │ STATUS?   │           │
 │                │           │            │           ├──────────▶│           │
 │                │           │            │           │◀──────────┤           │
 │                │           │            │           │ OK|STATUS │           │
 │                │           │            │           │           │           │
 │                │           │            │           │ init I2S  │           │
 │                │           │            │           ├──────────▶│           │
 │                │           │            │           │           │ start clk │
 │                │           │            │           │           │           │
 │                │           │            │           │ set source=tone       │
 │                │           │            │           ├──────────▶│           │
 │                │           │            │           │           │ start tone│
 │                │           │            │           │           │           │
 │  [Ready]       │           │            │           │           │           │
 │◀───────────────┴───────────┴────────────┴───────────┴───────────┴───────────┘
```

**Boot Timeline (Typical):**
- T+0ms: Power on, ESP-IDF bootloader
- T+500ms: `app_main()` starts
- T+600ms: NVS initialized, config loaded
- T+800ms: WiFi AP mode started
- T+1200ms: HTTP server listening on port 80
- T+1300ms: UART initialized, handshake with esp_bt_audio_source
- T+1500ms: I2S master started, tone generator playing
- **Total boot time: ~1.5 seconds**

---

### 3.2. Internet Radio Playback Flow

```
User        Web UI      HTTP Svr    Radio Player   ESP-ADF      I2S Mgr     UART Mgr   esp_bt_audio_source
 │            │            │            │             │            │           │               │
 │ enter URL  │            │            │             │            │           │               │
 ├───────────▶│            │            │             │            │           │               │
 │            │ POST       │            │             │            │           │               │
 │            │ /radio/url │            │             │            │           │               │
 │            ├───────────▶│            │             │            │           │               │
 │            │            │ validate   │             │            │           │               │
 │            │            │ set_url()  │             │            │           │               │
 │            │            ├───────────▶│             │            │           │               │
 │            │            │            │ save NVS    │            │           │               │
 │            │            │◀───────────┤             │            │           │               │
 │            │◀───────────┤            │             │            │           │               │
 │            │ 200 OK     │            │             │            │           │               │
 │◀───────────┤            │            │             │            │           │               │
 │            │            │            │             │            │           │               │
 │ click Play │            │            │             │            │           │               │
 │───────────▶│ POST /bt/command       │             │            │           │               │
 │            │ {cmd: START}            │             │            │           │               │
 │            ├────────────┼────────────┼─────────────┼────────────┼──────────▶│               │
 │            │            │            │             │            │           │ START\n       │
 │            │            │            │             │            │           ├──────────────▶│
 │            │            │            │             │            │           │◀──────────────┤
 │            │            │            │             │            │           │ OK|START      │
 │            │            │            │             │            │           │               │
 │            │            │ start()    │             │            │           │               │
 │            │            ├───────────▶│             │            │           │               │
 │            │            │            │ http connect│            │           │               │
 │            │            │            ├────────────▶│            │           │               │
 │            │            │            │◀────────────┤            │           │               │
 │            │            │            │ HTTP 200    │            │           │               │
 │            │            │            │             │            │           │               │
 │            │            │            │ buffer fill │            │           │               │
 │            │            │            │ (0→30%)     │            │           │               │
 │            │            │            │             │            │           │               │
 │            │            │            │ decode MP3  │            │           │               │
 │            │            │            ├────────────▶│            │           │               │
 │            │            │            │◀────────────┤            │           │               │
 │            │            │            │ PCM frames  │            │           │               │
 │            │            │            │             │            │           │               │
 │            │            │            │ write I2S   │            │           │               │
 │            │            │            ├─────────────┼───────────▶│           │               │
 │            │            │            │             │            │ DMA TX    │               │
 │            │            │            │             │            ├───────────┼──────────────▶│
 │            │            │            │             │            │           │  I2S clocks   │
 │            │            │            │             │            │           │  + PCM data   │
 │            │            │            │             │            │           │               │
 │  [Music Playing] ◀──────┴────────────┴─────────────┴────────────┴───────────┴───────────────┘
```

---

### 3.3. WiFi STA Mode Switching with Auto-Revert

```
User        Web UI      HTTP Svr    WiFi Mgr     NVS Mgr      State Mgr
 │            │            │            │            │             │
 │ enter SSID/pass         │            │            │             │
 ├───────────▶│            │            │            │             │
 │            │ POST       │            │            │             │
 │            │ /wifi/sta  │            │            │             │
 │            ├───────────▶│            │            │             │
 │            │            │ save creds │            │             │
 │            │            ├───────────▶│            │             │
 │            │            │            │ nvs_set()  │             │
 │            │            │            ├───────────▶│             │
 │            │            │            │◀───────────┤             │
 │            │◀───────────┤            │            │             │
 │            │ 200 OK     │            │            │             │
 │◀───────────┤            │            │            │             │
 │            │            │            │            │             │
 │ click "Switch to STA"   │            │            │             │
 ├───────────▶│            │            │            │             │
 │            │ POST       │            │            │             │
 │            │ /wifi/mode │            │            │             │
 │            ├───────────▶│            │            │             │
 │            │            │ set_mode(STA)          │             │
 │            │            ├───────────▶│            │             │
 │            │            │            │ save mode  │             │
 │            │            │            ├───────────▶│             │
 │            │            │            │            │             │
 │            │            │            │ stop AP    │             │
 │            │            │            │────────┐   │             │
 │            │            │            │◀───────┘   │             │
 │            │            │            │            │             │
 │            │            │            │ start STA  │             │
 │            │            │            │────────┐   │             │
 │            │            │            │        │ scan/connect    │
 │            │            │            │        │ [30s timeout]   │
 │            │            │            │◀───────┘   │             │
 │            │            │            │            │             │
 │            │            │            │ ┌──────────────────┐     │
 │            │            │            │ │ SUCCESS: Got IP  │     │
 │            │            │            │ └──────────────────┘     │
 │            │            │            │ restart httpd            │
 │            │            │◀───────────┤            │             │
 │            │            │ notify STA connected    │             │
 │            │            │            │            │             │
 │ navigate to new IP      │            │            │             │
 │ (e.g., 192.168.1.100)   │            │            │             │
 ├───────────▶│            │            │            │             │
 │            │ GET /      │            │            │             │
 │            ├───────────▶│            │            │             │
 │◀───────────┴────────────┤            │            │             │
 │  [Web UI on STA mode]   │            │            │             │
 │                         │            │            │             │
 │                         │  ┌─────────────────────────────────┐  │
 │                         │  │ FAILURE: Timeout after 30s      │  │
 │                         │  │ → Auto-revert to AP mode        │  │
 │                         │  └─────────────────────────────────┘  │
 │                         │            │            │             │
 │                         │            │ stop STA   │             │
 │                         │            │────────┐   │             │
 │                         │            │◀───────┘   │             │
 │                         │            │            │             │
 │                         │            │ start AP   │             │
 │                         │            │────────┐   │             │
 │                         │            │◀───────┘   │             │
 │                         │            │            │             │
 │                         │            │ update mode=AP (preserve creds)
 │                         │            ├───────────▶│             │
 │                         │            │            │             │
 │ reconnect to AP         │            │            │             │
 │ (192.168.4.1)           │            │            │             │
 ├───────────▶│            │            │            │             │
 │◀───────────┘            │            │            │             │
 │  [Back to AP mode]      │            │            │             │
 │  STA creds preserved    │            │            │             │
 │  for manual retry       │            │            │             │
```

---

### 3.4. Web UI Authentication Flow

```
User        Browser     HTTP Svr     NVS Mgr      State Mgr
 │            │            │            │             │
 │ navigate to IP          │            │             │
 │ (first boot)            │            │             │
 ├───────────▶│            │            │             │
 │            │ GET /      │            │             │
 │            ├───────────▶│            │             │
 │            │            │ check auth │             │
 │            │            │ first_login=true         │
 │            │            │            │             │
 │            │◀───────────┤            │             │
 │            │ 302 Redirect            │             │
 │            │ /change_password        │             │
 │◀───────────┤            │            │             │
 │            │            │            │             │
 │ [Forced Password Change Page]       │             │
 │            │            │            │             │
 │ enter new password      │            │             │
 ├───────────▶│            │            │             │
 │            │ POST       │            │             │
 │            │ /change_password        │             │
 │            ├───────────▶│            │             │
 │            │            │ validate (8+ chars)      │
 │            │            │ hash SHA256│             │
 │            │            │ save hash  │             │
 │            │            ├───────────▶│             │
 │            │            │            │             │
 │            │            │ set first_login=false    │
 │            │            │ create session           │
 │            │◀───────────┤            │             │
 │            │ 302 Redirect            │             │
 │            │ /dashboard │            │             │
 │◀───────────┤            │            │             │
 │            │            │            │             │
 │  [Subsequent Logins]    │            │             │
 │            │            │            │             │
 │ navigate to IP          │            │             │
 ├───────────▶│            │            │             │
 │            │ GET /      │            │             │
 │            ├───────────▶│            │             │
 │            │            │ check session            │
 │            │            │ (no cookie)│             │
 │            │◀───────────┤            │             │
 │            │ 302 Redirect            │             │
 │            │ /login     │            │             │
 │◀───────────┤            │            │             │
 │            │            │            │             │
 │ enter admin/password    │            │             │
 ├───────────▶│            │            │             │
 │            │ POST /login│            │             │
 │            ├───────────▶│            │             │
 │            │            │ load hash  │             │
 │            │            ├───────────▶│             │
 │            │            │◀───────────┤             │
 │            │            │ compare hash            │
 │            │            │ create session           │
 │            │            │ set cookie │             │
 │            │◀───────────┤            │             │
 │            │ 302 Redirect            │             │
 │            │ /dashboard │            │             │
 │◀───────────┤            │            │             │
 │            │ Set-Cookie: session_token=...         │
 │            │            │            │             │
 │  [Authenticated Session - 1 hour]   │             │
```

---

## 4. State Machines

### 4.1. WiFi Mode State Machine

```
                    ┌─────────────┐
                    │    INIT     │
                    └──────┬──────┘
                           │
                           │ load NVS config
                           │
                    ┌──────▼──────────────┐
                    │ Check wifi_mode     │
                    └──────┬──────────────┘
                           │
              ┌────────────┼────────────┐
              │                         │
        mode=AP                    mode=STA
              │                         │
              ▼                         ▼
     ┌────────────────┐        ┌────────────────┐
     │   AP_STARTING  │        │  STA_STARTING  │
     └────────┬───────┘        └────────┬───────┘
              │                         │
              │ start AP                │ start STA
              │                         │ [30s timeout]
              ▼                         │
     ┌────────────────┐                 │
     │   AP_ACTIVE    │                 │
     └────────┬───────┘                 │
              │                         │
              │ user: switch to STA     │
              │                         │
              └────────────────┬────────┘
                               │
                               ▼
                      ┌─────────────────┐
                      │ STA_CONNECTING  │
                      └────────┬────────┘
                               │
                    ┌──────────┼──────────┐
                    │                     │
              SUCCESS: got IP       FAIL: timeout
                    │                     │
                    ▼                     ▼
           ┌─────────────────┐   ┌────────────────┐
           │  STA_CONNECTED  │   │  AUTO_REVERT   │
           └────────┬────────┘   └────────┬───────┘
                    │                     │
                    │                     │ stop STA
                    │                     │ start AP
                    │                     │ preserve creds
                    │                     │
                    │                     ▼
                    │            ┌────────────────┐
                    │            │   AP_ACTIVE    │
                    │            │ (with error)   │
                    │            └────────────────┘
                    │
                    │ user: disconnect
                    │
                    ▼
           ┌─────────────────┐
           │ STA_DISCONNECTED│
           └────────┬────────┘
                    │
                    │ auto-retry (3x)
                    │ OR user: revert to AP
                    │
                    ▼
           ┌─────────────────┐
           │   AP_ACTIVE     │
           └─────────────────┘
```

**State Transition Events:**
- `WIFI_EVENT_AP_STARTED` → `AP_ACTIVE`
- `WIFI_EVENT_STA_CONNECTED` → `STA_CONNECTING`
- `IP_EVENT_STA_GOT_IP` → `STA_CONNECTED`
- `WIFI_EVENT_STA_DISCONNECTED` → `STA_DISCONNECTED` → retry or `AUTO_REVERT`
- Timeout (30s) → `AUTO_REVERT` → `AP_ACTIVE`

---

### 4.2. Audio Source State Machine

```
                    ┌─────────────┐
                    │    IDLE     │
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              │                         │
        user: start tone          user: start radio
              │                         │
              ▼                         ▼
     ┌────────────────┐        ┌────────────────┐
     │  TONE_STARTING │        │ RADIO_STARTING │
     └────────┬───────┘        └────────┬───────┘
              │                         │
              │ init tone               │ set URL
              │ start task              │ init pipeline
              │                         │ http connect
              ▼                         │
     ┌────────────────┐                 │
     │  TONE_PLAYING  │                 │
     └────────┬───────┘                 │
              │                         │
              │                         ▼
              │                ┌─────────────────┐
              │                │ RADIO_BUFFERING │
              │                └────────┬────────┘
              │                         │
              │                         │ buffer 30%
              │                         │
              │                         ▼
              │                ┌─────────────────┐
              │                │ RADIO_PLAYING   │
              │                └────────┬────────┘
              │                         │
              │                         │
              ├─────────────────────────┤
              │                         │
         user: stop               network error
              │                         │
              ▼                         ▼
     ┌────────────────┐        ┌────────────────┐
     │    STOPPING    │        │ RADIO_RETRYING │
     └────────┬───────┘        └────────┬───────┘
              │                         │
              │ cleanup                 │ exponential backoff
              │                         │ 1s → 2s → 4s
              │                         │ max 3 attempts
              ▼                         │
     ┌────────────────┐                 │
     │     IDLE       │                 │
     └────────────────┘                 │
              ▲                         │
              │                         │
              │                ┌────────┴────────┐
              │                │                 │
              │          success: reconnect   fail: max attempts
              │                │                 │
              │                ▼                 ▼
              │       ┌─────────────────┐ ┌──────────────┐
              │       │ RADIO_BUFFERING │ │ RADIO_ERROR  │
              │       └─────────────────┘ └──────┬───────┘
              │                                  │
              │                            user: retry
              │                                  │
              └──────────────────────────────────┘
```

**State Transition Triggers:**
- API calls: `tone_start()`, `radio_start()`, `*_stop()`
- Network events: `HTTP_STREAM_EVENT_ON_CONNECTED`, `HTTP_STREAM_EVENT_ON_ERROR`
- Buffer fill level: 30% threshold for buffering → playing
- Error classification: recoverable (retry) vs non-recoverable (error state)

---

### 4.3. UART Command State Machine

```
                    ┌─────────────┐
                    │    IDLE     │
                    └──────┬──────┘
                           │
                      send command
                           │
                           ▼
                  ┌─────────────────┐
                  │ WAIT_RESPONSE   │
                  │ [5s timeout]    │
                  └────────┬────────┘
                           │
         ┌─────────────────┼─────────────────┐
         │                 │                 │
    receive OK      receive ERR        timeout
         │                 │                 │
         ▼                 ▼                 ▼
┌─────────────────┐ ┌──────────────┐ ┌──────────────┐
│    SUCCESS      │ │    ERROR     │ │   TIMEOUT    │
└────────┬────────┘ └──────┬───────┘ └──────┬───────┘
         │                 │                 │
         │                 │                 │ retry_count++
         │                 │                 │
         │                 │          ┌──────┴──────┐
         │                 │          │             │
         │                 │    retry_count < 2   retry_count >= 2
         │                 │          │             │
         │                 │          ▼             ▼
         │                 │   ┌──────────┐  ┌──────────┐
         │                 │   │  RETRY   │  │  FAILED  │
         │                 │   └────┬─────┘  └──────────┘
         │                 │        │
         │                 │        │ wait 1s
         │                 │        │
         │                 │        ▼
         │                 │  ┌──────────────┐
         │                 │  │WAIT_RESPONSE │
         │                 │  └──────────────┘
         │                 │
         ├─────────────────┴─────────────────┐
         │                                   │
         │ callback(result)                  │
         │                                   │
         ▼                                   ▼
┌─────────────────┐                  ┌──────────────┐
│     IDLE        │                  │     IDLE     │
└─────────────────┘                  └──────────────┘


[Asynchronous Event Handling - Parallel]

         Receive "EVENT|..."
                 │
                 ▼
         ┌──────────────┐
         │ Parse Event  │
         └──────┬───────┘
                │
                ▼
         ┌──────────────┐
         │ Event Queue  │
         └──────┬───────┘
                │
                ▼
         ┌──────────────┐
         │ Event Task   │
         │ (callback)   │
         └──────────────┘
```

**Concurrency Handling:**
- Mutex for command send (prevent interleaved commands)
- Separate queue for asynchronous events (non-blocking)
- Response matching: track `pending_command` string, match against `OK|CMD` or `ERR|CMD`

---

## 5. API Specifications

### 5.1. Main Application API

**Header:** `main/include/app_main.h`

```c
// Application lifecycle
void app_main(void);
esp_err_t app_init(void);
esp_err_t app_shutdown(void);

// Global state access
typedef struct {
    wifi_status_t wifi;
    radio_status_t radio;
    i2s_stats_t i2s;
    bool bt_connected;
    char bt_device_mac[18];
    audio_source_t current_source;  // TONE or RADIO
} app_status_t;

esp_err_t app_get_status(app_status_t *status);
```

### 5.2. Error Codes

**Header:** `main/include/app_errors.h`

```c
// Custom error codes (ESP_ERR_* base + offset)
#define ESP_ERR_WIFI_BASE           (ESP_ERR_WIFI_BASE + 0x1000)
#define ESP_ERR_WEBSERVER_BASE      (ESP_ERR_WIFI_BASE + 0x2000)
#define ESP_ERR_UART_BASE           (ESP_ERR_WIFI_BASE + 0x3000)
#define ESP_ERR_RADIO_BASE          (ESP_ERR_WIFI_BASE + 0x4000)

// WiFi errors
#define ESP_ERR_WIFI_STA_TIMEOUT    (ESP_ERR_WIFI_BASE + 1)
#define ESP_ERR_WIFI_AUTH_FAIL      (ESP_ERR_WIFI_BASE + 2)

// Web server errors
#define ESP_ERR_WEB_AUTH_REQUIRED   (ESP_ERR_WEBSERVER_BASE + 1)
#define ESP_ERR_WEB_MAX_SESSIONS    (ESP_ERR_WEBSERVER_BASE + 2)

// UART errors
#define ESP_ERR_UART_TIMEOUT        (ESP_ERR_UART_BASE + 1)
#define ESP_ERR_UART_PARSE_ERROR    (ESP_ERR_UART_BASE + 2)
#define ESP_ERR_UART_BT_ERROR       (ESP_ERR_UART_BASE + 3)

// Radio errors
#define ESP_ERR_RADIO_CONNECT_FAIL  (ESP_ERR_RADIO_BASE + 1)
#define ESP_ERR_RADIO_DECODE_FAIL   (ESP_ERR_RADIO_BASE + 2)
#define ESP_ERR_RADIO_BUFFER_UNDERRUN (ESP_ERR_RADIO_BASE + 3)
```

---

## 6. Memory Layout and Budgets

### 6.1. ESP32-S3 Memory Map

```
ESP32-S3 Total SRAM: ~512 KB
After WiFi/BT/System: ~200-300 KB available

┌──────────────────────────────────────┐ 0x3FC88000
│   WiFi/System Reserved (~200 KB)    │
├──────────────────────────────────────┤ 0x3FCB8000
│                                      │
│   Available Heap (~200-300 KB)      │
│                                      │
│   ┌────────────────────────────┐    │
│   │ ESP-ADF Framework (~60 KB) │    │
│   ├────────────────────────────┤    │
│   │ Network Buffer (64 KB)     │    │
│   ├────────────────────────────┤    │
│   │ I2S DMA Buffers (8 KB)     │    │
│   ├────────────────────────────┤    │
│   │ Web Server (15 KB)         │    │
│   ├────────────────────────────┤    │
│   │ WiFi Stack (40 KB)         │    │
│   ├────────────────────────────┤    │
│   │ Free Heap (70-110 KB)      │    │
│   └────────────────────────────┘    │
│                                      │
└──────────────────────────────────────┘ 0x3FD00000
```

### 6.2. Memory Budget (ESP32-S3)

| Component | Heap Usage | Stack | Notes |
|-----------|-----------|-------|-------|
| **ESP-ADF Framework** | 60 KB | — | Audio pipeline, elements, codecs |
| **Network Buffer** | 64 KB | — | Circular buffer (32-128 KB configurable) |
| **I2S DMA Buffers** | 8 KB | — | 8 buffers × 512 samples × 2 bytes × 2 channels |
| **Web Server** | 15 KB | — | httpd connections, scratch buffers |
| **WiFi Stack** | 40 KB | — | esp_wifi internal buffers |
| **FreeRTOS Tasks** | — | 48 KB | 12 tasks × 4 KB avg stack |
| **Static/BSS** | 10 KB | — | Global variables, const data |
| **Total Used** | ~187 KB | 48 KB | |
| **Available Heap** | ~200-300 KB | — | After WiFi/system |
| **Free Margin** | **70-110 KB** | — | Comfortable for growth |

### 6.3. Task Stack Allocations

| Task Name | Priority | Stack Size | Notes |
|-----------|----------|------------|-------|
| `wifi_task` | 5 | 4096 | WiFi event handling |
| `httpd_task` | 4 | 4096 | HTTP server (per connection) |
| `uart_rx_task` | 6 | 3072 | UART RX line parsing |
| `radio_task` | 5 | 4096 | ESP-ADF pipeline control |
| `tone_gen_task` | 4 | 3072 | Tone generation |
| `i2s_write_task` | 6 | 3072 | I2S DMA writes |
| `state_mgr_task` | 3 | 2048 | State machine coordination |
| `event_loop_task` | 5 | 4096 | ESP-IDF event loop |

**Total Stack:** ~48 KB (12 tasks × 4 KB average)

---

## 7. Error Handling

### 7.1. Internet Radio Stream Resilience

**Error Classification Table:**

| Error Type | HTTP Code | Action | Retry? | User Message |
|------------|-----------|--------|--------|--------------|
| Network timeout | — | Exponential backoff | Yes (3x) | "Buffering... reconnecting" |
| DNS failure | — | Retry immediately | Yes (3x) | "Cannot resolve stream URL" |
| Connection refused | — | Wait 2s, retry | Yes (3x) | "Stream server unavailable" |
| HTTP 404 Not Found | 404 | Stop immediately | No | "Stream not found (404)" |
| HTTP 403 Forbidden | 403 | Stop immediately | No | "Access denied (403)" |
| HTTP 500 Server Error | 500 | Wait 5s, retry | Yes (2x) | "Server error, retrying..." |
| Decode error (bad MP3) | — | Skip frame, continue | N/A | "Audio glitch detected" |
| Buffer underrun | — | Mute, refill buffer | N/A | "Buffering..." |

**Retry Logic Implementation:**
```c
typedef struct {
    uint8_t attempt;            // Current retry attempt (0-2)
    uint32_t backoff_ms;        // Current backoff delay
    time_t last_attempt;
    bool recoverable;           // Determined by error type
} retry_state_t;

esp_err_t radio_handle_error(http_error_t error, retry_state_t *retry) {
    // Classify error
    if (error.code == 404 || error.code == 403) {
        retry->recoverable = false;
        return ESP_FAIL;  // Stop immediately
    }
    
    // Exponential backoff: 1s, 2s, 4s
    if (retry->attempt < 3) {
        retry->backoff_ms = (1 << retry->attempt) * 1000;  // 2^n seconds
        vTaskDelay(pdMS_TO_TICKS(retry->backoff_ms));
        retry->attempt++;
        return ESP_ERR_RETRY;
    }
    
    // Max attempts exceeded
    return ESP_FAIL;
}
```

### 7.2. UART Command Error Handling

**Error Mapping Table (from PRD Section 5.3):**

| esp_bt_audio_source Error | esp_i2s_source Action | Web UI Message | HTTP Code |
|---------------------------|----------------------|----------------|-----------|
| `ERR\|CONNECT\|NOT_FOUND` | Stop, display error | "Device not paired. Please pair first." | 400 |
| `ERR\|CONNECT\|FAILED` | Stop, display error | "Connection failed. Check device is on." | 500 |
| `ERR\|START\|BUSY` | Retry 1s (3x max) | "Audio busy, retrying..." | 409 |
| `ERR\|START\|NOT_CONNECTED` | Stop, display error | "No Bluetooth speaker connected." | 400 |
| `ERR\|PLAY\|FS\|FILE_NOT_FOUND` | Display error | "File not found on BT device." | 404 |
| `ERR\|VOLUME\|OUT_OF_RANGE` | Clamp 0-100, retry | "Volume adjusted to valid range." | 200 |
| `ERR\|SCAN\|BUSY` | Wait 2s, retry once | "Scan in progress, please wait..." | 409 |
| `ERR\|<any>\|TIMEOUT` | Log timeout | "Command timeout. Check UART connection." | 504 |

**Implementation:**
```c
typedef struct {
    const char *error_prefix;   // e.g., "ERR|CONNECT|NOT_FOUND"
    bool retryable;
    uint16_t http_status;
    const char *user_message;
} error_mapping_t;

static const error_mapping_t error_map[] = {
    {"ERR|CONNECT|NOT_FOUND", false, 400, "Device not paired. Please pair first."},
    {"ERR|START|BUSY", true, 409, "Audio busy, retrying..."},
    // ... (complete table)
};

const error_mapping_t* uart_map_error(const uart_response_t *response) {
    for (int i = 0; i < ARRAY_SIZE(error_map); i++) {
        if (strncmp(response->result, error_map[i].error_prefix, 
                    strlen(error_map[i].error_prefix)) == 0) {
            return &error_map[i];
        }
    }
    return NULL;  // Unknown error
}
```

### 7.3. Web UI Error Responses

**JSON Error Format:**
```json
{
  "status": "error",
  "code": 400,
  "message": "Device not paired. Please pair first.",
  "details": "ERR|CONNECT|NOT_FOUND|Device AA:BB:CC:DD:EE:FF not in paired list"
}
```

**Implementation:**
```c
esp_err_t webserver_send_error(httpd_req_t *req, uint16_t http_code, 
                                const char *message, const char *details) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, http_status_to_str(http_code));
    
    char json[512];
    snprintf(json, sizeof(json), 
             "{\"status\":\"error\",\"code\":%d,\"message\":\"%s\",\"details\":\"%s\"}",
             http_code, message, details ? details : "");
    
    return httpd_resp_send(req, json, strlen(json));
}
```

---

## 8. Configuration Management

### 8.1. Kconfig Options

**File:** `main/Kconfig.projbuild`

```kconfig
menu "ESP I2S Source Configuration"

    menu "WiFi Configuration"
        config ESP_I2S_WIFI_AP_SSID
            string "Default AP SSID"
            default "ESP_I2S_SOURCE"
            help
                Default SSID for AP mode on first boot.

        config ESP_I2S_WIFI_AP_PASSWORD
            string "Default AP Password"
            default "esp32audio"
            help
                Default password for AP mode (WPA2-PSK).

        config ESP_I2S_WIFI_STA_TIMEOUT_MS
            int "STA mode connection timeout (ms)"
            default 30000
            range 10000 120000
            help
                Timeout before auto-reverting to AP mode if STA join fails.
    endmenu

    menu "Internet Radio Configuration"
        config ESP_I2S_RADIO_BUFFER_SIZE
            int "Network buffer size (bytes)"
            default 65536
            range 32768 131072
            help
                Circular buffer for internet radio streaming.
                Larger = more resilience against jitter, more RAM usage.

        config ESP_I2S_RADIO_MAX_RETRIES
            int "Max reconnection attempts"
            default 3
            range 1 10
            help
                Number of retry attempts before failing stream.
    endmenu

    menu "I2S Configuration"
        config ESP_I2S_SAMPLE_RATE
            int "Sample rate (Hz)"
            default 48000
            help
                Fixed output sample rate. All audio sources normalized to this rate.

        config ESP_I2S_BCLK_PIN
            int "I2S BCLK GPIO pin"
            default 18
            range 0 48
            help
                GPIO for I2S bit clock (master mode, output).

        config ESP_I2S_WS_PIN
            int "I2S WS GPIO pin"
            default 23
            range 0 48
            help
                GPIO for I2S word select (master mode, output).

        config ESP_I2S_DOUT_PIN
            int "I2S DOUT GPIO pin"
            default 19
            range 0 48
            help
                GPIO for I2S data output.

        config ESP_I2S_DMA_BUF_COUNT
            int "I2S DMA buffer count"
            default 8
            range 2 16
            help
                Number of DMA buffers for I2S TX.

        config ESP_I2S_DMA_BUF_LEN
            int "I2S DMA buffer length (samples)"
            default 512
            range 128 1024
            help
                Length of each DMA buffer in samples.
    endmenu

    menu "UART Configuration"
        config ESP_I2S_UART_PORT
            int "UART port number"
            default 1
            range 0 2
            help
                UART port for communication with esp_bt_audio_source.

        config ESP_I2S_UART_TX_PIN
            int "UART TX GPIO pin"
            default 16
            range 0 48

        config ESP_I2S_UART_RX_PIN
            int "UART RX GPIO pin"
            default 17
            range 0 48

        config ESP_I2S_UART_BAUD_RATE
            int "UART baud rate"
            default 115200

        config ESP_I2S_UART_TIMEOUT_MS
            int "UART command timeout (ms)"
            default 5000
            range 1000 30000
    endmenu

    menu "Web Server Configuration"
        config ESP_I2S_WEB_PORT
            int "HTTP server port"
            default 80
            range 1 65535

        config ESP_I2S_WEB_MAX_CONNECTIONS
            int "Max concurrent connections"
            default 4
            range 1 8

        config ESP_I2S_WEB_SESSION_TIMEOUT_S
            int "Session timeout (seconds)"
            default 3600
            range 300 86400
            help
                Session expires after this many seconds of inactivity.
    endmenu

endmenu
```

### 8.2. Runtime Configuration Override

```c
// Allow runtime override of Kconfig defaults
typedef struct {
    // WiFi (overrides Kconfig)
    char wifi_ap_ssid[32];
    char wifi_ap_password[64];
    uint32_t wifi_sta_timeout_ms;
    
    // Radio (overrides Kconfig)
    uint32_t radio_buffer_size;
    uint8_t radio_max_retries;
    
    // I2S (overrides Kconfig)
    uint32_t i2s_sample_rate;
    // ... (pins usually not overridden at runtime)
    
    // UART (overrides Kconfig)
    uint32_t uart_timeout_ms;
} runtime_config_t;

// Load Kconfig defaults, then apply NVS overrides if present
esp_err_t config_load_with_overrides(runtime_config_t *cfg);
```

---

## 9. Implementation Guidelines

### 9.1. Component Directory Structure

```
esp_i2s_source/
├── main/
│   ├── app_main.c              # Entry point, init sequence
│   ├── state_manager.c         # Global state coordination
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   └── include/
│       ├── app_main.h
│       ├── app_errors.h
│       └── state_manager.h
├── components/
│   ├── wifi_manager/
│   │   ├── wifi_manager.c
│   │   ├── CMakeLists.txt
│   │   └── include/
│   │       └── wifi_manager.h
│   ├── webserver/
│   │   ├── webserver.c
│   │   ├── handlers.c          # REST endpoint handlers
│   │   ├── auth.c              # Authentication logic
│   │   ├── CMakeLists.txt
│   │   └── include/
│   │       └── webserver.h
│   ├── uart_manager/
│   │   ├── uart_manager.c
│   │   ├── uart_parser.c       # Line-buffered parsing
│   │   ├── CMakeLists.txt
│   │   └── include/
│   │       └── uart_manager.h
│   ├── radio_player/
│   │   ├── radio_player.c
│   │   ├── adf_pipeline.c      # ESP-ADF pipeline setup
│   │   ├── CMakeLists.txt
│   │   └── include/
│   │       └── radio_player.h
│   ├── tone_generator/
│   │   ├── tone_generator.c
│   │   ├── waveforms.c         # Sine/square/sawtooth
│   │   ├── CMakeLists.txt
│   │   └── include/
│   │       └── tone_generator.h
│   ├── i2s_manager/
│   │   ├── i2s_manager.c
│   │   ├── CMakeLists.txt
│   │   └── include/
│   │       └── i2s_manager.h
│   └── nvs_manager/
│       ├── nvs_manager.c
│       ├── CMakeLists.txt
│       └── include/
│           └── nvs_manager.h
├── test/
│   ├── host/                   # Host tests (mocked ESP-IDF)
│   │   ├── test_uart_parser.c
│   │   ├── test_wifi_state_machine.c
│   │   ├── test_nvs_schema_migration.c
│   │   └── CMakeLists.txt
│   └── device/                 # Device tests (Unity on ESP32)
│       ├── test_i2s_tone.c
│       ├── test_uart_echo.c
│       └── CMakeLists.txt
├── docs/
│   ├── PRD.md                  # Product Requirements
│   ├── FS.md                   # Functional Spec (this file)
│   ├── ARCH.md                 # Architecture overview
│   └── DOC_REVIEW.md           # Review findings
├── sdkconfig.defaults
├── partitions.csv
└── CMakeLists.txt
```

### 9.2. Coding Standards

**Follow ESP-IDF and repository conventions:**

1. **Naming:**
   - Components: `snake_case` (e.g., `wifi_manager`)
   - Functions: `component_verb_noun()` (e.g., `wifi_manager_init()`)
   - Types: `name_t` suffix (e.g., `wifi_config_t`)
   - Enums: `UPPER_CASE` (e.g., `WIFI_MODE_AP`)
   - Macros: `UPPER_CASE` with component prefix (e.g., `WIFI_MGR_DEFAULT_TIMEOUT`)

2. **Error Handling:**
   - Always check `esp_err_t` return values with `ESP_ERROR_CHECK()` or explicit handling
   - Use `ESP_LOGx()` for all logging (no `printf`)
   - Log error code with `esp_err_to_name(err)`

3. **Memory Management:**
   - Prefer static allocation for task stacks and buffers where possible
   - Use `heap_caps_malloc()` for specific memory types (PSRAM, DMA-capable)
   - Always check malloc/calloc return values
   - Free resources in reverse order of allocation (RAII pattern)

4. **Concurrency:**
   - Use FreeRTOS mutexes for shared state
   - Use queues for producer-consumer patterns
   - Document locking order to prevent deadlocks
   - Keep critical sections short (<1ms)

5. **ISR Safety:**
   - Never allocate memory in ISRs
   - Use `*FromISR` API variants
   - Mark ISR functions with `IRAM_ATTR` if frequently called

### 9.3. Testing Requirements

**From PRD Section 11 (Test Matrix Summary):**

- **30+ test cases** (10 unit, 8 integration, 5 stress, 4 performance)
- **Code coverage target:** >80% minimum, >90% preferred
- **CI/CD pipeline:** Pre-commit (<30s), PR (<5min), nightly (full suite), release (1-hour soak)
- **Logic analyzer validation:** I2S BCLK/WS timing ±0.1%, data alignment

**Test Implementation Strategy:**
1. **Host tests** (fast, CI-friendly):
   - Mock ESP-IDF APIs (`esp_wifi`, `httpd`, `i2s`, `nvs`)
   - In-memory NVS simulation
   - Deterministic failure injection

2. **Device tests** (Unity on ESP32-S3):
   - Tone generator frequency accuracy (<0.1% error)
   - UART echo test (loopback)
   - WiFi AP mode startup

3. **Integration tests** (two-device harness):
   - Cross-device audio playback
   - UART command round-trip
   - End-to-end internet radio stream

4. **Performance tests:**
   - CPU profiling (≤30% during streaming)
   - Memory profiling (>70KB free heap)
   - Latency profiling (<20ms decode → I2S)

---

## 10. Testing Specifications

### 10.1. Unit Test Examples

**Test: UART Command Serialization**
```c
TEST_CASE("uart_manager serializes CONNECT command correctly", "[uart]") {
    uart_config_t config = UART_CONFIG_DEFAULT();
    uart_manager_init(&config);
    
    uart_response_t response;
    esp_err_t err = uart_manager_send_command("CONNECT AA:BB:CC:DD:EE:FF", &response);
    
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_STRING("CONNECT AA:BB:CC:DD:EE:FF", response.command);
}
```

**Test: WiFi Mode State Machine**
```c
TEST_CASE("wifi_manager auto-reverts to AP on STA timeout", "[wifi]") {
    // Mock WiFi driver
    wifi_mock_set_sta_timeout(30000);
    
    wifi_manager_init();
    wifi_manager_set_mode(WIFI_MODE_STA);
    wifi_manager_start();
    
    // Simulate timeout
    vTaskDelay(pdMS_TO_TICKS(31000));
    
    wifi_status_t status;
    wifi_manager_get_status(&status);
    
    TEST_ASSERT_EQUAL(WIFI_MODE_AP, status.current_mode);
    TEST_ASSERT_EQUAL(true, status.connected);
}
```

**Test: NVS Schema Migration**
```c
TEST_CASE("nvs_manager migrates from v0 to v1 schema", "[nvs]") {
    // Populate NVS with v0 schema (no version key)
    nvs_set_str(nvs_handle, "sta_ssid", "OldNetwork");
    
    nvs_config_t config;
    esp_err_t err = nvs_manager_load_config(&config);
    
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, config.nvs_schema_ver);
    TEST_ASSERT_EQUAL_STRING("OldNetwork", config.sta_ssid);
    // New keys should have defaults
    TEST_ASSERT_EQUAL(0, config.boot_count);
}
```

### 10.2. Integration Test Examples

**Test: Cross-Device Audio Playback**
```python
# Python test script (runs on host PC, controls 2x ESP32s via USB serial)
import serial
import time

def test_tone_to_bluetooth_playback():
    # Setup
    wifi_esp = serial.Serial('/dev/ttyUSB0', 115200)  # esp_i2s_source
    bt_esp = serial.Serial('/dev/ttyUSB1', 115200)    # esp_bt_audio_source
    
    # Start tone on WiFi ESP32
    send_uart_command(wifi_esp, "TONE 1000 50\n")
    response = wait_uart_response(wifi_esp, timeout=2.0)
    assert response == "OK|TONE"
    
    # Verify I2S clocks on logic analyzer
    logic_analyzer = LogicAnalyzer('/dev/ttyACM0')
    bclk_freq = logic_analyzer.measure_frequency('BCLK')
    ws_freq = logic_analyzer.measure_frequency('WS')
    
    assert abs(bclk_freq - 1536000) < 1000  # 48kHz × 32 bits/sample ±0.1%
    assert abs(ws_freq - 48000) < 50        # 48kHz ±0.1%
    
    # Check audio output on BT speaker (manual verification)
    input("Verify 1kHz tone playing on Bluetooth speaker. Press Enter to continue...")
    
    # Cleanup
    send_uart_command(wifi_esp, "STOP\n")
```

### 10.3. Performance Benchmark Examples

**CPU Profiling:**
```c
void benchmark_radio_cpu_usage(void) {
    // Start internet radio stream
    radio_player_start();
    vTaskDelay(pdMS_TO_TICKS(30000));  // 30 seconds
    
    // Measure CPU usage
    TaskStatus_t task_stats[20];
    uint32_t total_runtime;
    uxTaskGetSystemState(task_stats, 20, &total_runtime);
    
    float cpu_pct = 0.0;
    for (int i = 0; i < 20; i++) {
        if (strcmp(task_stats[i].pcTaskName, "radio_task") == 0) {
            cpu_pct = (float)task_stats[i].ulRunTimeCounter / total_runtime * 100.0;
            break;
        }
    }
    
    ESP_LOGI("BENCH", "Radio player CPU usage: %.1f%%", cpu_pct);
    TEST_ASSERT_LESS_THAN(30.0, cpu_pct);  // Target: <30%
}
```

**Memory Profiling:**
```c
void benchmark_radio_memory_usage(void) {
    size_t free_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    
    // Start internet radio stream
    radio_player_start();
    vTaskDelay(pdMS_TO_TICKS(600000));  // 10 minutes (leak detection)
    
    size_t free_after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    int32_t leak = (int32_t)(free_before - free_after);
    
    ESP_LOGI("BENCH", "Heap before: %d, after: %d, leak: %d bytes", 
             free_before, free_after, leak);
    
    TEST_ASSERT_GREATER_THAN(70000, free_after);  // Target: >70KB free
    TEST_ASSERT_LESS_THAN(1024, leak);            // Max leak: 1KB in 10 min
}
```

---

## Appendix A: References

1. **ESP-IDF Programming Guide:** https://docs.espressif.com/projects/esp-idf/en/v5.4/
2. **ESP-ADF Audio Development Framework:** https://docs.espressif.com/projects/esp-adf/en/latest/
3. **ESP32-S3 Technical Reference Manual:** https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf
4. **esp_bt_audio_source Documentation:**
   - `/esp_bt_audio_source/docs/FS.md` (Command Protocol)
   - `/esp_bt_audio_source/main/README.md` (I2S Slave Config)
   - `/esp_bt_audio_source/ARCH.md` (System Architecture)
5. **ESP I2S Source PRD:** `/esp_i2s_source/docs/PRD.md`
6. **DOC_REVIEW Findings:** `/esp_i2s_source/docs/DOC_REVIEW.md`

---

## Appendix B: Acronyms

- **AP:** Access Point (WiFi mode)
- **STA:** Station (WiFi client mode)
- **UART:** Universal Asynchronous Receiver-Transmitter
- **I2S:** Inter-IC Sound (digital audio interface)
- **BCLK:** Bit Clock (I2S timing signal)
- **WS:** Word Select (I2S channel select signal)
- **DMA:** Direct Memory Access
- **NVS:** Non-Volatile Storage (ESP-IDF flash storage)
- **ESP-ADF:** Espressif Audio Development Framework
- **MP3:** MPEG-1 Audio Layer III (audio codec)
- **PCM:** Pulse Code Modulation (uncompressed audio format)
- **HTTP:** Hypertext Transfer Protocol
- **HTTPS:** HTTP Secure (with TLS/SSL)
- **REST:** Representational State Transfer (API design)
- **JSON:** JavaScript Object Notation (data format)
- **SHA256:** Secure Hash Algorithm 256-bit
- **WPA2:** WiFi Protected Access 2 (security protocol)

---

**Document End**

*This Functional Specification is a living document and will be updated as implementation progresses and new requirements emerge.*
