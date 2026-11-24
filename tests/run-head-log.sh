#!/usr/bin/env bash
# The "Head:" line's counting and wording (src/head_log.h): tests/head_log_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/head_log_test.c" -lm -o "$TFP_ROOT/work/tests/head_log_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/head_log_test"
