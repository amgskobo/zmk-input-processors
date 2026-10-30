/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>
#include <zephyr/sys_clock.h>
#include <zmk-input-processors/runtime_temp_layer.h>

/* The published setting range fits a uint32 numerator at ordinary clock
 * rates. Larger remaining durations and faster clocks retain exact ceiling
 * through a seconds/fraction split. Only the final product uses the tick ABI. */
static inline k_ticks_t runtime_temp_layer_timeout_ticks(uint32_t ms, uint32_t hz) {
    if (ms <= RUNTIME_TEMP_LAYER_MAX_MS &&
        hz <= (UINT32_MAX - 999U) / RUNTIME_TEMP_LAYER_MAX_MS) {
        return (ms * hz + 999U) / 1000U;
    }
    const uint32_t seconds = ms / 1000U;
    const uint32_t remainder = ms % 1000U;
    const uint32_t fraction = remainder * (hz / 1000U) +
                              (remainder * (hz % 1000U) + 999U) / 1000U;
    return (k_ticks_t)seconds * hz + fraction;
}
