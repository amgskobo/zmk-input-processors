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

int main(void) {
    assert(runtime_code_mapper_settings_event_cb(&changed_event) == ZMK_EV_EVENT_BUBBLE);
    assert(calls == 1 && applied);
    runtime_code_mapper_cs_enabled_0.value = 0;
    runtime_code_mapper_apply_settings();
    assert(calls == 2 && !applied);
    runtime_code_mapper_cs_enabled_0.readable = false;
    runtime_code_mapper_apply_settings();
    assert(calls == 2);
    puts("code mapper settings bridge: PASS");
    return 0;
}
