#!/usr/bin/env bash
# Replays tests/vectors/pose_adapter_vectors.h (the retail pose.lua's own answers) against src/pose_adapter.c.
# TFP_POSE_ADAPTER_SOURCE / TFP_POSE_ADAPTER_WORK: another C source / build folder (the mutation check uses them).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_POSE_ADAPTER_SOURCE:-$TFP_ROOT/src/pose_adapter.c}"
TFP_WORK="${TFP_POSE_ADAPTER_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_SOURCE" "$TFP_ROOT/tests/pose_adapter_test.c" -lm -o "$TFP_WORK/pose_adapter"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/pose_adapter" "$@"
