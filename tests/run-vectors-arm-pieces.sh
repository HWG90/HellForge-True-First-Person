#!/usr/bin/env bash
# Replays tests/vectors/arm_pieces_vectors.h (the retail arm_pieces.lua's own answers and engine calls) against src/arm_pieces.c.
# TFP_ARM_PIECES_SOURCE / TFP_ARM_PIECES_WORK: another C source / build folder (the mutation check uses them).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_ARM_PIECES_SOURCE:-$TFP_ROOT/src/arm_pieces.c}"
TFP_WORK="${TFP_ARM_PIECES_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_SOURCE" "$TFP_ROOT/tests/arm_pieces_test.c" -lm -o "$TFP_WORK/arm_pieces"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/arm_pieces" "$@"
