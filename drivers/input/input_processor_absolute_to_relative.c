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

/* Sentinel values for uninitialized coordinates */
#define COORD_UNINITIALIZED UINT16_MAX
#define COORD_INVALID_ZERO  0xFFF

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

/**
 * Initialize coordinates when touch starts
 */
static inline void touch_init(struct absolute_to_relative_data *data) {
    data->previous_x = COORD_UNINITIALIZED;
    data->previous_y = COORD_UNINITIALIZED;
    data->previous_dx = 0;
    data->previous_dy = 0;
    if (IS_ENABLED(CONFIG_ZMK_LOG_LEVEL_DBG)) {
        LOG_DBG("Touch started - coordinates initialized");
    }
}

/**
 * Process absolute-to-relative conversion for a single axis
 * Returns true if first position (should suppress event), false if normal motion
 */
static inline bool process_axis(struct input_event *event, uint16_t *previous_pos, int16_t *previous_delta,
                         uint16_t rel_code) {
    const uint16_t value = event->value;

    uint16_t prev = *previous_pos;
    if (prev == COORD_UNINITIALIZED) {
        /* First report on this axis - store position and suppress output */
        *previous_pos = value;
        *previous_delta = 0;
        if (IS_ENABLED(CONFIG_ZMK_LOG_LEVEL_DBG)) {
            LOG_DBG("Initial %s position: %u (suppressed)", (rel_code == INPUT_REL_X) ? "X" : "Y", value);
        }

        /* Mark event as invalid for clarity */
        event->code = COORD_INVALID_ZERO;
        event->sync = false;

        return true; /* Signal to suppress this event */
    }

    /* Calculate delta and apply smoothing (use local prev to reduce memory access) */
    int16_t delta = (int16_t)value - (int16_t)prev;
    int16_t smooth_delta = (delta + *previous_delta) >> 1;

    if (IS_ENABLED(CONFIG_ZMK_LOG_LEVEL_DBG)) {
        LOG_DBG("%s: %u -> rel_%s: %d (raw_delta: %d, smoothed: %d)",
                (rel_code == INPUT_REL_X) ? "X" : "Y", value,
                (rel_code == INPUT_REL_X) ? "x" : "y", smooth_delta, delta, smooth_delta);
    }

    /* Update event and state */
    event->type = INPUT_EV_REL;
    event->code = rel_code;
    event->value = smooth_delta;
    *previous_delta = delta;
    *previous_pos = value;
    
    return false; /* Signal to continue processing */
}

/**
 * Handle touch button events (BTN_TOUCH)
 */
static int handle_touch_button(struct input_event *event, struct absolute_to_relative_data *data,
                               const struct absolute_to_relative_config *config) {
    if (event->value == 1) {
        /* Touch started */
        data->touching = true;
        touch_init(data);
    } else {
        /* Touch ended */
        data->touching = false;
        if (IS_ENABLED(CONFIG_ZMK_LOG_LEVEL_DBG)) {
            LOG_DBG("Touch released");
        }
    }

    if (config->suppress_btn_touch) {
        if (IS_ENABLED(CONFIG_ZMK_LOG_LEVEL_DBG)) {
            LOG_DBG("Suppressing BTN_TOUCH");
        }
        event->code = COORD_INVALID_ZERO;
        event->sync = false;
        return ZMK_INPUT_PROC_STOP;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

/**
 * Handle button suppression (BTN_0)
 */
static int handle_button_suppress(struct input_event *event, const struct absolute_to_relative_config *config) {
    if (config->suppress_btn0) {
        if (IS_ENABLED(CONFIG_ZMK_LOG_LEVEL_DBG)) {
            LOG_DBG("Suppressing BTN_0");
        }
        event->code = COORD_INVALID_ZERO;
        event->sync = false;
        return ZMK_INPUT_PROC_STOP;
    }
    return ZMK_INPUT_PROC_CONTINUE;
}

/**
 * Main event handler - converts absolute input events to relative
 */
static int absolute_to_relative_handle_event(const struct device *dev, struct input_event *event,
                                             uint32_t param1, uint32_t param2,
                                             struct zmk_input_processor_state *state) {
    const struct absolute_to_relative_config *config = dev->config;
    struct absolute_to_relative_data *data = (struct absolute_to_relative_data *)dev->data;

    /* Handle button events */
    if (event->type == INPUT_EV_KEY) {
        if (event->code == INPUT_BTN_TOUCH) {
            return handle_touch_button(event, data, config);
        }
        if (event->code == INPUT_BTN_0) {
            return handle_button_suppress(event, config);
        }
    }

    /* Only process absolute axis events when touching */
    if (!data->touching || event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* Convert absolute axes to relative motion */
    bool suppress_event = false;
    
    if (event->code == INPUT_ABS_X) {
        suppress_event = process_axis(event, &data->previous_x, &data->previous_dx, INPUT_REL_X);
    } else if (event->code == INPUT_ABS_Y) {
        suppress_event = process_axis(event, &data->previous_y, &data->previous_dy, INPUT_REL_Y);
    }

    if (suppress_event) {
        return ZMK_INPUT_PROC_STOP;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

/**
 * Device initialization
 */
static int absolute_to_relative_init(const struct device *dev) {
    struct absolute_to_relative_data *data = (struct absolute_to_relative_data *)dev->data;
    const struct absolute_to_relative_config *config = dev->config;

    data->dev = dev;
    data->touching = false;

    LOG_INF("Initialized (suppress_btn_touch=%d, suppress_btn0=%d)",
            config->suppress_btn_touch, config->suppress_btn0);

    return 0;
}

/**
 * Driver API
 */
static const struct zmk_input_processor_driver_api absolute_to_relative_driver_api = {
    .handle_event = absolute_to_relative_handle_event,
};

/**
 * Device instantiation macro
 */
#define ABSOLUTE_TO_RELATIVE_INST(n)                                                   \
    static struct absolute_to_relative_data processor_absolute_to_relative_data_##n = {\
        .touching = false,                                                              \
        .previous_x = COORD_UNINITIALIZED,                                              \
        .previous_y = COORD_UNINITIALIZED,                                              \
        .previous_dx = 0,                                                               \
        .previous_dy = 0,                                                               \
    };                                                                                  \
    static const struct absolute_to_relative_config                                    \
        processor_absolute_to_relative_config_##n = {                                  \
            .suppress_btn_touch = DT_INST_PROP_OR(n, suppress_btn_touch, false),       \
            .suppress_btn0 = DT_INST_PROP_OR(n, suppress_btn0, false),                 \
        };                                                                              \
    DEVICE_DT_INST_DEFINE(n, absolute_to_relative_init, NULL,                         \
                          &processor_absolute_to_relative_data_##n,                    \
                          &processor_absolute_to_relative_config_##n, POST_KERNEL,     \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                         \
                          &absolute_to_relative_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ABSOLUTE_TO_RELATIVE_INST)
