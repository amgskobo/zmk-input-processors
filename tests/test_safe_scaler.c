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

#include <zmk-input-processors/safe_scaler_math.h>

/*
 * 889/500 is the speed this was found at. ZMK's own scaler holds the product
 * in an int16_t, so 37 * 889 = 32893 wraps to -32643 and the pointer travels
 * backwards. 36 is the last delta that fits, which is why the fault only ever
 * appeared on a fast movement.
 */
static void test_no_reversal_at_speed(void) {
    assert(safe_scaler_scale(36, 889, 500, NULL) == 64);
    assert(safe_scaler_scale(37, 889, 500, NULL) == 65);
    assert(safe_scaler_scale(100, 889, 500, NULL) == 177);
    assert(safe_scaler_scale(1000, 889, 500, NULL) == 1778);

    /* Monotonic across the boundary the stock scaler breaks at. */
    for (int32_t v = 1; v < 400; v++) {
        assert(safe_scaler_scale(v, 889, 500, NULL) >= safe_scaler_scale(v - 1, 889, 500, NULL));
        assert(safe_scaler_scale(v, 889, 500, NULL) > 0);
    }
}

static void test_sign_symmetry(void) {
    for (int32_t v = 1; v < 400; v++) {
        assert(safe_scaler_scale(-v, 889, 500, NULL) == -safe_scaler_scale(v, 889, 500, NULL));
    }
}

/* A ratio below one moves the pointer only because the remainder is kept. */
static void test_remainder_accumulates(void) {
    int16_t remainder = 0;
    int32_t total = 0;

    for (int i = 0; i < 16; i++) {
        total += safe_scaler_scale(1, 1, 16, &remainder);
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
            (void)safe_scaler_scale(v, 889, divisors[d], &remainder);
            assert(remainder > -(int32_t)divisors[d]);
            assert(remainder < (int32_t)divisors[d]);
        }
    }
}

/* Zero is how a chain kills one axis. */
static void test_zero_multiplier(void) {
    int16_t remainder = 0;

    assert(safe_scaler_scale(1000, 0, 1, &remainder) == 0);
    assert(remainder == 0);
    assert(safe_scaler_scale(-1000, 0, 1, NULL) == 0);
}

static void test_identity(void) {
    assert(safe_scaler_scale(0, 889, 500, NULL) == 0);
    assert(safe_scaler_scale(123, 1, 1, NULL) == 123);
    assert(safe_scaler_scale(-123, 1, 1, NULL) == -123);
}

/* Out of range saturates rather than wrapping: too fast beats backwards. */
static void test_saturates(void) {
    assert(safe_scaler_scale(INT32_MAX, 32767, 1, NULL) == INT32_MAX);
    assert(safe_scaler_scale(INT32_MIN, 32767, 1, NULL) == INT32_MIN);
}

static void test_params_valid(void) {
    assert(safe_scaler_params_valid(889, 500));
    assert(safe_scaler_params_valid(0, 1));
    assert(safe_scaler_params_valid(32767, 32767));

    assert(!safe_scaler_params_valid(1, 0));
    assert(!safe_scaler_params_valid(32768, 1));
    assert(!safe_scaler_params_valid(1, 32768));
}

int main(void) {
    test_no_reversal_at_speed();
    test_sign_symmetry();
    test_remainder_accumulates();
    test_remainder_stays_in_range();
    test_zero_multiplier();
    test_identity();
    test_saturates();
    test_params_valid();

    printf("safe scaler: all tests passed\n");

    return 0;
}
