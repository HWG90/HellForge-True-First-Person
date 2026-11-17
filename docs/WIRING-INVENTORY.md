# Wiring inventory: binding every host pointer to HellForge (build 25480438)

Written 2026-11-16 at commit 9c279df (branch `m1b-camera`). Research only: no source file was changed, nothing was built or run. "Not found" means I looked and found nothing.

Short names used below:

- `SDK` = `~/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk`
- `MODS` = the same install's `mods` folder
- `BP` = `~/Projects/HellForge/Backports`
- `KIT` = `~/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit` (retail, read only)
- `census` = `docs/research-0.5.34/live-sdk-census-g1-9472b065.tsv` (the `stingray` table as registered in the running game on this build, session 9472b065; `docs/research-0.5.34/LIVE-SDK-CENSUS.md:1-9`). A census line proves a function is registered and where its C body is. It does not prove its arguments or effect.
- `map` = `tools/build_map.py --retail KIT --to-here …` (I ran it for the constants of section 2).

Status words:

- **READY**: exists and fits; at most a forwarding line (or, for the runtime host, the thin calling-convention adapter every pointer needs, `docs/RUNTIME-TRANSLATION.md:94`). Also used, marked "stub", where the pointer belongs to a part that is a stated non-goal (`docs/superpowers/specs/2026-11-16-c-rewrite-design.md:20`) and a fixed answer is retail's own answer on the shipped path.
- **ADAPTER**: the thing exists (a Win32 call, a donor function, a registered engine function, another translated unit), glue has to be written; the row says what.
- **MISSING**: nothing known provides it; the row says what has to be found or written.

Two findings that change the plan are stated first.

1. **Four parts of the live mode have no C to bind to.** The settings menu (`menu.lua`), the F8 placement editor (`placement_editor.lua`), the settings store behind `mod.set` / `mod.config`, and the controller's placement methods (`controller.lua:102-135` in KIT: `get_placement`, `placement_target`, `set_placement`). `src/controller.h:1-7` says only the manual branch was translated; `grep -i placement src/controller.c` has one hit, a comment (line 148). `docs/RUNTIME-TRANSLATION.md:127,177-182,195` says the same. `on_enable` raises unless `controller_set_placement` answers true (`docs/RUNTIME-TRANSLATION.md:127`), so step 1 needs at least a stand-in for it.
2. **The installed Overlay Host is 0.8.2, API 10** (`MODS/overlay-host/hellforge-mod.json:5`, `MODS/overlay-host/goose_overlay.h:49`). This repository vendors the API 8 header (`src/goose_overlay.h:49`), which connects to it (a host answers any version up to its own; `src/goose_overlay.h:378`). The memory note "host 0.4.0 = API 6" is out of date.

## 0. What HellForge gives a C mod (the sources the tables point at)

| Need | What exists | Where |
|---|---|---|
| Entry point | `hellforge_mod_init(const HfApi*, HfMod*)`, once, on the loader's thread. No unload for a non-live mod; last call is `HF_EVENT_SESSION_END`, not delivered after a crash | `SDK/hellforge_mod.h:1603`, `:6-12`, `:23-27`, `:60-65`; donor `src/mod.c:215-235` |
| Per-frame tick on the game thread | `hf->on_game_thread(mod, cb, user)`: every game update, **before** the game's update. Or the Overlay Host's shared slot `ov_on_game_thread` | `SDK/hellforge_mod.h:1246-1251`; `src/goose_overlay.h:285-291`; donor `src/mod.c:206`, probe `src/probe_mod.c:149` |
| Guarded read / write | `hf->read`, `hf->write` (0 = ok); `hf->read_code` / `code_matches` give the bytes from before any HellForge hook | `SDK/hellforge_mod.h:1128-1129`, `:1360-1363`; donor `src/mod.c:21-24`, `src/probe_mod.c:44-50` |
| Module bases | `hf->module_base("game.dll" / "helldivers2.exe", &size)` | `SDK/hellforge_mod.h:1119`; donor `src/mod.c:184-185` |
| Inline hooks | `hf->hook`, `hf->hook_ex` (shared chains, listed in the session's `hooks.tsv`). A detour runs on its caller's thread | `SDK/hellforge_mod.h:1140`, `:1348-1354`; `SDK/docs/modding/hooks.md:275-276` |
| Engine script functions (`Unit.*`, `World.*`, `Mesh.*`, …) | **No SDK support** (not found in `hellforge_mod.h`, `hellforge_lua.h`, `hellforge_functions.h`). The donor bridge `src/engine.c` drives the game's own Lua state through `lua51.dll`'s C API: it hooks `lua_pcall` and `lua_close` with `hf->hook` (`src/engine.c:258`, `:264`), accepts only the VM whose `stingray.Unit.set_mesh_visibility` is `exe+0x4128e0` (`src/engine.c:149-165`), and calls `stingray.<path>` with `eng_call(path, sig, …)` (`src/engine.h:28-39`). Game thread only, while the engine's script is not running (`src/engine.h:6-7`). `goose.photo-mode` uses the same bridge (`BP/photo-mode/src/engine.c:186-245`) | as cited |
| Native API tables (no Lua) | `*(game+0x3326308)` = `exe+0x27c8d80`; World API at root+0x08, Application +0x10, Unit +0x18, Camera +0x20. The First Person Controller calls these directly and proves each slot by value and bytes | `BP/true-first-person-controller/src/native_camera_source.h:123-162`; `src/profile.h:23` (`FP_API_ROOT`) |
| Keys | **No SDK input API** (not found). Donor: `GetAsyncKeyState` in the tick with a foreground-window test and edge detection (`src/mod.c:25-30`, `:85-97`). The Overlay Host's `key_down` / `key_pressed` work only inside a draw callback (`src/goose_overlay.h:187-188`). Engine `Keyboard.button`, `Mouse.button` are registered (census 1332, 1416) | as cited |
| Menu | `ov_connect()` from `HF_EVENT_GAME_LOADED` or later, `ov->register_panel(&OvPanelDef)`, colours from `ov->theme(…)`. Draw callbacks run on the render thread | `src/goose_overlay.h:14`, `:27-30`, `:89-111`, `:233`, `:373-378`; donor `src/mod.c:193-203`, `src/ui.c:249-262` |
| Clock | none in the SDK (not found). Win32 `QueryPerformanceCounter`, `GetTickCount64`; engine `Application.time_since_launch` (census 274) | donor `src/mod.c:52` |
| Other mods | `hf->mods(mod, index, &info)` lists them; no dependency or conflict field in the manifest (not found in `SDK/docs/modding/concepts.md:40-53` or any installed manifest) | `SDK/hellforge_mod.h:1304` |

Where the tick sits: HellForge's game-thread callback is a hook on `game.dll+0x13270b0`, "the menu sound manager's update" (`…/HellForge/logs/sessions/bea4e14a93cb4be99ae4bd21877dd4f3/modloader.log:344-345`). Retail's update runs inside the game's script update (`docs/RETAIL-SMOOTH66-INVENTORY.md:305-338`). Whether this build's tick comes before or after the game's script update and animation in a frame is **not established** (see section 4).

## 1. Every host pointer

`n` is the number of pointers in the row. Rows of one unit share one struct; the header line is cited once per unit.

### 1.1 The five helpers (what retail's helper DLLs took from Windows)

Common sources: `sdk_read` = `src/mod.c:21-24` (wraps `hf->read`); `fp_atomic_pointer` = `src/slot_atomic.c:12-15` (CAS inside `__try`, false on a fault); `slot_valid` = `src/slot_platform.c:6-15` (static; the VirtualQuery rule for a writable image slot, Wine's `MEM_MAPPED` included); `executable` = `src/native_call.c:5-14` (static). Retail's rules are `KIT/native/first_person/native_memory.h:2-13` and, for example, `native_ads_editor.cpp:267-282` (writable slot) and `:178-182` (executable).

| Unit (struct, header) | Pointer | n | Must do | HellForge-side source | Status |
|---|---|---|---|---|---|
| camera helper (`TfpCameraHost`, `src/camera_helper.h:70-107`) | `hook` | 1 | Address of the function put into CameraAPI+0x20; it passes its own return address to `tfp_camera_setter_from` (`src/camera_helper.h:115`) | New `noinline` wrapper with `_ReturnAddress()`; the donor did exactly this at `src/camera_policy.c:427` (file to be deleted, pattern kept) | ADAPTER |
| | `read` | 1 | All bytes or false | `sdk_read` | READY |
| | `readable_span` | 1 | VirtualQuery walk over a span, only after a failed read | none in the repository; Win32, from `KIT/…/native_ads_camera.cpp:164-178` | ADAPTER |
| | `now` | 1 | Milliseconds as a double (QPC, GetTickCount64 fallback) | Win32, from `native_ads_camera.cpp:139-151` | ADAPTER |
| | `writable_slot` | 1 | Slot is committed writable data of the executable image | `slot_valid(…, write=true)`, to be exposed with `(at, exe)` | ADAPTER |
| | `executable` | 1 | Address is committed executable image code | `native_call.c:5-14` differs from retail (it demands the allocation base and takes `MEM_MAPPED` without the Wine test): rewrite to retail's rule | ADAPTER |
| | `cas` | 1 | Pointer CAS, false when it faulted | `fp_atomic_pointer` | READY |
| | `write` | 1 | Copy into a caller's result struct | guarded copy; the callers are this DLL's own adapters | ADAPTER |
| | `pin` | 1 | `GetModuleHandleExW(FROM_ADDRESS\|PIN)` on own module and the executable | Win32 | ADAPTER |
| | `lock_exclusive`, `unlock_exclusive`, `lock_shared`, `unlock_shared` | 4 | Two SRW locks (control 0, policy 1) | Win32 `SRWLOCK` (the donor uses one at `src/mod.c:38`) | ADAPTER |
| | `forward` | 1 | Test seam | leave NULL (`docs/CAMERA-HELPER-TRANSLATION.md:21`) | READY |
| fov helper (`TfpFovHost`, `src/fov_helper.h:44-88`) | `hook` | 1 | Wrapper for CameraAPI+0x80; the lens arrives in `xmm1` and must reach `tfp_fov_setter_from` with the return address (`docs/FOV-HELPER-TRANSLATION.md:107`) | `noinline void (void*, float)` with `_ReturnAddress()`; donor `src/fov_policy.c:49` | ADAPTER |
| | `read`, `cas` | 2 | as above | `sdk_read`, `fp_atomic_pointer` | READY |
| | `readable_span`, `seconds`, `writable_slot`, `writable_request_field`, `executable`, `write`, `pin` | 7 | as above; `seconds` = QPC ticks / frequency; `writable_request_field` = VirtualQuery on a 4-byte request field | Win32 / `slot_valid` as above | ADAPTER |
| | `cas32` | 1 | 32-bit CAS on a camera request field, false on a fault | none in the repository; copy the `slot_atomic.c` pattern with `InterlockedCompareExchange` | ADAPTER |
| | `lock_exclusive`, `unlock_exclusive` | 2 | SRW | Win32 | ADAPTER |
| | `forward` | 1 | Test seam | NULL | READY |
| near helper (`TfpNearHost`, `src/near_helper.h:51-85`) | `hook` | 1 | Wrapper for CameraAPI+0x68 | as fov; donor `src/near_policy.c:91` | ADAPTER |
| | `read`, `cas` | 2 | | `sdk_read`, `fp_atomic_pointer` | READY |
| | `now_us`, `writable_slot`, `executable`, `write`, `pin` | 5 | `now_us` = QPC in microseconds, the scaling is the host's (`docs/NEAR-HELPER-TRANSLATION.md:13`) | Win32 / `slot_valid` | ADAPTER |
| | four locks | 4 | SRW | Win32 | ADAPTER |
| | `forward` | 1 | | NULL | READY |
| blur helper (`TfpBlurHost`, `src/blur_helper.h:51-84`) | `hook` | 1 | Wrapper in the weapon parameter table `exe+0x27cc638`; must pass exactly `game+0x100a80b+3` as caller (`docs/BLUR-HELPER-TRANSLATION.md:123`) | 4-argument wrapper with `_ReturnAddress()`; donor `src/blur_policy.c:291` | ADAPTER |
| | `read`, `cas` | 2 | | `sdk_read`, `fp_atomic_pointer` | READY |
| | `readable_span`, `writable_slot`, `executable`, `write`, `pin` | 5 | | Win32 / `slot_valid` | ADAPTER |
| | `lock_exclusive`, `unlock_exclusive` | 2 | SRW | Win32 | ADAPTER |
| | `forward` | 1 | | NULL | READY |
| pose helper (`TfpPoseHost`, `src/pose_helper.h:89-141`) | `hooks[0..3]` | 4 | Wrappers for UnitAPI+0x38, +0x48, +0x40, +0x00. Position and getter also pass the caller's stack pointer (own return-address slot + 8) | C with `_ReturnAddress()` and `_AddressOfReturnAddress()+8`; donor `src/pose_policy.c:438-454` | ADAPTER |
| | `hooks[4]`, `hooks[5]` (input mode) | 2 | Wrappers that also capture the caller's `r15` (the camera request). Cannot be C (`docs/POSE-HELPER-TRANSLATION.md:175-176`) | Port `KIT/native/first_person/native_ads_input.S:5-19` and `:43-57` (57 lines; calls retargeted to `tfp_pose_input90_from` / `tfp_pose_position_getter_from`, argument order differs). Only reached through `tfp_pose_begin_input`; see section 2 A | ADAPTER |
| | `read`, `cas` | 2 | | `sdk_read`, `fp_atomic_pointer` | READY |
| | `tick`, `thread` | 2 | `GetTickCount64`, `GetCurrentThreadId` | Win32 | ADAPTER |
| | `writable_slot`, `executable`, `write`, `pin`, `writable32`, `region` | 6 | `pin` is called with own module, the executable, and (input mode) the game | Win32 / `slot_valid` | ADAPTER |
| | `cas32` | 1 | Retail: `NADSE_InputCompareExchange` with its own exception handler (`native_ads_input.S:24-41`, `native_ads_editor.cpp:31-44`) | SEH-guarded C in the `slot_atomic.c` style gives the same answer (true + value found, or false on a fault) | ADAPTER |
| | `allocate` | 1 | `VirtualAlloc(at, 0x10000, RESERVE\|COMMIT, READWRITE)` next to the preset table | Win32; input mode only | ADAPTER |
| | four locks | 4 | SRW | Win32 | ADAPTER |
| | `native_exp`, `forward_setter`, `forward_getter` | 3 | Test seams | NULL (`docs/POSE-HELPER-TRANSLATION.md:27-28`) | READY |

All five helpers dereference a NULL host if anything calls them before `tfp_*_set_host` (`docs/CAMERA-HELPER-TRANSLATION.md:133`, same note in the other four). The binder must set the five hosts before the first adapter call.

### 1.2 Lua-side adapters of the helpers

"runtime clock" = the entry `clock` of the runtime (`src/tfp_runtime_host.h:434`), which rests on `clock_now` (1.9).

| Unit (struct, header) | Pointer | n | Must do | Source | Status |
|---|---|---|---|---|---|
| camera adapter (`TfpCameraHelperApi`, `src/camera_adapter.h:14-24`) | the nine helper calls | 9 | `NADSC_*` | `TFP_CAMERA_HELPER_LINKED` (`src/camera_adapter.h:25`) | READY |
| (`TfpCameraAdapterApi`, `src/camera_adapter.h:66-82`) | `load` | 1 | DLL loader | NULL: helper linked in | READY |
| | `camera_config` | 1 | `backend:camera_config()` | `tfp_ops_camera_config` (`src/backend_ops.h`), copy into `TfpCameraBackendConfig` (14 fields with presence bits) | ADAPTER |
| | `snapshot` | 1 | weapon unit of the snapshot | `tfp_ops_snapshot` | ADAPTER |
| | `controller_result`, `controller_is_enabled` | 2 | mode is pose_offset; enabled | `TfpController` fields (`src/controller.h:55-67`); the C controller has only that mode | ADAPTER |
| | `clock` | 1 | seconds | runtime clock | ADAPTER |
| | `log` | 1 | | `hf->log(mod, "%s", text)` | READY |
| fov adapter (`TfpFovHelperApi`, `src/fov_adapter.h:18-26`) | seven helper calls | 7 | `NADSF_*` | `tfp_fov_*` of `src/fov_helper.h`, same signatures (`docs/LENS-ADAPTERS-TRANSLATION.md:29`); no ready initializer macro (not found) | READY |
| (`TfpFovIo`, `src/fov_adapter.h:64-79`) | `fov_config` | 1 | `backend:fov_config(lease)` | `tfp_ops_fov_config` | ADAPTER |
| | `presentation_transition` | 1 | the shared 0.18 s phase packet | runtime entry `presentation_transition` | ADAPTER |
| | `controller_result`, `controller_enabled`, `controller_lease` | 3 | | `TfpController`; the lease fields come from `TfpOpsLease`, not from `controller.h`'s `TfpLease` (`docs/RUNTIME-TRANSLATION.md:113`) | ADAPTER |
| | `clock` | 1 | | runtime clock | ADAPTER |
| | `log` | 1 | | `hf->log` | READY |
| near adapter (`TfpNearHelperApi`, `src/near_adapter.h:18-24`) | five helper calls | 5 | `NADSN_*` | `tfp_near_*` | READY |
| (`TfpNearIo`, `src/near_adapter.h:48-61`) | `fov_config`, `controller_result`, `controller_enabled`, `controller_lease` | 4 | as fov | as fov | ADAPTER |
| | `options` | 1 | the menu's hip / aim near distances | runtime entry `near_options`; without a menu it answers 0 / 0 (`docs/RUNTIME-TRANSLATION.md:145`) | ADAPTER |
| | `clock` | 1 | may raise; the caller must treat `TFP_NEAR_RAISED` as a failed call, not a refusal (`docs/LENS-ADAPTERS-TRANSLATION.md:113`) | runtime clock | ADAPTER |
| | `log` | 1 | | `hf->log` | READY |
| blur adapter (`TfpBlurHelperApi`, `src/blur_adapter.h:20-26`) | five helper calls | 5 | `NADSB_*` | `tfp_blur_*` | READY |
| (`TfpBlurIo`, `src/blur_adapter.h:52-63`) | `blur_config`, `controller_result`, `controller_enabled`, `controller_lease`, `clock` | 5 | | `tfp_ops_blur_config`, `TfpController`, runtime clock | ADAPTER |
| | `log` | 1 | | `hf->log` | READY |
| pose adapter (`TfpPoseHelperApi`, `src/pose_adapter.h:18-36`) | seventeen helper calls | 17 | `NADSE_*` | `tfp_pose_*` fit as they are, compile-checked (`docs/POSE-ADAPTER-TRANSLATION.md:23`) | READY |
| (`TfpPoseAdapterIo`, `src/pose_adapter.h:47-51`) | `clock` | 1 | | NULL: retail gives none (`docs/POSE-ADAPTER-TRANSLATION.md:24`) | READY |
| | `tick` | 1 | `GetTickCount64` | Win32 | ADAPTER |

### 1.3 Backend operations and the controller

| Unit (struct, header) | Pointer | n | Must do | Source | Status |
|---|---|---|---|---|---|
| backend ops (`TfpOpsHost`, `src/backend_ops.h:124-143`) | `wine` (a field) | 1 | ntdll exports `wine_get_version` | `GetProcAddress` | ADAPTER |
| | `module` | 1 | base of game.dll / helldivers2.exe | `hf->module_base` | READY |
| | `read` | 1 | | `sdk_read` | READY |
| | `write` | 1 | Retail's raw stores: `controller[4]=0`, `request+0x98=0` (`KIT/src/first_person/backend.lua:1350,1360`) | `hf->write` (`SDK/hellforge_mod.h:1129`) | READY |
| | `query` | 1 | VirtualQuery into `TfpOpsRegion` | Win32 | ADAPTER |
| | `call` | 1 | Seven native calls (`src/backend_ops.h:113-121`): allowed, enter, exit, presentation, body exists / trigger, detach | `fp_native_platform` (`src/native_call.c:28-46`) does allowed / enter / exit under SEH from an `FpSnapshot`; it has to take `args[4]` and gain the other four kinds. Addresses are known except presentation (section 2 G) | ADAPTER |
| | `clock` | 1 | carried for callers | runtime clock | ADAPTER |
| | `log` | 1 | | `hf->log` | READY |
| | `foreign_camera`, `camera_before_entry`, `presentation_transition` | 3 | fields `runtime.lua` installs | runtime entries of the same names (`docs/RUNTIME-TRANSLATION.md:100`) | ADAPTER |
| | `placement_resolver` | 1 | per-weapon placement | the adapter calls `tfp_action_resolve` itself (`docs/RUNTIME-TRANSLATION.md:100`); it needs the saved placements, i.e. the settings store (1.9) | ADAPTER |
| | `load_pose` | 1 | the pose adapter object | a `TfpOpsPoseHelper` over `tfp_pose_adapter_*` | ADAPTER |
| | `load_alignment` | 1 | the alignment helper (not on the shipped path, `docs/RETAIL-SMOOTH66-INVENTORY.md:1158`) | no translation exists; answer "unavailable". That a NULL pointer is accepted is stated for `load_pose` only (`docs/BACKEND-OPS-TRANSLATION.md:64`); for `load_alignment` not found, so give a function that raises | ADAPTER |
| (`TfpOpsPoseHelper`, `src/backend_ops.h:98-108`) | `prepare`, `begin`, `owned_handle`, `finish`, `stats`, `set_placement`, `set_zoom_policy`, `set_ads_placement`, `transition`, `set_motion` | 10 | the Lua adapter's methods | `tfp_pose_adapter_prepare`, `_begin`, `_owned_handle`, `_finish`, `_stats`, `_set_placement`, `_set_zoom_policy`, `_set_ads_placement`, `_transition`, `_set_motion` (`docs/POSE-ADAPTER-TRANSLATION.md:27`). Type glue; the interface is the translator's reading and was never run against the real adapter (`docs/BACKEND-OPS-TRANSLATION.md:214-215`) | ADAPTER |
| controller (`TfpBackendApi`, `src/controller.h:40-52`) | `snapshot`, `player_snapshot`, `camera_only_weapon`, `prepare_pose`, `enter_pose`, `pose_status`, `release`, `set_pose_transition` | 8 | | `tfp_ops_*` of the same names; "a field-for-field copy" not yet written (`docs/BACKEND-OPS-TRANSLATION.md:154`) | ADAPTER |
| | `presentation_transition`, `clock` | 2 | | runtime entries (`docs/RUNTIME-TRANSLATION.md:121`) | ADAPTER |

Two translations of the snapshot now exist: `src/snapshot.c` (`tfp_snapshot`, used by `action_state.c`, `locomotion.c` and the probe) and `tfp_ops_snapshot` in `src/backend_ops.c`. `docs/MOTION-MODULES-TRANSLATION.md:308` leaves the choice to "whoever joins the parts". Bind everything to `backend_ops` and keep `snapshot.c` for the probe, or the frame takes two different snapshots.

### 1.4 Engine script functions (head visibility, arms, vignette, depth of field, movement)

Mechanism for all rows: `eng_call` through the donor bridge. What has to be written once: a table that gives each opaque handle (`TfpEngineUnit`, `TfpArmUnit`, the vignette's `uint64_t`) an `EngRef` and back, valid for one `eng_state_generation`; `eng_temp_begin` / `eng_temp_end` around the frame (`src/engine.h:83-86`; the donor does it at `src/mod.c:134-142`); and filling each table once, because head visibility compares the nine pointers between frames (`docs/VISIBILITY-TRANSLATION.md:52`). "Donor call" = the donor called this function in the game through this bridge (the 0.5.34 port ran in the game, `docs/PROVENANCE.md:9`).

| Unit (struct, header) | Pointer | n | Source | Status |
|---|---|---|---|---|
| head visibility (`TfpEngineIface`, `src/engine_iface.h:24-47`) | `Application_main_world` | 1 | donor call `src/models.c:681`, `src/effects.c:224`; census 282 | ADAPTER |
| | `World_units` | 1 | donor call `src/models.c:697` (`"r>r"`), then `eng_len` / `eng_index`; up to 131,072 handles | ADAPTER |
| | `Unit_set_mesh_visibility` (4 arguments, pass name) | 1 | donor call `src/models.c:277` (`"ribs"`); census 2150 = `exe+0x4128e0` | ADAPTER |
| | `Unit_has_mesh`, `Unit_mesh` (by name) | 2 | donor calls `src/models.c:326`, `:329` | ADAPTER |
| | `Mesh_name`, `Mesh_node`, `Mesh_num_materials`, `Mesh_has_material` | 4 | donor calls `src/models.c:331-334` | ADAPTER |
| arms (`TfpArmEngine`, `src/arm_engine_iface.h:36-119`) | `Application_main_world` | 1 | as above | ADAPTER |
| | `World_link_unit` | 1 | census 132 (`exe+0x3eb860`). No donor or sibling ever called it on this build (not found in `src/*.c`, `BP/photo-mode/src/engine_api.c`, the First Person Controller). Argument order `(world, child, child_node, parent, parent_node)` is retail's (`docs/ARMS-TRANSLATION.md:45`) and unproven here | ADAPTER |
| | `Unit_alive`, `Unit_num_scene_graph_items`, `Unit_world_pose`, `Unit_world_position`, `Unit_world_rotation`, `Unit_set_local_pose` | 6 | census 2196, 2184, 2124, 2132, 2222, 2147. Not called by this repository's donor; photo-mode calls `alive`, `num_scene_graph_items`, `world_pose`, `world_position`, `set_local_pose` (`BP/photo-mode/src/engine_api.c:13-280`); `world_rotation`: no caller found | ADAPTER |
| | `Unit_num_meshes`, `Unit_mesh` (by index), `Unit_set_mesh_visibility` (3 arguments) | 3 | census 2220, 2212, 2150 | ADAPTER |
| | `Mesh_visibility`, `Mesh_num_materials` | 2 | census 1039, 1034 | ADAPTER |
| | the 27 value functions: `Vector3_call`, `_length`, `_dot`, `_cross`, `_normalize`, `_lerp`, `_x`, `_y`, `_z`, `_op_add`, `_op_sub`, `_op_mul`, `_op_unm`; `Quaternion_identity`, `_axis_angle`, `_multiply`, `_rotate`, `_lerp`, `_inverse`, `_to_elements`, `_from_elements`; `Matrix4x4_identity`, `_from_quaternion_position`, `_multiply`, `_inverse`, `_translation`, `_rotation` | 27 | All registered (census 949-992, 1161-1189, 1473-1508). `eng_call` converts Vector3 and Quaternion to floats (`v`, `q`); a Matrix4x4 only as a reference (`Matrix4x4.to_elements` / `from_elements`, census 1472, 1501, would give floats). A choice for the binder: call the engine for each (exact engine arithmetic, several hundred protected calls a frame while grips run), or compute in C (cheap; the engine's own conventions, e.g. `Quaternion.multiply(second, first)`, `docs/ARMS-TRANSLATION.md:92`, and its last bits are then the binder's to match). Not decided here | ADAPTER |
| (`TfpArmBackend`, `src/arm_engine_iface.h:123-136`) | `object`, `read`, `ptr`, `u32` | 4 | `tfp_ops_alignment_context`, `tfp_ops_read`, `tfp_ops_ptr`, `tfp_ops_u32`; addresses are doubles here | ADAPTER |
| | `clock` | 1 | runtime clock | ADAPTER |
| vignette (`TfpVignetteHost`, `src/vignette.h:64-98`) | `stingray_api` | 1 | seven binding tokens: `eng_cfunction(path)` gives a stable identity (`src/engine.h:43`) | ADAPTER |
| | `application_main_world`, `world_entities`, `entity_manager_render_data_component`, `entity_manager_alive`, `render_data_component_instances`, `render_data_component_get_property`, `render_data_component_set_property` | 7 | donor calls `src/effects.c:224`, `:267`, `:258`, `:145`, `:121`, `:164-174`, `:183` | ADAPTER |
| | `clock`, `log` | 2 | runtime clock; `hf->log` | ADAPTER |
| | `compat_foreign`, `backend_foreign_camera`, `active_view`, `backend_player_snapshot`, `controller_result`, `controller_is_enabled` | 6 | camera-compat unit, runtime entries `foreign_camera` / `active_view`, `tfp_ops_player_snapshot`, `TfpController` | ADAPTER |
| depth of field (`TfpDofHost`, `src/dof.h:48-90`) | `application_binding` | 1 | three tokens via `eng_cfunction` (donor `src/effects.c:215-216`) | ADAPTER |
| | `application_render_setting`, `application_set_render_setting` | 2 | donor calls `src/effects.c:50`, `:61`; census 238 (`exe+0x3fa3d0`), 267 (`exe+0x3fa120`) | ADAPTER |
| | `backend_renderer_dof_value`, `backend_alignment_executable`, `backend_read` | 3 | `tfp_ops_renderer_dof_value`, `tfp_ops_alignment_executable`, `tfp_ops_read` | ADAPTER |
| | `backend_foreign_camera`, `active_view`, `fallback_ready`, `clock` | 4 | runtime entries `foreign_camera`, `active_view`, `dof_fallback_ready`, `clock` (`docs/RUNTIME-TRANSLATION.md:144`) | ADAPTER |
| | `controller_is_enabled`, `controller_result` | 2 | `TfpController` | ADAPTER |
| | `vignette_update`, `_stop`, `_shutdown`, `_is_clean`, `_result`, `_status` | 6 | `tfp_vignette_*`; the tri-state / bool glue between the two units is untested (`docs/EFFECTS-MODULES-TRANSLATION.md:284`) | ADAPTER |
| | `verify` | 1 | not passed on the shipped path | NULL | READY |
| | `log` | 1 | | `hf->log` | READY |

`backend_profile_pose` of the depth-of-field host is a field, not a pointer (`docs/EFFECTS-MODULES-TRANSLATION.md:74`): true.

### 1.5 Head visibility, model shadows (the non-engine half)

| Unit (struct, header) | Pointer | n | Source | Status |
|---|---|---|---|---|
| head (`TfpHeadIo`, `src/head_visibility.h:24-59`) | `read`, `ptr`, `alignment_executable`, `player_snapshot`, `pin_player_snapshot`, `backpack`, `action_state`, `on_foot` | 8 | `tfp_ops_read`, `_ptr`, `_alignment_executable`, `_player_snapshot`, `_pin_player_snapshot`, `_backpack`, `_action_state`, `_on_foot`; typed glue | ADAPTER |
| | `foreign_camera`, `fallback_ready`, `pinned_ready`, `full_body`, `hide_torso`, `show_torso`, `seated_head` | 7 | runtime entries of the same names (`docs/RUNTIME-TRANSLATION.md:139`) | ADAPTER |
| | `userdata_address` | 1 | `eng_pointer(ref)` (`src/engine.h:66`: a Unit handle encodes `(registry_ref << 2) \| 1`, which is retail's "reference times 4 plus tag bits", `docs/VISIBILITY-TRANSLATION.md:65`). For a Mesh and a World it must be the mesh object and the block that holds the native world pointer; `lua_touserdata` on this build's handles was never compared with that (`docs/VISIBILITY-TRANSLATION.md:189`): log the three once in step 5 | ADAPTER |
| | `clock` | 1 | `GetTickCount64()/1000` | ADAPTER |
| | `controller_result_is_pose_offset`, `controller_is_enabled` | 2 | `TfpController` | ADAPTER |
| | `log` | 1 | `hf->log` | READY |
| model shadows (`TfpModelShadowIo`, `src/model_shadow_source.h:44-57`) | `snapshot`, `alignment_context`, `read`, `ptr`, `u32`, `u8`, `descriptor`, `foreign_camera` | 8 | `tfp_ops_snapshot`, `_alignment_context`, `_read`, `_ptr`, `_u32`, `_u8`, `_descriptor`, entry `foreign_camera` | ADAPTER |

### 1.6 Motion

| Unit (struct, header) | Pointer | n | Source | Status |
|---|---|---|---|---|
| motion runtime (`TfpMotionRuntimeHost`, `src/motion_runtime.h:63-119`) | the four live fields (controller lease, alignment lease, weapon key, placement revision) | 4 | `TfpController.lease`, `TfpOps.alignment_lease`, `.placement_weapon_key`; the revision is a counter the binder owns because `TfpOps` has none (`docs/RUNTIME-TRANSLATION.md:110`) | ADAPTER |
| | `clock`, `options`, `action_placement`, `action_mapping`, `presentation_transition`, `presentation_action`, `presentation_resume` | 7 | runtime entries `clock`, `motion_options`, `action_placement`, `action_mapping`, `presentation_*` (`docs/RUNTIME-TRANSLATION.md:146`) | ADAPTER |
| | `log` | 1 | `hf->log` | READY |
| | `controller_result`, `controller_is_enabled` | 2 | `TfpController` | ADAPTER |
| | `controller_get_placement`, `controller_set_placement` | 2 | **no C exists** (finding 1). Retail: `KIT/src/first_person/controller.lua:102-135` | MISSING |
| | `set_pose_motion`, `set_ads_placement`, `player_snapshot` | 3 | `tfp_ops_set_pose_motion`, `_set_ads_placement`, `_player_snapshot` | ADAPTER |
| | `editor_is_active`, `editor_preview_rotation` | 2 | no editor in C; NULL is "no editor" (`docs/MOTION-RUNTIME-TRANSLATION.md:42`), which is correct until the editor exists | MISSING |
| | `sample` | 1 | `tfp_locomotion_sample` (`src/locomotion.h:89`) with a `TfpLocomotionBackend` | ADAPTER |
| | `to_parent` | 1 | NULL = `tfp_locomotion_to_parent` | READY |
| action state (`TfpActionStateBackend`, `src/action_state.h:31-39`) | `player_snapshot`, `alignment_context` | 2 | `tfp_action_state_backend()` fills both from this repository's own functions (`src/action_state.h:42`); or bind to `tfp_ops_*` (see the note under 1.3) | READY |
| locomotion (`TfpLocomotionBackend`, `src/locomotion.h:48-55`) | `player_snapshot`, `alignment_executable`, `action_state`, `on_foot` | 4 | `tfp_locomotion_backend()` (`src/locomotion.h:60`) | READY |

The motion runtime keeps lease addresses across updates; a lease must stay readable, and its address must not be reused, until the first update after it stopped being the controller's (`docs/MOTION-RUNTIME-TRANSLATION.md:108`).

### 1.7 Aim markers, movement, native aim option

| Unit (struct, header) | Pointer | n | Source | Status |
|---|---|---|---|---|
| aim markers (`TfpAimMarkersHost`, `src/aim_markers.h:56-98`) | `backend_module_identity`, `backend_read`, `backend_ptr`, `backend_u32`, `backend_u8`, `backend_snapshot`, `backend_pin_player_snapshot`, `backend_pose_context` | 8 | `tfp_ops_module`, `_read`, `_ptr`, `_u32`, `_u8`, `_snapshot`, `_pin_player_snapshot`, `_pose_context` | ADAPTER |
| | `backend_windows` | 1 | the Wine flag | ADAPTER |
| | `windows_virtual_query` | 1 | VirtualQuery into the 48-byte region | ADAPTER |
| | `game_set_widget_shown` | 1 | call the game's `void(uintptr_t widget, uint8_t shown)` under SEH | `fp_markers_native` (`src/markers_native.c:8-17`) is this call with the address fixed at `game+0x144cfb0`; it has to take the address as an argument | ADAPTER |
| | `setter` | 1 | not on the shipped path | NULL | READY |
| | `foreign`, `pinned_ready`, `fallback_ready` | 3 | runtime entries `foreign_camera`, `pinned_ready`, `fallback_ready` | ADAPTER |
| | `controller_is_enabled`, `controller_pose_offset` | 2 | `TfpController` | ADAPTER |
| | `settings`, `save`, `global_setting`, `save_global` | 4 | runtime entries `markers_settings`, `markers_save`, `markers_global_setting`, `markers_save_global`, which read and write the settings store (1.9) | ADAPTER |
| | `log` | 1 | `hf->log` | READY |
| movement (`TfpMovementHost`, `src/first_person_movement.h:43-89`) | `backend_player_snapshot`, `backend_pin_player_snapshot`, `backend_read`, `backend_ptr`, `backend_u32`, `backend_action_state`, `backend_on_foot`, `backend_foreign_camera` | 8 | `tfp_ops_*`, entry `foreign_camera` | ADAPTER |
| | `windows_virtual_query`, `windows_write_process_memory` | 2 | Win32 | ADAPTER |
| | `keyboard_button_index`, `keyboard_button_id`, `keyboard_button` | 3 | `eng_call("Keyboard.button_index", "s>n")` etc.; census 1324, 1323, 1332. Never called on this build by the donor, which read the S key with `GetAsyncKeyState('S')` (`src/mod.c:141`). The module also takes the identity of the `Keyboard` table as a field (`docs/EFFECTS-MODULES-TRANSLATION.md:129`) | ADAPTER |
| | `window_mouse_focus`, `window_show_cursor` | 2 | donor calls `src/mod.c:125-131` | ADAPTER |
| | `controller_is_enabled`, `controller_result_pose_offset`, `deps_fallback_ready` | 3 | `TfpController`, entry `fallback_ready` | ADAPTER |
| | `controller_focus_owned`, `deps_writable`, `deps_write` | 3 | Fird only / not passed on the shipped path | NULL | READY |
| | `log` | 1 | `hf->log` | READY |
| native aim option (`TfpNativeAimHost`, `src/native_ads_option.h:36-62`) | `backend_snapshot`, `backend_player_snapshot`, `backend_pin_player_snapshot`, `backend_read`, `backend_ptr`, `backend_u32`, `backend_u8`, `backend_map`, `backend_foreign_camera` | 9 | `tfp_ops_*` (`tfp_ops_map` for the map) | ADAPTER |
| | `memory_copy` | 1 | write one byte of the game's aim preference (retail: kit `src/memory.lua` writer) | `fp_private_byte` (`src/private_byte.c:9-24`) is a guarded byte CAS on private memory; it depends on the dead `preferences.h` and has to be lifted out. Or `hf->write` | ADAPTER |
| | `enabled` | 1 | the menu switch `true_native_ads` | entry `native_aim_enabled` → settings store (default off) | ADAPTER |
| | `foreign`, `controller_is_enabled`, `controller_result` | 3 | entry `foreign_camera`, `TfpController` | ADAPTER |

### 1.8 Camera sharing with Photo Mode (`TfpCameraCompatHost`, `src/camera_compat.h:48-74`)

Retail looks at two Lua globals of the retail Photo Mode (`DBF_PHOTO_CAMERA_OWNERSHIP`, `DBF_PHOTO_ACTIONCAM_COMPAT`), at `package.loaded['mods/actioncam/actioncam']`, and patches an upvalue named `input_reader` (`docs/EFFECTS-MODULES-TRANSLATION.md:155-169`). On HellForge the photo mod is a C mod (`goose.photo-mode`); whether it exposes anything for camera ownership was deferred to milestone 4 by the design (`docs/superpowers/specs/2026-11-16-c-rewrite-design.md:89`). I did not find such an interface (`BP/photo-mode/src/engine.h`, `photo.c` were only searched for the tick and input, not for this).

| Pointer | n | Source | Status |
|---|---|---|---|
| `photo_camera_ownership`, `photo_actioncam_compat`, `actioncam_loaded`, `table_field`, `debug_getupvalue`, `set_input_blocked`, `previous_reader_call` | 7 | Nothing known on HellForge. Until something exists: answer "nil / not loaded", which is retail with neither mod installed. To find: what `goose.photo-mode` can export, or whether an HF-LLL Lua mod sets these globals in the game's VM (they could then be read with `eng_field`) | MISSING |
| `controller_is_enabled` | 1 | `TfpController.enabled` | ADAPTER |
| `log` | 1 | `hf->log` | READY |

### 1.9 The runtime (`TfpRtHost`, `src/tfp_runtime_host.h:51-387`; 292 pointers)

Binding is by `docs/RUNTIME-TRANSLATION.md:96-186`; the "Mismatch" column there is the adapter's work and is not repeated. Pointer names are the header's.

| Group (header lines) | Pointers | n | Binds to | Status |
|---|---|---|---|---|
| backend (55-71) | `backend_begin_frame`, `_end_frame`, `_keep_aim_view`, `_detach_seated_camera`, `_pose_status` | 5 | `tfp_ops_*` | READY |
| | `backend_new`, `_pin_player_snapshot`, `_player_snapshot`, `_snapshot`, `_ptr`, `_read`, `_alignment_context`, `_prepare_pose` | 8 | `tfp_ops_*` with the conversions of `docs/RUNTIME-TRANSLATION.md:100-107` (snapshot to 19 fields with a stable `ref`; double addresses) | ADAPTER |
| | `backend_set_pin_first_person`, `_set_camera_update_token`, `_set_action_placement_revision`, `_set_fird_camera_offset` | 4 | no such field in `TfpOps` for the first three: binder-owned variables (`docs/RUNTIME-TRANSLATION.md:109-111`) | ADAPTER |
| helper (73-74) | `helper_depth_stats`, `helper_input_stats` | 2 | `tfp_pose_adapter_depth_stats`, `_input_stats` via the lease | ADAPTER |
| controller (76-89) | `controller_new`, `_update`, `_stop`, `_suspend`, `_retry_cleanup`, `_shutdown`, `_is_clean` | 7 | `tfp_controller_*` (`src/controller.h:74-81`) | READY |
| | `controller_is_enabled`, `_result`, `_start` | 3 | struct fields; no `result()`, no `(seconds, mode)` (`docs/RUNTIME-TRANSLATION.md:124-126`) | ADAPTER |
| | `controller_get_placement`, `_set_placement`, `_placement_target`, `_status` | 4 | **none** (`docs/RUNTIME-TRANSLATION.md:127`). To write: a translation of `controller.lua:102-135` | MISSING |
| editor (91-97) | `editor_new`, `_update`, `_is_active`, `_cancel`, `_shutdown`, `_request_open`, `_status` | 7 | **none**; stubs until `placement_editor.lua` is written for the Overlay Host (`docs/RUNTIME-TRANSLATION.md:177`) | MISSING |
| menu (99-108) | `menu_new`, `_begin_shutdown`, `_update`, `_shutdown`, `_native_ads`, `_ads_mode`; optional `_fird_person_arms`, `_show_torso`, `_near_options`, `_motion_options` | 10 | **none**; `src/ui.c` is the 0.5.34 menu. In retail `menu:update()` is also where F11 / F8 are polled (`docs/RETAIL-SMOOTH66-INVENTORY.md:327`): the stand-in must poll the keys and call the entries `toggle_pose` etc. | MISSING |
| fov (110-117), blur (119-126) | eight each | 16 | `tfp_fov_lease_*`, `tfp_blur_lease_*` | READY |
| camera (128-148) | `camera_new`, `_stop`, `_update`, `_result`, `_is_clean`, `_shutdown`, `_preflight`, `_status`, `_prepare` | 9 | `tfp_camera_adapter_*` | READY |
| | `camera_set_handoff`, `_set_cut_hold` | 2 | same, values to numbers | ADAPTER |
| | optional `camera_is_pinned`, `_look_stats` | 2 | NULL | READY |
| | `camera_observe_parent`, `_free_aim`, `_profile`, `_profile_stats`, `_kick_stats`, `_parent_stats`, `_handoff_stats`, `_free_aim_stats` | 8 | stub: the helper features behind them were dropped as retired (`docs/CAMERA-HELPER-TRANSLATION.md:86-95`) | READY |
| compat (150-157) | eight | 8 | `tfp_camera_compat_*` | READY |
| model shadows, head (159-167) | `model_shadows_new`, `head_new`, `_stop`, `_update`, `_result`, `_is_clean`, `_shutdown` | 7 | `tfp_model_shadow_init`, `tfp_head_*` | READY |
| | `head_sdk_handles` | 1 | the adapter owns one `TfpArmHandles` (`docs/RUNTIME-TRANSLATION.md:140`) | ADAPTER |
| movement (169-174) | `movement_new`, `_stop`, `_result`, `_is_clean`, `_shutdown` | 5 | `tfp_movement_*` | READY |
| | `movement_update` | 1 | extra `skip` argument | ADAPTER |
| arm pieces, grip hand (176-186) | `*_stop`, `*_is_clean`, `*_result` | 6 | `tfp_arm_pieces_*`, `tfp_grip_hand_*` | READY |
| | `*_new`, `*_update` | 4 | snapshot behind the ref to `TfpArmSnapshot` | ADAPTER |
| dof (188-194), near (196-202), motion (204-208), native aim (210-216) | all | 26 | `tfp_dof_*`, `tfp_near_lease_*`, `tfp_motion_runtime_*`, `tfp_native_aim_*` | READY |
| aim markers (218-228) | `aim_markers_source`, `_new`, `_shutdown`, `_stop`, `_update`, `_is_clean`, `_setting`, `_result` | 8 | `tfp_aim_markers_*` | READY |
| | `aim_markers_target`, `_toggle`, `_set_setting` | 3 | values to texts / booleans | ADAPTER |
| recovery (230-241) | `recovery_new`, `_reset`, `_owner_reset`, `_has_policies`, `_needs_proof`, `_success`, `_cleanup` | 7 | `tfp_recovery_*` (policy name text to `TfpPolicy`) | READY |
| | `recovery_action`, `_revalidate`, `_failure`, `_snapshot`, `_blocks_view` | 5 | `docs/RUNTIME-TRANSLATION.md:155-159`; `snapshot` has no C function, build it from `TfpRecovery.rows[]` | ADAPTER |
| diagnostics (243-247) | `diagnostics_new`, `_clear`, `_snapshot` (stub, debug) | 3 | `tfp_diagnostics_init`, `_clear` | READY |
| | `diagnostics_event`, `_record` | 2 | copy into the 74-field sample by name | ADAPTER |
| transition (249-255) | `transition_new` | 1 | `tfp_transition_init` | READY |
| | `transition_native_ads`, `_result`, `_update`, `_reset`, `_action`, `_resume` | 6 | `docs/RUNTIME-TRANSLATION.md:165-167` | ADAPTER |
| actions (257-261) | `actions_weapon_info` | 1 | `tfp_action_weapon_info` | READY |
| | `actions_configure_catalogue`, `_resolve`, `_saved`, `_default_placement` | 4 | `tfp_action_*`; the catalogue rows are `src/action_placement_data.h`; `resolve` / `saved` need the saved placements (settings store) | ADAPTER |
| head positioner (263-272) | ten | 10 | stub: only the editor's parked camera mode and Fird Person reach them (`docs/RUNTIME-TRANSLATION.md:179`) | READY |
| Fird Person, ready stance, held fire, capture gate (274-341) | all the pointers of these groups | 57 | stubs; called only through the debug entries `start_fird` / `probe_fird` (`docs/RUNTIME-TRANSLATION.md:180`; non-goal) | READY |
| mod (343-344) | `mod_log` | 1 | `hf->log` | READY |
| | `mod_set` (optional) | 1 | **none**: the settings store. `src/settings.c` holds the 0.5.34 keys (`toggle_key`, `editor_key`, `native_ads`, … at `src/settings.c:75-77`), not retail's eleven (`docs/RUNTIME-TRANSLATION.md:182`) | MISSING |
| clock (346) | `clock_now` | 1 | `eng_call("Application.time_since_launch", ">n")`; census 274 (`exe+0x3fbef0`). Not called by the donor on this build; photo-mode lists it (`BP/photo-mode/src/engine_api.c`). Must not be NULL after `on_enable` (`docs/RUNTIME-TRANSLATION.md:184`) | ADAPTER |
| mouse (348-349) | `mouse_button_index`, `mouse_button` | 2 | `eng_call("Mouse.button_index", "s>n")`, `("Mouse.button", "n>n")`; census 1407, 1416. Never called on this build. NULL for both switches the aim-release one-shot off (`docs/RUNTIME-TRANSLATION.md:185`) | ADAPTER |
| fields: backend (351-357) | `backend_pin_first_person`, `_frame_active`, `_frame_epoch`, `_placement_weapon_key`, `_presentation_pose_epoch`, `_camera_ready` | 6 | `TfpOps` fields; constants false / nil for the first and last | READY |
| | `backend_action_placement_revision` | 1 | the binder's counter | ADAPTER |
| fields: leases (358-361) | `backend_warm_lease`, `_alignment_lease`, `_presentation_pose_lease`, `controller_lease` | 4 | flatten `TfpOpsLease` (`docs/RUNTIME-TRANSLATION.md:113`) | ADAPTER |
| fields: others (362-365) | `camera_retired`, `aim_markers_fault`, `actions_observed` | 3 | nil; `TfpAimMarkers.fault`; `tfp_action_observed` | READY |
| | `actions_actions` | 1 | the editor's row list: none | MISSING |
| fields: Fird (366-376, 383) | eleven `fird_*` fields, `ref_field` | 12 | stubs (non-goal) | READY |
| fields: profile (377-379) | `profile_body_sleeves`, `profile_grip_hand`, `profile_seated_head` | 3 | retail's `profile.lua` camera section has all three true (`docs/RETAIL-SMOOTH66-INVENTORY.md:1607` onward); `src/profile.h` has no such names (`docs/RUNTIME-TRANSLATION.md:183`): add three constants | ADAPTER |
| fields: config (380-382) | `config_table`, `config_field`, `config_action_mapped` | 3 | **none**: the settings store | MISSING |
| fields: global (384-386) | `global_get`, `global_set` | 2 | a flag in the binder | ADAPTER |

The binder cannot include `controller.h` and `recovery.h` in one file as they are: both define `TFP_PHASE_WAITING` (`docs/RUNTIME-TRANSLATION.md:128`).

### 1.10 Totals

| | READY | ADAPTER | MISSING | all |
|---|---|---|---|---|
| Unit hosts (1.1 to 1.8) | 88 | 298 | 11 | 397 |
| Runtime host (1.9) | 208 | 58 | 26 | 292 |
| **All** | **296** | **356** | **37** | **689** |

Of the 208 READY of the runtime host, 87 are stubs of non-goal parts (57 + 12 Fird, 10 head positioner, 8 camera debug). The 37 MISSING are five things: the menu (10), the editor (7 + 2 + 1), the settings store (1 + 3), the controller's placement methods (4 + 2), and the Photo Mode interface (7).

## 2. Build constants still holding a retail value, or open

"Map" says what `tools/build_map.py --to-here` answers for the retail value. Units whose notes say "none open" are listed once at the end.

| | Constant (where) | Retail | Map | Value here, evidence | Status |
|---|---|---|---|---|---|
| A | Look-gain normalisation: 27 names for 20 unknowns, all 0, behind `B_INPUT_ESTABLISHED 0` (`src/pose_helper.c`, constant block; `docs/POSE-HELPER-TRANSLATION.md:104-120`): nine code pins (`B_PIN_2`, `_3`, `_EXP_FNV`, `_6`, `_7`, `_9`, `_EXIT_FNV`, `_12`, `_13`), seven constant addresses (`B_CONST_*`), three steering cells (`B_GAME_NO_AVATAR`, `_HOST_AVATAR`, `_IDLE_AVATAR`), `B_INPUT_DEVICE` | `game+0x111a948`, `0x1114950`, `0x1f8ff70` (1013 bytes), `0x1109640`, `0x110b830`, `0xe69ca0`, exit `0x1dd0` bytes, `exe+0x1f3050`, `0x1f3090`; `game+0x22465c4`, `0x2247894`, `0x2246fb0`, `0x22478c8`, `0x2246774`, `0x2247ab0`, `0x2246900`; `game+0x2e3fcb8`, `0x2e3fa9c`, `0x2e3f0e4`; owner `+0x324` | all in "no value here yet" (`docs/BUILD-DATA-25480438.md:110-266`) or not in the records | None. Leads: `docs/research-0.5.34/native-input-evidence.json`, `NATIVE-INPUT-DEEP-DIVE.md`; the donor pins inside the same functions, `src/native_contract.h:20-25` (`look_stride`, `look_field`, `look_clock`, `look_gain_y`, `look_gain_x`, `look_gain_table`); the evaluator at `game+0x1286330` loads the seven numbers | MISSING |
| A' | Consequence for wiring: `TfpPoseAdapterProfile.normalize_zoom_input` is true in retail (`docs/POSE-ADAPTER-TRANSLATION.md:25`), so the adapter calls `tfp_pose_begin_input`, which installs the UnitAPI+0x90 hook and the input variant of slot 3 although the code proof can never pass here (`docs/POSE-HELPER-TRANSLATION.md:183`) | | | Either wire the two assembly thunks (1.1) and carry two hooks that do nothing, or set the profile switch off for this build and record it in `docs/BUILD-DIFFERENCES.md`. Scoped weapons outside the sights keep the game's zoomed look speed either way | decision |
| A'' | The seven established input pins' FNV hashes (`docs/POSE-HELPER-TRANSLATION.md:97-101`), `B_GAME_CLOCK` `0x3326348` (`:187`), `B_WORLD_ENTITY_AVATAR` `0xf32f20` (`:188`) | | partly | computed from the image, never compared with the running game; used by the refused path only | READY, unverified live |
| B | `TFP_DOF_GET_THUNK`, `TFP_DOF_SET_THUNK` and their 8 bytes each (`src/dof.h:21-22`, used at `src/dof.c:109-112`) | `exe+0x3d43d0` `488bd1e908fdffff`, `exe+0x3d3ff0` `488bd1e988faffff` | `0x3fa3d0`, `0x3fa120` | `src/feature_contract.h:26-27` with this build's bytes `488bd1e938feffff`, `488bd1e998fbffff`; the same in `docs/BACKEND-OPS-TRANSLATION.md:101`; census 238, 267 | ADAPTER (edit the two numbers and the two byte strings) |
| C | Aim-marker setter `TFP_AIM_MARKERS_SET_SHOWN_RVA` (`src/aim_markers.h:24`) and its 32-byte fingerprint (`src/aim_markers.c:47-48`) | `game+0x12e4e20` | UNKNOWN (added on 26568081) | `game+0x144cfb0`: `src/feature_contract.h:17` holds the same 32 leading bytes; the donor called it there (`src/markers_native.c:12`); photo-mode names it `game.hud_set_visible` (`BP/photo-mode/src/witness.c`) | ADAPTER |
| C | Three more fingerprints (`src/aim_markers.c:49-51`) | `game+0x1635ccf`, `0x1635d61`, `0x117c88d` | UNKNOWN | `game+0x17b48df`, `0x17b4971` (same first 7 bytes), `0x12e98ed` (bytes differ: `…8ff8aa1900e837af4c00…97f8aa1900`) at `src/feature_contract.h:18-20` | ADAPTER |
| C | HUD root global and crosshair offset (`src/aim_markers.c:75`, `:78`) | `game+0x2e28788`, `+0x19a678` | not in the records | `game+0x346d518`, `+0x19aaf8` (`src/markers.c:8`, `:11`; the second also inside the `0x12e98ed` bytes). `+0x24e334`, `+0x24e340`, `+0x820`, `+0x110`, `+0x220` are the same (`src/markers.c:9-19`) | ADAPTER |
| C | State global and scene field (`src/aim_markers.c:70`, `:72`) | `game+0x2ce1508`, `+0xac29c` | `0x3326340`, `0xac21c` | `src/profile.h:21-22` | ADAPTER |
| D | Six locomotion anchors with their bytes (`src/first_person_movement.c:36-48`) | `game+0x9de28c`, `0x9de309`, `0x829b92`, `0x9dd574`, `0x9dc7db`, `0x839073` | `0x9cc2ec`, `0x9cc369`, `0x81ce72`, `0x9cb5d4`, `0x9ca83b`, `0x82c1e3` | `src/feature_contract.h:11-16` with this build's bytes (anchor 0 and anchor 5 differ from retail's) | ADAPTER |
| D | Locomotion pool (`src/first_person_movement.c:85`) | `game+0x2ce1eb8` | `0x3326cf8` | donor read it there (`src/preferences.c:51`). The row layout (`rows + index*0xA0 + 0x94`, `docs/EFFECTS-MODULES-TRANSLATION.md:235`) matches the `+0x94` in this build's anchor bytes; the 0xA0 stride is `lea rdx,[rcx+rcx*4]; shl rdx,5` in `feature_contract.h:12` | ADAPTER |
| E | Native aim: player and weapon pool globals (`src/native_ads_option.c:61`, `:71`) | `game+0x2ce1618`, `0x2ce1ea0` | `0x3326468`, `0x3326ce0` | `src/profile.h:14`, `:17` | ADAPTER |
| E | Native aim: three code witnesses (`src/native_ads_option.c:98-100`) | `game+0xa5cff5`, `0xa5cf03`, `0xa5d0b3` | UNKNOWN (added on 26568081) | `game+0xa427b5`, `0xa426c3` (displacement differs), `0xa42873` at `src/feature_contract.h:8-10`. The row `player+0x2ec + index*0x38` is the donor's too (`src/preferences.c:41`) | ADAPTER |
| F | `B_SPRINT_GAIT_ESTABLISHED`, `B_BRIDGE_SPRINT_ESTABLISHED` (`src/locomotion.c:55`, `:59`) | flag byte 9 bit value 4; layer 14 = 3 with layer 3 = 68 and layer 1 = 75 / 78 | not build addresses | Unknown on this build. To find: log the 24 flag bytes and layers 1, 3, 14 while sprinting with a primary, a secondary and on the ship bridge (the probe already logs snapshots, `docs/LIVE-PROBE.md:9-13`). Until then sprint is seen only as layer 3 = 71 (`docs/MOTION-MODULES-TRANSLATION.md:266`, `:305`) | MISSING |
| G | Presentation switch: `B_PRESENTATION_FUNCTION`, nine pins, `_MODE`, `_MODE_STRIDE`, `_ESTABLISHED` (`src/backend_ops.c:79-83`) | `game+0x833440`, `avatar_owner+0x546B20`, `0x1B8` | UNKNOWN (not in the records) | None. Lead: the call after `game+0x127eb1b` (`docs/BACKEND-OPS-TRANSLATION.md:133`). Nothing on the shipped path calls it (`:131`) | MISSING, does not block |
| H | Arm modules, unit object offsets `+0x70`, `+0x98`, `+0xA0`, `+0x1D0` (`src/arm_pieces.c:53`, `:60`, `:62`, `:142`; `src/grip_hand.c:150`, `:207`, `:210`, `:245`) | same numbers | layout, not in the map | The First Person Controller reads the same four on this build in the game (`BP/true-first-person-controller/src/native_camera_source.h:170-190`, `native_poser.h:164-183`); `docs/ARMS-TRANSLATION.md:220` did not check them | READY |
| H | Arm modules, bone name hashes (`src/arm_pieces.c:16-19`, `:26-27`; `src/grip_hand.c:15-18`, `:52`) and rig facts: 63 bones, arm bones 10 / 12 / 13 and 30 / 32 / 34, weapon bone on rig bone 8, wrist-screen mesh = the visible mesh with 2 materials (`docs/ARMS-TRANSLATION.md:229-235`) | as listed | names, not addresses | The 63-bone rig is read on this build (`docs/CAMERA-HELPER-TRANSLATION.md:81`). The hashes, the arm bone numbers, the weapon bone and the mesh rule were not checked on this build (`docs/ARMS-TRANSLATION.md:220`). To check: one log of the rig's and body's hash arrays (object `+0xA0`) in step 6 | MISSING (a live read, no search) |
| I | Head visibility: `tfp_head_build_25480438` (`src/head_visibility.c:380`) | `tfp_head_build_retail` (`:372`) | yes | every field has a value (`docs/VISIBILITY-TRANSLATION.md:75-85`). Three were not read from the image: the bytes of `registry_code` and the two context ids (`:188`). The binder must pass this table, not the retail one, to `tfp_head_new` (`src/head_visibility.h:102`) | READY, three unverified |
| J | Model shadows: `TFP_VIS_APPEARANCE_25480438` (`src/model_shadow_source.h:104`) | `0x546ab0` | `0x546a70` | pass it to `tfp_model_shadow_init` (`:106`) | READY |
| K | Camera helper `L_CONTEXT_BASIS` (context `+0x0C`; also `src/locomotion.c:45`) | same | layout | Only the 0.5.34 port's read (`docs/CAMERA-HELPER-TRANSLATION.md:128`). It decides "pose invalid" and the eye's forward / up. To settle: log 11 floats there once | READY, verify in step 2 |
| K | Camera helper scene test: bridge only with scene 3 (`docs/CAMERA-HELPER-TRANSLATION.md:129`) | 3 | | `action.c` here takes 3 or 4; if 4 is a real on-foot case the head camera refuses with error 18 | READY, watch the log |
| L | Backend ops: backpack slot, dword `+12` of the inventory row (`docs/BACKEND-OPS-TRANSLATION.md:210`; `docs/BUILD-DIFFERENCES.md:13`) | same | by hand | pool `game+0x3326738` used in the game by the Controller; this dword never read here | READY, unverified live |
| M | Action catalogue weapon type ids (`src/action_placement_data.h`; `docs/MOTION-MODULES-TRANSLATION.md:316`) | from the kit as built for 26251122 | no | not checked on this build; menu label and opt-in family defaults only | unverified |
| N | Vignette class id `TFP_VIGNETTE_CLASS 0x35b0f6c2` (`src/vignette.h:16`), blur name hashes (`docs/BLUR-HELPER-TRANSLATION.md:78`), renderer keys `0x06f1d0d3` / `0x72e8f18c` (`docs/BACKEND-OPS-TRANSLATION.md:116`) | same | names | the renderer keys and blur hashes are the donor's on this build (`effects.c`, `blur_policy.c:22-23`); the vignette id was not compared with the donor (not looked up) | READY / unverified |
| O | Runtime diagnostics reads: `+0x18`, `+0x28`, `+16`, `+0x80`, `+0x88`, `5*0x40+0x24`, `5*0x40+0x30` (`docs/RUNTIME-TRANSLATION.md:197`) | 26568081's | no | not checked; the same numbers appear as this build's layout in `docs/CAMERA-HELPER-TRANSLATION.md:76-79`. Diagnostics fields only | READY, unverified |

None open, every value from the map, the image or `profile.h`: camera helper (26 build constants, `docs/CAMERA-HELPER-TRANSLATION.md:62`), fov helper (31, `docs/FOV-HELPER-TRANSLATION.md:61`), near helper (21, `docs/NEAR-HELPER-TRANSLATION.md:56`), blur helper (30, `docs/BLUR-HELPER-TRANSLATION.md:63`), action state (`docs/MOTION-MODULES-TRANSLATION.md:184`), backend ops apart from G (`docs/BACKEND-OPS-TRANSLATION.md:83-118`), the 99 established names of the pose helper (`docs/POSE-HELPER-TRANSLATION.md:46-102`). The probe confirmed 39 pins and the snapshot chain in the running game (`docs/LIVE-PROBE.md:36-45`); `tools/verify_profile.py` does not read the constant blocks of the translated units or `TfpHeadBuild` (`docs/VISIBILITY-TRANSLATION.md:188`), so those rest on their notes.

## 3. Donor files once the translated units are wired

**Dead (delete, with their headers and tests).** These are the 0.5.34 behaviour files (`docs/PROVENANCE.md:11`) and what only they use:
`camera_policy.c`, `pose_policy.c`, `fov_policy.c`, `near_policy.c`, `blur_policy.c`, `visibility_policy.c`, `lifecycle.c`, `runtime.c` / `runtime.h` (not `tfp_runtime.*`), `models.c`, `effects.c`, `markers.c`, `motion.c`, `placement.c`, `action.c`, `correction.c`, `lens.c`, `depth.c`, `presentation.c`, `preferences.c`, `scene.c`, `policy_owner.h`; and `dispatch_lease.c` / `.h`, whose only users are those policies and `slot_platform.h` (the translated helpers keep their own generation and do their own CAS).

Before deleting: the translation notes cite several of these files by line as their evidence for this build (`blur_policy.c:22-64`, `depth.c:179`, `scene.c`, `action.c`, `markers.c:8-19`, `preferences.c:41-51`, `camera_policy.c:472`). Copy the values of section 2 C, D, E first and name commit 9c279df in the notes, or the citations point at nothing.

**Stay.**

| File | Why |
|---|---|
| `engine.c` / `engine.h` | the only way to the engine's script functions (section 0) |
| `backend.c` / `backend.h` | `FpMemory`, `fp_read`, `fp_bytes`, `fp_image`: used by `snapshot.c`, `action_state.c`, `locomotion.c`, `feature_contract.h`, the probe |
| `snapshot.c` | the probe, and the two motion backends unless they are bound to `backend_ops` |
| `slot_atomic.c` | every helper's `cas` |
| `profile.h`, `native_contract.h`, `feature_contract.h` | constants and pins; `feature_contract.h` is the source for section 2 B to E |
| `probe_mod.c`, `probe.c`, `probe_log.c`, `request_log.c`, `smooth_meter.c` | the read-only probe and the session log tools |
| `goose_overlay.h` | the menu (API 8 connects to the installed API 10 host) |

**Stay in another shape.**

| File | Change |
|---|---|
| `slot_platform.c` | `slot_valid` becomes the hosts' `writable_slot`; `fp_slot_read` / `fp_slot_cas` go with `dispatch_lease.h` |
| `native_call.c` | becomes `TfpOpsHost.call` (seven kinds, `args[4]`); it includes the dead `lifecycle.h` today |
| `markers_native.c`, `private_byte.c` | the two guarded native operations (aim-marker setter, one-byte write); each includes a dead header (`markers.h`, `preferences.h`) |
| `mod.c` | rewritten: it drives `FpRuntime`. Its key polling (`:85-97`), focus test (`:25-30`), image check (`:186-192`) and registration order (`:193-213`) carry over |
| `ui.c`, `settings.c` | the 0.5.34 menu and settings; to be replaced by the missing menu, editor and settings store. `settings.c`'s path under `%LOCALAPPDATA%\HellForge\TrueFirstPerson` (`:32-43`) is the file the design says must not be read (`docs/superpowers/specs/2026-11-16-c-rewrite-design.md:95`): the new store needs another file |
| `sdk_inventory.c`, `native_observer.c` | research build only (`TFP_RESEARCH`, `build.sh:17`); not needed by the mod |

## 4. The Windows build

### What `build.sh` has to become

Today (`build.sh:18-21`) it compiles the 0.5.34 module list with

`zig cc -std=c11 -fms-extensions -target x86_64-windows-gnu -shared -O2 -Wall -Wextra -Werror -I SDK -I src … -luser32 -o goose_true_first_person.dll`

(zig from `<HellForge>/tools/zig-linux/zig`, `build.sh:5`). The flags can stay; `-fms-extensions` is what makes `__try` / `__except` and `_ReturnAddress()` work.

New source list:

- the translated units: `camera_helper`, `fov_helper`, `near_helper`, `blur_helper`, `pose_helper`, `camera_adapter`, `fov_adapter`, `near_adapter`, `blur_adapter`, `pose_adapter`, `backend_ops`, `controller`, `recovery`, `transition`, `tfp_runtime`, `motion_math`, `motion_runtime`, `action_select`, `action_state`, `locomotion`, `head_visibility`, `model_shadow_source`, `arm_pieces`, `grip_hand`, `dof`, `vignette`, `aim_markers`, `first_person_movement`, `camera_compat`, `native_ads_option`, `diagnostics`;
- kept plumbing: `backend`, `snapshot`, `engine`, `slot_atomic`, the reshaped `slot_platform` / `native_call`, `smooth_meter`, `request_log`;
- new: the binder (one file per host group is easier to review than one file), the hook wrappers, the settings store, the menu, `mod.c`;
- one assembly file only if input mode is wired (section 2 A'): the two thunks of `native_ads_input.S`. `zig cc` takes a `.S` on the same command line; the one recorded retail command that links the editor does exactly that (`KIT/docs/first-person-zoom-input-validation.json:27`, `zig c++ … native_ads_editor.cpp native_ads_input.S`). The thunks use `.seh_proc` / `.seh_stackalloc`, keep `rcx` / `rdx`, pass `r15`, entry `rsp+8` and the return address, and save no register (`native_ads_input.S:8-18`).

Libraries: `-luser32` (keys, foreground window) and kernel32 (default). `-lm` is not on the line today; the controller sibling links it (`BP/true-first-person-controller/build.sh:54-57`). The translated units call `expf`, `tanf`, `atan2`, `acos` and so on; whether the current line links them without `-lm` was not tried here.

Flags that must not be added: `-ffast-math`, `-ffp-contract=fast`, `-march=native` (`docs/POSE-ADAPTER-TRANSLATION.md:122`, `:128`; `docs/CAMERA-HELPER-TRANSLATION.md:132`; `docs/POSE-HELPER-TRANSLATION.md:191`).

The manifest (`mod/hellforge-mod.json`) keeps id, `sdk: 16`, `game: "25480438"`; version and description change. `mod/probe-manifest.json` is the old 0.1.0 probe text; the live one is `mod-probe/hellforge-mod.json` (`build.sh:13`).

### Not proven on Windows

| Item | State |
|---|---|
| Compiles for `x86_64-windows-gnu` | Stated for the five helpers and `backend_ops` (`docs/CAMERA-HELPER-TRANSLATION.md:5`, `docs/BACKEND-OPS-TRANSLATION.md:12`). For the adapters, effects, arms, motion and runtime units the notes describe native test runs only; a Windows compile of them: not found. `tests/build-windows.sh` builds only 0.5.34 tests |
| Run under Wine | None of the translated units. `tests/run-windows.py:12` lists 0.5.34 tests only |
| Math library last bits | Proven with glibc. MinGW's `expf`, `tanf`, `atanf`, `exp`, `acos`, `atan2`, `sin`, `cos`, `sqrt` against retail's (zig C++ `std::` on MinGW) were not compared (`docs/CAMERA-HELPER-TRANSLATION.md:132`, `docs/FOV-HELPER-TRANSLATION.md:108`, `docs/NEAR-HELPER-TRANSLATION.md:94`, `docs/MOTION-MODULES-TRANSLATION.md:307`, `docs/MOTION-RUNTIME-TRANSLATION.md:107`). The pose helper's `exp` is the game's own at `game+0x21087d0`, input path only |
| `_Thread_local` in the mod DLL | Compiles; native or emulated TLS not looked at (`docs/CAMERA-HELPER-TRANSLATION.md:134`). Camera, fov and blur helpers use it |
| `setjmp` / `longjmp` | `tfp_runtime.c` and `arm_engine_calls.h` raise with `longjmp`; "compiled, not run" with mingw-w64 (`docs/RUNTIME-TRANSLATION.md:192`). A `longjmp` must never cross an `eng_call` (a Lua protected call) or an `__try` frame: the binder's engine wrappers have to return false and let `ARM_DONE` raise afterwards (`src/arm_engine_calls.h:53`) |
| Stack and static size | grip hand keeps about 556 KiB of state and uses about 20 KiB of stack in its lookup (`docs/ARMS-TRANSLATION.md:242`); vignette 256 KiB, the action-state reader about 260 KB. Static storage, not the tick's stack |
| Tick source | `hf->on_game_thread` runs before the game's update. Retail's arm modules rely on "the game's animation runs after the kit's update" and read the previous evaluation's pose (`docs/ARMS-TRANSLATION.md:92`; `docs/RETAIL-SMOOTH66-INVENTORY.md:351`). Where HellForge's tick falls relative to this build's script update, animation and the camera evaluator: not established (`docs/RETAIL-SMOOTH66-INVENTORY.md:354`; `docs/superpowers/specs/2026-11-16-c-rewrite-design.md:110`) |
| Clock | Retail's frame clock is `Application.time_since_launch` and the runtime refuses a clock that is not finite or goes back (`docs/RUNTIME-TRANSLATION.md:184`). The helpers use QPC / `GetTickCount64` on their own. Mixing is retail's own; do not replace the frame clock with QPC |
| Memory reads | Retail's helpers read with `ReadProcessMemory` on their own process, several times per hook call. Under Proton that call goes through the wineserver (the same cost was measured as a cause of input lag on retail in another investigation, memory note "HD2 mouse input lag diagnosis"). `hf->read`'s implementation is not documented (not found); the Controller reads with an SEH-guarded copy instead (`BP/true-first-person-controller/src/native_read.h:8-48`). Measure `hf->read` in step 1 before binding five hooks to it |

### Threads

- Which thread calls the hooked API slots on this build is **not established**: retail's sources never name it (`docs/RETAIL-SMOOTH66-INVENTORY.md:340`, `:354`); the SDK only says a detour runs on its caller's thread, "often a worker" (`SDK/docs/modding/concepts.md:68`).
- What retail does about it, and the translations keep: every hook first tests atomics (armed, return address, watched unit) without a lock and forwards strangers; a matching call takes `policy_lock` (exclusive in camera, fov, blur and pose, shared in near) and re-checks armed and generation; every export takes `control_lock`; End clears `armed`, takes and drops `policy_lock` to drain, then restores the slot only if it still holds its own hook, and reports cleanup-pending otherwise (`KIT/native/first_person/native_ads_editor.cpp:1043`, `:1277`, `:1350-1393`; `native_ads_camera.cpp:749-759`, `:1063-1072`). The pose helper pairs an observation with its consumer by thread id and caller stack (`native_ads_editor.cpp:496`, `:795-823`).
- The binder's part: real SRW locks behind the lock pointers; hook wrappers that touch nothing but their arguments and the helper; no `eng_call`, no `hf->log` formatting cost, no Lua from a hook (the bridge is game-thread only, `src/engine.h:6-7`).
- Overlay draw callbacks run on the render thread (`src/goose_overlay.h:27-30`): the menu must hand values to the tick under a lock, as the donor does (`src/mod.c:64-84`, `:148-157`).
- `HF_EVENT_SESSION_END` may run under the loader lock: save settings only, no engine call (`SDK/hellforge_mod.h:60-65`). There is no unload: a slot still swapped at session end is left to process exit, as in retail.

## 5. Proposed order of wiring

Every step ends in log lines in `mod-goose.true-first-person.log` (session folder under the install's `logs/sessions/`). No install without the user's word; the First Person Controller must be off for every step from 1 on, because its hook sits on the pinned `controller_exit` at `game+0x12b2ae0` and the snapshot then refuses (`docs/LIVE-PROBE.md:32`). The session's `hooks.tsv` shows whether anything else holds a slot.

| Step | Wire | Log line that passes it | Blocked by |
|---|---|---|---|
| 0. Build and load, no game change | New `build.sh` list; `mod.c` skeleton: image check, `ov_on_game_thread` tick, `eng_init`; the five helper hosts set; `tfp_ops_init`; one `tfp_ops_snapshot` per second. No hook, no slot, no key | `Image ok`; `Engine: state caught`; `Snapshot: kind=4 safe=1 …` equal to what the probe printed (`docs/LIVE-PROBE.md:22-26`); the thread id of the tick; time per `hf->read` | nothing MISSING; the Windows compile and `-lm` (section 4) |
| 1. The view | Controller + backend ops + pose helper and adapter in plain mode (`tfp_pose_begin`, input mode off) + camera, fov, near adapters and helpers; transition, recovery, diagnostics; the runtime with stubs for menu, editor, markers, head, arms, effects; F11 by `GetAsyncKeyState` in the tick calling the entry `toggle_pose`; `clock_now` | `Validate ok` for pose, camera, fov, near before the first Begin; on F11 `Enter: kind 6`, `Begin generation N` for each helper; one line per hook the first time it fires, with thread id and return address (they must be `game+0x1288424`, `+0x12887cd`, `+0x12890df`, `+0x1287a44`, `+0x127a139`, `+0x127a120`: the call sites of section 2's notes plus the call lengths); on F11 again `End clean` for each and the four slots back at their pinned values | MISSING: `controller_get_placement` / `set_placement` / `placement_target` / `status` (a default-placement stand-in is enough: `on_enable` needs `set_placement` to answer true); the settings store (stand-in: retail defaults in memory); the menu (stand-in: key poll + option defaults). Open, not MISSING: the hook thread; `hf->read` cost; `L_CONTEXT_BASIS` (K); decision A' |
| 2. Aim-release and seat one-shots, diagnostics rows | `mouse_button_index` / `mouse_button` through the engine; `keep_aim_view`, `detach_seated_camera`; the camera helper's eye and exit holds judged from the diagnostics rows | `Keep aim view` once per button release; `Detach seated camera` once per seat; the 11 floats at context `+0x0C` orthonormal | nothing MISSING; `Mouse.*` never called on this build |
| 3. Scoped blur | blur helper and adapter | `Begin`, a non-zero "zeroed" count in the blur statistics while aiming a scoped weapon outside full sights, `End clean` | nothing |
| 4. Placement and motion | motion runtime, locomotion, action selection, `set_pose_motion`, ADS placement; the real controller placement; saved per-weapon placements | per action change one line (`action=…`, placement, softened); sprint seen on a primary | MISSING: controller placement translation; settings store (weapon placements); sprint bits F for secondaries and the bridge |
| 5. Head, shadows, movement, markers, native aim option | head visibility with `tfp_head_build_25480438`; model shadows; movement (anchors D); aim markers (C); native aim (E); `Keyboard.*` | head: handles found, pass bits written and restored, the three `userdata_address` numbers for a Unit, a Mesh and the World; movement: `anchors ok`; markers: `HUD found`, both widgets hidden and shown again | Constants C, D, E must be edited in (all values known). `userdata_address` unverified. Nothing to search for |
| 6. Arms | arm pieces, grip hand; `World.link_unit`, `Unit.set_local_pose` and the 27 value functions | rig count 63; the eight sleeve and three grip hashes found on rig and body (log the hash arrays once); `linked N pieces`; `result().reason` empty | MISSING: the live check of bone hashes and arm bone numbers (H). Open: `World.link_unit`'s argument order on this build; where the tick falls relative to animation (if arm writes are lost or lag a frame, this is why); the choice engine math or C math |
| 7. Depth of field, vignette | dof with the edited thunk constants (B); vignette | `dof_enabled` read, set false, restored; vignette rows found and restored | Constant B (known) |
| 8. Menu, editor, Photo Mode sharing | Overlay Host panels for every retail setting; F8 editor; settings file; one-time import of the retail values (`docs/superpowers/specs/2026-11-16-c-rewrite-design.md:94`); camera sharing | each setting change logged with its key; editor open / save | MISSING: menu, editor, settings store (all to be written); the Photo Mode interface (to be found or added to `goose.photo-mode`) |
| later | look-gain normalisation (A, 20 values and the two thunks); presentation switch (G) only if Fird Person is ever wanted | | MISSING A, G |

## 6. Not read, or read only in part

- `docs/RETAIL-SMOOTH66-INVENTORY.md`: sections 2.1, the kit frame, the helper table and section 8's first list were read; the per-module detail (397-1148), the address list (1738-2521), settings (2522-2682) and the behaviours list were not.
- `src/backend.c`, `src/engine.c`, `src/ui.c`, `src/settings.c`, `src/probe_mod.c`: read by their function lists and the cited lines, not line by line.
- The HellForge SDK, the sibling mods and the retail helper sources were surveyed by two delegated readers; I checked a sample of their citations myself (`hellforge_mod.h:1246-1251`, `:1348-1364`; both Overlay Host version lines; the 57-line length of `native_ads_input.S`; the installed mods list). Their other citations (`BP/…`, `KIT/native/…` line numbers) are as they reported them.
- `tools/retail_ref/runtime_spec.py` and `src/tfp_runtime.c` were read only where cited.
