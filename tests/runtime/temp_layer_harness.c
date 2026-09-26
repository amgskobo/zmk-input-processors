/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <zmk-input-processors/runtime_temp_layer_policy.h>

typedef int32_t atomic_t;
typedef int32_t atomic_val_t;
typedef struct { int64_t ms; } k_timeout_t;
struct k_mutex { int depth; };
struct k_work { void (*handler)(struct k_work *work); bool pending; };
struct k_work_delayable { struct k_work work; bool scheduled; int64_t delay_ms; };
struct k_work_q { int unused; };
struct device { const void *config; void *data; const char *name; };
struct input_event { uint8_t type; uint16_t code; int32_t value; };
struct zmk_input_processor_state { int unused; };
typedef struct { int kind; } zmk_event_t;
struct zmk_position_state_changed { zmk_event_t header; uint32_t position; bool state; };
struct zmk_keycode_state_changed { zmk_event_t header; bool state; int64_t timestamp; };
struct zmk_layer_state_changed { zmk_event_t header; };
enum { POSITION_EVENT = 1, KEYCODE_EVENT, LAYER_EVENT, OTHER_EVENT };

struct runtime_temp_layer_config {
    const uint16_t *excluded_positions;
    size_t num_positions;
    const uint16_t *blocked_by_layers;
    size_t num_blocked_layers;
};
struct runtime_temp_layer_data {
    const struct device *dev;
    struct k_mutex lock;
    struct runtime_temp_layer_params params;
    bool is_active;
    bool activation_pending;
    uint8_t pending_layer;
    atomic_val_t pending_generation;
    atomic_t generation;
    uint8_t active_layer;
    int64_t last_tapped;
    int64_t deactivate_at;
    bool force_deactivate;
    struct k_work activate_work;
    struct k_work_delayable deactivate_work;
};

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define CONTAINER_OF(ptr, type, field) ((type *)(void *)((char *)(ptr) - offsetof(type, field)))
#define ARG_UNUSED(x) ((void)(x))
#define LOG_DBG(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define K_FOREVER ((k_timeout_t){-1})
#define K_NO_WAIT ((k_timeout_t){0})
#define K_MSEC(ms) ((k_timeout_t){(ms)})
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_INPUT_PROC_CONTINUE 0
#define ZMK_KEYMAP_LAYERS_LEN 8
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define DEVICE_DT_INST_GET(n) (&valid_device)

static struct device valid_device;
/* Another instance first, so a lookup has to walk the list. */
static struct device first_device;
static const struct device *const runtime_temp_layer_devices[] = {&first_device, &valid_device};
static struct k_work_q lowprio_queue;
static int64_t now_ms;
static int lock_failures;
static uint32_t active_layers;
static uint32_t locked_layers;
static int layer_events;
/* Runs inside zmk_keymap_layer_activate(), where a settings writer can land. */
static void (*during_activate)(void);

static int handle_layer_state_changed(const struct device *dev);
static bool runtime_temp_layer_device_valid(const struct device *dev);

static atomic_val_t atomic_get(const atomic_t *value) { return *value; }
static void atomic_set(atomic_t *value, atomic_val_t next) { *value = next; }
static void atomic_inc(atomic_t *value) { (*value)++; }
static int64_t k_uptime_get(void) { return now_ms; }
static void k_mutex_init(struct k_mutex *mutex) { mutex->depth = 0; }
static int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout) {
    ARG_UNUSED(timeout);
    if (lock_failures > 0) {
        lock_failures--;
        return -EAGAIN;
    }
    mutex->depth++;
    return 0;
}
static void k_mutex_unlock(struct k_mutex *mutex) {
    assert(mutex->depth > 0);
    mutex->depth--;
}
static struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return &lowprio_queue; }
static void k_work_init(struct k_work *work, void (*handler)(struct k_work *work)) {
    *work = (struct k_work){.handler = handler};
}
static void k_work_init_delayable(struct k_work_delayable *work,
                                  void (*handler)(struct k_work *work)) {
    *work = (struct k_work_delayable){.work = {.handler = handler}};
}
static struct k_work_delayable *k_work_delayable_from_work(struct k_work *work) {
    return CONTAINER_OF(work, struct k_work_delayable, work);
}
static int k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work) {
    assert(queue == &lowprio_queue);
    work->pending = true;
    return 1;
}
static int k_work_cancel(struct k_work *work) {
    work->pending = false;
    return 0;
}
static int k_work_reschedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                                       k_timeout_t delay) {
    assert(queue == &lowprio_queue);
    work->scheduled = true;
    work->delay_ms = delay.ms;
    return 1;
}
static int k_work_cancel_delayable(struct k_work_delayable *work) {
    work->scheduled = false;
    return 0;
}

/* ZMK sets the bit and then raises the layer event on the same thread. */
static void raise_layer_event(void) {
    layer_events++;
    handle_layer_state_changed(&valid_device);
}
static int zmk_keymap_layer_activate(uint8_t layer, bool locking) {
    assert(!locking);
    active_layers |= 1U << layer;
    raise_layer_event();
    if (during_activate != NULL) {
        during_activate();
    }
    return 0;
}
/* A non-forcing deactivate leaves a locked layer up and still returns 0. */
static int zmk_keymap_layer_deactivate(uint8_t layer, bool locking) {
    assert(!locking);
    if ((locked_layers & (1U << layer)) == 0) {
        active_layers &= ~(1U << layer);
        raise_layer_event();
    }
    return 0;
}
static bool zmk_keymap_layer_active(uint8_t layer) { return (active_layers & (1U << layer)) != 0; }

static const struct zmk_position_state_changed *
as_zmk_position_state_changed(const zmk_event_t *eh) {
    return eh->kind == POSITION_EVENT ? (const void *)eh : NULL;
}
static const struct zmk_keycode_state_changed *
as_zmk_keycode_state_changed(const zmk_event_t *eh) {
    return eh->kind == KEYCODE_EVENT ? (const void *)eh : NULL;
}
static const struct zmk_layer_state_changed *as_zmk_layer_state_changed(const zmk_event_t *eh) {
    return eh->kind == LAYER_EVENT ? (const void *)eh : NULL;
}

/* DRIVER_FUNCTIONS */

static struct runtime_temp_layer_data data;

static void bump_generation(void) { atomic_inc(&data.generation); }

static void run_activate(void) {
    data.activate_work.pending = false;
    data.activate_work.handler(&data.activate_work);
}

static void run_deactivate(void) {
    data.deactivate_work.scheduled = false;
    data.deactivate_work.work.handler(&data.deactivate_work.work);
}

static int input(void) {
    struct input_event event = {0};
    return runtime_temp_layer_handle_event(&valid_device, &event, 0, 0, NULL);
}

static int position(uint32_t at, bool pressed) {
    const struct zmk_position_state_changed ev = {{POSITION_EVENT}, at, pressed};
    return dispatch(&valid_device, &ev.header);
}

static int keycode(bool pressed, int64_t timestamp) {
    const struct zmk_keycode_state_changed ev = {{KEYCODE_EVENT}, pressed, timestamp};
    return dispatch(&valid_device, &ev.header);
}

static struct runtime_temp_layer_params params_of(bool enabled, uint8_t layer, uint32_t timeout_ms,
                                                  uint16_t idle_ms) {
    return (struct runtime_temp_layer_params){enabled, layer, timeout_ms, idle_ms};
}

/* Raise layer 2 through the real input -> work path. */
static void raise_by_input(void) {
    assert(input() == 0);
    assert(data.activation_pending && data.activate_work.pending);
    run_activate();
    assert(data.is_active && zmk_keymap_layer_active(2));
}

static void test_api_and_init(void) {
    static const uint16_t bad_blockers[] = {1, ZMK_KEYMAP_LAYERS_LEN};
    const struct runtime_temp_layer_config bad = {.blocked_by_layers = bad_blockers,
                                                  .num_blocked_layers = 2};
    struct device bad_device = {.config = &bad, .data = &data, .name = "bad"};
    struct device other_device = {.config = valid_device.config, .data = &data};
    struct runtime_temp_layer_params out;
    const struct runtime_temp_layer_params valid = params_of(true, 2, 100, 0);

    assert(runtime_temp_layer_init(&bad_device) == -EINVAL);
    static const uint16_t bad_first[] = {ZMK_KEYMAP_LAYERS_LEN, 1};
    const struct runtime_temp_layer_config bad_head = {.blocked_by_layers = bad_first,
                                                       .num_blocked_layers = 2};
    bad_device.config = &bad_head;
    assert(runtime_temp_layer_init(&bad_device) == -EINVAL);
    data.generation = 5;
    data.lock.depth = 3;
    assert(runtime_temp_layer_init(&valid_device) == 0);
    assert(data.dev == &valid_device && data.generation == 0 && data.lock.depth == 0);

    assert(runtime_temp_layer_get_params(NULL, &out) == -EINVAL);
    assert(runtime_temp_layer_get_params(&valid_device, NULL) == -EINVAL);
    assert(runtime_temp_layer_get_params(&other_device, &out) == -ENODEV);
    lock_failures = 1;
    assert(runtime_temp_layer_get_params(&valid_device, &out) == -EAGAIN);
    assert(runtime_temp_layer_get_params(&valid_device, &out) == 0);
    assert(out.enabled && out.layer == 2 && out.timeout_ms == 100 && out.require_prior_idle_ms == 0);

    assert(runtime_temp_layer_set_params(NULL, &valid) == -EINVAL);
    assert(runtime_temp_layer_set_params(&valid_device, NULL) == -EINVAL);
    assert(runtime_temp_layer_set_params(&other_device, &valid) == -ENODEV);
    const struct runtime_temp_layer_params bad_layer = params_of(true, ZMK_KEYMAP_LAYERS_LEN, 0, 0);
    assert(runtime_temp_layer_set_params(&valid_device, &bad_layer) == -EINVAL);
    /* A writer that cannot take the lock still invalidates queued work. */
    const atomic_val_t before = atomic_get(&data.generation);
    lock_failures = 1;
    assert(runtime_temp_layer_set_params(&valid_device, &valid) == -EAGAIN);
    assert(atomic_get(&data.generation) == before + 1);
}

static void test_raise_and_timeout(void) {
    /* Enabled, nothing held: the timer is cancelled, not armed. */
    const struct runtime_temp_layer_params p = params_of(true, 2, 100, 0);
    data.deactivate_work.scheduled = true;
    assert(runtime_temp_layer_set_params(&valid_device, &p) == 0);
    assert(!data.deactivate_work.scheduled && data.deactivate_at == 0);

    now_ms = 1000;
    raise_by_input();
    assert(data.deactivate_work.scheduled && data.deactivate_work.delay_ms == 100);
    assert(data.deactivate_at == 1100);

    /* Movement while held re-arms the timer and queues nothing. */
    now_ms = 1050;
    assert(input() == 0);
    assert(!data.activate_work.pending && data.deactivate_at == 1150);

    /* A stale timer that fires early re-arms for what is left. */
    now_ms = 1100;
    run_deactivate();
    assert(data.is_active && data.deactivate_work.scheduled && data.deactivate_work.delay_ms == 50);
    now_ms = 1149;
    run_deactivate();
    assert(data.is_active && data.deactivate_work.scheduled && data.deactivate_work.delay_ms == 1);
    now_ms = 1150;
    run_deactivate();
    assert(!data.is_active && !zmk_keymap_layer_active(2) && data.deactivate_at == 0);

    /* Each operand of the timeout guard can stop a drop on its own. */
    raise_by_input();
    data.params.enabled = false;
    run_deactivate();
    assert(data.is_active);
    data.params.enabled = true;
    data.params.timeout_ms = 0;
    now_ms = data.deactivate_at;
    run_deactivate();
    assert(data.is_active);
    data.params.timeout_ms = 100;
    data.deactivate_at = 0;
    run_deactivate();
    assert(data.is_active);
    lock_failures = 1;
    run_deactivate();
    assert(data.is_active);

    /* The keymap refused to drop a locked layer; the processor believes it. */
    locked_layers = 1U << 2;
    data.deactivate_at = now_ms;
    run_deactivate();
    assert(data.is_active && zmk_keymap_layer_active(2));
    locked_layers = 0;
    data.deactivate_at = now_ms;
    run_deactivate();
    assert(!data.is_active);

    /* Not held: the guard's is_active operand. */
    data.deactivate_at = now_ms;
    run_deactivate();
    assert(!data.is_active);
}

static void test_activation_guards(void) {
    now_ms = 5000;

    /* The layer came up elsewhere: the work sees it held and does nothing. */
    assert(input() == 0);
    lock_failures = 1;
    run_activate();
    assert(data.activation_pending && !data.is_active);
    data.is_active = true;
    run_activate();
    assert(!data.activation_pending && !zmk_keymap_layer_active(2));
    data.is_active = false;

    /* Work with nothing pending does nothing. */
    run_activate();
    assert(!data.is_active);

    /* A settings write between the event and the work discards it. */
    assert(input() == 0);
    bump_generation();
    run_activate();
    assert(!data.is_active && !data.activation_pending);

    /* A blocking layer, found first or last in the list, stops activation. */
    active_layers = 1U << 5;
    assert(input() == 0);
    run_activate();
    assert(!data.is_active);
    active_layers = 1U << 6;
    assert(input() == 0);
    run_activate();
    assert(!data.is_active);
    active_layers = 0;

    /* A write that lands while the layer is being raised undoes it. */
    assert(input() == 0);
    during_activate = bump_generation;
    const int events = layer_events;
    run_activate();
    during_activate = NULL;
    assert(!data.is_active && !zmk_keymap_layer_active(2) && layer_events == events + 2);

    /* The idle guard blocks both the queueing and the queued work. */
    const struct runtime_temp_layer_params guarded = params_of(true, 2, 100, 200);
    assert(runtime_temp_layer_set_params(&valid_device, &guarded) == 0);
    assert(keycode(false, now_ms) == 0);
    assert(data.last_tapped == 0); /* releases are not typing */
    assert(keycode(true, now_ms) == 0);
    assert(data.last_tapped == now_ms);
    assert(input() == 0);
    assert(!data.activate_work.pending);
    data.activation_pending = true;
    data.activate_work.pending = true;
    run_activate();
    assert(!data.is_active);
    now_ms += 200;
    raise_by_input();

    /* A key press clears a pending activation; a failed lock leaves it. */
    data.is_active = false;
    active_layers = 0;
    assert(input() == 0);
    lock_failures = 1;
    assert(keycode(true, now_ms) == 0);
    assert(data.activation_pending);
    assert(keycode(true, now_ms) == 0);
    assert(!data.activation_pending);

    /* Disabled, input is a no-op; a failed lock passes the event on. */
    now_ms += 1000;
    run_activate();
    data.params.enabled = false;
    assert(input() == 0);
    assert(!data.activation_pending && !data.activate_work.pending);
    data.params.enabled = true;
    lock_failures = 1;
    assert(input() == 0);
    assert(!data.activation_pending);

    /* A zero timeout raises without arming a timer. */
    data.params.timeout_ms = 0;
    data.deactivate_work.scheduled = false;
    raise_by_input();
    assert(!data.deactivate_work.scheduled);
}

static void test_settings_while_held(void) {
    /* Re-timing a held layer restarts the timer from the write. */
    const struct runtime_temp_layer_params retimed = params_of(true, 3, 300, 0);
    now_ms = 20000;
    data.activation_pending = true;
    data.activate_work.pending = true;
    assert(runtime_temp_layer_set_params(&valid_device, &retimed) == 0);
    assert(!data.activation_pending && !data.activate_work.pending);
    assert(data.deactivate_at == 20300 && data.deactivate_work.delay_ms == 300);
    assert(data.active_layer == 2 && data.params.layer == 3);

    /* A one-millisecond timeout on a held layer is still a timeout. */
    const struct runtime_temp_layer_params brief = params_of(true, 2, 1, 0);
    assert(runtime_temp_layer_set_params(&valid_device, &brief) == 0);
    assert(data.deactivate_at == now_ms + 1 && data.deactivate_work.delay_ms == 1);
    data.deactivate_work.scheduled = false;
    run_deactivate();
    assert(data.is_active && data.deactivate_work.scheduled && data.deactivate_work.delay_ms == 1);

    /* A zero timeout on a held layer cancels the timer. */
    const struct runtime_temp_layer_params untimed = params_of(true, 2, 0, 0);
    assert(runtime_temp_layer_set_params(&valid_device, &untimed) == 0);
    assert(!data.deactivate_work.scheduled && data.deactivate_at == 0 && data.is_active);

    /* Switching off drops the held layer through the work item. */
    const struct runtime_temp_layer_params off = params_of(false, 2, 100, 0);
    assert(runtime_temp_layer_set_params(&valid_device, &off) == 0);
    assert(data.force_deactivate && data.deactivate_work.delay_ms == 0 && data.is_active);
    data.deactivate_at = 777;
    run_deactivate();
    assert(data.deactivate_at == 0);
    assert(!data.force_deactivate && !data.is_active && !zmk_keymap_layer_active(2));

    /* Switching off with nothing held arms nothing; a forced drop of an
     * already-dropped layer is harmless. */
    assert(runtime_temp_layer_set_params(&valid_device, &off) == 0);
    assert(!data.force_deactivate && !data.deactivate_work.scheduled);
    data.force_deactivate = true;
    run_deactivate();
    assert(!data.force_deactivate && !data.is_active);
}

static void test_events(void) {
    static const uint16_t no_positions[1] = {0};
    const struct runtime_temp_layer_config bare = {.excluded_positions = no_positions};
    struct device bare_device = {.config = &bare, .data = &data, .name = "bare"};
    const struct runtime_temp_layer_params p = params_of(true, 2, 100, 0);
    assert(runtime_temp_layer_set_params(&valid_device, &p) == 0);
    now_ms = 30000;

    /* Releases and instances without an exclusion list ignore positions. */
    raise_by_input();
    assert(position(0, false) == 0);
    assert(data.is_active);
    data.activation_pending = true;
    assert(position(0, false) == 0);
    assert(data.activation_pending);
    data.activation_pending = false;
    const struct zmk_position_state_changed press = {{POSITION_EVENT}, 0, true};
    assert(dispatch(&bare_device, &press.header) == 0);
    assert(data.is_active);

    /* An excluded position keeps the layer; a failed lock changes nothing. */
    assert(position(7, true) == 0);
    assert(position(9, true) == 0);
    assert(data.is_active);
    lock_failures = 1;
    assert(position(0, true) == 0);
    assert(data.is_active);

    /* Any other position drops it, its timer and any queued forced drop. */
    data.force_deactivate = true;
    assert(position(0, true) == 0);
    assert(!data.is_active && !data.deactivate_work.scheduled && data.deactivate_at == 0);
    assert(!data.force_deactivate);

    /* Not held, an excluded press leaves a pending activation alone and any
     * other press cancels it. */
    assert(input() == 0);
    assert(position(9, true) == 0);
    assert(data.activation_pending);
    assert(position(0, true) == 0);
    assert(!data.activation_pending);

    /* Something else dropping our layer makes us forget it. */
    raise_by_input();
    const struct zmk_layer_state_changed layer = {{LAYER_EVENT}};
    active_layers = 0;
    lock_failures = 1;
    assert(dispatch(&valid_device, &layer.header) == 0);
    assert(data.is_active);
    data.force_deactivate = true;
    assert(dispatch(&valid_device, &layer.header) == 0);
    assert(!data.is_active && !data.deactivate_work.scheduled && data.deactivate_at == 0);
    assert(!data.force_deactivate);

    /* The listener hands every event to each instance. */
    raise_by_input();
    active_layers = 0;
    assert(runtime_temp_layer_event_cb(&layer.header) == 0);
    assert(!data.is_active);

    raise_by_input();
    data.force_deactivate = true;
    assert(input() == 0);
    assert(!data.force_deactivate && data.deactivate_work.scheduled);
    active_layers = 0;
    assert(dispatch(&valid_device, &layer.header) == 0);
    assert(!data.is_active);

    /* Unrelated events and a layer event with nothing held pass through. */
    const zmk_event_t other = {OTHER_EVENT};
    assert(dispatch(&valid_device, &other) == 0);
    assert(dispatch(&valid_device, &layer.header) == 0);
    assert(!data.is_active);
}

int main(void) {
    static const uint16_t excluded[] = {7, 9};
    static const uint16_t blockers[] = {5, 6};
    const struct runtime_temp_layer_config config = {
        .excluded_positions = excluded, .num_positions = 2,
        .blocked_by_layers = blockers, .num_blocked_layers = 2};
    data.params = params_of(true, 2, 100, 0);
    valid_device = (struct device){.config = &config, .data = &data, .name = "test"};

    test_api_and_init();
    test_raise_and_timeout();
    test_activation_guards();
    test_settings_while_held();
    test_events();
    assert(data.lock.depth == 0);
    /* Every device in the list counts, the first as much as the last. */
    assert(runtime_temp_layer_device_valid(&first_device));
    puts("runtime temp layer driver: PASS");
    return 0;
}
