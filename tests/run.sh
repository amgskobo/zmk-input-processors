#!/usr/bin/env bash
set -euo pipefail

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -pedantic \
  -Iinclude \
  tests/test_safe_scaler.c \
  -o tests/test_safe_scaler

./tests/test_safe_scaler
rm -f tests/test_safe_scaler
