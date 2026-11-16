#!/usr/bin/env bash
# Motion runtime: the C translation (src/motion_runtime.c) replayed against the retail
# motion_runtime.lua's own answers (tests/vectors/motion_runtime_vectors.h, cut by
# tools/retail_ref/gen_motion_runtime.py): per operation the call's answer, the ordered trace of
# every call into the scripted collaborators with its arguments, the log lines and the whole state;
# doubles by their bits. The already-proven motion model, action selection and conversion are
# linked in as they are. Prints one summary line; exits non-zero on any difference.
#   tests/run-vectors-motion-runtime.sh [retail commit the vectors must come from]
# TFP_MOTION_RUNTIME_SOURCE / TFP_MOTION_RUNTIME_WORK: another C source / build folder (the mutation
# check uses them). TFP_MOTION_RUNTIME_KEEP_TEST_OBJECT: reuse the compiled test and support objects.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_MOTION_RUNTIME_SOURCE:-$TFP_ROOT/src/motion_runtime.c}"
TFP_WORK="${TFP_MOTION_RUNTIME_WORK:-$TFP_ROOT/work/tests}"
TFP_SHARED="${TFP_MOTION_RUNTIME_SHARED:-$TFP_WORK}"
mkdir -p "$TFP_WORK" "$TFP_SHARED"
TFP_FLAGS=(-std=c11 -Wall -Wextra -Werror -I "$TFP_ROOT/src")
TFP_PARTS=()
for tfp_part in motion_math action_select locomotion action_state snapshot backend; do
  TFP_PARTS+=("$TFP_SHARED/motion_runtime_$tfp_part.o")
done
if [ ! -f "$TFP_SHARED/motion_runtime_test.o" ] || [ -z "${TFP_MOTION_RUNTIME_KEEP_TEST_OBJECT:-}" ]; then
  "$TFP_CC" "${TFP_FLAGS[@]}" -O0 -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/motion_runtime_test.c" -o "$TFP_SHARED/motion_runtime_test.o"
  for tfp_part in motion_math action_select locomotion action_state snapshot backend; do
    "$TFP_CC" "${TFP_FLAGS[@]}" -O1 -ffp-contract=off -c "$TFP_ROOT/src/$tfp_part.c" -o "$TFP_SHARED/motion_runtime_$tfp_part.o"
  done
fi
"$TFP_CC" "${TFP_FLAGS[@]}" -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -c "$TFP_SOURCE" -o "$TFP_WORK/motion_runtime_under_test.o"
"$TFP_CC" -fsanitize=address,undefined "$TFP_SHARED/motion_runtime_test.o" "$TFP_WORK/motion_runtime_under_test.o" "${TFP_PARTS[@]}" -lm \
  -o "$TFP_WORK/motion_runtime_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/motion_runtime_test" "$@"
