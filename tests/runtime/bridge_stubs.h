/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * What the per-processor custom-settings bridges need on the host: one
 * devicetree instance, settings whose reads can fail, and the shared readers
 * (gated on their own in custom_settings_harness.c) reduced to those reads.
 */

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/device.h>

#define ARG_UNUSED(x) ((void)(x))
#define ZMK_EV_EVENT_BUBBLE 0
/* One instance: every instance expands the same code. */
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define DEVICE_DT_INST_GET(n) (&instance)

typedef struct { int kind; } zmk_event_t;
/* A stored setting: whether it reads back as its own type, and its value. */
struct zmk_custom_setting { bool readable; uint32_t value; };

static struct device instance = {.name = "instance"};

bool input_processors_read_bool(const struct zmk_custom_setting *setting, bool *out) {
    if (!setting->readable) {
        return false;
    }
    *out = setting->value != 0;
    return true;
}

bool input_processors_read_uint32(const struct zmk_custom_setting *setting, uint32_t *out) {
    if (!setting->readable) {
        return false;
    }
    *out = setting->value;
    return true;
}

/* Both subscribed events apply the settings the same way. */
static const zmk_event_t changed_event = {1};
