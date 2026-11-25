#!/usr/bin/env bash
# The Windows glue to DBF-MCM (src/mod_menu.c) on Linux: tests/mod_menu_test.c plays goose_mcm.dll through
# tests/fake_windows/windows.h. Needs HellForge's SDK header (read only).
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
TFP_HF="${HF:-$HOME/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge}"
TFP_WORK="$TFP_ROOT/work/tests/mod-menu"
mkdir -p "$TFP_WORK"
if [ ! -f "$TFP_HF/app/sdk/hellforge_mod.h" ]; then
  echo "mod menu: FAILED HellForge's SDK is not at $TFP_HF/app/sdk"; exit 1
fi
TFP_FLAGS=(-std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g
           "-D__declspec(x)=" -I "$TFP_ROOT/tests/fake_windows" -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests")
TFP_SOURCES=()
for tfp_unit in wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery transition camera_adapter \
    fov_adapter near_adapter blur_adapter action_select tfp_settings wire_settings wire_menu wire_panel; do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_unit.c"); done
# only the glue is built as Windows code: mcm_connect() then asks GetModuleHandleA / GetProcAddress, which the test plays
"$TFP_CC" "${TFP_FLAGS[@]}" -D_WIN32 -c "$TFP_ROOT/src/mod_menu.c" -o "$TFP_WORK/mod_menu.o"
"$TFP_CC" "${TFP_FLAGS[@]}" "${TFP_SOURCES[@]}" "$TFP_WORK/mod_menu.o" "$TFP_ROOT/tests/mod_menu_test.c" -lm -o "$TFP_WORK/mod_menu_test"
ASAN_OPTIONS=detect_leaks=0 "$TFP_WORK/mod_menu_test"
