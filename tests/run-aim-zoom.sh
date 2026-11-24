#!/usr/bin/env bash
# Round 20: full aim with 'zoom when aiming', an optic's zoom levels and the unzoom gate flipping, on a model of the
# game's side at 60/116/144 frames a second: tests/aim_zoom_test.c (src/fov_lock.h, src/depth_steady.h).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/aim_zoom_test.c" -lm -o "$TFP_ROOT/work/tests/aim_zoom_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/aim_zoom_test"
