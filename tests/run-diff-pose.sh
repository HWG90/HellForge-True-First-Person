#!/usr/bin/env bash
# Editor/pose helper: the C translation (src/pose_helper.c, src/pose_math.h) against the retail source
# (native/first_person/native_ads_editor.cpp and its arithmetic headers), compared after every call.
# Three programs: the arithmetic headers bit for bit; the helper as shipped (production object, in which the
# look-gain normalisation refuses); and the helper built with the test's values for its open constants
# (tests/diff_pose_helper_open.h), in which the normalisation runs. One summary line each; exits non-zero on
# any difference. Needs the retail tree (read only), g++ and Linux (process_vm_readv); without them it says
# "skipped" and exits 0. The look-gain program also needs the captured images of both builds.
#   tests/run-diff-pose.sh ["part of a script's name" | --list]
# TFP_RETAIL: the retail kit folder. TFP_POSE_SOURCE / TFP_POSE_WORK: another C source / build folder (the
# mutation check uses them). TFP_POSE_KEEP_TEST_OBJECT=1: reuse the compiled test. TFP_POSE_ONLY=production|input.
# TFP_DIFF_STOP=1: stop at the first difference. TFP_DIFF_NO_PROBES=1: the plain scripts only (a quick pass).
# TFP_DIFF_EXTRA_WALKS=n: n more random walks. TFP_INPUT_CAPTURE_DIR / TFP_HERE_IMAGE_DIR: the two builds' images.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_RETAIL_KIT="${TFP_RETAIL:-$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit}"
TFP_NATIVE="$TFP_RETAIL_KIT/native/first_person"
TFP_SOURCE="${TFP_POSE_SOURCE:-$TFP_ROOT/src/pose_helper.c}"
TFP_WORK="${TFP_POSE_WORK:-$TFP_ROOT/work/tests}"
TFP_ONLY="${TFP_POSE_ONLY:-}"
if [ ! -f "$TFP_NATIVE/native_ads_editor.cpp" ] || [ ! -f "$TFP_NATIVE/zoom_gain_math.h" ] || ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then
  echo "Pose helper: differential test skipped (retail source, g++ or Linux not found)"
  exit 0
fi
mkdir -p "$TFP_WORK"
if [ -z "$TFP_ONLY" ] && { [ $# -eq 0 ] || [ "${1:-}" != --list ]; }; then
  g++ -std=c++17 -O2 -ffp-contract=off -w -I "$TFP_NATIVE" -I "$TFP_ROOT/src" "$TFP_ROOT/tests/diff_pose_math.cpp" -o "$TFP_WORK/diff_pose_math"
  "$TFP_WORK/diff_pose_math"
fi
tfp_build() { # $1 program name, $2... extra defines for both compilations
  local tfp_name="$1"; shift
  "$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$@" -c "$TFP_SOURCE" \
    -o "$TFP_WORK/${tfp_name}_under_test.o"
  if [ ! -f "$TFP_WORK/$tfp_name.o" ] || [ -z "${TFP_POSE_KEEP_TEST_OBJECT:-}" ]; then
    g++ -std=c++17 -O1 -ffp-contract=off -w -fpermissive -I "$TFP_ROOT/tests/winshim" -I "$TFP_NATIVE" -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$@" \
      -c "$TFP_ROOT/tests/diff_pose_helper.cpp" -o "$TFP_WORK/$tfp_name.o"
  fi
  g++ "$TFP_WORK/$tfp_name.o" "$TFP_WORK/${tfp_name}_under_test.o" -lm -lpthread -o "$TFP_WORK/$tfp_name"
}
tfp_status=0
if [ -z "$TFP_ONLY" ] || [ "$TFP_ONLY" = production ]; then
  tfp_build diff_pose_helper
  "$TFP_WORK/diff_pose_helper" "$@" || tfp_status=1
fi
if [ -z "$TFP_ONLY" ] || [ "$TFP_ONLY" = input ]; then
  tfp_build diff_pose_helper_input -DDIFF_POSE_INPUT '-DTFP_POSE_TEST_OPEN_CONSTANTS="diff_pose_helper_open.h"'
  if [ $# -eq 0 ] || [ "${1:-}" != --list ]; then "$TFP_WORK/diff_pose_helper_input" "$@" || tfp_status=1; fi
fi
exit $tfp_status
