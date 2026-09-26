/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <zmk-input-processors/runtime_code_mapper_math.h>

typedef int atomic_t;
struct runtime_code_mapper_config {
    uint8_t type;
    size_t pairs;
    const uint16_t *map;
    bool start_enabled;
};
struct runtime_code_mapper_data { atomic_t enabled; };
struct device { const void *config; void *data; const char *name; };
struct input_event { uint8_t type; uint16_t code; int32_t value; };
struct zmk_input_processor_state { int unused; };
static struct device valid_device;
/* Another instance first, so a lookup has to walk the list. */
static struct device first_device;
static const struct device *const runtime_code_mapper_devices[] = {&first_device, &valid_device};
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ZMK_INPUT_PROC_CONTINUE 0
#define ARG_UNUSED(x) ((void)(x))
#define LOG_DBG(...) ((void)0)
static int atomic_get(const atomic_t *value) { return *value; }
static void atomic_set(atomic_t *value, int next) { *value = next; }
static bool runtime_code_mapper_device_valid(const struct device *dev);

/* DRIVER_FUNCTIONS */

int main(void) {
    static const uint16_t map[] = {1, 10, 2, 20, 1, 99, 3, 3};
    struct runtime_code_mapper_config config = {.type = 4, .pairs = 4,
                                                 .map = map, .start_enabled = true};
    struct runtime_code_mapper_data data = {0};
    valid_device = (struct device){.config = &config, .data = &data, .name = "test"};
    struct device other_device = {.data = &data};
    bool enabled = false;
    assert(runtime_code_mapper_get_enabled(NULL, &enabled) == -EINVAL);
    assert(runtime_code_mapper_get_enabled(&valid_device, NULL) == -EINVAL);
    assert(runtime_code_mapper_get_enabled(&other_device, &enabled) == -ENODEV);
    assert(runtime_code_mapper_set_enabled(NULL, true) == -EINVAL);
    assert(runtime_code_mapper_set_enabled(&other_device, true) == -ENODEV);
    assert(runtime_code_mapper_init(&valid_device) == 0);
    assert(runtime_code_mapper_get_enabled(&valid_device, &enabled) == 0 && enabled);

    struct input_event event = {.type = 4, .code = 1};
    assert(runtime_code_mapper_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(event.code == 10); /* first match wins */
    event.code = 3;
    assert(runtime_code_mapper_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(event.code == 3); /* self-map */
    event.code = 55;
    assert(runtime_code_mapper_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(event.code == 55); /* no match */
    event.type = 5;
    event.code = 2;
    assert(runtime_code_mapper_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(event.code == 2); /* wrong type */
    assert(runtime_code_mapper_set_enabled(&valid_device, false) == 0);
    assert(runtime_code_mapper_get_enabled(&valid_device, &enabled) == 0 && !enabled);
    event.type = 4;
    assert(runtime_code_mapper_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(event.code == 2); /* disabled */
    assert(runtime_code_mapper_set_enabled(&valid_device, true) == 0);
    assert(runtime_code_mapper_handle_event(&valid_device, &event, 0, 0, NULL) == 0);
    assert(event.code == 20);
    config.start_enabled = false;
    assert(runtime_code_mapper_init(&valid_device) == 0);
    assert(!data.enabled);
    /* Every device in the list counts, the first as much as the last. */
    assert(runtime_code_mapper_device_valid(&first_device));
    puts("runtime code mapper driver: PASS");
    return 0;
}
