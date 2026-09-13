#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Build and run everything in this module that needs ZMK itself.
#
#   firmware  the fixture shield, built for a real board as a keyboard would be
#   guards    devicetree that must fail to build, each for the stated reason
#   runtime   native_sim builds whose output is compared against a snapshot
#   upstream  the runtime cases that need no custom settings, on upstream ZMK
#
# The first three share a workspace on the DYA ZMK fork with
# zmk-feature-custom-settings. Upstream has its own beside it, which borrows
# the first one's clones. Neither depends on a keyboard configuration or on
# another input-processor module. Name suites as arguments to run a subset;
# with none, all of them run.
#
# ZMK_TEST_WORKSPACE names a directory that keeps the workspaces between runs,
# which turns every fetch into an update. Without it they are temporary.

set -euo pipefail

tests_dir=/src/tests/integration
suites=("$@")

if [ ${#suites[@]} -eq 0 ]; then
    suites=(firmware guards runtime upstream)
fi

for suite in "${suites[@]}"; do
    case "$suite" in
    firmware | guards | runtime | upstream) ;;
    *)
        echo "unknown suite \"$suite\": expected firmware, guards, runtime or upstream" >&2
        exit 2
        ;;
    esac
done

if [ -n "${ZMK_TEST_WORKSPACE:-}" ]; then
    work_dir="$ZMK_TEST_WORKSPACE"
    mkdir -p "$work_dir"
else
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-input-processors-integration.XXXXXX")"
    cleanup() {
        rm -rf "$work_dir"
    }
    trap cleanup EXIT HUP INT TERM
fi

# Always set up, even for upstream alone, whose fetch borrows these clones.
dya_dir="$work_dir/dya"
mkdir -p "$dya_dir"
rm -rf "$dya_dir/config"
cp -R "$tests_dir/config" "$dya_dir/config"
cd "$dya_dir"

if [ ! -d .west ]; then
    west init -l config
fi
# The fixture needs ZMK's imported source projects, but not their history or
# tags. West resolves the imported project names only during a plain update,
# so this is intentionally broad but narrow and shallow.
west update --narrow --fetch-opt=--depth=1
# ZMK's app finds Zephyr through the CMake package registry, not ZEPHYR_BASE.
west zephyr-export
export ZEPHYR_BASE="$dya_dir/zephyr"
export ZMK_TEST_WORK_DIR="$work_dir"
export ZMK_TEST_DYA_WORKSPACE="$dya_dir"
export ZMK_TEST_ZMK_APP="$dya_dir/zmk/app"

failed=()

for suite in "${suites[@]}"; do
    echo "=== $suite"
    if ! (cd "$dya_dir" && bash "$tests_dir/$suite/run.sh"); then
        failed+=("$suite")
    fi
done

if [ ${#failed[@]} -gt 0 ]; then
    echo "integration: failed: ${failed[*]}" >&2
    exit 1
fi

echo "integration: passed: ${suites[*]}"
