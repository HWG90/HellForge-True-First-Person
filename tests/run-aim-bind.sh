#!/usr/bin/env bash
# The binder lines of round 20 and 20c (src/aim_bind.h) over the real pose, near and field-of-view helpers:
# tests/aim_bind_test.c (review 28, I2).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -I "$TFP_ROOT/src" "$TFP_ROOT/src/pose_helper.c" "$TFP_ROOT/src/near_helper.c" "$TFP_ROOT/src/fov_helper.c" \
  "$TFP_ROOT/tests/aim_bind_test.c" -lm -o "$TFP_ROOT/work/tests/aim_bind"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/aim_bind"
