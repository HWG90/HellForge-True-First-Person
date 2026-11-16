#!/usr/bin/env bash
# Action state: the C translation (src/action_state.c) replayed against the retail action_state.lua's
# own answers (tests/vectors/action_state_vectors.h, cut by tools/retail_ref/gen_action_state.py): the
# same scenes in retail's memory layout for the Lua and in build 25480438's for the C.
# Prints one summary line; exits non-zero on any difference. Needs only a C compiler.
#   tests/run-vectors-action_state.sh [retail commit the vectors must come from]
# TFP_ACTION_STATE_SOURCE / TFP_ACTION_STATE_WORK: another C source / build folder (the mutation check
# uses them). TFP_ACTION_STATE_KEEP_TEST_OBJECT: reuse the compiled test and support objects.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_ACTION_STATE_SOURCE:-$TFP_ROOT/src/action_state.c}"
TFP_WORK="${TFP_ACTION_STATE_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" -c "$TFP_SOURCE" \
  -o "$TFP_WORK/action_state_under_test.o"
if [ ! -f "$TFP_WORK/action_state_test.o" ] || [ -z "${TFP_ACTION_STATE_KEEP_TEST_OBJECT:-}" ]; then
  "$TFP_CC" -std=c11 -O0 -Wall -Wextra -Werror -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/action_state_test.c" \
    -o "$TFP_WORK/action_state_test.o"
  "$TFP_CC" -std=c11 -O1 -Wall -Wextra -Werror -I "$TFP_ROOT/src" -c "$TFP_ROOT/src/snapshot.c" -o "$TFP_WORK/action_state_snapshot.o"
  "$TFP_CC" -std=c11 -O1 -Wall -Wextra -Werror -I "$TFP_ROOT/src" -c "$TFP_ROOT/src/backend.c" -o "$TFP_WORK/action_state_backend.o"
fi
"$TFP_CC" "$TFP_WORK/action_state_test.o" "$TFP_WORK/action_state_under_test.o" "$TFP_WORK/action_state_snapshot.o" \
  "$TFP_WORK/action_state_backend.o" -lm -o "$TFP_WORK/action_state_test"
"$TFP_WORK/action_state_test" "$@"
