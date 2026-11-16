#!/usr/bin/env bash
# Field-of-view helper: the C translation (src/fov_helper.c) against the retail source, compared after every
# call (tens of thousands of child processes over all cores). Needs the retail tree (read only), g++ and Linux
# (process_vm_readv). Prints one summary line; exits non-zero on any difference.
#   tests/run-diff-fov.sh ["part of a script's name" | --list]
#   TFP_RETAIL=<kit folder>   the retail kit (default ~/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit)
#   TFP_FOV_C=<file>          another C file to test (mutation runs); TFP_FOV_WORK=<dir> where to build
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_RETAIL_NATIVE="${TFP_RETAIL:-$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit}/native/first_person"
TFP_WORK="${TFP_FOV_WORK:-$TFP_ROOT/work/tests}"
TFP_C="${TFP_FOV_C:-$TFP_ROOT/src/fov_helper.c}"
if [ ! -f "$TFP_RETAIL_NATIVE/native_ads_fov.cpp" ] || ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then
  echo "FOV helper: differential test skipped (retail source, g++ or Linux not found)"
  exit 0
fi
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" -c "$TFP_C" -o "$TFP_WORK/fov_helper_under_test.o"
if [ ! -f "$TFP_WORK/diff_fov_helper.o" ] || [ -n "$(find "$TFP_ROOT/tests/diff_fov_helper.cpp" "$TFP_ROOT/tests/diff_fov_helper_world.h" \
     "$TFP_ROOT/tests/diff_fov_helper_scripts.h" "$TFP_ROOT/tests/winshim" "$TFP_ROOT/src/fov_helper.h" "$TFP_RETAIL_NATIVE/native_ads_fov.cpp" \
     "$TFP_RETAIL_NATIVE/native_ads_fov.h" "$TFP_RETAIL_NATIVE/native_memory.h" -newer "$TFP_WORK/diff_fov_helper.o" -print -quit)" ]; then
  g++ -std=c++17 -O1 -ffp-contract=off -w -fpermissive -I "$TFP_ROOT/tests/winshim" -I "$TFP_RETAIL_NATIVE" -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
    -c "$TFP_ROOT/tests/diff_fov_helper.cpp" -o "$TFP_WORK/diff_fov_helper.o"
fi
g++ "$TFP_WORK/diff_fov_helper.o" "$TFP_WORK/fov_helper_under_test.o" -lm -lpthread -o "$TFP_WORK/diff_fov_helper"
TFP_OUT="$("$TFP_WORK/diff_fov_helper" "$@")" && TFP_STATUS=0 || TFP_STATUS=$?
if [ "${1:-}" = --list ]; then printf '%s\n' "$TFP_OUT"; exit "$TFP_STATUS"; fi
printf '%s\n' "$TFP_OUT" | grep -v '^FOV helper:' || true
printf '%s\n' "$TFP_OUT" | grep '^FOV helper:' | tr '\n' ' ' | sed 's/ $/\n/'
exit "$TFP_STATUS"
