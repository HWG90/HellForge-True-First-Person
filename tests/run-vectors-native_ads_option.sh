#!/usr/bin/env bash
# The native ADS option: src/native_ads_option.c replayed against the retail native_ads_option.lua's own answers
# (tests/vectors/native_ads_option_vectors.h, cut by tools/retail_ref/gen_native_ads_option.py).
# Prints one summary line; exits non-zero on any difference. Needs only a C compiler.
# TFP_FX_SOURCE: another C source (the mutation check uses it). TFP_FX_WORK: another build folder.
# TFP_FX_CFLAGS: more compiler flags.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="${TFP_FX_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -g ${TFP_FX_CFLAGS:-} -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "${TFP_FX_SOURCE:-$TFP_ROOT/src/native_ads_option.c}" "$TFP_ROOT/tests/native_ads_option_test.c" -lm -o "$TFP_WORK/native_ads_option"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/native_ads_option" "$@"
