/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Readers shared by this module's custom-settings bridges.
 *
 * Each bridge applies its processor's values only when every one of them
 * reads back as the type it was defined with, so a stored record of the wrong
 * shape leaves the processor on what it was already running. The readers live
 * once, in input_processors_custom_settings.c, which every bridge build links.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct zmk_custom_setting;

/* True, and *out set, when the setting reads back as a boolean. */
bool input_processors_read_bool(const struct zmk_custom_setting *setting, bool *out);

/* True, and *out set, when the setting reads back as a non-negative int32. */
bool input_processors_read_uint32(const struct zmk_custom_setting *setting, uint32_t *out);
