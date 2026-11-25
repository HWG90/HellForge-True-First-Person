#!/usr/bin/env bash
# The pose helper's look-gain steering read with HellForge 0.5's moved fields (tests/pose_fields_test.c): built as
# the tests are, then with -DTFP_FIELDS_REQUIRED as the shipped DLL is. Needs only a C compiler.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
for tfp_mode in tests shipped; do
  tfp_defines=()
  if [ "$tfp_mode" = shipped ]; then tfp_defines=(-DTFP_FIELDS_REQUIRED=1); fi
  "$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror "${tfp_defines[@]}" -DTFP_POSE_TEST_STEERING=1 -I "$TFP_ROOT/src" \
    -c "$TFP_ROOT/src/pose_helper.c" -o "$TFP_ROOT/work/tests/pose_fields_helper_$tfp_mode.o"
  "$TFP_CC" -std=c11 -O1 -Wall -Wextra -Werror "${tfp_defines[@]}" -I "$TFP_ROOT/src" "$TFP_ROOT/tests/pose_fields_test.c" \
    "$TFP_ROOT/work/tests/pose_fields_helper_$tfp_mode.o" -lm -lpthread -o "$TFP_ROOT/work/tests/pose_fields_$tfp_mode"
  "$TFP_ROOT/work/tests/pose_fields_$tfp_mode"
done
