#!/usr/bin/env bash
# The feel build's join and the motion binder's adapters with no game: tests/wire_feel_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
TFP_SOURCES=()
for tfp_unit in wire_feel wire_settings tfp_settings action_select wire_pose pose_adapter pose_helper wire_motion motion_runtime motion_math action_state \
    locomotion backend snapshot wire_engine head_visibility model_shadow_source arm_pieces grip_hand wire_effects dof vignette aim_markers \
    first_person_movement native_ads_option wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery transition \
    camera_adapter fov_adapter near_adapter blur_adapter; do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_unit.c"); done
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -I "$TFP_ROOT/src" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/wire_feel_test.c" -lm -o "$TFP_ROOT/work/tests/wire_feel"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/wire_feel"
