/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Contract tests for the code mapper in runtime_code_mapper_math.h.
 *
 * A code is 16 bits, so every one of them is checked, against a lookup table
 * that states the first-match rule as data instead of as a search.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zmk-input-processors/runtime_code_mapper_math.h>

#include "test.h"

static void test_disabled_is_a_no_op(void) {
    const uint16_t map[] = {1, 10, 2, 20};

    for (uint32_t c = 0; c <= UINT16_MAX; c++) {
        uint16_t code = (uint16_t)c;

        runtime_code_mapper_apply(&code, map, ARRAY_LEN(map) / 2, false);
        EXPECT_EQ(code, c, "disabled, code %" PRIu32, c);
    }
}

static void test_pinned_cases(void) {
    uint16_t code;

    const uint16_t duplicated[] = {1, 10, 1, 11};
    code = 1;
    runtime_code_mapper_apply(&code, duplicated, ARRAY_LEN(duplicated) / 2, true);
    EXPECT_EQ(code, 10, "the first of two pairs for one code wins");

    /* One instance rewrites once: a chain of rewrites needs a chain of stages. */
    const uint16_t chained[] = {1, 2, 2, 3};
    code = 1;
    runtime_code_mapper_apply(&code, chained, ARRAY_LEN(chained) / 2, true);
    EXPECT_EQ(code, 2, "a rewritten code is not looked up again");
    code = 2;
    runtime_code_mapper_apply(&code, chained, ARRAY_LEN(chained) / 2, true);
    EXPECT_EQ(code, 3, "the second pair still applies to its own code");

    const uint16_t identity[] = {5, 5};
    code = 5;
    runtime_code_mapper_apply(&code, identity, ARRAY_LEN(identity) / 2, true);
    EXPECT_EQ(code, 5, "a pair onto itself");

    const uint16_t limits[] = {0, UINT16_MAX, UINT16_MAX, 0};
    code = 0;
    runtime_code_mapper_apply(&code, limits, ARRAY_LEN(limits) / 2, true);
    EXPECT_EQ(code, UINT16_MAX, "the lowest code");
    code = UINT16_MAX;
    runtime_code_mapper_apply(&code, limits, ARRAY_LEN(limits) / 2, true);
    EXPECT_EQ(code, 0, "the highest code");

    const uint16_t three[] = {1, 10, 2, 20, 3, 30};
    code = 3;
    runtime_code_mapper_apply(&code, three, ARRAY_LEN(three) / 2, true);
    EXPECT_EQ(code, 30, "the last pair is reachable");
    code = 3;
    runtime_code_mapper_apply(&code, three, 2, true);
    EXPECT_EQ(code, 3, "a pair past the declared count is not read");

    code = 1;
    runtime_code_mapper_apply(&code, three, 0, true);
    EXPECT_EQ(code, 1, "an empty map");
}

static void test_every_code(void) {
    static const uint16_t map[] = {3, 30, 7, 70, 3, 31, 70, 700, 0, 1, UINT16_MAX, 2, 7, 71};
    static uint16_t expected[UINT16_MAX + 1];
    const size_t pairs = ARRAY_LEN(map) / 2;

    for (uint32_t c = 0; c <= UINT16_MAX; c++) {
        expected[c] = (uint16_t)c;
    }
    /* Written last pair first, so that an earlier pair overwrites a later one. */
    for (size_t i = pairs; i-- > 0;) {
        expected[map[i * 2]] = map[(i * 2) + 1];
    }

    for (uint32_t c = 0; c <= UINT16_MAX; c++) {
        uint16_t code = (uint16_t)c;

        runtime_code_mapper_apply(&code, map, pairs, true);
        EXPECT_EQ(code, expected[c], "enabled, code %" PRIu32, c);
    }
}

int main(void) {
    test_disabled_is_a_no_op();
    test_pinned_cases();
    test_every_code();

    return test_report();
}
