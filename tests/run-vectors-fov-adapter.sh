#!/usr/bin/env bash
# Replays tests/vectors/fov_adapter_vectors.h (the retail fov.lua's own answers) against src/fov_adapter.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/src/fov_adapter.c" "$TFP_ROOT/tests/fov_adapter_test.c" -lm -o "$TFP_ROOT/work/tests/fov_adapter"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/fov_adapter" "$@"
