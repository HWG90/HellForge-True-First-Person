#!/usr/bin/env bash
# Round 37: an exosuit or vehicle seat under the camera lock: tests/camera_vehicle_lock_test.cpp, through the camera
# helper on the differential test's fake game memory. Needs g++ and Linux (process_vm_readv); no retail source.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
if ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then echo "Camera vehicle: skipped (g++ or Linux not found)"; exit 0; fi
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -c "$TFP_ROOT/src/camera_helper.c" \
  -o "$TFP_ROOT/work/tests/camera_helper_vehicle.o"
g++ -std=c++17 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/tests/camera_vehicle_lock_test.cpp" "$TFP_ROOT/work/tests/camera_helper_vehicle.o" -lm -o "$TFP_ROOT/work/tests/camera_vehicle_lock_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/camera_vehicle_lock_test"
