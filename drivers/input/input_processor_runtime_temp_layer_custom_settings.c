/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Publishes each automatic layer's three numbers through
 * zmk-feature-custom-settings.
 *
 * These are the values a person revises after living with a pointer for a
 * week -- how long the layer stays up, and how recently a keypress blocks it
 * from coming up -- and they are plain bounded scalars, which a generic
 * settings list draws without any help. Publishing them is what lets someone
 * converge on values that suit them instead of guessing once at build time.
 *
 * This file owns persistence. The driver deliberately stores nothing, so
 * there is one owner for the value and no way for the two to disagree after a
 * reboot.
 */

#define DT_DRV_COMPAT zmk_input_processor_runtime_temp_layer

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>

#include <zmk-input-processors/custom_settings.h>
#include <zmk-input-processors/runtime_temp_layer.h>
#include <zmk-input-processors/runtime_temp_layer_policy.h>

#include "input_processors_custom_settings.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define RUNTIME_TEMP_LAYER_SETTING(n, field, key, ...)                                             \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        runtime_temp_layer_cs_##field##_##n, ZMK_INPUT_PROCESSORS_SUBSYSTEM,                       \
        ZMK_INPUT_PROCESSORS_SETTING_KEY(n, key), ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,             \
        ZMK_CUSTOM_SETTING_VALUE_INT32(DT_INST_PROP_OR(n, field, 0)),                              \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, __VA_ARGS__);

/*
 * The switch that gives this stage a no-op.
 *
 * Every other processor here can be made to pass through by editing its own
 * values -- a scaler at mul == div, a transform with no flags set -- so a
 * chain is reshaped by changing numbers rather than by rebuilding it. This one
 * has no such value, because a timeout of zero means "never drop by timeout"
 * rather than "never raise". The devicetree property is the negative one
 * because a Zephyr boolean is presence-based and so cannot default to true;
 * the setting a client sees is the plain positive one.
 */
#define RUNTIME_TEMP_LAYER_ENABLED(n)                                                              \
    ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(                                                    \
        runtime_temp_layer_cs_enabled_##n, ZMK_INPUT_PROCESSORS_SUBSYSTEM,                         \
        ZMK_INPUT_PROCESSORS_SETTING_KEY(n, "enabled"), ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,        \
        ZMK_CUSTOM_SETTING_VALUE_BOOL(!DT_INST_PROP(n, start_disabled)),                           \
        ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,     \
        ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_NO_CONSTRAINT);

/*
 * The layer is declared as a layer, not as a number in a range.
 *
 * A client that understands the constraint draws the keymap's own layer list,
 * so the row reads "MOUSE (3)" instead of "3" -- and a layer is the one value
 * here that a person cannot sanity-check by looking at it. The range is
 * declared alongside so a client that does not understand LAYER_ID still
 * refuses an impossible number rather than falling back to unbounded input.
 *
 * Neither is trusted: runtime_temp_layer_set_params() rejects a layer outside
 * the keymap regardless, because a constraint is a drawing hint that reaches
 * the client, not a rule the firmware is entitled to assume was obeyed.
 */
#define RUNTIME_TEMP_LAYER_SETTINGS(n)                                                             \
    ZMK_INPUT_PROCESSORS_ASSERT_NAME_FITS(n, "prior_idle_ms")                                      \
    RUNTIME_TEMP_LAYER_ENABLED(n)                                                                  \
    RUNTIME_TEMP_LAYER_SETTING(n, layer, "layer", ZMK_CUSTOM_SETTING_LAYER_ID,                     \
                               ZMK_CUSTOM_SETTING_RANGE_INT32(0, ZMK_KEYMAP_LAYERS_LEN - 1))       \
    RUNTIME_TEMP_LAYER_SETTING(n, timeout_ms, "timeout_ms",                                        \
                               ZMK_CUSTOM_SETTING_RANGE_INT32(0, RUNTIME_TEMP_LAYER_MAX_MS))       \
    RUNTIME_TEMP_LAYER_SETTING(n, require_prior_idle_ms, "prior_idle_ms",                          \
                               ZMK_CUSTOM_SETTING_RANGE_INT32(0, RUNTIME_TEMP_LAYER_MAX_MS))

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_TEMP_LAYER_SETTINGS)

/*
 * Applied as a set, so the layer and the timers never come from different
 * generations of the same edit.
 */
#define RUNTIME_TEMP_LAYER_APPLY(n)                                                                \
    {                                                                                              \
        struct runtime_temp_layer_params params;                                                   \
        uint32_t layer;                                                                            \
        uint32_t prior_idle;                                                                       \
                                                                                                   \
        if (input_processors_read_bool(&runtime_temp_layer_cs_enabled_##n, &params.enabled) &&     \
            input_processors_read_uint32(&runtime_temp_layer_cs_layer_##n, &layer) &&              \
            input_processors_read_uint32(&runtime_temp_layer_cs_timeout_ms_##n,                    \
                                         &params.timeout_ms) &&                                    \
            input_processors_read_uint32(&runtime_temp_layer_cs_require_prior_idle_ms_##n,         \
                                         &prior_idle) &&                                           \
            runtime_temp_layer_values_valid(layer, params.timeout_ms, prior_idle,                  \
                                            ZMK_KEYMAP_LAYERS_LEN)) {                              \
            params.layer = (uint8_t)layer;                                                         \
            params.require_prior_idle_ms = (uint16_t)prior_idle;                                   \
            (void)runtime_temp_layer_set_params(DEVICE_DT_INST_GET(n), &params);                   \
        }                                                                                          \
    }

static void runtime_temp_layer_apply_settings(void) {
    DT_INST_FOREACH_STATUS_OKAY(RUNTIME_TEMP_LAYER_APPLY)
}

static int runtime_temp_layer_settings_event_cb(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    /*
     * Both subscribed events mean the same thing here -- some stored value may
     * now differ from what the processor is running -- and re-reading every
     * instance is cheaper than working out which one moved.
     */
    runtime_temp_layer_apply_settings();

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
ZMK_LISTENER(runtime_temp_layer_custom_settings, runtime_temp_layer_settings_event_cb);
ZMK_SUBSCRIPTION(runtime_temp_layer_custom_settings, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(runtime_temp_layer_custom_settings, zmk_custom_settings_initialized);
