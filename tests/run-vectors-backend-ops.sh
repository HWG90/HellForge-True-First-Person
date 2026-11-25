#!/usr/bin/env bash
# Backend operations: the C translation (src/backend_ops.c) replayed against the retail
# backend.lua's own answers (tests/vectors/backend_ops_vectors.h, cut by
# tools/retail_ref/gen_backend_ops.py). Two programs: the production object (the presentation
# switch refuses: its constants are open on build 25480438) and a second object built with the
# test-only open constants of tests/backend_ops_open.h, which runs every script. Each prints
# one summary line; exits non-zero on any difference. Each runs twice: as the game runs by default, and with the
# snapshot memo live (TFP_OPS_MEMO=1, the game's memo_on.txt; round 35). Needs only a C compiler.
#   tests/run-vectors-backend-ops.sh [retail commit the vectors must come from]
# TFP_BACKEND_OPS_SOURCE / TFP_BACKEND_OPS_WORK: another C source / build folder (the mutation
# check uses them). TFP_BACKEND_OPS_KEEP_TEST_OBJECT: reuse the compiled test objects.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_BACKEND_OPS_SOURCE:-$TFP_ROOT/src/backend_ops.c}"
TFP_WORK="${TFP_BACKEND_OPS_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
if [ ! -f "$TFP_ROOT/tests/vectors/backend_ops_vectors.h" ]; then
  echo "Backend operations: skipped (tests/vectors/backend_ops_vectors.h not cut; see tools/retail_ref/gen_backend_ops.py)"
  exit 0
fi
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" -c "$TFP_SOURCE" \
  -o "$TFP_WORK/backend_ops_under_test.o"
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  '-DTFP_OPS_TEST_OPEN_CONSTANTS="backend_ops_open.h"' -c "$TFP_SOURCE" -o "$TFP_WORK/backend_ops_open_under_test.o"
if [ ! -f "$TFP_WORK/backend_ops_test.o" ] || [ -z "${TFP_BACKEND_OPS_KEEP_TEST_OBJECT:-}" ]; then
  "$TFP_CC" -std=c11 -O0 -Wall -Wextra -Werror -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/backend_ops_test.c" \
    -o "$TFP_WORK/backend_ops_test.o"
  "$TFP_CC" -std=c11 -O0 -Wall -Wextra -Werror -DOPS_OPEN_CONSTANTS -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
    -c "$TFP_ROOT/tests/backend_ops_test.c" -o "$TFP_WORK/backend_ops_open_test.o"
fi
"$TFP_CC" "$TFP_WORK/backend_ops_test.o" "$TFP_WORK/backend_ops_under_test.o" -lm -o "$TFP_WORK/backend_ops_test"
"$TFP_CC" "$TFP_WORK/backend_ops_open_test.o" "$TFP_WORK/backend_ops_open_under_test.o" -lm -o "$TFP_WORK/backend_ops_open_test"
"$TFP_WORK/backend_ops_test" "$@"
"$TFP_WORK/backend_ops_open_test" "$@"
# Round 35: again with the snapshot memo live (memo_on.txt in the game; tfp_ops_set_memo after every "new").
TFP_OPS_MEMO=1 "$TFP_WORK/backend_ops_test" "$@"
TFP_OPS_MEMO=1 "$TFP_WORK/backend_ops_open_test" "$@"
