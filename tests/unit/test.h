/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The assertion harness shared by the contract tests.
 *
 * Every test file is a program of its own, and a failure stops it at the first
 * wrong value. The message carries the inputs that produced that value, which
 * is what makes a failing sweep over thousands of inputs debuggable.
 */

#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

static unsigned long test_checks;

/* The trailing arguments are a printf format and its values, naming the case. */
#define EXPECT_EQ(actual, expected, ...)                                                           \
    do {                                                                                           \
        const int64_t actual_ = (int64_t)(actual);                                                 \
        const int64_t expected_ = (int64_t)(expected);                                             \
                                                                                                   \
        test_checks++;                                                                             \
        if (actual_ != expected_) {                                                                \
            fprintf(stderr, "%s:%d: ", __FILE__, __LINE__);                                        \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, ": got %" PRId64 ", expected %" PRId64 "\n", actual_, expected_);      \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    } while (0)

#define EXPECT_TRUE(expression, ...) EXPECT_EQ(!!(expression), 1, __VA_ARGS__)
#define EXPECT_FALSE(expression, ...) EXPECT_EQ(!!(expression), 0, __VA_ARGS__)

/* xorshift64*, seeded by each caller: a failing input reproduces on every run. */
static inline uint64_t test_random(uint64_t *state) {
    uint64_t x = *state;

    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;

    return x * UINT64_C(0x2545F4914F6CDD1D);
}

static inline int test_report(void) {
    printf("%lu checks passed\n", test_checks);

    return EXIT_SUCCESS;
}
