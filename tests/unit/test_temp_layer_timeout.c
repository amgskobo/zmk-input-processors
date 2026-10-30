/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "runtime_temp_layer_timeout.h"

#ifdef RUNTIME_TIMEOUT_WIDTH_PROBE
k_ticks_t probe_timeout(uint32_t ms, uint32_t hz);
k_ticks_t probe_timeout(uint32_t ms, uint32_t hz) {
    return runtime_temp_layer_timeout_ticks(ms, hz);
}
#else
static void check(uint32_t ms, uint32_t hz) {
    const k_ticks_t expected = (k_ticks_t)(((uint64_t)ms * hz + 999U) / 1000U);
    assert(runtime_temp_layer_timeout_ticks(ms, hz) == expected);
}

int main(void) {
    static const uint32_t rates[] = {
        0, 1, 10, 100, 128, 1000, 1024, 32768, 65536, 71582, 71583,
        1000000, INT32_MAX, UINT32_MAX,
    };
    static const uint32_t remaining[] = {
        60001, 65535, 65536, 131071, 131072, INT32_MAX,
        (uint32_t)INT32_MAX + 1U, UINT32_MAX - 1U, UINT32_MAX,
    };
    for (unsigned h = 0; h < sizeof(rates) / sizeof(rates[0]); ++h) {
        for (uint32_t ms = 0; ms <= RUNTIME_TEMP_LAYER_MAX_MS; ++ms)
            check(ms, rates[h]);
        for (unsigned n = 0; n < sizeof(remaining) / sizeof(remaining[0]); ++n)
            check(remaining[n], rates[h]);
    }
    uint32_t seed = UINT32_C(0x9e3779b9);
    for (unsigned n = 0; n < 65536U; ++n) {
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        for (unsigned h = 0; h < sizeof(rates) / sizeof(rates[0]); ++h)
            check(seed, rates[h]);
    }
    puts("temp-layer timeout every0..60000 wait, fulluint32 remaining,14rates and exact ceil: PASS");
    return 0;
}
#endif
