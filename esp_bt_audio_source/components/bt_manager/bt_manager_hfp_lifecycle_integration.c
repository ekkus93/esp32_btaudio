#include "bt_manager.h"
#include "bt_hfp_ag.h"
#include "bt_duplex_state.h"

/*
 * This translation unit intentionally contains no independent lifecycle.
 * The authoritative integration lives in bt_manager.c so initialization and
 * rollback ordering remain visible in one place. This compile-time marker
 * prevents future code from quietly creating a second HFP owner.
 */
const char *bt_manager_hfp_lifecycle_owner(void)
{
    return "bt_manager.c";
}
