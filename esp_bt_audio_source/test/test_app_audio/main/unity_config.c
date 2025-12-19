#include "unity_config.h"
#include <stddef.h>
#include "unity.h"

void (*unity_setup_function)(void) = NULL;
void (*unity_teardown_function)(void) = NULL;

void unity_set_setup_function(void (*setup)(void))
{
    unity_setup_function = setup;
}

void unity_set_teardown_function(void (*teardown)(void))
{
    unity_teardown_function = teardown;
}

void setUp(void)
{
    if (unity_setup_function != NULL) {
        unity_setup_function();
    }
}

void tearDown(void)
{
    if (unity_teardown_function != NULL) {
        unity_teardown_function();
    }
}
