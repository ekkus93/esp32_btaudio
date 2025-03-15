#include "audio_pipeline.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

// If not defined in audio_pipeline.h, define dummy values.
#ifndef AUDIO_ELEMENT_TYPE_ELEMENT
#define AUDIO_ELEMENT_TYPE_ELEMENT 1
#endif
#ifndef AEL_MSG_CMD_REPORT_STATUS
#define AEL_MSG_CMD_REPORT_STATUS 2
#endif
#ifndef AEL_STATUS_STATE_FINISHED
#define AEL_STATUS_STATE_FINISHED 3
#endif

esp_err_t audio_pipeline_wait_for_event(audio_pipeline_handle_t pipeline,
                                          audio_event_iface_msg_t *msg,
                                          TickType_t timeout)
{
    (void)pipeline;  // Unused in stub implementation
    // For the indefinite wait timeout, wait 5000ms; otherwise, wait for the specified timeout.
    if (timeout == portMAX_DELAY) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    } else {
        vTaskDelay(timeout);
    }
    // Populate the event message with a dummy 'finished' event.
    msg->source_type = AUDIO_ELEMENT_TYPE_ELEMENT;
    msg->cmd = AEL_MSG_CMD_REPORT_STATUS;
    msg->data = (void *)AEL_STATUS_STATE_FINISHED;
    return ESP_OK;
}
