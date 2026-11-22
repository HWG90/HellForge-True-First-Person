#!/usr/bin/env bash
# The mod's own settings panel and the locked view's options, with no game and no overlay host:
# tests/wire_panel_test.c (a stand-in OvApi records what src/wire_panel.c draws and plays the user).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="$TFP_ROOT/work/tests/wire-panel"
mkdir -p "$TFP_WORK"
TFP_SOURCES=()
for tfp_unit in wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery transition camera_adapter \
    fov_adapter near_adapter blur_adapter action_select tfp_settings wire_settings wire_menu wire_panel wire_options; do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_unit.c"); done
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/wire_panel_test.c" -lm -o "$TFP_WORK/wire_panel_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/wire_panel_test"
