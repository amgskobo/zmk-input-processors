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
 * This file owns persistence. The driver deliberately stores nothing, so
 * there is one owner for the value and no way for the two to disagree after a
 * reboot.
 */

#define DT_DRV_COMPAT zmk_input_processor_runtime_transform

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>

#include <zmk-input-processors/custom_settings.h>
#include <zmk-input-processors/runtime_transform.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * Keys are qualified by instance because a board routes more than one
 * transform -- a pointer and a scroll route, say -- and each keeps its own
 * orientation.
 */
#define RUNTIME_TRANSFORM_SETTING(n, field, key)                                                   \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        runtime_transform_cs_##field##_##n, ZMK_INPUT_PROCESSORS_SUBSYSTEM,                        \
        "runtime_transform." key "." ZMK_INPUT_PROCESSORS_SETTING_NAME(n),                         \
        ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,                                                        \
        ZMK_CUSTOM_SETTING_VALUE_BOOL(DT_INST_PROP(n, field)),                                     \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

#define RUNTIME_TRANSFORM_SETTINGS(n)                                                              \
    RUNTIME_TRANSFORM_SETTING(n, xy_swap, "xy_swap")                                               \
    RUNTIME_TRANSFORM_SETTING(n, x_invert, "x_invert")                                             \
    RUNTIME_TRANSFORM_SETTING(n, y_invert, "y_invert")

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_TRANSFORM_SETTINGS)

static bool read_flag(const struct zmk_custom_setting *setting, bool *out) {
    struct zmk_custom_setting_value value;

    if (zmk_custom_setting_read(setting, &value) != 0 ||
        value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL) {
        return false;
    }

    *out = value.bool_value;

    return true;
}

/*
 * Applied as a set. A client moving one at a time would otherwise put the
 * pointer through an orientation nobody chose, and a swap combined with a
 * stale inversion is a visibly wrong direction rather than a small error.
 */
#define RUNTIME_TRANSFORM_APPLY(n)                                                                 \
    {                                                                                              \
        struct runtime_transform_flags flags;                                                      \
                                                                                                   \
        if (read_flag(&runtime_transform_cs_xy_swap_##n, &flags.xy_swap) &&                        \
            read_flag(&runtime_transform_cs_x_invert_##n, &flags.x_invert) &&                      \
            read_flag(&runtime_transform_cs_y_invert_##n, &flags.y_invert)) {                      \
            (void)runtime_transform_set_flags(DEVICE_DT_INST_GET(n), &flags);                      \
        }                                                                                          \
    }

static void runtime_transform_apply_settings(void) {
    DT_INST_FOREACH_STATUS_OKAY(RUNTIME_TRANSFORM_APPLY)
}

static int runtime_transform_setting_changed_cb(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *event = as_zmk_custom_setting_changed(eh);

    /* Cheap enough to re-read every instance rather than match the setting. */
    if (event != NULL) {
        runtime_transform_apply_settings();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(runtime_transform_custom_settings, runtime_transform_setting_changed_cb);
ZMK_SUBSCRIPTION(runtime_transform_custom_settings, zmk_custom_setting_changed);

/* Stored values land before this runs, so orientation starts where it was. */
static int runtime_transform_custom_settings_init(void) {
    runtime_transform_apply_settings();

    return 0;
}

SYS_INIT(runtime_transform_custom_settings_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
