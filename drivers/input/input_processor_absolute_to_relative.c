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
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>

LOG_MODULE_REGISTER(absolute_to_relative, CONFIG_ZMK_LOG_LEVEL);

/* Sentinel values for uninitialized coordinates */
#define COORD_UNINITIALIZED UINT16_MAX
#define COORD_INVALID_ZERO  0xFFF

struct absolute_to_relative_config {
    bool suppress_btn_touch;
    bool suppress_btn0;
};

struct absolute_to_relative_data {
    /*
     * Written from the input thread and cleared from whichever thread raises a
     * layer change. No lock: each field is a single aligned store, the clearing
     * side only ever invalidates and the reading side only ever re-establishes,
     * so a half-seen clear costs at most one more sample against the old
     * reference. A lock would not buy the pair of axes either - they arrive as
     * separate events, so a change can always land between them.
     */
    uint16_t previous_x, previous_y;
    int16_t previous_dx, previous_dy;
    /*
     * Set while a BTN_0 press has been suppressed here and its release has not
     * been seen yet. Suppression has to stay paired: which processors run is
     * decided per event from the layer active at that moment, so a press and
     * its release can be routed differently when the layer changes in between.
     * Dropping a release whose press was never suppressed leaves the button
     * held down on the host with nothing left to release it.
     */
    bool btn0_press_suppressed;
};

/**
 * Drop the reference point, so the next sample on each axis establishes a new
 * one instead of being measured against a position that no longer relates to it.
 */
static inline void drop_reference(struct absolute_to_relative_data *data) {
    data->previous_x = COORD_UNINITIALIZED;
    data->previous_y = COORD_UNINITIALIZED;
    data->previous_dx = 0;
    data->previous_dy = 0;
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

    /*
     * Calculate delta and apply smoothing (use local prev to reduce memory
     * access).
     *
     * Halved by dividing rather than shifting. A shift rounds towards minus
     * infinity, so an odd sum loses its half going one way and gains it going
     * the other, and the same path travelled in opposite directions does not
     * come back to where it started. Division truncates towards zero, which
     * treats both directions alike.
     */
    int16_t delta = (int16_t)value - (int16_t)prev;
    int16_t smooth_delta = (delta + *previous_delta) / 2;

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
 *
 * Both edges drop the reference point. A press begins a contact that bears no
 * relation to the last one, and a release ends one - and the driver emits a
 * final absolute pair just after the release, which would otherwise be turned
 * into motion against a position that has stopped meaning anything.
 *
 * The press is never treated as redundant, even when a contact already looks
 * active. Which processors run is decided per event from the layer active at
 * that moment, so this instance may simply have missed the release that ended
 * the previous contact; skipping the reset in that case would measure the new
 * contact against the old one's position.
 */
static int handle_touch_button(struct input_event *event, struct absolute_to_relative_data *data,
                               const struct absolute_to_relative_config *config) {
    drop_reference(data);

    if (IS_ENABLED(CONFIG_ZMK_LOG_LEVEL_DBG)) {
        LOG_DBG("Touch %s - reference dropped", event->value ? "started" : "released");
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
 *
 * A release is only dropped when the matching press was dropped here. A press
 * that reached the host on another layer keeps its release, so the button can
 * never be left stuck down.
 */
static int handle_button_suppress(struct input_event *event, struct absolute_to_relative_data *data,
                                  const struct absolute_to_relative_config *config) {
    if (!config->suppress_btn0) {
        /* Not suppressing here, so nothing of ours is outstanding. */
        data->btn0_press_suppressed = false;
        return ZMK_INPUT_PROC_CONTINUE;
    }

    bool was_suppressed = data->btn0_press_suppressed;

    data->btn0_press_suppressed = event->value != 0;

    if (!event->value && !was_suppressed) {
        LOG_WRN("Passing BTN_0 release: its press was not suppressed here");
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (IS_ENABLED(CONFIG_ZMK_LOG_LEVEL_DBG)) {
        LOG_DBG("Suppressing BTN_0 %s", event->value ? "press" : "release");
    }
    event->code = COORD_INVALID_ZERO;
    event->sync = false;
    return ZMK_INPUT_PROC_STOP;
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
            return handle_button_suppress(event, data, config);
        }
    }

    if (event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /*
     * Convert absolute axes to relative motion.
     *
     * There is deliberately no contact-state gate here. Contact state would be
     * per instance, but an instance only sees the events that arrive while it
     * holds the chain, and the chain is chosen per event from the layer active
     * at that moment. An instance that missed the press of the contact now in
     * progress would gate itself off for the rest of it and pass absolute
     * events through unconverted - which reads as the pointer dying mid-stroke
     * until the finger is lifted, and only intermittently, since an instance
     * that once saw a press without its release stays open by accident.
     *
     * The reference point already covers not knowing where the finger was: the
     * first sample on an axis establishes it and is suppressed, and the next
     * one converts. That is the same sample every contact spends at its start.
     */
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

    data->btn0_press_suppressed = false;
    drop_reference(data);

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

/**
 * Drop the reference point when the layer changes.
 *
 * Which processors run is decided per event from the layer active at that
 * moment, so one contact can be split across two instances of this processor.
 * The instance the contact moves to has a reference point left over from an
 * earlier contact, and the first sample it sees is turned into the distance
 * between two unrelated touches - a single delta of up to the whole pad, which
 * an acceleration or inertia stage downstream then multiplies.
 *
 * Dropping the reference costs the one sample spent re-establishing it, the
 * same sample every contact already spends when it starts.
 *
 * Nothing here records whether a contact is in progress, deliberately. An
 * instance only sees the events that arrive while it holds the chain, so a
 * flag taken from BTN_TOUCH would be wrong for exactly the contact this reset
 * exists to rescue.
 */
#define ABSOLUTE_TO_RELATIVE_RESYNC(n)                                                             \
    {                                                                                              \
        struct absolute_to_relative_data *data = DEVICE_DT_INST_GET(n)->data;                      \
        drop_reference(data);                                                                      \
        /*                                                                                         \
         * A press suppressed here whose release is routed elsewhere would                         \
         * leave this set for good, and the next unrelated release to reach                        \
         * this instance would be swallowed - the stuck button the record                          \
         * exists to prevent. A layer change is the moment that split becomes                      \
         * possible, so clear it here rather than expiring it on a timer.                          \
         */                                                                                        \
        data->btn0_press_suppressed = false;                                                       \
    }

static int absolute_to_relative_layer_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    DT_INST_FOREACH_STATUS_OKAY(ABSOLUTE_TO_RELATIVE_RESYNC)

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(absolute_to_relative_layer, absolute_to_relative_layer_listener);
ZMK_SUBSCRIPTION(absolute_to_relative_layer, zmk_layer_state_changed);
