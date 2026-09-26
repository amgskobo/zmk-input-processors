/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ZMK_INPUT_PROCESSORS_SUBSYSTEM "amgskobo__rip"

enum zmk_custom_setting_value_type {
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 = 1,
    ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,
};
struct zmk_custom_setting_value {
    enum zmk_custom_setting_value_type type;
    union {
        int32_t int32_value;
        bool bool_value;
    };
};
struct zmk_custom_setting {
    const char *custom_subsystem_id;
    const char *key;
    int read_result;
    struct zmk_custom_setting_value value;
};
typedef struct { int unused; } zmk_custom_CallRequest;
typedef struct { int unused; } pb_callback_t;

#define ARG_UNUSED(x) ((void)(x))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static int errors;
#define LOG_ERR(...) ((void)errors++)

static struct zmk_custom_setting *settings;
static size_t settings_len;
#define ZMK_CUSTOM_SETTING_FOREACH(_var)                                                           \
    for (struct zmk_custom_setting *_var = settings; _var < settings + settings_len; _var++)

static int zmk_custom_setting_read(const struct zmk_custom_setting *setting,
                                   struct zmk_custom_setting_value *value) {
    *value = setting->value;
    return setting->read_result;
}

bool input_processors_read_bool(const struct zmk_custom_setting *setting, bool *out);
bool input_processors_read_uint32(const struct zmk_custom_setting *setting, uint32_t *out);

/* DRIVER_FUNCTIONS */

static struct zmk_custom_setting int32_setting(int32_t value) {
    return (struct zmk_custom_setting){
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32, .int32_value = value}};
}

static struct zmk_custom_setting bool_setting(bool value) {
    return (struct zmk_custom_setting){
        .value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL, .bool_value = value}};
}

static void test_readers(void) {
    bool flag = false;
    uint32_t number = 7;

    struct zmk_custom_setting setting = bool_setting(true);
    assert(input_processors_read_bool(&setting, &flag) && flag);
    setting.read_result = -2;
    flag = false;
    assert(!input_processors_read_bool(&setting, &flag) && !flag);
    setting = int32_setting(1);
    assert(!input_processors_read_bool(&setting, &flag) && !flag);

    setting = int32_setting(0);
    assert(input_processors_read_uint32(&setting, &number) && number == 0);
    setting = int32_setting(INT32_MAX);
    assert(input_processors_read_uint32(&setting, &number) && number == INT32_MAX);
    number = 7;
    setting = int32_setting(-1);
    assert(!input_processors_read_uint32(&setting, &number) && number == 7);
    setting.read_result = -2;
    setting.value.int32_value = 3;
    assert(!input_processors_read_uint32(&setting, &number) && number == 7);
    setting = bool_setting(true);
    assert(!input_processors_read_uint32(&setting, &number) && number == 7);
}

static void test_unique_keys(void) {
    struct zmk_custom_setting list[] = {
        {.custom_subsystem_id = "other", .key = "scale.divisor"},
        {.custom_subsystem_id = ZMK_INPUT_PROCESSORS_SUBSYSTEM, .key = "scale.divisor"},
        {.custom_subsystem_id = "other", .key = "scale.multiplier"},
        {.custom_subsystem_id = ZMK_INPUT_PROCESSORS_SUBSYSTEM, .key = "scale.multiplier"},
        /* Another subsystem reusing a key after ours is not a duplicate. */
        {.custom_subsystem_id = "other", .key = "scale.multiplier"},
        {.custom_subsystem_id = ZMK_INPUT_PROCESSORS_SUBSYSTEM, .key = "scale.divisor"},
    };
    settings = list;

    /* One name clash between our own settings is reported once; the same key
     * under another subsystem is not a clash. */
    settings_len = ARRAY_SIZE(list);
    assert(input_processors_check_unique_keys() == 0);
    assert(errors == 1);

    errors = 0;
    settings_len = ARRAY_SIZE(list) - 1;
    assert(input_processors_check_unique_keys() == 0);
    assert(errors == 0);

    /* Distinct keys never clash, whichever sorts first. */
    struct zmk_custom_setting distinct[] = {
        {.custom_subsystem_id = ZMK_INPUT_PROCESSORS_SUBSYSTEM, .key = "b"},
        {.custom_subsystem_id = ZMK_INPUT_PROCESSORS_SUBSYSTEM, .key = "a"},
        {.custom_subsystem_id = ZMK_INPUT_PROCESSORS_SUBSYSTEM, .key = "c"},
    };
    settings = distinct;
    settings_len = ARRAY_SIZE(distinct);
    assert(input_processors_check_unique_keys() == 0);
    assert(errors == 0);
}

int main(void) {
    assert(!input_processors_namespace_handler(NULL, NULL));
    test_readers();
    test_unique_keys();
    puts("custom settings helpers: PASS");
    return 0;
}
