/* Add support for using the libuv loop in tests. */

#ifndef TEST_LOOP_H
#define TEST_LOOP_H

#include <uv.h>

#include "../../include/raft.h"

#include "munit.h"

/* Max n. of loop iterations ran by a single function call */
#define LOOP_MAX_RUN 10

#define FIXTURE_LOOP struct uv_loop_s loop

#define SETUP_LOOP                                                         \
    {                                                                      \
        int rv_;                                                           \
        rv_ = uv_replace_allocator(raft_malloc, raft_realloc, raft_calloc, \
                                   raft_free);                             \
        munit_assert_int(rv_, ==, 0);                                      \
        rv_ = uv_loop_init(&f->loop);                                      \
        munit_assert_int(rv_, ==, 0);                                      \
    }

#define TEAR_DOWN_LOOP                                                     \
    {                                                                      \
        int rv_;                                                           \
        int alive_ = uv_loop_alive(&f->loop);                              \
        if (alive_ != 0) {                                                 \
            LOOP_STOP;                                                     \
        }                                                                  \
        rv_ = uv_loop_close(&f->loop);                                     \
        if (rv_ != 0) {                                                    \
            uv_walk(&f->loop, test_loop_walk_cb, NULL);                    \
            munit_errorf("uv_loop_close: %s (%d)", uv_strerror(rv_), rv_); \
        }                                                                  \
        rv_ = uv_replace_allocator(malloc, realloc, calloc, free);         \
        munit_assert_int(rv_, ==, 0);                                      \
    }

/* Run the loop until there are no pending active handles or the given amount of
 * iterations is reached. */
#define LOOP_RUN(N)                                                       \
    {                                                                     \
        unsigned i_;                                                      \
        int rv__;                                                         \
        munit_assert_int(N, >, 0);                                        \
        for (i_ = 0; i_ < N; i_++) {                                      \
            rv__ = uv_run(&f->loop, UV_RUN_ONCE);                         \
            if (rv__ < 0) {                                               \
                munit_errorf("uv_run: %s (%d)", uv_strerror(rv__), rv__); \
            }                                                             \
            if (rv__ == 0) {                                              \
                break;                                                    \
            }                                                             \
        }                                                                 \
    }

/* Run the loop until the given function returns true.
 *
 * If the loop exhausts all active handles or if #LOOP_MAX_RUN is
 * reached without @F returning #true, the test fails. */
#define LOOP_RUN_UNTIL(F, DATA)                                              \
    {                                                                        \
        unsigned i_;                                                         \
        int rv_;                                                             \
        for (i_ = 0; i_ < LOOP_MAX_RUN; i_++) {                              \
            if (F(DATA)) {                                                   \
                break;                                                       \
            }                                                                \
            rv_ = uv_run(&f->loop, UV_RUN_ONCE);                             \
            if (rv_ < 0) {                                                   \
                munit_errorf("uv_run: %s (%d)", uv_strerror(rv_), rv_);      \
            }                                                                \
            if (rv_ == 0) {                                                  \
                if (F(DATA)) {                                               \
                    break;                                                   \
                }                                                            \
                munit_errorf("uv_run: stopped after %u iterations", i_ + 1); \
            }                                                                \
        }                                                                    \
        if (i_ == LOOP_MAX_RUN) {                                            \
            munit_errorf("uv_run: condition not met in %d iterations",       \
                         LOOP_MAX_RUN);                                      \
        }                                                                    \
    }

/* Run the loop until there are no pending active handles.
 *
 * If there are still pending active handles after LOOP_MAX_RUN iterations, the
 * test will fail.
 *
 * This is meant to be used in tear down functions. */
#define LOOP_STOP                                                 \
    {                                                             \
        int alive__;                                              \
        LOOP_RUN(LOOP_MAX_RUN);                                   \
        alive__ = uv_loop_alive(&f->loop);                        \
        if (alive__ != 0) {                                       \
            munit_error("loop has still pending active handles"); \
        }                                                         \
    }

void test_loop_walk_cb(uv_handle_t *handle, void *arg);

#endif /* TEST_LOOP_H */
