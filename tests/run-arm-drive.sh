#!/usr/bin/env bash
# The arm drive with no game: the graft maths against the retired First Person Controller's own results on
# poses recorded in the game (tests/arm_graft_test.c), and the drive on a fake player, body, rig and weapon
# (tests/arm_drive_test.c). Where the retired tree and numpy exist (read only), the committed fixture is made
# again and must come out the same, and all 120 recorded pose pairs are compared instead of every fourth.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="$TFP_ROOT/work/tests"
TFP_FIXTURE="$TFP_ROOT/tests/reference/arm-graft/paired-poses.bin"
TFP_RETIRED_FPC="${TFP_RETIRED_FPC:-$HOME/Projects/HellForge/Retired/true-first-person-controller-body14-20261118}"
mkdir -p "$TFP_WORK"
TFP_FLAGS=(-std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests")
"$TFP_CC" "${TFP_FLAGS[@]}" "$TFP_ROOT/tests/arm_graft_test.c" -lm -o "$TFP_WORK/arm_graft"
"$TFP_WORK/arm_graft" "$TFP_FIXTURE"
# The maths again as the game build compiles it (optimised, no sanitizer): the same bits.
"$TFP_CC" -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" "$TFP_ROOT/tests/arm_graft_test.c" -lm -o "$TFP_WORK/arm_graft_o2"
"$TFP_WORK/arm_graft_o2" "$TFP_FIXTURE" | sed 's/^/-O2: /'
"$TFP_CC" "${TFP_FLAGS[@]}" -Wno-unused-function "$TFP_ROOT/src/arm_drive.c" "$TFP_ROOT/src/backend.c" "$TFP_ROOT/src/snapshot.c" \
  "$TFP_ROOT/tests/arm_drive_test.c" -lm -o "$TFP_WORK/arm_drive"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/arm_drive" "$TFP_FIXTURE"
if [ -f "$TFP_RETIRED_FPC/work/live-d9c4c484/paired-arm-poses-0.3.34.npz" ] && python3 -c 'import numpy' 2>/dev/null; then
  python3 "$TFP_ROOT/tests/arm_graft_fixture.py" "$TFP_WORK/arm-graft-every4.bin" --every 4 --retired "$TFP_RETIRED_FPC"
  cmp "$TFP_WORK/arm-graft-every4.bin" "$TFP_FIXTURE"
  echo "arm graft: the committed fixture is what the retired functions produce today"
  python3 "$TFP_ROOT/tests/arm_graft_fixture.py" "$TFP_WORK/arm-graft-all.bin" --every 1 --retired "$TFP_RETIRED_FPC"
  "$TFP_WORK/arm_graft_o2" "$TFP_WORK/arm-graft-all.bin" | sed 's/^/all pairs: /'
else
  echo "arm graft: the retired tree's capture or numpy is not here; the committed fixture (every fourth pair) was used alone"
fi
