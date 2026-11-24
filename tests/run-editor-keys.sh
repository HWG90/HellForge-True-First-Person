#!/usr/bin/env bash
# The placement editor's keys (Page Up / Page Down no longer depth; the wheel is): tests/editor_keys_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -I "$TFP_ROOT/src" "$TFP_ROOT/src/placement_editor.c" "$TFP_ROOT/tests/editor_keys_test.c" -lm -o "$TFP_ROOT/work/tests/editor_keys"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/editor_keys"
