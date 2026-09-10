/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Runtime-adjustable replacement for ZMK's stock transform processor.
 *
 * The event path is upstream's, unchanged: swap the X and Y codes if asked,
 * then negate the value if the code is on an inverted axis. One thing differs.
 *
 * The three flags are named devicetree properties held in RAM, instead of bits
 * of the chain's first parameter cell held in rodata. That is what gives them
 * names a client can address, and what lets a trackpad mounted the other way
 * up be corrected with a checkbox rather than a rebuild.
 */

#define DT_DRV_COMPAT zmk_input_processor_runtime_transform

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>

#include <zmk-input-processors/runtime_transform.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct runtime_transform_config {
    uint8_t type;
    size_t x_codes_len;
    size_t y_codes_len;
    const uint16_t *x_codes;
    const uint16_t *y_codes;
    struct runtime_transform_flags defaults;
};

struct runtime_transform_data {
    struct runtime_transform_flags flags;
};

int runtime_transform_get_flags(const struct device *dev, struct runtime_transform_flags *out) {
    if (dev == NULL || out == NULL) {
        return -EINVAL;
    }

    const struct runtime_transform_data *data = dev->data;

    *out = data->flags;

    return 0;
}

int runtime_transform_set_flags(const struct device *dev,
                                const struct runtime_transform_flags *flags) {
    if (dev == NULL || flags == NULL) {
        return -EINVAL;
    }

    struct runtime_transform_data *data = dev->data;

    unsigned int key = irq_lock();

    data->flags = *flags;

    irq_unlock(key);

    LOG_DBG("%s: swap %d, invert x %d y %d", dev->name, flags->xy_swap, flags->x_invert,
            flags->y_invert);

    return 0;
}

static int code_idx(uint16_t code, const uint16_t *list, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (list[i] == code) {
            return (int)i;
        }
    }

    return -ENODEV;
}

static int runtime_transform_handle_event(const struct device *dev, struct input_event *event,
                                          uint32_t param1, uint32_t param2,
                                          struct zmk_input_processor_state *state) {
    /* The flags are on the node; the chain declares zero cells. */
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    const struct runtime_transform_config *config = dev->config;
    struct runtime_transform_data *data = dev->data;

    if (event->type != config->type) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    unsigned int key = irq_lock();
    const struct runtime_transform_flags flags = data->flags;
    irq_unlock(key);

    if (flags.xy_swap) {
        int idx = code_idx(event->code, config->x_codes, config->x_codes_len);

        if (idx >= 0) {
            event->code = config->y_codes[idx];
        } else {
            idx = code_idx(event->code, config->y_codes, config->y_codes_len);

            if (idx >= 0) {
                event->code = config->x_codes[idx];
            }
        }
    }

    /*
     * Read after the swap, so a swapped event is inverted according to the
     * axis it now travels on rather than the one it arrived on. That is
     * upstream's order; reversing it would make swap and invert interact.
     */
    if ((flags.x_invert && code_idx(event->code, config->x_codes, config->x_codes_len) >= 0) ||
        (flags.y_invert && code_idx(event->code, config->y_codes, config->y_codes_len) >= 0)) {
        event->value = -event->value;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int runtime_transform_init(const struct device *dev) {
    const struct runtime_transform_config *config = dev->config;
    struct runtime_transform_data *data = dev->data;

    data->flags = config->defaults;

    return 0;
}

static const struct zmk_input_processor_driver_api runtime_transform_driver_api = {
    .handle_event = runtime_transform_handle_event,
};

#define RUNTIME_TRANSFORM_INST(n)                                                                  \
    static const uint16_t runtime_transform_x_codes_##n[] = DT_INST_PROP(n, x_codes);              \
    static const uint16_t runtime_transform_y_codes_##n[] = DT_INST_PROP(n, y_codes);              \
    BUILD_ASSERT(ARRAY_SIZE(runtime_transform_x_codes_##n) ==                                      \
                     ARRAY_SIZE(runtime_transform_y_codes_##n),                                    \
                 "x-codes and y-codes must be the same length: a swap pairs them by index");       \
    static const struct runtime_transform_config runtime_transform_config_##n = {                  \
        .type = DT_INST_PROP_OR(n, type, INPUT_EV_REL),                                            \
        .x_codes_len = DT_INST_PROP_LEN(n, x_codes),                                               \
        .y_codes_len = DT_INST_PROP_LEN(n, y_codes),                                               \
        .x_codes = runtime_transform_x_codes_##n,                                                  \
        .y_codes = runtime_transform_y_codes_##n,                                                  \
        .defaults =                                                                                \
            {                                                                                      \
                .xy_swap = DT_INST_PROP(n, xy_swap),                                               \
                .x_invert = DT_INST_PROP(n, x_invert),                                             \
                .y_invert = DT_INST_PROP(n, y_invert),                                             \
            },                                                                                     \
    };                                                                                             \
    static struct runtime_transform_data runtime_transform_data_##n;                               \
    DEVICE_DT_INST_DEFINE(n, &runtime_transform_init, NULL, &runtime_transform_data_##n,           \
                          &runtime_transform_config_##n, POST_KERNEL,                              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &runtime_transform_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_TRANSFORM_INST)
