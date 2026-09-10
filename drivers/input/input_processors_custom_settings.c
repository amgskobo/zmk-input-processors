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

#include <string.h>

#include <zephyr/init.h>
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

/*
 * Two instances of one processor can end up sharing a settings key, and
 * nothing downstream will say so.
 *
 * A key ends in the node's setting-name, which is a free-form string a board
 * writes, so two nodes can carry the same one. The registry does not reject
 * that: zmk_custom_setting_find() returns the first match, so a client's write
 * always lands on whichever instance linked first, and the second one silently
 * keeps its devicetree value while appearing in the list as though it were
 * being edited. A stored value restores into only one of them too.
 *
 * The check cannot be a BUILD_ASSERT -- string equality across instances is
 * not something the preprocessor can evaluate -- so it runs once at startup
 * and says which key is duplicated. It walks descriptors, not values, so it
 * needs nothing from settings_load() and is safe this early.
 */
static int input_processors_check_unique_keys(void) {
    ZMK_CUSTOM_SETTING_FOREACH(setting) {
        if (strcmp(setting->custom_subsystem_id, ZMK_INPUT_PROCESSORS_SUBSYSTEM) != 0) {
            continue;
        }

        ZMK_CUSTOM_SETTING_FOREACH(other) {
            if (other == setting) {
                /* Only report a pair once: stop at the first of the two. */
                break;
            }

            if (strcmp(other->custom_subsystem_id, ZMK_INPUT_PROCESSORS_SUBSYSTEM) == 0 &&
                strcmp(other->key, setting->key) == 0) {
                LOG_ERR("Duplicate setting key \"%s\": give these nodes different "
                        "setting-name values, or only one of them is editable",
                        setting->key);
            }
        }
    }

    return 0;
}

SYS_INIT(input_processors_check_unique_keys, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
