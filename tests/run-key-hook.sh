#!/usr/bin/env bash
# Round 50: the keyboard hook and the tap reader of src/wire_win.c on a window of the test (tests/key_hook_test.c).
# Built for Windows with HellForge's zig; run under Wine in a scratch prefix (TFP_WINEPREFIX, default
# ~/.cache/tfp-wine-tests) inside a headless gamescope, so nothing opens on the desktop. Without zig, wine or gamescope
# it says so and stops.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_HF="${HF:-$HOME/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge}"
TFP_ZIG="${ZIG:-$TFP_HF/tools/zig-linux/zig}"
export ZIG_GLOBAL_CACHE_DIR="$TFP_ROOT/work/zig-global"
mkdir -p "$TFP_ROOT/work/tests" "$ZIG_GLOBAL_CACHE_DIR"
if [ ! -x "$TFP_ZIG" ] || [ ! -d "$TFP_HF/app/sdk" ]; then echo "Key hook: skipped (HellForge's zig or SDK not found)"; exit 0; fi
TFP_UNITS=(wire_win wire_mouse camera_helper fov_helper near_helper blur_helper pose_helper slot_atomic)
TFP_SOURCES=();for tfp_unit in "${TFP_UNITS[@]}";do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_unit.c");done
"$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -O2 -ffp-contract=off -UNDEBUG -Wall -Wextra -Werror -DTFP_FEEL=1 \
  -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/key_hook_test.c" -o "$TFP_ROOT/work/tests/key_hook_test.exe"
if ! command -v wine >/dev/null; then echo "Key hook: built; not run (no wine)"; exit 0; fi
# A window needs a display: a headless gamescope gives one that is not the desktop (no window, no pointer there).
# TFP_WINE: another wine to run it with (a Proton's files/bin/wine answers GetAsyncKeyState by Proton's rule; give it a
# prefix of its own with TFP_WINEPREFIX).
if ! command -v gamescope >/dev/null; then echo "Key hook: built; not run (no gamescope for a display that is not the desktop)"; exit 0; fi
TFP_WINE="${TFP_WINE:-wine}"
TFP_PREFIX="${TFP_WINEPREFIX:-$HOME/.cache/tfp-wine-tests}"
mkdir -p "$TFP_PREFIX"
tfp_out="$(env -u DISPLAY -u WAYLAND_DISPLAY WINEPREFIX="$TFP_PREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" \
  timeout 300 gamescope --backend headless -W 640 -H 480 -- "$TFP_WINE" "$TFP_ROOT/work/tests/key_hook_test.exe" 2>/dev/null | tr -d '\r' | grep -i '^key hook' || true)"
echo "$tfp_out"
echo "$tfp_out" | grep -q "^Key hook: not run" && exit 0
echo "$tfp_out" | grep -q "^Key hook: .* 0 failed"
