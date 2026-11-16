#!/usr/bin/env bash
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_HF="${HF:-$HOME/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge}"
TFP_ZIG="${ZIG:-$TFP_HF/tools/zig-linux/zig}"
export ZIG_GLOBAL_CACHE_DIR="$TFP_ROOT/work/zig-global"
mkdir -p "$TFP_ROOT/work/tests"
TFP_COMMON=(backend scene depth presentation correction motion lens placement action dispatch_lease slot_platform slot_atomic)
TFP_SOURCES=();for tfp_source in "${TFP_COMMON[@]}";do TFP_SOURCES+=("$TFP_ROOT/src/$tfp_source.c");done
for tfp_test in camera pose fov near visibility;do
  TFP_EXTRA=();if [ "$tfp_test" = pose ];then TFP_EXTRA+=("$TFP_ROOT/src/camera_policy.c");fi
  "$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror \
   -I "$TFP_ROOT/src" "${TFP_SOURCES[@]}" "${TFP_EXTRA[@]}" "$TFP_ROOT/tests/${tfp_test}_policy_test.c" -o "$TFP_ROOT/work/tests/${tfp_test}_policy_test.exe"
done
for tfp_test in models settings;do
 "$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror \
  -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "$TFP_ROOT/src/backend.c" "$TFP_ROOT/src/scene.c" "$TFP_ROOT/src/presentation.c" "$TFP_ROOT/src/placement.c" "$TFP_ROOT/src/${tfp_test}.c" "$TFP_ROOT/tests/${tfp_test}_test.c" -o "$TFP_ROOT/work/tests/${tfp_test}_test.exe"
done
"$TFP_ZIG" cc -std=c11 -target x86_64-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror -I "$TFP_HF/app/sdk" "$TFP_ROOT/tests/dll_smoke.c" -o "$TFP_ROOT/work/tests/dll_smoke.exe"
"$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror \
 -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "$TFP_ROOT/src/backend.c" "$TFP_ROOT/src/effects.c" "$TFP_ROOT/tests/effects_test.c" -o "$TFP_ROOT/work/tests/effects_test.exe"
"$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror \
 -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "$TFP_ROOT/src/backend.c" "$TFP_ROOT/src/scene.c" "$TFP_ROOT/src/presentation.c" "$TFP_ROOT/src/placement.c" "$TFP_ROOT/src/lifecycle.c" "$TFP_ROOT/src/action.c" "$TFP_ROOT/src/preferences.c" "$TFP_ROOT/src/markers.c" "$TFP_ROOT/src/settings.c" "$TFP_ROOT/src/runtime.c" "$TFP_ROOT/tests/runtime_test.c" -o "$TFP_ROOT/work/tests/runtime_test.exe"
"$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror \
 -I "$TFP_ROOT/src" "$TFP_ROOT/src/backend.c" "$TFP_ROOT/src/dispatch_lease.c" "$TFP_ROOT/tests/blur_policy_test.c" -o "$TFP_ROOT/work/tests/blur_policy_test.exe"
for tfp_test in engine_capture engine_result engine_vm_close engine_admission engine_sdk_census;do
 "$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror \
 -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "$TFP_ROOT/tests/${tfp_test}_test.c" -o "$TFP_ROOT/work/tests/${tfp_test}_test.exe"
done

"$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror \
 -I "$TFP_ROOT/src" "$TFP_ROOT/src/backend.c" "$TFP_ROOT/src/scene.c" "$TFP_ROOT/src/presentation.c" "$TFP_ROOT/src/native_observer.c" "$TFP_ROOT/tests/native_observer_test.c" -o "$TFP_ROOT/work/tests/native_observer_test.exe"
"$TFP_ZIG" cc -std=c11 -fms-extensions -target x86_64-windows-gnu -O2 -UNDEBUG -Wall -Wextra -Werror \
 -I "$TFP_HF/app/sdk" -I "$TFP_ROOT/src" "$TFP_ROOT/src/backend.c" "$TFP_ROOT/tests/research_recording_test.c" -o "$TFP_ROOT/work/tests/research_recording_test.exe"
