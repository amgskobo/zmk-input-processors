/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The transform's settings bridge: the three flags go to the driver together,
 * and any one that cannot be read keeps all three as they are.
 */
#include "bridge_stubs.h"
#include <zmk-input-processors/runtime_transform.h>

static struct zmk_custom_setting runtime_transform_cs_xy_swap_0 = {true, 1};
static struct zmk_custom_setting runtime_transform_cs_x_invert_0 = {true, 0};
static struct zmk_custom_setting runtime_transform_cs_y_invert_0 = {true, 1};
static int calls;
static struct runtime_transform_flags applied;

int runtime_transform_set_flags(const struct device *dev,
                                const struct runtime_transform_flags *flags) {
    assert(dev == &instance);
    calls++;
    applied = *flags;
    return 0;
}

/* DRIVER_FUNCTIONS */

static void test_foreign_events(void) {
    const struct zmk_custom_setting foreign={true,1};
    const zmk_event_t unrelated={1,&foreign};
    int before=calls;
    assert(runtime_transform_settings_event_cb(&unrelated)==ZMK_EV_EVENT_BUBBLE);
    assert(calls==before && "foreign setting must not reapply processor");
}

int main(void) {
    test_foreign_events();
    assert(runtime_transform_settings_event_cb(&initialized_event) == ZMK_EV_EVENT_BUBBLE);
    assert(calls == 1 && applied.xy_swap && !applied.x_invert && applied.y_invert);

    struct zmk_custom_setting *flags[] = {&runtime_transform_cs_xy_swap_0,
                                          &runtime_transform_cs_x_invert_0,
                                          &runtime_transform_cs_y_invert_0};
    for (size_t i = 0; i < 3; i++) {
        flags[i]->readable = false;
        runtime_transform_apply_settings(NULL);
        flags[i]->readable = true;
    }
    assert(calls == 1);
    struct zmk_custom_setting *own[]={&runtime_transform_cs_xy_swap_0,&runtime_transform_cs_x_invert_0,&runtime_transform_cs_y_invert_0};
    int before=calls;
    for(size_t i=0;i<sizeof(own)/sizeof(*own);i++) { own[i]->readable=true; }
    for(size_t i=0;i<sizeof(own)/sizeof(*own);i++) {
        const zmk_event_t changed={1,own[i]};
        assert(runtime_transform_settings_event_cb(&changed)==ZMK_EV_EVENT_BUBBLE);
        assert(calls==before+(int)i+1);
    }
    puts("transform settings bridge: PASS");
    return 0;
}
