#include "unity.h"
#include "esp_system.h"

void app_main(void)
{
    /* Run Unity tests embedded in components. The test in
     * components/audio/test/test_audio_tags.c includes its own main when
     * built for host, but on-device we call Unity runner here. */

    UNITY_BEGIN();

    /* If component tests register via RUN_TEST or custom entrypoints, they
     * will be executed here. For simplicity, invoke unity run loop only.
     * The audio tag test has a main() guarded by CONFIG_BT_MOCK_TESTING; if
     * the component test is compiled in as a separate object it will run
     * automatically. */

    UNITY_END();

    /* Prevent exit */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
