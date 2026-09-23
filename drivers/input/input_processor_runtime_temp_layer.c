/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Runtime-adjustable replacement for ZMK's stock temp-layer processor.
 *
 * The behaviour is upstream's: pointer movement raises a layer, a key press
 * outside the excluded positions drops it, a timeout drops it, and a keypress
 * in the recent past stops it coming up at all. Three things differ.
 *
 * The layer, the timeout and the idle guard are named properties held in RAM,
 * where upstream takes the first two from the chain's parameter cells. Those
 * are the numbers a person revises after living with a pointer for a week.
 *
 * Work items are per instance. Upstream keeps one global array indexed by
 * layer and its work handler resolves the device with DEVICE_DT_INST_GET(0),
 * so a second instance drives the first one's state.
 *
 * Two instances pointed at the same layer still share it, because a ZMK layer
 * is a bit and not a reference count: whichever drops it first drops it for
 * both. That resolves itself rather than sticking -- the layer change reaches
 * both instances, each sees the layer is down and stops believing it holds it,
 * and the next pointer movement raises it again -- so it costs one dropped
 * layer, not a stuck one. Two instances wanting independent lifetimes need
 * separate layers.
 *
 * The layer number is used consistently as a layer ID. Upstream activates with
 * zmk_keymap_layer_activate(toggle_layer) -- an ID -- but checks with
 * zmk_keymap_layer_active(zmk_keymap_layer_index_to_id(toggle_layer)),
 * converting a value that was never an index. The two agree until layers are
 * reordered, which a Studio client can do.
 */

#define DT_DRV_COMPAT zmk_input_processor_runtime_temp_layer

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <drivers/input_processor.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/workqueue.h>

#include <zmk-input-processors/runtime_temp_layer.h>
#include <zmk-input-processors/runtime_temp_layer_policy.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

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
    /* Set when input requested activation; invalidating events clear it. */
    bool activation_pending;
    uint8_t pending_layer;
    atomic_val_t pending_generation;
    /* Incremented before a parameter writer waits for the mutex. A callback
     * that was already running can therefore detect the concurrent edit. */
    atomic_t generation;
    /* The layer actually raised, which params.layer may have moved past. */
    uint8_t active_layer;
    int64_t last_tapped;
    /* Absolute deadline lets a running stale timeout recognize a newer one. */
    int64_t deactivate_at;
    bool force_deactivate;

    struct k_work activate_work;
    struct k_work_delayable deactivate_work;
};

/*
 * Caller holds the lock.
 *
 * is_active is written before the call and corrected after it, and both halves
 * matter.
 *
 * Before, because ZMK raises zmk_layer_state_changed synchronously from inside
 * zmk_keymap_layer_activate, and this processor listens for it: the handler
 * runs on this thread, inside this function, and re-takes this mutex (Zephyr
 * mutexes are recursive for the owning thread). Finding is_active already
 * equal to what the keymap now says is what makes it a no-op instead of an
 * immediate undo. ZMK writes the layer bit before raising, so the two agree.
 *
 * After, because the keymap can refuse. A non-forcing deactivate does nothing
 * to a locked layer, and set_layer_state() returns 0 either way, so asking is
 * the only way to find out. Believing the request instead would leave this
 * processor thinking it had dropped a layer that is still up, with nothing
 * left that would ever drop it.
 */
static void set_layer_locked(struct runtime_temp_layer_data *data, bool activate) {
    if (data->is_active == activate) {
        return;
    }

    data->is_active = activate;

    if (activate) {
        zmk_keymap_layer_activate(data->active_layer, false);
    } else {
        zmk_keymap_layer_deactivate(data->active_layer, false);
    }

    data->is_active = zmk_keymap_layer_active(data->active_layer);

    LOG_DBG("%s: layer %d %s", data->dev->name, data->active_layer,
            data->is_active ? "raised" : "dropped");
}

/*
 * Raising and dropping happen on ZMK's low-priority work queue rather than in
 * the input callback: activating a layer raises events of its own, and doing
 * that from inside event handling is how a processor ends up re-entering
 * itself. Keeping these callbacks off Zephyr's shared system work queue also
 * prevents a layer listener from delaying Bluetooth, split, watchdog, or
 * device-PM work.
 */
static void activate_work_cb(struct k_work *work) {
    struct runtime_temp_layer_data *data =
        CONTAINER_OF(work, struct runtime_temp_layer_data, activate_work);
    const struct runtime_temp_layer_config *config = data->dev->config;

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return;
    }

    const bool typing = runtime_temp_layer_is_typing(
        data->last_tapped, data->params.require_prior_idle_ms, k_uptime_get());

    const atomic_val_t generation = data->pending_generation;

    bool blocked = false;
    for (size_t i = 0; i < config->num_blocked_layers; i++) {
        if (zmk_keymap_layer_active(config->blocked_by_layers[i])) {
            blocked = true;
            break;
        }
    }

    if (data->activation_pending && generation == atomic_get(&data->generation) && !blocked &&
        runtime_temp_layer_should_activate(data->params.enabled, data->is_active, typing)) {
        data->active_layer = data->pending_layer;
        set_layer_locked(data, true);

        /* A setting writer increments the generation before waiting for this
         * mutex. If it arrived while set_layer_locked() synchronously raised
         * events, undo the obsolete activation before releasing the lock. */
        if (generation != atomic_get(&data->generation)) {
            set_layer_locked(data, false);
        }
    }
    data->activation_pending = false;

    k_mutex_unlock(&data->lock);
}

static void deactivate_work_cb(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct runtime_temp_layer_data *data =
        CONTAINER_OF(delayable, struct runtime_temp_layer_data, deactivate_work);

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return;
    }

    if (data->force_deactivate) {
        data->force_deactivate = false;
        data->deactivate_at = 0;
        set_layer_locked(data, false);
    } else if (data->params.enabled && data->is_active && data->params.timeout_ms > 0 &&
               data->deactivate_at > 0) {
        const int64_t remaining = data->deactivate_at - k_uptime_get();

        if (remaining > 0) {
            k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(),
                                        &data->deactivate_work, K_MSEC(remaining));
        } else {
            data->deactivate_at = 0;
            set_layer_locked(data, false);
        }
    }

    k_mutex_unlock(&data->lock);
}

static bool runtime_temp_layer_device_valid(const struct device *dev);

int runtime_temp_layer_get_params(const struct device *dev, struct runtime_temp_layer_params *out) {
    if (dev == NULL || out == NULL) {
        return -EINVAL;
    }
    if (!runtime_temp_layer_device_valid(dev)) {
        return -ENODEV;
    }

    struct runtime_temp_layer_data *data = dev->data;

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return -EAGAIN;
    }

    *out = data->params;

    k_mutex_unlock(&data->lock);

    return 0;
}

int runtime_temp_layer_set_params(const struct device *dev,
                                  const struct runtime_temp_layer_params *params) {
    if (dev == NULL || params == NULL) {
        return -EINVAL;
    }
    if (!runtime_temp_layer_device_valid(dev)) {
        return -ENODEV;
    }

    if (!runtime_temp_layer_values_valid(params->layer, params->timeout_ms,
                                         params->require_prior_idle_ms, ZMK_KEYMAP_LAYERS_LEN)) {
        LOG_WRN("%s: rejected layer %u, timeout %u ms, prior idle %u ms", dev->name, params->layer,
                params->timeout_ms, params->require_prior_idle_ms);
        return -EINVAL;
    }

    struct runtime_temp_layer_data *data = dev->data;

    /* Invalidate queued and already-running activation work before waiting
     * for its mutex. The callback checks this generation both before and
     * after raising a layer. */
    atomic_inc(&data->generation);

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return -EAGAIN;
    }

    data->params = *params;

    /* A queued event belongs to the parameters it observed. Do not let it
     * raise either the old or the new layer after a setting update. */
    data->activation_pending = false;
    (void)k_work_cancel(&data->activate_work);

    /*
     * A layer this processor is holding has nothing else responsible for
     * lowering it once the processor is switched off, so drop it here rather
     * than leaving it up until a stray key press happens to. It goes through
     * the same work item as every other drop, which keeps layer changes off
     * whichever thread wrote the setting.
     */
    const bool release_held_layer = !params->enabled && data->is_active;
    const bool restart_timeout = params->enabled && data->is_active && params->timeout_ms > 0;

    data->force_deactivate = release_held_layer;
    if (restart_timeout) {
        data->deactivate_at = k_uptime_get() + params->timeout_ms;
    } else {
        data->deactivate_at = 0;
    }

    if (release_held_layer) {
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &data->deactivate_work,
                                    K_NO_WAIT);
    } else if (restart_timeout) {
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &data->deactivate_work,
                                    K_MSEC(params->timeout_ms));
    } else {
        k_work_cancel_delayable(&data->deactivate_work);
    }

    k_mutex_unlock(&data->lock);

    LOG_DBG("%s: %s, layer %d, timeout %u ms, prior idle %u ms", dev->name,
            params->enabled ? "on" : "off", params->layer, params->timeout_ms,
            params->require_prior_idle_ms);

    return 0;
}

static bool position_is_excluded(const struct runtime_temp_layer_config *config,
                                 uint32_t position) {
    for (size_t i = 0; i < config->num_positions; i++) {
        if (config->excluded_positions[i] == position) {
            return true;
        }
    }

    return false;
}

static int handle_layer_state_changed(const struct device *dev) {
    struct runtime_temp_layer_data *data = dev->data;

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /*
     * Something else dropped the layer we raised. Forget that we hold it,
     * rather than dropping it again later on top of whoever raised it next.
     */
    if (data->is_active && !zmk_keymap_layer_active(data->active_layer)) {
        data->is_active = false;
        data->deactivate_at = 0;
        data->force_deactivate = false;
        k_work_cancel_delayable(&data->deactivate_work);
    }

    k_mutex_unlock(&data->lock);

    return ZMK_EV_EVENT_BUBBLE;
}

static int handle_position_state_changed(const struct device *dev,
                                         const struct zmk_position_state_changed *ev) {
    const struct runtime_temp_layer_config *config = dev->config;
    struct runtime_temp_layer_data *data = dev->data;

    if (!ev->state || config->num_positions == 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const bool excluded = position_is_excluded(config, ev->position);

    if (runtime_temp_layer_should_drop_for_position(data->is_active, ev->state,
                                                    config->num_positions > 0, excluded)) {
        set_layer_locked(data, false);
        data->deactivate_at = 0;
        data->force_deactivate = false;
        k_work_cancel_delayable(&data->deactivate_work);
    } else if (!excluded) {
        /* Do not let queued work raise the layer after a disqualifying press. */
        data->activation_pending = false;
    }

    k_mutex_unlock(&data->lock);

    return ZMK_EV_EVENT_BUBBLE;
}

static int handle_keycode_state_changed(const struct device *dev,
                                        const struct zmk_keycode_state_changed *ev) {
    struct runtime_temp_layer_data *data = dev->data;

    if (!ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    data->last_tapped = ev->timestamp;
    /* The queued activation rechecks typing, but clearing this also covers a
     * zero idle guard and preserves event order. */
    data->activation_pending = false;

    k_mutex_unlock(&data->lock);

    return ZMK_EV_EVENT_BUBBLE;
}

static int dispatch(const struct device *dev, const zmk_event_t *eh) {
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(eh);

    if (position != NULL) {
        return handle_position_state_changed(dev, position);
    }

    const struct zmk_keycode_state_changed *keycode = as_zmk_keycode_state_changed(eh);

    if (keycode != NULL) {
        return handle_keycode_state_changed(dev, keycode);
    }

    if (as_zmk_layer_state_changed(eh) != NULL) {
        return handle_layer_state_changed(dev);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

#define DISPATCH_TO_INST(n) dispatch(DEVICE_DT_INST_GET(n), eh);

static int runtime_temp_layer_event_cb(const zmk_event_t *eh) {
    DT_INST_FOREACH_STATUS_OKAY(DISPATCH_TO_INST)

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(runtime_temp_layer, runtime_temp_layer_event_cb);
ZMK_SUBSCRIPTION(runtime_temp_layer, zmk_layer_state_changed);

/*
 * Position events are only interesting to an instance that has an exclusion
 * list, and that list stays in devicetree, so no instance can grow one later
 * and this can be compiled out.
 *
 * The idle guard cannot be treated the same way, even though upstream does:
 * require-prior-idle-ms is a runtime value now, so an instance that starts at
 * zero can be given a real one from a client. A subscription compiled out on
 * the devicetree value would leave that setting accepting edits and doing
 * nothing. Runtime-ifying a value invalidates every #if that used to depend on
 * it, which is the price of the move and worth paying once here.
 */
#define NEEDS_POSITIONS(n) DT_INST_PROP_HAS_IDX(n, excluded_positions, 0) ||

#if DT_INST_FOREACH_STATUS_OKAY(NEEDS_POSITIONS) 0
ZMK_SUBSCRIPTION(runtime_temp_layer, zmk_position_state_changed);
#endif

ZMK_SUBSCRIPTION(runtime_temp_layer, zmk_keycode_state_changed);

static int runtime_temp_layer_handle_event(const struct device *dev, struct input_event *event,
                                           uint32_t param1, uint32_t param2,
                                           struct zmk_input_processor_state *state) {
    /* The layer and the timeout are on the node; the chain declares no cells. */
    ARG_UNUSED(event);
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    struct runtime_temp_layer_data *data = dev->data;

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const struct runtime_temp_layer_params params = data->params;

    /* Switched off, this stage is a no-op: it raises nothing and, because it
     * reschedules nothing either, holds no timer that could fire later. */
    if (!params.enabled) {
        k_mutex_unlock(&data->lock);

        return ZMK_INPUT_PROC_CONTINUE;
    }

    const bool typing = runtime_temp_layer_is_typing(data->last_tapped,
                                                     params.require_prior_idle_ms, k_uptime_get());

    if (runtime_temp_layer_should_activate(params.enabled, data->is_active, typing)) {
        data->pending_layer = params.layer;
        data->pending_generation = atomic_get(&data->generation);
        data->activation_pending = true;
        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &data->activate_work);
    }

    if (runtime_temp_layer_should_schedule_timeout(&params)) {
        data->deactivate_at = k_uptime_get() + params.timeout_ms;
        data->force_deactivate = false;
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &data->deactivate_work,
                                    K_MSEC(params.timeout_ms));
    }

    k_mutex_unlock(&data->lock);

    return ZMK_INPUT_PROC_CONTINUE;
}

static int runtime_temp_layer_init(const struct device *dev) {
    struct runtime_temp_layer_data *data = dev->data;
    const struct runtime_temp_layer_config *config = dev->config;

    for (size_t i = 0; i < config->num_blocked_layers; i++) {
        if (config->blocked_by_layers[i] >= ZMK_KEYMAP_LAYERS_LEN) {
            LOG_ERR("%s: blocked layer %u is outside the keymap", dev->name,
                    config->blocked_by_layers[i]);
            return -EINVAL;
        }
    }

    data->dev = dev;
    atomic_set(&data->generation, 0);
    k_mutex_init(&data->lock);
    k_work_init(&data->activate_work, activate_work_cb);
    k_work_init_delayable(&data->deactivate_work, deactivate_work_cb);

    return 0;
}

static const struct zmk_input_processor_driver_api runtime_temp_layer_driver_api = {
    .handle_event = runtime_temp_layer_handle_event,
};

#define RUNTIME_TEMP_LAYER_INST(n)                                                                 \
    BUILD_ASSERT(DT_INST_PROP(n, layer) >= 0 && DT_INST_PROP(n, layer) < ZMK_KEYMAP_LAYERS_LEN,    \
                 "layer must name a layer this keymap has");                                       \
    BUILD_ASSERT(DT_INST_PROP(n, timeout_ms) >= 0 &&                                               \
                     DT_INST_PROP(n, timeout_ms) <= RUNTIME_TEMP_LAYER_MAX_MS,                     \
                 "timeout-ms must be 0-60000");                                                    \
    BUILD_ASSERT(DT_INST_PROP_OR(n, require_prior_idle_ms, 0) >= 0 &&                              \
                     DT_INST_PROP_OR(n, require_prior_idle_ms, 0) <= RUNTIME_TEMP_LAYER_MAX_MS,    \
                 "require-prior-idle-ms must be 0-60000");                                         \
    static const uint16_t runtime_temp_layer_positions_##n[] =                                     \
        DT_INST_PROP_OR(n, excluded_positions, {});                                                \
    static const uint16_t runtime_temp_layer_blockers_##n[] =                                      \
        DT_INST_PROP_OR(n, blocked_by_layers, {});                                                 \
    static const struct runtime_temp_layer_config runtime_temp_layer_config_##n = {                \
        .excluded_positions = runtime_temp_layer_positions_##n,                                    \
        .num_positions = DT_INST_PROP_LEN_OR(n, excluded_positions, 0),                            \
        .blocked_by_layers = runtime_temp_layer_blockers_##n,                                      \
        .num_blocked_layers = DT_INST_PROP_LEN_OR(n, blocked_by_layers, 0),                         \
    };                                                                                             \
    static struct runtime_temp_layer_data runtime_temp_layer_data_##n = {                          \
        .params =                                                                                  \
            {                                                                                      \
                .enabled = !DT_INST_PROP(n, start_disabled),                                       \
                .layer = DT_INST_PROP(n, layer),                                                   \
                .timeout_ms = DT_INST_PROP(n, timeout_ms),                                         \
                .require_prior_idle_ms = DT_INST_PROP_OR(n, require_prior_idle_ms, 0),             \
            },                                                                                     \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, &runtime_temp_layer_init, NULL, &runtime_temp_layer_data_##n,         \
                          &runtime_temp_layer_config_##n, POST_KERNEL,                             \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &runtime_temp_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_TEMP_LAYER_INST)

#define RUNTIME_TEMP_LAYER_DEVICE_REF(n) DEVICE_DT_INST_GET(n),

/* The runtime API reads dev->data as this driver's data, so it accepts only the
 * devices this driver defined. */
static const struct device *const runtime_temp_layer_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(RUNTIME_TEMP_LAYER_DEVICE_REF)};

static bool runtime_temp_layer_device_valid(const struct device *dev) {
    for (size_t i = 0U; i < ARRAY_SIZE(runtime_temp_layer_devices); i++) {
        if (runtime_temp_layer_devices[i] == dev) {
            return true;
        }
    }

    return false;
}
