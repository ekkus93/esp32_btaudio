## RH-S3-21 — Stop unsupported codec sessions explicitly

### Problem

When the HTTP stream returns an unsupported codec (e.g. `audio/ogg`, `video/mp4`), the stream task continues to read data and fill the compressed ring indefinitely. The decoder task spins waiting for a codec that never comes. The session runs forever with no useful output.

### Fix

**File: `esp_i2s_source/components/radio/radio.c`**

In `stream_task()`, after `s_codec = codec_from_ct(s_hdr_ct);` (line ~350), add a check:

```c
if (s_codec == RADIO_CODEC_UNKNOWN) {
    set_radio_error(RADIO_ERR_UNSUPPORTED_CONTENT, s_hdr_ct);
    ESP_LOGW(TAG, "unsupported codec: %s", s_hdr_ct);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    /* Terminate session — codec won't change on reconnect for same URL. */
    atomic_store_explicit(&s->stop_requested, true, memory_order_release);
    xSemaphoreTake(s_control_mtx, portMAX_DELAY);
    s_radio_state = RADIO_STATE_FAULTED;
    xSemaphoreGive(s_control_mtx);
    continue;  /* session_should_run() now false; outer loop exits */
}
```

This:
- Records the error so `radio_get_status()` reports `RADIO_ERR_UNSUPPORTED_CONTENT` + the content-type string
- Sets the stop flag so both workers exit cleanly
- Sets state to FAULTED so the caller knows it didn't complete normally
- Cleans up the HTTP client resources
- The `continue` re-evaluates `session_should_run(s)` which is now false, so the outer loop exits

**Test: `test_radio_unsupported_codec_fault`**

Since the host test mock `xTaskCreate` doesn't actually spawn threads, the stream task code never runs. The test verifies:
1. `RADIO_ERR_UNSUPPORTED_CONTENT` is a valid error code
2. `radio_get_status()` properly reports the error field
3. The codec field in status reflects `RADIO_CODEC_UNKNOWN`

The actual codec detection logic is verified by code inspection (the check is present in `stream_task`).

### Trade-off

Alternative: could also set the stop flag and then call `radio_stop_sync()` from within the stream task. But that's recursive/complex since `radio_stop_sync()` waits for the tasks to exit, and the stream task is one of those tasks. Setting the stop flag and fault state from within the task is simpler and correct — the next `radio_stop_sync()` call (from the command worker on stop/play) will handle the cleanup.

### Files modified

- `esp_i2s_source/components/radio/radio.c` — add codec check in `stream_task()`
- `esp_i2s_source/test/host_test/test_radio_lifecycle.c` — add error-reporting test