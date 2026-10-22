/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Publishes each mapper's switch through zmk-feature-custom-settings.
 *
 * One boolean, which a generic settings list draws as a checkbox, so this
 * needs no protocol and no page of its own. It is the switch that turns a
 * scroll route back into a pointer route, which is the reason the mapper is
 * worth having a runtime form at all.
 *
 * This adapter publishes settings and applies coherent live snapshots.
 * The custom-settings registry owns persistence; the driver owns live state.
 */

#define DT_DRV_COMPAT zmk_input_processor_runtime_code_mapper

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>

#include <zmk-input-processors/custom_settings.h>
#include <zmk-input-processors/runtime_code_mapper.h>

#include "input_processors_custom_settings.h"

#ifndef CONFIG_ZMK_INPUT_PROCESSORS_LOG_LEVEL
#define CONFIG_ZMK_INPUT_PROCESSORS_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_input_processors_input_processor_runtime_code_mapper_custom_settings, CONFIG_ZMK_INPUT_PROCESSORS_LOG_LEVEL);

static K_MUTEX_DEFINE(settings_lock);

/*
 * The devicetree property is start-disabled rather than enabled because a
 * Zephyr boolean is presence-based and so cannot default to true. The setting
 * a client sees is the plain positive one.
 */
#define RUNTIME_CODE_MAPPER_SETTINGS(n)                                                            \
    ZMK_INPUT_PROCESSORS_ASSERT_NAME_FITS(n, "enabled")                                            \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        runtime_code_mapper_cs_enabled_##n, ZMK_INPUT_PROCESSORS_SUBSYSTEM,                        \
        ZMK_INPUT_PROCESSORS_SETTING_KEY(n, "enabled"), ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,        \
        ZMK_CUSTOM_SETTING_VALUE_BOOL(!DT_INST_PROP(n, start_disabled)),                           \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_CODE_MAPPER_SETTINGS)

#define RUNTIME_CODE_MAPPER_APPLY(n)                                                               \
    if (changed_setting == NULL || \
        changed_setting == &runtime_code_mapper_cs_enabled_##n) { \
        bool enabled;                                                                              \
        if (input_processors_read_bool(&runtime_code_mapper_cs_enabled_##n, &enabled)) {           \
            (void)runtime_code_mapper_set_enabled(DEVICE_DT_INST_GET(n), enabled);                 \
        }                                                                                          \
    }

static void runtime_code_mapper_apply_settings(const struct zmk_custom_setting *changed_setting) {
    /* Concurrent notifications must not apply an older snapshot last. */
    k_mutex_lock(&settings_lock, K_FOREVER);
    DT_INST_FOREACH_STATUS_OKAY(RUNTIME_CODE_MAPPER_APPLY)
    k_mutex_unlock(&settings_lock);
}

static int runtime_code_mapper_settings_event_cb(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *changed = as_zmk_custom_setting_changed(eh);
    /* Boot loads every node. An edit must not reset another node's live state. */
    runtime_code_mapper_apply_settings(changed == NULL ? NULL : changed->setting);
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
ZMK_LISTENER(runtime_code_mapper_custom_settings, runtime_code_mapper_settings_event_cb);
ZMK_SUBSCRIPTION(runtime_code_mapper_custom_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(runtime_code_mapper_custom_settings, zmk_custom_settings_initialized);
