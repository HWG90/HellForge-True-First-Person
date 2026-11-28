#!/usr/bin/env bash
# Round 53: the near plane and the gun (src/weapon_clip.h) and the "Clip:" line: tests/weapon_clip_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I "$TFP_ROOT/src" \
  "$TFP_ROOT/tests/weapon_clip_test.c" -lm -o "$TFP_ROOT/work/tests/weapon_clip_test"
"$TFP_ROOT/work/tests/weapon_clip_test"
