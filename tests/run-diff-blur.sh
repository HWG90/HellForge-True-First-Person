#!/usr/bin/env bash
# Blur helper: the C translation (src/blur_helper.c) against the retail source, compared after every call.
# Needs the retail tree (read only), g++ and Linux (process_vm_readv). Prints one summary line; exits
# non-zero on any difference. One script only: tests/run-diff-blur.sh "<part of its name>"; --list names them.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_RETAIL_NATIVE="${TFP_RETAIL:-$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit}/native/first_person"
if [ ! -f "$TFP_RETAIL_NATIVE/native_ads_blur.cpp" ] || ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then
  echo "Blur helper: differential test skipped (retail source, g++ or Linux not found)"
  exit 0
fi
TFP_OUT="${TFP_BLUR_OUT:-$TFP_ROOT/work/tests}"
TFP_SOURCE="${TFP_BLUR_SOURCE:-$TFP_ROOT/src/blur_helper.c}"   # a mutation run passes a scratch copy
mkdir -p "$TFP_OUT"
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" -c "$TFP_SOURCE" \
  -o "$TFP_OUT/blur_helper_under_test.o"
if [ ! -f "$TFP_OUT/diff_blur_helper.o" ] || [ -z "${TFP_BLUR_REUSE:-}" ]; then
  g++ -std=c++17 -O1 -ffp-contract=off -w -fpermissive -I "$TFP_ROOT/tests/winshim" -I "$TFP_RETAIL_NATIVE" -I "$TFP_ROOT/src" \
    -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/diff_blur_helper.cpp" -o "$TFP_OUT/diff_blur_helper.o"
fi
g++ "$TFP_OUT/diff_blur_helper.o" "$TFP_OUT/blur_helper_under_test.o" -lm -lpthread -o "$TFP_OUT/diff_blur_helper"
"$TFP_OUT/diff_blur_helper" "$@" | tail -n 2
exit "${PIPESTATUS[0]}"
