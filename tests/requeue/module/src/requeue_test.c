/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <drivers/input_processor.h>
#include <zmk/keymap.h>
#include <zmk/workqueue.h>
#include <zmk-input-processors/runtime_temp_layer.h>

static const struct device *const processor = DEVICE_DT_GET(DT_NODELABEL(requeue_test));
K_SEM_DEFINE(paused, 0, 1);
K_SEM_DEFINE(resume, 0, 1);
static atomic_t armed;
static int submitted;
int __real_z_impl_k_mutex_unlock(struct k_mutex *mutex);
int __wrap_z_impl_k_mutex_unlock(struct k_mutex *mutex) {
 int ret = __real_z_impl_k_mutex_unlock(mutex);
 if (k_current_get() == k_work_queue_thread_get(zmk_workqueue_lowprio_work_q()) &&
     atomic_cas(&armed, 1, 0)) {
  k_sem_give(&paused);
  __ASSERT_NO_MSG(k_sem_take(&resume, K_SECONDS(1)) == 0);
 }
 return ret;
}
int __real_k_work_submit_to_queue(struct k_work_q *q, struct k_work *work);
int __wrap_k_work_submit_to_queue(struct k_work_q *q, struct k_work *work) {
 int ret = __real_k_work_submit_to_queue(q, work);
 submitted = ret;
 return ret;
}
static void move(void) {
 struct input_event event = {.type = INPUT_EV_REL, .code = INPUT_REL_X, .value = 1};
 struct zmk_input_processor_state state = {0};
 __ASSERT_NO_MSG(zmk_input_processor_handle_event(processor, &event, 0, 0, &state) == 0);
}
static void run(void *a, void *b, void *c) {
 ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
 __ASSERT_NO_MSG(device_is_ready(processor));
 zmk_keymap_layer_activate(2, false); /* old callback takes the no-transition path */
 atomic_set(&armed, 1);
 move();
 __ASSERT_NO_MSG(k_sem_take(&paused, K_SECONDS(1)) == 0);
 struct runtime_temp_layer_params params;
 __ASSERT_NO_MSG(runtime_temp_layer_get_params(processor, &params) == 0);
 __ASSERT_NO_MSG(runtime_temp_layer_set_params(processor, &params) == 0);
 zmk_keymap_layer_deactivate(2, false);
 move(); /* exactly one new input while the old callback is still RUNNING */
 printk("activation requeue result=%d\n", submitted);
 __ASSERT(submitted == 2, "running cancellation rejected new activation");
 k_sem_give(&resume);
 for (int i = 0; i < 100 && !zmk_keymap_layer_active(1); i++) { k_msleep(1); }
 __ASSERT_NO_MSG(zmk_keymap_layer_active(1));
 printk("PASS: one new input requeues activation after a concurrent setter\n");
 exit(0);
}
K_THREAD_DEFINE(requeue_test_thread, 3072, run, NULL, NULL, NULL, 3, 0, 100);
