/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Runtime-switchable replacement for ZMK's stock code mapper.
 *
 * The event path is upstream's, unchanged: walk the pairs, rewrite the code on
 * the first match. One thing differs.
 *
 * The map can be turned off while the keyboard runs, from a flag held in RAM.
 * That is the only question a mapper has that is worth asking at runtime --
 * the map itself is structural -- and it is the one that turns a scroll route
 * into a plain pointer route without a layer or a rebuild.
 */

#define DT_DRV_COMPAT zmk_input_processor_runtime_code_mapper

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <drivers/input_processor.h>

#include <zmk-input-processors/runtime_code_mapper.h>
#include <zmk-input-processors/runtime_code_mapper_math.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct runtime_code_mapper_config {
    uint8_t type;
    size_t pairs;
    const uint16_t *map;
    bool start_enabled;
};

struct runtime_code_mapper_data {
    atomic_t enabled;
};

static bool runtime_code_mapper_device_valid(const struct device *dev);

int runtime_code_mapper_get_enabled(const struct device *dev, bool *out) {
    if (dev == NULL || out == NULL) {
        return -EINVAL;
    }
    if (!runtime_code_mapper_device_valid(dev)) {
        return -ENODEV;
    }

    struct runtime_code_mapper_data *data = dev->data;

    *out = atomic_get(&data->enabled) != 0;

    return 0;
}

int runtime_code_mapper_set_enabled(const struct device *dev, bool enabled) {
    if (dev == NULL) {
        return -EINVAL;
    }
    if (!runtime_code_mapper_device_valid(dev)) {
        return -ENODEV;
    }

    struct runtime_code_mapper_data *data = dev->data;

    atomic_set(&data->enabled, enabled ? 1 : 0);

    LOG_DBG("%s: map %s", dev->name, enabled ? "on" : "off");

    return 0;
}

static int runtime_code_mapper_handle_event(const struct device *dev, struct input_event *event,
                                            uint32_t param1, uint32_t param2,
                                            struct zmk_input_processor_state *state) {
    /* The switch is on the node; the chain declares zero cells. */
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    const struct runtime_code_mapper_config *config = dev->config;
    struct runtime_code_mapper_data *data = dev->data;

    if (atomic_get(&data->enabled) == 0 || event->type != config->type) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const uint16_t original = event->code;
    runtime_code_mapper_apply(&event->code, config->map, config->pairs, true);
    if (event->code != original) {
        LOG_DBG("Remapped %d to %d", original, event->code);
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int runtime_code_mapper_init(const struct device *dev) {
    const struct runtime_code_mapper_config *config = dev->config;
    struct runtime_code_mapper_data *data = dev->data;

    atomic_set(&data->enabled, config->start_enabled ? 1 : 0);

    return 0;
}

static const struct zmk_input_processor_driver_api runtime_code_mapper_driver_api = {
    .handle_event = runtime_code_mapper_handle_event,
};

#define RUNTIME_CODE_MAPPER_INST(n)                                                                \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, map) % 2 == 0,                                                \
                 "map is a list of from/to pairs, so it needs an even number of entries");         \
    static const uint16_t runtime_code_mapper_map_##n[] = DT_INST_PROP(n, map);                    \
    static const struct runtime_code_mapper_config runtime_code_mapper_config_##n = {              \
        .type = DT_INST_PROP_OR(n, type, INPUT_EV_REL),                                            \
        .pairs = DT_INST_PROP_LEN(n, map) / 2,                                                     \
        .map = runtime_code_mapper_map_##n,                                                        \
        .start_enabled = !DT_INST_PROP(n, start_disabled),                                         \
    };                                                                                             \
    static struct runtime_code_mapper_data runtime_code_mapper_data_##n;                           \
    DEVICE_DT_INST_DEFINE(n, &runtime_code_mapper_init, NULL, &runtime_code_mapper_data_##n,       \
                          &runtime_code_mapper_config_##n, POST_KERNEL,                            \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &runtime_code_mapper_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_CODE_MAPPER_INST)

#define RUNTIME_CODE_MAPPER_DEVICE_REF(n) DEVICE_DT_INST_GET(n),

/* The runtime API reads dev->data as this driver's data, so it accepts only the
 * devices this driver defined. */
static const struct device *const runtime_code_mapper_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(RUNTIME_CODE_MAPPER_DEVICE_REF)};

static bool runtime_code_mapper_device_valid(const struct device *dev) {
    for (size_t i = 0U; i < ARRAY_SIZE(runtime_code_mapper_devices); i++) {
        if (runtime_code_mapper_devices[i] == dev) {
            return true;
        }
    }

    return false;
}
