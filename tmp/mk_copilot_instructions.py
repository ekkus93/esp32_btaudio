from textwrap import dedent

content = dedent("""\
    # GitHub Copilot Instructions — ESP32 (Espressif ESP‑IDF, C/C++)

    ## Scope & Environment
    - Platform: **ESP‑IDF** (C/C++), **FreeRTOS** runtime.
    - Build: **CMake + idf.py** (Ninja). **Do not** switch to PlatformIO/Make unless explicitly asked.
    - Project layout: `main/`, optional `components/<name>/`, top‑level `CMakeLists.txt`, `sdkconfig` and/or `sdkconfig.defaults`. **Respect existing structure.**
    - Targets: match repo (e.g., `esp32`, `esp32s3`, `esp32c3`). **Never change target** silently.
    - Language level: C99 by default. Prefer `static` internal linkage; use `const`/`volatile` appropriately.

    > If another `.github/copilot-instructions.md` exists, **merge** with these rules rather than replacing. Prefer repo‑specifics when in conflict.

    ---

    ## Agent‑mode compliance (MANDATORY)
    These rules apply to **Copilot Agent** as well as inline/chat. If an action would violate this file:
    1) **Stop** and post a clarification that cites the rule.
    2) **Do not proceed** until the user authorizes an exception.
    3) Prefer **asking** over assuming; never ignore MUST/NEVER rules.

    **Violation response (use verbatim):**
    ```text
    Cannot comply: requested action conflicts with repo policy — “[rule name/number]”.
    Proposed alternatives:
    1) [Option A — compliant]
    2) [Option B — minimal exception + impact]
    Please choose one or authorize an exception.
    ```

    **Ask‑first actions (confirmation required):**
    - Modifying **sdkconfig** / `sdkconfig.defaults`
    - Changing **chip target**, flash size/mode/frequency, partition table, bootloader/OTA scheme
    - Adding/removing **components** or third‑party libraries (including IDF Component Manager packages)
    - Introducing **new peripherals** or remapping pins
    - Changing **log levels**, disabling warnings, or suppressing `ESP_ERROR_CHECK`
    - Altering **power management**, clocking, sleep/deep‑sleep strategy
    - Enabling/altering **Wi‑Fi/BLE** features, credentials handling, or security (TLS certs, PSKs)

    ---

    ## Directive compliance (HIGHEST PRIORITY — MANDATORY)
    **User directives override convenience.** When the user specifies constraints (e.g., *“use hardware timers, not `vTaskDelay` loops”*), do **not** substitute alternatives.

    **Directive Acknowledgement Block (post before large changes):**
    ```text
    Directives understood:
    - [repeat constraints word‑for‑word]
    Implementation plan:
    - [brief plan that adheres to directives]
    Conflicts:
    - [empty OR list impossibilities + reason + proposed remedy]
    Proceeding per directives.
    ```

    **Non‑substitution rule (NEVER):**
    - Do **not** replace a mandated API/approach with another because it’s “easier.”
    - If the directive is impossible on the current target or IDF version, **stop** and use the Violation response. Do **not** auto‑downgrade.

    **Design‑choice locks (optional per task):**
    ```text
    Concurrency: ALLOWED = FreeRTOS tasks/queues; BANNED = busy‑wait loops
    Timing: ALLOWED = esp_timer; BANNED = arbitrary vTaskDelay polling
    Networking: ALLOWED = esp_http_client; BANNED = raw sockets (unless asked)
    Storage: ALLOWED = NVS; BANNED = hard‑coded creds in source
    ```

    ---

    ## Clarity over assumptions (MANDATORY)
    - If requirements or hardware details are **unclear**, do **not** guess. **Ask for clarification** first.
    - Avoid “bad defaults”: do not invent pin mappings, partition layouts, credentials, SSIDs, certificates, or peripherals.
    - For any ambiguity, provide both (a) the assumption you’d make and (b) a **request for confirmation** before expanding the change.
    - When choices exist, propose **≤3 options** with one‑line trade‑offs and wait for selection.

    **Clarification template:**
    ```text
    Clarification needed: [what’s unclear].
    Options:
    1) [Option A — pro/con]
    2) [Option B — pro/con]
    3) [Option C — pro/con]
    I recommend [A/B/C] because […]. Please confirm.
    ```

    ---

    ## Good design & architecture (MANDATORY)
    - Strive for **clean, maintainable, idiomatic** ESP‑IDF code — not quick hacks to silence warnings or “make it pass.”
    - Separation of concerns: **drivers** (peripheral access), **services** (logic), **app** (orchestration).
    - Keep ISRs **short** and IRAM‑safe; use `IRAM_ATTR` when required, and only call IRAM‑safe functions.
    - Use **queues/semaphores/notifications** for ISR↔task communication; no blocking from ISRs.
    - Prefer **event‑driven** patterns (esp_event) over polling loops.
    - Avoid tight coupling between unrelated components; export public headers via `include/` and `idf_component_register`.

    ---

    ## Dependency & configuration policy (MANDATORY)
    - Do **not** create or modify `sdkconfig` automatically; propose minimal diffs instead.
    - All third‑party components must be added explicitly (e.g., via **IDF Component Manager**) — no hidden vendoring.
    - No **conditional compilation** that silently removes functionality to make warnings go away.
    - No **hard‑coded credentials/URLs/pins** in source. Use NVS, Kconfig options, or a config header.

    ---

    ## Code validity (MANDATORY)
    - Suggestions must **compile** under `idf.py build` for the current target and IDF version.
    - Respect the repo’s **clang‑format/clang‑tidy** settings if present; otherwise format consistently.
    - Do **not** emit incomplete, mismatched, or mangled code blocks.
    - If unsure about ISR‑safety, memory caps, or task stack sizing, **ask first**.

    ---

    ## Working‑firmware policy (MANDATORY)
    - Primary goal: **fully implemented, working firmware** that runs end‑to‑end on the target board.
    - Do **not** propose placeholder drivers or stubs that “pass tests” but don’t exercise hardware.
    - Implement **complete behavior** per comments/specs; document any temporary limitations.

    ### Acceptance block (before larger changes)
    - **Behavior**: one sentence.
    - **Interfaces**: public headers/APIs, events.
    - **Resources**: tasks, timers, queues, ISR, heap/PSRAM use.
    - **Limits**: known constraints/unimplemented edges.

    ---

    ## Error handling & logging
    - Check all `esp_err_t` returns; prefer `ESP_ERROR_CHECK()` or explicit handling with `esp_err_to_name(err)`.
    - Use module log tags and `ESP_LOGx(TAG, …)`; avoid `printf`. Do not log from ISR unless safe.
    - **No silent fallbacks.** If init fails, propagate error; do not quietly bypass peripherals.

    ---

    ## FreeRTOS & timing
    - Create tasks with appropriate priority/stack; justify sizing.
    - Use `...FromISR` APIs in interrupts and `portYIELD_FROM_ISR()` as needed.
    - Use `esp_timer` or hardware timers for precise timing; avoid long `vTaskDelay` polling loops.
    - Never allocate with `malloc` in ISRs; pre‑allocate or use pools. For PSRAM, prefer `heap_caps_malloc()` with flags.

    ---

    ## Peripherals & storage
    - GPIO: configure pull‑ups/downs and interrupt type explicitly; debounce as needed.
    - I2C/SPI/UART: check bus/ACK/overruns; avoid busy‑wait loops.
    - Filesystems (SPIFFS/LittleFS/FFat): mount with checks; handle failures explicitly.
    - NVS: check every `nvs_*` call; version config if schema evolves.
    - Partition table: **do not change** without confirmation.

    ---

    ## CMake / component rules (propose, don’t auto‑edit)
    - Each component: `idf_component_register(SRCS … INCLUDE_DIRS … REQUIRES … PRIV_REQUIRES …)`.
    - Prefer `PRIV_REQUIRES` unless headers are needed by consumers.
    - Public headers in `components/<name>/include/<name>/…`.

    ---

    ## Anti‑paperclip rules (MANDATORY)
    0) **No stray configs/components** created just to quell warnings (no extra `sdkconfig`, dummy components, or alternate CMake trees).
    1) **Warnings are potential errors — fix root cause.** Don’t hide with `#pragma` disables or global log‑level changes (unless temporary and justified).
    2) **No silent fallbacks.** Degraded modes must be **opt‑in**, visibly logged, and documented.
    3) **Preserve functionality.** Don’t delete features to “make it pass”; refactor for clarity.
    4) **No stealth hard‑coded values.** Centralize pins/timing/creds; mark temporary values with `// TODO(<you>): externalize`.
    5) **Loose coupling.** Clear boundaries between components; no reach‑through into private headers.
    6) **Safety & integrity.** Validate parameters, buffer lengths, and concurrency; never block in ISRs.
    7) **Change‑proposal protocol:** output *Problem*, *Root cause*, *Minimal fix (≤10 lines)*, *Impact*, *Alternatives* before sweeping edits.
    8) **If uncertain… ask.** Prefer a question or minimal diff over broad speculative changes.

    ---

    ## Pre‑flight checklist (Agent & Chat)
    - [ ] **Directive Acknowledgement Block** posted and matches user constraints
    - [ ] No conflicts with MUST/NEVER rules; used **Violation response** if needed
    - [ ] Builds under `idf.py build` for current target; respects repo format/lint
    - [ ] No edits to `sdkconfig`, partition table, or target without confirmation
    - [ ] All `esp_err_t` paths checked; ISR‑safe code where required
    - [ ] No silent fallbacks; functionality preserved
    - [ ] Separation of concerns respected; component boundaries clean

    ---

    ## Example snippets

    **Component CMakeLists.txt**
    ```cmake
    idf_component_register(
        SRCS "driver_xyz.c"
        INCLUDE_DIRS "include"
        REQUIRES driver
        PRIV_REQUIRES freertos log
    )
    ```

    **GPIO ISR skeleton**
    ```c
    static xQueueHandle s_gpio_evt_queue;

    static void IRAM_ATTR gpio_isr(void* arg) {
        uint32_t io_num = (uint32_t) arg;
        xQueueSendFromISR(s_gpio_evt_queue, &io_num, NULL);
        // keep ISR short; no logging here
    }

    void gpio_init_button(gpio_num_t pin) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE
        };
        ESP_ERROR_CHECK(gpio_config(&io));
        s_gpio_evt_queue = xQueueCreate(8, sizeof(uint32_t));
        ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
        ESP_ERROR_CHECK(gpio_isr_handler_add(pin, gpio_isr, (void*)pin));
    }
    ```

    **Event‑driven Wi‑Fi STA init (sketch)**
    ```c
    static const char *TAG = "wifi";
    static EventGroupHandle_t s_wifi_event_group;
    #define WIFI_CONNECTED_BIT BIT0

    static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
        if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            const ip_event_got_ip_t* e = (ip_event_got_ip_t*)data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG, "Disconnected; retrying");
            esp_wifi_connect();
        }
    }

    esp_err_t wifi_init_sta(const char* ssid, const char* pass) {
        s_wifi_event_group = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
        wifi_config_t wifi_config = {0};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid)-1);
        strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password)-1);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
        return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
    }
    ```

    ---

    ## Optional CI guardrails (propose; do not auto‑enable)
    Propose stricter compilation only on request or when tied to a fix:
    ```cmake
    # root CMakeLists.txt (suggested)
    add_compile_options(-Wall -Wextra -Werror -Wformat=2 -Wshadow -Wconversion -Wsign-conversion)
    ```
    Explain what these catch, possible noise, and how to gate by env/CI.
    """)

path = "./copilot-instructions.md"
with open(path, "w") as f:
    f.write(content)

path
