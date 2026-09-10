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
 * The multiplication is done in int64_t. This is the whole point: ZMK's own
 * scaler holds the product in an int16_t, so a multiplier of 889 makes every
 * delta of 37 or more come out negative, and the pointer jumps backwards
 * exactly when it is moving fastest.
 *
 * The quotient can still leave int32_t even though the arithmetic cannot
 * overflow, so it saturates. Saturating keeps a large delta large; wrapping
 * would reverse it, which is the failure this function exists to avoid.
 */
static inline int32_t runtime_scaler_scale(int32_t value, uint32_t multiplier, uint32_t divisor,
                                        int16_t *remainder) {
    int64_t numerator = (int64_t)value * (int64_t)multiplier;

    if (remainder != NULL) {
        numerator += *remainder;
    }

    int64_t scaled = numerator / (int64_t)divisor;

    if (remainder != NULL) {
        /* |remainder| < divisor <= INT16_MAX, so the int16_t slot holds it. */
        *remainder = (int16_t)(numerator - scaled * (int64_t)divisor);
    }

    if (scaled > INT32_MAX) {
        return INT32_MAX;
    }

    if (scaled < INT32_MIN) {
        return INT32_MIN;
    }

    return (int32_t)scaled;
}
