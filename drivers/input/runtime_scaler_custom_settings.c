/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Publishes each runtime scaler's ratio through zmk-feature-custom-settings.
 *
 * The two numbers are plain bounded scalars, which is exactly what that
 * registry is for: declaring them here means a Studio client lists and edits
 * them with no page of its own, the declared type and range driving the
 * widget. A module only needs a protocol of its own when it has something to
 * show that a generic settings list cannot draw.
 *
 * This file also owns persistence. The driver deliberately stores nothing, so
 * there is one owner for the value and no way for the two to disagree after a
 * reboot.
 */

#define DT_DRV_COMPAT zmk_input_processor_runtime_scaler

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>

#include <zmk-input-processors/custom_settings.h>
#include <zmk-input-processors/runtime_scaler.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * Keys are qualified by instance because a board routes more than one scaler
 * -- a pointer speed, a scroll speed and an axis kill are all scalers -- and
 * each keeps its own ratio.
 */
#define RUNTIME_SCALER_SETTING(n, field, key, lo)                                                  \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        runtime_scaler_cs_##field##_##n, ZMK_INPUT_PROCESSORS_SUBSYSTEM,                           \
        "runtime_scaler." key "." ZMK_INPUT_PROCESSORS_SETTING_NAME(n),                            \
        ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,                                                       \
        ZMK_CUSTOM_SETTING_VALUE_INT32(DT_INST_PROP(n, field)),                                    \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE,                                                      \
        ZMK_CUSTOM_SETTING_RANGE_INT32(lo, RUNTIME_SCALER_PARAM_MAX));

#define RUNTIME_SCALER_SETTINGS(n)                                                                 \
    RUNTIME_SCALER_SETTING(n, multiplier, "mul", RUNTIME_SCALER_MULTIPLIER_MIN)                    \
    RUNTIME_SCALER_SETTING(n, divisor, "div", RUNTIME_SCALER_DIVISOR_MIN)

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_SCALER_SETTINGS)

static bool read_param(const struct zmk_custom_setting *setting, uint32_t *out) {
    struct zmk_custom_setting_value value;

    if (zmk_custom_setting_read(setting, &value) != 0 ||
        value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 || value.int32_value < 0) {
        return false;
    }

    *out = (uint32_t)value.int32_value;

    return true;
}

/*
 * Applied as a pair. A client that moves one number at a time would otherwise
 * make the pointer briefly run at a ratio nobody chose, which on a large
 * multiplier is a visible jump rather than a rounding difference.
 */
#define RUNTIME_SCALER_APPLY(n)                                                                    \
    {                                                                                              \
        uint32_t multiplier;                                                                       \
        uint32_t divisor;                                                                          \
                                                                                                   \
        if (read_param(&runtime_scaler_cs_multiplier_##n, &multiplier) &&                          \
            read_param(&runtime_scaler_cs_divisor_##n, &divisor)) {                                \
            (void)runtime_scaler_set_params(DEVICE_DT_INST_GET(n), multiplier, divisor);           \
        }                                                                                          \
    }

static void runtime_scaler_apply_settings(void) {
    DT_INST_FOREACH_STATUS_OKAY(RUNTIME_SCALER_APPLY)
}

static int runtime_scaler_setting_changed_cb(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *event = as_zmk_custom_setting_changed(eh);

    /* Cheap enough to re-read every instance rather than match the setting. */
    if (event != NULL) {
        runtime_scaler_apply_settings();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(runtime_scaler_custom_settings, runtime_scaler_setting_changed_cb);
ZMK_SUBSCRIPTION(runtime_scaler_custom_settings, zmk_custom_setting_changed);

/* Stored values land before this runs, so the ratio starts where it left off. */
static int runtime_scaler_custom_settings_init(void) {
    runtime_scaler_apply_settings();

    return 0;
}

SYS_INIT(runtime_scaler_custom_settings_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
