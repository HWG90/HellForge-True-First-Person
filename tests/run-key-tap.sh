#!/usr/bin/env bash
# Round 50: a key tapped between two game updates is one press (src/key_tap.h): tests/key_tap_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/key_tap_test.c" -lm -o "$TFP_ROOT/work/tests/key_tap_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/key_tap_test"
