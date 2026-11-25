#!/usr/bin/env bash
# Round 28 (docs/design/specs/2026-11-25-performance.md): host_write as a guarded copy gives the five helpers' outputs
# byte for byte what WriteProcessMemory gave, and the Perf line's Windows counters count (tests/host_write_test.c).
# Built for Windows with HellForge's zig; run under Wine in a scratch prefix (TFP_WINEPREFIX, default
# ~/.cache/tfp-wine-tests) with no display, so nothing opens on the desktop. Without zig or wine it says so and stops.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_HF="${HF:-$HOME/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge}"
TFP_ZIG="${ZIG:-$TFP_HF/tools/zig-linux/zig}"
export ZIG_GLOBAL_CACHE_DIR="$TFP_ROOT/work/zig-global"
mkdir -p "$TFP_ROOT/work/tests" "$ZIG_GLOBAL_CACHE_DIR"
if [ ! -x "$TFP_ZIG" ] || [ ! -d "$TFP_HF/app/sdk" ]; then echo "Host write: skipped (HellForge's zig or SDK not found)"; exit 0; fi
TFP_UNITS=(wire_win wire_mouse camera_helper fov_helper near_helper blur_helper pose_helper slot_atomic)
TFP_SOURCES=();for tfp_unit in "${TFP_UNITS[@]}";do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_unit.c");done
"$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -O2 -ffp-contract=off -UNDEBUG -Wall -Wextra -Werror -DTFP_FEEL=1 \
  -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "${TFP_SOURCES[@]}" "$TFP_ROOT/tests/host_write_test.c" -o "$TFP_ROOT/work/tests/host_write_test.exe"
if ! command -v wine >/dev/null; then echo "Host write: built; not run (no wine)"; exit 0; fi
TFP_PREFIX="${TFP_WINEPREFIX:-$HOME/.cache/tfp-wine-tests}"
mkdir -p "$TFP_PREFIX"
tfp_out="$(env -u DISPLAY -u WAYLAND_DISPLAY WINEPREFIX="$TFP_PREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" \
  timeout 300 wine "$TFP_ROOT/work/tests/host_write_test.exe" 2>/dev/null | tr -d '\r')" || { echo "$tfp_out"; exit 1; }
echo "$tfp_out" | grep -v '^$'
echo "$tfp_out" | grep -q "^Host write: .* 0 failed"
