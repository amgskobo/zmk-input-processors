#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Build every runtime case for native_sim, run it, and compare what it printed
# with the case's snapshot, the way ZMK's own tests are checked.
#
# A case is a directory holding native_sim.keymap and native_sim.conf, read by
# ZMK as a user config; events.patterns, a sed script picking the lines that
# matter; and keycode_events.snapshot, the lines expected. The self-tests some
# cases run live in the test module beside them. Cases build in parallel.
#
# The upstream suite points ZMK_TEST_ZMK_APP and ZMK_TEST_BUILD_ROOT at its own
# workspace, and ZMK_TEST_CASES at the cases that need no custom settings.

set -euo pipefail

cases_dir=/src/tests/integration/runtime
app_dir="$ZMK_TEST_ZMK_APP"
build_root="${ZMK_TEST_BUILD_ROOT:-$ZMK_TEST_WORK_DIR/build/runtime}"

run_case() {
    local case_dir="$1"
    local name
    local build_dir
    local status
    local boots=1
    local boot
    local flash=()

    name="$(basename "$case_dir")"
    build_dir="$build_root/$name"

    rm -rf "$build_dir"
    mkdir -p "$build_dir"

    if ! west build -s "$app_dir" -d "$build_dir" -b native_sim//zmk_test_mock \
        -- -DCONFIG_ASSERT=y -DZMK_CONFIG="$case_dir" \
        -DZMK_EXTRA_MODULES="/src;$cases_dir/module" >"$build_dir.build.log" 2>&1; then
        echo "FAILED: $name did not build"
        tail -n 40 "$build_dir.build.log"
        return 1
    fi

    # A case with a boots file runs that many times against one flash file, so
    # that what one boot stores, the next one loads.
    if [ -f "$case_dir/boots" ]; then
        boots="$(cat "$case_dir/boots")"
        flash=(--flash="$build_dir/flash.bin")
    fi

    status=0
    : >"$build_dir.run.log"
    for ((boot = 1; boot <= boots; boot++)); do
        timeout 300 "$build_dir/zephyr/zmk.exe" ${flash[@]+"${flash[@]}"} \
            >>"$build_dir.run.log" 2>&1 || status=$?
        if [ "$status" -ne 0 ]; then
            break
        fi
    done

    sed -e 's/.*> //' "$build_dir.run.log" | sed -n -f "$case_dir/events.patterns" \
        >"$build_dir.events.log"

    if ! diff -auZ "$case_dir/keycode_events.snapshot" "$build_dir.events.log"; then
        echo "FAILED: $name printed something other than its snapshot"
        return 1
    fi

    if [ "$status" -ne 0 ]; then
        echo "FAILED: $name exited with status $status"
        tail -n 40 "$build_dir.run.log"
        return 1
    fi

    echo "PASS: $name"
}

mkdir -p "$build_root"
pids=()
names=()

for keymap in "$cases_dir"/*/native_sim.keymap; do
    case_dir="$(dirname "$keymap")"
    name="$(basename "$case_dir")"

    if [ -n "${ZMK_TEST_CASES:-}" ] && [[ " $ZMK_TEST_CASES " != *" $name "* ]]; then
        continue
    fi

    names+=("$name")
    run_case "$case_dir" >"$build_root/$name.result" 2>&1 &
    pids+=("$!")
done

status=0

for i in "${!pids[@]}"; do
    if ! wait "${pids[$i]}"; then
        status=1
    fi
    cat "$build_root/${names[$i]}.result"
done

exit "$status"
