#!/usr/bin/env bash
# The glide of the head camera's eye to the incoming weapon's eye: the arithmetic alone (tests/camera_glide_test.c),
# then through the camera helper on the fake game memory of tests/camera_smooth_test.cpp
# (tests/camera_glide_helper_test.cpp; needs g++ and Linux, as tests/run-camera-smooth.sh).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/camera_glide_test.c" -lm -o "$TFP_ROOT/work/tests/camera_glide_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/camera_glide_test"
if ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then echo "Camera glide through the helper: skipped (g++ or Linux not found)"; exit 0; fi
"${CC:-cc}" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -c "$TFP_ROOT/src/camera_helper.c" \
  -o "$TFP_ROOT/work/tests/camera_helper_glide.o"
g++ -std=c++17 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -fno-var-tracking-assignments -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/tests/camera_glide_helper_test.cpp" "$TFP_ROOT/work/tests/camera_helper_glide.o" -lm -o "$TFP_ROOT/work/tests/camera_glide_helper_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/camera_glide_helper_test"
