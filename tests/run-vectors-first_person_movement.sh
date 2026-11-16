#!/usr/bin/env bash
# Focused locomotion (first_person_movement): src/first_person_movement.c replayed against the retail first_person_movement.lua's own answers
# (tests/vectors/first_person_movement_vectors.h, cut by tools/retail_ref/gen_first_person_movement.py).
# Prints one summary line; exits non-zero on any difference. Needs only a C compiler.
# TFP_FX_SOURCE: another C source (the mutation check uses it). TFP_FX_WORK: another build folder.
# TFP_FX_CFLAGS: more compiler flags.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="${TFP_FX_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -g ${TFP_FX_CFLAGS:-} -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "${TFP_FX_SOURCE:-$TFP_ROOT/src/first_person_movement.c}" "$TFP_ROOT/tests/first_person_movement_test.c" -lm -o "$TFP_WORK/first_person_movement"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/first_person_movement" "$@"
