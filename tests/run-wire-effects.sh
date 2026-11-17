#!/usr/bin/env bash
# The effect modules of the feel build behind the runtime host: tests/wire_effects_test.c
# (src/wire_effects.c over the five units, src/wire_win_effects.c over a scripted engine bridge),
# then tools/verify_effects_constants.py on the constant tables the units carry.
# The two image files of build 25480438 are read when they are there (TFP_GAME_IMAGE, TFP_EXE_IMAGE
# name others); without them the image checks are skipped with a line and the rest still runs.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_WORK="${TFP_FX_WORK:-$TFP_ROOT/work/tests}"
mkdir -p "$TFP_WORK"
TFP_SOURCES=()
for tfp_unit in wire_effects wire_win_effects dof vignette aim_markers first_person_movement native_ads_option \
    wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery transition camera_adapter \
    fov_adapter near_adapter blur_adapter; do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_unit.c"); done
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -I "$TFP_ROOT/src" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/wire_effects_test.c" -lm -o "$TFP_WORK/wire_effects"
TFP_CENSUS="$TFP_ROOT/docs/research-0.5.34/live-sdk-census-g1-9472b065.tsv" ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/wire_effects"
"$TFP_WORK/wire_effects" --constants > "$TFP_WORK/wire_effects_constants.txt"
python3 "$TFP_ROOT/tools/verify_effects_constants.py" "$TFP_WORK/wire_effects_constants.txt" > "$TFP_WORK/wire_effects_constants.log" || {
  cat "$TFP_WORK/wire_effects_constants.log"; exit 1; }
tail -n 1 "$TFP_WORK/wire_effects_constants.log" | sed 's/^/constants: /'
grep -c '^ok' "$TFP_WORK/wire_effects_constants.log" | sed 's/$/ constant checks against the images (tools\/verify_effects_constants.py)/'
