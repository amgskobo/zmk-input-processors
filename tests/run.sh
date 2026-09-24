#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Run the dependency-free contract tests for every runtime processor.
#
# Each test is built three ways and run each time: optimised, under
# AddressSanitizer and UndefinedBehaviorSanitizer, and as a 32-bit program
# like the firmware it models. The headers under test must also compile on
# their own, with nothing from Zephyr, under stricter warnings than the tests.
#
# Compiler output belongs in a temporary directory, never beside the source.
# run-docker.sh is the reproducible entry point for hosts and CI.

set -euo pipefail

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-input-processors-test.XXXXXX")"

cleanup() {
    rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

warnings=(-std=c11 -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes
    -Wmissing-prototypes -Wundef)

# The headers that must stay free of Zephyr for anything here to run. Listed
# rather than globbed, so a Zephyr include creeping into one fails instead of
# quietly dropping it from the check.
host_headers=(
    runtime_code_mapper.h
    runtime_code_mapper_math.h
    runtime_scaler_math.h
    runtime_temp_layer.h
    runtime_temp_layer_policy.h
    runtime_transform.h
    runtime_transform_math.h
)

# -Wconversion is the warning that would have caught the stock scaler's
# int16_t product, so the arithmetic headers are held to it.
for header in "${host_headers[@]}"; do
    printf '#include <zmk-input-processors/%s>\n#include <zmk-input-processors/%s>\n' \
        "$header" "$header" >"$build_dir/header.c"
    cc "${warnings[@]}" -Wconversion -Wsign-conversion -fsyntax-only -I"$repo_root/include" \
        "$build_dir/header.c"
done

echo "headers: ${#host_headers[@]} compile on their own"

variants=(
    "optimised:-O2"
    "sanitized:-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
    "coverage:-O0 --coverage"
    "32-bit:-O2 -m32"
)

# Nothing here allocates, and LeakSanitizer needs ptrace, which containers
# commonly deny.
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=print_stacktrace=1

for source in "$repo_root"/tests/unit/test_*.c; do
    name="$(basename "$source" .c)"

    for variant in "${variants[@]}"; do
        label="${variant%%:*}"
        read -r -a flags <<<"${variant#*:}"

        cc "${warnings[@]}" "${flags[@]}" -I"$repo_root/include" "$source" \
            -o "$build_dir/$name-$label"
        printf '%s (%s): ' "${name#test_}" "$label"
        "$build_dir/$name-$label"
    done
done

coverage_report="$(cd "$build_dir" && gcov -b -c *coverage*.gcno)"
printf '%s\n' "$coverage_report"
for core in runtime_code_mapper_math runtime_scaler_math runtime_temp_layer_policy runtime_transform_math; do
    core_report="$(printf '%s\n' "$coverage_report" | grep -F -A4 "File '$repo_root/include/zmk-input-processors/$core.h'")"
    printf '%s\n' "$core_report" | grep -Fq 'Lines executed:100.00%'
    printf '%s\n' "$core_report" | grep -Fq 'Taken at least once:100.00%'
done

python3 "$repo_root/tests/runtime/run.py"
