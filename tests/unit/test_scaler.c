/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Contract tests for the scaler arithmetic in runtime_scaler_math.h.
 *
 * The reversal this arithmetic exists to prevent shipped in two independent
 * implementations and was found on hardware rather than in review, so it is
 * checked from three directions: values pinned from that session, a reference
 * model written without the implementation's shortcuts, and the invariant that
 * makes a tracked remainder worth having -- no movement is lost or invented,
 * however the deltas arrive.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zmk-input-processors/runtime_scaler_math.h>

#include "test.h"

/* The ranges the bindings document, and the one that is a correctness limit. */
_Static_assert(RUNTIME_SCALER_MULTIPLIER_MIN == 0, "a multiplier may be zero");
_Static_assert(RUNTIME_SCALER_DIVISOR_MIN == 1, "a divisor may not be zero");
_Static_assert(RUNTIME_SCALER_PARAM_MAX == 32767, "the bindings document 32767");
_Static_assert(RUNTIME_SCALER_PARAM_MAX <= INT16_MAX, "a remainder must fit ZMK's int16_t slot");

/* BUILD_ASSERT evaluates the devicetree form, so it has to be a constant expression. */
_Static_assert(RUNTIME_SCALER_DT_PARAMS_VALID(0, 1), "zero multiplier");
_Static_assert(RUNTIME_SCALER_DT_PARAMS_VALID(32767, 32767), "both at the ceiling");
_Static_assert(!RUNTIME_SCALER_DT_PARAMS_VALID(-1, 1), "negative multiplier");
_Static_assert(!RUNTIME_SCALER_DT_PARAMS_VALID(32768, 1), "multiplier past the ceiling");
_Static_assert(!RUNTIME_SCALER_DT_PARAMS_VALID(1, 0), "zero divisor");
_Static_assert(!RUNTIME_SCALER_DT_PARAMS_VALID(1, -1), "negative divisor");
_Static_assert(!RUNTIME_SCALER_DT_PARAMS_VALID(1, 32768), "divisor past the ceiling");

/* Why the pinned pair below is 36 and 37: the stock scaler's int16_t product. */
_Static_assert(36 * 889 <= INT16_MAX && 37 * 889 > INT16_MAX, "37 is the first delta that wraps");

static void test_parameter_domain(void) {
    static const uint32_t samples[] = {0, 1, 2, 500, 889, 32766, 32767, 32768, 65535, UINT32_MAX};

    for (size_t m = 0; m < ARRAY_LEN(samples); m++) {
        for (size_t d = 0; d < ARRAY_LEN(samples); d++) {
            const uint32_t multiplier = samples[m];
            const uint32_t divisor = samples[d];
            const bool valid = multiplier <= 32767 && divisor >= 1 && divisor <= 32767;

            EXPECT_EQ(runtime_scaler_params_valid(multiplier, divisor), valid,
                      "params_valid(%" PRIu32 ", %" PRIu32 ")", multiplier, divisor);
        }
    }

    /*
     * Devicetree values arrive signed, which the unsigned runtime form never
     * sees, so the devicetree form is swept through the negatives as well; and
     * wherever both forms can be asked, they have to agree.
     */
    static const int64_t signed_samples[] = {INT32_MIN, -32768, -1,    0,     1,     2,        500,
                                             889,       32766,  32767, 32768, 65535, INT32_MAX};

    for (size_t m = 0; m < ARRAY_LEN(signed_samples); m++) {
        for (size_t d = 0; d < ARRAY_LEN(signed_samples); d++) {
            const int64_t multiplier = signed_samples[m];
            const int64_t divisor = signed_samples[d];
            const bool valid =
                multiplier >= 0 && multiplier <= 32767 && divisor >= 1 && divisor <= 32767;

            EXPECT_EQ(RUNTIME_SCALER_DT_PARAMS_VALID(multiplier, divisor), valid,
                      "DT_PARAMS_VALID(%" PRId64 ", %" PRId64 ")", multiplier, divisor);
            if (multiplier >= 0 && divisor >= 0) {
                EXPECT_EQ(RUNTIME_SCALER_DT_PARAMS_VALID(multiplier, divisor),
                          runtime_scaler_params_valid((uint32_t)multiplier, (uint32_t)divisor),
                          "both forms of (%" PRId64 ", %" PRId64 ")", multiplier, divisor);
            }
        }
    }
}

static void test_pinned_values(void) {
    /*
     * 889/500 is the speed the reversal was found at. The stock scaler holds
     * the product in an int16_t, so 36 was the last delta that came out right
     * and 37 the first that sent the pointer backwards.
     */
    EXPECT_EQ(runtime_scaler_scale(36, 889, 500, NULL), 64, "last delta the stock scaler survived");
    EXPECT_EQ(runtime_scaler_scale(37, 889, 500, NULL), 65,
              "first delta the stock scaler reversed");
    EXPECT_EQ(runtime_scaler_scale(100, 889, 500, NULL), 177, "a fast delta");
    EXPECT_EQ(runtime_scaler_scale(1000, 889, 500, NULL), 1778, "a very fast delta");
    EXPECT_EQ(runtime_scaler_scale(-1000, 889, 500, NULL), -1778, "a very fast delta backwards");
    EXPECT_EQ(runtime_scaler_scale(0, 889, 500, NULL), 0, "no movement");
    EXPECT_EQ(runtime_scaler_scale(123, 1, 1, NULL), 123, "identity");
    EXPECT_EQ(runtime_scaler_scale(-123, 1, 1, NULL), -123, "identity backwards");
}

static void test_remainder_contract(void) {
    int16_t remainder = 0;
    int32_t total = 0;

    /* A ratio below one moves the pointer at all only because the remainder is kept. */
    for (int i = 0; i < 16; i++) {
        total += runtime_scaler_scale(1, 1, 16, &remainder);
    }
    EXPECT_EQ(total, 1, "sixteen sixteenths forwards");
    EXPECT_EQ(remainder, 0, "nothing left over forwards");

    remainder = 0;
    total = 0;
    for (int i = 0; i < 16; i++) {
        total += runtime_scaler_scale(-1, 1, 16, &remainder);
    }
    EXPECT_EQ(total, -1, "sixteen sixteenths backwards");
    EXPECT_EQ(remainder, 0, "nothing left over backwards");

    /* A remainder the current divisor could have left is carried... */
    remainder = 15;
    EXPECT_EQ(runtime_scaler_scale(1, 1, 16, &remainder), 1, "largest carry forwards");
    EXPECT_EQ(remainder, 0, "largest carry forwards is spent");
    remainder = -15;
    EXPECT_EQ(runtime_scaler_scale(-1, 1, 16, &remainder), -1, "largest carry backwards");
    EXPECT_EQ(remainder, 0, "largest carry backwards is spent");

    /* ...and one that only a different ratio could have left is dropped. */
    remainder = 16;
    EXPECT_EQ(runtime_scaler_scale(1, 1, 16, &remainder), 0, "smallest stale remainder");
    EXPECT_EQ(remainder, 1, "smallest stale remainder is replaced");
    remainder = -16;
    EXPECT_EQ(runtime_scaler_scale(-1, 1, 16, &remainder), 0, "smallest stale remainder backwards");
    EXPECT_EQ(remainder, -1, "smallest stale remainder backwards is replaced");
    remainder = 400;
    EXPECT_EQ(runtime_scaler_scale(7, 16, 16, &remainder), 7, "remainder left by 889/500");
    EXPECT_EQ(remainder, 0, "remainder left by 889/500 is dropped");
    remainder = -400;
    EXPECT_EQ(runtime_scaler_scale(-7, 16, 16, &remainder), -7,
              "negative remainder left by 889/500");
    EXPECT_EQ(remainder, 0, "negative remainder left by 889/500 is dropped");
    remainder = INT16_MIN;
    EXPECT_EQ(runtime_scaler_scale(0, 1, RUNTIME_SCALER_PARAM_MAX, &remainder), 0, "slot minimum");
    EXPECT_EQ(remainder, 0, "slot minimum is dropped even at the largest divisor");
    remainder = INT16_MAX;
    EXPECT_EQ(runtime_scaler_scale(0, 1, RUNTIME_SCALER_PARAM_MAX, &remainder), 0, "slot maximum");
    EXPECT_EQ(remainder, 0, "slot maximum is dropped even at the largest divisor");

    /* Turning round spends the carried fraction instead of reversing at once. */
    remainder = 15;
    EXPECT_EQ(runtime_scaler_scale(-1, 1, 16, &remainder), 0, "reversal against a carry");
    EXPECT_EQ(remainder, 14, "reversal spends part of the carry");

    /* Chains park unused stages at mul == div, which must leave nothing behind. */
    static const uint32_t ratios[] = {1, 2, 16, 500, RUNTIME_SCALER_PARAM_MAX};

    for (size_t r = 0; r < ARRAY_LEN(ratios); r++) {
        remainder = 0;
        for (int32_t value = -5000; value <= 5000; value += 7) {
            EXPECT_EQ(runtime_scaler_scale(value, ratios[r], ratios[r], &remainder), value,
                      "identity %" PRIu32 " at %" PRId32, ratios[r], value);
            EXPECT_EQ(remainder, 0, "identity %" PRIu32 " remainder at %" PRId32, ratios[r], value);
        }
    }

    /* Zero is how a chain kills one axis, and a killed axis has to stay silent. */
    static const int32_t values[] = {INT32_MIN, -1000, -1, 1, 1000, INT32_MAX};

    remainder = 0;
    for (size_t v = 0; v < ARRAY_LEN(values); v++) {
        EXPECT_EQ(runtime_scaler_scale(values[v], 0, 1, &remainder), 0,
                  "zero multiplier at %" PRId32, values[v]);
        EXPECT_EQ(remainder, 0, "zero multiplier remainder at %" PRId32, values[v]);
    }
}

static void test_saturation(void) {
    /* Too fast beats backwards: a result out of range clamps rather than wrapping. */
    EXPECT_EQ(runtime_scaler_scale(INT32_MAX, RUNTIME_SCALER_PARAM_MAX, 1, NULL), INT32_MAX,
              "largest product");
    EXPECT_EQ(runtime_scaler_scale(INT32_MIN, RUNTIME_SCALER_PARAM_MAX, 1, NULL), INT32_MIN,
              "most negative product");
    EXPECT_EQ(runtime_scaler_scale(1073741823, 2, 1, NULL), INT32_MAX - 1, "just inside the range");
    EXPECT_EQ(runtime_scaler_scale(1073741824, 2, 1, NULL), INT32_MAX, "one past the range");
    EXPECT_EQ(runtime_scaler_scale(-1073741824, 2, 1, NULL), INT32_MIN, "exactly the minimum");
    EXPECT_EQ(runtime_scaler_scale(-1073741825, 2, 1, NULL), INT32_MIN, "one past the minimum");

    int16_t remainder = 0;

    EXPECT_EQ(runtime_scaler_scale(INT32_MAX, RUNTIME_SCALER_PARAM_MAX, 16, &remainder), INT32_MAX,
              "clamped while tracking a remainder");
    EXPECT_TRUE(remainder > -16 && remainder < 16,
                "a clamped result leaves a slot-sized remainder");
}

static void test_shape(void) {
    static const struct {
        uint32_t multiplier;
        uint32_t divisor;
    } ratios[] = {{889, 500}, {1, 16},    {16, 1},    {5, 3},
                  {3, 5},     {1, 32767}, {32767, 1}, {32767, 32766}};

    for (size_t r = 0; r < ARRAY_LEN(ratios); r++) {
        const uint32_t multiplier = ratios[r].multiplier;
        const uint32_t divisor = ratios[r].divisor;
        int32_t previous = runtime_scaler_scale(-5001, multiplier, divisor, NULL);

        for (int32_t value = -5000; value <= 5000; value++) {
            const int32_t scaled = runtime_scaler_scale(value, multiplier, divisor, NULL);

            EXPECT_TRUE(scaled >= previous, "monotonic at %" PRId32 " under %" PRIu32 "/%" PRIu32,
                        value, multiplier, divisor);
            EXPECT_EQ(runtime_scaler_scale(-value, multiplier, divisor, NULL), -scaled,
                      "symmetric at %" PRId32 " under %" PRIu32 "/%" PRIu32, value, multiplier,
                      divisor);
            EXPECT_TRUE(value > 0 ? scaled >= 0 : scaled <= 0,
                        "never reverses at %" PRId32 " under %" PRIu32 "/%" PRIu32, value,
                        multiplier, divisor);
            if (multiplier >= divisor) {
                EXPECT_TRUE(value == 0 || scaled != 0,
                            "never swallows %" PRId32 " under %" PRIu32 "/%" PRIu32, value,
                            multiplier, divisor);
            }

            previous = scaled;
        }
    }
}

struct expected_scale {
    int32_t value;
    int16_t remainder;
    int64_t numerator;
    bool clamped;
};

/*
 * The contract stated without the implementation's shortcuts: carry a
 * remainder only if the current divisor could have left it, divide magnitudes
 * so that truncation towards zero is explicit rather than inherited from C,
 * give the remainder the numerator's sign, and clamp into int32_t.
 */
static struct expected_scale reference_scale(int32_t value, uint32_t multiplier, uint32_t divisor,
                                             int16_t carried) {
    const bool carry = carried < (int32_t)divisor && carried > -(int32_t)divisor;
    const int64_t numerator = (int64_t)value * multiplier + (carry ? carried : 0);
    const bool negative = numerator < 0;
    const uint64_t magnitude = negative ? 0U - (uint64_t)numerator : (uint64_t)numerator;
    const int64_t quotient = (int64_t)(magnitude / divisor);
    const int64_t rest = (int64_t)(magnitude % divisor);
    const int64_t truncated = negative ? -quotient : quotient;
    const bool clamped = truncated > INT32_MAX || truncated < INT32_MIN;

    return (struct expected_scale){
        .value = clamped ? (truncated > 0 ? INT32_MAX : INT32_MIN) : (int32_t)truncated,
        .remainder = (int16_t)(negative ? -rest : rest),
        .numerator = numerator,
        .clamped = clamped,
    };
}

#define SCALE_CASE "scale(%" PRId32 ", %" PRIu32 "/%" PRIu32 ", %s %" PRId16 ")"
#define SCALE_ARGS value, multiplier, divisor, tracked ? "carrying" : "untracked", carried

static void check_scale(int32_t value, uint32_t multiplier, uint32_t divisor, bool tracked,
                        int16_t carried) {
    const struct expected_scale want =
        reference_scale(value, multiplier, divisor, tracked ? carried : 0);
    int16_t remainder = carried;
    const int32_t scaled =
        runtime_scaler_scale(value, multiplier, divisor, tracked ? &remainder : NULL);

    EXPECT_EQ(scaled, want.value, SCALE_CASE, SCALE_ARGS);

    if (!tracked) {
        EXPECT_EQ(remainder, carried, SCALE_CASE " wrote a remainder it was not given", SCALE_ARGS);
        return;
    }

    EXPECT_EQ(remainder, want.remainder, SCALE_CASE " remainder", SCALE_ARGS);
    EXPECT_TRUE(remainder > -(int32_t)divisor && remainder < (int32_t)divisor,
                SCALE_CASE " remainder fits the divisor", SCALE_ARGS);
    EXPECT_TRUE(remainder == 0 || (remainder < 0) == (want.numerator < 0),
                SCALE_CASE " remainder has the numerator's sign", SCALE_ARGS);
    if (!want.clamped) {
        EXPECT_EQ((int64_t)scaled * divisor + remainder, want.numerator,
                  SCALE_CASE " conserves the numerator", SCALE_ARGS);
    }
}

static void test_reference_grid(void) {
    static const int32_t values[] = {
        INT32_MIN, INT32_MIN + 1,
        -2147483,  -65536,
        -32769,    -32768,
        -1001,     -1000,
        -37,       -36,
        -17,       -16,
        -15,       -2,
        -1,        0,
        1,         2,
        15,        16,
        17,        36,
        37,        1000,
        1001,      32767,
        32768,     65536,
        2147483,   INT32_MAX - 1,
        INT32_MAX,
    };
    static const uint32_t multipliers[] = {0,   1,   2,   3,   15,    16,    17,
                                           499, 500, 501, 889, 16383, 32766, 32767};
    static const uint32_t divisors[] = {1,   2,   3,   15,    16,    17,   499,
                                        500, 501, 889, 16383, 32766, 32767};

    for (size_t d = 0; d < ARRAY_LEN(divisors); d++) {
        const int32_t divisor = (int32_t)divisors[d];
        /* Around both edges of the discard threshold, and the slot's own limits. */
        const int32_t carries[] = {0,        1,        -1,          divisor - 1,    -(divisor - 1),
                                   divisor,  -divisor, divisor + 1, -(divisor + 1), INT16_MAX,
                                   INT16_MIN};

        for (size_t v = 0; v < ARRAY_LEN(values); v++) {
            for (size_t m = 0; m < ARRAY_LEN(multipliers); m++) {
                check_scale(values[v], multipliers[m], divisors[d], false, 0);

                for (size_t c = 0; c < ARRAY_LEN(carries); c++) {
                    if (carries[c] >= INT16_MIN && carries[c] <= INT16_MAX) {
                        check_scale(values[v], multipliers[m], divisors[d], true,
                                    (int16_t)carries[c]);
                    }
                }
            }
        }
    }
}

static void test_reference_random(void) {
    uint64_t state = UINT64_C(0x853C49E6748FEA9B);

    for (unsigned long i = 0; i < 1000000; i++) {
        const uint64_t a = test_random(&state);
        const uint64_t b = test_random(&state);
        const uint32_t multiplier = (uint32_t)(a % (RUNTIME_SCALER_PARAM_MAX + 1U));
        const uint32_t divisor = 1U + (uint32_t)((a >> 32) % RUNTIME_SCALER_PARAM_MAX);
        int32_t value = (int32_t)(uint32_t)b;
        int16_t carried = (int16_t)(uint16_t)(b >> 32);

        /* Real deltas are mostly small, and real carries mostly fit the divisor. */
        if (i % 2 == 0) {
            value /= 65536;
        }
        if (i % 3 != 0) {
            carried =
                (int16_t)((int32_t)((b >> 32) % (2U * divisor - 1U)) - (int32_t)(divisor - 1U));
        }

        check_scale(value, multiplier, divisor, i % 4 != 0, carried);
    }
}

static void test_motion_is_conserved(void) {
    static const struct {
        uint32_t multiplier;
        uint32_t divisor;
    } ratios[] = {{1, 16}, {889, 500}, {5, 3}, {3, 5}, {7, 2}, {1, 32767}, {32767, 32767}, {0, 1}};
    uint64_t state = UINT64_C(0xDA3E39CB94B95BDB);

    for (size_t r = 0; r < ARRAY_LEN(ratios); r++) {
        const uint32_t multiplier = ratios[r].multiplier;
        const uint32_t divisor = ratios[r].divisor;

        for (int pattern = 0; pattern < 4; pattern++) {
            int16_t remainder = 0;
            int64_t moved = 0;
            int64_t reported = 0;

            for (int step = 0; step < 10000; step++) {
                int32_t value;

                switch (pattern) {
                case 0:
                    value = 1;
                    break;
                case 1:
                    value = -1;
                    break;
                case 2:
                    value = step % 2 == 0 ? 3 : -2;
                    break;
                default:
                    value = (int32_t)(test_random(&state) % 101) - 50;
                    break;
                }

                moved += (int64_t)value * multiplier;
                reported += runtime_scaler_scale(value, multiplier, divisor, &remainder);

                /* Everything moved is either reported or still held as a remainder. */
                EXPECT_EQ(reported * divisor + remainder, moved,
                          "motion under %" PRIu32 "/%" PRIu32 ", pattern %d, step %d", multiplier,
                          divisor, pattern, step);
            }
        }
    }
}

int main(void) {
    test_parameter_domain();
    test_pinned_values();
    test_remainder_contract();
    test_saturation();
    test_shape();
    test_reference_grid();
    test_reference_random();
    test_motion_is_conserved();

    return test_report();
}
