#include "../../include/raft/fixture.h"

#include "../lib/fsm.h"
#include "../lib/runner.h"

TEST_MODULE(fixture);

#define N_SERVERS 3

/******************************************************************************
 *
 * Fixture
 *
 *****************************************************************************/

struct fixture
{
    struct raft_fsm fsms[N_SERVERS];
    struct raft_fixture fixture;
};

static void *setup(const MunitParameter params[], void *user_data)
{
    struct fixture *f = munit_malloc(sizeof *f);
    struct raft_configuration configuration;
    unsigned i;
    int rc;
    (void)user_data;
    (void)params;
    for (i = 0; i < N_SERVERS; i++) {
        test_fsm_setup(params, &f->fsms[i]);
    }

    rc = raft_fixture_init(&f->fixture, N_SERVERS, f->fsms);
    munit_assert_int(rc, ==, 0);

    rc = raft_fixture_configuration(&f->fixture, N_SERVERS, &configuration);
    munit_assert_int(rc, ==, 0);

    rc = raft_fixture_bootstrap(&f->fixture, &configuration);
    munit_assert_int(rc, ==, 0);

    raft_configuration_close(&configuration);

    rc = raft_fixture_start(&f->fixture);
    munit_assert_int(rc, ==, 0);

    return f;
}

static void tear_down(void *data)
{
    struct fixture *f = data;
    unsigned i;
    raft_fixture_close(&f->fixture);
    for (i = 0; i < N_SERVERS; i++) {
        test_fsm_tear_down(&f->fsms[i]);
    }
    free(f);
}

/******************************************************************************
 *
 * Helper macros
 *
 *****************************************************************************/

#define GET(I) raft_fixture_get(&f->fixture, I)
#define STEP raft_fixture_step(&f->fixture)
#define STEP_N(N) raft_fixture_step_n(&f->fixture, N)
#define STEP_UNTIL_STATE_IS(I, STATE)                                          \
    {                                                                          \
        bool done_;                                                            \
        done_ = raft_fixture_step_until_state_is(&f->fixture, I, STATE, 2000); \
        munit_assert_true(done_);                                              \
    }
#define STATE(I) raft_state(GET(I))
#define ELECT(I) raft_fixture_elect(&f->fixture, I)
#define DEPOSE raft_fixture_depose(&f->fixture)
#define APPLY(I, REQ)                                \
    {                                                \
        struct raft_buffer buf;                      \
        int rc;                                      \
        test_fsm_encode_add_x(1, &buf);              \
        rc = raft_apply(GET(I), REQ, &buf, 1, NULL); \
        munit_assert_int(rc, ==, 0);                 \
    }
#define STEP_UNTIL_APPLIED(INDEX) \
    raft_fixture_step_until_applied(&f->fixture, N_SERVERS, INDEX, INDEX * 1000)

/******************************************************************************
 *
 * Assertions
 *
 *****************************************************************************/

/* Assert that the fixture time matches the given value */
#define ASSERT_TIME(TIME) \
    munit_assert_int(raft_fixture_time(&f->fixture), ==, TIME)

/* Assert that the I'th server is in the given state. */
#define ASSERT_STATE(I, S) munit_assert_int(STATE(I), ==, S)

/* Assert that the x field of the FSM with the given index matches the given
 * value. */
#define ASSERT_FSM_X(I, VALUE) \
    munit_assert_int(test_fsm_get_x(&f->fsms[I]), ==, VALUE)

/******************************************************************************
 *
 * raft_fixture_step
 *
 *****************************************************************************/

TEST_SUITE(step);
TEST_SETUP(step, setup);
TEST_TEAR_DOWN(step, tear_down);

/* If there is no disk I/O in progress or network messages in flight, the tick
 * callbacks are called. */
TEST_CASE(step, tick, NULL)
{
    struct fixture *f = data;
    struct raft_fixture_event *event;
    (void)params;

    ASSERT_TIME(0);

    event = STEP;
    munit_assert_int(event->server_index, ==, 0);
    munit_assert_int(event->type, ==, RAFT_FIXTURE_TICK);
    ASSERT_TIME(100);

    event = STEP;
    munit_assert_int(event->server_index, ==, 1);
    munit_assert_int(event->type, ==, RAFT_FIXTURE_TICK);
    ASSERT_TIME(100);

    event = STEP;
    munit_assert_int(event->server_index, ==, 2);
    munit_assert_int(event->type, ==, RAFT_FIXTURE_TICK);
    ASSERT_TIME(100);

    event = STEP;
    munit_assert_int(event->server_index, ==, 0);
    munit_assert_int(event->type, ==, RAFT_FIXTURE_TICK);
    ASSERT_TIME(200);

    return MUNIT_OK;
}

/* By default the election timeout of server 0 is the first to expire . */
TEST_CASE(step, election_timeout, NULL)
{
    struct fixture *f = data;
    struct raft_fixture_event *event;
    (void)params;
    event = STEP_N(28);
    munit_assert_int(event->server_index, ==, 0);
    munit_assert_int(event->type, ==, RAFT_FIXTURE_TICK);
    ASSERT_TIME(1000);
    ASSERT_STATE(0, RAFT_CANDIDATE);
    ASSERT_STATE(1, RAFT_FOLLOWER);
    ASSERT_STATE(2, RAFT_FOLLOWER);
    munit_log(MUNIT_LOG_INFO, "done");
    return MUNIT_OK;
}

/* Send requests are flushed immediately. */
TEST_CASE(step, flush_send, NULL)
{
    struct fixture *f = data;
    struct raft_fixture_event *event;
    (void)params;
    STEP_UNTIL_STATE_IS(0, RAFT_CANDIDATE);
    event = STEP;
    munit_assert_int(event->server_index, ==, 0);
    munit_assert_int(event->type, ==, RAFT_FIXTURE_NETWORK);
    ASSERT_TIME(1000);
    event = STEP;
    munit_assert_int(event->server_index, ==, 0);
    munit_assert_int(event->type, ==, RAFT_FIXTURE_NETWORK);
    ASSERT_TIME(1000);
    return MUNIT_OK;
}

/* Messages are delivered according to the current network latency. */
TEST_CASE(step, deliver, NULL)
{
    struct fixture *f = data;
    struct raft_fixture_event *event;
    (void)params;
    STEP_UNTIL_STATE_IS(0, RAFT_CANDIDATE); /* Server 0 starts election */
    STEP_N(2);                              /* Server 0 sends 2 RequestVote */
    STEP_N(2);                              /* Ticks for server 1 and 2 */
    ASSERT_TIME(1000);
    event = STEP;
    munit_assert_int(event->server_index, ==, 0);
    munit_assert_int(event->type, ==, RAFT_FIXTURE_NETWORK);
    ASSERT_TIME(1015);
    return MUNIT_OK;
}

/******************************************************************************
 *
 * raft_fixture_elect
 *
 *****************************************************************************/

TEST_SUITE(elect);
TEST_SETUP(elect, setup);
TEST_TEAR_DOWN(elect, tear_down);

/* Trigger the election of the first server. */
TEST_CASE(elect, first, NULL)
{
    struct fixture *f = data;
    (void)params;
    ELECT(0);
    ASSERT_STATE(0, RAFT_LEADER);
    ASSERT_STATE(1, RAFT_FOLLOWER);
    ASSERT_STATE(2, RAFT_FOLLOWER);
    return MUNIT_OK;
}

/* Trigger the election of the second server. */
TEST_CASE(elect, second, NULL)
{
    struct fixture *f = data;
    (void)params;
    ELECT(1);
    ASSERT_STATE(0, RAFT_FOLLOWER);
    ASSERT_STATE(1, RAFT_LEADER);
    ASSERT_STATE(2, RAFT_FOLLOWER);
    return MUNIT_OK;
}

/* Trigger an election change. */
TEST_CASE(elect, change, NULL)
{
    struct fixture *f = data;
    (void)params;
    ELECT(0);
    DEPOSE;
    ASSERT_STATE(0, RAFT_FOLLOWER);
    ASSERT_STATE(1, RAFT_FOLLOWER);
    ASSERT_STATE(2, RAFT_FOLLOWER);
    ELECT(1);
    ASSERT_STATE(0, RAFT_FOLLOWER);
    ASSERT_STATE(1, RAFT_LEADER);
    ASSERT_STATE(2, RAFT_FOLLOWER);
    return MUNIT_OK;
}

/* Trigger an election that re-elects the same node. */
TEST_CASE(elect, again, NULL)
{
    struct fixture *f = data;
    (void)params;
    ELECT(0);
    DEPOSE;
    ASSERT_STATE(0, RAFT_FOLLOWER);
    ASSERT_STATE(1, RAFT_FOLLOWER);
    ASSERT_STATE(2, RAFT_FOLLOWER);
    ELECT(0);
    ASSERT_STATE(0, RAFT_LEADER);
    ASSERT_STATE(1, RAFT_FOLLOWER);
    ASSERT_STATE(2, RAFT_FOLLOWER);
    return MUNIT_OK;
}

/******************************************************************************
 *
 * raft_fixture_step_until_applied
 *
 *****************************************************************************/

TEST_SUITE(step_until_applied);
TEST_SETUP(step_until_applied, setup);
TEST_TEAR_DOWN(step_until_applied, tear_down);

/* Wait for one entry to be applied. */
TEST_CASE(step_until_applied, one, NULL)
{
    struct fixture *f = data;
    struct raft_apply *req = munit_malloc(sizeof *req);
    (void)params;
    ELECT(0);
    APPLY(0, req);
    STEP_UNTIL_APPLIED(2);
    ASSERT_FSM_X(0, 1);
    ASSERT_FSM_X(1, 1);
    ASSERT_FSM_X(2, 1);
    free(req);
    return MUNIT_OK;
}

/* Wait for two entries to be applied. */
TEST_CASE(step_until_applied, two, NULL)
{
    struct fixture *f = data;
    struct raft_apply *req1 = munit_malloc(sizeof *req1);
    struct raft_apply *req2 = munit_malloc(sizeof *req2);
    (void)params;
    ELECT(0);
    APPLY(0, req1);
    APPLY(0, req2);
    STEP_UNTIL_APPLIED(3);
    ASSERT_FSM_X(0, 2);
    ASSERT_FSM_X(1, 2);
    ASSERT_FSM_X(2, 2);
    free(req1);
    free(req2);
    return MUNIT_OK;
}
