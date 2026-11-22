#!/usr/bin/env bash
# The field-of-view lock through the helper (lock_field in src/fov_helper.c), on the differential test's fake game
# memory: tests/fov_lock_helper_test.cpp. Needs g++ and Linux (process_vm_readv); no retail source.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
if ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then echo "FOV lock helper: skipped (g++ or Linux not found)"; exit 0; fi
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -c "$TFP_ROOT/src/fov_helper.c" \
  -o "$TFP_ROOT/work/tests/fov_helper_lock.o"
g++ -std=c++17 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/tests/fov_lock_helper_test.cpp" "$TFP_ROOT/work/tests/fov_helper_lock.o" -lm -o "$TFP_ROOT/work/tests/fov_lock_helper_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/fov_lock_helper_test"
