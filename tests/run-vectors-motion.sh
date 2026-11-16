#!/usr/bin/env bash
# Motion math: the C translation (src/motion_math.c) replayed against the retail motion.lua's
# own answers (tests/vectors/motion_vectors.h, cut by tools/retail_ref/gen_motion.py).
# Prints one summary line; exits non-zero on any difference. Needs only a C compiler.
#   tests/run-vectors-motion.sh [retail commit the vectors must come from]
# TFP_MOTION_SOURCE / TFP_MOTION_WORK: another C source / build folder (the mutation check
# uses them). TFP_MOTION_KEEP_TEST_OBJECT: reuse the compiled test object.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_MOTION_SOURCE:-$TFP_ROOT/src/motion_math.c}"
TFP_WORK="${TFP_MOTION_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" -c "$TFP_SOURCE" \
  -o "$TFP_WORK/motion_math_under_test.o"
if [ ! -f "$TFP_WORK/motion_test.o" ] || [ -z "${TFP_MOTION_KEEP_TEST_OBJECT:-}" ]; then
  "$TFP_CC" -std=c11 -O0 -Wall -Wextra -Werror -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/motion_test.c" \
    -o "$TFP_WORK/motion_test.o"
fi
"$TFP_CC" "$TFP_WORK/motion_test.o" "$TFP_WORK/motion_math_under_test.o" -lm -o "$TFP_WORK/motion_test"
"$TFP_WORK/motion_test" "$@"
