/**
 * @file bt_app_utils.c
 * @brief Bluetooth utility functions implementation
 * 
 * This module provides utility functions used by the Bluetooth application,
 * including validation of Bluetooth MAC addresses and other helper functions
 * that support the Bluetooth connection and pairing process.
 */
#include "bluetooth/bt_app_utils.h"
#include <esp_bt_defs.h>
#include "custom_log.h"
#include "bluetooth/bt_app_global.h"

#define TAG "BT_APP_UTILS"

/**
 * @brief Validates a Bluetooth MAC address string
 * 
 * Checks if a string is a valid Bluetooth MAC address in the format xx:xx:xx:xx:xx:xx
 * where each xx is a hexadecimal value. If valid, the function also stores the
 * parsed binary MAC address in the global pending_pair_addr variable for later use.
 *
 * The function performs several validation checks:
 * - Verifies the string length is exactly 17 characters
 * - Ensures each hex digit is a valid hexadecimal character
 * - Confirms the separators are colons
 * - Converts the valid hex string to a binary MAC address
 *
 * @param mac_str String representation of MAC address to validate
 * @return true if MAC address is valid, false otherwise
 */
bool is_valid_mac(const char *mac_str) {
    esp_bd_addr_t bd_addr;
    
    // Log the MAC string for debugging
    SAFE_ESP_LOGI(TAG, "Validating MAC address: %s", mac_str);
    
    // Verify length - a valid MAC address string must be 17 chars (xx:xx:xx:xx:xx:xx)
    if (strlen(mac_str) != 17) {
        SAFE_ESP_LOGW(TAG, "Invalid MAC address length: %d (expected 17)", strlen(mac_str));
        return false;
    }
    
    // Manual parsing of each byte in the MAC address
    // Format should be xx:xx:xx:xx:xx:xx where xx is a hexadecimal number
    char hex_str[3] = {0}; // Buffer for 2 hex chars plus null terminator
    
    for (int i = 0; i < 6; i++) {
        // Extract the 2 hex characters for this byte
        hex_str[0] = mac_str[i*3];
        hex_str[1] = mac_str[i*3+1];
        hex_str[2] = '\0';
        
        // Verify each character is a valid hex digit
        for (int j = 0; j < 2; j++) {
            if (!((hex_str[j] >= '0' && hex_str[j] <= '9') || 
                  (hex_str[j] >= 'a' && hex_str[j] <= 'f') || 
                  (hex_str[j] >= 'A' && hex_str[j] <= 'F'))) {
                SAFE_ESP_LOGW(TAG, "Invalid hex character in MAC address: %c", hex_str[j]);
                return false;
            }
        }
        
        // Convert hex string to byte value
        char *endptr = NULL;
        bd_addr[i] = (uint8_t)strtol(hex_str, &endptr, 16);
        
        // Verify successful conversion
        if (endptr != hex_str + 2) {
            SAFE_ESP_LOGW(TAG, "Failed to parse hex value: %s", hex_str);
            return false;
        }
        
        // Check for colon separator (except after the last byte)
        if (i < 5 && mac_str[i*3+2] != ':') {
            SAFE_ESP_LOGW(TAG, "Missing colon separator in MAC address");
            return false;
        }
    }
    
    // If all validations pass, the MAC address is valid
    SAFE_ESP_LOGI(TAG, "MAC validated: %02x:%02x:%02x:%02x:%02x:%02x", 
                 bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
    
    // Store the validated MAC address for use in bluetooth_pair_device
    memcpy(pending_pair_addr, bd_addr, ESP_BD_ADDR_LEN);
    
    return true;
}

/**
 * @brief Wait for the Bluetooth stack to recover from congestion
 * 
 * This function ensures the Bluetooth stack has enough time to process 
 * pending operations before initiating new ones. It helps prevent
 * stack assertion failures during intensive operations like pairing.
 *
 * @param min_time_ms Minimum time to wait regardless of congestion state
 */
void bt_wait_for_stack_ready(uint32_t min_time_ms)
{
    bool ready = false;
    uint32_t wait_time = 0;
    
    while (!ready && wait_time < 3000) { // Maximum 3 second wait
        // Check if there's congestion or if minimum time hasn't passed
        if (s_l2cap_congestion_flag || wait_time < min_time_ms) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait_time += 50;
        } else {
            ready = true;
        }
    }
    
    if (!ready) {
        SAFE_ESP_LOGW(TAG, "Bluetooth stack still may be congested after waiting %u ms", (unsigned int)wait_time);
    } else if (wait_time > min_time_ms) {
        SAFE_ESP_LOGI(TAG, "Bluetooth stack ready after waiting %u ms", (unsigned int)wait_time);
    }
}