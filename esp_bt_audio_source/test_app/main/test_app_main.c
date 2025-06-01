#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"  // Standard Unity header (available in ESP-IDF)
#include "bt_source.h"
#include "esp_log.h"

// Forward declarations for all test functions
static void test_func_1(void);  // "Bluetooth scan starts successfully"
static void test_func_2(void);  // "Bluetooth connects to A2DP sink"
static void test_func_3(void);  // "A2DP starts and stops streaming"
static void test_func_4(void);  // "Bluetooth disconnects properly"
static void test_func_5(void);  // "A2DP remembers paired devices"
static void test_func_6(void);  // "Bluetooth stack initializes successfully"
static void test_func_7(void);  // "Parse SCAN command"
static void test_func_8(void);  // "Parse CONNECT command"
static void test_func_9(void);  // "Bluetooth scan reports discovered devices"
static void test_func_10(void); // "Bluetooth scan filters by device type"
static void test_func_11(void); // "Bluetooth scanning basic functionality"
static void test_func_12(void); // "Bluetooth scan filters devices by type"
static void test_func_13(void); // "Bluetooth scan returns device details"
static void test_func_14(void); // "Bluetooth scan times out properly"
static void test_func_15(void); // "Bluetooth scan can be stopped early"
static void test_func_16(void); // "Connect to a device by address"
static void test_func_17(void); // "Connect to a device by name"
static void test_func_18(void); // "Handle connection failure gracefully"
static void test_func_19(void); // "Handle connection timeout"
static void test_func_20(void); // "Get connection status information"
static void test_func_21(void); // "Auto-reconnect when connection drops"
static void reset_bt_state(void);

// Helper function to display available tests
static void show_test_menu(void)
{
    printf("Here's the test menu, pick your combo:\n");
    printf("(1)     \"Bluetooth scan starts successfully\" [bluetooth][a2dp]\n");
    printf("(2)     \"Bluetooth connects to A2DP sink\" [bluetooth][a2dp]\n");
    printf("(3)     \"A2DP starts and stops streaming\" [bluetooth][a2dp]\n");
    printf("(4)     \"Bluetooth disconnects properly\" [bluetooth][a2dp]\n");
    printf("(5)     \"A2DP remembers paired devices\" [bluetooth][a2dp]\n");
    printf("(6)     \"Bluetooth stack initializes successfully\" [bluetooth]\n");
    printf("(7)     \"Parse SCAN command\" [commands]\n");
    printf("(8)     \"Parse CONNECT command\" [commands]\n");
    printf("(9)     \"Bluetooth scan reports discovered devices\" [bluetooth][a2dp][scan]\n");
    printf("(10)    \"Bluetooth scan filters by device type\" [bluetooth][a2dp][scan]\n");
    printf("(11)    \"Bluetooth scanning basic functionality\" [bluetooth][a2dp][scan]\n");
    printf("(12)    \"Bluetooth scan filters devices by type\" [bluetooth][a2dp][scan]\n");
    printf("(13)    \"Bluetooth scan returns device details\" [bluetooth][a2dp][scan]\n");
    printf("(14)    \"Bluetooth scan times out properly\" [bluetooth][a2dp][scan]\n");
    printf("(15)    \"Bluetooth scan can be stopped early\" [bluetooth][a2dp][scan]\n");
    printf("(16)    \"Connect to a device by address\" [bluetooth][a2dp][connection]\n");
    printf("(17)    \"Connect to a device by name\" [bluetooth][a2dp][connection]\n");
    printf("(18)    \"Handle connection failure gracefully\" [bluetooth][a2dp][connection]\n");
    printf("(19)    \"Handle connection timeout\" [bluetooth][a2dp][connection]\n");
    printf("(20)    \"Get connection status information\" [bluetooth][a2dp][connection]\n");
    printf("(21)    \"Auto-reconnect when connection drops\" [bluetooth][a2dp][connection]\n");
}

// Helper function to reset BT state between tests
static void reset_bt_state(void)
{
    // Ensure we're disconnected
    if (bt_is_connected()) {
        bt_disconnect();
    }
    
    // Stop streaming if active
    if (bt_is_streaming()) {
        bt_stop_streaming();
    }
    
    // Additional cleanup as needed
    bt_scan_stop();
    
    // Small delay to allow state transitions to complete
    vTaskDelay(pdMS_TO_TICKS(100));
}

// Add these helper functions at the beginning of the file
static void report_test_start(const char *test_name) {
    printf("===== RUNNING TEST: %s =====\n", test_name);
}

static void report_test_result(bool passed, const char *test_name) {
    if (passed) {
        printf("===== TEST PASSED: %s =====\n", test_name);
    } else {
        printf("===== TEST FAILED: %s =====\n", test_name);
    }
    printf("\n");
}

void app_main(void)
{
    // Initialize Unity test framework
    UNITY_BEGIN();
    
    int test_num = 0;
    char input[16] = {0};
    
    // Show the menu once at startup
    show_test_menu();
    
    while (1) {
        // Clear prompt and show it once
        printf("Enter test number: ");
        fflush(stdout);
        
        // Wait for actual input - use a blocking approach with timeout
        // that works with UART on ESP32
        int input_len = 0;
        bool input_ready = false;
        
        // Set longer timeout for input
        vTaskDelay(pdMS_TO_TICKS(500));  // Give time for prompt to appear
        
        while (!input_ready) {
            // Check if there's data available
            int c = getchar();
            if (c != EOF && c != 0xFF) {
                if (c == '\n' || c == '\r') {
                    // End of input
                    input[input_len] = '\0';
                    input_ready = true;
                    putchar('\n'); // Echo newline
                } else {
                    // Add character to input buffer
                    if (input_len < sizeof(input) - 1) {
                        input[input_len++] = (char)c;
                        putchar(c); // Echo character
                    }
                }
            } else {
                // No data available, wait a bit
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        
        // If we have valid input, process it
        if (input_len > 0) {
            // Try to parse as a number
            if (sscanf(input, "%d", &test_num) == 1) {
                printf("Running test %d...\n", test_num);
                
                // Reset state before running a test
                reset_bt_state();
                
                // Run the selected test
                switch (test_num) {
                    case 1:
                        test_func_1();
                        break;
                    case 2:
                        test_func_2();
                        break;
                    case 3:
                        test_func_3();
                        break;
                    case 4:
                        test_func_4();
                        break;
                    case 5:
                        test_func_5();
                        break;
                    case 6:
                        test_func_6();
                        break;
                    case 7:
                        test_func_7();
                        break;
                    case 8:
                        test_func_8();
                        break;
                    case 9:
                        test_func_9();
                        break;
                    case 10:
                        test_func_10();
                        break;
                    case 11:
                        test_func_11();
                        break;
                    case 12:
                        test_func_12();
                        break;
                    case 13:
                        test_func_13();
                        break;
                    case 14:
                        test_func_14();
                        break;
                    case 15:
                        test_func_15();
                        break;
                    case 16:
                        test_func_16();
                        break;
                    case 17:
                        test_func_17();
                        break;
                    case 18:
                        test_func_18();
                        break;
                    case 19:
                        test_func_19();
                        break;
                    case 20:
                        test_func_20();
                        break;
                    case 21:
                        test_func_21();
                        break;
                    default:
                        printf("Test %d not found\n", test_num);
                        break;
                }
            } else if (input_len == 0) {
                // Empty input, show menu
                show_test_menu();
            } else {
                printf("Invalid input. Please enter a number.\n");
            }
            
            // Clear the input buffer for next iteration
            memset(input, 0, sizeof(input));
            input_len = 0;
        }
        
        // Short delay to prevent CPU hogging
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // This won't be reached, but included for completeness
    UNITY_END();
}

// Test function implementations

// Test 1: "Bluetooth scan starts successfully"
static void test_func_1(void)
{
    printf("Running Bluetooth scan starts successfully...\n");
    
    // Test BT initialization
    esp_err_t init_result = bt_init();
    TEST_ASSERT_EQUAL(ESP_OK, init_result);
    
    // Test scan start
    esp_err_t scan_result = bt_scan_start();
    TEST_ASSERT_EQUAL(ESP_OK, scan_result);
    
    // Stop scan to clean up
    bt_scan_stop();
}

// Test 2: "Bluetooth connects to A2DP sink"
static void test_func_2(void)
{
    printf("Running Bluetooth connects to A2DP sink...\n");
    
    // Initialize BT
    bt_init();
    
    // Start scan to discover devices
    bt_scan_start();
    vTaskDelay(pdMS_TO_TICKS(2000));
    bt_scan_stop();
    
    // Connect to the first mock device
    esp_err_t connect_result = bt_connect("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, connect_result);
    
    // Verify connection state
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Disconnect to clean up
    bt_disconnect();
    TEST_ASSERT_FALSE(bt_is_connected());
}

// Test 3: "A2DP starts and stops streaming"
static void test_func_3(void)
{
    printf("Running A2DP starts and stops streaming...\n");
    
    // Initialize and connect
    bt_init();
    bt_connect("11:22:33:44:55:66");
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Start streaming
    esp_err_t start_result = bt_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, start_result);
    
    // Verify streaming state
    TEST_ASSERT_TRUE(bt_is_streaming());
    
    // Stop streaming
    esp_err_t stop_result = bt_stop_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, stop_result);
    
    // Verify streaming state
    TEST_ASSERT_FALSE(bt_is_streaming());
    
    // Disconnect to clean up
    bt_disconnect();
}

// Test 4: "Bluetooth disconnects properly"
static void test_func_4(void)
{
    printf("Running Bluetooth disconnects properly...\n");
    
    // Initialize and connect
    bt_init();
    bt_connect("11:22:33:44:55:66");
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Test disconnect
    esp_err_t result = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_FALSE(bt_is_connected());
}

// Test 9: "Bluetooth scan reports discovered devices"
static void test_func_9(void)
{
    bool test_passed = true;
    report_test_start("Bluetooth scan reports discovered devices");
    
    // Initialize BT
    bt_init();
    
    // Start scan
    bt_scan_start();
    
    // Wait for scan to collect devices
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Stop scan
    bt_scan_stop();
    
    // Check number of discovered devices
    uint16_t count = bt_get_discovered_device_count();
    test_passed &= (count > 0);
    printf("Discovered devices: %d\n", count);
    
    // Get the discovered devices
    bt_device_t devices[10];
    uint16_t returned_count = 0;
    esp_err_t result = bt_get_discovered_devices(devices, 10, &returned_count);
    
    test_passed &= (result == ESP_OK);
    test_passed &= (count == returned_count);
    
    report_test_result(test_passed, "Bluetooth scan reports discovered devices");
}

// Test 17: "Connect to a device by name"
static void test_func_17(void)
{
    bool test_passed = true;
    report_test_start("Connect to a device by name");
    
    ESP_LOGI("BT_CONN_TEST", "Testing connection by name");
    
    // Initialize BT
    bt_init();
    
    // Start scanning to discover devices
    bt_scan_start();
    
    // Wait for scan to complete
    vTaskDelay(pdMS_TO_TICKS(2000));
    bt_scan_stop();
    
    // Connect by name
    const char* test_name = "Mock Headphones";
    esp_err_t err = bt_connect_by_name(test_name);
    
    // Verify connection success
    test_passed &= (err == ESP_OK);
    test_passed &= bt_is_connected();
    
    // Clean up
    bt_disconnect();
    test_passed &= !bt_is_connected();
    
    report_test_result(test_passed, "Connect to a device by name");
}

// Implement stubs for remaining tests
// You can add more implementations as needed

static void test_func_5(void) { printf("Test 5 not fully implemented\n"); }
static void test_func_6(void) { printf("Test 6 not fully implemented\n"); }
static void test_func_7(void) { printf("Test 7 not fully implemented\n"); }
static void test_func_8(void) { printf("Test 8 not fully implemented\n"); }
static void test_func_10(void) { printf("Test 10 not fully implemented\n"); }
static void test_func_11(void) { printf("Test 11 not fully implemented\n"); }
static void test_func_12(void) { printf("Test 12 not fully implemented\n"); }
static void test_func_13(void) { printf("Test 13 not fully implemented\n"); }
static void test_func_14(void) { printf("Test 14 not fully implemented\n"); }
static void test_func_15(void) { printf("Test 15 not fully implemented\n"); }
static void test_func_16(void) { printf("Test 16 not fully implemented\n"); }
static void test_func_18(void) { printf("Test 18 not fully implemented\n"); }
static void test_func_19(void) { printf("Test 19 not fully implemented\n"); }
static void test_func_20(void) { printf("Test 20 not fully implemented\n"); }
static void test_func_21(void) { printf("Test 21 not fully implemented\n"); }