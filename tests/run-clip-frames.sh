#!/usr/bin/env bash
# Round 53: full aim shown wider than the game's lens, through whole frames of the real field-of-view and near helpers
# bound as the live binder binds them (src/aim_bind.h): tests/clip_frames_test.cpp. Needs g++ and Linux
# (process_vm_readv); no retail source.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="$TFP_ROOT/work/tests/clip-frames"
mkdir -p "$TFP_WORK"
if ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then echo "Clip frames: skipped (g++ or Linux not found)"; exit 0; fi
for tfp_unit in near_helper fov_helper pose_helper; do
  "$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -c "$TFP_ROOT/src/$tfp_unit.c" \
    -o "$TFP_WORK/$tfp_unit.o"
done
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -c "$TFP_ROOT/tests/clip_frames_bind.c" \
  -o "$TFP_WORK/clip_frames_bind.o"
g++ -std=c++17 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/tests/clip_frames_test.cpp" "$TFP_ROOT/tests/clip_frames_fov.cpp" "$TFP_WORK/clip_frames_bind.o" "$TFP_WORK/near_helper.o" \
  "$TFP_WORK/fov_helper.o" "$TFP_WORK/pose_helper.o" -lm -o "$TFP_WORK/clip_frames_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/clip_frames_test"
