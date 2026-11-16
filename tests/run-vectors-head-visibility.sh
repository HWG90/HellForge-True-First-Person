#!/usr/bin/env bash
# Replays tests/vectors/head_visibility_vectors.h (the retail head_visibility.lua's own answers, with the
# retail model_shadow_source.lua behind it) against src/head_visibility.c and src/model_shadow_source.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/src/head_visibility.c" "$TFP_ROOT/src/model_shadow_source.c" "$TFP_ROOT/tests/head_visibility_test.c" \
  -lm -o "$TFP_ROOT/work/tests/head_visibility"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/head_visibility" "$@"
