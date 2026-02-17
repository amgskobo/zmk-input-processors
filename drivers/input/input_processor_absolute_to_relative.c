/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_absolute_to_relative

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/devicetree.h>

LOG_MODULE_REGISTER(absolute_to_relative, CONFIG_ZMK_LOG_LEVEL);

struct absolute_to_relative_config {
    bool suppress_btn_touch;
    bool suppress_btn0;
};

struct absolute_to_relative_data {
    uint16_t previous_x, previous_y;
    int16_t previous_dx, previous_dy;
    bool touching;
    const struct device *dev;
};
 
static int absolute_to_relative_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                               uint32_t param2, struct zmk_input_processor_state *state) { 
    const struct absolute_to_relative_config *config = dev->config;
    struct absolute_to_relative_data *data = (struct absolute_to_relative_data *)dev->data;

    // Handle touch control via INPUT_BTN_TOUCH event
    if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_TOUCH) {
        if (event->value == 0) {
            // Touch released - reset coordinates
            LOG_DBG("Touch released");
            data->previous_x = UINT16_MAX;
            data->previous_y = UINT16_MAX;
        } else {
            LOG_DBG("Touch started");
        }
        data->touching = (event->value != 0);
        if (config->suppress_btn_touch) {
            LOG_DBG("Suppressing BTN_TOUCH event");
            event->code = 0xFFF;
            event->sync = false;
            return ZMK_INPUT_PROC_STOP;
        }
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* Optionally suppress BTN_0 events */
    if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_0) {
        if (config->suppress_btn0) {
            LOG_DBG("Suppressing BTN_0 event");
            event->code = 0xFFF;
            event->sync = false;
            return ZMK_INPUT_PROC_STOP;
        }
        return ZMK_INPUT_PROC_CONTINUE;
    }

    // Only process absolute events if touch is active
    if (!data->touching) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->type == INPUT_EV_ABS) {
        uint16_t value = event->value;

        if (event->code == INPUT_ABS_X) {
            if (data->previous_x == UINT16_MAX) {
                LOG_DBG("Initial X position: %u", value);
                data->previous_x = value;
                data->previous_dx = 0;
                return ZMK_INPUT_PROC_CONTINUE;
            }
            int16_t dx = (int16_t)value - (int16_t)data->previous_x;
            int16_t smooth_dx = (dx + data->previous_dx) >> 1;
            LOG_DBG("X: %u -> rel_x: %d (raw_dx: %d, smooth_dx: %d)", value, smooth_dx, dx, smooth_dx);
            event->type = INPUT_EV_REL;
            event->code = INPUT_REL_X;
            event->value = smooth_dx;
            data->previous_dx = dx;
            data->previous_x = value;
        } else if (event->code == INPUT_ABS_Y) {
            if (data->previous_y == UINT16_MAX) {
                LOG_DBG("Initial Y position: %u", value);
                data->previous_y = value;
                data->previous_dy = 0;
                return ZMK_INPUT_PROC_CONTINUE;
            }
            int16_t dy = (int16_t)value - (int16_t)data->previous_y;
            int16_t smooth_dy = (dy + data->previous_dy) >> 1;
            LOG_DBG("Y: %u -> rel_y: %d (raw_dy: %d, smooth_dy: %d)", value, smooth_dy, dy, smooth_dy);
            event->type = INPUT_EV_REL;
            event->code = INPUT_REL_Y;
            event->value = smooth_dy;
            data->previous_dy = dy;
            data->previous_y = value;
        }
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int absolute_to_relative_init(const struct device *dev) {
    struct absolute_to_relative_data *data = (struct absolute_to_relative_data *)dev->data;
    data->dev = dev;
    
    const struct absolute_to_relative_config *config = dev->config;
    LOG_INF("Initialized. suppress_btn_touch: %d, suppress_btn0: %d", 
            config->suppress_btn_touch, config->suppress_btn0);

    return 0;
}

static const struct zmk_input_processor_driver_api absolute_to_relative_driver_api = {
    .handle_event = absolute_to_relative_handle_event,
};

#define ABSOLUTE_TO_RELATIVE_INST(n)                                                                    \
    static struct absolute_to_relative_data processor_absolute_to_relative_data_##n = {                 \
        .touching = false,                                                                                \
        .previous_x = UINT16_MAX,                                                                         \
        .previous_y = UINT16_MAX,                                                                         \
        .previous_dx = 0,                                                                                 \
        .previous_dy = 0,                                                                                 \
    };                                                                                                  \
    static const struct absolute_to_relative_config processor_absolute_to_relative_config_##n = {       \
        .suppress_btn_touch = DT_INST_PROP_OR(n, suppress_btn_touch, false),                             \
        .suppress_btn0 = DT_INST_PROP_OR(n, suppress_btn0, false),                                       \
    };                                                                                                  \
    DEVICE_DT_INST_DEFINE(n, absolute_to_relative_init, NULL, &processor_absolute_to_relative_data_##n, \
                          &processor_absolute_to_relative_config_##n, POST_KERNEL,                      \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &absolute_to_relative_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ABSOLUTE_TO_RELATIVE_INST)
