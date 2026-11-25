#!/usr/bin/env bash
# Round 38: the "Seat:" line (src/seat_log.h): tests/seat_log_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined -g -I "$TFP_ROOT/src" "$TFP_ROOT/tests/seat_log_test.c" -o "$TFP_ROOT/work/tests/seat_log_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/seat_log_test"
