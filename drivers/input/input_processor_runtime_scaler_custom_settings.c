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
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>

#include <zmk-input-processors/custom_settings.h>
#include <zmk-input-processors/runtime_scaler.h>

#include "input_processors_custom_settings.h"

#ifndef CONFIG_ZMK_INPUT_PROCESSORS_LOG_LEVEL
#define CONFIG_ZMK_INPUT_PROCESSORS_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_input_processors_input_processor_runtime_scaler_custom_settings, CONFIG_ZMK_INPUT_PROCESSORS_LOG_LEVEL);

static K_MUTEX_DEFINE(settings_lock);

#define RUNTIME_SCALER_SETTING(n, field, key, lo)                                                  \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        runtime_scaler_cs_##field##_##n, ZMK_INPUT_PROCESSORS_SUBSYSTEM,                           \
        ZMK_INPUT_PROCESSORS_SETTING_KEY(n, key), ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,             \
        ZMK_CUSTOM_SETTING_VALUE_INT32(DT_INST_PROP(n, field)),                                    \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE,                                                      \
        ZMK_CUSTOM_SETTING_RANGE_INT32(lo, RUNTIME_SCALER_PARAM_MAX));

#define RUNTIME_SCALER_SETTINGS(n)                                                                 \
    ZMK_INPUT_PROCESSORS_ASSERT_NAME_FITS(n, "multiplier")                                         \
    RUNTIME_SCALER_SETTING(n, multiplier, "multiplier", RUNTIME_SCALER_MULTIPLIER_MIN)             \
    RUNTIME_SCALER_SETTING(n, divisor, "divisor", RUNTIME_SCALER_DIVISOR_MIN)

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_SCALER_SETTINGS)

/*
 * Applied as a pair. A client that moves one number at a time would otherwise
 * make the pointer briefly run at a ratio nobody chose, which on a large
 * multiplier is a visible jump rather than a rounding difference.
 */
#define RUNTIME_SCALER_APPLY(n)                                                                    \
    if (changed_setting == NULL || \
        changed_setting == &runtime_scaler_cs_multiplier_##n || \
        changed_setting == &runtime_scaler_cs_divisor_##n) { \
        uint32_t multiplier;                                                                       \
        uint32_t divisor;                                                                          \
        if (input_processors_read_uint32(&runtime_scaler_cs_multiplier_##n, &multiplier) &&        \
            input_processors_read_uint32(&runtime_scaler_cs_divisor_##n, &divisor)) {              \
            (void)runtime_scaler_set_params(DEVICE_DT_INST_GET(n), multiplier, divisor);           \
        }                                                                                          \
    }

static void runtime_scaler_apply_settings(const struct zmk_custom_setting *changed_setting) {
    /* Concurrent notifications must not apply an older snapshot last. */
    k_mutex_lock(&settings_lock, K_FOREVER);
    DT_INST_FOREACH_STATUS_OKAY(RUNTIME_SCALER_APPLY)
    k_mutex_unlock(&settings_lock);
}

static int runtime_scaler_settings_event_cb(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *changed = as_zmk_custom_setting_changed(eh);
    /* Boot loads every node. An edit must not reset another node's live state. */
    runtime_scaler_apply_settings(changed == NULL ? NULL : changed->setting);
    return ZMK_EV_EVENT_BUBBLE;
}

/*
 * Applied on two signals, and deliberately not from a SYS_INIT.
 *
 * zmk_custom_settings_initialized fires from the settings-subtree commit that
 * ends the boot settings_load pass, which is the only point at which a stored
 * value is both present and readable. A SYS_INIT is too early: it runs before
 * settings_load(), so it would read the devicetree default and leave the
 * processor on it for the rest of the session -- the value would persist and
 * show correctly in a client while having no effect on the hardware.
 *
 * The load path stores values without raising zmk_custom_setting_changed, so
 * that event alone would never deliver a stored value either. Together the two
 * cover boot and every later edit.
 *
 * In a build without CONFIG_SETTINGS nothing is stored and the event never
 * fires, which is correct: the driver already starts from its devicetree
 * values.
 */
ZMK_LISTENER(runtime_scaler_custom_settings, runtime_scaler_settings_event_cb);
ZMK_SUBSCRIPTION(runtime_scaler_custom_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(runtime_scaler_custom_settings, zmk_custom_settings_initialized);
