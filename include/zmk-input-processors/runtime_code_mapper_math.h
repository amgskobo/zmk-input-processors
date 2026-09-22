/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline void runtime_code_mapper_apply(uint16_t *code, const uint16_t *map, size_t pairs,
                                             bool enabled) {
    if (!enabled) {
        return;
    }

    for (size_t i = 0; i < pairs; i++) {
        if (map[i * 2] == *code) {
            *code = map[(i * 2) + 1];
            return;
        }
    }
}
