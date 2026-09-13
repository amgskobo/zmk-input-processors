/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Checks the Studio subsystem this module's settings are published under.
 *
 * custom-settings drops every setting whose subsystem is not registered, and
 * the registration spells the name as a token where the settings spell it as a
 * string. If the two ever disagreed, everything would still build, and every
 * setting would silently vanish from clients, so the name is checked where the
 * two meet.
 */

#include <string.h>

#include <zephyr/init.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/printk.h>

#include <zmk/studio/custom.h>

#include <zmk-input-processors/custom_settings.h>

#include "runtime_test.h"

static int check_subsystem(void) {
    struct runtime_test t = {
        .name = "settings: the subsystem is registered under the same name and refuses calls"};
    size_t registered = 0;

    STRUCT_SECTION_FOREACH(zmk_rpc_custom_subsystem, subsystem) {
        if (strcmp(subsystem->identifier, ZMK_INPUT_PROCESSORS_SUBSYSTEM) != 0) {
            continue;
        }

        registered++;

        if (subsystem->meta == NULL) {
            printk("FAIL: %s: the subsystem has no metadata\n", t.name);
            t.failures++;
        } else {
            RT_EXPECT_EQ(&t, subsystem->meta->security, ZMK_STUDIO_RPC_HANDLER_UNSECURED,
                         "security");
            RT_EXPECT_EQ(&t, subsystem->meta->ui_urls_count, 1, "documentation URLs");
            RT_EXPECT_TRUE(&t,
                           subsystem->meta->ui_urls_count == 1 &&
                               strcmp(subsystem->meta->ui_urls[0],
                                      "https://github.com/amgskobo/zmk-input-processors") == 0,
                           "the URL is this module's own documentation");
        }

        zmk_custom_CallRequest request = zmk_custom_CallRequest_init_zero;
        pb_callback_t response = {0};

        RT_EXPECT_FALSE(&t, subsystem->handler(&request, &response), "a call is refused");
    }

    RT_EXPECT_EQ(&t, registered, 1, "registrations under the settings' subsystem");
    rt_finish(&t);

    return 0;
}

/* After the duplicate-key check, which runs at the default application priority. */
SYS_INIT(check_subsystem, APPLICATION, 99);
