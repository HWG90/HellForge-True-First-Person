#!/usr/bin/env bash
# The settings store and its binder: tools/convert_settings.py, src/tfp_settings.c, src/wire_settings.c.
#   1. when LuaJIT and the retail tree are there: the vectors are what the unmodified retail Lua answers now
#      (tools/retail_ref/gen_settings.py --check; also the converter's defaults against retail's own defaults);
#   2. tests/wire_settings_test.c: the JSON reader's refusals, the store, retail's answers through the binder;
#   3. tools/retail_ref/check_settings.py: the conversion of the retail defaults and of the user's file, and the
#      C store's answers after loading the converted file, key by key;
#   4. when git has commit e9b3c10 (the published 0.6.0-feel.19): the settings.json this build writes is read by
#      that commit's own loader (tests/old_loader_check.c), and so is the settings.json the build ships.
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
  # The menu sends every choice as "selector": MCM's window must still draw that name as its one-click < choice > row.
  if [ -f "$TFP_MCM/ui.c" ] && ! grep -q 'Same(c->presentation,"selector")' "$TFP_MCM/ui.c"; then
    echo "wire settings: FAILED DBF-MCM's ui.c no longer draws presentation \"selector\" as < choice >"; exit 1
  fi
else
  echo "wire settings: DBF-MCM's source is not at $TFP_MCM; the menu definition is not run through its schema parser"
fi
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/wire_settings_test.c" "${TFP_MCM_FLAGS[@]}" -lm -o "$TFP_WORK/wire_settings_test"
TFP_SETTINGS_OUT="$TFP_WORK/written-settings.json" ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/wire_settings_test"
ASAN_OPTIONS=detect_leaks=0 python3 "$TFP_ROOT/tools/retail_ref/check_settings.py" --binary "$TFP_WORK/wire_settings_test" --work "$TFP_WORK"
# The settings.json shipped beside the DLL (build.sh --feel makes it with the same command) holds no "locked_view".
python3 "$TFP_ROOT/tools/convert_settings.py" --config "$TFP_ROOT/import/retail-config-2026-11-16.cfg" \
  --menu "$TFP_ROOT/import/retail-menu-first-person-2026-11-16.ini" --out "$TFP_WORK/shipped-settings.json" >/dev/null
if grep -q locked_view "$TFP_WORK/shipped-settings.json" "$TFP_WORK/written-settings.json"; then
  echo "wire settings: FAILED a settings.json holds \"locked_view\", which 0.6.0-feel.19 and older refuse"; exit 1
fi
TFP_OLD="${TFP_OLD:-e9b3c10}"
if git -C "$TFP_ROOT" cat-file -e "$TFP_OLD:src/tfp_settings.c" 2>/dev/null; then
  rm -rf "$TFP_WORK/old"; mkdir -p "$TFP_WORK/old"
  git -C "$TFP_ROOT" archive "$TFP_OLD" src | tar -x -C "$TFP_WORK/old"
  "$TFP_CC" -std=c11 -w -I "$TFP_WORK/old/src" "$TFP_WORK/old/src/tfp_settings.c" "$TFP_WORK/old/src/action_select.c" \
    "$TFP_ROOT/tests/old_loader_check.c" -lm -o "$TFP_WORK/old_loader_check"
  "$TFP_WORK/old_loader_check" "$TFP_WORK/written-settings.json"
  "$TFP_WORK/old_loader_check" "$TFP_WORK/shipped-settings.json"
  # The check bites: the same text with the member 0.6.0-feel.20 to feel.24 wrote is refused by that loader.
  sed 's/^ "menu": {$/ "locked_view": {"fov_degrees": 104},\n "menu": {/' "$TFP_WORK/written-settings.json" > "$TFP_WORK/feel24-settings.json"
  if "$TFP_WORK/old_loader_check" "$TFP_WORK/feel24-settings.json" > "$TFP_WORK/old-refusal.txt"; then
    echo "wire settings: FAILED the loader of $TFP_OLD took a settings.json with \"locked_view\"; the check proves nothing"; exit 1
  fi
  grep -q "locked_view: an unknown member" "$TFP_WORK/old-refusal.txt"
  echo "wire settings: the loader of $TFP_OLD (0.6.0-feel.19) reads the settings.json this build writes and the one it ships, and refuses one with \"locked_view\""
else
  echo "wire settings: commit $TFP_OLD is not in this clone; the 0.6.0-feel.19 loader itself was not run (the member names are checked in the test)"
fi
