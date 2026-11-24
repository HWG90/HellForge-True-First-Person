#!/usr/bin/env bash
# The eye going back and forth, the rule alone (src/camera_alternation.h): tests/camera_alternation_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/camera_alternation_test.c" -lm -o "$TFP_ROOT/work/tests/camera_alternation_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/camera_alternation_test"
