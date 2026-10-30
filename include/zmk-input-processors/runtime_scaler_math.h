/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The scaler arithmetic, with no Zephyr dependency so it can be run on the
 * host. It is separated for that reason alone: this is six lines of
 * arithmetic that shipped wrong in two independent implementations and took a
 * hardware session to find, so it is worth pinning with a test that runs
 * without a keyboard attached.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * ZMK keeps a tracked remainder in an int16_t slot. Holding both numbers
 * inside the positive int16 range is what bounds the remainder to fit it,
 * so the ceiling is a correctness limit rather than a chosen one.
 *
 * A multiplier of zero is allowed because it is how a chain kills one axis:
 * a scroll route that wants vertical movement only scales X by 0/1 rather
 * than carrying a separate processor to drop it. Zero numerator leaves zero
 * remainder, so the bound above still holds.
 */
#define RUNTIME_SCALER_MULTIPLIER_MIN 0
#define RUNTIME_SCALER_DIVISOR_MIN 1
#define RUNTIME_SCALER_PARAM_MAX INT16_MAX

/*
 * The devicetree form, for BUILD_ASSERT. A call to the function below is not a
 * constant expression, and devicetree values arrive signed, so the lower bound
 * on the multiplier is worth checking here even though the unsigned runtime
 * path cannot cross it.
 */
#define RUNTIME_SCALER_DT_PARAMS_VALID(mul, div)                                                   \
    ((mul) >= RUNTIME_SCALER_MULTIPLIER_MIN && (mul) <= RUNTIME_SCALER_PARAM_MAX &&                \
     (div) >= RUNTIME_SCALER_DIVISOR_MIN && (div) <= RUNTIME_SCALER_PARAM_MAX)

static inline bool runtime_scaler_params_valid(uint32_t multiplier, uint32_t divisor) {
    /* No lower bound on the multiplier: zero is allowed and it is unsigned. */
    return multiplier <= RUNTIME_SCALER_PARAM_MAX && divisor >= RUNTIME_SCALER_DIVISOR_MIN &&
           divisor <= RUNTIME_SCALER_PARAM_MAX;
}

/*
 * Scales one value, carrying the fraction division discards in *remainder
 * when one is supplied.
 *
 * Normal inputs use a checked 32-bit multiply/add and one division. If that
 * numerator does not fit, divide the input first: the remaining product is
 * bounded by (divisor - 1) * multiplier plus one carried fraction, which fits
 * int32_t for the supported parameters. No wide integer or software wide
 * division is needed, including for full-range input and saturated results.
 */
static inline int32_t runtime_scaler_scale(int32_t value, uint32_t multiplier, uint32_t divisor,
                                           int16_t *remainder) {
    /* A unit divisor cannot retain a fraction, including one from an old ratio.
     * Keep the placed pass-through and axis-kill stages free of multiplication
     * and division. Larger divisors must still spend their valid carried part. */
    if (divisor == 1U && multiplier <= 1U) {
        if (remainder != NULL) {
            *remainder = 0;
        }
        return multiplier == 0U ? 0 : value;
    }

    int32_t carried = 0;
    if (remainder != NULL) {
        /*
         * A remainder left by a different divisor is discarded rather than
         * carried in. It is a fraction of one count in the ratio that produced
         * it and means nothing in this one, and carrying it across a change
         * costs up to old_divisor / new_divisor counts of movement nobody
         * asked for -- 31 of them going from 889/500 to 1/16, which is a
         * visible jump on the first report after the edit rather than the
         * rounding difference the remainder exists to smooth.
         *
         * The ratio only changes when a client writes one, so this is a
         * comparison on a path that is otherwise free.
         */
        if (*remainder >= (int32_t)divisor || *remainder <= -(int32_t)divisor) {
            *remainder = 0;
        }

        carried = *remainder;
    }

    const int32_t mul = (int32_t)multiplier;
    const int32_t div = (int32_t)divisor;
    int32_t numerator;
    if (!__builtin_mul_overflow(value, mul, &numerator) &&
        !__builtin_add_overflow(numerator, carried, &numerator)) {
        if (remainder != NULL) {
            *remainder = (int16_t)(numerator % div);
        }
        return numerator / div;
    }

    const int32_t whole = value / div;
    const int32_t fraction = (value % div) * mul + carried;
    int32_t extra = fraction / div;
    int32_t rest = fraction % div;
    /* Truncation is applied to the full signed numerator, once. */
    if (whole > 0 && rest < 0) {
        extra--;
        rest += div;
    } else if (whole < 0 && rest > 0) {
        extra++;
        rest -= div;
    }
    if (remainder != NULL) {
        *remainder = (int16_t)rest;
    }

    int32_t output;
    if (__builtin_mul_overflow(whole, mul, &output) ||
        __builtin_add_overflow(output, extra, &output)) {
        return value < 0 ? INT32_MIN : INT32_MAX;
    }
    return output;
}
