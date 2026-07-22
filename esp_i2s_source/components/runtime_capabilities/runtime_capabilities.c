/* runtime_capabilities — see include/runtime_capabilities.h. */
#include "runtime_capabilities.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t        s_mtx;
static runtime_capabilities_t   s_caps;   /* zero-initialized: all-false until publish */

static SemaphoreHandle_t mtx(void)
{
    if (!s_mtx) {
        s_mtx = xSemaphoreCreateMutex();
    }
    return s_mtx;
}

void runtime_capabilities_publish(const runtime_capabilities_t *caps)
{
    if (!caps) return;
    SemaphoreHandle_t m = mtx();
    if (!m) return;
    xSemaphoreTake(m, portMAX_DELAY);
    s_caps = *caps;
    xSemaphoreGive(m);
}

void runtime_capabilities_get(runtime_capabilities_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    SemaphoreHandle_t m = mtx();
    if (!m) return;
    xSemaphoreTake(m, portMAX_DELAY);
    *out = s_caps;
    xSemaphoreGive(m);
}
