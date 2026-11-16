#!/usr/bin/env bash
# Camera sharing with Photo Mode and ActionCam: src/camera_compat.c replayed against the retail camera_compat.lua's own answers
# (tests/vectors/camera_compat_vectors.h, cut by tools/retail_ref/gen_camera_compat.py).
# Prints one summary line; exits non-zero on any difference. Needs only a C compiler.
# TFP_FX_SOURCE: another C source (the mutation check uses it). TFP_FX_WORK: another build folder.
# TFP_FX_CFLAGS: more compiler flags.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="${TFP_FX_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -g ${TFP_FX_CFLAGS:-} -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "${TFP_FX_SOURCE:-$TFP_ROOT/src/camera_compat.c}" "$TFP_ROOT/tests/camera_compat_test.c" -lm -o "$TFP_WORK/camera_compat"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/camera_compat" "$@"
