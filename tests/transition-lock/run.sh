#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo; SPDX-License-Identifier: MIT
set -euo pipefail
if [ "$#" -ne 1 ]; then echo "pass one resolved west workspace" >&2; exit 2; fi
workspace="$(cd "$1" && pwd)"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
output="${ZMK_TEST_OUTPUT:-$workspace/build/processors-transition-lock}"
if [ -e "$output" ] || [ -L "$output" ] || [ -e "$output.build.log" ]; then
  echo "output already exists: $output" >&2
  exit 2
fi
mkdir -p "$output"
cd "$workspace"
west zephyr-export
west build -p=always -o=-j2 -s "$workspace/zmk/app" -d "$output/build" \
  -b native_sim//zmk_test_mock -- -DCONFIG_ASSERT=y \
  -DZMK_CONFIG="$repo/tests/transition-lock/config" \
  -DZMK_EXTRA_MODULES="$repo;$repo/tests/transition-lock/module" \
  >"$output/build.log" 2>&1
failures=0
for run in 1 2 3; do
  status=0
  timeout 5 "$output/build/zephyr/zmk.exe" >"$output/run-$run.log" 2>&1 || status=$?
  if [ "$status" -ne 0 ] || ! grep -Fq \
      'PASS: runtime temp-layer cross-instance transitions and ownership' "$output/run-$run.log"; then
    failures=$((failures + 1))
    echo "FAIL: transition lock run $run (exit $status)"
  else echo "PASS: transition lock run $run"; fi
  cat "$output/run-$run.log"
done
test "$failures" -eq 0
