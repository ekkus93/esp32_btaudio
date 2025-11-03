// Transition shim: redirect legacy `bt_init()` calls to the new
// `test_bt_manager_init()` helper. This header is a thin compatibility
// layer so we can remove the legacy `test_bt_init.*` sources safely.
#ifndef TEST_BT_INIT_H
#define TEST_BT_INIT_H

#include "test_helpers.h" /* provides prototype for test_bt_manager_init() */

/* Redirect legacy bt_init() calls to the new helper. */
#ifndef bt_init
#define bt_init() test_bt_manager_init()
#endif

#endif // TEST_BT_INIT_H
