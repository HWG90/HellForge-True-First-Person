#!/usr/bin/env bash
# The veil: the head camera's eye kept in front of the head in the frames the game draws the body at a leave of the
# native view (src/camera_helper.h tfp_camera_set_veil), through the camera helper on the fake game memory of
# tests/camera_smooth_test.cpp (tests/camera_veil_helper_test.cpp; needs g++ and Linux, as tests/run-camera-smooth.sh).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
if ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then echo "Camera veil through the helper: skipped (g++ or Linux not found)"; exit 0; fi
"${CC:-cc}" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -c "$TFP_ROOT/src/camera_helper.c" \
  -o "$TFP_ROOT/work/tests/camera_helper_veil.o"
g++ -std=c++17 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -fno-var-tracking-assignments -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/tests/camera_veil_helper_test.cpp" "$TFP_ROOT/work/tests/camera_helper_veil.o" -lm -o "$TFP_ROOT/work/tests/camera_veil_helper_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/camera_veil_helper_test"
