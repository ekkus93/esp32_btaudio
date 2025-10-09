// Simple host test for SSP confirm reply flow
#include <string.h>
#include <assert.h>
#include "unity.h"
#include "esp_bt.h"

// Prototypes for helper inspection functions provided by mock_gap.c
extern void mock_gap_reset(void);
extern const char* mock_gap_get_last_mac(void);
extern int mock_gap_get_last_pin_len(void);
extern const char* mock_gap_get_last_pin(void);
extern int mock_gap_get_last_confirm(void);

void setUp(void) {}
void tearDown(void) { mock_gap_reset(); }

void test_ssp_confirm_reply_sets_last_confirm(void) {
    // Simulate a BD address and call the confirm reply path used by commands.c
    uint8_t bd_addr[6] = {0xDE,0xAD,0xBE,0xEF,0x00,0x01};

    // Call the mock SSP confirm reply directly (commands.c will call into esp_bt API which is mocked)
    int rc = esp_bt_gap_ssp_confirm_reply(bd_addr, true);
    TEST_ASSERT_EQUAL_INT(0, rc);

    const char* mac = mock_gap_get_last_mac();
    TEST_ASSERT_NOT_NULL(mac);
    TEST_ASSERT_EQUAL_CHAR('\0', mac[17]);

    int confirm = mock_gap_get_last_confirm();
    TEST_ASSERT_EQUAL_INT(1, confirm);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ssp_confirm_reply_sets_last_confirm);
    return UNITY_END();
}
