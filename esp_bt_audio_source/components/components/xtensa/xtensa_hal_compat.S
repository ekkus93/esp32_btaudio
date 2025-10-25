/*
 * Minimal Xtensa HAL fallbacks for CONFIG_APP_NO_BLOBS builds.
 * Provides implementations for window spill and extra state save/restore
 * routines that are normally supplied by the prebuilt libxt_hal.a blob.
 */

#include <xtensa/coreasm.h>
#include <xtensa/config/specreg.h>
#include <xtensa/config/tie.h>

#define UR_THREADPTR    231
#define UR_F64R_LO      234
#define UR_F64R_HI      235
#define UR_F64S         236

    .text
    .align  4

/* Spill all caller register windows without requiring privileged mode. */
    .global xthal_window_spill_nw
    .type   xthal_window_spill_nw, @function
xthal_window_spill_nw:
#if XCHAL_HAVE_WINDOWED
#if XCHAL_NUM_AREGS == 64
    entry           sp, 48
    bbci.l          a0, 31, 1f
    bbsi.l          a0, 30, 2f
    call4           spill_assist52
    retw
1:  call8           spill_assist52
    retw
2:  call12          spill_assist40
    retw
#elif XCHAL_NUM_AREGS == 32
    entry           sp, 48
    bbci.l          a0, 31, 1f
    bbsi.l          a0, 30, 2f
    call8           spill_assist16
    retw
1:  call12          spill_assist16
    retw
2:  call4           spill_assist16
    retw
#elif XCHAL_NUM_AREGS == 16
    entry           sp, 16
    bbci.l          a0, 31, 1f
    bbsi.l          a0, 30, 2f
    movi            a7, 0
    retw
1:  movi            a11, 0
2:  retw
#else
#error "Unsupported XCHAL_NUM_AREGS value"
#endif
#else
    ret
#endif

    .global xthal_window_spill
    .type   xthal_window_spill, @function
    .set    xthal_window_spill, xthal_window_spill_nw

#if XCHAL_HAVE_WINDOWED
#if XCHAL_NUM_AREGS == 64
    .align 4
spill_assist52:
    entry           sp, 48
    call12          spill_assist40
    retw

    .align 4
spill_assist40:
    entry           sp, 48
    call12          spill_assist28
    retw

    .align 4
spill_assist28:
    entry           sp, 48
    call12          spill_assist16
    retw

    .align 4
spill_assist16:
    entry           sp, 16
    movi            a15, 0
    retw
#elif XCHAL_NUM_AREGS == 32
    .align 4
spill_assist16:
    entry           sp, 16
    movi            a15, 0
    retw
#endif
#endif

#if XCHAL_NCP_SA_SIZE > 0
    .set    NCP_OFF_THREADPTR, 0
    .set    NCP_OFF_ACCLO,     4
    .set    NCP_OFF_ACCHI,     8
    .set    NCP_OFF_BR,        12
    .set    NCP_OFF_SCOMPARE1, 16
    .set    NCP_OFF_M0,        20
    .set    NCP_OFF_M1,        24
    .set    NCP_OFF_M2,        28
    .set    NCP_OFF_M3,        32
    .set    NCP_OFF_F64R_LO,   36
    .set    NCP_OFF_F64R_HI,   40
    .set    NCP_OFF_F64S,      44
#endif

/* Save non-coprocessor TIE state used by the FreeRTOS port. */
    .align  4
    .global xthal_save_extra_nw
    .type   xthal_save_extra_nw, @function
xthal_save_extra_nw:
#if XCHAL_NCP_SA_SIZE > 0
    /* Argument: a2 = base of the save area. Use caller-saved registers for temps. */
    rur             a3, UR_THREADPTR
    s32i           a3, a2, NCP_OFF_THREADPTR

    rsr            a3, ACCLO
    s32i           a3, a2, NCP_OFF_ACCLO
    rsr            a3, ACCHI
    s32i           a3, a2, NCP_OFF_ACCHI

    rsr            a3, BR
    s32i           a3, a2, NCP_OFF_BR
    rsr            a3, SCOMPARE1
    s32i           a3, a2, NCP_OFF_SCOMPARE1

    rsr            a3, MR_0
    s32i           a3, a2, NCP_OFF_M0
    rsr            a3, MR_1
    s32i           a3, a2, NCP_OFF_M1
    rsr            a3, MR_2
    s32i           a3, a2, NCP_OFF_M2
    rsr            a3, MR_3
    s32i           a3, a2, NCP_OFF_M3

    rur            a3, UR_F64R_LO
    s32i           a3, a2, NCP_OFF_F64R_LO
    rur            a3, UR_F64R_HI
    s32i           a3, a2, NCP_OFF_F64R_HI
    rur            a3, UR_F64S
    s32i           a3, a2, NCP_OFF_F64S
#endif
    ret

/* Restore the non-coprocessor TIE state from the save area. */
    .align  4
    .global xthal_restore_extra_nw
    .type   xthal_restore_extra_nw, @function
xthal_restore_extra_nw:
#if XCHAL_NCP_SA_SIZE > 0
    l32i           a3, a2, NCP_OFF_THREADPTR
    wur            a3, UR_THREADPTR

    l32i           a3, a2, NCP_OFF_ACCLO
    wsr            a3, ACCLO
    l32i           a3, a2, NCP_OFF_ACCHI
    wsr            a3, ACCHI

    l32i           a3, a2, NCP_OFF_BR
    wsr            a3, BR
    l32i           a3, a2, NCP_OFF_SCOMPARE1
    wsr            a3, SCOMPARE1

    l32i           a3, a2, NCP_OFF_M0
    wsr            a3, MR_0
    l32i           a3, a2, NCP_OFF_M1
    wsr            a3, MR_1
    l32i           a3, a2, NCP_OFF_M2
    wsr            a3, MR_2
    l32i           a3, a2, NCP_OFF_M3
    wsr            a3, MR_3

    l32i           a3, a2, NCP_OFF_F64R_LO
    wur            a3, UR_F64R_LO
    l32i           a3, a2, NCP_OFF_F64R_HI
    wur            a3, UR_F64R_HI
    l32i           a3, a2, NCP_OFF_F64S
    wur            a3, UR_F64S
#endif
    ret
