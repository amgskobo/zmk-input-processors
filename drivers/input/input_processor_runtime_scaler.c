/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Overflow-safe, runtime-adjustable replacement for ZMK's stock scaler.
 *
 * Two differences from the stock processor, and nothing else:
 *
 * 1. The arithmetic is done in int64_t, and lives in runtime_scaler_math.h so a
 *    host test can pin it. The stock scaler holds `event->value * multiplier`
 *    in an int16_t, so a multiplier of 889 turns any delta of 37 or more
 *    negative before it is divided -- the pointer reverses exactly when it is
 *    moving fastest. A runtime scaler that copies the expression inherits the
 *    same reversal.
 *
 * 2. The ratio lives on the node instead of in the chain's parameter cells,
 *    and in RAM rather than rodata, so it can be changed while the keyboard
 *    is running. The event path is otherwise the stock one: match the type,
 *    match the code, scale, keep the remainder.
 */

#define DT_DRV_COMPAT zmk_input_processor_runtime_scaler

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <drivers/input_processor.h>

#include <zmk-input-processors/runtime_scaler.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct runtime_scaler_config {
    uint8_t type;
    size_t codes_len;
    const uint16_t *codes;
    uint32_t multiplier;
    uint32_t divisor;
};

struct runtime_scaler_data {
    atomic_t ratio;
};

static atomic_val_t encode_ratio(uint32_t multiplier, uint32_t divisor) {
    return (atomic_val_t)((multiplier << 16) | divisor);
}

static void decode_ratio(atomic_val_t encoded, uint32_t *multiplier, uint32_t *divisor) {
    const uint32_t ratio = (uint32_t)encoded;

    *multiplier = ratio >> 16;
    *divisor = ratio & UINT16_MAX;
}

static bool runtime_scaler_device_valid(const struct device *dev);

int runtime_scaler_get_params(const struct device *dev, uint32_t *multiplier, uint32_t *divisor) {
    if (dev == NULL || multiplier == NULL || divisor == NULL) {
        return -EINVAL;
    }
    if (!runtime_scaler_device_valid(dev)) {
        return -ENODEV;
    }

    struct runtime_scaler_data *data = dev->data;

    decode_ratio(atomic_get(&data->ratio), multiplier, divisor);

    return 0;
}

int runtime_scaler_set_params(const struct device *dev, uint32_t multiplier, uint32_t divisor) {
    if (dev == NULL) {
        return -EINVAL;
    }
    if (!runtime_scaler_device_valid(dev)) {
        return -ENODEV;
    }

    if (!runtime_scaler_params_valid(multiplier, divisor)) {
        LOG_WRN("%s: rejected ratio %u/%u", dev->name, multiplier, divisor);
        return -EINVAL;
    }

    struct runtime_scaler_data *data = dev->data;

    /* Both numbers move in one atomic word, so readers never see a mixed ratio. */
    atomic_set(&data->ratio, encode_ratio(multiplier, divisor));

    LOG_DBG("%s: ratio now %u/%u", dev->name, multiplier, divisor);

    return 0;
}

static int scale_value(struct input_event *event, uint32_t multiplier, uint32_t divisor,
                       struct zmk_input_processor_state *state) {
    const int32_t original = event->value;

    event->value = runtime_scaler_scale(original, multiplier, divisor,
                                        state != NULL ? state->remainder : NULL);

    LOG_DBG("scaled %d by %u/%u to %d", original, multiplier, divisor, event->value);

    return ZMK_INPUT_PROC_CONTINUE;
}

static int runtime_scaler_handle_event(const struct device *dev, struct input_event *event,
                                       uint32_t param1, uint32_t param2,
                                       struct zmk_input_processor_state *state) {
    /* The ratio is on the node; the chain declares zero cells. */
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);

    const struct runtime_scaler_config *config = dev->config;
    struct runtime_scaler_data *data = dev->data;

    if (event->type != config->type) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    for (size_t i = 0; i < config->codes_len; i++) {
        if (config->codes[i] == event->code) {
            uint32_t multiplier;
            uint32_t divisor;

            decode_ratio(atomic_get(&data->ratio), &multiplier, &divisor);

            return scale_value(event, multiplier, divisor, state);
        }
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int runtime_scaler_init(const struct device *dev) {
    const struct runtime_scaler_config *config = dev->config;
    struct runtime_scaler_data *data = dev->data;

    atomic_set(&data->ratio, encode_ratio(config->multiplier, config->divisor));

    return 0;
}

static const struct zmk_input_processor_driver_api runtime_scaler_driver_api = {
    .handle_event = runtime_scaler_handle_event,
};

#define RUNTIME_SCALER_INST(n)                                                                     \
    BUILD_ASSERT(                                                                                  \
        RUNTIME_SCALER_DT_PARAMS_VALID(DT_INST_PROP(n, multiplier), DT_INST_PROP(n, divisor)),     \
        "multiplier must be 0-32767 and divisor 1-32767");                                         \
    static const uint16_t runtime_scaler_codes_##n[] = DT_INST_PROP(n, codes);                     \
    static const struct runtime_scaler_config runtime_scaler_config_##n = {                        \
        .type = DT_INST_PROP_OR(n, type, INPUT_EV_REL),                                            \
        .codes_len = DT_INST_PROP_LEN(n, codes),                                                   \
        .codes = runtime_scaler_codes_##n,                                                         \
        .multiplier = DT_INST_PROP(n, multiplier),                                                 \
        .divisor = DT_INST_PROP(n, divisor),                                                       \
    };                                                                                             \
    static struct runtime_scaler_data runtime_scaler_data_##n;                                     \
    DEVICE_DT_INST_DEFINE(n, &runtime_scaler_init, NULL, &runtime_scaler_data_##n,                 \
                          &runtime_scaler_config_##n, POST_KERNEL,                                 \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &runtime_scaler_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RUNTIME_SCALER_INST)

#define RUNTIME_SCALER_DEVICE_REF(n) DEVICE_DT_INST_GET(n),

/* The runtime API reads dev->data as this driver's data, so it accepts only the
 * devices this driver defined. */
static const struct device *const runtime_scaler_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(RUNTIME_SCALER_DEVICE_REF)};

static bool runtime_scaler_device_valid(const struct device *dev) {
    for (size_t i = 0U; i < ARRAY_SIZE(runtime_scaler_devices); i++) {
        if (runtime_scaler_devices[i] == dev) {
            return true;
        }
    }

    return false;
}
