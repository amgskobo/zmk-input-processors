/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zmk-input-processors/runtime_temp_layer.h>

static inline bool runtime_temp_layer_values_valid(uint32_t layer, uint32_t timeout_ms,
                                                   uint32_t require_prior_idle_ms,
                                                   uint32_t layer_count) {
    return layer < layer_count && timeout_ms <= RUNTIME_TEMP_LAYER_MAX_MS &&
           require_prior_idle_ms <= RUNTIME_TEMP_LAYER_MAX_MS;
}

/* A press at t blocks activation for [t, t + require_prior_idle_ms); zero turns it off. */
static inline bool runtime_temp_layer_is_typing(int64_t last_tapped, uint16_t require_prior_idle_ms,
                                                int64_t now) {
    if (require_prior_idle_ms == 0) {
        return false;
    }

    if (now < last_tapped) {
        return true;
    }

    const uint64_t elapsed = (uint64_t)now - (uint64_t)last_tapped;

    return elapsed < require_prior_idle_ms;
}

static inline bool runtime_temp_layer_should_activate(bool enabled, bool is_active, bool typing) {
    return enabled && !is_active && !typing;
}

static inline bool
runtime_temp_layer_should_schedule_timeout(const struct runtime_temp_layer_params *params) {
    return params->enabled && params->timeout_ms > 0;
}

static inline bool runtime_temp_layer_should_drop_for_position(bool is_active, bool pressed,
                                                               bool has_excluded_positions,
                                                               bool is_excluded) {
    return is_active && pressed && has_excluded_positions && !is_excluded;
}
