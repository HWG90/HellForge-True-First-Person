#!/usr/bin/env bash
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
for tfp_test in backend placement dispatch correction; do
  tfp_source="$tfp_test"
  if [ "$tfp_test" = dispatch ]; then tfp_source=dispatch_lease; fi
  "$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I "$TFP_ROOT/src" "$TFP_ROOT/src/$tfp_source.c" "$TFP_ROOT/tests/${tfp_test}_test.c" \
    -lm -o "$TFP_ROOT/work/tests/$tfp_test"
  ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/$tfp_test"
done
for tfp_test in scene attachment lifecycle markers preferences action depth native_observer;do
 TFP_EXTRA=();case "$tfp_test" in
  native_observer) TFP_EXTRA+=("$TFP_ROOT/src/native_observer.c" "$TFP_ROOT/src/scene.c" "$TFP_ROOT/src/presentation.c");;
  scene|attachment) TFP_EXTRA+=("$TFP_ROOT/src/scene.c" "$TFP_ROOT/src/presentation.c");;
  lifecycle|markers|preferences) TFP_EXTRA+=("$TFP_ROOT/src/$tfp_test.c");;
  action) TFP_EXTRA+=("$TFP_ROOT/src/action.c" "$TFP_ROOT/src/placement.c");;
 esac
 "$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" \
 "$TFP_ROOT/src/backend.c" "${TFP_EXTRA[@]}" "$TFP_ROOT/tests/${tfp_test}_test.c" -lm -o "$TFP_ROOT/work/tests/$tfp_test"
 ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/$tfp_test"
done

# Session log tools (from the First Person Controller, 2026-11-16).
for tfp_test in smooth_meter request_log; do
  "$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" \
    "$TFP_ROOT/src/$tfp_test.c" "$TFP_ROOT/tests/${tfp_test}_test.c" -lm -o "$TFP_ROOT/work/tests/$tfp_test"
  ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/$tfp_test"
done

# Retail smooth.66 translations.
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -g -I "$TFP_ROOT/src" \
  "$TFP_ROOT/src/backend.c" "$TFP_ROOT/src/scene.c" "$TFP_ROOT/src/presentation.c" "$TFP_ROOT/src/dispatch_lease.c" \
  "$TFP_ROOT/src/snapshot.c" "$TFP_ROOT/tests/snapshot_test.c" -lm -o "$TFP_ROOT/work/tests/snapshot"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/snapshot"
for tfp_test in transition recovery; do
  "$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
    "$TFP_ROOT/src/$tfp_test.c" "$TFP_ROOT/tests/${tfp_test}_test.c" -lm -o "$TFP_ROOT/work/tests/$tfp_test"
  "$TFP_ROOT/work/tests/$tfp_test"
done
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/src/controller.c" "$TFP_ROOT/tests/controller_test.c" -lm -o "$TFP_ROOT/work/tests/controller"
"$TFP_ROOT/work/tests/controller"
