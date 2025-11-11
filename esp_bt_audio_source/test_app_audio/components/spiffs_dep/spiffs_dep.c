/* Minimal component to pull in the IDF 'spiffs' component for test builds. */
#include "esp_log.h"

static const char *TAG = "spiffs_dep";

void spiffs_dep_dummy(void)
{
    ESP_LOGD(TAG, "spiffs_dep dummy init");
}
