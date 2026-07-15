/*
 * test_bt_link_session — LINK-1b: in-flight tracking, verb correlation,
 * terminal completion, timeout, and EVENT fan-out (incl. interleaving).
 */
#include "unity.h"
#include "bt_link_session.h"
#include "bt_link_parser.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* helper: parse a literal into a msg (mutates a local copy) */
static void feed(bt_link_session_t *s, const char *literal)
{
    char buf[BT_LINK_LINE_MAX];
    strcpy(buf, literal);
    bt_link_msg_t m;
    bt_link_parse_line(buf, &m);
    bt_link_session_on_message(s, &m);
}

/* event capture */
typedef struct { int count; char last_cmd[32]; char last_data[64]; } ev_t;
static void ev_cb(void *ctx, const bt_link_msg_t *m)
{
    ev_t *e = (ev_t *)ctx;
    e->count++;
    strncpy(e->last_cmd, m->command, sizeof(e->last_cmd) - 1);
    strncpy(e->last_data, m->data, sizeof(e->last_data) - 1);
}

static void test_begin_pending_and_reject_second(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_IDLE, bt_link_session_state(&s));
    TEST_ASSERT_TRUE(bt_link_session_begin(&s, "VERSION"));
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_PENDING, bt_link_session_state(&s));
    TEST_ASSERT_FALSE(bt_link_session_begin(&s, "STATUS"));  /* one in flight */
}

static void test_terminal_ok_completes(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    bt_link_session_begin(&s, "VERSION");
    feed(&s, "OK|VERSION|0.2.0|");
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_DONE_OK, bt_link_session_state(&s));
    TEST_ASSERT_EQUAL_STRING("0.2.0", s.last_result);
    TEST_ASSERT_EQUAL_UINT32(1, s.completed_ok);
}

static void test_terminal_err_completes(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    bt_link_session_begin(&s, "CONNECT 30ED");   /* verb = CONNECT */
    feed(&s, "ERR|CONNECT|TIMEOUT|");
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_DONE_ERR, bt_link_session_state(&s));
    TEST_ASSERT_EQUAL_STRING("TIMEOUT", s.last_result);
}

static void test_mismatched_terminal_ignored(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    bt_link_session_begin(&s, "VERSION");
    feed(&s, "OK|OTHERCMD|whatever|");
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_PENDING, bt_link_session_state(&s));
}

static void test_info_does_not_complete(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    bt_link_session_begin(&s, "SCAN");
    feed(&s, "INFO|SCAN|ITEM|name=Buds");
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_PENDING, bt_link_session_state(&s));
    feed(&s, "OK|SCAN|DONE|");
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_DONE_OK, bt_link_session_state(&s));
}

static void test_event_fanout_to_subscribers(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    ev_t a = {0}, b = {0};
    TEST_ASSERT_EQUAL_INT(0, bt_link_session_subscribe(&s, ev_cb, &a));
    TEST_ASSERT_EQUAL_INT(0, bt_link_session_subscribe(&s, ev_cb, &b));
    feed(&s, "EVENT|PAIR|CONFIRM|123456");
    TEST_ASSERT_EQUAL_INT(1, a.count);
    TEST_ASSERT_EQUAL_INT(1, b.count);
    TEST_ASSERT_EQUAL_STRING("PAIR", a.last_cmd);
    TEST_ASSERT_EQUAL_STRING("123456", a.last_data);
    TEST_ASSERT_EQUAL_UINT32(1, s.events_dispatched);
}

static void test_info_fanout_to_subscribers(void)
{
    /* BTUI-1: INFO lines (scan results, paired items) fan out to subscribers
     * too — both async (no command pending) and during a pending command —
     * without completing it. */
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    ev_t a = {0};
    bt_link_session_subscribe(&s, ev_cb, &a);

    /* async INFO, no command pending */
    feed(&s, "INFO|SCAN|RESULT|A0:B7:65:2B:E6:5E,Echo Buds");
    TEST_ASSERT_EQUAL_INT(1, a.count);
    TEST_ASSERT_EQUAL_STRING("SCAN", a.last_cmd);
    TEST_ASSERT_EQUAL_STRING("A0:B7:65:2B:E6:5E,Echo Buds", a.last_data);
    TEST_ASSERT_EQUAL_UINT32(1, s.infos_dispatched);

    /* INFO during a pending command still fans out AND doesn't complete it */
    bt_link_session_begin(&s, "PAIRED");
    feed(&s, "INFO|PAIRED|ITEM|11:22:33:44:55:66,Speaker");
    TEST_ASSERT_EQUAL_INT(2, a.count);
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_PENDING, bt_link_session_state(&s));
    feed(&s, "OK|PAIRED|COUNT|1");
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_DONE_OK, bt_link_session_state(&s));
    TEST_ASSERT_EQUAL_UINT32(2, s.infos_dispatched);
    TEST_ASSERT_EQUAL_UINT32(0, s.events_dispatched);
}

static void test_event_interleaved_with_pending(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    ev_t a = {0};
    bt_link_session_subscribe(&s, ev_cb, &a);
    bt_link_session_begin(&s, "CONNECT");
    feed(&s, "EVENT|PAIR|CONFIRM|9");        /* event mid-command */
    TEST_ASSERT_EQUAL_INT(1, a.count);
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_PENDING, bt_link_session_state(&s));  /* still pending */
    feed(&s, "OK|CONNECT|DONE|");
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_DONE_OK, bt_link_session_state(&s));
}

static void test_subscribe_table_full(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    ev_t e = {0};
    for (int i = 0; i < BT_LINK_MAX_SUBSCRIBERS; i++)
        TEST_ASSERT_EQUAL_INT(0, bt_link_session_subscribe(&s, ev_cb, &e));
    TEST_ASSERT_EQUAL_INT(-1, bt_link_session_subscribe(&s, ev_cb, &e));
}

static void test_timeout_fires_once(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 100);
    bt_link_session_begin(&s, "PING");
    TEST_ASSERT_FALSE(bt_link_session_tick(&s, 60));   /* 60 < 100 */
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_PENDING, bt_link_session_state(&s));
    TEST_ASSERT_TRUE(bt_link_session_tick(&s, 60));    /* 120 >= 100 */
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_TIMEOUT, bt_link_session_state(&s));
    TEST_ASSERT_EQUAL_UINT32(1, s.timeouts);
    TEST_ASSERT_FALSE(bt_link_session_tick(&s, 60));   /* no longer pending */
}

static void test_session_reusable_after_completion(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    bt_link_session_begin(&s, "VERSION");
    feed(&s, "OK|VERSION|1|");
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_DONE_OK, bt_link_session_state(&s));
    /* a new command can be started once the previous one is done */
    TEST_ASSERT_TRUE(bt_link_session_begin(&s, "STATUS"));
    feed(&s, "OK|STATUS|IDLE|");
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_DONE_OK, bt_link_session_state(&s));
    TEST_ASSERT_EQUAL_STRING("IDLE", s.last_result);
    TEST_ASSERT_EQUAL_UINT32(2, s.completed_ok);
}

static void test_late_response_after_timeout_ignored(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 100);
    bt_link_session_begin(&s, "PING");
    bt_link_session_tick(&s, 100);   /* -> TIMEOUT */
    feed(&s, "OK|PING|PONG|");        /* arrives too late */
    TEST_ASSERT_EQUAL_INT(BT_LINK_CMD_TIMEOUT, bt_link_session_state(&s));
}

static void test_all_four_subscribers_receive_event(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    ev_t subs[4] = {{0},{0},{0},{0}};

    /* Subscribe all 4 slots. */
    for (int i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_INT(0, bt_link_session_subscribe(&s, ev_cb, &subs[i]));

    feed(&s, "EVENT|SCAN|RESULT|A0:B7:65:2B:E6:5E");

    /* All 4 should have received the event. */
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(1, subs[i].count);
        TEST_ASSERT_EQUAL_STRING("SCAN", subs[i].last_cmd);
    }
    TEST_ASSERT_EQUAL_UINT32(1, s.events_dispatched);
}

static void test_subscriber_count_tracking(void)
{
    bt_link_session_t s; bt_link_session_init(&s, 1000);
    ev_t e = {0};
    TEST_ASSERT_EQUAL_INT(0, s.n_subs);

    TEST_ASSERT_EQUAL_INT(0, bt_link_session_subscribe(&s, ev_cb, &e));
    TEST_ASSERT_EQUAL_INT(1, s.n_subs);

    TEST_ASSERT_EQUAL_INT(0, bt_link_session_subscribe(&s, ev_cb, &e));
    TEST_ASSERT_EQUAL_INT(2, s.n_subs);

    /* Fill remaining slots. */
    for (int i = 0; i < BT_LINK_MAX_SUBSCRIBERS - 2; i++)
        TEST_ASSERT_EQUAL_INT(0, bt_link_session_subscribe(&s, ev_cb, &e));
    TEST_ASSERT_EQUAL_INT(BT_LINK_MAX_SUBSCRIBERS, s.n_subs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_pending_and_reject_second);
    RUN_TEST(test_terminal_ok_completes);
    RUN_TEST(test_terminal_err_completes);
    RUN_TEST(test_mismatched_terminal_ignored);
    RUN_TEST(test_info_does_not_complete);
    RUN_TEST(test_event_fanout_to_subscribers);
    RUN_TEST(test_info_fanout_to_subscribers);
    RUN_TEST(test_event_interleaved_with_pending);
    RUN_TEST(test_subscribe_table_full);
    RUN_TEST(test_timeout_fires_once);
    RUN_TEST(test_session_reusable_after_completion);
    RUN_TEST(test_late_response_after_timeout_ignored);
    RUN_TEST(test_all_four_subscribers_receive_event);
    RUN_TEST(test_subscriber_count_tracking);
    return UNITY_END();
}
