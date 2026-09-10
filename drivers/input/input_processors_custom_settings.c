/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Registers the namespace this module's parameters are published under.
 *
 * The subsystem answers no calls of its own. It exists because
 * custom-settings resolves a setting's subsystem identifier to an index
 * before it can put the setting on the wire, and a setting whose subsystem
 * was never registered is dropped with -ENOENT however correctly it was
 * defined. Registration and definition are complementary, not alternatives.
 *
 * Nothing here is a protocol: the values are read and written through
 * custom-settings' own RPC, which is what lets these processors appear in a
 * client that has no page for them. The advertised UI is the custom settings
 * editor.
 */

#include <zephyr/logging/log.h>

#include <cormoran/zmk/custom_settings.h>
#include <zmk/studio/custom.h>

#include <zmk-input-processors/custom_settings.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static bool input_processors_namespace_handler(const zmk_custom_CallRequest *request,
                                               pb_callback_t *encode_response);

static struct zmk_rpc_custom_subsystem_meta input_processors_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://cormoran.github.io/zmk-feature-custom-settings/"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

ZMK_RPC_CUSTOM_SUBSYSTEM(amgskobo__runtime_processors, &input_processors_meta,
                         input_processors_namespace_handler);

static bool input_processors_namespace_handler(const zmk_custom_CallRequest *request,
                                               pb_callback_t *encode_response) {
    ARG_UNUSED(request);
    ARG_UNUSED(encode_response);

    return false;
}
