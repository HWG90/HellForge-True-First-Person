#!/usr/bin/env bash
# Round 38: which exosuit the local diver sits in, by guarded reads alone (src/cockpit_find.h): tests/cockpit_find_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/cockpit_find_test.c" -lm -o "$TFP_ROOT/work/tests/cockpit_find_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/cockpit_find_test"
