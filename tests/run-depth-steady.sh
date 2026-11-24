#!/usr/bin/env bash
# The rig's depth held steady against the unzoom gate, the rule alone (src/depth_steady.h): tests/depth_steady_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/depth_steady_test.c" -lm -o "$TFP_ROOT/work/tests/depth_steady_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/depth_steady_test"
