/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Publishes each transform's orientation through zmk-feature-custom-settings.
 *
 * Three booleans are exactly what a generic settings list draws well, which
 * is the argument for declaring them here rather than inventing a protocol:
 * the client renders three checkboxes from the declared type, and this module
 * ships no page of its own. It is also the difference that makes them worth
 * publishing at all -- a pointer mounted the other way up is corrected in the
 * client instead of in a rebuild.
 *
 * This adapter publishes settings and applies coherent live snapshots.
 * The custom-settings registry owns persistence; the driver owns live state.
 */

#define DT_DRV_COMPAT zmk_input_processor_runtime_transform

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>

#include <zmk-input-processors/custom_settings.h>
#include <zmk-input-processors/runtime_transform.h>

#include "input_processors_custom_settings.h"

#ifndef CONFIG_ZMK_INPUT_PROCESSORS_LOG_LEVEL
#define CONFIG_ZMK_INPUT_PROCESSORS_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_input_processors_input_processor_runtime_transform_custom_settings, CONFIG_ZMK_INPUT_PROCESSORS_LOG_LEVEL);

static K_MUTEX_DEFINE(settings_lock);

#define RUNTIME_TRANSFORM_SETTING(n, field, key)                                                   \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        runtime_transform_cs_##field##_##n, ZMK_INPUT_PROCESSORS_SUBSYSTEM,                        \
        ZMK_INPUT_PROCESSORS_SETTING_KEY(n, key), ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,              \
        ZMK_CUSTOM_SETTING_VALUE_BOOL(DT_INST_PROP(n, field)),                                     \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

#define RUNTIME_TRANSFORM_SETTINGS(n)                                                              \
    ZMK_INPUT_PROCESSORS_ASSERT_NAME_FITS(n, "x_invert")                                           \
    RUNTIME_TRANSFORM_SETTING(n, xy_swap, "xy_swap")                                               \
    RUNTIME_TRANSFORM_SETTING(n, x_invert, "x_invert")                                             \
    RUNTIME_TRANSFORM_SETTING(n, y_invert, "y_invert")

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_TRANSFORM_SETTINGS)

/*
 * Applied as a set. A client moving one at a time would otherwise put the
 * pointer through an orientation nobody chose, and a swap combined with a
 * stale inversion is a visibly wrong direction rather than a small error.
 */
#define RUNTIME_TRANSFORM_APPLY(n)                                                                 \
    if (changed_setting == NULL || \
        changed_setting == &runtime_transform_cs_xy_swap_##n || \
        changed_setting == &runtime_transform_cs_x_invert_##n || \
        changed_setting == &runtime_transform_cs_y_invert_##n) { \
        struct runtime_transform_flags flags;                                                      \
        if (input_processors_read_bool(&runtime_transform_cs_xy_swap_##n, &flags.xy_swap) &&       \
            input_processors_read_bool(&runtime_transform_cs_x_invert_##n, &flags.x_invert) &&     \
            input_processors_read_bool(&runtime_transform_cs_y_invert_##n, &flags.y_invert)) {     \
            (void)runtime_transform_set_flags(DEVICE_DT_INST_GET(n), &flags);                      \
        }                                                                                          \
    }

static void runtime_transform_apply_settings(const struct zmk_custom_setting *changed_setting) {
    /* Concurrent notifications must not apply an older snapshot last. */
    k_mutex_lock(&settings_lock, K_FOREVER);
    DT_INST_FOREACH_STATUS_OKAY(RUNTIME_TRANSFORM_APPLY)
    k_mutex_unlock(&settings_lock);
}

static int runtime_transform_settings_event_cb(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *changed = as_zmk_custom_setting_changed(eh);
    /* Boot loads every node. An edit must not reset another node's live state. */
    runtime_transform_apply_settings(changed == NULL ? NULL : changed->setting);
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
ZMK_LISTENER(runtime_transform_custom_settings, runtime_transform_settings_event_cb);
ZMK_SUBSCRIPTION(runtime_transform_custom_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(runtime_transform_custom_settings, zmk_custom_settings_initialized);
