#!/usr/bin/env bash
# The pose binder of the feel build (src/wire_pose.c) over the real pose adapter and pose helper:
# tests/wire_pose_test.c. The test file is compiled twice: as C, and as C++ with -DTFP_POSE_WORLD_SHIM for
# the few lines that build the fake game memory of the pose helper's differential test
# (tests/diff_pose_helper_world.h); and the near helper on its own differential test's memory (tests/near_world_shim.cpp),
# stepped frame by frame beside the pose helper (round 20). Needs Linux (process_vm_readv) and g++; without them it says "skipped".
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_CXX="${CXX:-g++}"
if [ "$(uname -s)" != Linux ] || ! command -v "$TFP_CXX" >/dev/null; then
  echo "Wire pose: skipped (Linux and g++ are needed for the fake game memory)"
  exit 0
fi
TFP_WORK="$TFP_ROOT/work/tests/wire_pose"
mkdir -p "$TFP_WORK"
TFP_FLAGS=(-ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests")
TFP_OBJECTS=()
for tfp_unit in wire_pose pose_adapter pose_helper wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery \
    transition camera_adapter fov_adapter near_adapter blur_adapter near_helper; do
  "$TFP_CC" -std=c11 -Wall -Wextra -Werror "${TFP_FLAGS[@]}" -c "$TFP_ROOT/src/$tfp_unit.c" -o "$TFP_WORK/$tfp_unit.o" &
  TFP_OBJECTS+=("$TFP_WORK/$tfp_unit.o")
done
"$TFP_CC" -std=c11 -Wall -Wextra -Werror "${TFP_FLAGS[@]}" -c "$TFP_ROOT/tests/wire_pose_test.c" -o "$TFP_WORK/test.o" &
"$TFP_CXX" -std=c++17 -x c++ -w -DTFP_POSE_WORLD_SHIM "${TFP_FLAGS[@]}" -c "$TFP_ROOT/tests/wire_pose_test.c" -o "$TFP_WORK/world.o" &
"$TFP_CXX" -std=c++17 -Wall -Wextra -Werror "${TFP_FLAGS[@]}" -c "$TFP_ROOT/tests/near_world_shim.cpp" -o "$TFP_WORK/near_world.o" &
tfp_failed=0
for tfp_job in $(jobs -p); do wait "$tfp_job" || tfp_failed=1; done
[ "$tfp_failed" = 0 ] || exit 1
"$TFP_CXX" -fsanitize=address,undefined "${TFP_OBJECTS[@]}" "$TFP_WORK/test.o" "$TFP_WORK/world.o" "$TFP_WORK/near_world.o" -lm -o "$TFP_WORK/wire_pose"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/wire_pose"
