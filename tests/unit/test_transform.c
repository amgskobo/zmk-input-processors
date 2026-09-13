/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Contract tests for the transform in runtime_transform_math.h.
 *
 * Three flags and a handful of code classes are a domain small enough to check
 * in full, against a model that states the rule directly: swap first, then
 * invert by the axis the event travels on afterwards.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zmk-input-processors/runtime_transform_math.h>

#include "test.h"

/* Two pairs, as a device reporting both pointer and scroll axes declares them. */
static const uint16_t x_codes[] = {10, 12};
static const uint16_t y_codes[] = {11, 13};

enum axis {
    AXIS_NONE,
    AXIS_X,
    AXIS_Y,
};

static enum axis axis_of(uint16_t code) {
    switch (code) {
    case 10:
    case 12:
        return AXIS_X;
    case 11:
    case 13:
        return AXIS_Y;
    default:
        return AXIS_NONE;
    }
}

static uint16_t partner_of(uint16_t code) {
    switch (code) {
    case 10:
        return 11;
    case 11:
        return 10;
    case 12:
        return 13;
    case 13:
        return 12;
    default:
        return code;
    }
}

static struct runtime_transform_flags flags_from(unsigned int mask) {
    return (struct runtime_transform_flags){
        .xy_swap = (mask & 1U) != 0,
        .x_invert = (mask & 2U) != 0,
        .y_invert = (mask & 4U) != 0,
    };
}

static void test_code_index(void) {
    const uint16_t duplicated[] = {7, 7};

    EXPECT_EQ(runtime_transform_code_index(10, x_codes, ARRAY_LEN(x_codes)), 0, "first code");
    EXPECT_EQ(runtime_transform_code_index(12, x_codes, ARRAY_LEN(x_codes)), 1, "second code");
    EXPECT_EQ(runtime_transform_code_index(11, x_codes, ARRAY_LEN(x_codes)), -1, "absent code");
    EXPECT_EQ(runtime_transform_code_index(10, x_codes, 0), -1, "empty list");
    EXPECT_EQ(runtime_transform_code_index(7, duplicated, ARRAY_LEN(duplicated)), 0,
              "first of two matches");
}

static void test_every_combination(void) {
    static const uint16_t codes[] = {10, 11, 12, 13, 0, 99, UINT16_MAX};
    static const int32_t values[] = {0, 1, -1, 7, -5, INT32_MAX, INT32_MIN + 1};

    for (unsigned int mask = 0; mask < 8; mask++) {
        const struct runtime_transform_flags flags = flags_from(mask);

        for (size_t c = 0; c < ARRAY_LEN(codes); c++) {
            for (size_t v = 0; v < ARRAY_LEN(values); v++) {
                const uint16_t want_code = flags.xy_swap ? partner_of(codes[c]) : codes[c];
                const enum axis travels = axis_of(want_code);
                const bool inverted =
                    (travels == AXIS_X && flags.x_invert) || (travels == AXIS_Y && flags.y_invert);
                const int32_t want_value = inverted ? -values[v] : values[v];
                uint16_t code = codes[c];
                int32_t value = values[v];

                runtime_transform_apply(&code, &value, &flags, x_codes, y_codes,
                                        ARRAY_LEN(x_codes));

                EXPECT_EQ(code, want_code, "code of %u carrying %" PRId32 " under flags %u",
                          (unsigned int)codes[c], values[v], mask);
                EXPECT_EQ(value, want_value, "value of %u carrying %" PRId32 " under flags %u",
                          (unsigned int)codes[c], values[v], mask);
            }
        }
    }
}

static void test_documented_order(void) {
    const struct runtime_transform_flags swap_and_invert_x = {.xy_swap = true, .x_invert = true};
    uint16_t code = 11;
    int32_t value = 8;

    /* An event that arrives on Y and is swapped onto X takes X's inversion... */
    runtime_transform_apply(&code, &value, &swap_and_invert_x, x_codes, y_codes,
                            ARRAY_LEN(x_codes));
    EXPECT_EQ(code, 10, "Y swapped onto X");
    EXPECT_EQ(value, -8, "Y swapped onto X is inverted as X");

    /* ...and one that arrives on X and leaves for Y escapes it. */
    code = 10;
    value = 8;
    runtime_transform_apply(&code, &value, &swap_and_invert_x, x_codes, y_codes,
                            ARRAY_LEN(x_codes));
    EXPECT_EQ(code, 11, "X swapped onto Y");
    EXPECT_EQ(value, 8, "X swapped onto Y is not inverted as X");
}

static void test_no_codes(void) {
    for (unsigned int mask = 0; mask < 8; mask++) {
        const struct runtime_transform_flags flags = flags_from(mask);
        uint16_t code = 10;
        int32_t value = 7;

        runtime_transform_apply(&code, &value, &flags, x_codes, y_codes, 0);
        EXPECT_EQ(code, 10, "no codes declared, flags %u", mask);
        EXPECT_EQ(value, 7, "no codes declared, flags %u", mask);
    }
}

static void test_extreme_values(void) {
    const struct runtime_transform_flags invert_both = {.x_invert = true, .y_invert = true};
    const struct runtime_transform_flags untouched = {0};
    uint16_t code = 10;
    int32_t value = INT32_MIN;

    /*
     * The scaler clamps to INT32_MIN on a large enough delta, so an inverting
     * stage after it can be handed exactly that, and negating it overflows.
     * It clamps too, keeping the change of direction.
     */
    runtime_transform_apply(&code, &value, &invert_both, x_codes, y_codes, ARRAY_LEN(x_codes));
    EXPECT_EQ(value, INT32_MAX, "inverting the minimum on X clamps");

    code = 13;
    value = INT32_MIN;
    runtime_transform_apply(&code, &value, &invert_both, x_codes, y_codes, ARRAY_LEN(x_codes));
    EXPECT_EQ(value, INT32_MAX, "inverting the minimum on Y clamps");

    code = 10;
    value = INT32_MAX;
    runtime_transform_apply(&code, &value, &invert_both, x_codes, y_codes, ARRAY_LEN(x_codes));
    EXPECT_EQ(value, -INT32_MAX, "inverting the maximum is exact");

    code = 10;
    value = INT32_MIN;
    runtime_transform_apply(&code, &value, &untouched, x_codes, y_codes, ARRAY_LEN(x_codes));
    EXPECT_EQ(value, INT32_MIN, "an uninverted minimum passes untouched");
}

int main(void) {
    test_code_index();
    test_every_combination();
    test_documented_order();
    test_no_codes();
    test_extreme_values();

    return test_report();
}
