# ESP32 Bluetooth Audio Source - Architecture Diagrams

This document contains Mermaid diagrams visualizing the architecture decisions from the CODE_REVIEW2 cleanup (Jan-Feb 2026).

## Initialization Sequence

This diagram shows the boot sequence with ownership and layer separation:

```mermaid
sequenceDiagram
    participant ESP32 as ESP32 Boot
    participant Main as main.c<br/>(Platform+Policy)
    participant NVS as nvs_storage<br/>(Platform)
    participant CMD as cmd_init<br/>(Control Plane)
    participant BT as bt_manager<br/>(Data Plane)
    participant Audio as audio_processor<br/>(Media Plane)

    Note over ESP32,Audio: Platform Layer Initialization (Fail-Fast)
    ESP32->>Main: app_main() entry
    Main->>Main: esp_bt_controller_mem_release(BLE)<br/>Memory optimization
    Main->>Main: uart_driver_install()<br/>Early diagnostics (DIAG markers)
    Note right of Main: UART owned by main.c<br/>NEVER delete!
    Main->>NVS: nvs_storage_init()<br/>Platform service init
    Note right of NVS: NVS owned by main.c<br/>Single init, all assume ready

    Note over Main,Audio: Control Plane Initialization
    Main->>CMD: cmd_init()<br/>Command interface ready
    Main->>CMD: xTaskCreate(cmd_process_task)<br/>Command processing starts
    Note right of CMD: Control plane ready<br/>BEFORE data plane

    Note over Main,Audio: Data Plane Initialization (Graceful Degradation)
    Main->>BT: bt_manager_init()<br/>Bluetooth stack ready
    Note right of BT: Commands now work!<br/>SCAN/PAIR available
    BT-->>Main: ESP_OK or error (logged, continue)

    Note over Main,Audio: Media Plane Initialization (Optional)
    Main->>Main: load_audio_boot_config()<br/>Config: NVS → Kconfig → fallback
    Main->>Main: nvs_storage_get_audio_autostart()<br/>Check autostart flag
    alt Autostart Enabled (default)
        Main->>Audio: audio_processor_init(config)<br/>I2S, DMA, queues
        Main->>Audio: audio_processor_start()<br/>Audio pipeline running
        Audio-->>Main: ESP_OK or error (logged, continue)
    else Autostart Disabled
        Main->>Main: Skip audio init<br/>Deferred until command
        Note right of Main: Headless mode:<br/>BT+CMD functional,<br/>no audio resources
    end

    Main->>Main: return from app_main()<br/>FreeRTOS tasks keep running

    Note over ESP32,Audio: System Running: CMD → BT → Audio flow established
```

## Ownership Model

This diagram shows who owns each resource:

```mermaid
graph TB
    subgraph Platform["Platform Layer (main.c owns)"]
        NVS[NVS Storage<br/>Owner: main.c<br/>Init: nvs_storage_init<br/>Policy: Fail-fast ESP_ERROR_CHECK]
        UART[UART Driver<br/>Owner: main.c<br/>Init: uart_driver_install<br/>Policy: NEVER delete]
        BLE_MEM[BLE Memory Release<br/>Owner: main.c<br/>Init: esp_bt_controller_mem_release<br/>Policy: A2DP-only optimization]
    end

    subgraph Policy["Policy Layer (main.c orchestrates)"]
        INIT_ORDER[Init Order<br/>CMD before BT<br/>Control → Data Plane]
        CONFIG[Configuration Loading<br/>NVS → Kconfig → Fallback<br/>load_audio_boot_config]
        AUTOSTART[Autostart Decision<br/>Check NVS flag<br/>Optional audio init]
    end

    subgraph Application["Application Layer (components implement)"]
        BT_MGR[bt_manager<br/>Owner: Bluetooth stack<br/>Init: bt_manager_init<br/>Policy: Graceful degradation]
        AUDIO_PROC[audio_processor<br/>Owner: Audio pipeline<br/>Init: audio_processor_init<br/>Policy: Graceful degradation]
        CMD_IFACE[cmd_init<br/>Owner: Command protocol<br/>Init: cmd_init + task<br/>Policy: Graceful degradation]
    end

    Main[main.c<br/>Bootstrap Orchestrator] --> NVS
    Main --> UART
    Main --> BLE_MEM
    Main --> INIT_ORDER
    Main --> CONFIG
    Main --> AUTOSTART
    Main --> BT_MGR
    Main --> AUDIO_PROC
    Main --> CMD_IFACE

    NVS -.->|Assumes ready| BT_MGR
    NVS -.->|Assumes ready| AUDIO_PROC
    NVS -.->|Assumes ready| CMD_IFACE
    UART -.->|Assumes ready| CMD_IFACE

    style Main fill:#e1f5ff
    style Platform fill:#fff3e0
    style Policy fill:#f3e5f5
    style Application fill:#e8f5e9
```

## Component Dependencies

This diagram shows the dependency relationships between components:

```mermaid
graph LR
    subgraph Main["main.c (Thin Orchestrator)"]
        MAIN[app_main<br/>226→319 lines<br/>WHY comments]
    end

    subgraph Platform["Platform Services"]
        NVS_STORAGE[nvs_storage<br/>Persistence API<br/>autostart flag]
        UART_DRV[UART Driver<br/>ESP-IDF<br/>Diagnostics]
    end

    subgraph Control["Control Plane"]
        CMD_INTERFACE[command_interface<br/>UART protocol<br/>Command parsing]
        CMD_HANDLERS[cmd_handlers<br/>Business logic<br/>AUDIO_AUTOSTART]
    end

    subgraph Data["Data Plane"]
        BT_MANAGER[bt_manager<br/>A2DP/AVRCP/GAP<br/>State machines<br/>ALL BT logic]
    end

    subgraph Media["Media Plane"]
        AUDIO_PROCESSOR[audio_processor<br/>I2S/DMA/Queues<br/>Audio pipeline]
        I2S_MANAGER[i2s_manager<br/>I2S hardware<br/>DMA buffers]
        PLAY_MANAGER[play_manager<br/>WAV playback<br/>File streaming]
        BEEP_MANAGER[beep_manager<br/>Tone generation<br/>Beep synthesis]
    end

    MAIN -->|Calls once| NVS_STORAGE
    MAIN -->|Installs once| UART_DRV
    MAIN -->|Calls init| CMD_INTERFACE
    MAIN -->|Calls init| BT_MANAGER
    MAIN -->|Calls init<br/>if autostart| AUDIO_PROCESSOR

    CMD_INTERFACE --> CMD_HANDLERS
    CMD_HANDLERS -->|Reads config| NVS_STORAGE
    CMD_HANDLERS -->|Controls| BT_MANAGER
    CMD_HANDLERS -->|Controls| AUDIO_PROCESSOR

    BT_MANAGER -->|Reads pairing| NVS_STORAGE
    BT_MANAGER -->|Streams to| AUDIO_PROCESSOR

    AUDIO_PROCESSOR --> I2S_MANAGER
    AUDIO_PROCESSOR --> PLAY_MANAGER
    AUDIO_PROCESSOR --> BEEP_MANAGER
    AUDIO_PROCESSOR -->|Reads pins| NVS_STORAGE

    style MAIN fill:#e1f5ff,stroke:#01579b,stroke-width:3px
    style Platform fill:#fff3e0
    style Control fill:#e8f5e9
    style Data fill:#f3e5f5
    style Media fill:#fce4ec
```

## Configuration Hierarchy

This diagram shows the three-level configuration system:

```mermaid
graph TD
    subgraph User["User Configuration Sources"]
        MENUCONFIG[idf.py menuconfig<br/>Kconfig compile-time<br/>sdkconfig file]
        COMMANDS[UART Commands<br/>I2S_CONFIG pins<br/>AUDIO_AUTOSTART on/off]
    end

    subgraph ConfigLayers["Configuration Hierarchy (Highest → Lowest Priority)"]
        NVS_LAYER[1. NVS Runtime Overrides<br/>• I2S pins<br/>• Audio autostart flag<br/>Field customizable]
        KCONFIG_LAYER[2. Kconfig Compile-Time<br/>• Sample rate 8-96kHz<br/>• Volume 0-100<br/>• Bit depth 16/24/32<br/>• Autostart default]
        FALLBACK_LAYER[3. Hard-Coded Fallbacks<br/>• Last resort only<br/>• Used if Kconfig invalid]
    end

    subgraph Runtime["Runtime Application"]
        LOAD_CONFIG[load_audio_boot_config<br/>Applies hierarchy<br/>Returns audio_config_t]
        AUDIO_INIT[audio_processor_init<br/>Uses final config<br/>I2S/DMA setup]
    end

    MENUCONFIG -->|Build time| KCONFIG_LAYER
    COMMANDS -->|Runtime| NVS_LAYER

    NVS_LAYER -->|Override?| LOAD_CONFIG
    KCONFIG_LAYER -->|Default?| LOAD_CONFIG
    FALLBACK_LAYER -->|Last resort| LOAD_CONFIG

    LOAD_CONFIG --> AUDIO_INIT

    style NVS_LAYER fill:#c8e6c9,stroke:#2e7d32,stroke-width:3px
    style KCONFIG_LAYER fill:#fff9c4,stroke:#f57f17,stroke-width:2px
    style FALLBACK_LAYER fill:#ffccbc,stroke:#d84315,stroke-width:1px
```

## Error Handling Policy

This diagram shows the hybrid fail-fast/graceful approach:

```mermaid
graph TB
    subgraph Platform["Platform Services (Fail-Fast)"]
        NVS_INIT[nvs_storage_init<br/>ESP_ERROR_CHECK<br/>Abort on failure]
        UART_INIT[uart_driver_install<br/>Log error, continue<br/>printf fallback]
        BLE_RELEASE[esp_bt_controller_mem_release<br/>ESP_ERROR_CHECK<br/>Abort on failure]
    end

    subgraph Subsystems["Subsystems (Graceful Degradation)"]
        BT_INIT[bt_manager_init<br/>Log error with esp_err_to_name<br/>Continue to audio]
        AUDIO_INIT[audio_processor_init<br/>Log error with diagnostics<br/>System still functional]
        CMD_INIT[cmd_init<br/>Log warning if failed<br/>Idempotent init OK]
    end

    BOOT[app_main start] --> NVS_INIT
    BOOT --> BLE_RELEASE
    BOOT --> UART_INIT

    NVS_INIT -->|Success| UART_INIT
    NVS_INIT -->|Failure| ABORT1[Abort: System cannot<br/>operate without NVS]

    BLE_RELEASE -->|Success| CMD_INIT
    BLE_RELEASE -->|Failure| ABORT2[Abort: Memory<br/>constraint violated]

    UART_INIT --> CMD_INIT
    CMD_INIT -->|Any result| BT_INIT

    BT_INIT -->|Success| AUDIO_INIT
    BT_INIT -->|Failure| PARTIAL1[Partial Mode:<br/>CMD + Audio work<br/>No BT connectivity]

    AUDIO_INIT -->|Success| FULL[Full System<br/>BT + Audio + CMD]
    AUDIO_INIT -->|Failure| PARTIAL2[Partial Mode:<br/>BT + CMD work<br/>Audio unavailable]

    CMD_INIT -->|Failure| PARTIAL3[Degraded:<br/>BT + Audio work<br/>No command control]

    style Platform fill:#ffebee,stroke:#c62828,stroke-width:2px
    style Subsystems fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px
    style ABORT1 fill:#ef5350,color:#fff
    style ABORT2 fill:#ef5350,color:#fff
    style FULL fill:#66bb6a,color:#fff
    style PARTIAL1 fill:#ffa726,color:#fff
    style PARTIAL2 fill:#ffa726,color:#fff
    style PARTIAL3 fill:#ffa726,color:#fff
```

## Layer Separation

This diagram shows the three-layer architecture:

```mermaid
graph TB
    subgraph Platform["Platform Layer (main.c owns)"]
        P1[Memory Management<br/>BLE mem release]
        P2[NVS Initialization<br/>Single call early]
        P3[UART Driver Install<br/>Diagnostics + CMD]
        P4[Early Boot Markers<br/>DIAG output]
        PLATFORM_POLICY[Policy: Fail-fast<br/>Once at boot<br/>ESP_ERROR_CHECK]
    end

    subgraph Policy["Policy Layer (main.c orchestrates)"]
        POL1[Init Order Decisions<br/>Control → Data plane]
        POL2[Configuration Loading<br/>NVS → Kconfig → fallback]
        POL3[Autostart Logic<br/>Check flag, decide init]
        POL4[Resource Allocation<br/>Audio defaults, pins]
        POLICY_POLICY[Policy: Configurable<br/>Runtime + Compile-time<br/>Documents intent]
    end

    subgraph Application["Application Layer (components implement)"]
        A1[bt_manager<br/>BT stack, A2DP/AVRCP<br/>State machines]
        A2[audio_processor<br/>I2S pipeline, DMA<br/>Audio queue]
        A3[cmd_handlers<br/>Command logic<br/>Event dispatch]
        A4[nvs_storage API<br/>Persistence helpers<br/>Get/set functions]
        APP_POLICY[Policy: Graceful<br/>Stateful + Complex<br/>Log + continue]
    end

    Main[main.c<br/>Thin Bootstrap<br/>319 lines] --> P1
    Main --> P2
    Main --> P3
    Main --> P4

    Main --> POL1
    Main --> POL2
    Main --> POL3
    Main --> POL4

    Main -.->|Calls init| A1
    Main -.->|Calls init| A2
    Main -.->|Calls init| A3

    P2 -.->|Assumes ready| A1
    P2 -.->|Assumes ready| A2
    P2 -.->|Assumes ready| A4
    P3 -.->|Assumes ready| A3

    A1 --> A4
    A2 --> A4
    A3 --> A4

    style Platform fill:#fff3e0,stroke:#e65100,stroke-width:2px
    style Policy fill:#f3e5f5,stroke:#6a1b9a,stroke-width:2px
    style Application fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px
    style Main fill:#e1f5ff,stroke:#01579b,stroke-width:3px
```

## Anti-Patterns to Avoid

```mermaid
graph LR
    subgraph Bad["❌ Anti-Patterns (DO NOT DO)"]
        BAD1[main.c calls<br/>esp_a2d_* directly<br/>Violates delegation]
        BAD2[bt_manager calls<br/>nvs_flash_init<br/>Violates ownership]
        BAD3[main.c calls<br/>uart_driver_delete<br/>Breaks subsystems]
        BAD4[Audio defaults<br/>in audio_processor<br/>Violates policy layer]
        BAD5[BT init before<br/>CMD init<br/>Violates control→data]
        BAD6[Multiple<br/>nvs_storage_init calls<br/>Violates single owner]
    end

    subgraph Good["✅ Correct Patterns"]
        GOOD1[main.c calls<br/>bt_manager_init<br/>Proper delegation]
        GOOD2[main.c owns<br/>nvs_storage_init<br/>Single ownership]
        GOOD3[UART installed once<br/>Never deleted<br/>Stable platform]
        GOOD4[Audio config<br/>in load_audio_boot_config<br/>Policy in main.c]
        GOOD5[CMD init then<br/>BT init<br/>Control → Data]
        GOOD6[Components assume<br/>NVS ready<br/>Clear dependency]
    end

    BAD1 -.->|Instead do| GOOD1
    BAD2 -.->|Instead do| GOOD2
    BAD3 -.->|Instead do| GOOD3
    BAD4 -.->|Instead do| GOOD4
    BAD5 -.->|Instead do| GOOD5
    BAD6 -.->|Instead do| GOOD6

    style Bad fill:#ffebee,stroke:#c62828,stroke-width:2px
    style Good fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px
```

## Notes

- All diagrams reflect the architecture as of **Feb 5, 2026** (post CODE_REVIEW6 ring buffer migration)
- **Major changes since Feb 1**:
  - SPSC ring buffer architecture completed and validated (CODE_REVIEW6)
  - Legacy audio_queue removed from codebase
  - All 259 host tests passing, clang-tidy clean
  - Duplicate DIAG-EVENT prints fixed in commands.c
- See ARCH.md for detailed technical documentation
- See README.md for user-facing configuration and usage
- See memory.md for rolling engineering log (CODE_REVIEW6 completion entry)
- See code_review/CODE_REVIEW2_TODO.md for implementation history
- These diagrams are rendered by GitHub/GitLab and most Markdown viewers with Mermaid support
