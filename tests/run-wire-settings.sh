#!/usr/bin/env bash
# The settings store and its binder: tools/convert_settings.py, src/tfp_settings.c, src/wire_settings.c.
#   1. when LuaJIT and the retail tree are there: the vectors are what the unmodified retail Lua answers now
#      (tools/retail_ref/gen_settings.py --check; also the converter's defaults against retail's own defaults);
#   2. tests/wire_settings_test.c: the JSON reader's refusals, the store, retail's answers through the binder;
#   3. tools/retail_ref/check_settings.py: the conversion of the retail defaults and of the user's file, and the
#      C store's answers after loading the converted file, key by key.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_RETAIL="${TFP_RETAIL:-$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit}"
TFP_WORK="$TFP_ROOT/work/tests/wire-settings"
mkdir -p "$TFP_WORK"
if command -v luajit >/dev/null && [ -f "$TFP_RETAIL/src/first_person/menu.lua" ]; then
  python3 "$TFP_ROOT/tools/retail_ref/gen_settings.py" --retail "$TFP_RETAIL" --check
else
  echo "wire settings: the retail tree or luajit is not here; the committed vectors are replayed as they are"
fi
TFP_SOURCES=()
for tfp_unit in wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery transition camera_adapter \
    fov_adapter near_adapter blur_adapter action_select tfp_settings wire_settings wire_menu; do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_unit.c"); done
# The menu definition also goes through DBF-MCM's own schema parser when its source is beside this repository.
TFP_MCM="${TFP_MCM:-$TFP_ROOT/../dbf-mcm/hellforge/mcm/src}"
TFP_MCM_FLAGS=()
if [ -f "$TFP_MCM/schema.c" ]; then
  for tfp_unit in schema json num; do "$TFP_CC" -std=c11 -w -c -I "$TFP_MCM" "$TFP_MCM/$tfp_unit.c" -o "$TFP_WORK/mcm_$tfp_unit.o"; done
  TFP_MCM_FLAGS=(-DTFP_HAVE_MCM_SCHEMA -I "$TFP_MCM" "$TFP_WORK/mcm_schema.o" "$TFP_WORK/mcm_json.o" "$TFP_WORK/mcm_num.o")
else
  echo "wire settings: DBF-MCM's source is not at $TFP_MCM; the menu definition is not run through its schema parser"
fi
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/wire_settings_test.c" "${TFP_MCM_FLAGS[@]}" -lm -o "$TFP_WORK/wire_settings_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/wire_settings_test"
ASAN_OPTIONS=detect_leaks=0 python3 "$TFP_ROOT/tools/retail_ref/check_settings.py" --binary "$TFP_WORK/wire_settings_test" --work "$TFP_WORK"
