#pragma once

/**
 * @file mock_gap.h
 * @brief Mock GAP (Generic Access Profile) functions for host testing
 * 
 * Provides mocks for esp_bt_gap_* functions including pairing and discovery.
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_gap_bt_api.h"

/* Pairing mock control functions */
void mock_gap_reset(void);
const char* mock_gap_get_last_mac(void);
int mock_gap_get_last_pin_len(void);
const char* mock_gap_get_last_pin(void);
int mock_gap_get_last_confirm(void);

/* Discovery mock control functions */
void mock_gap_set_start_discovery_result(esp_err_t result);
void mock_gap_set_cancel_discovery_result(esp_err_t result);
bool mock_gap_was_start_discovery_called(void);
bool mock_gap_was_cancel_discovery_called(void);
