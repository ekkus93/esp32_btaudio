/* CTRL-1a: host tests for the pure ctrl_cfg MAC validator. */
#include "unity.h"
#include "ctrl_cfg.h"

void setUp(void) {}
void tearDown(void) {}

static void test_valid_macs(void)
{
    TEST_ASSERT_TRUE(ctrl_cfg_mac_valid("A0:B7:65:2B:E6:5E"));
    TEST_ASSERT_TRUE(ctrl_cfg_mac_valid("00:00:00:00:00:00"));
    TEST_ASSERT_TRUE(ctrl_cfg_mac_valid("ff:ff:ff:ff:ff:ff"));  /* lowercase */
    TEST_ASSERT_TRUE(ctrl_cfg_mac_valid("aB:cD:eF:01:23:45"));  /* mixed case */
}

static void test_null_and_empty(void)
{
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid(NULL));
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid(""));
}

static void test_wrong_length(void)
{
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid("A0:B7:65:2B:E6"));      /* too short (5 pairs) */
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid("A0:B7:65:2B:E6:5E:99")); /* too long (7 pairs) */
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid("A0:B7:65:2B:E6:5"));    /* last pair 1 digit */
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid("A0:B7:65:2B:E6:5E0"));  /* trailing junk */
}

static void test_bad_separators(void)
{
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid("A0-B7-65-2B-E6-5E"));  /* dashes */
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid("A0:B7:65:2B:E65E"));   /* missing colon */
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid("A0B7:65:2B:E6:5E:00")); /* colon misplaced */
}

static void test_non_hex(void)
{
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid("A0:B7:65:2B:E6:5G"));  /* G not hex */
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid("ZZ:B7:65:2B:E6:5E"));  /* Z not hex */
    TEST_ASSERT_FALSE(ctrl_cfg_mac_valid("A0:B7:65:2B:E6: E"));  /* space */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_macs);
    RUN_TEST(test_null_and_empty);
    RUN_TEST(test_wrong_length);
    RUN_TEST(test_bad_separators);
    RUN_TEST(test_non_hex);
    return UNITY_END();
}
