/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Replay a two-listener race through ZMK's actual input thread.
 */
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <zmk/keymap.h>
#include <zmk/workqueue.h>

static const struct device *const left_pad = DEVICE_DT_GET(DT_NODELABEL(left_pad));
static const struct device *const right_pad = DEVICE_DT_GET(DT_NODELABEL(right_pad));

K_SEM_DEFINE(block_started, 0, 1);
K_SEM_DEFINE(block_release, 0, 1);
static struct k_work blocker;

static void block_work(struct k_work *work) {
    ARG_UNUSED(work);
    k_sem_give(&block_started);
    (void)k_sem_take(&block_release, K_FOREVER);
}

static void report(const struct device *pad, uint16_t code, int32_t value) {
    __ASSERT_NO_MSG(input_report(pad, INPUT_EV_REL, code, value, true, K_FOREVER) == 0);
    k_msleep(2);
}

static void replay(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    __ASSERT_NO_MSG(device_is_ready(left_pad) && device_is_ready(right_pad));
    k_work_init(&blocker, block_work);
    for (int order = 0; order < 2; order++) {
        __ASSERT_NO_MSG(k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &blocker) >= 0);
        __ASSERT_NO_MSG(k_sem_take(&block_started, K_FOREVER) == 0);

        /* Each default route runs before either layer activation can execute. */
        report(order == 0 ? left_pad : right_pad, INPUT_REL_X, 1);
        report(order == 0 ? right_pad : left_pad, INPUT_REL_X, 1);
        __ASSERT_NO_MSG(!zmk_keymap_layer_active(1));
        __ASSERT_NO_MSG(!zmk_keymap_layer_active(2));
        k_sem_give(&block_release);
        k_msleep(20);
        __ASSERT_NO_MSG(zmk_keymap_layer_active(order == 0 ? 2 : 1));
        __ASSERT_NO_MSG(!zmk_keymap_layer_active(order == 0 ? 1 : 2));
        printk("PASS: first pointer owns the touch role (%s first)\n",
               order == 0 ? "left" : "right");

        printk("REPLAY: pointer-and-scroll begin\n");
        report(left_pad, INPUT_REL_Y, 4);
        report(right_pad, INPUT_REL_Y, 7);
        printk("REPLAY: pointer-and-scroll end\n");

        k_msleep(450);
        __ASSERT_NO_MSG(!zmk_keymap_layer_active(1) && !zmk_keymap_layer_active(2));
        printk("PASS: both touch layers timed out\n");

        /* The former scroller can become the pointer after the owner stops. */
        report(order == 0 ? right_pad : left_pad, INPUT_REL_X, 1);
        k_msleep(20);
        __ASSERT_NO_MSG(zmk_keymap_layer_active(order == 0 ? 1 : 2));
        __ASSERT_NO_MSG(!zmk_keymap_layer_active(order == 0 ? 2 : 1));
        printk("PASS: pointer role transferred (%s now)\n",
               order == 0 ? "right" : "left");
        printk("REPLAY: after-transfer begin\n");
        report(left_pad, INPUT_REL_Y, 4);
        report(right_pad, INPUT_REL_Y, 7);
        printk("REPLAY: after-transfer end\n");
        k_msleep(450);
        __ASSERT_NO_MSG(!zmk_keymap_layer_active(1) && !zmk_keymap_layer_active(2));
    }
    exit(0);
}

K_THREAD_DEFINE(dual_route_replay, 4096, replay, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
