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
 * The last part of a setting key, identifying which instance it belongs to.
 *
 * A board routes several instances of the same processor -- a pointer speed, a
 * scroll speed and an axis kill are all scalers -- and in a client they are
 * otherwise identically named rows told apart only by a number whose order
 * comes from however devicetree happened to enumerate the nodes. So a node may
 * name itself with setting-name, and falls back to that number only when it
 * does not.
 *
 * Keys are capped at CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN (48 bytes)
 * including the processor and parameter parts, which a short word clears
 * easily.
 */
#define ZMK_INPUT_PROCESSORS_SETTING_NAME(n) DT_INST_PROP_OR(n, setting_name, STRINGIFY(n))
