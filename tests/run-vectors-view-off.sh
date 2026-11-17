#!/usr/bin/env bash
# The "module off" answers of the view build (src/wire_off.c) against retail runtime.lua: see
# tests/wire_off_test.c. Prints two summary lines; exits non-zero on any difference.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="$TFP_ROOT/work/tests"
mkdir -p "$TFP_WORK"
TFP_FLAGS=(-std=c11 -Wall -Wextra -Werror -I "$TFP_ROOT/src")
"$TFP_CC" "${TFP_FLAGS[@]}" -Wno-unused-function -O0 -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/wire_off_test.c" -o "$TFP_WORK/wire_off_test.o"
"$TFP_CC" "${TFP_FLAGS[@]}" -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all \
  -c "$TFP_ROOT/src/tfp_runtime.c" -o "$TFP_WORK/wire_off_runtime.o"
"$TFP_CC" "${TFP_FLAGS[@]}" -g -fsanitize=address,undefined -c "$TFP_ROOT/src/wire_off.c" -o "$TFP_WORK/wire_off_stubs.o"
"$TFP_CC" -fsanitize=address,undefined "$TFP_WORK/wire_off_test.o" "$TFP_WORK/wire_off_runtime.o" "$TFP_WORK/wire_off_stubs.o" -lm \
  -o "$TFP_WORK/wire_off_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/wire_off_test" "$@"
