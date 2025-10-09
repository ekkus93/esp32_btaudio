// Simple host test for ENTER_PIN reply flow
#include <string.h>
#include <assert.h>
#include "unity.h"
#include "esp_bt.h"

extern void mock_gap_reset(void);
extern const char* mock_gap_get_last_mac(void);
extern int mock_gap_get_last_pin_len(void);
extern const char* mock_gap_get_last_pin(void);

void setUp(void) {}
void tearDown(void) { mock_gap_reset(); }

void test_enter_pin_reply_records_pin(void) {
    uint8_t bd_addr[6] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
    uint8_t pin[] = { '1','2','3','4' };
    int rc = esp_bt_gap_pin_reply(bd_addr, true, sizeof(pin), pin);
    TEST_ASSERT_EQUAL_INT(0, rc);

    const char* mac = mock_gap_get_last_mac();
    TEST_ASSERT_NOT_NULL(mac);

    int pin_len = mock_gap_get_last_pin_len();
    TEST_ASSERT_EQUAL_INT((int)sizeof(pin), pin_len);

    const char* last_pin = mock_gap_get_last_pin();
    TEST_ASSERT_EQUAL_STRING_LEN("1234", last_pin, 4);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_enter_pin_reply_records_pin);
    return UNITY_END();
}
