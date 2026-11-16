#!/usr/bin/env bash
# Action placement: the C translation (src/action_select.c) replayed against the retail action_placement.lua's
# own answers (tests/vectors/action_placement_vectors.h, cut by tools/retail_ref/gen_action_placement.py).
# Prints one summary line; exits non-zero on any difference. Needs only a C compiler.
#   tests/run-vectors-action_placement.sh [retail commit the vectors must come from]
# TFP_ACTION_PLACEMENT_SOURCE / TFP_ACTION_PLACEMENT_WORK: another C source / build folder (the mutation check
# uses them). TFP_ACTION_PLACEMENT_KEEP_TEST_OBJECT: reuse the compiled test object.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_ACTION_PLACEMENT_SOURCE:-$TFP_ROOT/src/action_select.c}"
TFP_WORK="${TFP_ACTION_PLACEMENT_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" -c "$TFP_SOURCE" \
  -o "$TFP_WORK/action_select_under_test.o"
if [ ! -f "$TFP_WORK/action_placement_test.o" ] || [ -z "${TFP_ACTION_PLACEMENT_KEEP_TEST_OBJECT:-}" ]; then
  "$TFP_CC" -std=c11 -O0 -Wall -Wextra -Werror -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/action_placement_test.c" \
    -o "$TFP_WORK/action_placement_test.o"
fi
"$TFP_CC" "$TFP_WORK/action_placement_test.o" "$TFP_WORK/action_select_under_test.o" -lm -o "$TFP_WORK/action_placement_test"
"$TFP_WORK/action_placement_test" "$@"
