#!/usr/bin/env bash
# The eye held through a reload (src/reload_hold.h, round 39): tests/reload_hold_test.c, with the replay of the user's
# reload of 2026-11-25 (tests/reference/reload/), and (round 40) the harness's feel.58 reload whose scaled edges round 39 refused,
# and (round 49e) the feel.60 sidearm reload the hold did not keep still, with the edges looked at. The game.dll image is used where it is found (TFP_GAME_IMAGE).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/reload_hold_test.c" -lm -o "$TFP_ROOT/work/tests/reload_hold_test"
TFP_IMAGE="${TFP_GAME_IMAGE:-$HOME/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll}"
if [ -f "$TFP_IMAGE" ]; then export TFP_GAME_IMAGE="$TFP_IMAGE"; else unset TFP_GAME_IMAGE; fi
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/reload_hold_test" "$TFP_ROOT/tests/reference/reload/feel56-L6985-7100.log" "$TFP_ROOT/tests/reference/reload/feel58-L4150-4266.log" \
  "$TFP_ROOT/tests/reference/reload/feel60-L3545-3625.log"
