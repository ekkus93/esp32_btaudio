// Unity test runner for test_app3. Invoke component-registered tests here.
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern void audio_pipeline_tests_register(void);
extern void pcm_processing_tests_register(void);

void app_main(void)
{
	UNITY_BEGIN();
	audio_pipeline_tests_register();
	pcm_processing_tests_register();
	UNITY_END();

	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
