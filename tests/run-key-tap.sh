#!/usr/bin/env bash
# Round 50: a key tapped between two game updates is one press (src/key_tap.h): tests/key_tap_test.c, and
# tests/key_tap_fuzz.c: random presses, hitches (to 2 s), focus changes and late key messages (to 600 ms) against a model
# of the keyboard, under each source (Proton's low bit with messages, Windows' with and without, messages alone) and
# both kinds of consumer. TFP_KEY_FUZZ_SEEDS: schedules per run (default 150, two minutes of keyboard each).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/key_tap_test.c" -lm -o "$TFP_ROOT/work/tests/key_tap_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/key_tap_test"
cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "$TFP_ROOT/tests/key_tap_fuzz.c" -o "$TFP_ROOT/work/tests/key_tap_fuzz"
for tfp_consumer in 0 1; do
  for tfp_model in "2 1 600 1" "0 1 600 1" "1 1 600 300" "1 0 0 300" "2 1 60 1" "2 1 0 1"; do
    # shellcheck disable=SC2086
    ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/key_tap_fuzz" $tfp_model "$tfp_consumer" "${TFP_KEY_FUZZ_SEEDS:-150}"
  done
done
