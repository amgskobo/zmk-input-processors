/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the runtime processor drivers, run inside a native_sim build.
 *
 * The arithmetic each driver delegates is pinned by the host tests. These pin
 * what only a running kernel has: the devicetree values an instance starts
 * from, the event filtering around the arithmetic, the argument checks on the
 * public API, and the temp layer's work items, timers and event handling
 * against ZMK's own keymap.
 */

#include <errno.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <drivers/input_processor.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#include <zmk-input-processors/runtime_code_mapper.h>
#include <zmk-input-processors/runtime_scaler.h>
#include <zmk-input-processors/runtime_temp_layer.h>
#include <zmk-input-processors/runtime_transform.h>

#include "runtime_test.h"

static const struct device *const scale = DEVICE_DT_GET(DT_NODELABEL(rt_scale));
static const struct device *const xform = DEVICE_DT_GET(DT_NODELABEL(rt_xform));
static const struct device *const map = DEVICE_DT_GET(DT_NODELABEL(rt_map));
static const struct device *const map_off = DEVICE_DT_GET(DT_NODELABEL(rt_map_off));
static const struct device *const layer = DEVICE_DT_GET(DT_NODELABEL(rt_layer));
static const struct device *const layer_hold = DEVICE_DT_GET(DT_NODELABEL(rt_layer_hold));
static const struct device *const layer_off = DEVICE_DT_GET(DT_NODELABEL(rt_layer_off));

#define LAYER DT_PROP(DT_NODELABEL(rt_layer), layer)
#define HOLD_LAYER DT_PROP(DT_NODELABEL(rt_layer_hold), layer)
#define OFF_LAYER DT_PROP(DT_NODELABEL(rt_layer_off), layer)
#define TIMEOUT_MS DT_PROP(DT_NODELABEL(rt_layer), timeout_ms)
#define IDLE_MS DT_PROP(DT_NODELABEL(rt_layer), require_prior_idle_ms)

/* Positions in the case's keymap. */
#define KEY_POSITION 0
#define EXCLUDED_POSITION 1
#define EMPTY_POSITION 3

/* Long enough for a submitted work item to have run. */
#define WORK_MS 5

K_SEM_DEFINE(work_block_started, 0, 1);
K_SEM_DEFINE(work_block_release, 0, 1);
static struct k_work work_block;

static void work_block_cb(struct k_work *work) {
    ARG_UNUSED(work);

    k_sem_give(&work_block_started);
    (void)k_sem_take(&work_block_release, K_FOREVER);
}

static int process(const struct device *dev, struct input_event *event, int16_t *remainder) {
    struct zmk_input_processor_state state = {.remainder = remainder};

    return zmk_input_processor_handle_event(dev, event, 0, 0, &state);
}

static struct input_event rel(uint16_t code, int32_t value) {
    return (struct input_event){.type = INPUT_EV_REL, .code = code, .value = value};
}

static struct input_event key_type(uint16_t code, int32_t value) {
    return (struct input_event){.type = INPUT_EV_KEY, .code = code, .value = value};
}

static void queue_move(const struct device *dev) {
    struct input_event event = rel(INPUT_REL_X, 1);

    (void)process(dev, &event, NULL);
}

static void move(const struct device *dev) {
    queue_move(dev);
    k_sleep(K_MSEC(WORK_MS));
}

static void press(uint32_t position, bool pressed) {
    raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
        .position = position,
        .state = pressed,
        .timestamp = k_uptime_get(),
    });
    k_sleep(K_MSEC(WORK_MS));
}

/* Lower every layer and outlast every timer, so that no test inherits one. */
static void settle(void) {
    for (int id = 1; id < ZMK_KEYMAP_LAYERS_LEN; id++) {
        (void)zmk_keymap_layer_deactivate(id, true);
    }

    k_sleep(K_MSEC(TIMEOUT_MS + IDLE_MS + 50));
}

static void test_scaler_ratio(void) {
    struct runtime_test t = {.name = "scaler: applies the devicetree ratio to the codes it names"};
    uint32_t multiplier = 0;
    uint32_t divisor = 0;
    struct input_event event;

    RT_EXPECT_EQ(&t, runtime_scaler_get_params(scale, &multiplier, &divisor), 0, "read");
    RT_EXPECT_EQ(&t, multiplier, 889, "devicetree multiplier");
    RT_EXPECT_EQ(&t, divisor, 500, "devicetree divisor");

    event = rel(INPUT_REL_X, 37);
    RT_EXPECT_EQ(&t, process(scale, &event, NULL), ZMK_INPUT_PROC_CONTINUE, "chain continues");
    RT_EXPECT_EQ(&t, event.value, 65, "X at the stock scaler's first reversal");

    event = rel(INPUT_REL_Y, -37);
    (void)process(scale, &event, NULL);
    RT_EXPECT_EQ(&t, event.value, -65, "Y backwards");

    event = rel(INPUT_REL_WHEEL, 37);
    (void)process(scale, &event, NULL);
    RT_EXPECT_EQ(&t, event.value, 37, "a code it does not name passes");

    event = key_type(INPUT_REL_X, 37);
    (void)process(scale, &event, NULL);
    RT_EXPECT_EQ(&t, event.value, 37, "another event type passes");

    event = rel(INPUT_REL_X, 37);
    RT_EXPECT_EQ(&t, zmk_input_processor_handle_event(scale, &event, 0, 0, NULL),
                 ZMK_INPUT_PROC_CONTINUE, "no state at all");
    RT_EXPECT_EQ(&t, event.value, 65, "scaled without a state");

    rt_finish(&t);
}

static void test_scaler_remainder(void) {
    struct runtime_test t = {.name =
                                 "scaler: carries a remainder only through the listener's slot"};
    int16_t remainder = 0;
    int32_t total = 0;
    struct input_event event = rel(INPUT_REL_X, 1);

    (void)process(scale, &event, &remainder);
    RT_EXPECT_EQ(&t, event.value, 1, "one count at 889/500");
    RT_EXPECT_EQ(&t, remainder, 389, "leaves 389/500 in the slot");

    /* The slot outlives the ratio, and 389/500 means nothing at 1/16: carried, it is 24 counts. */
    RT_EXPECT_EQ(&t, runtime_scaler_set_params(scale, 1, 16), 0, "set 1/16");
    event = rel(INPUT_REL_X, 1);
    (void)process(scale, &event, &remainder);
    RT_EXPECT_EQ(&t, event.value, 0, "the old fraction is not carried into the new ratio");
    RT_EXPECT_EQ(&t, remainder, 1, "it is replaced by the new one");

    remainder = 0;
    for (int i = 0; i < 16; i++) {
        event = rel(INPUT_REL_X, 1);
        (void)process(scale, &event, &remainder);
        total += event.value;
    }
    RT_EXPECT_EQ(&t, total, 1, "sixteen sixteenths with a slot");
    RT_EXPECT_EQ(&t, remainder, 0, "the slot is spent");

    total = 0;
    for (int i = 0; i < 16; i++) {
        event = rel(INPUT_REL_X, 1);
        (void)process(scale, &event, NULL);
        total += event.value;
    }
    RT_EXPECT_EQ(&t, total, 0, "sixteen sixteenths without a slot");

    RT_EXPECT_EQ(&t, runtime_scaler_set_params(scale, 889, 500), 0, "restore");
    rt_finish(&t);
}

static void test_scaler_validation(void) {
    struct runtime_test t = {.name = "scaler: refuses an invalid ratio and keeps the old one"};
    uint32_t multiplier = 0;
    uint32_t divisor = 0;
    struct input_event event;

    RT_EXPECT_EQ(&t, runtime_scaler_set_params(scale, 1, 0), -EINVAL, "zero divisor");
    RT_EXPECT_EQ(&t, runtime_scaler_set_params(scale, 32768, 1), -EINVAL, "multiplier too large");
    RT_EXPECT_EQ(&t, runtime_scaler_set_params(scale, 1, 32768), -EINVAL, "divisor too large");
    RT_EXPECT_EQ(&t, runtime_scaler_get_params(scale, &multiplier, &divisor), 0, "read");
    RT_EXPECT_EQ(&t, multiplier, 889, "multiplier unchanged");
    RT_EXPECT_EQ(&t, divisor, 500, "divisor unchanged");

    RT_EXPECT_EQ(&t, runtime_scaler_set_params(NULL, 1, 1), -EINVAL, "no device");
    RT_EXPECT_EQ(&t, runtime_scaler_get_params(NULL, &multiplier, &divisor), -EINVAL,
                 "read with no device");
    RT_EXPECT_EQ(&t, runtime_scaler_get_params(scale, NULL, &divisor), -EINVAL, "no multiplier");
    RT_EXPECT_EQ(&t, runtime_scaler_get_params(scale, &multiplier, NULL), -EINVAL, "no divisor");

    /* Both ends of the accepted range take effect. */
    RT_EXPECT_EQ(&t, runtime_scaler_set_params(scale, 0, 1), 0, "zero multiplier");
    event = rel(INPUT_REL_X, 1000);
    (void)process(scale, &event, NULL);
    RT_EXPECT_EQ(&t, event.value, 0, "a killed axis");

    RT_EXPECT_EQ(&t, runtime_scaler_set_params(scale, 32767, 32767), 0, "both at the ceiling");
    event = rel(INPUT_REL_X, 1000);
    (void)process(scale, &event, NULL);
    RT_EXPECT_EQ(&t, event.value, 1000, "identity at the ceiling");

    RT_EXPECT_EQ(&t, runtime_scaler_set_params(scale, 889, 500), 0, "restore");
    rt_finish(&t);
}

static void test_transform(void) {
    struct runtime_test t = {
        .name = "transform: applies the devicetree flags, swapping before inverting"};
    struct runtime_transform_flags flags = {0};
    struct runtime_transform_flags read = {0};
    struct input_event event;

    RT_EXPECT_EQ(&t, runtime_transform_get_flags(xform, &flags), 0, "read");
    RT_EXPECT_FALSE(&t, flags.xy_swap, "devicetree swap");
    RT_EXPECT_FALSE(&t, flags.x_invert, "devicetree x-invert");
    RT_EXPECT_TRUE(&t, flags.y_invert, "devicetree y-invert");

    event = rel(INPUT_REL_Y, 5);
    (void)process(xform, &event, NULL);
    RT_EXPECT_EQ(&t, event.code, INPUT_REL_Y, "Y stays on Y");
    RT_EXPECT_EQ(&t, event.value, -5, "Y is inverted");

    event = rel(INPUT_REL_X, 5);
    (void)process(xform, &event, NULL);
    RT_EXPECT_EQ(&t, event.value, 5, "X is not inverted");

    event = rel(INPUT_REL_WHEEL, 5);
    (void)process(xform, &event, NULL);
    RT_EXPECT_EQ(&t, event.value, -5, "the second Y code is inverted too");

    event = key_type(INPUT_REL_Y, 5);
    (void)process(xform, &event, NULL);
    RT_EXPECT_EQ(&t, event.value, 5, "another event type passes");

    flags.xy_swap = true;
    RT_EXPECT_EQ(&t, runtime_transform_set_flags(xform, &flags), 0, "set swap");

    event = rel(INPUT_REL_X, 5);
    (void)process(xform, &event, NULL);
    RT_EXPECT_EQ(&t, event.code, INPUT_REL_Y, "X is swapped onto Y");
    RT_EXPECT_EQ(&t, event.value, -5, "and inverted as Y, where it now travels");

    event = rel(INPUT_REL_WHEEL, 3);
    (void)process(xform, &event, NULL);
    RT_EXPECT_EQ(&t, event.code, INPUT_REL_HWHEEL, "the second pair swaps by index");
    RT_EXPECT_EQ(&t, event.value, 3, "and is not inverted as X");

    RT_EXPECT_EQ(&t, runtime_transform_get_flags(xform, &read), 0, "read back");
    RT_EXPECT_TRUE(&t, read.xy_swap && !read.x_invert && read.y_invert, "flags read back as set");

    RT_EXPECT_EQ(&t, runtime_transform_set_flags(xform, NULL), -EINVAL, "no flags");
    RT_EXPECT_EQ(&t, runtime_transform_set_flags(NULL, &flags), -EINVAL, "no device");
    RT_EXPECT_EQ(&t, runtime_transform_get_flags(xform, NULL), -EINVAL, "no flags to read into");
    RT_EXPECT_EQ(&t, runtime_transform_get_flags(NULL, &read), -EINVAL, "read with no device");

    flags.xy_swap = false;
    RT_EXPECT_EQ(&t, runtime_transform_set_flags(xform, &flags), 0, "restore");
    rt_finish(&t);
}

static void test_code_mapper(void) {
    struct runtime_test t = {
        .name = "code mapper: rewrites while enabled and passes through while disabled"};
    bool enabled = false;
    struct input_event event;

    RT_EXPECT_EQ(&t, runtime_code_mapper_get_enabled(map, &enabled), 0, "read");
    RT_EXPECT_TRUE(&t, enabled, "enabled by default");
    RT_EXPECT_EQ(&t, runtime_code_mapper_get_enabled(map_off, &enabled), 0, "read start-disabled");
    RT_EXPECT_FALSE(&t, enabled, "start-disabled starts disabled");

    event = rel(INPUT_REL_Y, 4);
    (void)process(map, &event, NULL);
    RT_EXPECT_EQ(&t, event.code, INPUT_REL_WHEEL, "Y becomes WHEEL");
    RT_EXPECT_EQ(&t, event.value, 4, "the value is untouched");

    event = rel(INPUT_REL_X, 4);
    (void)process(map, &event, NULL);
    RT_EXPECT_EQ(&t, event.code, INPUT_REL_HWHEEL, "X becomes HWHEEL");

    event = rel(INPUT_REL_WHEEL, 4);
    (void)process(map, &event, NULL);
    RT_EXPECT_EQ(&t, event.code, INPUT_REL_WHEEL, "a code with no pair passes");

    event = key_type(INPUT_REL_Y, 4);
    (void)process(map, &event, NULL);
    RT_EXPECT_EQ(&t, event.code, INPUT_REL_Y, "another event type passes");

    event = rel(INPUT_REL_Y, 4);
    (void)process(map_off, &event, NULL);
    RT_EXPECT_EQ(&t, event.code, INPUT_REL_Y, "disabled passes through");

    RT_EXPECT_EQ(&t, runtime_code_mapper_set_enabled(map_off, true), 0, "switch on");
    event = rel(INPUT_REL_Y, 4);
    (void)process(map_off, &event, NULL);
    RT_EXPECT_EQ(&t, event.code, INPUT_REL_WHEEL, "switched on rewrites");

    RT_EXPECT_EQ(&t, runtime_code_mapper_set_enabled(map, false), 0, "switch off");
    event = rel(INPUT_REL_Y, 4);
    (void)process(map, &event, NULL);
    RT_EXPECT_EQ(&t, event.code, INPUT_REL_Y, "switched off passes through");

    RT_EXPECT_EQ(&t, runtime_code_mapper_set_enabled(NULL, true), -EINVAL, "no device");
    RT_EXPECT_EQ(&t, runtime_code_mapper_get_enabled(NULL, &enabled), -EINVAL, "read no device");
    RT_EXPECT_EQ(&t, runtime_code_mapper_get_enabled(map, NULL), -EINVAL, "nothing to read into");

    RT_EXPECT_EQ(&t, runtime_code_mapper_set_enabled(map, true), 0, "restore");
    RT_EXPECT_EQ(&t, runtime_code_mapper_set_enabled(map_off, false), 0, "restore start-disabled");
    rt_finish(&t);
}

static void test_temp_layer_parameters(void) {
    struct runtime_test t = {
        .name = "temp layer: reports its devicetree parameters and refuses a bad layer"};
    struct runtime_temp_layer_params params = {0};
    struct runtime_temp_layer_params other = {0};

    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read");
    RT_EXPECT_TRUE(&t, params.enabled, "enabled by default");
    RT_EXPECT_EQ(&t, params.layer, LAYER, "devicetree layer");
    RT_EXPECT_EQ(&t, params.timeout_ms, TIMEOUT_MS, "devicetree timeout");
    RT_EXPECT_EQ(&t, params.require_prior_idle_ms, IDLE_MS, "devicetree idle guard");

    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer_off, &other), 0, "read start-disabled");
    RT_EXPECT_FALSE(&t, other.enabled, "start-disabled starts disabled");

    other = params;
    other.layer = ZMK_KEYMAP_LAYERS_LEN;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &other), -EINVAL,
                 "layer past the keymap");
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &other), 0, "read back");
    RT_EXPECT_EQ(&t, other.layer, LAYER, "the layer is unchanged");

    other = params;
    other.timeout_ms = RUNTIME_TEMP_LAYER_MAX_MS + 1;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &other), -EINVAL,
                 "timeout past the maximum");
    other = params;
    other.require_prior_idle_ms = RUNTIME_TEMP_LAYER_MAX_MS + 1;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &other), -EINVAL,
                 "idle guard past the maximum");

    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(NULL, &params), -EINVAL, "no device");
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, NULL), -EINVAL, "no parameters");
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(NULL, &params), -EINVAL, "read no device");
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, NULL), -EINVAL, "nothing to read into");

    rt_finish(&t);
}

static void test_temp_layer_timeout(void) {
    struct runtime_test t = {.name = "temp layer: movement raises the layer and silence drops it"};

    settle();
    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "raised by movement");

    k_sleep(K_MSEC(TIMEOUT_MS / 2));
    move(layer);
    k_sleep(K_MSEC(TIMEOUT_MS / 2 + 10));
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "movement pushes the timeout back");

    k_sleep(K_MSEC(TIMEOUT_MS));
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(LAYER), "dropped once the pointer is silent");

    rt_finish(&t);
}

static void test_temp_layer_positions(void) {
    struct runtime_test t = {
        .name = "temp layer: an excluded key keeps the layer and any other key drops it"};

    settle();
    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "raised by movement");

    press(EXCLUDED_POSITION, true);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "an excluded key press");
    press(EXCLUDED_POSITION, false);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "an excluded key release");

    press(EMPTY_POSITION, true);
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(LAYER), "any other key press");
    press(EMPTY_POSITION, false);

    rt_finish(&t);
}

static void test_temp_layer_idle_guard(void) {
    struct runtime_test t = {.name = "temp layer: a recent key press keeps the layer from rising"};

    settle();
    press(KEY_POSITION, true);
    press(KEY_POSITION, false);
    move(layer);
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(LAYER), "movement straight after a key");

    k_sleep(K_MSEC(IDLE_MS));
    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "movement once the window has passed");

    rt_finish(&t);
}

static void test_temp_layer_external_drop(void) {
    struct runtime_test t = {
        .name = "temp layer: forgets a layer dropped elsewhere instead of dropping it again"};

    settle();
    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "raised by movement");

    (void)zmk_keymap_layer_deactivate(LAYER, false);
    (void)zmk_keymap_layer_activate(LAYER, false);
    k_sleep(K_MSEC(TIMEOUT_MS + 20));
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER),
                   "a layer raised elsewhere outlives the timer");

    (void)zmk_keymap_layer_deactivate(LAYER, false);
    rt_finish(&t);
}

static void test_temp_layer_locked(void) {
    struct runtime_test t = {
        .name = "temp layer: keeps holding a locked layer it could not drop, until it is unlocked"};
    struct runtime_temp_layer_params params = {0};

    settle();
    (void)zmk_keymap_layer_activate(LAYER, true);
    move(layer);
    k_sleep(K_MSEC(TIMEOUT_MS + 20));
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "a locked layer outlives the timeout");

    /*
     * The keymap refused the drop, so the layer is still this processor's: given
     * a new layer number, it raises nothing more until the old one has gone.
     */
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read");
    params.layer = OFF_LAYER;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "move to another layer");
    move(layer);
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(OFF_LAYER), "no second layer while one is held");

    (void)zmk_keymap_layer_deactivate(LAYER, true);
    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(OFF_LAYER), "unlocked, movement raises the new one");
    k_sleep(K_MSEC(TIMEOUT_MS + 20));
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(OFF_LAYER), "and silence drops it");

    params.layer = LAYER;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "restore");
    rt_finish(&t);
}

static void test_temp_layer_switch_off(void) {
    struct runtime_test t = {
        .name = "temp layer: switching it off drops a held layer and raises nothing"};
    struct runtime_temp_layer_params params = {0};

    settle();
    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "raised by movement");
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read");

    params.enabled = false;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "switch off");
    k_sleep(K_MSEC(WORK_MS));
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(LAYER), "the held layer is dropped");

    move(layer);
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(LAYER), "movement raises nothing");

    params.enabled = true;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "switch on");
    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "switched back on, movement raises it");

    /* Switched off while holding nothing, it lowers nothing it did not raise. */
    settle();
    (void)zmk_keymap_layer_activate(LAYER, false);
    params.enabled = false;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0,
                 "switch off, holding nothing");
    k_sleep(K_MSEC(WORK_MS));
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "a layer raised elsewhere stays up");

    params.enabled = true;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "restore");
    (void)zmk_keymap_layer_deactivate(LAYER, false);
    rt_finish(&t);
}

static void test_temp_layer_switch_off_while_activation_is_queued(void) {
    struct runtime_test t = {.name =
                                 "temp layer: switching off cancels an activation already queued"};
    struct runtime_temp_layer_params params = {0};

    settle();
    RT_EXPECT_EQ(&t, k_work_submit(&work_block) < 0, false, "queue the work blocker");
    RT_EXPECT_EQ(&t, k_sem_take(&work_block_started, K_FOREVER), 0, "work queue is blocked");

    queue_move(layer);
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read");
    params.enabled = false;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0,
                 "switch off before activation runs");

    k_sem_give(&work_block_release);
    k_sleep(K_MSEC(WORK_MS));
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(LAYER), "queued work raises nothing");

    params.enabled = true;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "restore");
    rt_finish(&t);
}

static void test_temp_layer_update_while_activation_is_queued(void) {
    struct runtime_test t = {
        .name = "temp layer: a parameter update discards an activation already queued"};
    struct runtime_temp_layer_params params = {0};

    settle();
    RT_EXPECT_EQ(&t, k_work_submit(&work_block) < 0, false, "queue the work blocker");
    RT_EXPECT_EQ(&t, k_sem_take(&work_block_started, K_FOREVER), 0, "work queue is blocked");

    queue_move(layer);
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read");
    params.layer = OFF_LAYER;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0,
                 "change the layer before activation runs");

    k_sem_give(&work_block_release);
    k_sleep(K_MSEC(WORK_MS));
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(LAYER), "queued work does not raise the old layer");
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(OFF_LAYER),
                    "queued work does not reinterpret the event with new parameters");

    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(OFF_LAYER), "a new event uses the new layer");
    k_sleep(K_MSEC(TIMEOUT_MS + 20));

    params.layer = LAYER;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "restore");
    rt_finish(&t);
}

static void test_temp_layer_timeout_update(void) {
    struct runtime_test t = {
        .name = "temp layer: a runtime timeout update replaces the pending deadline"};
    struct runtime_temp_layer_params params = {0};

    settle();
    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "raised by movement");
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read");

    k_sleep(K_MSEC(TIMEOUT_MS / 2));
    params.timeout_ms = 0;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "disable the timeout");
    k_sleep(K_MSEC(TIMEOUT_MS));
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "the old deadline was cancelled");

    params.timeout_ms = 2 * TIMEOUT_MS;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "start a new timeout");
    k_sleep(K_MSEC(TIMEOUT_MS + 20));
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "the new deadline replaced the old one");
    k_sleep(K_MSEC(TIMEOUT_MS));
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(LAYER), "the new deadline eventually drops it");

    params.timeout_ms = TIMEOUT_MS;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "restore");
    rt_finish(&t);
}

static void test_temp_layer_new_layer(void) {
    struct runtime_test t = {.name =
                                 "temp layer: a new layer number applies from the next activation"};
    struct runtime_temp_layer_params params = {0};

    settle();
    move(layer);
    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer, &params), 0, "read");

    params.layer = OFF_LAYER;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "move to another layer");
    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(LAYER), "the raised layer stays where it is");
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(OFF_LAYER), "the new layer is not raised yet");

    k_sleep(K_MSEC(TIMEOUT_MS + 20));
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(LAYER), "the old layer is the one dropped");

    move(layer);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(OFF_LAYER), "the next activation uses the new one");

    params.layer = LAYER;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer, &params), 0, "restore");
    rt_finish(&t);
}

static void test_temp_layer_hold(void) {
    struct runtime_test t = {
        .name = "temp layer: with no timeout and no exclusions the layer holds until switched off"};
    struct runtime_temp_layer_params params = {0};

    settle();
    move(layer_hold);
    k_sleep(K_MSEC(3 * TIMEOUT_MS));
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(HOLD_LAYER), "no timeout drops it");

    press(EMPTY_POSITION, true);
    press(EMPTY_POSITION, false);
    RT_EXPECT_TRUE(&t, zmk_keymap_layer_active(HOLD_LAYER), "without exclusions no key drops it");

    RT_EXPECT_EQ(&t, runtime_temp_layer_get_params(layer_hold, &params), 0, "read");
    params.enabled = false;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer_hold, &params), 0, "switch off");
    k_sleep(K_MSEC(WORK_MS));
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(HOLD_LAYER), "switching off drops it");

    params.enabled = true;
    RT_EXPECT_EQ(&t, runtime_temp_layer_set_params(layer_hold, &params), 0, "restore");
    rt_finish(&t);
}

static void test_temp_layer_start_disabled(void) {
    struct runtime_test t = {.name = "temp layer: starting disabled raises nothing"};

    settle();
    move(layer_off);
    RT_EXPECT_FALSE(&t, zmk_keymap_layer_active(OFF_LAYER), "movement raises nothing");

    rt_finish(&t);
}

static void run_tests(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    k_work_init(&work_block, work_block_cb);

    test_scaler_ratio();
    test_scaler_remainder();
    test_scaler_validation();
    test_transform();
    test_code_mapper();
    test_temp_layer_parameters();
    test_temp_layer_timeout();
    test_temp_layer_positions();
    test_temp_layer_idle_guard();
    test_temp_layer_external_drop();
    test_temp_layer_locked();
    test_temp_layer_switch_off();
    test_temp_layer_switch_off_while_activation_is_queued();
    test_temp_layer_update_while_activation_is_queued();
    test_temp_layer_timeout_update();
    test_temp_layer_new_layer();
    test_temp_layer_hold();
    test_temp_layer_start_disabled();

    exit(0);
}

/* After boot, at the lowest priority, so work items run whenever a test sleeps. */
K_THREAD_DEFINE(runtime_processor_driver_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
