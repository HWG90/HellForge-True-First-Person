#!/usr/bin/env bash
# The length of a Trace row and its Turn line (src/trace_row.h) against the log's line.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" "$TFP_ROOT/tests/trace_row_test.c" -lm -o "$TFP_ROOT/work/tests/trace_row_test"
"$TFP_ROOT/work/tests/trace_row_test"
