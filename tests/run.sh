#!/usr/bin/env bash
set -euo pipefail

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -pedantic \
  -Iinclude \
  tests/test_runtime_scaler.c \
  -o tests/test_runtime_scaler

./tests/test_runtime_scaler
rm -f tests/test_runtime_scaler
