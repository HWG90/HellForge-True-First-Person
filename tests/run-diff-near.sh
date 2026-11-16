#!/usr/bin/env bash
# Near helper: the C translation (src/near_helper.c) against the retail source
# (native/near_clip/native_ads_near.cpp), compared after every call. Prints one
# summary line; exits non-zero on any difference. Needs the retail tree (read
# only), g++ and Linux (process_vm_readv); without them it says so and exits 0.
#   tests/run-diff-near.sh ["part of a script's name" | --list]
# TFP_RETAIL: the retail kit folder. TFP_NEAR_SOURCE / TFP_NEAR_WORK: another C
# source / build folder (the mutation check uses them).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_RETAIL_KIT="${TFP_RETAIL:-$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit}"
TFP_NEAR_NATIVE="$TFP_RETAIL_KIT/native/near_clip"
TFP_SOURCE="${TFP_NEAR_SOURCE:-$TFP_ROOT/src/near_helper.c}"
TFP_WORK="${TFP_NEAR_WORK:-$TFP_ROOT/work/tests}"
if [ ! -f "$TFP_NEAR_NATIVE/native_ads_near.cpp" ] || ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then
  echo "Near helper: differential test skipped (retail source, g++ or Linux not found)"
  exit 0
fi
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" -c "$TFP_SOURCE" \
  -o "$TFP_WORK/near_helper_under_test.o"
if [ ! -f "$TFP_WORK/diff_near_helper.o" ] || [ -z "${TFP_NEAR_KEEP_TEST_OBJECT:-}" ]; then
  g++ -std=c++17 -O1 -ffp-contract=off -w -fpermissive -I "$TFP_ROOT/tests/winshim" -I "$TFP_NEAR_NATIVE" -I "$TFP_ROOT/src" \
    -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/diff_near_helper.cpp" -o "$TFP_WORK/diff_near_helper.o"
fi
g++ "$TFP_WORK/diff_near_helper.o" "$TFP_WORK/near_helper_under_test.o" -lm -lpthread -o "$TFP_WORK/diff_near_helper"
"$TFP_WORK/diff_near_helper" "$@"
