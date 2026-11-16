#!/usr/bin/env bash
# Replays tests/vectors/grip_hand_vectors.h (the retail grip_hand.lua's own answers and engine calls) against src/grip_hand.c.
# TFP_GRIP_HAND_SOURCE / TFP_GRIP_HAND_WORK: another C source / build folder (the mutation check uses them).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_GRIP_HAND_SOURCE:-$TFP_ROOT/src/grip_hand.c}"
TFP_WORK="${TFP_GRIP_HAND_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_SOURCE" "$TFP_ROOT/tests/grip_hand_test.c" -lm -o "$TFP_WORK/grip_hand"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/grip_hand" "$@"
