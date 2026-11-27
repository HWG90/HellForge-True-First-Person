#!/usr/bin/env bash
# Round 53: the placement editor's list of saved placements, grouped by category (src/editor_weapons.h): tests/editor_weapons_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -g -I "$TFP_ROOT/src" \
  "$TFP_ROOT/src/action_select.c" "$TFP_ROOT/tests/editor_weapons_test.c" -lm -o "$TFP_ROOT/work/tests/editor_weapons"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/editor_weapons"
