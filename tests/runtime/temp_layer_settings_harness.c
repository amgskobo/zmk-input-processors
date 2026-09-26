/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The automatic layer's settings bridge: the switch, the layer and both
 * timers are applied as one set, and only when every value is in range, so a
 * stored value from a larger keymap or a hand-edited record changes nothing.
 */
#include "bridge_stubs.h"
#include <zmk-input-processors/runtime_temp_layer.h>
#include <zmk-input-processors/runtime_temp_layer_policy.h>

#define ZMK_KEYMAP_LAYERS_LEN 6

static struct zmk_custom_setting runtime_temp_layer_cs_enabled_0 = {true, 1};
static struct zmk_custom_setting runtime_temp_layer_cs_layer_0 = {true, 3};
static struct zmk_custom_setting runtime_temp_layer_cs_timeout_ms_0 = {true, 400};
static struct zmk_custom_setting runtime_temp_layer_cs_require_prior_idle_ms_0 = {true, 150};
static int calls;
static struct runtime_temp_layer_params applied;

int runtime_temp_layer_set_params(const struct device *dev,
                                  const struct runtime_temp_layer_params *params) {
    assert(dev == &instance);
    calls++;
    applied = *params;
    return 0;
}

/* DRIVER_FUNCTIONS */

int main(void) {
    assert(runtime_temp_layer_settings_event_cb(&changed_event) == ZMK_EV_EVENT_BUBBLE);
    assert(calls == 1 && applied.enabled && applied.layer == 3);
    assert(applied.timeout_ms == 400 && applied.require_prior_idle_ms == 150);

    /* Any value unreadable: nothing is applied. */
    struct zmk_custom_setting *values[] = {
        &runtime_temp_layer_cs_enabled_0, &runtime_temp_layer_cs_layer_0,
        &runtime_temp_layer_cs_timeout_ms_0, &runtime_temp_layer_cs_require_prior_idle_ms_0};
    for (size_t i = 0; i < 4; i++) {
        values[i]->readable = false;
        runtime_temp_layer_apply_settings();
        values[i]->readable = true;
    }
    assert(calls == 1);

    /* A layer this keymap lacks, or a timer past the bound: nothing either. */
    runtime_temp_layer_cs_layer_0.value = ZMK_KEYMAP_LAYERS_LEN;
    runtime_temp_layer_apply_settings();
    runtime_temp_layer_cs_layer_0.value = 3;
    runtime_temp_layer_cs_timeout_ms_0.value = RUNTIME_TEMP_LAYER_MAX_MS + 1;
    runtime_temp_layer_apply_settings();
    assert(calls == 1);

    /* The largest values in range are applied whole. */
    runtime_temp_layer_cs_enabled_0.value = 0;
    runtime_temp_layer_cs_timeout_ms_0.value = RUNTIME_TEMP_LAYER_MAX_MS;
    runtime_temp_layer_cs_require_prior_idle_ms_0.value = RUNTIME_TEMP_LAYER_MAX_MS;
    runtime_temp_layer_apply_settings();
    assert(calls == 2 && !applied.enabled);
    assert(applied.require_prior_idle_ms == RUNTIME_TEMP_LAYER_MAX_MS);
    puts("temp layer settings bridge: PASS");
    return 0;
}
