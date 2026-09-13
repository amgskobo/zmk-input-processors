/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Checks for the self-tests that run inside a native_sim build.
 *
 * Results are printed rather than logged: printk reaches the console at once
 * and in order, where a deferred log line can still be queued when the
 * process exits. A case passes when its PASS lines match the snapshot, so a
 * test that never ran fails as surely as a test that failed.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/printk.h>

struct runtime_test {
    const char *name;
    int failures;
};

#define RT_EXPECT_EQ(test, actual, expected, what)                                                 \
    do {                                                                                           \
        const int64_t actual_ = (int64_t)(actual);                                                 \
        const int64_t expected_ = (int64_t)(expected);                                             \
                                                                                                   \
        if (actual_ != expected_) {                                                                \
            printk("FAIL: %s: %s (line %d): got %lld, expected %lld\n", (test)->name, (what),      \
                   __LINE__, (long long)actual_, (long long)expected_);                            \
            (test)->failures++;                                                                    \
        }                                                                                          \
    } while (0)

#define RT_EXPECT_TRUE(test, expression, what) RT_EXPECT_EQ(test, !!(expression), 1, what)
#define RT_EXPECT_FALSE(test, expression, what) RT_EXPECT_EQ(test, !!(expression), 0, what)

static inline void rt_finish(const struct runtime_test *test) {
    if (test->failures == 0) {
        printk("PASS: %s\n", test->name);
    }
}
