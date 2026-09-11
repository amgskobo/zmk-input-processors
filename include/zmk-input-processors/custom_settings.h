/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * The custom-settings namespace shared by every processor in this module.
 *
 * One namespace rather than one per processor: a setting is dropped with
 * -ENOENT unless its subsystem is registered, and a client groups the list it
 * renders by subsystem. Every parameter in this module therefore arrives in a
 * client under one heading, and adding a processor adds settings to it rather
 * than another heading with one row in it.
 */

#pragma once

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#define ZMK_INPUT_PROCESSORS_SUBSYSTEM "amgskobo__runtime_processors"

/*
 * A setting key is the owning node's devicetree name, then the field:
 *
 *     runtime_pointer_scaler.mul
 *     runtime_pointer_scaler.div
 *     runtime_scroll_scaler.mul
 *
 * The node name is the one identifier both halves of the problem already hold.
 * A view drawing the chain walks devicetree for the processors in each
 * listener and gets a `const struct device *` per stage, whose ->name is
 * DEVICE_DT_NAME(), which is DT_NODE_FULL_NAME() -- the same string this
 * builds the key from. So a stage's settings are the ones whose key starts
 * with its device name, and nothing has to be registered, agreed between
 * modules, or typed into devicetree by a board author for that to hold.
 *
 * It also removes the commonest way to collide, since a name is no longer
 * hand-written. It does not make collision impossible: DT_NODE_FULL_NAME is a
 * node's own name and not its path, so devicetree keeps it unique only among
 * siblings, and a board node and a module node under different parents can
 * still pick the same one. That is what the startup check in
 * input_processors_custom_settings.c is for.
 *
 * The cost is that renaming a node orphans its stored value, and that keys are
 * as long as the node names -- capped at
 * CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN (48 bytes), which a too-long name
 * fails loudly against at build time rather than silently truncating.
 */
#define ZMK_INPUT_PROCESSORS_SETTING_KEY(n, field) DT_NODE_FULL_NAME(DT_DRV_INST(n)) "." field

/*
 * Fail by name when a node cannot fit a settings key.
 *
 * custom-settings already refuses a key over
 * CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN, but it can only say that some key
 * was too long: the key is built inside its own macro, so the message names
 * the settings file and not the devicetree node that caused it. Since the
 * length is now the node's name plus the field, the node is the only thing
 * anyone can act on, so it is worth asserting again to put it in the message.
 *
 * Pass the processor's longest field. The budget a node actually has is
 * KEY_MAX_LEN minus that field and the dot -- 28 characters where the longest
 * field is "suppress_btn_touch", and more for every other processor -- so
 * checking the longest is checking all of them.
 */
#define ZMK_INPUT_PROCESSORS_ASSERT_NAME_FITS(n, longest_field)                                    \
    BUILD_ASSERT(sizeof(ZMK_INPUT_PROCESSORS_SETTING_KEY(n, longest_field)) <=                     \
                     CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN,                                       \
                 "devicetree node \"" DT_NODE_FULL_NAME(DT_DRV_INST(n))                            \
                 "\" has a name too long to key its settings; shorten the node name");
