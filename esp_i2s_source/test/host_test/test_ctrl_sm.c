/* CTRL-1b: host tests for the pure boot-orchestrator state machine. */
#include "unity.h"
#include "ctrl_sm.h"

void setUp(void) {}
void tearDown(void) {}

/* --- step helpers --- */
static ctrl_action_t tick(ctrl_sm_t *sm, uint32_t dt)
{
    ctrl_input_t in = { .ev = CTRL_EV_TICK, .dt_ms = dt };
    return ctrl_sm_step(sm, &in);
}
static ctrl_action_t wifi_up(ctrl_sm_t *sm)
{
    ctrl_input_t in = { .ev = CTRL_EV_WIFI_UP };
    return ctrl_sm_step(sm, &in);
}
static ctrl_action_t status(ctrl_sm_t *sm, bool connected)
{
    ctrl_input_t in = { .ev = CTRL_EV_STATUS, .connected = connected };
    return ctrl_sm_step(sm, &in);
}
static ctrl_action_t connect_ack(ctrl_sm_t *sm, bool ok)
{
    ctrl_input_t in = { .ev = CTRL_EV_CONNECT_ACK, .ok = ok };
    return ctrl_sm_step(sm, &in);
}
static ctrl_action_t start_ack(ctrl_sm_t *sm, bool ok)
{
    ctrl_input_t in = { .ev = CTRL_EV_START_ACK, .ok = ok };
    return ctrl_sm_step(sm, &in);
}
static ctrl_action_t resume_done(ctrl_sm_t *sm)
{
    ctrl_input_t in = { .ev = CTRL_EV_RESUME_DONE };
    return ctrl_sm_step(sm, &in);
}

/* advance TICKs in `step` increments until at least `total` ms elapsed,
 * returning the first non-WAIT action seen (or WAIT). */
static ctrl_action_t run_ticks(ctrl_sm_t *sm, uint32_t total, uint32_t step)
{
    for (uint32_t t = 0; t < total; t += step) {
        ctrl_action_t a = tick(sm, step);
        if (a != CTRL_ACT_WAIT) return a;
    }
    return CTRL_ACT_WAIT;
}

/* --- tests --- */

static void test_autostart_off_is_idle(void)
{
    ctrl_sm_t sm;
    ctrl_sm_init(&sm, /*autostart*/false, /*have_station*/true);
    TEST_ASSERT_EQUAL(CTRL_ST_IDLE, sm.state);
    TEST_ASSERT_EQUAL(CTRL_ACT_IDLE, wifi_up(&sm));   /* never orchestrates */
    TEST_ASSERT_EQUAL(CTRL_ST_IDLE, sm.state);
}

static void test_happy_path_connect_start_resume(void)
{
    ctrl_sm_t sm;
    ctrl_sm_init(&sm, true, /*have_station*/true);
    TEST_ASSERT_EQUAL(CTRL_ST_WAIT_WIFI, sm.state);

    TEST_ASSERT_EQUAL(CTRL_ACT_SEND_STATUS, wifi_up(&sm));
    TEST_ASSERT_EQUAL(CTRL_ST_QUERY, sm.state);

    TEST_ASSERT_EQUAL(CTRL_ACT_SEND_CONNECT, status(&sm, false));
    TEST_ASSERT_EQUAL(CTRL_ST_CONNECTING, sm.state);

    TEST_ASSERT_EQUAL(CTRL_ACT_WAIT, connect_ack(&sm, true));
    TEST_ASSERT_EQUAL(CTRL_ST_CONNECT_WAIT, sm.state);

    /* settle delay elapses -> START */
    TEST_ASSERT_EQUAL(CTRL_ACT_SEND_START, run_ticks(&sm, 5000, 500));
    TEST_ASSERT_EQUAL(CTRL_ST_STARTING, sm.state);

    TEST_ASSERT_EQUAL(CTRL_ACT_RESUME_RADIO, start_ack(&sm, true));
    TEST_ASSERT_EQUAL(CTRL_ST_RESUMING, sm.state);

    TEST_ASSERT_EQUAL(CTRL_ACT_WAIT, resume_done(&sm));
    TEST_ASSERT_EQUAL(CTRL_ST_RUNNING, sm.state);
}

static void test_already_connected_skips_connect(void)
{
    ctrl_sm_t sm;
    ctrl_sm_init(&sm, true, true);
    wifi_up(&sm);
    TEST_ASSERT_EQUAL(CTRL_ACT_SEND_START, status(&sm, true)); /* connected at QUERY */
    TEST_ASSERT_EQUAL(CTRL_ST_STARTING, sm.state);
}

static void test_no_station_goes_running_without_resume(void)
{
    ctrl_sm_t sm;
    ctrl_sm_init(&sm, true, /*have_station*/false);
    wifi_up(&sm);
    status(&sm, true);                 /* -> STARTING */
    TEST_ASSERT_EQUAL(CTRL_ACT_WAIT, start_ack(&sm, true));
    TEST_ASSERT_EQUAL(CTRL_ST_RUNNING, sm.state);   /* no RESUME */
}

static void test_start_fails_then_retry(void)
{
    ctrl_sm_t sm;
    ctrl_sm_init(&sm, true, true);
    wifi_up(&sm);
    status(&sm, false);                /* -> CONNECTING */
    connect_ack(&sm, true);            /* -> CONNECT_WAIT */
    /* settle -> START */
    TEST_ASSERT_EQUAL(CTRL_ACT_SEND_START, run_ticks(&sm, 5000, 500));
    /* link never came up: START fails -> BACKOFF */
    TEST_ASSERT_EQUAL(CTRL_ACT_WAIT, start_ack(&sm, false));
    TEST_ASSERT_EQUAL(CTRL_ST_BACKOFF, sm.state);

    /* backoff (5s default) elapses -> retry CONNECT, retries incremented */
    TEST_ASSERT_EQUAL(CTRL_ACT_SEND_CONNECT, run_ticks(&sm, 6000, 500));
    TEST_ASSERT_EQUAL(CTRL_ST_CONNECTING, sm.state);
    TEST_ASSERT_EQUAL(1, sm.retries);
}

static void test_connect_rejected_backs_off(void)
{
    ctrl_sm_t sm;
    ctrl_sm_init(&sm, true, true);
    wifi_up(&sm);
    status(&sm, false);
    TEST_ASSERT_EQUAL(CTRL_ACT_WAIT, connect_ack(&sm, false)); /* rejected */
    TEST_ASSERT_EQUAL(CTRL_ST_BACKOFF, sm.state);
}

static void test_running_detects_drop_and_reconnects(void)
{
    ctrl_sm_t sm;
    ctrl_sm_init(&sm, true, false);
    wifi_up(&sm);
    status(&sm, true);
    start_ack(&sm, true);
    TEST_ASSERT_EQUAL(CTRL_ST_RUNNING, sm.state);

    /* health poll fires, reports disconnected -> BACKOFF -> reconnect */
    TEST_ASSERT_EQUAL(CTRL_ACT_SEND_STATUS, run_ticks(&sm, 3500, 500));
    TEST_ASSERT_EQUAL(CTRL_ACT_WAIT, status(&sm, false));
    TEST_ASSERT_EQUAL(CTRL_ST_BACKOFF, sm.state);
    TEST_ASSERT_EQUAL(CTRL_ACT_SEND_CONNECT, run_ticks(&sm, 6000, 500));
}

static void test_running_healthy_poll_resets_retries(void)
{
    ctrl_sm_t sm;
    ctrl_sm_init(&sm, true, false);
    wifi_up(&sm);
    status(&sm, true);
    start_ack(&sm, true);
    sm.retries = 3;                    /* pretend we recovered after retries */
    run_ticks(&sm, 3500, 500);         /* -> SEND_STATUS */
    status(&sm, true);                 /* healthy */
    TEST_ASSERT_EQUAL(CTRL_ST_RUNNING, sm.state);
    TEST_ASSERT_EQUAL(0, sm.retries);
}

static void test_gives_up_after_max_retries(void)
{
    ctrl_sm_t sm;
    ctrl_sm_init(&sm, true, true);
    sm.cfg.max_retries = 2;
    wifi_up(&sm);
    status(&sm, false);
    connect_ack(&sm, false);           /* -> BACKOFF (attempt 0) */

    int connects = 0;
    for (int i = 0; i < 100; i++) {
        ctrl_action_t a = tick(&sm, 1000);
        if (a == CTRL_ACT_SEND_CONNECT) { connects++; connect_ack(&sm, false); }
        if (sm.state == CTRL_ST_GAVEUP) break;
    }
    TEST_ASSERT_EQUAL(CTRL_ST_GAVEUP, sm.state);
    TEST_ASSERT_EQUAL(2, connects);    /* exactly max_retries attempts, then giveup */
    TEST_ASSERT_EQUAL(CTRL_ACT_IDLE, tick(&sm, 1000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_autostart_off_is_idle);
    RUN_TEST(test_happy_path_connect_start_resume);
    RUN_TEST(test_already_connected_skips_connect);
    RUN_TEST(test_no_station_goes_running_without_resume);
    RUN_TEST(test_start_fails_then_retry);
    RUN_TEST(test_connect_rejected_backs_off);
    RUN_TEST(test_running_detects_drop_and_reconnects);
    RUN_TEST(test_running_healthy_poll_resets_retries);
    RUN_TEST(test_gives_up_after_max_retries);
    return UNITY_END();
}
