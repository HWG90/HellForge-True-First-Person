#!/usr/bin/env bash
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TFP_HF="${HF:-$HOME/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge}"
TFP_ZIG="${ZIG:-$TFP_HF/tools/zig-linux/zig}"
export ZIG_GLOBAL_CACHE_DIR="$TFP_ROOT/work/zig-global"
mkdir -p "$ZIG_GLOBAL_CACHE_DIR"
if [ "${1:-}" = --probe ]; then
 mkdir -p "$TFP_ROOT/dist/true-first-person-probe"
 "$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -shared -O2 -Wall -Wextra -Werror \
  -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "$TFP_ROOT"/src/{probe_mod,backend,snapshot,probe_log,request_log}.c \
  -o "$TFP_ROOT/dist/true-first-person-probe/goose_true_first_person_probe.dll"
 cp "$TFP_ROOT/manifests/probe/hellforge-mod.json" "$TFP_ROOT/dist/true-first-person-probe/hellforge-mod.json"
 echo "Built the read-only probe"
 exit 0
fi
if [ "${1:-}" = --view ]; then
 # The view build (0.6.0-view.1): the translated retail units behind the runtime order, bound by src/wire_*.c.
 # -ffp-contract=off: the translations were proven without fused multiply-add (docs/translation/RUNTIME-TRANSLATION.md).
 mkdir -p "$TFP_ROOT/dist/true-first-person-view"
 TFP_VIEW=(mod_view wire_win wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery transition
  camera_adapter fov_adapter near_adapter blur_adapter camera_helper fov_helper near_helper blur_helper slot_atomic wire_mouse)
 TFP_VIEW_SOURCES=();for tfp_module in "${TFP_VIEW[@]}";do TFP_VIEW_SOURCES+=("$TFP_ROOT/src/$tfp_module.c");done
 "$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -shared -O2 -ffp-contract=off -Wall -Wextra -Werror -DTFP_FIELDS_REQUIRED=1 \
  -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "${TFP_VIEW_SOURCES[@]}" -luser32 \
  -o "$TFP_ROOT/dist/true-first-person-view/goose_true_first_person.dll"
 cp "$TFP_ROOT/manifests/view/hellforge-mod.json" "$TFP_ROOT/dist/true-first-person-view/hellforge-mod.json"
 echo "Built the True First Person view build"
 exit 0
fi
if [ "${1:-}" = --feel ]; then
 # The feel build (0.6.0-feel.1): the view build plus the pose helper, motion, the engine modules, the
 # effects and the settings store (src/wire_feel.h). Same flags as --view. docs/wiring/WIRING-FEEL.md.
 TFP_OUT="$TFP_ROOT/dist/true-first-person-feel"
 mkdir -p "$TFP_OUT"
 TFP_FEEL=(mod_view wire_win wire_rt wire_off wire_recovery wire_log tfp_runtime backend_ops controller recovery transition
  camera_adapter fov_adapter near_adapter blur_adapter camera_helper fov_helper near_helper blur_helper slot_atomic wire_mouse
  wire_feel wire_settings tfp_settings action_select
  pose_helper pose_adapter wire_pose wire_win_pose
  wire_motion motion_runtime motion_math action_state locomotion backend snapshot
  engine wire_engine wire_win_engine head_visibility model_shadow_source arm_pieces grip_hand
  wire_effects wire_win_effects dof vignette aim_markers first_person_movement native_ads_option
  wire_menu mod_menu wire_panel mod_panel wire_editor placement_editor mod_editor
  arm_drive render_slot wire_options warp_glow wire_warp)
 TFP_FEEL_SOURCES=();for tfp_module in "${TFP_FEEL[@]}";do TFP_FEEL_SOURCES+=("$TFP_ROOT/src/$tfp_module.c");done
 "$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -shared -O2 -ffp-contract=off -Wall -Wextra -Werror -DTFP_FIELDS_REQUIRED=1 -DTFP_FEEL=1 \
  -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "${TFP_FEEL_SOURCES[@]}" "$TFP_ROOT/src/pose_input_thunks.S" -luser32 \
  -o "$TFP_OUT/goose_true_first_person.dll"
 cp "$TFP_ROOT/manifests/feel/hellforge-mod.json" "$TFP_OUT/hellforge-mod.json"
 # The user's own retail settings and menu values, converted for the C settings store and shipped beside the DLL.
 python3 "$TFP_ROOT/tools/convert_settings.py" --config "$TFP_ROOT/import/retail-config-2026-11-16.cfg" \
  --menu "$TFP_ROOT/import/retail-menu-first-person-2026-11-16.ini" --out "$TFP_OUT/settings.json"
 echo "Built the True First Person feel build"
 exit 0
fi
if [ "${1:-}" != --donor ]; then
 echo "Usage: bash build.sh --feel | --view | --probe | --donor"
 echo "  --feel   the current build: view, placement, motion, head hiding, effects, look gain (dist/true-first-person-feel)"
 echo "  --view   camera, field of view, near clip and scope blur only (dist/true-first-person-view)"
 echo "  --probe  read-only logger of the game's camera state (dist/true-first-person-probe)"
 echo "  --donor  the cancelled 0.5.34 C port this repository was seeded from (dist/true-first-person)"
 exit 2
fi
TFP_DEFINES=();if [ "${TFP_RESEARCH:-0}" = 1 ];then TFP_DEFINES+=(-DTFP_READONLY_SDK_CENSUS=1);fi
TFP_MODULES=(backend scene presentation correction lens depth motion placement dispatch_lease slot_platform slot_atomic lifecycle native_call camera_policy pose_policy fov_policy near_policy blur_policy visibility_policy action preferences private_byte markers markers_native engine sdk_inventory native_observer effects models settings runtime ui mod)
mkdir -p "$TFP_ROOT/dist/true-first-person"
TFP_SOURCES=();for tfp_module in "${TFP_MODULES[@]}";do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_module.c");done
"$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -shared -O2 -Wall -Wextra -Werror \
 "${TFP_DEFINES[@]}" -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "${TFP_SOURCES[@]}" -luser32 -o "$TFP_ROOT/dist/true-first-person/goose_true_first_person.dll"
cp "$TFP_ROOT/manifests/donor-0.5.34/hellforge-mod.json" "$TFP_ROOT/dist/true-first-person/hellforge-mod.json"
echo "Built C11 True First Person development candidate"
