/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The reversal these tests pin was found on hardware, not in review, and it
 * was present in two independent implementations at once. It is cheap to
 * check here and expensive to find anywhere else.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <zmk-input-processors/runtime_scaler_math.h>

/*
 * 889/500 is the speed this was found at. ZMK's own scaler holds the product
 * in an int16_t, so 37 * 889 = 32893 wraps to -32643 and the pointer travels
 * backwards. 36 is the last delta that fits, which is why the fault only ever
 * appeared on a fast movement.
 */
static void test_no_reversal_at_speed(void) {
    assert(runtime_scaler_scale(36, 889, 500, NULL) == 64);
    assert(runtime_scaler_scale(37, 889, 500, NULL) == 65);
    assert(runtime_scaler_scale(100, 889, 500, NULL) == 177);
    assert(runtime_scaler_scale(1000, 889, 500, NULL) == 1778);

    /* Monotonic across the boundary the stock scaler breaks at. */
    for (int32_t v = 1; v < 400; v++) {
        assert(runtime_scaler_scale(v, 889, 500, NULL) >=
               runtime_scaler_scale(v - 1, 889, 500, NULL));
        assert(runtime_scaler_scale(v, 889, 500, NULL) > 0);
    }
}

static void test_sign_symmetry(void) {
    for (int32_t v = 1; v < 400; v++) {
        assert(runtime_scaler_scale(-v, 889, 500, NULL) ==
               -runtime_scaler_scale(v, 889, 500, NULL));
    }
}

/* A ratio below one moves the pointer only because the remainder is kept. */
static void test_remainder_accumulates(void) {
    int16_t remainder = 0;
    int32_t total = 0;

    for (int i = 0; i < 16; i++) {
        total += runtime_scaler_scale(1, 1, 16, &remainder);
    }

    assert(total == 1);
    assert(remainder == 0);
}

/* The int16_t slot ZMK provides has to hold whatever this leaves in it. */
static void test_remainder_stays_in_range(void) {
    const uint32_t divisors[] = {1, 2, 16, 500, 32767};

    for (size_t d = 0; d < sizeof(divisors) / sizeof(divisors[0]); d++) {
        int16_t remainder = 0;

        for (int32_t v = -5000; v <= 5000; v += 7) {
            (void)runtime_scaler_scale(v, 889, divisors[d], &remainder);
            assert(remainder > -(int32_t)divisors[d]);
            assert(remainder < (int32_t)divisors[d]);
        }
    }
}

/*
 * The ratio can change while the keyboard is running, and the remainder lives
 * in the listener's slot rather than in the processor, so a value left by the
 * old ratio is still there when the new one runs. Carried in, 400/16 would be
 * 25 counts of movement nobody asked for on the very first report after the
 * edit.
 */
static void test_stale_remainder_is_discarded(void) {
    int16_t remainder = 400; /* plausible under a divisor of 500 */

    assert(runtime_scaler_scale(7, 16, 16, &remainder) == 7);
    assert(remainder == 0);

    remainder = -400;
    assert(runtime_scaler_scale(-7, 16, 16, &remainder) == -7);
    assert(remainder == 0);

    /* A remainder the current divisor could have produced is still carried. */
    remainder = 15;
    assert(runtime_scaler_scale(1, 1, 16, &remainder) == 1);
    assert(remainder == 0);
}

/*
 * A scaler at multiplier == divisor is the no-op every chain here relies on:
 * stages are placed in advance at pass-through values so they can be reached
 * from a client later. If this drifts, so does every chain holding one.
 */
static void test_equal_ratio_is_a_noop(void) {
    const uint32_t ratios[] = {1, 2, 16, 500, 32767};

    for (size_t r = 0; r < sizeof(ratios) / sizeof(ratios[0]); r++) {
        int16_t remainder = 0;

        for (int32_t v = -5000; v <= 5000; v += 7) {
            assert(runtime_scaler_scale(v, ratios[r], ratios[r], &remainder) == v);
            assert(remainder == 0);
        }
    }
}

/* Zero is how a chain kills one axis. */
static void test_zero_multiplier(void) {
    int16_t remainder = 0;

    assert(runtime_scaler_scale(1000, 0, 1, &remainder) == 0);
    assert(remainder == 0);
    assert(runtime_scaler_scale(-1000, 0, 1, NULL) == 0);
}

static void test_identity(void) {
    assert(runtime_scaler_scale(0, 889, 500, NULL) == 0);
    assert(runtime_scaler_scale(123, 1, 1, NULL) == 123);
    assert(runtime_scaler_scale(-123, 1, 1, NULL) == -123);
}

/* Out of range saturates rather than wrapping: too fast beats backwards. */
static void test_saturates(void) {
    assert(runtime_scaler_scale(INT32_MAX, 32767, 1, NULL) == INT32_MAX);
    assert(runtime_scaler_scale(INT32_MIN, 32767, 1, NULL) == INT32_MIN);
}

static void test_params_valid(void) {
    assert(runtime_scaler_params_valid(889, 500));
    assert(runtime_scaler_params_valid(0, 1));
    assert(runtime_scaler_params_valid(32767, 32767));

    assert(!runtime_scaler_params_valid(1, 0));
    assert(!runtime_scaler_params_valid(32768, 1));
    assert(!runtime_scaler_params_valid(1, 32768));
}

int main(void) {
    test_no_reversal_at_speed();
    test_sign_symmetry();
    test_remainder_accumulates();
    test_remainder_stays_in_range();
    test_stale_remainder_is_discarded();
    test_equal_ratio_is_a_noop();
    test_zero_multiplier();
    test_identity();
    test_saturates();
    test_params_valid();

    printf("runtime scaler: all tests passed\n");

    return 0;
}
