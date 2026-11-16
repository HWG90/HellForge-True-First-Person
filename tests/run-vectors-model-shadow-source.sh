#!/usr/bin/env bash
# Replays tests/vectors/model_shadow_source_vectors.h (the retail model_shadow_source.lua's own answers)
# against src/model_shadow_source.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/src/model_shadow_source.c" "$TFP_ROOT/tests/model_shadow_source_test.c" -lm -o "$TFP_ROOT/work/tests/model_shadow_source"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/model_shadow_source" "$@"
