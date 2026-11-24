#!/usr/bin/env bash
# What the continuity and the eye damping are told in a clamber or a vault (round 24): tests/camera_traversal_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$TFP_ROOT/work/tests"
cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror -fsanitize=address,undefined -g -I "$TFP_ROOT/src" "$TFP_ROOT/tests/camera_traversal_test.c" -lm -o "$TFP_ROOT/work/tests/camera_traversal_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/camera_traversal_test"
