#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Build devicetree that has to be refused, and check that each case is refused
# for exactly the reasons its expected-errors.txt lists: no fewer and no others.
#
# ZMK compiles with -Wfatal-errors, so a source file stops at its first failed
# assertion. A case therefore breaks at most one node per source file, after
# the shared nodes that sit exactly at each limit, which are checked first and
# have to pass. Only this module's own objects are built, cases in parallel.

set -euo pipefail

cases_dir=/src/tests/integration/guards
build_root="$ZMK_TEST_WORK_DIR/build/guards"

run_case() {
    local case_dir="$1"
    local name
    local build_dir
    local log
    local targets

    name="$(basename "$case_dir")"
    build_dir="$build_root/$name"
    log="$build_root/$name.log"

    rm -rf "$build_dir"
    mkdir -p "$build_dir"

    if ! west build -s "$ZMK_TEST_ZMK_APP" -d "$build_dir" -b native_sim//zmk_test_mock \
        --cmake-only -- -DZMK_CONFIG="$case_dir" -DZMK_EXTRA_MODULES=/src >"$log" 2>&1; then
        echo "FAILED: $name did not configure"
        tail -n 40 "$log"
        return 1
    fi

    mapfile -t targets < <(ninja -C "$build_dir" -t targets all |
        grep -oE '^[^:]*input_processors?_[a-z_]+\.c\.obj')

    if [ ${#targets[@]} -eq 0 ]; then
        echo "FAILED: $name has none of this module's sources to build"
        return 1
    fi

    if ninja -C "$build_dir" -k 0 "${targets[@]}" >>"$log" 2>&1; then
        echo "FAILED: $name built, but it has to be refused"
        return 1
    fi

    # One line per reported error, without the compiler's colour codes.
    sed -E 's/\x1b\[[0-9;]*[mK]//g' "$log" | sed -n 's/^.*: error: //p' | LC_ALL=C sort \
        >"$build_dir/errors.txt"
    grep -v '^#' "$case_dir/expected-errors.txt" | LC_ALL=C sort >"$build_dir/expected.txt"

    if ! diff -u "$build_dir/expected.txt" "$build_dir/errors.txt"; then
        echo "FAILED: $name was refused for other reasons than expected"
        return 1
    fi

    echo "PASS: $name, refused with exactly the expected errors ($(wc -l <"$build_dir/errors.txt"))"
}

mkdir -p "$build_root"
pids=()
names=()

for keymap in "$cases_dir"/*/native_sim.keymap; do
    case_dir="$(dirname "$keymap")"
    names+=("$(basename "$case_dir")")
    run_case "$case_dir" >"$build_root/$(basename "$case_dir").result" 2>&1 &
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
