#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
set -euo pipefail
if [ "$#" -ne 1 ]; then echo "pass exactly one resolved west workspace" >&2; exit 2; fi
workspace="$(cd "${1:?pass an already resolved west workspace}" && pwd)"
repo="$(cd "$(dirname "$0")/.." && pwd)"
output="${ZMK_TEST_OUTPUT:-$workspace/build/processors-native}"
if [ -e "$output" ] || [ -L "$output" ] || [ -e "$output.build.log" ]; then
  echo "output already exists: $output" >&2
  exit 2
fi
cd "$workspace"
west zephyr-export
export ZEPHYR_BASE="$workspace/zephyr"
source "$repo/tests/integration/workspace-env.sh"
zmk_test_select_workspace "$workspace"

mkdir -p "$output"
for case in drivers dual_route; do
  config="$repo/tests/integration/runtime/$case"
  build="$output/$case"
  west build -p=always -o=-j2 -s "$workspace/zmk/app" -d "$build" \
    -b native_sim//zmk_test_mock -- -DCONFIG_ASSERT=y -DZMK_CONFIG="$config" \
    -DZMK_EXTRA_MODULES="$repo;$repo/tests/integration/runtime/module" >"$build.build.log" 2>&1
  for run in 1 2 3; do
    timeout 30 "$build/zephyr/zmk.exe" >"$build.run-$run.log" 2>&1
    sed -e 's/.*> //' "$build.run-$run.log" | sed -n -f "$config/events.patterns" >"$build.events-$run.log"
    diff -auZ "$config/keycode_events.snapshot" "$build.events-$run.log"
    cat "$build.events-$run.log"
  done
done
