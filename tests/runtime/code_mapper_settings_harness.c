/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The code mapper's settings bridge: a readable switch reaches the driver, an
 * unreadable one leaves it as it is.
 */
#include "bridge_stubs.h"
#include <zmk-input-processors/runtime_code_mapper.h>

static struct zmk_custom_setting runtime_code_mapper_cs_enabled_0 = {true, 1};
static int calls;
static bool applied;

int runtime_code_mapper_set_enabled(const struct device *dev, bool enabled) {
    assert(dev == &instance);
    calls++;
    applied = enabled;
    return 0;
}

/* DRIVER_FUNCTIONS */

static void test_foreign_events(void) {
    const struct zmk_custom_setting foreign={true,1};
    const zmk_event_t unrelated={1,&foreign};
    int before=calls;
    assert(runtime_code_mapper_settings_event_cb(&unrelated)==ZMK_EV_EVENT_BUBBLE);
    assert(calls==before && "foreign setting must not reapply processor");
}

int main(void) {
    test_foreign_events();
    assert(runtime_code_mapper_settings_event_cb(&initialized_event) == ZMK_EV_EVENT_BUBBLE);
    assert(calls == 1 && applied);
    runtime_code_mapper_cs_enabled_0.value = 0;
    runtime_code_mapper_apply_settings(NULL);
    assert(calls == 2 && !applied);
    runtime_code_mapper_cs_enabled_0.readable = false;
    runtime_code_mapper_apply_settings(NULL);
    assert(calls == 2);
    struct zmk_custom_setting *own[]={&runtime_code_mapper_cs_enabled_0};
    int before=calls;
    for(size_t i=0;i<sizeof(own)/sizeof(*own);i++) { own[i]->readable=true; }
    for(size_t i=0;i<sizeof(own)/sizeof(*own);i++) {
        const zmk_event_t changed={1,own[i]};
        assert(runtime_code_mapper_settings_event_cb(&changed)==ZMK_EV_EVENT_BUBBLE);
        assert(calls==before+(int)i+1);
    }
    puts("code mapper settings bridge: PASS");
    return 0;
}
