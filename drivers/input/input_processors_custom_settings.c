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
 * client that has no page for them.
 *
 * The advertised URL is this module's own documentation, which is the only
 * thing that explains what these settings do. A client shows it on the card
 * for a subsystem it does not recognise, and DYA Studio disconnects before
 * following it -- so pointing it at somebody else's page, as this did, sends
 * a reader away from their keyboard to a document about a different thing.
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
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://github.com/amgskobo/zmk-input-processors"),
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
 * Two nodes can still produce one key, and nothing downstream would say so.
 *
 * A key is the owning node's DT_NODE_FULL_NAME, which is the node's own name
 * and not its path, so devicetree keeps it unique only among its siblings. A
 * board that puts a processor under /input_processors and a module that puts
 * one at the root can pick the same name and neither Zephyr nor the settings
 * registry objects: zmk_custom_setting_find() returns the first match, so a
 * client's write always lands on whichever linked first while the second
 * silently keeps its devicetree values and appears in the list as though it
 * were being edited. A stored value restores into only one of them too.
 *
 * String equality across instances is not something the preprocessor can
 * evaluate, so the check runs once at startup and names the duplicated key. It
 * walks descriptors, not values, so it needs nothing from settings_load().
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
                LOG_ERR("Duplicate setting key \"%s\": two devicetree nodes share a "
                        "name, so only one of them is editable",
                        setting->key);
            }
        }
    }

    return 0;
}

SYS_INIT(input_processors_check_unique_keys, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
