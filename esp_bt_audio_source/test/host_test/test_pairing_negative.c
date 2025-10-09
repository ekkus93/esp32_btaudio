// Negative and edge-case tests for pairing reply flows
#include "unity.h"
#include "esp_bt.h"

extern void mock_gap_reset(void);
extern const char* mock_gap_get_last_mac(void);
extern int mock_gap_get_last_pin_len(void);
extern const char* mock_gap_get_last_pin(void);
extern int mock_gap_get_last_confirm(void);

void setUp(void) { mock_gap_reset(); }
void tearDown(void) { mock_gap_reset(); }

// Rejecting an SSP confirmation should record a zero confirm value
void test_ssp_confirm_reject_records_zero(void) {
    uint8_t bd_addr[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    int rc = esp_bt_gap_ssp_confirm_reply(bd_addr, false);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(0, mock_gap_get_last_confirm());
}

// Null bd_addr should clear last_mac and still return success (mock behavior)
void test_pin_reply_null_bdaddr_clears_mac(void) {
    uint8_t pin[] = { '9','9','9','9' };
    int rc = esp_bt_gap_pin_reply(NULL, true, sizeof(pin), pin);
    TEST_ASSERT_EQUAL_INT(0, rc);
    const char* mac = mock_gap_get_last_mac();
    TEST_ASSERT_NOT_NULL(mac);
    TEST_ASSERT_EQUAL_CHAR('\0', mac[0]);
}

// Empty PIN length should record zero-length pin
void test_pin_reply_zero_length_records_empty(void) {
    uint8_t bd_addr[6] = {0x10,0x20,0x30,0x40,0x50,0x60};
    int rc = esp_bt_gap_pin_reply(bd_addr, true, 0, NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(0, mock_gap_get_last_pin_len());
}

// Too-long PIN should be truncated to buffer size in mock
void test_pin_reply_too_long_truncates(void) {
    uint8_t bd_addr[6] = {0xFE,0xED,0xFA,0xCE,0x00,0x02};
    // Create a long pin > 32
    char longpin[64];
    for (int i = 0; i < (int)sizeof(longpin)-1; ++i) longpin[i] = 'A' + (i%26);
    longpin[sizeof(longpin)-1] = '\0';
    int rc = esp_bt_gap_pin_reply(bd_addr, true, (uint8_t)strlen(longpin), (uint8_t*)longpin);
    TEST_ASSERT_EQUAL_INT(0, rc);
    int pin_len = mock_gap_get_last_pin_len();
    TEST_ASSERT(pin_len <= 32);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ssp_confirm_reject_records_zero);
    RUN_TEST(test_pin_reply_null_bdaddr_clears_mac);
    RUN_TEST(test_pin_reply_zero_length_records_empty);
    RUN_TEST(test_pin_reply_too_long_truncates);
    return UNITY_END();
}
