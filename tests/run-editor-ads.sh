#!/usr/bin/env bash
# The placement editor opened while aiming (round 20): tests/editor_ads_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
TFP_SOURCES=()
for tfp_unit in placement_editor motion_runtime motion_math action_select locomotion action_state snapshot backend; do
  TFP_SOURCES+=("$TFP_ROOT/src/$tfp_unit.c")
done
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -I "$TFP_ROOT/src" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/editor_ads_test.c" -lm -o "$TFP_ROOT/work/tests/editor_ads"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/editor_ads"
