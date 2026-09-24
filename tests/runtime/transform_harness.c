/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

typedef int atomic_t;
typedef int atomic_val_t;
struct runtime_transform_flags { bool xy_swap, x_invert, y_invert; };
struct runtime_transform_config {
    uint8_t type;
    size_t x_codes_len, y_codes_len;
    const uint16_t *x_codes, *y_codes;
    struct runtime_transform_flags defaults;
};
struct runtime_transform_data { atomic_t flags; };
struct device { const void *config; void *data; const char *name; };
struct input_event { uint8_t type; uint16_t code; int32_t value; };
struct zmk_input_processor_state { int unused; };
static struct device valid_device;
static const struct device *const runtime_transform_devices[] = {&valid_device};
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define BIT(n) (1 << (n))
#define ZMK_INPUT_PROC_CONTINUE 0
#define ARG_UNUSED(x) ((void)(x))
#define LOG_DBG(...) ((void)0)
enum runtime_transform_flag_bit {
    RUNTIME_TRANSFORM_XY_SWAP_BIT,
    RUNTIME_TRANSFORM_X_INVERT_BIT,
    RUNTIME_TRANSFORM_Y_INVERT_BIT,
};
static atomic_val_t atomic_get(const atomic_t *value) { return *value; }
static void atomic_set(atomic_t *value, atomic_val_t next) { *value = next; }
static bool runtime_transform_device_valid(const struct device *dev);
static int applies;
static struct runtime_transform_flags last_flags;
static void runtime_transform_apply(uint16_t *code, int32_t *value,
                                    const struct runtime_transform_flags *flags,
                                    const uint16_t *x_codes, const uint16_t *y_codes,
                                    size_t codes_len) {
    assert(codes_len == 1 && x_codes[0] == 1 && y_codes[0] == 2);
    last_flags = *flags;
    ++applies;
    if (flags->xy_swap && *code == 1) { *code = 2; }
    if (flags->y_invert) { *value = -*value; }
}

/* DRIVER_FUNCTIONS */

int main(void) {
    struct runtime_transform_flags flags = {0};
    assert(encode_flags(&flags) == 0);
    flags.xy_swap = true;
    assert(encode_flags(&flags) == 1);
    flags.xy_swap = false;
    flags.x_invert = true;
    assert(encode_flags(&flags) == 2);
    flags.x_invert = false;
    flags.y_invert = true;
    assert(encode_flags(&flags) == 4);
    flags.xy_swap = flags.x_invert = true;
    assert(encode_flags(&flags) == 7);
    flags = decode_flags(0);
    assert(!flags.xy_swap && !flags.x_invert && !flags.y_invert);
    flags = decode_flags(7);
    assert(flags.xy_swap && flags.x_invert && flags.y_invert);

    static const uint16_t x_codes[] = {1}, y_codes[] = {2};
    struct runtime_transform_config config = {.type = 4, .x_codes_len = 1,
                                               .y_codes_len = 1, .x_codes = x_codes,
                                               .y_codes = y_codes,
                                               .defaults = {.xy_swap = true}};
    struct runtime_transform_data data = {0};
    valid_device = (struct device){.config = &config, .data = &data, .name = "test"};
    struct device other_device = {.data = &data};
    assert(runtime_transform_get_flags(NULL, &flags) == -EINVAL);
    assert(runtime_transform_get_flags(&valid_device, NULL) == -EINVAL);
    assert(runtime_transform_get_flags(&other_device, &flags) == -ENODEV);
    assert(runtime_transform_set_flags(NULL, &flags) == -EINVAL);
    assert(runtime_transform_set_flags(&valid_device, NULL) == -EINVAL);
    assert(runtime_transform_set_flags(&other_device, &flags) == -ENODEV);
    assert(runtime_transform_init(&valid_device) == 0);
    assert(runtime_transform_get_flags(&valid_device, &flags) == 0);
    assert(flags.xy_swap && !flags.x_invert && !flags.y_invert);

    struct input_event event = {.type = 5, .code = 1, .value = 5};
    assert(runtime_transform_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(applies == 0 && event.code == 1 && event.value == 5);
    event.type = 4;
    assert(runtime_transform_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(applies == 1 && event.code == 2 && event.value == 5 && last_flags.xy_swap);
    flags = (struct runtime_transform_flags){.y_invert = true};
    assert(runtime_transform_set_flags(&valid_device, &flags) == 0);
    assert(runtime_transform_get_flags(&valid_device, &flags) == 0);
    assert(!flags.xy_swap && !flags.x_invert && flags.y_invert);
    event = (struct input_event){.type = 4, .code = 2, .value = 6};
    assert(runtime_transform_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(applies == 2 && event.code == 2 && event.value == -6 && last_flags.y_invert);
    puts("runtime transform driver: PASS");
    return 0;
}
