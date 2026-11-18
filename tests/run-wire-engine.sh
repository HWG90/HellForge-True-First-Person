#!/usr/bin/env bash
# The engine binder of the feel build (src/wire_engine.c, src/wire_win_engine.c over src/engine.c):
#  1. tests/wire_engine_test.c: the bridge's marshalling against a small stand-in for the Lua C API, the handle
#     table, the function identities, every in-game check passing and failing, the runtime adapters;
#  2. tests/wire_recent_test.c: the binder's memory of the snapshots it handed out (src/wire_recent.h);
#  3. the arm modules' vectors replayed over the binder's value maths (tests/wire_engine_maths.h).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_HF="${HF:-$HOME/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge}"
TFP_WORK="$TFP_ROOT/work/tests"
mkdir -p "$TFP_WORK"
TFP_FLAGS=(-std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests")
TFP_SOURCES=()
for tfp_unit in wire_engine wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery transition camera_adapter \
    fov_adapter near_adapter blur_adapter head_visibility model_shadow_source arm_pieces grip_hand; do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_unit.c"); done
# engine.c and wire_win_engine.c are compiled into the test itself, over tests/wire_engine_host/windows.h.
"$TFP_CC" "${TFP_FLAGS[@]}" -I "$TFP_ROOT/tests/wire_engine_host" -I "$TFP_HF/app/sdk" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/wire_engine_test.c" \
  -lm -o "$TFP_WORK/wire_engine"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/wire_engine"
"$TFP_CC" "${TFP_FLAGS[@]}" "$TFP_ROOT/tests/wire_recent_test.c" "$TFP_ROOT/src/model_shadow_source.c" "$TFP_ROOT/src/backend_ops.c" -lm -o "$TFP_WORK/wire_recent"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/wire_recent"
for tfp_test in pieces grip; do
  "$TFP_CC" "${TFP_FLAGS[@]}" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/wire_engine_maths_$tfp_test.c" -lm -o "$TFP_WORK/wire_engine_maths_$tfp_test"
  ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/wire_engine_maths_$tfp_test" | sed 's/^/Binder value maths: /'
done
