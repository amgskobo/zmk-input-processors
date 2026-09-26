/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <zmk-input-processors/runtime_scaler_math.h>

typedef int32_t atomic_t;
typedef int32_t atomic_val_t;
struct runtime_scaler_config {
    uint8_t type;
    size_t codes_len;
    const uint16_t *codes;
    uint32_t multiplier, divisor;
};
struct runtime_scaler_data { atomic_t ratio; };
struct device { const void *config; void *data; const char *name; };
struct input_event { uint8_t type; uint16_t code; int32_t value; };
struct zmk_input_processor_state { int16_t *remainder; };
static struct device valid_device;
/* Another instance first, so a lookup has to walk the list. */
static struct device first_device;
static const struct device *const runtime_scaler_devices[] = {&first_device, &valid_device};
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ZMK_INPUT_PROC_CONTINUE 0
#define ARG_UNUSED(x) ((void)(x))
#define LOG_DBG(...) ((void)0)
#define LOG_WRN(...) ((void)0)
static atomic_val_t atomic_get(const atomic_t *value) { return *value; }
static void atomic_set(atomic_t *value, atomic_val_t next) { *value = next; }
static bool runtime_scaler_device_valid(const struct device *dev);

/* DRIVER_FUNCTIONS */

int main(void) {
    static const uint16_t codes[] = {1, 3};
    struct runtime_scaler_config config = {.type = 4, .codes_len = 2,
                                            .codes = codes, .multiplier = 3, .divisor = 2};
    struct runtime_scaler_data data = {0};
    valid_device = (struct device){.config = &config, .data = &data, .name = "test"};
    struct device other_device = {.data = &data};
    uint32_t multiplier = 0, divisor = 0;
    assert(runtime_scaler_get_params(NULL, &multiplier, &divisor) == -EINVAL);
    assert(runtime_scaler_get_params(&valid_device, NULL, &divisor) == -EINVAL);
    assert(runtime_scaler_get_params(&valid_device, &multiplier, NULL) == -EINVAL);
    assert(runtime_scaler_get_params(&other_device, &multiplier, &divisor) == -ENODEV);
    assert(runtime_scaler_set_params(NULL, 1, 1) == -EINVAL);
    assert(runtime_scaler_set_params(&other_device, 1, 1) == -ENODEV);
    assert(runtime_scaler_init(&valid_device) == 0);
    assert(runtime_scaler_get_params(&valid_device, &multiplier, &divisor) == 0);
    assert(multiplier == 3 && divisor == 2);
    assert(runtime_scaler_set_params(&valid_device, 32768, 1) == -EINVAL);
    assert(runtime_scaler_set_params(&valid_device, 1, 0) == -EINVAL);
    assert(runtime_scaler_get_params(&valid_device, &multiplier, &divisor) == 0);
    assert(multiplier == 3 && divisor == 2);

    struct input_event event = {.type = 5, .code = 1, .value = 10};
    assert(runtime_scaler_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(event.value == 10); /* wrong type */
    event.type = 4;
    event.code = 2;
    assert(runtime_scaler_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(event.value == 10); /* unmatched code */
    event.code = 3;
    assert(runtime_scaler_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(event.value == 15); /* second code, no remainder pointer */
    int16_t remainder = 0;
    struct zmk_input_processor_state state = {.remainder = &remainder};
    event.code = 1;
    event.value = 1;
    assert(runtime_scaler_handle_event(&valid_device, &event, 0, 0, &state) == 0);
    assert(event.value == 1 && remainder == 1);
    event.value = 1;
    assert(runtime_scaler_handle_event(&valid_device, &event, 0, 0, &state) == 0);
    assert(event.value == 2 && remainder == 0);
    assert(runtime_scaler_set_params(&valid_device, 0, 1) == 0);
    assert(runtime_scaler_get_params(&valid_device, &multiplier, &divisor) == 0);
    assert(multiplier == 0 && divisor == 1);
    event.value = 7;
    assert(runtime_scaler_handle_event(&valid_device, &event, 0, 0, &state) == 0);
    assert(event.value == 0);
    /* Every device in the list counts, the first as much as the last. */
    assert(runtime_scaler_device_valid(&first_device));
    puts("runtime scaler driver: PASS");
    return 0;
}
