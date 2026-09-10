/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Publishes each safe scaler's ratio through zmk-feature-custom-settings.
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

#define DT_DRV_COMPAT zmk_input_processor_safe_scaler

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>

#include <zmk-input-processors/custom_settings.h>
#include <zmk-input-processors/safe_scaler.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * Keys carry the devicetree instance index because a board routes more than
 * one scaler -- a pointer speed and a scroll speed, say -- and each keeps its
 * own ratio.
 */
#define SAFE_SCALER_SETTING(n, field, key, lo)                                                     \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        safe_scaler_cs_##field##_##n, ZMK_INPUT_PROCESSORS_SUBSYSTEM, "safe_scaler." key "." #n,   \
        ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,                                                       \
        ZMK_CUSTOM_SETTING_VALUE_INT32(DT_INST_PROP(n, field)),                                    \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE,                                                      \
        ZMK_CUSTOM_SETTING_RANGE_INT32(lo, SAFE_SCALER_PARAM_MAX));

#define SAFE_SCALER_SETTINGS(n)                                                                    \
    SAFE_SCALER_SETTING(n, multiplier, "mul", SAFE_SCALER_MULTIPLIER_MIN)                          \
    SAFE_SCALER_SETTING(n, divisor, "div", SAFE_SCALER_DIVISOR_MIN)

DT_INST_FOREACH_STATUS_OKAY(SAFE_SCALER_SETTINGS)

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
#define SAFE_SCALER_APPLY(n)                                                                       \
    {                                                                                              \
        uint32_t multiplier;                                                                       \
        uint32_t divisor;                                                                          \
                                                                                                   \
        if (read_param(&safe_scaler_cs_multiplier_##n, &multiplier) &&                             \
            read_param(&safe_scaler_cs_divisor_##n, &divisor)) {                                   \
            (void)safe_scaler_set_params(DEVICE_DT_INST_GET(n), multiplier, divisor);              \
        }                                                                                          \
    }

static void safe_scaler_apply_settings(void) { DT_INST_FOREACH_STATUS_OKAY(SAFE_SCALER_APPLY) }

static int safe_scaler_setting_changed_cb(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *event = as_zmk_custom_setting_changed(eh);

    /* Cheap enough to re-read every instance rather than match the setting. */
    if (event != NULL) {
        safe_scaler_apply_settings();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(safe_scaler_custom_settings, safe_scaler_setting_changed_cb);
ZMK_SUBSCRIPTION(safe_scaler_custom_settings, zmk_custom_setting_changed);

/* Stored values land before this runs, so the ratio starts where it left off. */
static int safe_scaler_custom_settings_init(void) {
    safe_scaler_apply_settings();

    return 0;
}

SYS_INIT(safe_scaler_custom_settings_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
