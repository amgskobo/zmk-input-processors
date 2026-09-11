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
 * This file owns persistence. The driver deliberately stores nothing, so
 * there is one owner for the value and no way for the two to disagree after a
 * reboot.
 */

#define DT_DRV_COMPAT zmk_input_processor_runtime_code_mapper

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>

#include <zmk-input-processors/custom_settings.h>
#include <zmk-input-processors/runtime_code_mapper.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * The devicetree property is start-disabled rather than enabled because a
 * Zephyr boolean is presence-based and so cannot default to true. The setting
 * a client sees is the plain positive one.
 */
#define RUNTIME_CODE_MAPPER_SETTINGS(n)                                                            \
    ZMK_INPUT_PROCESSORS_ASSERT_NAME_FITS(n, "enabled")                                            \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        runtime_code_mapper_cs_enabled_##n, ZMK_INPUT_PROCESSORS_SUBSYSTEM,                        \
        ZMK_INPUT_PROCESSORS_SETTING_KEY(n, "enabled"),                       \
        ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,                                                        \
        ZMK_CUSTOM_SETTING_VALUE_BOOL(!DT_INST_PROP(n, start_disabled)),                           \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_CODE_MAPPER_SETTINGS)

#define RUNTIME_CODE_MAPPER_APPLY(n)                                                               \
    {                                                                                              \
        struct zmk_custom_setting_value value;                                                     \
                                                                                                   \
        if (zmk_custom_setting_read(&runtime_code_mapper_cs_enabled_##n, &value) == 0 &&           \
            value.type == ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL) {                                    \
            (void)runtime_code_mapper_set_enabled(DEVICE_DT_INST_GET(n), value.bool_value);        \
        }                                                                                          \
    }

static void runtime_code_mapper_apply_settings(void) {
    DT_INST_FOREACH_STATUS_OKAY(RUNTIME_CODE_MAPPER_APPLY)
}

static int runtime_code_mapper_settings_event_cb(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    /*
     * Both subscribed events mean the same thing here -- some stored value may
     * now differ from what the processor is running -- and re-reading every
     * instance is cheaper than working out which one moved.
     */
    runtime_code_mapper_apply_settings();

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
