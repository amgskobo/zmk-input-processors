/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the custom-settings glue, run inside a native_sim build.
 *
 * What a client sees is pinned first: every key, its type, its devicetree
 * default, its range and who may read or write it. Then what a client does:
 * a write reaches the driver, a value outside the declared range never does,
 * and the stored values are applied again when settings finish loading.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <cormoran/zmk/custom_settings.h>
#include <drivers/input_processor.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>

#include <zmk-input-processors/custom_settings.h>
#include <zmk-input-processors/runtime_code_mapper.h>
#include <zmk-input-processors/runtime_scaler.h>
#include <zmk-input-processors/runtime_temp_layer.h>
#include <zmk-input-processors/runtime_transform.h>

#include "runtime_test.h"

/*
 * Another module's setting, published under a key this module also uses. The
 * boot-time duplicate check has to leave it alone: keys only collide within a
 * subsystem. Named to sort ahead of this module's settings, so the check meets
 * it both as the setting it examines and as one it compares against.
 */
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    aa_foreign_rt_scale_multiplier, "rt_test_other", "rt_scale.multiplier",
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, ZMK_CUSTOM_SETTING_VALUE_INT32(1),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

static const struct device *const scale = DEVICE_DT_GET(DT_NODELABEL(rt_scale));
static const struct device *const xform = DEVICE_DT_GET(DT_NODELABEL(rt_xform));
static const struct device *const map = DEVICE_DT_GET(DT_NODELABEL(rt_map));
static const struct device *const layer = DEVICE_DT_GET(DT_NODELABEL(rt_layer));

#define LAYER DT_PROP(DT_NODELABEL(rt_layer), layer)
#define WORK_MS 5

struct expected_setting {
    const char *key;
    enum zmk_custom_setting_value_type type;
    int32_t default_value;
    bool ranged;
    int32_t min;
    int32_t max;
    bool layer_id;
};

static const struct expected_setting expected[] = {
    {"rt_scale.multiplier", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 889, true, 0, 32767, false},
    {"rt_scale.divisor", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 500, true, 1, 32767, false},
    {"rt_xform.xy_swap", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, false},
    {"rt_xform.x_invert", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, true},
    {"rt_xform.y_invert", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, false},
    {"rt_map.enabled", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, false},
    {"rt_layer.enabled", ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, true},
    {"rt_layer.layer", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 1, true, 0, ZMK_KEYMAP_LAYERS_LEN - 1,
     true},
    {"rt_layer.timeout_ms", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 200, true, 0, 60000, false},
    {"rt_layer.prior_idle_ms", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, 30, true, 0, 60000, false},
};

static const struct zmk_custom_setting *find(const char *key) {
    return zmk_custom_setting_find(ZMK_INPUT_PROCESSORS_SUBSYSTEM, key);
}

static const struct zmk_custom_setting_constraint *
constraint_of(const struct zmk_custom_setting *setting,
              enum zmk_custom_setting_constraint_type type) {
    for (uint8_t i = 0; i < setting->constraints_count; i++) {
        if (setting->constraints[i].type == type) {
            return &setting->constraints[i];
        }
    }

    return NULL;
}

static int write_int(const char *key, int32_t value) {
    return zmk_custom_setting_write(find(key), &ZMK_CUSTOM_SETTING_VALUE_INT32(value),
                                    ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
}

static int write_bool(const char *key, bool value) {
    return zmk_custom_setting_write(find(key), &ZMK_CUSTOM_SETTING_VALUE_BOOL(value),
                                    ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
}

static void test_published(void) {
    struct runtime_test t = {
        .name = "settings: every parameter is published with its type, default and range"};
    size_t published = 0;

    ZMK_CUSTOM_SETTING_FOREACH(setting) {
        if (strcmp(setting->custom_subsystem_id, ZMK_INPUT_PROCESSORS_SUBSYSTEM) == 0) {
            published++;
        }
    }
    /* The ten below, and the two keys each of the two twins publish. */
    RT_EXPECT_EQ(&t, published, ARRAY_SIZE(expected) + 4, "settings under the subsystem");

    for (size_t i = 0; i < ARRAY_SIZE(expected); i++) {
        const struct expected_setting *want = &expected[i];
        const struct zmk_custom_setting *setting = find(want->key);
        struct zmk_custom_setting_value value;

        if (setting == NULL) {
            printk("FAIL: %s: %s is not published\n", t.name, want->key);
            t.failures++;
            continue;
        }

        RT_EXPECT_EQ(&t, setting->value_type, want->type, want->key);
        RT_EXPECT_EQ(&t, setting->confidentiality, ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC,
                     want->key);
        RT_EXPECT_EQ(&t, setting->read_permission, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
                     want->key);
        RT_EXPECT_EQ(&t, setting->write_permission, ZMK_CUSTOM_SETTING_PERMISSION_SECURE,
                     want->key);

        RT_EXPECT_EQ(&t, zmk_custom_setting_read_default(setting, &value), 0, want->key);
        if (want->type == ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL) {
            RT_EXPECT_EQ(&t, value.bool_value, want->default_value, want->key);
        } else {
            RT_EXPECT_EQ(&t, value.int32_value, want->default_value, want->key);
        }

        const struct zmk_custom_setting_constraint *range =
            constraint_of(setting, ZMK_CUSTOM_SETTING_CONSTRAINT_RANGE);

        RT_EXPECT_EQ(&t, range != NULL, want->ranged, want->key);
        if (range != NULL && want->ranged) {
            RT_EXPECT_EQ(&t, range->range.min.int32_value, want->min, want->key);
            RT_EXPECT_EQ(&t, range->range.max.int32_value, want->max, want->key);
        }

        RT_EXPECT_EQ(&t, constraint_of(setting, ZMK_CUSTOM_SETTING_CONSTRAINT_LAYER_ID) != NULL,
                     want->layer_id, want->key);
    }

    rt_finish(&t);
}

static void test_twins(void) {
    struct runtime_test t = {.name = "settings: two nodes sharing a name publish one key twice"};
    size_t multipliers = 0;

    ZMK_CUSTOM_SETTING_FOREACH(setting) {
        if (strcmp(setting->custom_subsystem_id, ZMK_INPUT_PROCESSORS_SUBSYSTEM) == 0 &&
            strcmp(setting->key, "rt_twin.multiplier") == 0) {
            multipliers++;
        }
    }
    RT_EXPECT_EQ(&t, multipliers, 2, "rt_twin.multiplier descriptors");

    rt_finish(&t);
}

static void test_scaler_writes(void) {
    struct runtime_test t = {.name = "settings: a written ratio reaches the scaler"};
    uint32_t multiplier = 0;
    uint32_t divisor = 0;
    struct input_event event = {.type = INPUT_EV_REL, .code = INPUT_REL_X, .value = 9};
    struct zmk_input_processor_state state = {0};

    RT_EXPECT_EQ(&t, write_int("rt_scale.multiplier", 2), 0, "write multiplier");
    RT_EXPECT_EQ(&t, runtime_scaler_get_params(scale, &multiplier, &divisor), 0, "read");
    RT_EXPECT_EQ(&t, multiplier, 2, "multiplier applied");
    RT_EXPECT_EQ(&t, divisor, 500, "divisor kept");

    RT_EXPECT_EQ(&t, write_int("rt_scale.divisor", 3), 0, "write divisor");
    RT_EXPECT_EQ(&t, runtime_scaler_get_params(scale, &multiplier, &divisor), 0, "read");
    RT_EXPECT_EQ(&t, divisor, 3, "divisor applied");

    (void)zmk_input_processor_handle_event(scale, &event, 0, 0, &state);
    RT_EXPECT_EQ(&t, event.value, 6, "events scale by the written ratio");

    RT_EXPECT_EQ(&t, write_int("rt_scale.multiplier", 889), 0, "restore multiplier");
    RT_EXPECT_EQ(&t, write_int("rt_scale.divisor", 500), 0, "restore divisor");
    rt_finish(&t);
}

static void test_out_of_range(void) {
    struct runtime_test t = {
        .name = "settings: a value outside its declared range never reaches a driver"};
    uint32_t multiplier = 0;
    uint32_t divisor = 0;
    struct runtime_temp_layer_params params = {0};

    RT_EXPECT_TRUE(&t, write_int("rt_scale.divisor", 0) < 0, "zero divisor refused");
    RT_EXPECT_TRUE(&t, write_int("rt_scale.multiplier", 32768) < 0, "multiplier refused");
    RT_EXPECT_TRUE(&t, write_int("rt_scale.multiplier", -1) < 0, "negative multiplier refused");
    RT_EXPECT_EQ(&t, runtime_scaler_get_params(scale, &multiplier, &divisor), 0, "read");
    RT_EXPECT_TRUE(&t, multiplier == 889 && divisor == 500, "the scaler kept its ratio");

    RT_EXPECT_TRUE(&t, write_int("rt_layer.layer", ZMK_KEYMAP_LAYERS_LEN) < 0, "layer refused");
    RT_EXPECT_TRUE(&t, write_int("rt_layer.timeout_ms", 60001) < 0, "long timeout refused");
    RT_EXPECT_TRUE(&t, write_int("rt_layer.prior_idle_ms", -1) < 0, "negative idle refused");
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read");
    RT_EXPECT_EQ(&t, params.layer, LAYER, "the temp layer kept its layer");
    RT_EXPECT_EQ(&t, params.timeout_ms, 200, "the temp layer kept its timeout");
    RT_EXPECT_EQ(&t, params.require_prior_idle_ms, 30, "the temp layer kept its idle guard");

    rt_finish(&t);
}

static void test_transform_writes(void) {
    struct runtime_test t = {.name = "settings: written orientation flags reach the transform"};
    struct runtime_transform_flags flags = {0};

    RT_EXPECT_EQ(&t, write_bool("rt_xform.xy_swap", true), 0, "write swap");
    RT_EXPECT_EQ(&t, runtime_transform_get_flags(xform, &flags), 0, "read");
    RT_EXPECT_TRUE(&t, flags.xy_swap && flags.x_invert && !flags.y_invert, "swap added");

    RT_EXPECT_EQ(&t, write_bool("rt_xform.x_invert", false), 0, "write x-invert");
    RT_EXPECT_EQ(&t, write_bool("rt_xform.y_invert", true), 0, "write y-invert");
    RT_EXPECT_EQ(&t, runtime_transform_get_flags(xform, &flags), 0, "read");
    RT_EXPECT_TRUE(&t, flags.xy_swap && !flags.x_invert && flags.y_invert, "inversions moved");

    RT_EXPECT_EQ(&t, write_bool("rt_xform.xy_swap", false), 0, "restore swap");
    RT_EXPECT_EQ(&t, write_bool("rt_xform.x_invert", true), 0, "restore x-invert");
    RT_EXPECT_EQ(&t, write_bool("rt_xform.y_invert", false), 0, "restore y-invert");
    rt_finish(&t);
}

static void test_mapper_writes(void) {
    struct runtime_test t = {.name = "settings: the written switch reaches the code mapper"};
    bool enabled = true;

    RT_EXPECT_EQ(&t, runtime_code_mapper_get_enabled(map, &enabled), 0, "read");
    RT_EXPECT_FALSE(&t, enabled, "start-disabled at boot");

    RT_EXPECT_EQ(&t, write_bool("rt_map.enabled", true), 0, "switch on");
    RT_EXPECT_EQ(&t, runtime_code_mapper_get_enabled(map, &enabled), 0, "read");
    RT_EXPECT_TRUE(&t, enabled, "switched on");

    RT_EXPECT_EQ(&t, write_bool("rt_map.enabled", false), 0, "switch off");
    RT_EXPECT_EQ(&t, runtime_code_mapper_get_enabled(map, &enabled), 0, "read");
    RT_EXPECT_FALSE(&t, enabled, "switched off");

    rt_finish(&t);
}

static void test_temp_layer_writes(void) {
    struct runtime_test t = {
        .name = "settings: written temp layer values reach the driver, and off drops the layer"};
    struct runtime_temp_layer_params params = {0};
    struct input_event event = {.type = INPUT_EV_REL, .code = INPUT_REL_X, .value = 1};
    struct zmk_input_processor_state state = {0};

    RT_EXPECT_EQ(&t, write_int("rt_layer.timeout_ms", 250), 0, "write timeout");
    RT_EXPECT_EQ(&t, write_int("rt_layer.prior_idle_ms", 0), 0, "write idle guard");
    RT_EXPECT_EQ(&t, write_int("rt_layer.layer", 2), 0, "write layer");
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read");
    RT_EXPECT_EQ(&t, params.timeout_ms, 250, "timeout applied");
    RT_EXPECT_EQ(&t, params.require_prior_idle_ms, 0, "idle guard applied");
    RT_EXPECT_EQ(&t, params.layer, 2, "layer applied");

    RT_EXPECT_EQ(&t, write_int("rt_layer.layer", LAYER), 0, "restore layer");
    (void)zmk_input_processor_handle_event(layer, &event, 0, 0, &state);
    k_sleep(K_MSEC(WORK_MS));
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "movement raises the layer");

    RT_EXPECT_EQ(&t, write_bool("rt_layer.enabled", false), 0, "switch off");
    k_sleep(K_MSEC(WORK_MS));
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(LAYER), "switching off drops the layer");

    RT_EXPECT_EQ(&t, write_bool("rt_layer.enabled", true), 0, "restore switch");
    RT_EXPECT_EQ(&t, write_int("rt_layer.timeout_ms", 200), 0, "restore timeout");
    RT_EXPECT_EQ(&t, write_int("rt_layer.prior_idle_ms", 30), 0, "restore idle guard");
    rt_finish(&t);
}

static void test_reapplied_after_load(void) {
    struct runtime_test t = {
        .name = "settings: stored values are applied again when settings finish loading"};
    uint32_t multiplier = 0;
    uint32_t divisor = 0;
    struct runtime_transform_flags flags = {.xy_swap = true};
    struct runtime_temp_layer_params params = {0};
    bool enabled = true;

    /* Move every driver away from what the settings hold, behind their backs. */
    RT_EXPECT_EQ(&t, runtime_scaler_set_params(scale, 7, 7), 0, "diverge scaler");
    RT_EXPECT_EQ(&t, runtime_transform_set_flags(xform, &flags), 0, "diverge transform");
    RT_EXPECT_EQ(&t, runtime_code_mapper_set_enabled(map, true), 0, "diverge mapper");
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read temp layer");
    params.timeout_ms = 5;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "diverge temp layer");

    raise_zmk_custom_settings_initialized((struct zmk_custom_settings_initialized){0});

    RT_EXPECT_EQ(&t, runtime_scaler_get_params(scale, &multiplier, &divisor), 0, "read scaler");
    RT_EXPECT_TRUE(&t, multiplier == 889 && divisor == 500, "scaler reapplied");
    RT_EXPECT_EQ(&t, runtime_transform_get_flags(xform, &flags), 0, "read transform");
    RT_EXPECT_TRUE(&t, !flags.xy_swap && flags.x_invert && !flags.y_invert, "transform reapplied");
    RT_EXPECT_EQ(&t, runtime_code_mapper_get_enabled(map, &enabled), 0, "read mapper");
    RT_EXPECT_FALSE(&t, enabled, "mapper reapplied");
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read temp layer");
    RT_EXPECT_EQ(&t, params.timeout_ms, 200, "temp layer reapplied");

    rt_finish(&t);
}

static void run_tests(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    test_published();
    test_twins();
    test_scaler_writes();
    test_out_of_range();
    test_transform_writes();
    test_mapper_writes();
    test_temp_layer_writes();
    test_reapplied_after_load();

    exit(0);
}

/* After boot, at the lowest priority, so work items run whenever a test sleeps. */
K_THREAD_DEFINE(runtime_processor_settings_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
