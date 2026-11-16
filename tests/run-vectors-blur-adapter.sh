#!/usr/bin/env bash
# Replays tests/vectors/blur_adapter_vectors.h (the retail blur.lua's own answers) against src/blur_adapter.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/src/blur_adapter.c" "$TFP_ROOT/tests/blur_adapter_test.c" -lm -o "$TFP_ROOT/work/tests/blur_adapter"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/blur_adapter" "$@"
