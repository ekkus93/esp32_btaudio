/**
 * @file unity_fixture.h
 * @brief Compatibility layer for Unity test framework
 */

#pragma once

#include "unity.h"

/* Unity Test Fixture API macros */
#define TEST_GROUP(name) \
    void setUp_##name(void); \
    void tearDown_##name(void); \
    static void test_##name##_run(void)

#define TEST_SETUP(name) void setUp_##name(void)
#define TEST_TEAR_DOWN(name) void tearDown_##name(void)

#define TEST(group, name) static void test_##group##_##name(void)

#define TEST_GROUP_RUNNER(group) void run_test_group_##group(void)
#define RUN_TEST_CASE(group, name) Unity.TestFile = __FILE__; UnityDefaultTestRun(test_##group##_##name, #name, __LINE__)
#define RUN_TEST_GROUP(group) run_test_group_##group()
