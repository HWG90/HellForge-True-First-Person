#!/usr/bin/env bash
# Round 20: the sight brought closer in full aim and the "Aim:" line: tests/ads_seat_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I "$TFP_ROOT/src" \
  "$TFP_ROOT/tests/ads_seat_test.c" -lm -o "$TFP_ROOT/work/tests/ads_seat_test"
"$TFP_ROOT/work/tests/ads_seat_test"
