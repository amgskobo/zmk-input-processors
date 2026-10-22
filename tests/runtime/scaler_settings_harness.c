/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The scaler's settings bridge: the ratio is applied as a pair or not at all,
 * so the pointer never runs at half of an edit.
 */
#include "bridge_stubs.h"
#include <zmk-input-processors/runtime_scaler.h>

static struct zmk_custom_setting runtime_scaler_cs_multiplier_0 = {true, 3};
static struct zmk_custom_setting runtime_scaler_cs_divisor_0 = {true, 2};
static int calls;
static uint32_t applied_multiplier, applied_divisor;

int runtime_scaler_set_params(const struct device *dev, uint32_t multiplier, uint32_t divisor) {
    assert(dev == &instance);
    calls++;
    applied_multiplier = multiplier;
    applied_divisor = divisor;
    return 0;
}

/* DRIVER_FUNCTIONS */

static void test_foreign_events(void) {
    const struct zmk_custom_setting foreign={true,1};
    const zmk_event_t unrelated={1,&foreign};
    int before=calls;
    assert(runtime_scaler_settings_event_cb(&unrelated)==ZMK_EV_EVENT_BUBBLE);
    assert(calls==before && "foreign setting must not reapply processor");
}

int main(void) {
    test_foreign_events();
    assert(runtime_scaler_settings_event_cb(&initialized_event) == ZMK_EV_EVENT_BUBBLE);
    assert(calls == 1 && applied_multiplier == 3 && applied_divisor == 2);

    /* Either half unreadable: nothing changes. */
    runtime_scaler_cs_multiplier_0.readable = false;
    runtime_scaler_apply_settings(NULL);
    runtime_scaler_cs_multiplier_0.readable = true;
    runtime_scaler_cs_divisor_0.readable = false;
    runtime_scaler_apply_settings(NULL);
    assert(calls == 1);
    struct zmk_custom_setting *own[]={&runtime_scaler_cs_multiplier_0,&runtime_scaler_cs_divisor_0};
    int before=calls;
    for(size_t i=0;i<sizeof(own)/sizeof(*own);i++) { own[i]->readable=true; }
    for(size_t i=0;i<sizeof(own)/sizeof(*own);i++) {
        const zmk_event_t changed={1,own[i]};
        assert(runtime_scaler_settings_event_cb(&changed)==ZMK_EV_EVENT_BUBBLE);
        assert(calls==before+(int)i+1);
    }
    puts("scaler settings bridge: PASS");
    return 0;
}
