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

#include <drivers/input_processor.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#include <zmk-input-processors/runtime_temp_layer.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct runtime_temp_layer_config {
    const uint16_t *excluded_positions;
    size_t num_positions;
};

struct runtime_temp_layer_data {
    const struct device *dev;
    struct k_mutex lock;

    struct runtime_temp_layer_params params;

    bool is_active;
    /* The layer actually raised, which params.layer may have moved past. */
    uint8_t active_layer;
    int64_t last_tapped;

    struct k_work activate_work;
    struct k_work_delayable deactivate_work;
};

/* Caller holds the lock. */
static void set_layer_locked(struct runtime_temp_layer_data *data, bool activate) {
    if (data->is_active == activate) {
        return;
    }

    data->is_active = activate;

    if (activate) {
        zmk_keymap_layer_activate(data->active_layer, false);
        LOG_DBG("%s: layer %d raised", data->dev->name, data->active_layer);
    } else {
        zmk_keymap_layer_deactivate(data->active_layer, false);
        LOG_DBG("%s: layer %d dropped", data->dev->name, data->active_layer);
    }
}

/*
 * Raising and dropping happen on the system work queue rather than in the
 * input callback: activating a layer raises events of its own, and doing that
 * from inside event handling is how a processor ends up re-entering itself.
 */
static void activate_work_cb(struct k_work *work) {
    struct runtime_temp_layer_data *data =
        CONTAINER_OF(work, struct runtime_temp_layer_data, activate_work);

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return;
    }

    set_layer_locked(data, true);

    k_mutex_unlock(&data->lock);
}

static void deactivate_work_cb(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct runtime_temp_layer_data *data =
        CONTAINER_OF(delayable, struct runtime_temp_layer_data, deactivate_work);

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return;
    }

    set_layer_locked(data, false);

    k_mutex_unlock(&data->lock);
}

int runtime_temp_layer_get_params(const struct device *dev,
                                  struct runtime_temp_layer_params *out) {
    if (dev == NULL || out == NULL) {
        return -EINVAL;
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

    if (params->layer >= ZMK_KEYMAP_LAYERS_LEN) {
        LOG_WRN("%s: rejected layer %d", dev->name, params->layer);
        return -EINVAL;
    }

    struct runtime_temp_layer_data *data = dev->data;

    if (k_mutex_lock(&data->lock, K_FOREVER) < 0) {
        return -EAGAIN;
    }

    data->params = *params;

    k_mutex_unlock(&data->lock);

    LOG_DBG("%s: layer %d, timeout %u ms, prior idle %u ms", dev->name, params->layer,
            params->timeout_ms, params->require_prior_idle_ms);

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

    if (data->is_active && !position_is_excluded(config, ev->position)) {
        set_layer_locked(data, false);
        k_work_cancel_delayable(&data->deactivate_work);
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
ZMK_SUBSCRIPTION(runtime_temp_layer, zmk_position_state_changed);
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
    const int64_t now = k_uptime_get();
    const bool typing = params.require_prior_idle_ms > 0 &&
                        (data->last_tapped + params.require_prior_idle_ms) > now;

    if (!data->is_active && !typing) {
        data->active_layer = params.layer;
        k_work_submit(&data->activate_work);
    }

    if (params.timeout_ms > 0) {
        k_work_reschedule(&data->deactivate_work, K_MSEC(params.timeout_ms));
    }

    k_mutex_unlock(&data->lock);

    return ZMK_INPUT_PROC_CONTINUE;
}

static int runtime_temp_layer_init(const struct device *dev) {
    struct runtime_temp_layer_data *data = dev->data;

    data->dev = dev;
    k_mutex_init(&data->lock);
    k_work_init(&data->activate_work, activate_work_cb);
    k_work_init_delayable(&data->deactivate_work, deactivate_work_cb);

    return 0;
}

static const struct zmk_input_processor_driver_api runtime_temp_layer_driver_api = {
    .handle_event = runtime_temp_layer_handle_event,
};

#define RUNTIME_TEMP_LAYER_INST(n)                                                                 \
    BUILD_ASSERT(DT_INST_PROP(n, layer) < ZMK_KEYMAP_LAYERS_LEN,                                   \
                 "layer must name a layer this keymap has");                                       \
    static const uint16_t runtime_temp_layer_positions_##n[] =                                     \
        DT_INST_PROP_OR(n, excluded_positions, {});                                                \
    static const struct runtime_temp_layer_config runtime_temp_layer_config_##n = {                \
        .excluded_positions = runtime_temp_layer_positions_##n,                                    \
        .num_positions = DT_INST_PROP_LEN_OR(n, excluded_positions, 0),                            \
    };                                                                                             \
    static struct runtime_temp_layer_data runtime_temp_layer_data_##n = {                          \
        .params =                                                                                  \
            {                                                                                      \
                .layer = DT_INST_PROP(n, layer),                                                   \
                .timeout_ms = DT_INST_PROP(n, timeout_ms),                                         \
                .require_prior_idle_ms = DT_INST_PROP_OR(n, require_prior_idle_ms, 0),             \
            },                                                                                     \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, &runtime_temp_layer_init, NULL, &runtime_temp_layer_data_##n,         \
                          &runtime_temp_layer_config_##n, POST_KERNEL,                             \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &runtime_temp_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_TEMP_LAYER_INST)
