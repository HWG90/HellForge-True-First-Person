#!/usr/bin/env bash
# Round 45: the cockpit frame over the see-through exosuit seat (src/cockpit_frame.h): tests/cockpit_frame_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  -DTFP_SRC="\"$TFP_ROOT/src\"" -DTFP_WORK="\"$TFP_ROOT/work/tests\"" "$TFP_ROOT/tests/cockpit_frame_test.c" -lm -o "$TFP_ROOT/work/tests/cockpit_frame_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/cockpit_frame_test"
