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
 * It also makes a duplicate key impossible rather than merely detectable:
 * devicetree node names are unique by construction, where a hand-written name
 * could be repeated on two nodes and silently shadow one of them.
 *
 * The cost is that renaming a node orphans its stored value, and that keys are
 * as long as the node names -- capped at
 * CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN (48 bytes), which a too-long name
 * fails loudly against at build time rather than silently truncating.
 */
#define ZMK_INPUT_PROCESSORS_SETTING_KEY(n, field) DT_NODE_FULL_NAME(DT_DRV_INST(n)) "." field
