/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

/* Host boundary only. Native fixtures use the actual Zephyr declaration. */
#ifdef RUNTIME_TIMEOUT_32
typedef uint32_t k_ticks_t;
#else
typedef int64_t k_ticks_t;
#endif
