/*
 * runtime_capabilities — FIX3 10.1: a small, independently-linkable module
 * both main.c (the publisher, once per boot, from run_boot_sequence()'s
 * results) and web_ui/other components (readers) can depend on without a
 * circular requirement on "main". Web status and HTTP handlers must use
 * this rather than assuming every component initialized successfully.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool i2s;
    bool audio_task;
    bool bt_link;
    bool radio;
    bool stations;
    bool ctrl;
    bool wifi;
    bool web;
} runtime_capabilities_t;

/* Publish the boot-time result for every component. Call once, after
 * run_boot_sequence() finishes. Safe to call again (e.g. in a host test)
 * but there is normally only one publish per boot generation. */
void runtime_capabilities_publish(const runtime_capabilities_t *caps);

/* Snapshot the currently published capabilities. Before the first publish,
 * *out is all-false (never uninitialized garbage) — callers must treat
 * that as "not yet known / unavailable", not "still booting is fine". */
void runtime_capabilities_get(runtime_capabilities_t *out);

#ifdef __cplusplus
}
#endif
