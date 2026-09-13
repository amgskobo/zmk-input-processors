/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * A self-test that runs twice against one flash file.
 *
 * The settings glue applies stored values when custom-settings reports that
 * loading has finished, because anything earlier reads the devicetree default:
 * the value would persist, and show correctly in a client, while having no
 * effect on the hardware. Only a second boot can show that. The first boot
 * stores new values; the second checks that every driver starts on them.
 */

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include <cormoran/zmk/custom_settings.h>

#include <zmk-input-processors/custom_settings.h>
#include <zmk-input-processors/runtime_code_mapper.h>
#include <zmk-input-processors/runtime_scaler.h>
#include <zmk-input-processors/runtime_temp_layer.h>
#include <zmk-input-processors/runtime_transform.h>

#include "runtime_test.h"

static const struct device *const scale = DEVICE_DT_GET(DT_NODELABEL(rt_scale));
static const struct device *const xform = DEVICE_DT_GET(DT_NODELABEL(rt_xform));
static const struct device *const map = DEVICE_DT_GET(DT_NODELABEL(rt_map));
static const struct device *const layer = DEVICE_DT_GET(DT_NODELABEL(rt_layer));

static const struct zmk_custom_setting *find(const char *key) {
    return zmk_custom_setting_find(ZMK_INPUT_PROCESSORS_SUBSYSTEM, key);
}

static int store_int(const char *key, int32_t value) {
    return zmk_custom_setting_write(find(key), &ZMK_CUSTOM_SETTING_VALUE_INT32(value),
                                    ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST);
}

static int store_bool(const char *key, bool value) {
    return zmk_custom_setting_write(find(key), &ZMK_CUSTOM_SETTING_VALUE_BOOL(value),
                                    ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST);
}

static void first_boot(void) {
    struct runtime_test t = {
        .name = "persistence: the first boot starts from devicetree and stores new values"};
    uint32_t multiplier = 0;
    uint32_t divisor = 0;

    RT_EXPECT_EQ(&t, runtime_scaler_get_params(scale, &multiplier, &divisor), 0, "read scaler");
    RT_EXPECT_TRUE(&t, multiplier == 889 && divisor == 500, "the devicetree ratio");

    RT_EXPECT_EQ(&t, store_int("rt_scale.multiplier", 2), 0, "store multiplier");
    RT_EXPECT_EQ(&t, store_int("rt_scale.divisor", 3), 0, "store divisor");
    RT_EXPECT_EQ(&t, store_bool("rt_xform.xy_swap", true), 0, "store swap");
    RT_EXPECT_EQ(&t, store_bool("rt_map.enabled", true), 0, "store mapper switch");
    RT_EXPECT_EQ(&t, store_int("rt_layer.layer", 2), 0, "store layer");
    RT_EXPECT_EQ(&t, store_int("rt_layer.timeout_ms", 250), 0, "store timeout");
    RT_EXPECT_EQ(&t, store_bool("rt_layer.enabled", false), 0, "store temp layer switch");

    rt_finish(&t);
}

static void second_boot(void) {
    struct runtime_test t = {
        .name = "persistence: the second boot starts every driver on the stored values"};
    uint32_t multiplier = 0;
    uint32_t divisor = 0;
    struct runtime_transform_flags flags = {0};
    bool enabled = false;
    struct runtime_temp_layer_params params = {0};

    RT_EXPECT_EQ(&t, runtime_scaler_get_params(scale, &multiplier, &divisor), 0, "read scaler");
    RT_EXPECT_EQ(&t, multiplier, 2, "stored multiplier");
    RT_EXPECT_EQ(&t, divisor, 3, "stored divisor");

    RT_EXPECT_EQ(&t, runtime_transform_get_flags(xform, &flags), 0, "read transform");
    RT_EXPECT_TRUE(&t, flags.xy_swap && !flags.x_invert && !flags.y_invert, "stored swap");

    RT_EXPECT_EQ(&t, runtime_code_mapper_get_enabled(map, &enabled), 0, "read mapper");
    RT_EXPECT_TRUE(&t, enabled, "stored mapper switch, over start-disabled");

    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read temp layer");
    RT_EXPECT_EQ(&t, params.layer, 2, "stored layer");
    RT_EXPECT_EQ(&t, params.timeout_ms, 250, "stored timeout");
    RT_EXPECT_FALSE(&t, params.enabled, "stored temp layer switch");
    RT_EXPECT_EQ(&t, params.require_prior_idle_ms, 30, "the idle guard nobody stored");

    rt_finish(&t);
}

static void run_tests(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct zmk_custom_setting_value stored;

    /* Nothing stored reads back as the devicetree default, so this tells the boots apart. */
    if (zmk_custom_setting_read(find("rt_scale.multiplier"), &stored) == 0 &&
        stored.int32_value == 2) {
        second_boot();
    } else {
        first_boot();
    }

    exit(0);
}

/* After boot, so after settings have loaded and been applied. */
K_THREAD_DEFINE(runtime_processor_persistence_test, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
