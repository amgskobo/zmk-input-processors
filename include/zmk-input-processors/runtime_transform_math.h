/* SPDX-License-Identifier: MIT */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zmk-input-processors/runtime_transform.h>

static inline int runtime_transform_code_index(uint16_t code, const uint16_t *list, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (list[i] == code) {
            return (int)i;
        }
    }
    return -1;
}

static inline void runtime_transform_apply(uint16_t *code, int32_t *value,
                                           const struct runtime_transform_flags *flags,
                                           const uint16_t *x_codes, const uint16_t *y_codes,
                                           size_t codes_len) {
    if (flags->xy_swap) {
        int idx = runtime_transform_code_index(*code, x_codes, codes_len);
        if (idx >= 0) {
            *code = y_codes[idx];
        } else {
            idx = runtime_transform_code_index(*code, y_codes, codes_len);
            if (idx >= 0) {
                *code = x_codes[idx];
            }
        }
    }

    /*
     * Read after the swap, so a swapped event is inverted according to the
     * axis it now travels on rather than the one it arrived on. That is
     * upstream's order; reversing it would make swap and invert interact.
     */
    if ((flags->x_invert && runtime_transform_code_index(*code, x_codes, codes_len) >= 0) ||
        (flags->y_invert && runtime_transform_code_index(*code, y_codes, codes_len) >= 0)) {
        /* -INT32_MIN overflows, and a clamping scaler upstream can produce it. */
        *value = *value == INT32_MIN ? INT32_MAX : -*value;
    }
}
