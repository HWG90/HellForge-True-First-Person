#!/usr/bin/env bash
# Round 42: "Aim-in time", the shared transition clock's ADS_ENTER duration (src/transition.h tfp_transition_set_enter):
# tests/transition_enter_test.c. Retail's own answers at 0.18 s are tests/transition_test.c (tests/run-native.sh).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -g -I "$TFP_ROOT/src" \
  "$TFP_ROOT/src/transition.c" "$TFP_ROOT/tests/transition_enter_test.c" -lm -o "$TFP_ROOT/work/tests/transition_enter"
"$TFP_ROOT/work/tests/transition_enter"
