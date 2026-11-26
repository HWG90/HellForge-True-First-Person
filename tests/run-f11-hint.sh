#!/usr/bin/env bash
# Round 41: F11 pressed with the toggle key on U gets one notice (src/f11_hint.h): tests/f11_hint_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/f11_hint_test.c" -lm -o "$TFP_ROOT/work/tests/f11_hint_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/f11_hint_test"
