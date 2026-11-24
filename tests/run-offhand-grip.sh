#!/usr/bin/env bash
# The support hand's reach on the weapon's grip (src/offhand_grip.h) on a model left arm: reach, stretch, the elbow's
# side, the step limit over random target paths at four frame times, a target going round behind the shoulder, release
# and refusals. Built twice: with the sanitizers, and as the game build compiles it (optimised).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="$TFP_ROOT/work/tests"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g -I "$TFP_ROOT/src" \
  "$TFP_ROOT/tests/offhand_grip_test.c" -lm -o "$TFP_WORK/offhand_grip"
"$TFP_WORK/offhand_grip"
"$TFP_CC" -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" "$TFP_ROOT/tests/offhand_grip_test.c" -lm -o "$TFP_WORK/offhand_grip_o2"
"$TFP_WORK/offhand_grip_o2" | sed 's/^/-O2: /'
