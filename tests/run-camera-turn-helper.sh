#!/usr/bin/env bash
# Round 49: the direction shown across a change of camera, and the orphaned native frame's eye (src/camera_turn.h),
# through the camera helper on the fake game memory of tests/camera_smooth_test.cpp
# (tests/camera_turn_helper_test.cpp; needs g++ and Linux, as tests/run-camera-smooth.sh).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
if ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then echo "Camera turn through the helper: skipped (g++ or Linux not found)"; exit 0; fi
"${CC:-cc}" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -c "${TFP_CAMERA_HELPER_C:-$TFP_ROOT/src/camera_helper.c}" \
  -o "$TFP_ROOT/work/tests/camera_helper_turn.o"
g++ -std=c++17 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -fno-var-tracking-assignments -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/tests/camera_turn_helper_test.cpp" "$TFP_ROOT/work/tests/camera_helper_turn.o" -lm -o "$TFP_ROOT/work/tests/camera_turn_helper_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/camera_turn_helper_test"
