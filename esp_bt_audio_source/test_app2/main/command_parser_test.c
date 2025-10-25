// Device-side Unity test to exercise the real command parser on the ESP32.
#include "unity.h"
#include "command_interface.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// test_pairing_adapters.c provides this symbol; declare it here.
extern bool test_capture_event(char *out_buf, size_t out_len);

// Use the common setUp/tearDown defined in unity_config.c
// (do not redefine them here to avoid multiple-definition at link time)

void test_cmd_parse_and_execute_debug_mock_add(void)
{
    cmd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    // Use a representative DEBUG MOCK_ADD line that the host would send.
    const char *line = "DEBUG MOCK_ADD AA:BB:CC:DD:EE:FF 1";

    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(line, &ctx));

    printf("TEST-DIAG: type=%d param_count=%d first=%s\n", ctx.type, ctx.param_count, ctx.param_count > 0 ? ctx.params[0] : "<none>");

    // Execute the parsed command; the implementation should respond
    // and/or emit an EVENT that we can capture via test_capture_event().
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    // Try to capture events produced by the command. The adapter will
    // return true if an event was available. We don’t rely on exact
    // formatting, but assert that some event was produced within a
    // small window.
    char ev[256];
    bool got = false;
    for (int i = 0; i < 10; ++i) {
        if (test_capture_event(ev, sizeof(ev))) {
            got = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    TEST_ASSERT_MESSAGE(got, "No event captured after DEBUG MOCK_ADD execution");
}

// The test function is now invoked from the unified auto-runner
// (auto_run_tests.c). Keep this file as a collection of test helpers
// and test functions only; the single `app_main`/Unity harness is
// provided by the runner to avoid duplicate mains.

