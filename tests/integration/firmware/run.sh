#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Build the fixture shields for a real board, the way keyboards are built.
#
#   runtime_processors_test         one instance of every processor, with all
#                                   of their settings published
#   runtime_processors_split_left   the central half of a split keyboard, which
#                                   runs the processors and publishes settings
#   runtime_processors_split_right  the peripheral half, sharing that
#                                   devicetree: it has to build without any
#                                   settings or the central-only temp layer
#
# A string in the image shows that a settings descriptor was linked, not that
# it works; the runtime suite is what exercises them. Shields build in parallel.

set -euo pipefail

build_root="$ZMK_TEST_WORK_DIR/build/firmware"

build() {
    local shield="$1"
    local build_dir="$build_root/$shield"

    rm -rf "$build_dir"
    mkdir -p "$build_dir"

    if ! west build -s "$ZMK_TEST_ZMK_APP" -d "$build_dir" -b xiao_ble/nrf52840/zmk -- \
        -DZMK_EXTRA_MODULES="/src;/src/tests/integration/firmware" \
        -DSHIELD="$shield" >"$build_dir.log" 2>&1; then
        echo "FAILED: $shield did not build"
        tail -n 40 "$build_dir.log"
        return 1
    fi

    if [ ! -f "$build_dir/zephyr/zmk.uf2" ]; then
        echo "FAILED: $shield produced no zmk.uf2"
        return 1
    fi

    strings "$build_dir/zephyr/zmk.elf" >"$build_dir/strings.txt"
}

expect_strings() {
    local shield="$1"
    local expected

    shift
    for expected in "$@"; do
        if ! grep -Fxq "$expected" "$build_root/$shield/strings.txt"; then
            echo "FAILED: $shield: zmk.elf does not contain \"$expected\""
            return 1
        fi
    done
}

single() {
    local shield=runtime_processors_test

    build "$shield" || return 1
    expect_strings "$shield" amgskobo__rip \
        rt_test_scale.multiplier rt_test_scale.divisor \
        rt_test_xform.xy_swap rt_test_xform.x_invert rt_test_xform.y_invert \
        rt_test_map.enabled \
        rt_test_layer.enabled rt_test_layer.layer rt_test_layer.timeout_ms \
        rt_test_layer.prior_idle_ms || return 1
    echo "PASS: $shield, with every processor and every settings key"
}

central() {
    local shield=runtime_processors_split_left

    build "$shield" || return 1
    expect_strings "$shield" amgskobo__rip \
        rt_split_scale.multiplier rt_split_xform.xy_swap rt_split_map.enabled \
        rt_split_layer.prior_idle_ms || return 1
    echo "PASS: $shield, the central half, with its settings"
}

peripheral() {
    local shield=runtime_processors_split_right

    build "$shield" || return 1
    if grep -Fq amgskobo__rip "$build_root/$shield/strings.txt"; then
        echo "FAILED: $shield publishes settings, which only the central half may"
        return 1
    fi
    if grep -q '^CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_TEMP_LAYER=y' \
        "$build_root/$shield/zephyr/.config"; then
        echo "FAILED: $shield builds the temp layer, which is central-only"
        return 1
    fi
    echo "PASS: $shield, the peripheral half, without settings or the temp layer"
}

mkdir -p "$build_root"
names=(single central peripheral)
pids=()

single >"$build_root/single.result" 2>&1 &
pids+=("$!")
central >"$build_root/central.result" 2>&1 &
pids+=("$!")
peripheral >"$build_root/peripheral.result" 2>&1 &
pids+=("$!")

status=0

for i in "${!pids[@]}"; do
    if ! wait "${pids[$i]}"; then
        status=1
    fi
    cat "$build_root/${names[$i]}.result"
done

exit "$status"
