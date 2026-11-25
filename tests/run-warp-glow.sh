#!/usr/bin/env bash
# The Warp Pack module (src/warp_glow.c) on mocked memory (tests/warp_glow_test.c). Needs only a C compiler.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/tests/warp_glow_test.c" "$TFP_ROOT/src/warp_glow.c" -o "$TFP_ROOT/work/tests/warp_glow_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/warp_glow_test"
