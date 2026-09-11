/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Publishes each converter's suppression flags through
 * zmk-feature-custom-settings.
 *
 * Both flags decide whether a pad's physical click reaches the host at all,
 * which is the kind of thing that wants trying rather than deciding: on a pad
 * that also carries tap-to-click, one setting is a duplicate button and the
 * other is a missing one, and which is which depends on the pad.
 *
 * This file owns persistence. The driver deliberately stores nothing, so
 * there is one owner for the value and no way for the two to disagree after a
 * reboot.
 */

#define DT_DRV_COMPAT zmk_input_processor_absolute_to_relative

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>

#include <zmk-input-processors/absolute_to_relative.h>
#include <zmk-input-processors/custom_settings.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define ABSOLUTE_TO_RELATIVE_SETTING(n, field, key)                                                \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        absolute_to_relative_cs_##field##_##n, ZMK_INPUT_PROCESSORS_SUBSYSTEM,                     \
        ZMK_INPUT_PROCESSORS_SETTING_KEY(n, key),                                \
        ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,                                                        \
        ZMK_CUSTOM_SETTING_VALUE_BOOL(DT_INST_PROP_OR(n, field, false)),                           \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

/*
 * The key keeps the "suppress" that the devicetree property carries. Dropped,
 * the row reads btn0 = true where the value means the button is being taken
 * away, and a client has nothing to show that from: a key is the label, and a
 * label that hides a negation is worse than a long one.
 */
#define ABSOLUTE_TO_RELATIVE_SETTINGS(n)                                                           \
    ABSOLUTE_TO_RELATIVE_SETTING(n, suppress_btn_touch, "suppress_btn_touch")                      \
    ABSOLUTE_TO_RELATIVE_SETTING(n, suppress_btn0, "suppress_btn0")

DT_INST_FOREACH_STATUS_OKAY(ABSOLUTE_TO_RELATIVE_SETTINGS)

static bool read_flag(const struct zmk_custom_setting *setting, bool *out) {
    struct zmk_custom_setting_value value;

    if (zmk_custom_setting_read(setting, &value) != 0 ||
        value.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL) {
        return false;
    }

    *out = value.bool_value;

    return true;
}

#define ABSOLUTE_TO_RELATIVE_APPLY(n)                                                              \
    {                                                                                              \
        struct absolute_to_relative_suppression flags;                                             \
                                                                                                   \
        if (read_flag(&absolute_to_relative_cs_suppress_btn_touch_##n, &flags.btn_touch) &&        \
            read_flag(&absolute_to_relative_cs_suppress_btn0_##n, &flags.btn0)) {                  \
            (void)absolute_to_relative_set_suppression(DEVICE_DT_INST_GET(n), &flags);             \
        }                                                                                          \
    }

static void absolute_to_relative_apply_settings(void) {
    DT_INST_FOREACH_STATUS_OKAY(ABSOLUTE_TO_RELATIVE_APPLY)
}

static int absolute_to_relative_settings_event_cb(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    /*
     * Both subscribed events mean the same thing here -- some stored value may
     * now differ from what the processor is running -- and re-reading every
     * instance is cheaper than working out which one moved.
     */
    absolute_to_relative_apply_settings();

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
ZMK_LISTENER(absolute_to_relative_custom_settings, absolute_to_relative_settings_event_cb);
ZMK_SUBSCRIPTION(absolute_to_relative_custom_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(absolute_to_relative_custom_settings, zmk_custom_settings_initialized);
