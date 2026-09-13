/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Pins the ready-made nodes in input_processor_runtime.dtsi to the defaults the
 * README documents for them, read the way a client reads them: by key.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <cormoran/zmk/custom_settings.h>

#include <zmk-input-processors/custom_settings.h>

#include "runtime_test.h"

static const struct {
    const char *key;
    enum zmk_custom_setting_value_type type;
    int32_t value;
} documented[] = {
    {"zip_rt_xy_scale.multiplier", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 1},
    {"zip_rt_xy_scale.divisor", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 1},
    {"zip_rt_xy_xform.xy_swap", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, false},
    {"zip_rt_xy_xform.x_invert", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, false},
    {"zip_rt_xy_xform.y_invert", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, false},
    {"zip_rt_scr_scale.multiplier", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 1},
    {"zip_rt_scr_scale.divisor", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 1},
    {"zip_rt_scr_xform.xy_swap", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, false},
    {"zip_rt_scr_xform.x_invert", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, false},
    {"zip_rt_scr_xform.y_invert", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, false},
    {"zip_rt_scr_map.enabled", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, true},
    {"zip_rt_tmp_layer.enabled", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, true},
    {"zip_rt_tmp_layer.layer", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 0},
    {"zip_rt_tmp_layer.timeout_ms", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 400},
    {"zip_rt_tmp_layer.prior_idle_ms", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 300},
};

static int check_documented_defaults(void) {
    struct runtime_test t = {.name = "defaults: the module's ready-made nodes start as documented"};

    for (size_t i = 0; i < ARRAY_SIZE(documented); i++) {
        const struct zmk_custom_setting *setting =
            zmk_custom_setting_find(ZMK_INPUT_PROCESSORS_SUBSYSTEM, documented[i].key);
        struct zmk_custom_setting_value value;

        if (setting == NULL) {
            printk("FAIL: %s: %s is not published\n", t.name, documented[i].key);
            t.failures++;
            continue;
        }

        RT_EXPECT_EQ(&t, setting->value_type, documented[i].type, documented[i].key);
        RT_EXPECT_EQ(&t, zmk_custom_setting_read_default(setting, &value), 0, documented[i].key);
        RT_EXPECT_EQ(&t,
                     documented[i].type == ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL ? value.bool_value
                                                                              : value.int32_value,
                     documented[i].value, documented[i].key);
    }

    rt_finish(&t);

    return 0;
}

SYS_INIT(check_documented_defaults, APPLICATION, 99);
