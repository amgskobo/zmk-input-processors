/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Contract tests for the decisions in runtime_temp_layer_policy.h.
 *
 * The inputs are booleans and two small numbers, so each decision is written
 * out as its whole truth table. A table of literal answers is deliberately
 * not a restatement of the expression under test.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zmk-input-processors/runtime_temp_layer_policy.h>

#include "test.h"

static void test_activation(void) {
    static const struct {
        bool enabled;
        bool active;
        bool typing;
        bool raises;
    } rows[] = {
        {false, false, false, false}, {false, false, true, false}, {false, true, false, false},
        {false, true, true, false},   {true, false, false, true},  {true, false, true, false},
        {true, true, false, false},   {true, true, true, false},
    };

    for (size_t i = 0; i < ARRAY_LEN(rows); i++) {
        EXPECT_EQ(
            runtime_temp_layer_should_activate(rows[i].enabled, rows[i].active, rows[i].typing),
            rows[i].raises, "enabled %d, active %d, typing %d", rows[i].enabled, rows[i].active,
            rows[i].typing);
    }
}

static void test_values(void) {
    EXPECT_TRUE(runtime_temp_layer_values_valid(0, 0, 0, 1), "all lower bounds");
    EXPECT_TRUE(
        runtime_temp_layer_values_valid(7, RUNTIME_TEMP_LAYER_MAX_MS, RUNTIME_TEMP_LAYER_MAX_MS, 8),
        "all upper bounds");
    EXPECT_FALSE(runtime_temp_layer_values_valid(1, 0, 0, 1), "layer equals count");
    EXPECT_FALSE(runtime_temp_layer_values_valid(UINT32_MAX, 0, 0, 8), "layer far past count");
    EXPECT_FALSE(runtime_temp_layer_values_valid(0, RUNTIME_TEMP_LAYER_MAX_MS + 1, 0, 1),
                 "timeout past maximum");
    EXPECT_FALSE(runtime_temp_layer_values_valid(0, 0, RUNTIME_TEMP_LAYER_MAX_MS + 1, 1),
                 "idle guard past maximum");
}

static void test_timeout_scheduling(void) {
    static const struct {
        bool enabled;
        uint32_t timeout_ms;
        bool schedules;
    } rows[] = {
        {false, 0, false}, {false, 1, false}, {false, 400, false}, {false, UINT32_MAX, false},
        {true, 0, false},  {true, 1, true},   {true, 400, true},   {true, UINT32_MAX, true},
    };
    /* Neither the layer nor the idle guard has any say in the timer. */
    static const uint8_t layers[] = {0, 31};
    static const uint16_t idles[] = {0, UINT16_MAX};

    for (size_t i = 0; i < ARRAY_LEN(rows); i++) {
        for (size_t l = 0; l < ARRAY_LEN(layers); l++) {
            for (size_t g = 0; g < ARRAY_LEN(idles); g++) {
                const struct runtime_temp_layer_params params = {
                    .enabled = rows[i].enabled,
                    .layer = layers[l],
                    .timeout_ms = rows[i].timeout_ms,
                    .require_prior_idle_ms = idles[g],
                };

                EXPECT_EQ(runtime_temp_layer_should_schedule_timeout(&params), rows[i].schedules,
                          "enabled %d, timeout %" PRIu32 ", layer %u, idle %u", rows[i].enabled,
                          rows[i].timeout_ms, (unsigned int)layers[l], (unsigned int)idles[g]);
            }
        }
    }
}

static void test_position_drop(void) {
    static const struct {
        bool active;
        bool pressed;
        bool has_exclusions;
        bool excluded;
        bool drops;
    } rows[] = {
        {false, false, false, false, false}, {false, false, false, true, false},
        {false, false, true, false, false},  {false, false, true, true, false},
        {false, true, false, false, false},  {false, true, false, true, false},
        {false, true, true, false, false},   {false, true, true, true, false},
        {true, false, false, false, false},  {true, false, false, true, false},
        {true, false, true, false, false},   {true, false, true, true, false},
        {true, true, false, false, false},   {true, true, false, true, false},
        {true, true, true, false, true},     {true, true, true, true, false},
    };

    for (size_t i = 0; i < ARRAY_LEN(rows); i++) {
        EXPECT_EQ(runtime_temp_layer_should_drop_for_position(
                      rows[i].active, rows[i].pressed, rows[i].has_exclusions, rows[i].excluded),
                  rows[i].drops, "active %d, pressed %d, list %d, excluded %d", rows[i].active,
                  rows[i].pressed, rows[i].has_exclusions, rows[i].excluded);
    }
}

static void test_typing_guard(void) {
    /* Zero is the guard switched off, however recent the press. */
    EXPECT_FALSE(runtime_temp_layer_is_typing(1000, 0, 1000), "off, pressed this instant");
    EXPECT_FALSE(runtime_temp_layer_is_typing(1000, 0, 999), "off, press stamped in the future");

    /* A press at t holds the layer down for [t, t + idle): the end is open. */
    EXPECT_TRUE(runtime_temp_layer_is_typing(1000, 300, 1000), "the instant of the press");
    EXPECT_TRUE(runtime_temp_layer_is_typing(1000, 300, 1299), "the last blocked millisecond");
    EXPECT_FALSE(runtime_temp_layer_is_typing(1000, 300, 1300), "the first free millisecond");
    EXPECT_FALSE(runtime_temp_layer_is_typing(1000, 300, 60000), "long after the press");

    /* The comparison must not overflow at either end of the signed uptime range. */
    EXPECT_TRUE(runtime_temp_layer_is_typing(INT64_MAX, UINT16_MAX, INT64_MAX),
                "maximum timestamp at the instant of the press");
    EXPECT_TRUE(runtime_temp_layer_is_typing(INT64_MAX, UINT16_MAX, INT64_MAX - 1),
                "future maximum timestamp");
    EXPECT_FALSE(runtime_temp_layer_is_typing(INT64_MIN, UINT16_MAX, INT64_MAX),
                 "the whole signed range after the press");

    static const uint16_t idles[] = {1, 2, 300, UINT16_MAX};
    static const int64_t presses[] = {0, 1, 123456789, INT64_C(1) << 40};

    for (size_t g = 0; g < ARRAY_LEN(idles); g++) {
        for (size_t p = 0; p < ARRAY_LEN(presses); p++) {
            const int64_t end = presses[p] + idles[g];

            EXPECT_TRUE(runtime_temp_layer_is_typing(presses[p], idles[g], end - 1),
                        "idle %u, press %" PRId64 ", last blocked", (unsigned int)idles[g],
                        presses[p]);
            EXPECT_FALSE(runtime_temp_layer_is_typing(presses[p], idles[g], end),
                         "idle %u, press %" PRId64 ", first free", (unsigned int)idles[g],
                         presses[p]);
        }
    }
}

int main(void) {
    test_values();
    test_activation();
    test_timeout_scheduling();
    test_position_drop();
    test_typing_guard();

    return test_report();
}
