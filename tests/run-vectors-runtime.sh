#!/usr/bin/env bash
# Runtime order: the C translation (src/tfp_runtime.c) replayed against the retail runtime.lua's
# own answers (tests/vectors/runtime_vectors.h, cut by tools/retail_ref/gen_runtime.py): per
# operation the called function's answer, the ordered trace of every call into the scripted
# collaborators with its arguments, the log lines and the runtime's own state; doubles by their
# bits. Nothing else of the mod is linked in: every collaborator is a scripted fake.
# Prints one summary line; exits non-zero on any difference.
#   tests/run-vectors-runtime.sh [retail commit the vectors must come from]
# TFP_RUNTIME_SOURCE / TFP_RUNTIME_WORK: another C source / build folder (the mutation check uses
# them). TFP_RUNTIME_KEEP_TEST_OBJECT: reuse the compiled test object from TFP_RUNTIME_SHARED.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_RUNTIME_SOURCE:-$TFP_ROOT/src/tfp_runtime.c}"
TFP_WORK="${TFP_RUNTIME_WORK:-$TFP_ROOT/work/tests}"
TFP_SHARED="${TFP_RUNTIME_SHARED:-$TFP_WORK}"
if [ ! -f "$TFP_ROOT/tests/vectors/runtime_vectors.h" ]; then
  echo "Runtime: vector test skipped (tests/vectors/runtime_vectors.h not generated)"
  exit 0
fi
mkdir -p "$TFP_WORK" "$TFP_SHARED"
TFP_FLAGS=(-std=c11 -Wall -Wextra -Werror -I "$TFP_ROOT/src")
if [ ! -f "$TFP_SHARED/tfp_runtime_test.o" ] || [ -z "${TFP_RUNTIME_KEEP_TEST_OBJECT:-}" ]; then
  "$TFP_CC" "${TFP_FLAGS[@]}" -O0 -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/tfp_runtime_test.c" -o "$TFP_SHARED/tfp_runtime_test.o"
fi
"$TFP_CC" "${TFP_FLAGS[@]}" -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all \
  -c "$TFP_SOURCE" -o "$TFP_WORK/tfp_runtime_under_test.o"
"$TFP_CC" -fsanitize=address,undefined "$TFP_SHARED/tfp_runtime_test.o" "$TFP_WORK/tfp_runtime_under_test.o" -lm \
  -o "$TFP_WORK/tfp_runtime_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/tfp_runtime_test" "$@"
