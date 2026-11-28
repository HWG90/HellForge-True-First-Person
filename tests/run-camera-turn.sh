#!/usr/bin/env bash
# Round 49: the direction shown across a change of camera (src/camera_turn.h).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" "$TFP_ROOT/tests/camera_turn_test.c" -lm -o "$TFP_ROOT/work/tests/camera_turn_test"
"$TFP_ROOT/work/tests/camera_turn_test"
