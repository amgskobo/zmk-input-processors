/* Copyright (c) 2026 amgskobo; SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <drivers/input_processor.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

static const struct device *const first = DEVICE_DT_GET(DT_NODELABEL(transition_a));
static const struct device *const second = DEVICE_DT_GET(DT_NODELABEL(transition_b));
K_SEM_DEFINE(first_entered, 0, 1);
K_SEM_DEFINE(second_entered, 0, 1);
K_SEM_DEFINE(second_start, 0, 1);
K_SEM_DEFINE(second_done, 0, 1);
static atomic_t armed;

int __real_zmk_keymap_layer_activate(uint8_t layer, bool lock);
int __real_zmk_keymap_layer_deactivate(uint8_t layer, bool force);

/* The real keymap/event-manager implementation still performs the change.
 * Only the entry rendezvous is controlled, while the producer's real mutex
 * is held. A serialized transition cannot enter the second wrapper until
 * the first finishes, so its first wait has an explicit finite bound. */
int __wrap_zmk_keymap_layer_activate(uint8_t layer, bool lock) {
    if (atomic_get(&armed) && layer == 1) {
        k_sem_give(&first_entered);
        (void)k_sem_take(&second_entered, K_MSEC(40));
    }
    return __real_zmk_keymap_layer_activate(layer, lock);
}

int __wrap_zmk_keymap_layer_deactivate(uint8_t layer, bool force) {
    if (atomic_get(&armed) && layer == 2) {
        k_sem_give(&second_entered);
    }
    return __real_zmk_keymap_layer_deactivate(layer, force);
}

static void disqualifying_position(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    (void)k_sem_take(&second_start, K_FOREVER);
    raise_zmk_position_state_changed((struct zmk_position_state_changed){
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
        .position = 0, .state = true, .timestamp = k_uptime_get(),
    });
    k_sem_give(&second_done);
}
K_THREAD_DEFINE(position_transition, 2048, disqualifying_position, NULL, NULL, NULL,
                5, 0, 100);

static int move(const struct device *dev) {
    struct input_event event = {.type = INPUT_EV_REL, .code = INPUT_REL_X, .value = 1};
    struct zmk_input_processor_state state = {0};
    return zmk_input_processor_handle_event(dev, &event, 0, 0, &state);
}

static void run_test(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    if (move(second) != ZMK_INPUT_PROC_CONTINUE) { exit(2); }
    for (int i = 0; i < 100 && !zmk_keymap_layer_active(2); i++) {
        k_sleep(K_MSEC(1));
    }
    if (!zmk_keymap_layer_active(2)) {
        printk("FAIL: transition lock setup did not activate the second layer\n");
        exit(2);
    }
    atomic_set(&armed, 1);
    if (move(first) != ZMK_INPUT_PROC_CONTINUE ||
        k_sem_take(&first_entered, K_MSEC(100)) != 0) {
        printk("FAIL: transition lock first operation did not enter the keymap\n");
        exit(2);
    }
    k_sem_give(&second_start);
    if (k_sem_take(&second_done, K_MSEC(500)) != 0) {
        printk("FAIL: cross-instance transitions did not complete within 500ms\n");
        exit(1);
    }
    if (!zmk_keymap_layer_active(1) || zmk_keymap_layer_active(2)) {
        printk("FAIL: transition lock changed the requested layer ownership\n");
        exit(1);
    }
    printk("PASS: runtime temp-layer cross-instance transitions and ownership\n");
    exit(0);
}
K_THREAD_DEFINE(transition_lock_test, 2048, run_test, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
