#!/usr/bin/env bash
# The aiming markers: src/aim_markers.c replayed against the retail aim_markers.lua's own answers
# (tests/vectors/aim_markers_vectors.h, cut by tools/retail_ref/gen_aim_markers.py).
# Prints one summary line; exits non-zero on any difference. Needs only a C compiler.
# TFP_FX_SOURCE: another C source (the mutation check uses it). TFP_FX_WORK: another build folder.
# TFP_FX_CFLAGS: more compiler flags. The test object (27 MB of vectors) is rebuilt only when the
# test, the vectors or a header changed.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="${TFP_FX_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
TFP_FLAGS=(-std=c11 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests")
TFP_OBJECT="$TFP_WORK/aim_markers_test.o"
TFP_STALE=1
if [ -f "$TFP_OBJECT" ]; then
  TFP_STALE=0
  for tfp_input in "$TFP_ROOT/tests/aim_markers_test.c" "$TFP_ROOT/tests/vectors/aim_markers_vectors.h" "$TFP_ROOT/tests/effects_fx.h" \
      "$TFP_ROOT/src/aim_markers.h" "${BASH_SOURCE[0]}"; do
    if [ "$tfp_input" -nt "$TFP_OBJECT" ]; then TFP_STALE=1; fi
  done
fi
if [ "$TFP_STALE" = 1 ]; then "$TFP_CC" "${TFP_FLAGS[@]}" -c "$TFP_ROOT/tests/aim_markers_test.c" -o "$TFP_OBJECT"; fi
"$TFP_CC" "${TFP_FLAGS[@]}" ${TFP_FX_CFLAGS:-} "${TFP_FX_SOURCE:-$TFP_ROOT/src/aim_markers.c}" "$TFP_OBJECT" -lm -o "$TFP_WORK/aim_markers"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/aim_markers" "$@"
