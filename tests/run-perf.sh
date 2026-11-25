#!/usr/bin/env bash
# Round 28 (docs/design/specs/2026-11-25-performance.md): the once-a-second Perf line (src/perf_line.h) fits say()'s
# 700-byte buffer and reads as documented. The engine bridge's call count is checked in tests/wire_engine_test.c, the
# depth of field's and the vignette's timing in tests/wire_effects_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="$TFP_ROOT/work/tests"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -g -I "$TFP_ROOT/src" \
  "$TFP_ROOT/tests/perf_line_test.c" -lm -o "$TFP_WORK/perf_line"
"$TFP_WORK/perf_line"
