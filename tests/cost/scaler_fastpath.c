/* SPDX-License-Identifier: MIT */
#include <stdint.h>

/* Count the checked products in the real header, without replacing its math. */
static unsigned checked_products;
#define __builtin_mul_overflow(a, b, result) \
    (checked_products++, __builtin_mul_overflow(a, b, result))
#include <zmk-input-processors/runtime_scaler_math.h>
#undef __builtin_mul_overflow

#include "../unit/test.h"

int main(void) {
    static const int32_t values[] = {INT32_MIN, -1, 0, 1, INT32_MAX};
    for (size_t i = 0; i < ARRAY_LEN(values); i++) {
        for (uint32_t multiplier = 0; multiplier <= 1; multiplier++) {
            const int32_t expected = multiplier == 0 ? 0 : values[i];
            EXPECT_EQ(runtime_scaler_scale(values[i], multiplier, 1, NULL), expected,
                      "untracked unit-divisor output");
            /* Every possible stale listener fraction must be discarded. */
            for (int32_t carried = INT16_MIN; carried <= INT16_MAX; carried++) {
                int16_t remainder = (int16_t)carried;
                EXPECT_EQ(runtime_scaler_scale(values[i], multiplier, 1, &remainder), expected,
                          "unit-divisor output with carry %" PRId32, carried);
                EXPECT_EQ(remainder, 0, "unit-divisor clears carry %" PRId32, carried);
            }
        }
    }
    EXPECT_EQ(checked_products, 0, "1/1 and 0/1 must skip generic checked multiplication");

    /* Other identity/zero ratios still own a valid fraction; never shortcut them. */
    int16_t remainder = -15;
    EXPECT_EQ(runtime_scaler_scale(1, 16, 16, &remainder), 0, "identity spends opposite carry");
    EXPECT_EQ(remainder, 1, "identity retains signed truncation");
    remainder = 15;
    EXPECT_EQ(runtime_scaler_scale(-1, 16, 16, &remainder), 0, "reverse identity spends carry");
    EXPECT_EQ(remainder, -1, "reverse identity retains signed truncation");
    remainder = 15;
    EXPECT_EQ(runtime_scaler_scale(INT32_MAX, 0, 16, &remainder), 0, "zero keeps valid carry");
    EXPECT_EQ(remainder, 15, "zero with larger divisor preserves fraction");
    EXPECT_EQ(runtime_scaler_scale(INT32_MAX, 2, 1, NULL), INT32_MAX,
              "unit divisor with gain still saturates");
    return test_report();
}
