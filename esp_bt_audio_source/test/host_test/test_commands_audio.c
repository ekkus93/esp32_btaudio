/* test_commands_audio.c — audio-group test bodies, split out of
 * test_commands.c; linked into the same test_commands executable. */
#include "test_commands_shared.h"

/* --- test_debug_log_sets_level_and_response --- */
void test_debug_log_sets_level_and_response(void) {
    mock_uart_reset_tx();
    esp_log_level_set("AUDIO_PROC", ESP_LOG_INFO);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DEBUG LOG AUDIO_PROC WARN", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    TEST_ASSERT_EQUAL_INT(ESP_LOG_WARN, esp_log_level_get("AUDIO_PROC"));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|DEBUG|LOG_SET|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "AUDIO_PROC:WARN"));
}

/* --- test_send_response --- */
void test_send_response(void) {
    mock_uart_reset_tx();
    
    cmd_send_response("OK", "SCAN", "COMPLETE", "2");
    
    // Check that the correct response was sent
    const char* tx_data = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx_data);
    TEST_ASSERT_EQUAL_STRING("OK|SCAN|COMPLETE|2\r\n", tx_data);
}

/* --- test_command_processing --- */
void test_command_processing(void) {
    // Inject command into mock UART
    mock_uart_inject_rx_data("SCAN\r\n", 6);
    
    // Process the command
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    
    // Verify response
    const char* tx_data = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx_data);
    
    // Note: In a real implementation, this would return scan results
    // For the test, we're just checking it sent some response
    TEST_ASSERT_TRUE(strstr(tx_data, "SCAN") != NULL);
}

/* --- test_cmd_process_handles_multiple_commands_in_one_read --- */
void test_cmd_process_handles_multiple_commands_in_one_read(void) {
    mock_uart_reset_tx();

    /* Two commands arrive in a single UART read; both should be parsed and executed. */
    mock_uart_inject_rx_data("SCAN\r\nSTATUS\r\n", strlen("SCAN\r\nSTATUS\r\n"));

    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|SCAN"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|STATUS"));
}

/* --- test_cmd_process_accumulates_partial_line_across_calls --- */
void test_cmd_process_accumulates_partial_line_across_calls(void) {
    mock_uart_reset_tx();

    /* First call provides no terminator; nothing should be emitted. */
    mock_uart_inject_rx_data("VOLUME 10", strlen("VOLUME 10"));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    const char* tx_after_partial = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx_after_partial);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)strlen(tx_after_partial));

    /* Second call supplies the newline so the buffered command runs. */
    mock_uart_inject_rx_data("\r\n", 2);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|VOLUME|MOCK_SET|10"));
}

/* --- test_cmd_process_recovers_after_overflow_reset --- */
void test_cmd_process_recovers_after_overflow_reset(void) {
    mock_uart_reset_tx();

    /* Fill most of the line buffer without a newline to trigger the overflow path. */
    char filler1[250];
    memset(filler1, 'A', sizeof(filler1));
    mock_uart_inject_rx_data(filler1, sizeof(filler1));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    /* No response expected yet. */
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)strlen(mock_uart_get_tx_data()));

    /* Next read includes a valid command plus extra data to force line_len + to_copy past the buffer size.
     * cmd_process should reset the buffer and still execute the SCAN command. */
    char overflow_payload[260];
    memset(overflow_payload, 'B', sizeof(overflow_payload));
    const char cmd_str[] = "SCAN\r\n";
    memcpy(overflow_payload, cmd_str, sizeof(cmd_str) - 1);
    mock_uart_inject_rx_data(overflow_payload, sizeof(overflow_payload));

    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "SCAN"));
}

/* --- test_help_command --- */
void test_help_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("HELP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    /* The number of commands may change over time; parse the declared
     * count from the SUMMARY line and verify it matches the number of
     * ENTRY lines emitted. This keeps the test resilient to additions
     * or removals of help entries. */
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|HELP|SUMMARY|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|HELP|FORMAT|COMMAND [ARGS] - DESCRIPTION"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|HELP|ENTRY|HELP - Show this list"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|HELP|ENTRY|CONNECT <MAC> - Connect by MAC address"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HELP|DONE|"));

    int entry_count = count_substring(tx, "INFO|HELP|ENTRY|");
    /* Extract the numeric count from the SUMMARY line and compare. */
    const char* sumptr = strstr(tx, "INFO|HELP|SUMMARY|");
    TEST_ASSERT_NOT_NULL(sumptr);
    sumptr += strlen("INFO|HELP|SUMMARY|");
    int declared = -1;
    if (sumptr) {
        /* Expect format: "<N> commands available" */
        sscanf(sumptr, "%d commands available", &declared);
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, declared);
    TEST_ASSERT_EQUAL(declared, entry_count);
}

/* --- test_version_command --- */
void test_version_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("VERSION", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|VERSION|TEST-HOST-VERSION|"));
}

/* --- test_diag_command_reports_state --- */
void test_diag_command_reports_state(void) {
    mock_uart_reset_tx();
    bt_manager_test_set_connection_state(1);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DIAG", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|DIAG|STATE|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "CONN=1"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "I2S=1"));

    (void)audio_processor_stop();
    (void)audio_processor_deinit();
}

/* --- test_volume_invalid_param_should_error --- */
void test_volume_invalid_param_should_error(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("VOLUME 200", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|VOLUME|OUT_OF_RANGE"));
}

/* --- test_volume_missing_param_should_error --- */
void test_volume_missing_param_should_error(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("VOLUME", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|VOLUME|MISSING_PARAM"));
}

/* --- test_i2s_config_invalid_rate_should_error --- */
void test_i2s_config_invalid_rate_should_error(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("I2S_CONFIG 26,25,22,21 12345", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|I2S_CONFIG|INVALID_RATE"));
}

/* --- test_i2s_config_invalid_bit_depth_should_error --- */
void test_i2s_config_invalid_bit_depth_should_error(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("I2S_CONFIG 26,25,22,21 48000 20", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|I2S_CONFIG|INVALID_BIT_DEPTH"));
}

/* --- test_i2s_config_invalid_channels_should_error --- */
void test_i2s_config_invalid_channels_should_error(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("I2S_CONFIG 26,25,22,21 48000 16 3", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|I2S_CONFIG|INVALID_CHANNELS"));
}

/* --- test_debug_mock_add_missing_param_errors --- */
void test_debug_mock_add_missing_param_errors(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DEBUG MOCK_ADD", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|DEBUG|MOCK_ADD_MISSING"));
}

/* --- test_status_name_cmd_success --- */
void test_status_name_cmd_success(void) {
    const char *name = cmd_status_to_name(CMD_SUCCESS);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}

/* --- test_status_name_cmd_error_init_failed --- */
void test_status_name_cmd_error_init_failed(void) {
    const char *name = cmd_status_to_name(CMD_ERROR_INIT_FAILED);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}

/* --- test_status_name_cmd_error_invalid_param --- */
void test_status_name_cmd_error_invalid_param(void) {
    const char *name = cmd_status_to_name(CMD_ERROR_INVALID_PARAM);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}

/* --- test_status_name_cmd_error_unknown --- */
void test_status_name_cmd_error_unknown(void) {
    const char *name = cmd_status_to_name(CMD_ERROR_UNKNOWN);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}

/* --- test_status_name_cmd_error_not_initialized --- */
void test_status_name_cmd_error_not_initialized(void) {
    const char *name = cmd_status_to_name(CMD_ERROR_NOT_INITIALIZED);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}

/* --- test_status_name_cmd_error_too_many_params --- */
void test_status_name_cmd_error_too_many_params(void) {
    const char *name = cmd_status_to_name(CMD_ERROR_TOO_MANY_PARAMS);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}

/* --- test_status_name_out_of_range_returns_non_null --- */
void test_status_name_out_of_range_returns_non_null(void) {
    /* Unknown status codes should return a non-null fallback, not crash */
    const char *name = cmd_status_to_name((cmd_status_t)999);
    TEST_ASSERT_NOT_NULL(name);
}

/* --- test_mute_unmute_command --- */
void test_mute_unmute_command(void) {
    mock_uart_reset_tx();
    // Mute
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("MUTE", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "MUTE") != NULL);

    mock_uart_reset_tx();
    // Unmute
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNMUTE", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "UNMUTE") != NULL);
}

/* --- test_sample_rate_command --- */
void test_sample_rate_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SAMPLE_RATE 48000", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "SAMPLE_RATE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "48000") != NULL || strstr(tx, "MOCK_APPLIED") != NULL);
}

/* --- test_status_command --- */
void test_status_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STATUS", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "STATUS") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MUTE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "SAMPLE_RATE") != NULL || strstr(tx, "SAMPLE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "PAIRED_COUNT") != NULL || strstr(tx, "COUNT") != NULL);
    /* When streaming info is available, should include streaming stats */
    TEST_ASSERT_TRUE(strstr(tx, "BYTES_REQ") != NULL || strstr(tx, "STREAM_INFO") != NULL);
}

/* --- test_status_command_streaming_info_unavailable --- */
void test_status_command_streaming_info_unavailable(void) {
    extern void bt_manager_test_force_streaming_info_failure(bool force);
    
    bt_manager_test_force_streaming_info_failure(true);
    
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STATUS", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    /* Should still return basic status */
    TEST_ASSERT_TRUE(strstr(tx, "STATUS") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MUTE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "SAMPLE_RATE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "PAIRED_COUNT") != NULL);
    /* Should indicate streaming info is unavailable */
    TEST_ASSERT_TRUE(strstr(tx, "STREAM_INFO=UNAVAILABLE") != NULL);
    /* Should NOT include detailed streaming stats */
    TEST_ASSERT_TRUE(strstr(tx, "BYTES_REQ") == NULL);
    TEST_ASSERT_TRUE(strstr(tx, "UNDERRUNS") == NULL);
    
    /* Clean up */
    bt_manager_test_force_streaming_info_failure(false);
}

/* --- test_reset_command --- */
void test_reset_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("RESET", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "RESET") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MOCK_REBOOT") != NULL || strstr(tx, "REBOOTING") != NULL);
}

/* --- test_start_command --- */
void test_start_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("START", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "START") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MOCK_STARTED") != NULL || strstr(tx, "STARTED") != NULL || strstr(tx, "FAILED") != NULL);
}

/* --- test_start_command_stops_beep_and_enables_i2s --- */
void test_start_command_stops_beep_and_enables_i2s(void) {
    mock_uart_reset_tx();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 50,
        .mute = false,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(500, 440.0));
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("START", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "START") != NULL);

    TEST_ASSERT_TRUE(audio_processor_is_i2s_active());
    TEST_ASSERT_FALSE(audio_processor_is_beep_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    (void)audio_processor_drain_ring();
}

/* --- test_stop_command --- */
void test_stop_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STOP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "STOP") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MOCK_STOPPED") != NULL || strstr(tx, "STOPPED") != NULL || strstr(tx, "FAILED") != NULL);
}

/* --- test_start_failure_command --- */
void test_start_failure_command(void) {
    mock_uart_reset_tx();
    extern void bt_manager_test_set_force_start_failure(int v);
    extern void bt_manager_test_reset_forces(void);
    bt_manager_test_set_force_start_failure(1);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("START", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "START") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "ERR") != NULL || strstr(tx, "FAILED") != NULL);

    bt_manager_test_reset_forces();
}

/* --- test_stop_failure_command --- */
void test_stop_failure_command(void) {
    mock_uart_reset_tx();
    extern void bt_manager_test_set_force_stop_failure(int v);
    extern void bt_manager_test_reset_forces(void);
    bt_manager_test_set_force_stop_failure(1);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STOP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "STOP") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "ERR") != NULL || strstr(tx, "FAILED") != NULL);

    bt_manager_test_reset_forces();
}

/* --- test_beep_command_allowed_when_i2s_active --- */
void test_beep_command_allowed_when_i2s_active(void) {
    mock_uart_reset_tx();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 50,
        .mute = false,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());
    /* Disable synth to model live I2S capture so the BEEP busy guard trips. */
    audio_processor_set_synth_mode(false);
    TEST_ASSERT_TRUE(audio_processor_is_i2s_active());

    extern void bt_manager_mock_connection_established(const char* mac, const char* name);
    bt_manager_mock_connection_established("aa:bb:cc:11:22:33", "MockSpeaker");

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tx, "OK|BEEP|SENT"), tx);
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    (void)audio_processor_drain_ring();
}

/* --- test_beep_command_busy_when_beep_active --- */
void test_beep_command_busy_when_beep_active(void) {
    mock_uart_reset_tx();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 50,
        .mute = false,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&cfg));
    /* Seed an active beep so the subsequent BEEP command hits the busy guard. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(1000, 440.0));
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    extern void bt_manager_mock_connection_established(const char* mac, const char* name);
    bt_manager_mock_connection_established("aa:bb:cc:11:22:33", "MockSpeaker");

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    const char *expected = "ERR|BEEP|BUSY|BEEP_ACTIVE";
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tx, expected), tx);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    (void)audio_processor_drain_ring();
}

/* --- test_synth_on_command --- */
void test_synth_on_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SYNTH ON", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|SYNTH|ENABLED"));
}

/* --- test_synth_off_command --- */
void test_synth_off_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SYNTH OFF", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|SYNTH|DISABLED"));
}

/* --- test_diag_i2s_stop_clears_i2s_flag --- */
void test_diag_i2s_stop_clears_i2s_flag(void) {
    mock_uart_reset_tx();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 50,
        .mute = false,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());
    audio_processor_set_synth_mode(false);
    TEST_ASSERT_TRUE(audio_processor_is_i2s_active());

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DIAG I2S_STOP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|DIAG|I2S_STOPPED"));
    TEST_ASSERT_FALSE(audio_processor_is_i2s_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}

