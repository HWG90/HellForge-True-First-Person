#!/usr/bin/env bash
# Round 20: the near clip in full aim moves in with the gun, through the near helper (src/near_helper.c) on the
# differential test's fake game memory: tests/near_ads_test.cpp. Needs g++ and Linux (process_vm_readv); no retail source.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
if ! command -v g++ >/dev/null || [ "$(uname -s)" != Linux ]; then echo "Near ads: skipped (g++ or Linux not found)"; exit 0; fi
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -c "$TFP_ROOT/src/near_helper.c" \
  -o "$TFP_ROOT/work/tests/near_helper_ads.o"
g++ -std=c++17 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
  "$TFP_ROOT/tests/near_ads_test.cpp" "$TFP_ROOT/work/tests/near_helper_ads.o" -lm -o "$TFP_ROOT/work/tests/near_ads_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/near_ads_test"
