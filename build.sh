#!/usr/bin/env bash
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TFP_HF="${HF:-$HOME/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge}"
TFP_ZIG="${ZIG:-$TFP_HF/tools/zig-linux/zig}"
export ZIG_GLOBAL_CACHE_DIR="$TFP_ROOT/work/zig-global"
mkdir -p "$TFP_ROOT/dist/true-first-person" "$ZIG_GLOBAL_CACHE_DIR"
if [ "${1:-}" = --probe ]; then
 mkdir -p "$TFP_ROOT/dist/true-first-person-probe"
 "$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -shared -O2 -Wall -Wextra -Werror \
  -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "$TFP_ROOT"/src/{probe_mod,backend,snapshot,probe_log,request_log}.c \
  -o "$TFP_ROOT/dist/true-first-person-probe/goose_true_first_person_probe.dll"
 cp "$TFP_ROOT/mod-probe/hellforge-mod.json" "$TFP_ROOT/dist/true-first-person-probe/hellforge-mod.json"
 echo "Built the read-only probe"
 exit 0
fi
if [ "${1:-}" = --view ]; then
 # The view build (0.6.0-view.1): the translated retail units behind the runtime order, bound by src/wire_*.c.
 # -ffp-contract=off: the translations were proven without fused multiply-add (docs/RUNTIME-TRANSLATION.md).
 mkdir -p "$TFP_ROOT/dist/true-first-person-view"
 TFP_VIEW=(mod_view wire_win wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery transition
  camera_adapter fov_adapter near_adapter blur_adapter camera_helper fov_helper near_helper blur_helper slot_atomic)
 TFP_VIEW_SOURCES=();for tfp_module in "${TFP_VIEW[@]}";do TFP_VIEW_SOURCES+=("$TFP_ROOT/src/$tfp_module.c");done
 "$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -shared -O2 -ffp-contract=off -Wall -Wextra -Werror \
  -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "${TFP_VIEW_SOURCES[@]}" -luser32 \
  -o "$TFP_ROOT/dist/true-first-person-view/goose_true_first_person.dll"
 cp "$TFP_ROOT/mod-view/hellforge-mod.json" "$TFP_ROOT/dist/true-first-person-view/hellforge-mod.json"
 echo "Built the True First Person view build"
 exit 0
fi
TFP_DEFINES=();if [ "${TFP_RESEARCH:-0}" = 1 ];then TFP_DEFINES+=(-DTFP_READONLY_SDK_CENSUS=1);fi
TFP_MODULES=(backend scene presentation correction lens depth motion placement dispatch_lease slot_platform slot_atomic lifecycle native_call camera_policy pose_policy fov_policy near_policy blur_policy visibility_policy action preferences private_byte markers markers_native engine sdk_inventory native_observer effects models settings runtime ui mod)
TFP_SOURCES=();for tfp_module in "${TFP_MODULES[@]}";do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_module.c");done
"$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -shared -O2 -Wall -Wextra -Werror \
 "${TFP_DEFINES[@]}" -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "${TFP_SOURCES[@]}" -luser32 -o "$TFP_ROOT/dist/true-first-person/goose_true_first_person.dll"
cp "$TFP_ROOT/mod/hellforge-mod.json" "$TFP_ROOT/dist/true-first-person/hellforge-mod.json"
echo "Built C11 True First Person development candidate"
