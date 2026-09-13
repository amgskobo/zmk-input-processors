#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Run the module-owned ZMK fixtures in an isolated Docker workspace.
#
# Arguments name the suites to run (firmware, guards, runtime); none runs all.
# ZMK_TEST_WORKSPACE_VOLUME names a Docker volume to keep the west workspace
# in between runs, so that a rerun updates it rather than fetching it again.

set -euo pipefail

# Git Bash rewrites arguments that look like POSIX paths before Docker sees
# them, which turns the container's /src into a path under the Git install.
export MSYS_NO_PATHCONV=1

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
image="${ZMK_TEST_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
workspace=()

if [ -n "${ZMK_TEST_WORKSPACE_VOLUME:-}" ]; then
    workspace=(--volume "$ZMK_TEST_WORKSPACE_VOLUME:/workspace" --env ZMK_TEST_WORKSPACE=/workspace)
fi

docker run --rm \
  --volume "$repo_root:/src:ro" \
  ${workspace[@]+"${workspace[@]}"} \
  "$image" \
  /bin/bash /src/tests/integration/run.sh "$@"
