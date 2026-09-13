#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# The runtime cases that need no custom settings, built and run against
# upstream ZMK with zmk-feature-custom-settings absent. The README says the
# processors need nothing but upstream ZMK; this is what checks it.
#
# The workspace borrows the DYA workspace's clones as a cache. Both follow the
# same Zephyr, so little more than ZMK itself is fetched again.

set -euo pipefail

upstream_dir="$ZMK_TEST_WORK_DIR/upstream"

# West falls back on ZEPHYR_BASE to find its workspace, so the value exported
# for the DYA workspace would turn every west command here onto that one.
unset ZEPHYR_BASE

mkdir -p "$upstream_dir/config"
cp /src/tests/integration/upstream/west.yml "$upstream_dir/config/west.yml"
cd "$upstream_dir"

if [ ! -d .west ]; then
    west init -l config
fi
west update --narrow --fetch-opt=--depth=1 --path-cache "$ZMK_TEST_DYA_WORKSPACE"
west zephyr-export

export ZEPHYR_BASE="$upstream_dir/zephyr"
export ZMK_TEST_ZMK_APP="$upstream_dir/zmk/app"
export ZMK_TEST_BUILD_ROOT="$ZMK_TEST_WORK_DIR/build/upstream"
export ZMK_TEST_CASES="drivers chain"

bash /src/tests/integration/runtime/run.sh
