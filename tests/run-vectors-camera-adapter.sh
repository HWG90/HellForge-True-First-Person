#!/usr/bin/env bash
# Camera adapter: the C translation (src/camera_adapter.c) replayed against the retail
# camera.lua's own answers (tests/vectors/camera_adapter_vectors.h, cut by
# tools/retail_ref/gen_camera_adapter.py). Prints one summary line; exits non-zero on any
# difference. Needs only a C compiler.
#   tests/run-vectors-camera-adapter.sh [retail commit the vectors must come from]
# TFP_CAMERA_ADAPTER_SOURCE / TFP_CAMERA_ADAPTER_WORK: another C source / build folder (the
# mutation check uses them).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_SOURCE="${TFP_CAMERA_ADAPTER_SOURCE:-$TFP_ROOT/src/camera_adapter.c}"
TFP_WORK="${TFP_CAMERA_ADAPTER_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
"$TFP_CC" -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -I "$TFP_ROOT/src" -c "$TFP_SOURCE" \
  -o "$TFP_WORK/camera_adapter_under_test.o"
if [ ! -f "$TFP_WORK/camera_adapter_test.o" ] || [ -z "${TFP_CAMERA_ADAPTER_KEEP_TEST_OBJECT:-}" ]; then
  "$TFP_CC" -std=c11 -O0 -Wall -Wextra -Werror -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" -c "$TFP_ROOT/tests/camera_adapter_test.c" \
    -o "$TFP_WORK/camera_adapter_test.o"
fi
"$TFP_CC" "$TFP_WORK/camera_adapter_test.o" "$TFP_WORK/camera_adapter_under_test.o" -lm -o "$TFP_WORK/camera_adapter_test"
"$TFP_WORK/camera_adapter_test" "$@"
