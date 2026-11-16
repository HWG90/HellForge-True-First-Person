#!/usr/bin/env bash
# Locomotion: the C translation (src/locomotion.c) replayed against the retail locomotion.lua's own
# answers (tests/vectors/locomotion_vectors.h, cut by tools/retail_ref/gen_locomotion.py): the pure
# functions bit for bit, the sampler on the same scenes in retail's memory layout (Lua) and build
# 25480438's (C). Two builds of the C file are run: with its two open sprint tests switched on
# (everything must equal retail) and as shipped (the open tests answer "no").
# Prints one summary line per build; exits non-zero on any difference. Needs only a C compiler.
#   tests/run-vectors-locomotion.sh [retail commit the vectors must come from]
# TFP_LOCOMOTION_SOURCE / TFP_LOCOMOTION_WORK: another C source / build folder (the mutation check
# uses them). TFP_LOCOMOTION_KEEP_TEST_OBJECT: reuse the compiled test and support objects.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_LOCOMOTION_SOURCE:-$TFP_ROOT/src/locomotion.c}"
TFP_WORK="${TFP_LOCOMOTION_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
TFP_FLAGS=(-std=c11 -Wall -Wextra -Werror -I "$TFP_ROOT/src")
"$TFP_CC" "${TFP_FLAGS[@]}" -O1 -ffp-contract=off -DTFP_LOCOMOTION_TEST_OPEN_CONSTANTS -c "$TFP_SOURCE" -o "$TFP_WORK/locomotion_open_under_test.o"
"$TFP_CC" "${TFP_FLAGS[@]}" -O1 -ffp-contract=off -c "$TFP_SOURCE" -o "$TFP_WORK/locomotion_under_test.o"
if [ ! -f "$TFP_WORK/locomotion_test.o" ] || [ -z "${TFP_LOCOMOTION_KEEP_TEST_OBJECT:-}" ]; then
  "$TFP_CC" "${TFP_FLAGS[@]}" -O0 -I "$TFP_ROOT/tests" -DLOCOMOTION_OPEN_CONSTANTS -c "$TFP_ROOT/tests/locomotion_test.c" -o "$TFP_WORK/locomotion_open_test.o"
  "$TFP_CC" "${TFP_FLAGS[@]}" -O0 -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/locomotion_test.c" -o "$TFP_WORK/locomotion_test.o"
  for tfp_part in action_state snapshot backend; do
    "$TFP_CC" "${TFP_FLAGS[@]}" -O1 -ffp-contract=off -c "$TFP_ROOT/src/$tfp_part.c" -o "$TFP_WORK/locomotion_$tfp_part.o"
  done
fi
TFP_PARTS=("$TFP_WORK/locomotion_action_state.o" "$TFP_WORK/locomotion_snapshot.o" "$TFP_WORK/locomotion_backend.o")
"$TFP_CC" "$TFP_WORK/locomotion_open_test.o" "$TFP_WORK/locomotion_open_under_test.o" "${TFP_PARTS[@]}" -lm -o "$TFP_WORK/locomotion_open_test"
"$TFP_CC" "$TFP_WORK/locomotion_test.o" "$TFP_WORK/locomotion_under_test.o" "${TFP_PARTS[@]}" -lm -o "$TFP_WORK/locomotion_test"
"$TFP_WORK/locomotion_open_test" "$@"
"$TFP_WORK/locomotion_test" "$@"
