#!/usr/bin/env bash
# The glue of the view build with logic of its own: tests/wire_glue_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
TFP_SOURCES=()
for tfp_unit in wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery transition camera_adapter \
    fov_adapter near_adapter blur_adapter; do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_unit.c"); done
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -I "$TFP_ROOT/src" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/wire_glue_test.c" -lm -o "$TFP_ROOT/work/tests/wire_glue"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/wire_glue"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -g -I "$TFP_ROOT/src" \
  "$TFP_ROOT/src/wire_mouse.c" "$TFP_ROOT/tests/wire_mouse_test.c" -o "$TFP_ROOT/work/tests/wire_mouse"
"$TFP_ROOT/work/tests/wire_mouse"
