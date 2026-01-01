#include "unity.h"
#include "esp_system.h"

void app_main(void)
{
    /* Run Unity tests embedded in components. Component tests register their
     * entrypoints; invoke the Unity runner to execute them on-device. */

    UNITY_BEGIN();

    /* Component tests register via RUN_TEST or custom entrypoints; invoke
     * the Unity run loop. */

    UNITY_END();

    /* Prevent exit */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
