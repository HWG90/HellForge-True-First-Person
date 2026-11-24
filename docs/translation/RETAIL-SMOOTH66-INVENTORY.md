# True First Person (F11), Goose Gadget Kit 0.5.41-smooth.66: live inventory

Source read: `~/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit/` at git HEAD `1d0d453` (working tree clean when read, 2026-11-15). Game build 26568081. Nothing was built, run or installed; every statement below comes from reading the source, and `file:line` citations refer to that commit.

Conventions used in this file:

- Paths are relative to the kit folder. `fp/` is short for `src/first_person/` (some tables use the short form). "cpp:" inside the camera section means `native/first_person/native_ads_camera.cpp`.
- **LIVE** = runs on the shipped path (F11 with the shipped `profile.lua` and `config/defaults.cfg`). **CONDITIONAL** = runs only with a named flag, setting or environment condition; the shipped value is given. **PARKED** = not reachable on the shipped path (required nowhere, or only through the debug global `NativeADSCameraTest`, or only through a controller mode nothing starts).
- "kind 6" = the game's native first-person rig camera request; "kind 4" = the game's ordinary third-person camera. `s` = `backend:player_snapshot()` / `backend:snapshot()`.
- How liveness was decided: from what `fp/runtime.lua` requires in `on_enable` (lines 649-730) and calls in `update_frame` (561-640), what `start_pose` / `toggle_pose` start (`controller:start(0,'pose_offset')`, 844), and which objects stay `nil` (`fird`, `fird_entry`, `fird_warm`, `fird_render`, `ready_pose`, `held_fire` are only created inside `start_fird`, 935-1040, whose menu entry is commented out at 1056). File names were not used as evidence.
- The detailed tables were collected per module group; a few cells are long. Where a statement is my reading rather than something the code or its comments say, it is marked as such.
- Retail's Page Up / Page Down depth keys (VK 33/34; section 4: editor VK codes, key speed; section 5: "Editor: PgUp 33 / PgDn 34"; section 7: behaviour 144) are not in the C port: removed in round 17 (2026-11-24) at the user's request, depth is the wheel's alone; only the retail comparison compiles them back in (`TFP_ED_RETAIL_PAGE_KEYS`, tests/run-vectors-placement-editor.sh).

## 0. Result in short

| | files | lines |
|---|---|---|
| LIVE in `src/first_person/` | 28 | 9,676 |
| CONDITIONAL in `src/first_person/` | 5 | 1,296 |
| PARKED in `src/first_person/` | 26 | 4,905 |
| total | 59 | 15,877 |

- Kit files the mode needs outside that folder: `main.lua` (66, update order), `src/first_person.lua` (159, facade: config proxy, menu facade, lifecycle), `src/mcm.lua` (228, embeds the mode's two pages in the kit's menu entry), `src/loadout_catalogue.lua` (`gear` table: weapon names and families for placements), and `src/memory.lua` (389) only for the off-by-default "native ADS" option. `src/backend.lua`, `src/core.lua` and `src/controller.lua` are not used by the mode.
- Helper DLLs loaded on the shipped path: **camera** (`native_ads_camera_26568081_e88ff8dc46b2.dll`), **editor/pose** (`native_ads_editor_26568081_4940b83e17ad.dll`), **fov** (`native_ads_fov_26568081_764664986628.dll`), **blur** (`native_ads_blur_26568081_c18c221e2cc6.dll`), **near** (`native_ads_near_26568081_14a3c38c08f8.dll`). Not loaded: **alignment** (only controller mode `unlinked_sights`, which nothing starts), **heldfire** (Fird Person only), **zoom** (no profile entry, no 26568081 DLL, `zoom.lua` required nowhere). `guarded_reads` belongs to the kit, not the mode.
- All five live helpers work by swapping a function pointer in an engine API table (data slot), with byte and identity checks; none patches code.
- Engine-side work done by Lua directly: mesh visibility passes, linked-bone local poses and re-links for the arm pieces, the weapon root offset during a spin, renderer `dof_enabled`, vignette components, two one-shot memory writes on aim release, one focused-locomotion byte while S is held, three calls into game functions (enter, exit, allowed) plus the seated camera unlink and the aim-marker setter.

## 1. Module map

### 1.1 Compact table

| file | lines | status | role |
|---|---|---|---|
| `fp/runtime.lua` | 1228 | LIVE | Builds every object, owns the per-update order, the debug global `NativeADSCameraTest`, recovery wrapper, diagnostics record, aim-release and seated one-shots, arm module gating. Fird and ready-pose branches inside it are parked. |
| `fp/backend.lua` | 1471 | LIVE (about half; see 1.3) | The mode's own memory reader, snapshots, game calls (enter/exit/allowed), pose lease, one-shot writes, configs for the helpers. |
| `fp/controller.lua` | 589 | LIVE (manual branch) | Session and lease state machine (idle / waiting / running / cleanup_pending). Timed branch only for the menu test mode `native`. |
| `fp/profile.lua` | 126 | LIVE data | Build contract for 26568081 (section 4). `alignment`, `held_fire`, `body_pose.event` entries unused. |
| `fp/compat51.lua` | 217 | LIVE | `cffi` wrapper used by every file. |
| `fp/pose.lua` | 336 | LIVE | FFI adapter of the editor/pose DLL (`NADSE_*`). |
| `fp/camera.lua` | 464 | LIVE | FFI adapter and lease of the camera DLL (`NADSC_*`). |
| `fp/fov.lua` | 316 | LIVE | Adapter and lease of the FOV DLL (`NADSF_*`). |
| `fp/blur.lua` | 289 | LIVE | Adapter and lease of the blur DLL (`NADSB_*`). |
| `fp/near_clip.lua` | 227 | LIVE | Adapter and lease of the near-clip DLL (`NADSN_*`). |
| `fp/action_state.lua` | 129 | LIVE | Reads the body's animation state machine layers from memory. |
| `fp/action_placement.lua` | 139 | LIVE (`family_baseline` conditional, off) | Action selection and placement resolution per weapon key. |
| `fp/motion_runtime.lua` | 408 | LIVE (`capture_neutral` parked) | Per-update placement blend, bob/sway, pop guard, submit to the pose helper. |
| `fp/motion.lua` | 163 | LIVE | Pure bob / sway / sprint-pose math. |
| `fp/locomotion.lua` | 162 | LIVE | Samples root position, bases, sprint; camera-to-parent conversion. |
| `fp/head_visibility.lua` | 767 | LIVE (full-body / pinned branches dead) | Hides head, torso, shoulders, backpack from the camera pass, shadow handling, SDK handle resolution. |
| `fp/model_shadow_source.lua` | 129 | LIVE | Inventory of rig, arm pieces and weapon tree for shadow-off rows. |
| `fp/first_person_movement.lua` | 248 | LIVE (`update_fird` parked) | Focused-locomotion byte while S is held. |
| `fp/dof.lua` | 266 | LIVE | Renderer `dof_enabled` off while the view shows; wraps the vignette. |
| `fp/vignette.lua` | 127 | LIVE | Vignette components off while the view shows. |
| `fp/camera_compat.lua` | 181 | LIVE | Photo Mode / ActionCam camera ownership. |
| `fp/menu.lua` | 442 | LIVE | Pages, hotkeys F11 / F8, option getters. |
| `fp/placement_editor.lua` | 397 | LIVE (weapon mode; camera mode parked) | F8 editor. |
| `fp/ui_style.lua` | 280 | LIVE (only `editor_legacy_tokens`) | Colours for the editor panel; the rest unused. |
| `fp/aim_markers.lua` | 262 | LIVE | Hides crosshair and aiming circle while aiming. |
| `fp/recovery.lua` | 124 | LIVE | Retry ledger for fov, blur, camera, head, movement. |
| `fp/transition.lua` | 105 | LIVE | Shared 0.18 s presentation clock (epoch + progress). |
| `fp/diagnostics.lua` | 84 | LIVE (read only through the debug global) | Per-frame ring buffer, 720 rows. |
| `fp/grip_hand.lua` | 691 | CONDITIONAL: `profile.camera.grip_hand` (true) and menu "Fird Person Arms" (default on) | Hands onto the weapon, finger shapes, spin handling. |
| `fp/arm_pieces.lua` | 284 | CONDITIONAL: `profile.camera.body_sleeves` (true) and menu "Fird Person Arms" (default on) | Sleeve tops, collar, chest of the arm pieces placed as on the body. |
| `fp/native_ads_option.lua` | 93 | CONDITIONAL: menu `true_native_ads` (default off) | Flips the game's aim-mode preference byte while the mode is on. |
| `fp/shared_input.lua` | 197 | CONDITIONAL: `DBFMCM.shared_input_service` present and `DBFMCM.camera_input` absent | Input service consumer for hotkeys and the editor. |
| `fp/input_adapter.lua` | 31 | CONDITIONAL: same condition | Presents shared_input as a `camera_input` API 2 object. |
| `fp/alignment.lua` | 126 | PARKED (mode `unlinked_sights`, never started) | Adapter of the alignment DLL. |
| `fp/held_fire.lua` | 269 | PARKED (Fird natural branch) | Adapter of the held-fire DLL. |
| `fp/zoom.lua` | 181 | PARKED (required nowhere) | Adapter of a zoom DLL that does not exist for this build. |
| `fp/head_placement.lua` | 34 | PARKED in effect (constructed, never enabled) | Head-camera offset store for Fird Person; sets `backend.fird_camera_offset` once. |
| `fp/fird_person.lua` | 827 | PARKED (debug global, natural branch) | Fird Person manager. |
| `fp/fird_projection.lua` | 162 | PARKED (debug global, natural branch) | Render-placement math. |
| `fp/fird_fov.lua` | 142 | PARKED (debug global, natural branch) | FOV hold for Fird. |
| `fp/fird_native_ads.lua` | 79 | PARKED (debug global, natural branch) | Native ADS lease for Fird. |
| `fp/ready_pose.lua` | 312 | PARKED (debug global, natural branch) | Ready-stance owner. |
| `fp/ready_pose_source.lua` | 393 | PARKED (debug global, natural branch) | Calls the engine set-states function. |
| `fp/ready_pose_resources.lua` | 220 | PARKED (debug global, natural branch) | Animation resource proof. |
| `fp/ready_pose_timing.lua` | 156 | PARKED (debug global, natural branch) | Animation batch manager observation. |
| `fp/ready_pose_contract.lua` | 14 | PARKED (debug global, natural branch) | Pins; header still says 26251122. |
| `fp/fird_aim.lua` | 89 | PARKED (render-driven branch, never runs) | |
| `fp/fird_auto.lua` | 189 | PARKED (render-driven branch) | |
| `fp/fird_camera_pose.lua` | 106 | PARKED (render-driven branch) | |
| `fp/fird_capture_ready.lua` | 104 | PARKED (render-driven branch) | |
| `fp/fird_render.lua` | 113 | PARKED (render-driven branch) | |
| `fp/fird_retarget.lua` | 277 | PARKED (render-driven branch) | |
| `fp/fird_source_pose.lua` | 185 | PARKED (render-driven branch) | |
| `fp/fird_warm.lua` | 110 | PARKED (render-driven branch) | |
| `fp/fird_weapon_children.lua` | 156 | PARKED (render-driven branch) | |
| `fp/skinned_placement.lua` | 364 | PARKED (render-driven branch) | |
| `fp/support_hand.lua` | 52 | PARKED (render-driven branch) | |
| `fp/fird_handoff.lua` | 35 | PARKED (required nowhere) | |
| `fp/fird_presentation.lua` | 210 | PARKED (required nowhere) | |
| `main.lua` | 66 | LIVE | Kit entry and update order. |
| `src/first_person.lua` | 159 | LIVE | Facade between kit and mode. |
| `src/mcm.lua` | 228 | LIVE (kit level) | Kit menu registration that embeds the mode's pages. |
| `src/memory.lua` | 389 | CONDITIONAL for the mode (`true_native_ads`, off) | Kit memory layer; the mode uses one `copy()`. |
| `src/backend.lua`, `src/core.lua`, `src/controller.lua` | 387 / 229 / 247 | not used by the mode | Kit unlock machinery. |

Note on Fird Person: `fird_person.native_animation_default=true` (`fp/fird_person.lua:9`), so even through the debug global `start_fird` returns at `fp/runtime.lua:958-964` and everything from 966 to 1039 never runs. Live files still carry hooks for it (`backend.fird_camera_active`, `Backend:fird_fov_config`, `warm_start/warm_finish`, `movement:update_fird`, Fird branches in `aim_markers.lua`, the update-loop branches at `fp/runtime.lua:562-593`). None of these needs porting.

### 1.2 Detailed rows

Columns: file, lines, status (with evidence), what it does, requires, engine SDK functions called, helper DLL exports called.

| file | lines | status | what it does | requires | engine SDK functions | helper DLL exports |
|---|---|---|---|---|---|---|
| `src/first_person/runtime.lua` | 1228 | LIVE (`src/first_person.lua:121`) | See sections 2.1 and 2.2. Parked inside: `ensure_ready_pose` (123-153), `ready_update` body (83-107, no-op while `ready_pose` is nil), `render_fird_pose` (175-183), `start_fird` (935-1040), Fird branches of `update_frame` (569-593, 611-613), pinned branches (`pinned_ready` always false because `camera.is_pinned` is not defined), `start_free_aim` (DLL refuses), `store` camera mode. | `src/first_person.lua` (host), compat51, action_placement, `src/loadout_catalogue.lua` (gear), backend, controller, menu, placement_editor, fov, blur, dof, camera, motion_runtime, camera_compat, head_visibility, first_person_movement, near_clip, recovery, diagnostics, transition, model_shadow_source, arm_pieces, grip_hand, vignette, native_ads_option, aim_markers, head_placement, profile (lazy, 550/556/696) | `stingray.Application.time_since_launch` (160-172), `stingray.Mouse.button_index` / `button_id` ('right', 'middle') and `stingray.Mouse.button` (395-422) | through the pose adapter: `depth_stats` (378), `input_stats` (debug) |
| src/first_person/backend.lua | 1471 | LIVE (about half of it: snapshots, pose prepare/enter/release, placement/motion/transition setters, keep_aim_view, detach_seated_camera, configs for camera/fov/blur, renderer_dof_value). PARKED inside it: warm_start/warm_finish/_invoke_presentation/fird_fov_config/camera_ready (fird only), prepare_alignment/alignment_status/_alignment_helper (mode `unlinked_sights`, never started), prepare_body_pose/release_body_pose/_body_api/_invoke_body (mode `release_body_event`, never started), set_pose_zoom_policy (only caller zoom.lua is required by nothing) | The mode's own process-memory reader (not the kit's): validates game.dll + helldivers2.exe by PE header and byte pins, resolves local player / avatar / body / viewmodel / weapon / camera request by pointer chains, calls the game's enter/exit/allowed camera functions through FFI, owns the pose-helper lease, does the two one-shot memory writes | fp/compat51.lua (PORT51: private cffi `ffi`, `bit`, `kind`, `number`, `jit_util`), fp/profile.lua; lazily fp/action_state.lua (344-349), fp/pose.lua (804-808), fp/alignment.lua (681-685, never on shipped path) | none from stingray.*. Uses kernel32 `GetModuleHandleA`, `GetCurrentProcess`, `ReadProcessMemory`, `VirtualQuery` (48-57), ntdll `wine_get_version` probe (58-62); `jit_util.funcinfo(fn).addr` on the `Application.render_setting` getter/setter closures that dof.lua passes in (737) | via fp/pose.lua adapter (library `native/native_ads_editor_26568081_4940b83e17ad.dll`): `NADSE_ValidateInput` (prepare), `NADSE_ReadStats` (stats), `NADSE_BeginSteadyInput` or `NADSE_BeginInput` (begin; the non-Input variants `NADSE_BeginSteady`/`NADSE_Begin`/`NADSE_Validate` only if `pose.normalize_zoom_input` were false), `NADSE_SetPlacement`, `NADSE_SetADSPlacement`, `NADSE_SetTransform` / `NADSE_SetAuthoredTransform` (set_motion), `NADSE_SetTransition`, `NADSE_End`, `NADSE_Error`; `NADSE_SetZoomPolicy` unreachable. Via fp/alignment.lua (`NADS_Validate/Begin/End/ReadStats/Error`): never on shipped path |
| src/first_person/controller.lua | 589 | LIVE (manual branch `manual_update`, start/stop/suspend/retry_cleanup/set_placement). Timed branch (lines 495-584) only for mode `native` (menu button "Compare native ADS camera (30 seconds)"); modes `release_body_event`, `unlinked_sights`, timed `pose_offset` are never started | Timing and policy only: owns `phase`, the session (`enabled`, `manual`), the single camera lease, entry throttles, the launcher guard hold, ADS-return blend intent, cleanup retries, status text | fp/compat51.lua; backend passed in; clock passed in (`stingray.Application.time_since_launch` wrapper, fp/runtime.lua:160-173, 680) | none | none directly; through backend: pose_status (NADSE_ReadStats) every running update, prepare_pose/enter_pose, release, set_pose_placement, set_pose_transition |
| src/first_person/action_state.lua | 129 | LIVE (called from every full `snapshot()` via `Backend:action_state`, backend.lua:276) | Reads the body unit's animation state machine from copied memory: for chosen layers returns the index of the active state; verifies the state-machine resource is 4D1C334D294DFA97 with 31 layers | fp/compat51.lua; backend (`_read`, `_ptr`, `_u32`, `_alignment_context`, `player_snapshot`) | none (reproduces `animation_get_state` by memory reads) | none |
| src/first_person/action_placement.lua | 139 | LIVE (`resolve`, `select`, `configure_catalogue`, `weapon_info`, `saved`, `default_placement`, `valid`, `actions`, `observed_actions`). `family_baseline` CONDITIONAL on config `family_placement_defaults==true` (absent from config/defaults.cfg, so off as shipped; menu checkbox "Use weapon-family placement defaults", fp/menu.lua:378) | Pure policy: which action name applies to a motion sample, and which saved placement applies to (weapon key, action) | fp/compat51.lua | none | none |
| src/first_person/recovery.lua | 124 | LIVE (instance created fp/runtime.lua:667 with defaults) | Pure retry ledger for the per-frame policies fov, blur, camera, head, movement: classify failure, backoff, proof, `blocks_view` | none | none | none |
| src/first_person/transition.lua | 105 | LIVE (`new({duration=.18,max_gap=.25})` fp/runtime.lua:670; `native_ads` used at fp/runtime.lua:384) | One shared presentation clock: epoch + progress 0..1 packet restarted on weapon/ADS/zoom/shoulder/action/seat/camera edges | none | none | none |
| src/first_person/diagnostics.lua | 84 | LIVE, read-only (capacity 720, fp/runtime.lua:668); output only reachable through debug global `NativeADSCameraTest.diagnostics()` | Ring buffer of per-frame scalar rows + counters | none | none | none |
| src/first_person/profile.lua | 126 | LIVE data (see DATA). PARKED entries: `alignment` (helper never armed), `held_fire` (fird only), `body_pose.event/api offsets` (diagnostic mode never started; but `body_pose.executable.exists/trigger` pins ARE live: they are the executable fingerprint in `_alignment_executable`, backend.lua:716), `camera.handoff` (0/0 = off) | Build contract for 26568081 | fp/compat51.lua (loaded, unused) | none | names every helper DLL file |
| src/first_person.lua | 159 | LIVE | Facade between kit and mode: `host(mod)` config proxy (mode's `mod.config` = kit `config.first_person`, `mod.set(key,v)` rewrites that table); `enable/update/shutdown/poll_cleanup` pcall the runtime; `menu_api()` wraps global `DBFMCM` so the mode's pages register inside the kit's MCM entry with ids prefixed `first_person_`; `sync()` copies label/disabled of the mode's controls to the kit's registered copies each update | fp/runtime.lua (121), fp/input_adapter.lua (34, only when `DBFMCM.camera_input` is missing and `DBFMCM.shared_input_service` is a table) | none itself; passes `stingray` to input_adapter | none |
| src/backend.lua | 387 | LIVE for the kit (unlock catalogue, freezes, objectives). NOT used by the mode: fp/runtime.lua builds its own `fp/backend.lua` with no io (665). Only contact: main.lua calls `backend.mem:begin_frame()` (kit memory) before everything each update | Kit catalogue reader: account records, weapons, kits, stratagems, host-only session context, gameplay fingerprints | src/memory.lua | `stingray.Application.main_world()` (89) | via src/memory.lua (`native/guarded_reads*.dll`, `GGR_*`): not used by the mode's backend |
| src/core.lua | 229 | LIVE for the kit; NOT used by the mode at all | Pure inventory transaction plan/apply/restore journal | none | none | none |
| src/controller.lua | 247 | LIVE for the kit; NOT used by the mode at all (name clash only: the mode's controller is fp/controller.lua) | Kit unlock leases, freezes, unreleased weapons | src/core.lua, src/freezes.lua | none | none |
| main.lua | 66 | LIVE | Kit entry: on_enable creates kit backend/controller/equipment, then `first_person.enable()`; on_update order below; on_disable / on_cleanup_poll call `shutdown` | src/backend.lua, loadout_catalogue, armory_weapons, loadout_parts, controller, equipment, equipment_backend, first_person, spawner, objectives, mcm | none | none |
| `src/first_person/pose.lua` | 1-336 | LIVE (loaded by `backend.lua:804-808` with `Profile.pose`; every F11 lease goes through it) | LuaJIT FFI adapter for the editor helper DLL. Declares the `NADSE_*` ABI (`pose.lua:42-79`), loads `mod.dir/<profile.pose.library>` once and keeps the library object in `mod.scope.native_ads_pose_libraries` (`pose.lua:83-87`). Offers `prepare` (read-only validate, `212-222`), `begin` (arm, returns a handle holding a `uint64_t[1]` generation, `224-249`), `set_placement` (`251-261`), `set_ads_placement` (`262-272`), `set_motion` (xyz + quaternion, `274-298`), `set_zoom_policy` (`300-309`), `transition` (shared phase packet, `311-318`), `stats` (health/counters, counters refreshed at most every 0.1 s, `99-139`), `depth_stats` (`153-180`), `input_stats` (`183-210`), `finish` (End, `322-334`), `owned_handle` (`320`). Translates native error codes 1..18 to text (`pose.lua:9-21`). Range-checks all values before the call (placement within 0.5 m, motion within 0.15 m, unit quaternion with abs(w)/norm >= 0.5). | `src/first_person/compat51.lua` (`PORT51.ffi`, `PORT51.number`, `PORT51.kind`), global `mod` (`mod.dir`, `mod.scope`, `mod.require`) | None from the engine SDK. Windows only: `kernel32!GetTickCount64` bound as `NativeADSPose_StatsTick` (`pose.lua:32-35`), used for the 0.1 s stats throttle because `backend.lua:807` passes no clock. | LIVE: `NADSE_ReadStats`, `NADSE_ValidateInput`, `NADSE_BeginSteadyInput`, `NADSE_BeginInput`, `NADSE_SetPlacement`, `NADSE_SetADSPlacement`, `NADSE_SetTransform`, `NADSE_SetAuthoredTransform`, `NADSE_SetTransition`, `NADSE_End`, `NADSE_Error`, `NADSE_ReadDepthStats` (via `runtime.lua:378`). Debug API only: `NADSE_ReadInputStats` (`runtime.lua:1143-1153`). CONDITIONAL, not reached with shipped values: `NADSE_Validate`, `NADSE_Begin`, `NADSE_BeginSteady` (only when `profile.pose.normalize_zoom_input ~= true`; shipped `true`, `profile.lua:44`). Never: `NADSE_SetZoomPolicy` (only caller is `zoom.lua:117,142`, a module nothing requires), `NADSE_SetMotion` (declared `pose.lua:68`, no caller). Not declared at all in Lua: `NADSE_RotationTrace49`, `NADSE_SetRotationGuard49`, `NADSE_EntryPositionStats50`, `NADSE_SetEntryPosition50`, `NADSE_SetEntryAnimation50`. |
| `src/first_person/camera.lua` | 1-464 | LIVE (created at `runtime.lua:685`; updated every kit update through `managed_update('camera',camera,controller)` `runtime.lua:626`; armed before each native entry through `backend.camera_before_entry` -> `camera:prepare()` `runtime.lua:733-737`, called from `Backend:enter_pose` `backend.lua:674-675`). Inside the file: `Adapter` (12-256) = FFI wrapper; `Camera.new` (260-462) = lease manager with phases `idle` / `waiting` / `active` / `cleanup_pending`. Sub-parts: `set_offsets` CONDITIONAL (only `head_placement.lua:20` when `fird:is_enabled()`, shipped: fird nil -> never; and in `prepare` only when `config.right` is set, shipped nil `backend.lua:387,391`); `set_kick` CONDITIONAL on `config.kick_enabled`, shipped `false` (`backend.lua:390`) -> never; `set_motion` PARKED (no caller anywhere); `free_aim` menu-button-only ("Free aim (30 seconds)" `menu.lua:362-364` -> `runtime.lua:926-934`) and the DLL refuses it; `observe_parent`, `profile`, `profile_stats`, `kick_stats`, `parent_stats`, `set_handoff`, `handoff_stats`, `set_cut_hold`, `free_aim_stats`, `preflight` debug-API-only (`runtime.lua:1115-1124,1196`), except `observe_parent` which the Free aim button also calls (`runtime.lua:932`). | Owns one generation-token lease on the camera helper DLL. `prepare`: asks backend for the camera config, `NADSC_Begin`, then `NADSC_SetPresentation47`, `NADSC_SetHandoff49(0,0)`, `NADSC_SetCutHold50(15)`; `update`: reads stats, detects native disarm, re-arms when the camera identity changed, reports the wielded weapon unit as "weapon key"; `stop`/`shutdown`: `NADSC_End`. All camera math is in the DLL; the Lua side only arms, configures and watches. | `src/first_person/compat51.lua` (line 1), `src/first_person/profile.lua` (`.camera`, line 264); calls on the injected backend: `backend:camera_config()` (369,378,434), `backend:snapshot()` (453); on the controller: `controller:result()`, `controller:is_enabled()` (429-430) | none directly (no `stingray.*`, `Unit.*`, `Camera.*`); only LuaJIT `ffi.cdef` / `ffi.load` / `ffi.new` / `ffi.typeof` / `ffi.sizeof` (16-85) and `mod.dir`, `mod.scope` | Shipped path: `NADSC_Begin` (147), `NADSC_SetPresentation47` (178), `NADSC_SetHandoff49` (184), `NADSC_SetCutHold50` (196), `NADSC_SetWeaponKey50` (203), `NADSC_ReadStats` (108), `NADSC_End` (253), `NADSC_Error` (87, on failure). Not shipped: `NADSC_Validate` (139, debug `prepare_start`), `NADSC_SetOffsets` (158), `NADSC_FreeAim` (163), `NADSC_Profile47` (168), `NADSC_ProfileStats47` (173), `NADSC_HandoffStats49` (208), `NADSC_SetCameraMotion47` (218), `NADSC_SetKick46` (222), `NADSC_KickStats46` (227), `NADSC_ObserveParent` (232), `NADSC_ParentStats` (237), `NADSC_FreeAimStats` (243). Never declared/called from Lua: `NADSC_EntryTrace50`, `NADSC_AnchorStats51`. |
| `src/first_person/alignment.lua` | 1-126 | PARKED. The module is `require`d in exactly one place, `Backend:_alignment_helper` (`backend.lua:681-686`), which is called only from `Backend:prepare_alignment` (`backend.lua:1085`), which is called only by the controller when `mode=='unlinked_sights'` (`controller.lua:297-298`). Nothing starts that mode: `runtime.lua` starts only `'pose_offset'` (844) and `'native'` (924), the menu only `'pose_offset'` (`menu.lua:91`) and `'native'` (`menu.lua:414`), and the debug global has no entry for it. So on F11 (and on every menu button and debug API call) the alignment DLL is never loaded and never armed. `Backend.new` takes an `alignment` argument only for tests (`backend.lua:28-29`). Still listed in `manifest.json:24`. Note: the backend helpers named `_alignment_executable` / `_alignment_context` (`backend.lua:688,783`) are LIVE pure-Lua readers (exe image check, unit registry lookup); they read `Profile.alignment.executable` but do not touch this module or the DLL. | Lazy FFI adapter for the alignment helper: `prepare` (read-only `NADS_Validate` bracketed by two `NADS_ReadStats`), `begin` (`NADS_Begin`, keeps ownership on result 2), `stats`, `finish` (`NADS_End`). Would make the game skip its own "set local position / rotation of node 0" calls on the first-person viewmodel from two fixed call sites for at most 10,500 ms ("unlinked sights" experiment). | `src/first_person/compat51.lua` (line 1); receives `Profile.alignment` from backend | none (FFI only) | `NADS_ReadStats` (56), `NADS_Validate` (82), `NADS_Begin` (99), `NADS_End` (118), `NADS_Error` (49); none on the shipped path |
| src/first_person/fov.lua | 316 | LIVE (required at runtime.lua:655; `fov:update` through `managed_update('fov',…)` at runtime.lua:624 and once at start, runtime.lua:847; `preflight` at runtime.lua:1192) | Lua adapter + state machine for the FOV helper DLL: builds `NADSF_Config`, begins/binds/ends one "generation" of the CameraAPI FOV-setter slot hook, publishes the shared presentation phase each update, polls health/stats | `src/first_person/compat51.lua` (fov.lua:1), `src/first_person/profile.lua` `.fov` (fov.lua:167), backend methods `fov_config(lease)` (backend.lua:397) and `presentation_transition()` (set at runtime.lua:672) | none (only `ffi.load`, fov.lua:41) | LIVE: `NADSF_Validate`, `NADSF_Begin`, `NADSF_Bind`, `NADSF_SetTransition`, `NADSF_End`, `NADSF_ReadStats`, `NADSF_Error` (fov.lua:29-35). Not declared here: `NADSF_ValidateFird/BeginFird/BindFird` (declared in parked `fird_fov.lua:21-23`) |
| src/first_person/blur.lua | 289 | LIVE (required runtime.lua:656; `managed_update('blur',…)` runtime.lua:625, 848; `blur:stop()` in `backend.camera_before_entry` runtime.lua:735; `preflight` runtime.lua:1194) | Adapter + state machine for the blur helper DLL: one generation of the weapon-parameter setter slot hook that zeroes three scoped depth-of-field maxima for the held weapon's optic components; handles "weapon has no source" (code 18) with a 0.5 s recheck and "retired" codes 7/8 | compat51.lua (blur.lua:1), profile.lua `.blur` (blur.lua:148), backend `blur_config(lease)` (backend.lua:420, = `fov_config`) | none. `Adapter:now` would bind kernel32 `GetTickCount64` (blur.lua:122-127) but is not used on the shipped path because a clock is passed (runtime.lua `Blur.new(backend,log,nil,clock)`; blur.lua:154) | LIVE: `NADSB_Validate`, `NADSB_Begin`, `NADSB_End`, `NADSB_ReadStats`, `NADSB_Error` (blur.lua:33-37) |
| src/first_person/near_clip.lua | 227 | LIVE (required runtime.lua:663; created runtime.lua:713-716; `attempt(near,'update',controller)` after the managed updates, not through `managed_update`; `attempt(near,'stop')` when a foreign camera owns the view). Stays armed even with both distances 0 because `profile.near.pull_sight_distance=true` (profile.lua:65; near_clip.lua:169-170) | Adapter + state machine for the near-clip helper DLL: one generation of the CameraAPI near-setter slot hook; sends hip/aim metres from `menu:near_options()`; 1 s Begin retry; 4 Hz health poll | compat51.lua (near_clip.lua:1), profile.lua `.near` (near_clip.lua:117), backend `fov_config(lease)` (near_clip.lua:206), options callback → `menu:near_options()` (menu.lua:172-182) | none; `os.clock` only as default clock (near_clip.lua:119; runtime passes its own clock) | LIVE: `NADSN_Begin`, `NADSN_Set`, `NADSN_End`, `NADSN_ReadStats`, `NADSN_Error` (near_clip.lua:30-34). Never: `NADSN_ReadSites` (not in the cdef) |
| src/first_person/held_fire.lua | 269 | PARKED. Only required inside `ensure_ready_pose()` (runtime.lua:123-131), whose single caller is `start_fird` (runtime.lua:943) when `fird_person.native_animation_default==true` (it is `true`, fird_person.lua:9). `start_fird` is reachable only from the debug global `NativeADSCameraTest.start_fird` / `.probe_fird` (runtime.lua:1104-1110); the menu entry is commented out (runtime.lua:1056). F11 never reaches it | Adapter + admission logic for the held-fire DLL: publishes a ≤250 ms lease so the DLL drops the body's native hip-fire start/end animation events while the "ready pose" owns the body | compat51.lua (held_fire.lua:1), profile.lua `.held_fire` (held_fire.lua:189), backend `snapshot`, `_alignment_context`, `_alignment_executable`, `_u8/_u32/_ptr/_read`, `actions:sample`, `on_foot`, `foreign_camera`, `frame_active/frame_epoch` (held_fire.lua:142-185) | none | (all parked) `NADSHF_Begin`, `NADSHF_Publish`, `NADSHF_Pause`, `NADSHF_End`, `NADSHF_ReadStats`, `NADSHF_Error`; declared but never called: `NADSHF_LastError` (held_fire.lua:33-39) |
| src/first_person/zoom.lua | 181 | PARKED / dead. Required by no file (grep of `src`, `main.lua`); not listed in manifest.json; `profile.lua` has no `zoom` entry, so `Zoom.new` would fail at `Adapter.new(nil)` → `self.profile.library` (zoom.lua:100, 35). It would be reached through `Zoom.attach(fov,backend,log)` (zoom.lua:158) wrapping the FOV object | Adapter + state machine for the zoom (optic-selection) DLL, grouped with the FOV lease | compat51.lua (zoom.lua:1), profile.lua `.zoom` (absent), backend `fov_config`, `set_pose_zoom_policy` (backend.lua:905), `alignment_lease` | none | (all parked) `NADZ_Validate`, `NADZ_Begin`, `NADZ_Bind`, `NADZ_Update`, `NADZ_End`, `NADZ_ReadStats`, `NADZ_Error` (zoom.lua:24-30) |
| src/first_person/native_ads_option.lua | 93 | CONDITIONAL: object is always created (runtime.lua:720-723) and `native_aim:update(controller)` runs every kit update (runtime.lua:568), but it only acts when the menu toggle `true_native_ads` is on; shipped default `false` (menu.lua:369, 236). With the toggle off every update is `self:stop()` with no lease (native_ads_option.lua:76, 63) | "True First Person: native ADS" option: flips the game's own aim-mode preference byte (per weapon or per input device, depending on the game's aim setting) from 0 to 1 while the mode is on and writes the old value back on stop | `src/memory.lua` (lazy, native_ads_option.lua:53; a private `Memory.new()` instance), backend `snapshot`, `player_snapshot`, `pin_player_snapshot`, `_ptr/_u32/_u8/_read/_map`, `foreign_camera` | none | none (memory.lua's `copy` uses kernel32 `VirtualQuery`/`WriteProcessMemory`/`ReadProcessMemory`, not the guarded-reads DLL) |
| src/first_person/compat51.lua | 217 | LIVE (required by every first-person Lua file read here, line 1 of each) | Private wrapper around the host's `cffi` module: `ffi.cast` that converts engine userdata to an address, cached `ffi.typeof`, `number()` (cdata→number), `kind()` (reports cffi userdata as `'cdata'`), `u64()`, a pure-Lua `bit` library, a `jit.off` stub, native C-function address lookup (`jit_util.funcinfo`), 64-bit-safe `format` | `require('cffi')` (compat51.lua:2); lazily `port51/hd2_nativeinfo_f375fc4798c33ad5.dll` through `package.loadlib` (compat51.lua:7-8, 158-159); `jit.util` when the host is LuaJIT (compat51.lua:147) | none | `luaopen_hd2_nativeinfo` → `userdata_address`, `function_address` (only when a userdata is cast or a C function is inspected); kernel32 `GetCurrentProcess`/`ReadProcessMemory`/`VirtualQuery` in the last-resort closure reader (compat51.lua:177-179). None of the five adapters above triggers these: they only use `PORT51.ffi`, `.number`, `.kind` |
| src/memory.lua | 389 | LIVE for the kit (`src/backend.lua:5,46`; `main.lua:5,55` call `begin_frame`). For True First Person it is reached ONLY by `native_ads_option.lua:53-54` (`writer:copy`), i.e. CONDITIONAL on `true_native_ads` (default false) | Guarded process-memory I/O for the kit: bounded `ReadProcessMemory` reads, per-snapshot read cache, heap-only `WriteProcessMemory` with readback, fast scalar/pointer/hash-map/roster/module reads through the guarded-reads DLL with a pure-Lua fallback, plus two unrelated direct game calls (`refresh_health`, `complete_objective`) | `require('cffi')` (memory.lua:58); kernel32 via cffi (memory.lua:14-30, 86); `mod.dir..'/native/guarded_reads_218dbf400f24f282.dll'` (memory.lua:217) | none (`refresh_health` calls game+0x92F690 and `complete_objective` calls game+0x5E1720 directly through cffi function pointers, memory.lua:362, 379; both outside the mode) | `GGR_scalar`, `GGR_hashmap`, `GGR_roster`, `GGR_module` (memory.lua:216-218). Not called on the True First Person path |
| src/first_person/head_visibility.lua | 767 | LIVE (branches on `full_body`/`hide_torso`/`pinned_ready` are dead: always false on the shipped path; `show_torso` is CONDITIONAL on menu toggle `show_torso`, default false, menu.lua:371; seated branch CONDITIONAL on `profile.camera.seated_head`, shipped true, profile.lua:84) | Hides the local player's head, torso, shoulder and backpack renderers from the camera pass while keeping their shadow pass; forces the body unit's shadow pass on; hides the first-person rig's two shoulder pads; switches the shadow pass off on the rig, its arm pieces and the weapon tree (via model_shadow_source); resolves and publishes SDK Unit handles (`sdk_handles()`) used by arm_pieces and grip_hand | compat51.lua; backend (`_read`, `_ptr`, `_alignment_executable`, `player_snapshot`, `pin_player_snapshot`, `backpack`, `action_state`, `on_foot`, `foreign_camera`, `profile.pose`); model_shadow_source (deps.model_shadows); `package.loaded.hd2_nativeinfo.userdata_address` (optional fast path); deps.fallback_ready / pinned_ready / show_torso / seated_head / full_body / hide_torso from runtime.lua:691-696 | `Application.main_world`, `World.units`, `Unit.set_mesh_visibility(unit, index, visible, pass_name)`, `Unit.has_mesh`, `Unit.mesh`, `Mesh.name`, `Mesh.node`, `Mesh.num_materials`, `Mesh.has_material` | none (kernel32 `GetTickCount64` through FFI for the clock, hv:46-52; reads go through backend `_read` = kernel32 `ReadProcessMemory`, be:65-78) |
| src/first_person/first_person_movement.lua | 248 | LIVE (`update`); `update_fird` and every `focused` branch PARKED (only reachable from Fird Person, `fird` is nil) | While the game's S key is held in the mode, writes the local avatar's "focused locomotion" byte to 1 so the body backpedals facing the view instead of turning round; restores the byte when S is released or the mode leaves | compat51.lua; backend (`_read`, `_ptr`, `_u32`, `_windows`, `player_snapshot`, `action_state`, `on_foot`, `foreign_camera`, `pin_first_person`); deps.fallback_ready (camera module active, rt:687-690) | `Keyboard.button_index` or `Keyboard.button_id` (`'s'`), `Keyboard.button`, `Window.mouse_focus`, `Window.show_cursor` | none (kernel32 `WriteProcessMemory` mv:97-104, `VirtualQuery` mv:85, `ReadProcessMemory` via backend) |
| src/first_person/dof.lua | 266 | LIVE for `dof_enabled`; the `Setting` instance for `vignette_enabled` is PARKED (replaced by `vignette_factory`, dof.lua:237, rt:707-708); `deps.fallback_ready` branch dof.lua:142-148 dead because `deps.active_view` is supplied | Switches the renderer Boolean `dof_enabled` to false while the first-person view is shown, re-asserts it when the game switches it back on, restores it to true afterwards; wraps the vignette object | compat51.lua; backend (`_alignment_executable`, `_read`, `renderer_dof_value`, `foreign_camera`, `profile.pose`); camera_compat (`foreign`); deps.clock, deps.active_view = `first_person_view(fallback_ready)` (rt:48-79, 708-709) | `Application.render_setting(key)`, `Application.set_render_setting(key, 'true'/'false')` | none |
| src/first_person/vignette.lua | 127 | LIVE (constructed through `vignette_factory`, rt:708) | Finds shading-environment vignette components and sets their `vignette_enabled` property false while the first-person view is shown; restores on stop | backend (`foreign_camera`, `player_snapshot` only in the dead no-`active_view` branch vignette.lua:73-74); camera_compat (`foreign`); deps.clock, deps.active_view | `Application.main_world`, `World.entities`, `EntityManager.render_data_component`, `EntityManager.alive`, `RenderDataComponent.instances`, `RenderDataComponent.get_property`, `RenderDataComponent.set_property` | none |
| src/first_person/camera_compat.lua | 181 | LIVE (legacy ActionCam shim CONDITIONAL on the global `DBF_PHOTO_ACTIONCAM_COMPAT` existing; modern `DBF_PHOTO_CAMERA_OWNERSHIP` path only observes) | Tells the mode whether Photo Mode (or an unbridged ActionCam) owns the camera; with the legacy interface installs an input-blocked reader so ActionCam stays out while the mode is on | compat51.lua; controller (`is_enabled`); globals `DBF_PHOTO_CAMERA_OWNERSHIP`, `DBF_PHOTO_ACTIONCAM_COMPAT`, `package.loaded['mods/actioncam/actioncam']`, `debug.getupvalue` | none | none |
| src/first_person/model_shadow_source.lua | 129 | LIVE (only produces rows in camera kind 6 with `s.safe`; in the kind-4 fallback `probe` fails and head_visibility records `model_shadow_error`) | Read-only inventory of the first-person rig, the arm pieces hung on it (appearance slots 6-9) and the weapon unit tree, returned as shadow-only rows for head_visibility; cheap `unchanged` fingerprint for the no-write steady path; `valid` recheck before each write | backend (`snapshot`, `_alignment_context`, `_read`, `_ptr`, `_u32`, `_u8`, `_descriptor`, `foreign_camera`) | none | none |
| src/first_person/grip_hand.lua | 691 | CONDITIONAL: `profile.camera.grip_hand` (shipped true, profile.lua:93) and menu toggle `fird_person_arms` (default true, menu.lua:373); load failure tolerated (rt:703-705). Dead inside: `calm` (constant 1, gh:655), `ARM_PARENT`, `held_at/held_time/busy_until/steady`, result fields `handled/turning/sliding` | Moves the arm pieces linked to the rig's left arm so the hand sits on the weapon's grip bone with the body's finger shape; same for the right arm relative to the weapon bone; hides the rig's own two-material mesh while the left arm is corrected; one-handed spin handling (weapon root offset, left hand aside); per-weapon grip memory | compat51.lua; backend (`_alignment_context`, `_read`, `_ptr`, `_u32`); head_visibility `sdk_handles()` (passed in by rt:554-557); deps.clock | `Unit.alive`, `Unit.num_scene_graph_items`, `Unit.set_local_pose`, `Unit.world_pose`, `Unit.world_position`, `Unit.world_rotation`, `Unit.set_mesh_visibility(unit, index, bool)`, `Unit.num_meshes`, `Unit.mesh`, `Mesh.visibility`, `Mesh.num_materials`, `Matrix4x4.identity/multiply/inverse/translation/rotation/from_quaternion_position`, `Matrix4x4Box` (+`:store`, `:unbox`), `Vector3(x,y,z)`, `Vector3.length/cross/dot/normalize/lerp/x/y/z`, `Quaternion.identity/axis_angle/rotate/multiply/lerp/inverse/to_elements/from_elements` | none |
| src/first_person/arm_pieces.lua | 284 | CONDITIONAL: `profile.camera.body_sleeves` (shipped true, profile.lua:91) and menu toggle `fird_person_arms` (default true); load failure tolerated (rt:700-702) | Re-links the chest, collar, two spine and shoulder-group bones of each arm piece on the rig to the rig's shoulder bone of its side and places them every update as they sit on the body's torso | compat51.lua; backend (`_alignment_context`, `_read`, `_ptr`, `_u32`); head_visibility `sdk_handles()`; deps.clock | `Application.main_world`, `World.link_unit(world, child, child_node, parent, parent_node)`, `Unit.alive`, `Unit.num_scene_graph_items`, `Unit.set_local_pose`, `Unit.world_pose`, `Unit.world_position`, `Unit.world_rotation`, `Matrix4x4.identity/multiply/inverse/translation/from_quaternion_position`, `Matrix4x4Box` (+`:store`, `:unbox`), `Vector3(x,y,z)`, `Vector3.cross/length/dot/normalize`, `Quaternion.axis_angle/rotate/multiply/identity` | none |
| fp/menu.lua | 442 | LIVE (Fird entry `start_test`, `ads_mode` dropdown and Shift+F8 are commented out: 78-86, 341-342, 375-377, 279-280) | Registers the two menu pages through the `first_person.lua` facade, polls the F11/F8 hotkeys, keeps status text and disabled flags current, turns menu values into `motion_options`, `near_options`, `native_ads`, `show_torso`, `fird_person_arms`, saves per-weapon near-clip profiles and the family-defaults switch | fp/compat51.lua, src/first_person.lua | None directly. Uses the DBFMCM API through the facade: `register`, `camera_input.frame/down/focused`, `menu_binding_status`, `input_status`, `is_open`, handle `get/set/unregister`; reads global `LiveLuaLoader.input_status` (260-264) | none |
| fp/placement_editor.lua | 397 | LIVE (weapon mode). Camera mode is PARKED: nothing calls `request_open('camera')` (menu.lua:281, 361 both pass `false`) | F8 placement editor state machine: takes MCM's captured input and photo view, live-previews a draft placement on the rig, saves per weapon / all weapons per action, cancel restores | fp/compat51.lua, src/first_person.lua, fp/ui_style.lua (only `editor_legacy_tokens`) | `stingray.Window.mouse_focus()`, `stingray.Window.show_cursor()` (read-only probes, 74-81). Everything else through DBFMCM: `close`, `is_open`, `input_status`, `photo_resolution`, `create_photo_view` -> view `draw/release/refresh_font`; `camera_input.acquire/release/set_focus/menu_sample/down/focused`. Reads `package.loaded['dbf.epic_lut.frontend.v1']` (63) | none directly (placement goes through `controller:set_placement`) |
| fp/ui_style.lua | 280 | Only `M.editor_legacy_tokens` (70-79) is LIVE. `M.tokens`, `rounded_rect_shapes`, `rounded_outline_shapes` and `M.new` (HellForge drawing kit) are never called by the mode: the only requirer is placement_editor.lua:6 | Colour tokens for the F8 panel; plus an unused drawing helper kit | none | In unused `M.new`: `Gui.rect`, `Gui.triangle`, `Gui.text`, `Vector2`, `Vector3`, `Color` (206-239). None reached on the shipped path | none |
| fp/motion_runtime.lua | 408 | LIVE (`update`, `reset`, `result`). `capture_neutral` is PARKED (only called when `fird_entry:uses_fp()`, runtime.lua:611-613). Branch 297-308 is dead because `backend.action_placement` is always set (runtime.lua:740) | Per update: samples locomotion, blends the per-action saved placement, runs the bob/sway model, converts to the rig parent's space, applies ADS handoff fade and pop guard, submits to the pose helper | fp/compat51.lua, fp/motion.lua, fp/locomotion.lua, fp/action_placement.lua (lazy, 319) | none | Through `backend:set_pose_motion` -> `Pose:set_motion` -> `NADSE_SetTransform` or `NADSE_SetAuthoredTransform` (fp/pose.lua:274-296); `backend:set_pose_transition` -> `NADSE_SetTransition` (backend.lua:1012-1016); `backend:set_ads_placement` -> `NADSE_SetADSPlacement` (pose.lua:262-271); `controller:set_placement` (-> `NADSE_SetPlacement`, pose.lua:259) |
| fp/motion.lua | 163 | LIVE. Camera outputs (`camera_dx` ... `camera_roll`, 147-152) are computed but nothing reads them; `shoulder_aim_enabled` is never set so the shoulder offset (141-142) is always 0 | Pure bob / sway / sprint-pose math | fp/compat51.lua | none | none |
| fp/locomotion.lua | 162 | LIVE | Reads character root position, rig parent basis and camera basis from game memory; sprint detection; camera-space to parent-space conversion (`to_parent`, `rotation_to_parent`) | fp/compat51.lua (FFI) | none (raw memory through `backend:_read/_ptr/_u32/_u8`) | none (kernel32 `ReadProcessMemory` aliased `NativeADS_Read`, backend.lua:55) |
| fp/aim_markers.lua | 262 | LIVE (`update`, `stop`, `setting`, `set_setting`, `shutdown`, `result`, `M.source`). `toggle` and `target` are wired (runtime.lua:1081-1082) but the menu never calls them. `pinned_view` is dead: `backend.pin_first_person=false` (runtime.lua:676). Fird branches dead | Hides the game's fixed crosshair group and moving aiming-circle group while aiming in first person; restores them | fp/compat51.lua | none | none of the mod's DLLs. kernel32 `VirtualQuery` (aliased `NativeADS_Query`, 56) and a direct FFI call of the game's own setter at game+0x12e4e20 (60) |
| fp/head_placement.lua | 34 | PARKED in effect: built live (runtime.lua:744-747) and sets `backend.fird_camera_offset` at construction (12), but `is_enabled` is "Fird enabled" which is never true, and camera edit mode is unreachable | Head-camera offset store/positioner for Fird Person | fp/compat51.lua | none | none (`camera:set_offsets` only when Fird is on) |
| fp/shared_input.lua | 197 | CONDITIONAL: only when `DBFMCM.shared_input_service` is a table and `DBFMCM.camera_input` is absent (src/first_person.lua:33). README.md:97 says the current MCM is in that state, so treat as live | Consumer of LLL's token-owned input service v1: owner id `goose_first_person_editor`, pointer sample, wheel, optional captured keyboard, engine-keyboard fallback | none | `stingray.Window.has_focus` (102), `stingray.Gui.resolution` (144), `stingray.Keyboard.button_id` (180), `stingray.Keyboard.button` (183) | none (LLL service: `acquire`, `release`, `owns`, `status`, `snapshot`, `set_mode`, `capabilities`) |
| fp/input_adapter.lua | 31 | CONDITIONAL, same condition as shared_input.lua | Presents shared_input as a `camera_input` API 2 object (`api=2`, `isolated_look=true`, `frame`, `focused`, `down`, `acquire`, `release`, `set_focus`, `menu_sample`) | fp/shared_input.lua | `stingray.Window.has_focus` (9), `stingray.Application.time_since_launch` (24) | none |
| src/mcm.lua | 228 | LIVE (kit level). The mode uses exactly: `FirstPerson.pages()` appended after the kit's own pages (161), `on_change=FirstPerson.changed` on the kit registration (165), `FirstPerson.attach(handle)` after register / `attach(nil)` on unregister (176, 45), and re-registration whenever `FirstPerson.revision()` changes (81, 177). Everything else in the file is Unlocks / Gameplay / Spawner / Loadout Editor | Single DBF-MCM registration `goose_temporary_unlocks` ("Goose Gadget Kit") that embeds the mode's pages | src/first_person.lua, src/spawner/stratagem_menu.lua | none (global `DBFMCM`: `register`, `mods`, `close`, `is_open`; handle `get`, `set_many`, `unregister`) | none |
| `src/first_person/fird_aim.lua` | 89 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | Pure math: camera-relative hip and ADS targets from two captured first-person poses, smoothstep blend over 0.1 s (`M.available`, `M.blend`, `M.new():update`). Created only as `aim=not natural and ...` (`runtime.lua:953`). | none | none | none |
| `src/first_person/fird_auto.lua` | 189 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | State machine for the automatic "start native FP, sample, capture, hand over to Fird" entry with bounded retries; all work is in injected callbacks (`runtime.lua:1015-1037`). Skimmed. | none (callbacks injected) | none | none |
| `src/first_person/fird_camera_pose.lua` | 106 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | Reads the submitted final camera world matrix from `context+0x0C`, checks three game.dll byte pins and two exe pins, orthonormality and local/world epoch agreement (`fird_camera_pose.lua:8-12,72-105`). Passed as `camera_frame` (`runtime.lua:947`). | compat51 | none (backend memory reads only: `_read`, `_ptr`, `_u32`, `_u8`, `snapshot`, `_alignment_executable`) | none |
| `src/first_person/fird_capture_ready.lua` | 104 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | Read-only gate that a native FP sampling session is neutral, owned and stable for >=3 ticks and 0.18 s; `scene_lens` reads camera FOV at `camera+0x34` behind an exe byte pin (`fird_capture_ready.lua:12-23,78-103`). Required at `runtime.lua:972`. | compat51 | none | none |
| `src/first_person/fird_fov.lua` | 142 | PARKED (debug API only: NativeADSCameraTest.start_fird, natural branch) | Subclass of the live `fov.lua` adapter that holds the captured display lens on the normal camera while Fird is on (`fird_fov.lua:6-65`); manager `start/update/stop`. Created unconditionally in `start_fird` (`runtime.lua:957`), started by `fird_person:start_native` (`fird_person.lua:469-472`). | compat51, fov.lua, profile.lua | none (backend: `fird_fov_config`, `player_snapshot`) | FOV helper: `NADSF_ValidateFird`, `NADSF_BeginFird`, `NADSF_BindFird` plus the inherited `NADSF_End/ReadStats/Error/SetTransition` |
| `src/first_person/fird_handoff.lua` | 35 | PARKED (required nowhere) | Small "stop old owners, wait one clean frame, start Fird" handoff with a 3 s deadline. Superseded by fird_auto. No `mod.require` of it anywhere in `src/`. | compat51 | none | none |
| `src/first_person/fird_native_ads.lua` | 79 | PARKED (debug API only: NativeADSCameraTest.start_fird, natural branch) | Optional lease of the game's native ADS camera while Fird is on: `backend:enter`/`backend:release` when shoulder aim is held in normal camera and menu ADS mode is "native" (`fird_native_ads.lua:33-76`). Created unconditionally (`runtime.lua:955`), updated in `fird_person.lua:697-703`. | compat51 | none (backend: `snapshot`, `enter`, `release`) | none directly |
| `src/first_person/fird_person.lua` | 827 | PARKED (debug API only: NativeADSCameraTest.start_fird, natural branch) | The Fird Person manager. Natural mode (`start_native`, `fird_person.lua:452-476`): keeps the third-person rig and its native animation, sets `backend.fird_camera_active`, prepares the live eye camera, starts fird_fov and requests the ready pose; per update only native-ADS lease, focus movement (`movement:update_fird`) and FOV (`:686-758`). Render mode (`start`, `render_pose`): captures FP pose and rewrites linked arm/weapon render skeletons. Skimmed. | compat51, locomotion.lua (live), fird_projection.lua | `Application.main_world`, `World.units`, `World.update_unit`, `Unit.world_pose/world_position/node/local_rotation`, `Matrix4x4.inverse/transform`, `Quaternion.from_elements/from_matrix4x4/to_elements/rotate`, `Vector3.x/y/z` (most only in render mode); many backend raw reads | none directly (through fird_fov, held_fire) |
| `src/first_person/fird_presentation.lua` | 210 | PARKED (required nowhere) | Lease of the game's local first-person presentation switch through `backend._invoke_presentation`; fird_person has an `options.presentation` slot but runtime never fills it. Not in `manifest.json`. Skimmed. | none | none (backend: `snapshot`, `_alignment_context`, `_read`, `_u32`, `_u8`, `_invoke_presentation`) | none |
| `src/first_person/fird_projection.lua` | 162 | PARKED (debug API only: NativeADSCameraTest.start_fird, natural branch) | Pure render-placement math: `lens`, `basis_rotation`, `capture`, `reference`, `camera_anchor`, `lock`, `follow`, `rebase`, `apply`. Loaded at the top of fird_person (`fird_person.lua:3`), so it loads in both branches; the natural branch only calls `Projection.lens` (`fird_person.lua:46,462`). | none | none | none |
| `src/first_person/fird_render.lua` | 113 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | Owns one listener on the Lua render boundary: registers with a global `HUDRenderBridge` if present, otherwise wraps the global `render` function, and restores it on stop (`fird_render.lua:21-22,68-93`). Required after the natural branch has returned (`runtime.lua:966`). | none | none (global `render` / `HUDRenderBridge` only) | none |
| `src/first_person/fird_retarget.lua` | 277 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | Pure FPS-to-third-person arm retarget: two-bone solve with pole, twist, finger mapping; bone name hashes for both arms (`fird_retarget.lua:5-9`). Passed as `retarget` (`runtime.lua:951`). Skimmed. | none | none | none |
| `src/first_person/fird_source_pose.lua` | 185 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | Reads animation locals (64-byte records) from memory and rebuilds forward kinematics in a private frame; `reconstruct`, `world`, `new`. Passed as `source` (`runtime.lua:950`). Skimmed. | compat51 | none (backend raw reads) | none |
| `src/first_person/fird_warm.lua` | 110 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | One bounded stock-camera transaction to initialise packed FP code before pose-helper preparation; drives `backend:warm_start/warm_finish/camera_ready/snapshot`. Required at `runtime.lua:974`. | none | none | none |
| `src/first_person/fird_weapon_children.lua` | 156 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | Walks the weapon's linked descendant units (max 64, depth 8) from memory and republishes them after the weapon was rebuilt. Passed as `children` (`runtime.lua:952`). Skimmed. | compat51 | `World.update_unit` | none |
| `src/first_person/ready_pose.lua` | 312 | PARKED (debug API only: NativeADSCameraTest.start_fird, natural branch) | Persistent intent and compare-and-restore owner for the native "ready" stance; layer counts `{[4]=78,[24]=10,[25]=4}`; `request`, `update(context)`, `filter_owner`, `stop`. Created in `ensure_ready_pose` (`runtime.lua:123-152`), requested by `fird_person.lua:473`, updated in `runtime.lua:84-105`. Skimmed. | none (source factory injected) | none | none |
| `src/first_person/ready_pose_contract.lua` | 14 | PARKED (debug API only: NativeADSCameraTest.start_fird, natural branch) | Data only: UnitAPI table RVA `28703E0` and 11 byte pins for the engine's animation set-states path. The header still says build 26251122 (`ready_pose_contract.lua:1`). | none | none | none |
| `src/first_person/ready_pose_resources.lua` | 220 | PARKED (debug API only: NativeADSCameraTest.start_fird, natural branch) | Read-only proof that the expected animation resources (hash lists for layers 4, 24, 25, captured headers) are resident; `prove`, `inspect`. Skimmed. | none | none (backend raw reads) | none |
| `src/first_person/ready_pose_source.lua` | 393 | PARKED (debug API only: NativeADSCameraTest.start_fird, natural branch) | The only engine-facing part of the ready pose: snapshots 31 animation layers, verifies the contract pins, then calls the engine's native set-states function directly with a uint32 unit reference (`ready_pose_source.lua:374-376`) and checks the acknowledged states. Skimmed. | compat51, ready_pose_resources.lua, ready_pose_contract.lua | no Lua SDK call; one FFI call of the pinned native UnitAPI animation set-states function | none |
| `src/first_person/ready_pose_timing.lua` | 156 | PARKED (debug API only: NativeADSCameraTest.start_fird, natural branch) | Read-only observation of the native animation batch manager (idle, pending batches) behind byte pins; `sample`. Used by the native ready gate (`runtime.lua:132-147`). Skimmed. | none | none (backend raw reads) | none |
| `src/first_person/skinned_placement.lua` | 364 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | Writes local position/rotation of linked render-skeleton nodes (not the animated body), with coalescing and exact restore. Passed as `skin` (`runtime.lua:948`). Skimmed. | compat51 | `Unit.mesh/num_meshes/local_position/local_rotation/set_local_position/set_local_rotation/world_pose/world_position`, `Mesh.node`, `World.update_unit`, `Matrix4x4.inverse/transform`, `Quaternion.*`, `Vector3.x/y/z` | none |
| `src/first_person/support_hand.lua` | 52 | PARKED (debug API only, render-driven branch which never runs since native_animation_default=true) | Left-hand-only presentation constraint keyed on labelled animation layer indices (weapon layer 235, hold 585, fire 622); `capture`, `set`, `apply`. Passed as `support` and used by `module.capture` (`runtime.lua:949,991`). | compat51 | `Unit.world_pose`, `Unit.world_position`, `Matrix4x4.inverse/transform`, `Vector3`, `Quaternion.rotate` | none |

### 1.3 Every method of `fp/backend.lua`

| method | line | live? | called from | what it reads/writes |
|---|---|---|---|---|
| `uint(bytes,off)` (local) | 10 | LIVE | everywhere | little-endian u32 from a Lua string |
| `product(a,b)` (local) | 16 | LIVE | `_map` | 32-bit wrap multiply `(a*b) mod 2^32` |
| `valid_id(id)` (local) | 20 | LIVE | everywhere | `id>0 and id<0xffffffff` |
| `unpack_hex(hex)` (local) | 22 | LIVE | `_module`, `_alignment_executable` | hex text -> bytes, memoised |
| `Backend.new(log,io,alignment,pose)` | 28 | LIVE | fp/runtime.lua:665 as `Backend.new(log)` (io, alignment, pose all nil; io is a test fixture hook) | fields: `frame_epoch=0`, `frame_active=false`, `frame_executable_checks`, `frame_executable_bases`, `frame_game_checks` |
| `begin_frame()` | 36 | LIVE | fp/runtime.lua:113 (qualified_frame), 178 (fird render, off) | `frame_epoch+1`, `frame_active=true`, `pose_phase_publication=nil` |
| `end_frame()` | 39 | LIVE | fp/runtime.lua:118, 180 | `frame_active=false`, `pose_phase_publication=nil` |
| `_invalidate_frame_checks()` | 40 | LIVE | `_module`, `_alignment_executable` (base changed), `_invoke`, `_invoke_presentation`, `_invoke_body` | bumps `frame_epoch` while a frame is active, so the next `_module`/`_alignment_executable` re-reads PE header and all pins |
| `_windows()` | 43 | LIVE | `_read`, `_module`, `_invoke`, writes | FFI cdef of kernel32 `GetModuleHandleA`, `GetCurrentProcess`, `ReadProcessMemory`, `VirtualQuery`; 1024-byte buffer; `win.wine` = ntdll has `wine_get_version` |
| `_read(address,size)` | 65 | LIVE | everything, plus other fp modules | `ReadProcessMemory(self, address, size)`; asserts integer address >= 65536, `0<size<=1024`, `address+size < 2^47`; errors 'Invalid read', 'Memory unavailable', 'Incomplete read'. NOT cached |
| `_u32(at)` / `_u8(at)` | 79 / 80 | LIVE | everywhere | 4 / 1 byte read |
| `_ptr(at)` | 81 | LIVE | everywhere | 8-byte read; asserts `65536 <= value < 2^47`, error `Pointer unavailable at0x%x (value0x%x)` |
| `_module(scope)` | 86 | LIVE (`identity`, `full`); scope `entry` fird-only (only `warm_start`/`warm_finish` pass it) | `_snapshot` (identity), `camera_ready`, `prepare_pose` (full), `_invoke` (full), `backpack`, `release` (full, default), `_body_api` | base = `GetModuleHandleA('game.dll')`. Checks `MZ`; `e_lfanew` u32 at base+0x3c in 0x40..0x1000; 96 header bytes: `PE\0\0`, machine 0x8664, optional magic 0x20b at +24, TimeDateStamp(+8)==0x6af322c1, SizeOfImage(+80)==0x3382000 ('Unsupported game build'). Then every `Profile.checks` entry: `identity` = unreadable pins tolerated, readable ones must match; `entry` = all must be readable except evaluator, root_position_call, root_rotation_call, pose_branch_position_call (FP_COLD_CHECK, line 85); `full` = all must be readable ('Waiting for native ADS camera initialization (name)') and match ('Native ADS fingerprint mismatch: name'). Scope `entry` also requires 11 bytes at game+0x110FCC2 == `0f57d2488bcbe8a3860000`. Result cached per scope for the current `frame_epoch` |
| `camera_ready()` | 128 | fird-only (off) | fp/fird_warm.lua, fp/runtime.lua:973 (inside start_fird) | `pcall(_module,'full')` |
| `_map(at,id,limit)` | 132 | LIVE | `_snapshot`, `_descriptor`, `backpack`; fp/native_ads_option.lua | open-addressing hash map. 20-byte header at `at`: +0 buckets ptr, +8 capacity u32 (power of two, <=1048576), +12 empty-key u32, +16 multiplier u32. Probe `n=0..min(cap,4096)-1`: 8-byte row at `buckets+((id*mult mod 2^32 + n) mod cap)*8` = key u32, index u32. Stops with 'Entity absent' on the empty key; index must be `< limit` (default 4096) |
| `_descriptor(world,id)` | 151 | LIVE | `_snapshot` (weapon), `backpack`; fp/model_shadow_source.lua | index = `_map(world+0xf1c978,id,1048576)`; 24 bytes at `world+0xf349e0+index*24`; asserts u32(+8)==id and byte(+20) odd ('Entity generation unavailable') |
| `_top(base)` | 157 | LIVE | `_snapshot`, `warm_finish`, `release` | ctx = ptr[base+0x2e287d0]; 8 bytes at ctx+0x1f8 = head u32, tail u32 (both <32); record address = `ctx+0x200+((tail+31)%32)*0xe8` when head~=tail, else `ctx+0x1f00`; 20 bytes of the record: +0 kind u16, +4 avatar u32, +8 weapon u32. Returns base, context, camera=ptr[ctx+0], record, head, tail, camera_kind, request_avatar, request_weapon |
| `_snapshot(player_only,retiring,pin_player)` | 167 | LIVE | `snapshot`, `player_snapshot`, `pin_player_snapshot`, `release` (retiring form, line 1417) | see field list below |
| `snapshot()` | 291 | LIVE | controller (start, manual_update, timed update), fp/runtime.lua (recovery_proof 269, recover_new_owner 333, observe_presentation 366, record_presentation 448, editor save 778, save_presentation 1072, preflight 1188), every backend setter, fp/camera.lua, fp/aim_markers.lua, ready/fird modules | `pcall(_snapshot)`; returns table, or `nil, error text` |
| `backpack(s)` | 301 | LIVE | fp/head_visibility.lua:420, 633 | inventory = ptr[base+0x2ce18e8]; index = `_map(inventory+40, s.avatar)`; owner = ptr[ptr[inventory+64]+index*8], its 20 bytes must equal `s.avatar_descriptor`; row = ptr[inventory+80]+index*48; entity = u32[row+12] ("Inventory slot 3 (byte 12)"); descriptor via `_descriptor`; ref = u32(descriptor+12), must differ from body, viewmodel, weapon_unit. Returns `{ref, entity, identity}` or nil when no backpack |
| `camera_only_weapon(s)` | 317 | LIVE | controller.lua:282-283, 402-403 | true when the weapon unit's resource id (8 bytes at object+0x358, object resolved with `_alignment_context({viewmodel=s.weapon_unit})`) equals bytes `53 85 BD A2 BD C9 3E 5F` (Breaching Hammer). Cached per (world, weapon_unit, weapon_descriptor) in `camera_only_identity` |
| `player_snapshot()` | 330 | LIVE | controller, fp/runtime.lua:36, `camera_config`, `detach_seated_camera`, `release` (orphan path), action_state.lua, locomotion, movement, head_visibility, vignette, head_placement, native_ads_option | `_snapshot(true)`: stops before the weapon part |
| `pin_player_snapshot()` | 338 | LIVE despite "pinned off": fp/head_visibility.lua:58-61 calls it first on every view snapshot (because `deps.pinned_ready` is supplied, fp/runtime.lua:691) and fp/runtime.lua:367 uses it as fallback in observe_presentation. The other callers (controller.lua:61, runtime.lua:32, aim_markers, movement, native_ads_option) are gated on `backend.pin_first_person==true` = off | `_snapshot(true,nil,true)`: skips the 'Camera is not owned by local player' assert, adds `pin_camera=true`, `camera_unit=u32[context+8]`, and forces `safe=false` + `foreign_camera=true` + reason when `foreign_camera()~=false` |
| `action_state(snapshot)` | 344 | LIVE | `_snapshot` (276), `camera_config`, fp/locomotion.lua:145, fp/first_person_movement.lua:201, head_visibility | lazily creates action_state sampler; `sample(snapshot, layers)` with layers `{1,3,14}` (default) or `{1,3,10,11,14}` only when `self.support_grip_tracking` (set by fird_person only) |
| `on_foot(s,action)` | 351 | LIVE | `_snapshot` (279), `camera_config`, locomotion, movement, head_visibility | requires `action.resource_match==true` and `action.layer_count==31`. `action.l14==36` -> true. Else requires `action.l14==3`, `#s.flags==24`, `band(u32(flags),6)==2`, flags address == `avatar_owner+0x53e888+avatar_index*0x1240` with first 4 bytes unchanged, state = ptr[base+0x2ce1508], scene = u32[state+0xac29c] must be 3 or 4 (re-read twice) |
| `camera_config()` | 374 | LIVE | fp/camera.lua (3 refs); fird_person | player_snapshot; asserts not foreign, kind 4 or 6, not seated, on_foot with `action.owner==s.player_identity` and `action.body==s.body`. Returns game, exe (`_alignment_executable(Profile.camera)`), context, camera, avatar_owner, avatar_index, avatar, body, `pin_view=false`, `pin_look=false`, `presentation_enabled=true` (false only for fird direct), `kick_enabled=false`, `camera_unit=u32[context+8]`, camera_kind, forward=.18, up=.04, right=nil, smoothing_ms=100, blend_ms=120, max_gap_ms=250 (fird overrides off) |
| `fov_config(lease)` | 397 | LIVE | fp/fov.lua, fp/near_clip.lua (zoom.lua not loaded) | snapshot; with a lease: must be the current undone pose lease, `s.identity==lease.origin.identity`, kind 6, `s.record==lease.request`, engine_safe true; exe from lease context. Without lease: kind 4, safe, not aiming, not controller_active; exe via `_alignment_executable(Profile.pose)`. Returns game, exe, context, camera, avatar_owner, request=s.record, avatar_index, avatar, body, weapon |
| `blur_config(lease)` | 420 | LIVE | fp/blur.lua | same as `fov_config` |
| `fird_fov_config()` | 426 | fird-only (off) | fp/fird_fov.lua | - |
| `_invoke(name,s,scope)` | 438 | LIVE | `_enter` (allowed, enter), `release` (exit); warm_* (fird) | bumps frame epoch, `_module('full')==s.base` (or 'entry'), `VirtualQuery(address)`: State==0x1000 (MEM_COMMIT), Type==0x1000000 (MEM_IMAGE) or (0x40000 MEM_MAPPED and Wine and AllocationBase==s.base), Protect in {0x20,0x40,0x80}. Calls (default x64 C ABI): `allowed`: `uint8_t (*)(void *controller, uint32_t weapon)` at game+0x1144d60; `enter`: `void (*)(void *context, uint32_t avatar, uint32_t weapon)` at game+0x110fc80; `exit`: `void (*)(void *context)` at game+0x11108a0 |
| `warm_start(expected)` | 461 | fird-only (off) | fp/fird_warm.lua | stock enter with scope `entry`, sets `warm_lease` |
| `warm_finish(lease)` | 488 | fird-only (off) | fird_warm, `release` when `lease.bootstrap` | stock exit |
| `_invoke_presentation(expected,enabled)` | 517 | fird-only / unused (only named by fp/fird_presentation.lua, which nothing requires) | - | calls game+0x833440 `void(void *avatar_owner, uint32_t avatar, uint8_t enabled, uint8_t 1)` after 9 byte pins; mode byte at `avatar_owner+0x546B20+avatar_index*0x1B8` |
| `_enter(expected,token,kind,remaining_ms,blend_entry)` | 567 | LIVE | `enter_pose` (kind 'pose'), `enter` | see FRAME "backend entry/exit" |
| `enter(expected,token)` | 661 | native-mode-only (token nil). With a token (mode `unlinked_sights`) unused | controller.lua:321 | `_enter(expected,token,'alignment')`: plain game enter, no helper |
| `enter_pose(expected,token,remaining_ms,blend_entry)` | 665 | LIVE | controller.lua:319, 466, 554 (554 timed only) | validates `remaining_ms` integer 0..30500, blend_entry boolean or nil, token present; runs hook `camera_before_entry` (compat:prepare, blur:stop, camera:prepare; fp/runtime.lua:733-737) which must return true; then `_enter(...,'pose',...)` |
| `_alignment_helper()` | 681 | unused on shipped path | `prepare_alignment` | loads fp/alignment.lua |
| `_alignment_executable(helper_profile)` | 688 | LIVE | `_alignment_context`, `camera_config`, `fov_config`, `renderer_dof_value`; other fp modules | base = `GetModuleHandleA('helldivers2.exe')`; MZ/PE checks as `_module`; TimeDateStamp==0x6af2d5e6, SizeOfImage==0x29fe000; byte pins `Profile.body_pose.executable.exists` (exe+0x1f75d0) and `.trigger` (exe+0x1f77c0). Cached per `profile.executable` table for the current frame epoch |
| `renderer_dof_value(getter,setter,setting)` | 731 | LIVE (when dof.lua's `monitor` path is used, fp/dof.lua:69-70) | fp/dof.lua | reads the engine's render-settings table without calling the getter: asserts `funcinfo(getter).addr==exe+0x3d43d0`, `funcinfo(setter).addr==exe+0x3d3ff0`; pins 8 bytes at getter `48 8B D1 E9 08 FD FF FF`, at setter `48 8B D1 E9 88 FA FF FF`, exe+0x3d419a `48 8B 0D 1F 37 6E 01`, exe+0x3d420e `83 F8 04 75 1A 41 38 71 08`; table = ptr[exe+0x1ab78c0]; header 16 bytes: count u32 (+0, 1..4096), rows ptr (+8); rows 24 bytes sorted by key u32 at +16 (binary search), type u32 at +0 must be 4, Boolean byte at +8. Keys: dof_enabled 0x06f1d0d3, vignette_enabled 0x72e8f18c. Row index cached in `renderer_dof_contract`; everything re-read after |
| `_alignment_context(s,helper_profile)` | 783 | LIVE | `_pose_context`, `camera_only_weapon`, action_state.lua:44, fp/runtime.lua:496, arm_pieces, grip_hand, model_shadow_source | unit ref -> object: registry = ptr[exe+0x1ab76f8]; 32 bytes at registry+0x88: units ptr (+0x88), capacity u32 (+0x98, <=0x400000), generations ptr (+0xA0); index = ref % 0x400000; generation = floor(ref/0x400000) % 256; u8[generations+index]==generation ('Viewmodel generation retired'); object = ptr[units+index*8] (read twice). Returns exe, unit, index, generation, object, registry, units, generations, owner=s.avatar_owner, avatar_index |
| `_pose_helper()` | 804 | LIVE | `prepare_pose` | `Host.require('src/first_person/pose.lua').new(Profile.pose)`, kept in `self.pose` |
| `_pose_context(s)` | 811 | LIVE | `prepare_pose`, `_enter`, `set_pose_placement`, `set_pose_motion`, `pose_status`; fp/aim_markers.lua:98 | `_alignment_context(s, Profile.pose)` for the viewmodel, plus the weapon: 20 bytes at `s.weapon_component_descriptor` == `s.weapon_descriptor` and u32(+12)==s.weapon_unit; weapon index < u32[registry+0x98]; generation check; weapon_object = ptr[units+index*8]. Adds weapon_descriptor (address), weapon_bytes, weapon_unit, weapon_index, weapon_generation, weapon_object |
| `_same_pose_context(a,b)` | 831 | LIVE | prepare/enter/setters | field-wise equality of the above |
| `_pose_config(s,request,context,remaining_ms,blend_entry)` | 839 | LIVE | `prepare_pose` (no remaining_ms), `_enter` | builds the helper config: `_alignment_config` fields + `duration_ms = remaining_ms or 30500`, weapon_descriptor, weapon_unit, `blend_ms=150`, `steady_entry = (remaining_ms==0 and blend_entry~=true)`, offset_x/y/z. Weapon key = 16 lower-case hex chars of the 8 bytes at weapon_object+0x358 in memory order (must not be all zero). Placement = `placement_resolver(key)` (= `Actions.resolve(config,key,'idle')`) or `self.pose_placement` or profile offsets .15/0/-.12. Side effects: `self.pose_placement={x,y,z,yaw,pitch,roll}`, `self.placement_weapon_key=key` |
| `placement_copy(p)` (local) | 860 | LIVE | setters | x,y,z finite within +-0.5; yaw,pitch,roll (default 0) within +-90 |
| `set_placement_defaults(p)` | 878 | LIVE | controller.lua:130 (set_placement while not running) | stores `self.pose_placement` only |
| `set_pose_placement(lease,p)` | 884 | LIVE (placement editor F8 while running) | controller.lua:128 | lease must be current, not done/cleanup_only/ended, kind pose; snapshot identity equal, safe, kind 6, record==lease.request; pose context unchanged; `helper:set_placement(handle,x,y,z)` (NADSE_SetPlacement; angles are not sent) |
| `set_pose_zoom_policy(lease,active)` | 905 | unused (only fp/zoom.lua, loaded by nothing) | - | NADSE_SetZoomPolicy |
| `set_ads_placement(lease,value)` | 919 | LIVE | fp/motion_runtime.lua:327 | same lease/snapshot checks (no pose-context check); `helper:set_ads_placement(handle,x,y,z)` (NADSE_SetADSPlacement) |
| `set_pose_transition(lease,token)` | 936 | LIVE | controller.lua:364 (publish_transition), `set_pose_motion` (1014) | only inside a frame (`frame_active`); lease and handle ownership checks; token `{epoch int 1..2^53-1, progress 0..1, ads boolean, identity, player_identity}`; silently true when token belongs to another identity; de-duplicated per frame on fields epoch, progress, ads, identity, player_identity, reason, action, started, duration, continuous (935); `helper:transition(handle,token)` (NADSE_SetTransition(generation, epoch, progress, ads)); records `presentation_pose_lease`, `presentation_pose_epoch` |
| `set_pose_motion(lease,value)` | 983 | LIVE | fp/motion_runtime.lua:118 | dx,dy,dz each within +-0.15; quaternion (default 0,0,0,1) each component <=1.001, `abs(norm-1)<=.002`, `abs(qw)/sqrt(norm)>=.5` (<=120 degrees); lease + snapshot (safe, kind 6, same record) + pose context checks; publishes the transition packet first; `helper:set_motion(handle,dx,dy,dz,qx,qy,qz,qw,authored_ads)` (NADSE_SetTransform, or NADSE_SetAuthoredTransform when `value.authored_ads==true`) |
| `prepare_pose(expected)` | 1023 | LIVE | controller.lua:305, 457, 547; fp/runtime.lua:1190 (debug preflight) | see FRAME |
| `_same_alignment_context(a,b)` | 1055 | LIVE | via `_same_pose_context` | equality of exe, unit, index, generation, object, registry, units, generations, owner, avatar_index |
| `_alignment_config(s,request,context)` | 1063 | LIVE (as base of `_pose_config`) | `_pose_config`, `prepare_alignment` | `{game, exe, context, request, avatar_owner, avatar_index, avatar, weapon, body, viewmodel, duration_ms=10500}` (duration overwritten for pose) |
| `prepare_alignment(expected)` | 1069 | unused (mode `unlinked_sights` never started) | controller.lua:298 | - |
| `_stop_alignment(lease)` | 1100 | LIVE | `release`, `pose_status` | `helper:finish(handle)` (NADSE_End); on success `alignment.ended=true`, stores `lease.alignment_stats` / `lease.pose_stats`; otherwise 'Alignment disarm pending: ...' |
| `pose_status(lease)` | 1113 | LIVE | controller.lua:97 (every running update, and in stop); fp/runtime.lua:984 (fird capture) | `helper:stats(handle)`; stores `lease.pose_stats`, `lease.pose_error=stats.error`; normalises armed/expired to booleans; snapshot must be same identity, safe, and (kind 4, or kind 6 with `record==lease.request`), else sets `lease.pose_owner_retired=true`, `lease.pose_retire_reason`, disarms the helper and returns nil; pose context must be unchanged; `stats.error~=0` disarms: error 14 -> 'Pose test stopped: native sight node is zero; weapon-only offset is unavailable', else 'Pose helper error N' |
| `alignment_status(lease)` | 1155 | unused (controller only calls it for mode `unlinked_sights`) | controller.lua:186, 569 | - |
| `_body_api(game_base)` | 1189 | unused (diagnostic) | prepare/release_body_pose | api = ptr[ptr[game+0x2ce14c8]+0x18]; query = ptr[api+0x350] must equal exe+0x1f75d0; trigger = ptr[api+0x370] must equal exe+0x1f77c0 |
| `_invoke_body(name,s,expected)` | 1220 | unused | release_body_pose | `int exists(uint32 body, uint32 event)`, `void trigger(uint32 body, uint32 event)` with event 0x372071D0 |
| `same_body_api` (local) | 1241 | unused | - | - |
| `prepare_body_pose(expected)` | 1246 | unused (mode `release_body_event` never started) | controller.lua:291 | - |
| `release_body_pose(lease,token)` | 1277 | unused | controller.lua:578 | - |
| `keep_aim_view(s)` | 1340 | LIVE (`Profile.camera.keep_aim_view=true`) | fp/runtime.lua:537 | WRITES: byte `controller+4 = 0`; u32 `record+0x98 = 0`. See FRAME |
| `detach_seated_camera()` | 1372 | LIVE (`Profile.camera.seated_head=true`) | fp/runtime.lua:541 | CALLS engine function ptr[ptr[ptr[game+0x2ce14c8]+8]+0xF0] as `void(void *world, uint32_t unit)`. See FRAME |
| `release(lease)` | 1397 | LIVE | controller.lua:160 (retry_cleanup), 520 (timed) | see FRAME |
| `Backend.profile` | 1470 | LIVE | fp/locomotion.lua and others | = profile.lua table |

**Hooks installed on the instance by fp/runtime.lua** (plain fields, not methods): `presentation_transition` (672; read by controller.lua:359-361, backend.lua:1012, motion_runtime, fov), `presentation_coordinator` (673; set, read by nothing), `presentation_action` (674; motion_runtime.lua:321), `presentation_resume` (675; motion_runtime.lua:261), `pin_first_person=false` (676), `camera_update_token=nil` (679; read by nothing), `foreign_camera` (719), `camera_before_entry` (733), `placement_resolver` (739), `action_placement` (740), `action_mapping` (741), `action_placement_revision` (784, 812, 1078). State fields the backend itself keeps: `alignment_lease` (608/620; never cleared, compared with `.done`), `warm_lease`, `pose_tokens` (weak-keyed), `pose_placement`, `placement_weapon_key`, `pose_phase_publication`, `presentation_pose_lease`, `presentation_pose_epoch`, `camera_only_identity`, `renderer_dof_contract`, `aim_view_keeps`, `seated_detaches`, `actions`, `pose`, `win`.

#### Fields of the table returned by `snapshot()` / `player_snapshot()` (backend.lua:157-290)

`player_snapshot()` returns the rows marked P and stops at line 233 (`identity = player_identity`). `snapshot()` returns all rows.

| field | P | derivation | meaning |
|---|---|---|---|
| `base` | P | `GetModuleHandleA('game.dll')`, `_module('identity')` (168) | game.dll base |
| `context` | P | ptr[base+0x2e287d0] (158) | camera manager / request queue object |
| `camera` | P | ptr[context+0x0] (164) | render camera object |
| `head`, `tail` | P | u32[context+0x1f8], u32[context+0x1fc], each <32 (159-160) | ring indices of the camera request queue (32 slots) |
| `record` | P | head~=tail: `context+0x200+((tail+31)%32)*0xe8`; else `context+0x1f00` (162) | address of the newest camera request, or the fallback record ("Match the constructor's own latest-request / fallback selection") |
| `camera_kind` | P | u16 little-endian at record+0 (165) | request type: 4 = normal third-person gameplay camera, 6 = native first-person (ADS) camera request. Anything else = other camera (vehicle, scripted, ...) |
| `request_avatar` | P | u32[record+4] | avatar entity the request belongs to |
| `request_weapon` | P | u32[record+8] | weapon entity of the request |
| `world` | P | ptr[base+0x2ce41b8] (169) | game world / entity manager |
| `controller` | P | ptr[base+0x2e381a8] (170) | native first-person aim controller |
| `controller_bytes` | P | 6 bytes at controller+0 (171) | bytes 0..3 = u32 inhibit/action mask, byte 4 = aim "active", byte 5 = guard |
| `controller_active` | P | byte controller+4 ~= 0 (172) | the game's own full ADS is active |
| `blocked` | P | u32(controller+0)~=0 or byte controller+5~=0 (173) | any inhibit mask or guard set |
| `avatar` | P | player = ptr[base+0x2ce1618] (u32[player+0x88] > 0); light = u32[player+0x3a8]; index = `_map(world+0xf24990, light, 1048576)`; d = 24 bytes at world+0xf349e0+index*24 with u32(d+16)==light and byte d+20 odd; avatar = u32(d+8) (174-181) | local avatar entity id |
| `avatar_descriptor` | P | d bytes 0..19 | 20-byte generation descriptor |
| `request_local` | P | request_avatar==avatar (182) | camera request is the local player's. `snapshot`/`player_snapshot` FAIL ('Camera is not owned by local player') when false; only pin_player_snapshot continues |
| `avatar_owner`, `avatar_index` | P | owner = ptr[base+0x2ce1ee0]; ai = `_map(owner+0xf8, avatar, 8)`; ai < u32[owner+0x6c] <= 8 (184-187) | avatar component pool and the local slot (0..7) |
| `body` | P | ep = ptr[owner+0x110+ai*8], 20 bytes at ep == avatar_descriptor; body = u32[ep+0xc] (188-191) | third-person body unit ref |
| `flags_address`, `flags` | P | owner+0x53e888+ai*0x1240, 24 bytes (192-193) | packed character state flags ("mirrored at player-state row+8 (pm+547870, stride 0x78)") |
| `viewmodel` | P | u32[owner+0x53e9c0+ai*0x1240] (196) | first-person rig unit ref |
| `aiming` | P | flags byte 6 bit 0 ("Bit 48 ... tested by the game's native first-person controller at 12B28AD") (198) | legacy aim lifecycle bit |
| `shoulder_aim` | P | not controller_active and flags byte 10 & 4 (bit 82) and flags byte 15 & 32 (bit 125) (203-204) | normal (third-person shoulder) aim held, not full ADS |
| `seated` | P | flags byte 14 & 4 (bit 114) (205) | seated interaction |
| `reloading` | P | flags byte 15 & 2 (bit 121) (208) | reload |
| `player_identity` | P | `%.0f` of base, context, camera, world, controller, player, light, avatar_owner, avatar_index, avatar, body, ep joined by ':' + ':' + avatar_descriptor (209-214) | identity of the local player/body; changes on respawn, new world, new camera object |
| `safe` | P | request_local and (camera_kind==4 or 6) and not seated and not blocked (215); full snapshot only: additionally forced false when not `on_foot` (279-282) or when `foreign_camera()==true` (284-286) | the native view may be entered or kept |
| `weapon_transition` | P | kind 4 or 6, not seated, u32 mask == 2, byte 5 == 0 (219-220) | weapon-swap inhibit mask (annotation only; safe stays false) |
| `head_camera_transition` | P | kind == 4, not seated, u32 mask == 0x20, byte 5 == 0 (223-224) | observed vault mask; lets the head-camera fallback continue |
| `pin_camera`, `camera_unit` | pin only | true; u32[context+8] (227-228) | render camera unit ref |
| `foreign_camera` | pin / full | set true with `safe=false` when the hook says another camera owns the view (229-231, 284-286) | - |
| `reason` | pin / full | 'Photo Mode or another camera owns the view' (230, 285); or `action_state_error` or 'Character is not in the verified on-foot lifecycle' (281) | why safe is false. NOT set when safe is false only because of seated / blocked / camera kind |
| `identity` | P | player snapshot: = player_identity (233). Full: `%.0f` of base, context, camera, world, controller, avatar, body, weapon, weapon_unit, viewmodel, ep, weapon_ep joined by ':' + ':' + avatar_descriptor..weapon_descriptor (266-269) | full: player + exact weapon instance; changes on every weapon swap |
| `weapon` | | wield = ptr[base+0x2ce1fd0]; wi = `_map(wield+0x30, avatar)`; wep = ptr[ptr[wield+0x48]+wi*8], 20 bytes == avatar_descriptor; row = ptr[wield+0x60]+wi*0x1d0; weapon = u32[row+0] (245-250). In the retiring form: `retiring.weapon` (243) | wielded weapon entity id; snapshot FAILS with 'Equip a firearm before starting the camera test' when invalid |
| `weapon_unit` | | wd = `_descriptor(world, weapon)`; weapons = ptr[base+0x2ce1ea0]; index = `_map(weapons+0x30, weapon)`; weapon_ep = ptr[ptr[weapons+0x48]+index*8], 20 bytes == wd[0..19]; weapon_unit = u32[weapon_ep+0xc] (253-259) | weapon unit ref |
| `weapon_component_descriptor` | | weapon_ep address (260) | - |
| `weapon_descriptor` | | wd bytes 0..19 (261) | - |
| (kind 6 checks) | | request_weapon==weapon ('ADS request belongs to another weapon'); u32[record+0xc]==body ('ADS request body changed') (262-265) | - |
| `weapon_ready` | | always `true` on a full snapshot (270); nil on player snapshots, never `false` | - |
| `engine_safe` | | copy of `safe` before the on-foot / foreign overrides (271) | used by `fov_config` |
| `action_state`, `action_state_error` | | `action_state(s)` result table or nil; error text (276-278) | see FRAME action_state |
| `native_pose_eligible` | | `safe==true` (288) | - |

**camera_kind** is the 16-bit value at offset 0 of the newest record of the camera request ring at `[game+0x2e287d0]`: ring of 32 records of 0xe8 bytes from context+0x200, head at +0x1f8, tail at +0x1fc; the newest is slot `(tail+31)%32`; when the ring is empty (head==tail) the fallback record at context+0x1f00 is used.

**safe** = the newest camera request belongs to the local avatar, is kind 4 or 6, flags bit 114 (seated) clear, controller mask u32 == 0 and controller byte 5 == 0; and for the full snapshot also: animation resource verified + on-foot lifecycle (`l14==36`, or `l14==3` with flags&6==2 and scene 3/4), and no foreign camera (Photo Mode / camera_compat). `player_snapshot().safe` does NOT include the on-foot and foreign-camera tests.

**Failure (nil + text) instead of a table**, assert texts in order of the chain: 'game.dll unavailable', 'Invalid game module', 'Invalid PE header', 'AMD64 PE32+ required', 'Unsupported game build', 'Native ADS fingerprint mismatch: NAME', 'Invalid camera request queue', 'Local player unavailable', 'Invalid entity', 'Invalid registry', 'Empty entity', 'Entity absent', 'Entity index out of bounds', 'Entity lookup bound reached', 'Local player generation unavailable', 'Local avatar unavailable', 'Camera is not owned by local player', 'Local avatar retired', 'Avatar descriptor changed', 'First-person body unavailable', 'Local render camera unavailable' (pin), 'Retiring camera player ownership changed' (retiring), 'Wield ownership changed', 'Equip a firearm before starting the camera test', 'Entity generation unavailable', 'Weapon component ownership changed', 'Weapon unit unavailable', 'ADS request belongs to another weapon', 'ADS request body changed', plus the reader's 'Invalid read', 'Memory unavailable', 'Incomplete read', 'Pointer unavailable at0x.. (value0x..)'.

## 2. Per-frame order and state machine

### 2.1 Order of work in one game update while the mode is on

What the kit does in its own update callback (`main.lua:53-63`), in order:

1. `backend.mem:begin_frame()` (kit memory layer; unrelated to the mode's reads).
2. Kit `menu:update(dt)`: the kit's MCM registration, which contains the mode's two pages.
3. Kit `controller:update(dt)`, `equipment:update(...)` (unlock machinery, not the mode).
4. `first_person.update()` (`src/first_person.lua:133-145`) -> `pcall(runtime.on_update)` -> `qualified_frame` (`fp/runtime.lua:109-121, 1205-1225`):
   1. `backend:begin_frame()`, `ready_tick + 1`, `ready_context = {phase='qualified_update', tick}`.
   2. `update_frame()` (`fp/runtime.lua:561-640`):
      1. `fird_tick + 1`; `probe_update()` (only a debug probe deadline).
      2. `compat:update()` -> `handoff_ready` (Photo Mode / ActionCam ownership).
      3. `native_aim:update(controller)` (native ADS option; no-op while the option is off).
      4. Fird branches: skipped (`fird`, `fird_entry`, `fird_warm` are nil).
      5. Recovery ledger reset when the controller's enabled state changed (594-597).
      6. `recover_new_owner()` (325-362): only when a fault is recorded and the local body changed; drains every policy, then returns early after `menu:update()`.
      7. `observe_presentation()` (364-387): snapshot, `depth_stats` zoom index, `presentation:update(...)` (the shared 0.18 s clock).
      8. Recovery: advance retry state and revalidate proofs for recorded policies (600-607).
      9. `controller:update()` unless recovery blocks the view; while blocked and a lease exists, `controller:retry_cleanup()` (608-609). This is where the native view is entered, held, released (game calls `allowed`, `enter`, `exit`; pose helper Begin / End; `NADSE_SetTransition`).
      10. `editor:update()` (F8 placement editor).
      11. `motion:update()` (action placement blend, bob/sway; `NADSE_SetPlacement`, `NADSE_SetADSPlacement`, `NADSE_SetTransform`).
      12. Idle early-out: controller off, editor closed, everything already clean and no foreign camera -> `menu:update()`, return (615-617).
      13. Foreign camera: stop fov, blur, camera, head, movement, near; `dof:update`; `menu:update()`; return (618-623).
      14. `managed_update('fov')`, `('blur')`, `('camera')`, `('head')`, then `('movement')` (plain `stop` while the editor is open), `near:update(controller)`, `dof:update(controller,camera,compat)` (624-633). `managed_update` (278-311) is the recovery wrapper: on a failed update it stops the policy, records the failure and, for core policies, suspends or stops the controller.
      15. `menu:update()` (639): status text, option getters, and the F11 / F8 hotkey poll. A toggle therefore runs at the end of `update_frame`: `start_pose(0)` calls `controller:start` (which may enter the native view at once and runs one `controller:update`), then one managed fov and blur update (842-857).
   3. `ready_update()` (no-op: `ready_pose` is nil).
   4. `aim_markers:update()` (`stop` instead when `update_frame` raised or an owner drain is running) (1216).
   5. `record_presentation(ok, why)` (423-559), last in the frame so every reader above saw the game's own aim state:
      1. Diagnostics row (extra reads: camera world matrix, rig node 5, weapon root).
      2. Aim button edge (`aim_released()`, right mouse level change or middle mouse press) while enabled, running, not blocked, no foreign camera, editor closed -> `backend:keep_aim_view(s)` once (536-538).
      3. Seated view begins (`s.seated and s.camera_kind==4`, enabled, no foreign camera, editor closed) -> `backend:detach_seated_camera()` once (540-542).
      4. `arm_pieces:update(s, active, head:sdk_handles())` (548-552).
      5. `grip_hand:update(s, active, head:sdk_handles())` (554-558).
   6. `ready_context = nil`, `backend:end_frame()`. An error from `update_frame` is re-raised here; the facade then switches the mode off until the mod is re-enabled (`src/first_person.lua:136-141`).
   7. `M.sync(...)`: copies status labels and disabled flags to the kit's registered copies of the controls.
5. Kit `spawner:update`, `mission:update`, `menu:refresh_status()`.

What runs inside the helper DLLs during the game's own update (not in the kit's callback; the code never names the thread and protects every hook against the Lua caller with locks, so the rewrite must not assume the same thread):

| when (game code) | hook | what it does |
|---|---|---|
| The game's first-person camera evaluator `game+0x1118370`, once per evaluation of the owned kind-6 request. Order by call site: Unit API `+0x90` at `0x111863e`, getter for node 50 at `0x1119a81`, node 5 position at `0x111a460`, node 0 position at `0x111a809` (node 0 rotation `0x111a829` forwarded), node 5 rotation at `0x111b11b` | editor/pose DLL: five Unit API slots (`api+0x38`, `+0x48`, `+0x40`, `+0x00`, `+0x90`) | Look-gain preset swap and restore (scoped weapons outside full ADS), lens/distance observation, placement + motion + ADS placement + zoom depth term - draw animation added to the game's fresh node 5 position, rig root counter-shift, motion rotation multiplied onto the fresh node 5 rotation, 300 ms entry rotation guard. |
| The game's per-frame camera apply; FOV setter call at `game+0x110bfda`, then position setter call at `game+0x110bff5` (comment: "evaluator dispatch precedes the shared CameraAPI setter", `native_ads_fov.cpp:473`) | fov DLL: CameraAPI `+0x80`; camera DLL: CameraAPI `+0x20` | FOV: hip = third-person FOV setting, 5 % shoulder magnification, 180 ms ADS ease, pass-through in full ADS. Camera: kind 6 passes through untouched (history only); kind 4 gets the head camera position (smoothing, blend, eye hold, correction, aim anchor, seated view, exit hold). |
| Any game.dll caller that sets the near distance of the owned camera to 0.05 or to a value in (0.05, 1.0] | near DLL: CameraAPI `+0x68` | Hip / aim near distance, sight distance pulled back outside full ADS, 45 ms ease. |
| The game's weapon parameter push at `game+0xe9ac4b` | blur DLL: parameter setter table `exe+0x2873b18` | Zeroes three depth-of-field maxima of the held weapon's optic components while the kind-6 request is current. |

Where the game's animation and render fall, as far as the code and comments establish it:

- Animation runs after the kit update. `fp/grip_hand.lua` header (10-15, 38-39): writes to the rig's animated bones were overwritten by the game's animation before they were drawn, which is why both arm modules write local offsets on the linked arm-piece bones (a linked bone keeps its local pose as an offset from its rig bone) and why they read "the rig's animated pose of the previous evaluation" at update time. The runtime also keeps a comment that the aim button reads "up" one frame before the game clears its own aim state (`fp/runtime.lua:388-391`).
- The pose helper is armed before the game's native entry call, "including its immediate dt=0 evaluation" (`fp/backend.lua:627`); `native_ads_editor.cpp:1171-1174` says the frame on which a lease is armed renders before the game writes node 5 for the new weapon, which is why the remembered entry position is written at arming.
- In the camera hook, the camera's own world matrix "can still belong to the preceding frame" (`native_ads_camera.cpp:474-475`) and the rig pose read there "is not final" for the frame (669-670). The game copies the rendered camera pose into `context+0x0C..0x3C` after CameraAPI `+0x20` / `+0x28` (`SOURCE.md:29`).
- Not established anywhere in the source: which thread runs the hooks, and whether the camera evaluator runs before or after animation evaluation in the same frame. Treat both as open and measure them on the target build.

### 2.2 States and transitions

The runtime has no single state variable. What a player sees is the product of the controller phase, the camera kind the game reports, and a few holds inside the camera DLL. The table lists the views; details and every number are in 2.3 and section 3.

| view / state | how it is recognised | what is shown and which pieces act |
|---|---|---|
| Off | controller `phase='idle'`, `enabled=false` | Game untouched. After a stop, each policy's `stop` is retried until clean (`idle_clean`). |
| On, waiting ("head-camera gap view") | `enabled=true`, `phase='waiting'`, no lease; game camera kind 4 | Camera DLL replaces the kind-4 camera position with the head eye (head node + 0.18 m horizontal forward + 0.04 m up, 100 ms low-pass limited to 0.06 m, 120 ms blend from a normal camera). Head hidden only while the camera policy is `active`, armed and error-free. DoF and vignette stay off for the player the session entered with. Arm modules off. |
| On, rig view | `phase='running'`, lease held, camera kind 6, `s.safe` | Game's native first-person camera passes through; pose helper adds placement and motion; FOV, near, blur hooks act; head/torso/shoulders/backpack hidden, rig shadows off; arm pieces and grip hand act (grip hand only while the first four controller bytes are 0). |
| Native ADS inside the rig view | `s.controller_active` (the game's aim controller) on the leased request | Lease kept; placement weight fades to 0 and the ADS placement applies; FOV eases to the sight lens over 180 ms; near keeps the sight's distance; no added motion. `manual_ads_return` is remembered for a blended re-entry. |
| Aim release | right mouse level change or middle mouse press on a running frame, in full ADS, mask 0, guard 0 | `keep_aim_view`: `controller+4 = 0` and `record+0x98 = 0`, once. The native request stays. If the view still drops, the camera DLL's aim anchor covers about 50 ms. |
| Blocked action (swap, vault, dive, stim, stratagem, reload gaps...) | `s.safe ~= true` or camera kind not 4/6: non-zero controller mask, guard byte, seated, not on foot, foreign camera | `manual_wait`: lease released (helper End first, then the game's `exit` only if the request is still the kit's), back to "waiting". Camera DLL: eye hold at the native eye for up to 2.5 s (300 ms of no-action tolerated around a swap), 70 ms glide to a known next weapon's eye, 80 ms fade to the head eye in empty-handed actions, 150 ms correction when no hold can start, exit hold on the frames where the camera is still attached to the rig. |
| Guard holds | controller byte 5 (`controller_bytes:byte(6)`) non-zero | Controller: no entry for 0.75 s after the guard byte was last seen (`GUARD_HOLD`, `fp/controller.lua:19`). Camera DLL: head camera kept for up to 250 ms through a one- or two-frame guard state. Orphaned request (weapon destroyed): the game's `exit` is called after 4 consecutive polls, never during a stim (mask 64). |
| Re-entry | waiting, full snapshot `safe`, kind 4, not aiming, not `controller_active`, `now >= next_manual_entry` and `>= guard_until` | `prepare_pose` then `enter_pose`. At most one attempt per 0.25 s; first attempt 0.1 s after the session starts; never in the same update as a release. After an ADS return on the same weapon the placement blends in over 150 ms (`NADSE_BeginInput`); otherwise steady entry with the 220 ms draw animation (`NADSE_BeginSteadyInput`). |
| Seated (valve) | `s.seated` and kind 4 | Camera unlinked from the rig once (`detach_seated_camera`); camera DLL keeps the head position and turns toward the game's wanted direction with a 180 ms time constant; head hidden only if the head camera is ready. |
| Breaching Hammer | `camera_only_weapon` | No lease is requested; the session stays in "waiting" with the head camera and third-person animations. |
| Suspended by recovery | a core policy (fov, camera...) failed | Controller `suspend` (lease released, session on); retries after 0.25, 0.5 and 1 s for temporary read failures; otherwise stays suspended or the mode stops (`fp/recovery.lua`). |
| Foreign camera | `compat:foreign()` or `handoff_ready=false` | All policies stopped; DoF and vignette restored; controller keeps its session and waits. |
| Cleanup pending | `phase='cleanup_pending'` | `backend:release` retried every update until the helper is disarmed and the request is gone or exited. |
| Editor open | `editor:is_active()` | Motion frozen, draft placement previewed, movement policy stopped, arm modules stay on. |
| Menu test: native compare | menu button, `controller:start(30,'native')` | 30 s timed native request without the pose helper; camera policy stops itself in this mode. Not part of normal play. |

Timers and thresholds that decide transitions (all others are in 2.3):

| value | where | meaning |
|---|---|---|
| 0.1 s | `fp/controller.lua:210` | first entry attempt after the session starts |
| 0.25 s | `fp/controller.lua:456` | minimum spacing of entry attempts |
| 0.75 s | `fp/controller.lua:19` | no entry after the guard byte was seen |
| 4 polls | `fp/backend.lua:1420-1451` | orphaned request exit |
| 0.18 s / 0.25 s | `fp/runtime.lua:670` | shared presentation blend duration / maximum gap before the DLLs fall back to their own timers |
| 150 ms | `profile.pose.blend_ms` | placement blend on a blended entry |
| 220 ms, 0.35 / 0.06 | `native_ads_editor.cpp` | draw animation on a steady entry (drop / back) |
| 300 ms | `native_ads_editor.cpp` | entry rotation guard |
| 180 ms | `profile.fov.ads_blend_ms` | ADS lens ease |
| 100 ms, 0.06 m, 120 ms, 250 ms | `profile.camera` smoothing_ms, lag limit, blend_ms, max_gap_ms | head camera |
| 2.5 s, 300 ms, 70 ms, 80 ms, 150 ms, about 50 ms, 180 ms | `native_ads_camera.cpp` | eye hold limit, no-action tolerance, glide to next weapon, fade to head, correction, aim anchor, seated turn |
| 0.25 / 0.5 / 1 s | `fp/recovery.lua` | policy retry backoff |
| 0.6 s, 0.25 s, 0.5 s | `fp/arm_pieces.lua`, `fp/grip_hand.lua` | quick lookup window, rescan, confirmation scan |
| 20 cm / 32 cm, 0.15 s, 95 % | `fp/grip_hand.lua:105-106` | hand correction full / none, grip ease-in, stretch limit |

### 2.3 Per-module detail

#### Controller, backend entry and exit, one-shot writes, action state, recovery, transition, diagnostics, kit frame

##### controller.lua state machine

State (closure locals, controller.lua:10-25):

- `self.phase`: `'idle'` (initial; nothing happens), `'waiting'` (session on, no lease), `'running'` (lease held), `'cleanup_pending'` (release not yet acknowledged), `'reacquiring'` (timed pose mode only, never on shipped path), `'disabled'` (after `shutdown()`, when clean). There is no phase called "suspended": `suspend()` ends in `'waiting'`.
- `self.lease`: the one camera lease table from `backend:enter_pose` / `enter`, or nil. `self.closing`.
- `enabled` (session on), `manual` (`seconds == 0`, the persistent F11 session), `summary.mode`: `'pose_offset'` (shipped), `'native'` (menu test), `'release_body_event'`, `'unlinked_sights'` (both never started).
- `initial = {identity, player_identity, aiming}`: identity the session is bound to; rebased on every re-entry (475).
- `next_manual_entry`: earliest time of the next entry attempt. `guard_until`: launcher guard hold. `GUARD_HOLD = 0.75` s (19).
- `manual_ads_return = {identity, player_identity}`: "the game's ADS was seen while we held a pose" marker; makes the next entry on the same weapon a blended entry. `manual_has_pose`.
- `placement = {x=0.15,y=0,z=-0.12}` (22): last accepted placement (fallback for `get_placement`).
- `summary`: runs, enter_attempts, release_attempts, observed_native_without_aim, samples, duration, before_aiming, after_aiming, aiming_unchanged, mode, weapon_updates, zero_restores, aim_transitions, reacquisitions, `blend`, `target_weight` (both copied from helper stats, 94), `wait_reason`, `stop_reason`, `suspend_reason`, `transition_epoch`, `transition_progress`, skipped_position, skipped_rotation, body_event_pending, body_exit_event_sent.
- `result()` (148-155) returns a copy of `summary` + `phase`, `cleanup_pending = (lease ~= nil and phase == 'cleanup_pending')`, `enabled`, `manual`, `player_identity = initial.player_identity`. `result().blend` is the pose helper's hip blend.
- `is_clean()` = `lease == nil and not enabled and phase ~= 'reacquiring'` (137). `is_enabled()` = `enabled`.
- Status text: `report()` logs 'Native ADS Camera Test: ' + label on change and calls status listeners (27-33).

Timers and limits (all in seconds of the engine clock `Application.time_since_launch`):

| constant | value | line | effect |
|---|---|---|---|
| first entry delay after session start | 0.1 | 210 | `next_manual_entry = timestamp + 0.1` in `begin_session` |
| entry retry throttle | 0.25 | 456 | `next_manual_entry = timestamp + 0.25` at each entry attempt (prepare or enter may fail; next attempt no sooner) |
| GUARD_HOLD | 0.75 | 19, 391-393 | whenever controller byte 5 (`controller_bytes:byte(6)`) is non-zero, no entry before `timestamp + 0.75` |
| lease duration (manual) | 0 ms = persistent | 319, 466 | `enter_pose(current, token, 0, ...)`; there is no renewal anywhere |
| lease duration (timed) | `min(30500, ceil(seconds*1000))` ms | 319, 552 | not on shipped path |
| timed duration bound | 0 < seconds <= 30 | 235 | default 30 for non-pose modes, 0 for pose_offset (232) |
| reacquisition limit | 6 | 515 | timed pose mode only |
| aim-release wait | 0.25 s or 12 updates | 541 | timed pose mode only |
| body event dispatch delay | 0.1 | 224 | unused mode |

`start(seconds, mode)` (230-337), shipped call `start(0,'pose_offset')`:

1. Refuse: unknown mode; seconds not in 0..30 or 0 with a non-pose mode; `closing` ('Mod is stopping'); `self.lease or phase ~= 'idle'` ('A test or its cleanup is already active').
2. `current = backend:snapshot()`.
3. Manual only (242-258): if there is no valid full snapshot with `player_identity`, or `safe ~= true`, or `camera_kind ~= 4`, or `aiming`, or `controller_active` (or `weapon_ready == false`, which never happens): take `player = current` or `player_snapshot()`; if even that has no identity -> report 'Start refused: ...', return false. Otherwise `begin_session(player, now, 0, mode, nil, nil)` -> phase `'waiting'`, `wait_reason = player.reason or 'Waiting for a safe released firearm camera'`, report 'First person enabled; waiting: ...', return true. (So F11 while aiming, mid-action, seated or without a firearm still switches the session on.)
4. (259-270: refusals for the timed modes; cannot trigger for manual after step 3.)
5. `wait_for_pin` (271-280) returns nil because `backend.pin_first_person ~= true`.
6. Breaching Hammer (`camera_only_weapon(current) == true`, 282-289): `begin_session` without a lease, phase `'waiting'`, `wait_reason = 'Breaching hammer: TPV animations; first-person head camera'`, return true.
7. `prepare_pose(current)`; failure -> report 'Start refused before camera entry: REASON', return false (the session is NOT switched on; the user must press F11 again).
8. `enter_pose(current, token, 0)` (blend_entry nil -> steady entry); failure -> 'Start refused: REASON', return false.
9. `begin_session(current, now, 0, mode, token, lease)` -> phase `'running'`, `manual_has_pose = true`.
10. `self:update()` once immediately; if that ended the session return false; else report 'First person enabled until manually stopped', return true.

`begin_session` (207-229): sets lease/phase, `initial`, `enabled = true`, `manual = (seconds == 0)`, `next_manual_entry = timestamp + 0.1`, `guard_until = 0`, `manual_ads_return = nil`, `started = timestamp`, `deadline = seconds > 0 and timestamp + seconds or nil`, resets all summary counters, `pose_aiming = current.aiming or current.controller_active`.

`update()` (484-585), called once per frame from fp/runtime.lua:608 unless recovery blocks the view:

1. `phase == 'cleanup_pending'` -> `retry_cleanup()`.
2. phase not running / reacquiring / waiting -> return true (idle does nothing).
3. `now()`; clock unavailable or `timestamp < started` -> `stop('Monotonic clock moved backwards')`.
4. `manual and enabled` -> `manual_update(timestamp)` (below). Everything after line 495 is the timed path: timeout -> `stop('test timeout')`; invalid snapshot / identity change -> stop; mode `native`: `aiming or controller_active` -> `stop('Native aiming took control')`, `camera_kind ~= 6` -> `stop('Native camera exited')`, not safe -> stop.

`manual_update(timestamp)` (369-483), in order:

1. `current = snapshot()`; `player = current`, or `player_snapshot()` when the full snapshot is not valid. No player identity -> `manual_wait(reason)`.
2. Drop `manual_ads_return` when the player changed or (full snapshot valid and) the weapon identity changed (379-382).
3. Set `manual_ads_return = {identity, player_identity}` when `manual_has_pose` and the full snapshot shows `controller_active` on the session's identity (383-386).
4. Lease held and `player.player_identity ~= initial.player_identity` -> `manual_wait('Local player or camera context changed')`.
5. Guard: `player.controller_bytes` byte 5 non-zero -> `guard_until = timestamp + 0.75` (390-393).
6. `player.safe ~= true` or camera kind not 4/6 -> `manual_wait(player.reason or 'Gameplay action or camera owns the view')` (394-396). This is what drops the native view for every blocked action (mask non-zero, guard, seated, other camera).
7. Full snapshot valid: `observe(current)` (counters), then `publish_transition(current)`: takes the packet from `backend.presentation_transition()`; when its identity + player_identity equal the snapshot's, `backend:set_pose_transition(lease, packet)`; a refusal -> `manual_wait(problem)` (397-401).
8. Hammer equipped: lease held -> `manual_wait('Switching to hammer head camera')`; else phase `'waiting'`, wait_reason hammer text, return (402-411).
9. Holding branch (412-446): `phase == 'running'`, full snapshot safe, `identity == initial.identity`, `camera_kind == 6`, and record equals `lease.request`:
   - `lease.cleanup_only` -> `stop('Pose helper entry requires cleanup')`.
   - `pose_stats()` (`backend:pose_status(lease)` -> NADSE_ReadStats).
   - stats missing, or `armed ~= true`, or `expired`: take a fresh snapshot; `retiring_owner = lease.pose_owner_retired == true or error == 7 or error == 8`; if the player is gone / different / unsafe / wrong camera kind, or the fresh full snapshot is invalid, has another identity, is kind 4, has another record, or `retiring_owner` -> `manual_wait(problem or 'Native pose owner changed')`; otherwise `stop(problem or 'Pose helper is no longer armed')` (session OFF).
   - else: if `not controller_active and stats.target_weight == 1 and stats.blend == 1` clear `manual_ads_return`; return true.
10. Still holding a lease here (camera went to kind 4, weapon identity changed, record changed, or full snapshot failed) -> `manual_wait('Native camera request or firearm changed')` (447-450).
11. No lease: phase `'waiting'`. Entry requires all of: full snapshot with player identity, `safe == true`, `camera_kind == 4`, not `aiming`, not `controller_active`, `timestamp >= next_manual_entry`, `timestamp >= guard_until` (452-453). Otherwise return true.
12. `next_manual_entry = timestamp + 0.25`; `prepare_pose(current)`; failure -> report 'First person enabled; waiting: REASON', return true.
13. `blend_entry = manual_ads_return` matches this identity and player (463-465). `enter_pose(current, token, 0, blend_entry)`; failure -> report waiting, return true.
14. Success: lease stored, phase `'running'`, `manual_ads_return = nil`, `manual_has_pose = true`, `initial` rebased to the new identity, `wait_reason = nil`, `summary.reacquisitions + 1`. `lease.cleanup_only` -> `manual_wait('Pose helper entry requires cleanup')`. `publish_transition(current)`; refusal -> manual_wait. Report 'First person enabled; native camera reacquired'.

Consequence: a release and a re-entry never happen in the same update (step 10 returns); the earliest re-entry is the next frame, and not before 0.25 s after the previous entry attempt.

`manual_wait(reason)` (339-350): `summary.wait_reason = reason`; with a lease -> `retry_cleanup()` (failure returns false and leaves `'cleanup_pending'`); without -> phase `'waiting'`; report 'First person enabled; waiting: REASON'.

`retry_cleanup()` (156-181): no lease -> true. Sets phase `'cleanup_pending'`, `release_attempts + 1`, `backend:release(lease)`. Not done -> report 'Cleanup pending: REASON', return false (retried by the next `update()`, step 1). Done: accumulate final pose stats; if `manual and enabled` -> lease nil, phase `'waiting'`, report 'First person enabled; waiting for a safe released firearm camera'. Otherwise clear every session local, phase `'disabled'` when closing else `'idle'`, report 'Stopped: REASON'.

`stop(reason)` (182-202): `enabled = false`; clears `manual_ads_return`; final `pose_stats()`; `summary.stop_reason = reason or 'manual stop'`; no lease -> clear locals, phase idle/disabled, 'Stopped: ...'; with a lease -> `retry_cleanup()` (which now takes the full-clear branch). `shutdown()` = `closing = true; stop('mod disabled')`.

`suspend(reason)` (351-357): only for `enabled and manual and mode == 'pose_offset'` and not closing; `summary.suspend_reason = reason`; `manual_wait(reason)`. Used by recovery (fp/runtime.lua:307, 347). The session stays on; the lease is released.

`set_placement(value)` (110-135): validates x,y,z within +-0.5 and yaw,pitch,roll within +-90; running with a lease -> mode must be `pose_offset`, `backend:set_pose_placement(lease, value)`; otherwise `backend:set_placement_defaults(value)`; stores `placement`. `get_placement()` (102-105) returns `backend.pose_placement or placement` as x,y,z,yaw,pitch,roll.

`placement_target()` (106-109): nil unless `phase == 'running'`, a lease and `backend.placement_weapon_key`; returns `backend.placement_weapon_key, lease.origin.identity` (the weapon key and identity the placement editor must still match when saving).

`on_status_changed(listener)`, `display_status()`, `status()` (label + '; camera6/aimfalse observations=N' + '; arms/weapon offset updates=N; hip blend=B; aim transitions=N').

##### backend entry/exit

`prepare_pose(expected)` (backend.lua:1023-1053), read-only:

1. Refuse while `warm_lease` or `alignment_lease` exists and is not `done` ('Previous native helper lease has not completed cleanup').
2. `normal()`: fresh `snapshot()`; `expected.identity == s.identity`; `safe`, `camera_kind == 4`, not `aiming`, not `controller_active` ('Pose preparation requires a safe normal camera with real aiming released').
3. `_module('full')`: ALL 14 `checks` pins must be readable and equal, including the cold packed pages (evaluator 0x1118370, root_position_call 0x111a809, root_rotation_call 0x111a829, pose_branch_position_call 0x111a460); unreadable -> 'Waiting for native ADS camera initialization (NAME)'.
4. `_pose_context(s)` (viewmodel + weapon unit objects through the executable's unit registry).
5. `_pose_helper()` loads fp/pose.lua (DLL `native/native_ads_editor_26568081_4940b83e17ad.dll`).
6. `helper:prepare(config)` with `config = _pose_config(s, s.context+0x200+s.tail*0xe8, context)`: adapter requires stats readable and not armed, then `NADSE_ValidateInput(config)` must return 1 and helper state unchanged. Side effect of `_pose_config`: resolves the weapon key and placement and stores `pose_placement`, `placement_weapon_key`.
7. `normal()` again + `_pose_context` again; contexts must be identical.
8. Token: an empty table used as weak key in `pose_tokens`, value `{identity, context, helper, native={owner=helper}, used=false}`. Single use.

`enter_pose` -> `_enter(expected, token, 'pose', remaining_ms, blend_entry)` (567-659):

1. Hook `camera_before_entry()` (in `enter_pose`, 674-677): `compat:prepare()`, `blur:stop()`, `camera:prepare()` must all succeed.
2. `s = snapshot()`; `expected.identity == s.identity`; token found, unused, same identity.
3. `safe`, kind 4, not aiming, not controller_active ('Entry requires normal on-foot camera with real aiming released').
4. Game call `allowed(controller, weapon)` at game+0x1144d60 through `_invoke` (full pin check + executable-page check first); must return non-zero ('Weapon does not support native FPV').
5. Snapshot again (`current`); same identity and still eligible ('Owner changed during preflight').
6. Lease created BEFORE any mutation: `lease = {origin = current, request = current.context + 0x200 + current.tail*0xe8, pose_blend_entry = (blend_entry == true)}`. The request address is the ring slot at the OLD tail, which the game's constructor is about to fill.
7. `_pose_context(current)` must equal the prepared context. `lease.alignment = {helper, prepared, context, ended=false, kind='pose'}`; `prepared.used = true`.
8. Arm the helper: `helper:begin(_pose_config(current, lease.request, context, remaining_ms, blend_entry), prepared.native)`. Config: game, exe, context, request, avatar_owner, avatar_index, avatar, weapon, body, viewmodel, duration_ms (0 on the shipped path = no expiry), weapon_descriptor (address), weapon_unit, blend_ms=150, offset_x/y/z = resolved idle placement, `steady_entry = (remaining_ms == 0 and blend_entry ~= true)`. Export: `NADSE_BeginSteadyInput` when steady_entry, else `NADSE_BeginInput` (fp/pose.lua:233-238; "Input" because `pose.normalize_zoom_input=true`). Result 1 -> `lease.alignment.handle = handle`, `backend.alignment_lease = lease`. Otherwise: if the adapter reports an owned handle (Begin returned 2 = partial install) the lease is returned with `cleanup_only = true` and `error`; else 'Alignment arm refused: ...' and nothing is owned.
9. Final snapshot: same identity, kind 4, safe, not aiming, not controller_active, same `tail` and `record`, and same pose context; otherwise the lease is returned with `cleanup_only = true`, error 'Owner or viewmodel changed after alignment arm; native entry skipped' (the game's enter is NOT called).
10. Game call `enter(context, avatar, weapon)` at game+0x110fc80: `void (*)(void *, uint32_t, uint32_t)` with RCX = context (the wrapper reloads RCX itself from its global, profile.lua:17), EDX = avatar entity, R8D = weapon entity. A Lua error -> `lease.error`, `cleanup_only = true`, log 'Native entry error: ...'.
11. Read back: `lease.entry_aim_changed`, `lease.entry_controller_changed` (whether the call itself flipped `aiming` / `controller_bytes`). Log line 'Native entry: aim before=..., after=...; controller before=..., after=...; camera=K; packed flags unchanged=B'.
12. Returns `true, lease` (also for cleanup_only leases; the controller then releases them).

Lease objects and lifetimes:

- pose token: `prepare_pose` -> consumed by `_enter` (or garbage collected).
- camera lease (the table above; later fields `alignment_stats`, `pose_stats`, `pose_error`, `pose_owner_retired`, `pose_retire_reason`, `orphan_polls`, `done`): from `_enter` until `release()` sets `done = true`. `backend.alignment_lease` keeps pointing at the last lease and is only tested with `.done`.
- helper handle (fp/pose.lua:231): `{owner, disarmed, stats, generation = uint64[1]}`; from Begin to `NADSE_End`.
- per-frame `pose_phase_publication` (dedupe of transition packets), cleared by begin_frame/end_frame.
- Renewal: none. The persistent lease has `duration_ms = 0`. What is refreshed every frame is the transition packet (`set_pose_transition`, "the helper's expiring phase heartbeat", backend.lua:972-973) and the motion transform.

`release(lease)` (1397-1469), called by `controller:retry_cleanup()`:

1. `_stop_alignment(lease)`: `helper:finish(handle)` = `NADSE_End(generation)`; must succeed first, else return false 'Alignment disarm pending: ...' (the helper is always disarmed before the camera is touched).
2. `lease.done` -> true 'Already clean'.
3. `_top(_module())` (full pin check); unreadable -> false 'Waiting to verify native camera cleanup: ...'.
4. base/context/camera differ from origin -> `done`, 'Engine replaced camera context'.
5. Newest request is not kind 6, or its avatar/weapon differ from origin, or its record address is not `lease.request` -> `done`, 'Native camera request was replaced' (nothing to exit: the game already retired our request; this is the normal outcome after the game's own aim-release exit, a weapon swap, a vault ...).
6. Our request is still on top. `s = snapshot()`, or the retiring form `_snapshot(false, origin)` (requires same player_identity, same viewmodel, kind 6, request_weapon == origin.weapon, safe, not aiming, not controller_active).
7. Neither snapshot works (the request's weapon no longer exists; expendable launcher): orphan path (1420-1451). `player_snapshot()` with same player_identity, base, context, kind 6, not controller_active and controller mask u32 ~= 64 (64 = stim, whose native view must stay) -> `lease.orphan_polls + 1`; at `ORPHAN_POLLS = 4` consecutive polls call the game's `exit(context)` with `{base, context}` and verify the top request changed -> `done`, log 'Owned native camera exit acknowledged after its weapon was destroyed'. Otherwise reset the counter. Return false 'Waiting for owned native request to retire: ...'.
8. Snapshot identity differs or record differs -> false 'Owner changed; waiting for native request retirement'.
9. `(s.aiming or s.controller_active)` and none of `entry_aim_changed`, `entry_controller_changed`, `body_event_*` -> `done`, 'Player aiming took ownership' (the player is in the game's own ADS on our request; leave it to the game).
10. Game call `exit(context)` at game+0x11108a0: `void (*)(void *)`, RCX = context (pin + page checks first).
11. Read `_top` again: if the top is still kind 6 with our avatar and weapon on the same context -> false 'Native ADS request still present'; else `done`, log 'Owned native camera exit acknowledged', return true 'Native camera exited'.

The only memory the Lua side writes in enter/exit is nothing: all mutation is done by the three game functions and by the helper DLL.

##### keep_aim_view / detach_seated_camera / other one-shot writes

`keep_aim_view(s)` (backend.lua:1340-1365), called from fp/runtime.lua:536-538 at the very end of the frame, on a frame where `aim_released()` reports an edge AND `state.enabled`, `state.phase == 'running'`, recovery not blocking, no foreign camera, editor not active:

- `aim_released()` (fp/runtime.lua:395-422): edge = the right mouse button level (`stingray.Mouse.button(Mouse.button_index('right'))` > 0) changed since the last frame (either direction), or the middle mouse button went down. Hard-wired to the mouse buttons, not to the key binding.
- Conditions in the backend: `Profile.camera.keep_aim_view == true`; `s.safe == true`; `s.camera_kind == 6`; `s.controller_active == true`; not seated; `controller_bytes` is 6 bytes with u32 mask == 0 and byte 5 == 0; and the 6 bytes re-read at `s.controller` are unchanged. `s` is `view_snapshot()` = `player_snapshot()`.
- Write 1: byte at `controller + 4` = 0 (the aim controller's "active" byte). Raw pointer store, no VirtualProtect.
- Write 2 (when `s.record` is a number and `s.request_local`): u32 at `record + 0x98` = 0 (the float sight distance of the camera request, so the pose helper's scope-distance correction works again).
- `aim_view_keeps + 1`.
- Effect per the comment (1334-1339, profile.lua:85-89): the game's aim controller only exits the native request while that byte is set, so the owned request simply stays through the release; must be done once, never per frame.

`detach_seated_camera()` (backend.lua:1372-1394), called from fp/runtime.lua:540-542 once on the frame `seated_view` becomes true (`s.seated == true and s.camera_kind == 4 and state.enabled and not foreign and not edit`):

- Conditions: `Profile.camera.seated_head == true`; `player_snapshot()` with kind 4, seated, request_local.
- `object = ptr[camera + 0x18] - 0x60`; if the 8 bytes at `object + 0x1D0` are zero (camera has no parent link) -> nothing to do.
- `api = ptr[ptr[game + 0x2ce14c8] + 8]`; `fn = ptr[api + 0xF0]`; `world = ptr[context + 0x180]`; `unit = u32[context + 8]`.
- `VirtualQuery(fn)`: committed, protect 0x20/0x40/0x80. Call `void fn(void *world, uint32_t unit)`: the same unlink call the game's exit makes at game+0x111093e. `seated_detaches + 1`.

No other write exists in fp/backend.lua or fp/controller.lua. (fp/native_ads_option.lua writes through its own `src/memory.lua` instance; other part.)

##### action_state / action_placement

Animation state read (`action_state.lua:47-126`, per full snapshot, layers `{1,3,14}`):

1. Body unit object = `_alignment_context({viewmodel = s.body})` (unit registry lookup of the BODY ref).
2. vtable = ptr[object]; ptr[vt+0x1B0] must be exe+0x28d510 with bytes `48 8B 81 78 01 00 00 C3`; ptr[vt+0x1B8] must be exe+0x28d520 with bytes `48 8B 81 58 03 00 00 48 89 02 48 8B C2 C3`.
3. resource = 8 bytes at object+0x358; instance = ptr[object+0x178]; graph = ptr[instance+0x28]; header = 12 bytes at graph: layer count u32 at +4 (1..32), table offset u32 at +8 (12..0x10000); `table_at = graph + table_offset`; definitions = `count*4` bytes at table_at+4 (u32 offset per layer); active = ptr[instance+0x50] (array of 8-byte pointers, one per layer).
4. Per layer L (1-based): `def = table_at + offset[L-1]`; state count n = u32[def+8] (1..2048); entries = n u32 offsets at def+12; map `def + entry[i]` -> i (first wins; cached per layer while def/count are unchanged). Active pointer p = 8 bytes at `active + (L-1)*8`: p == 0 -> state -1; found -> its index; not found -> 0 and the layer is listed in `unknown_layers`.
5. Everything is re-read and compared after (player identity, body generation, instance, graph, header, definitions, each active pointer).
6. Result: `{owner, body, body_object, graph, instance, layer_count, resource (upper-case hex, byte-reversed), resource_match = (resource == 97 FA 4D 29 4D 33 1C 4D and count == 31), states, l1, l3, l10, l11, l14, unknown_layers, sprint_hypothesis = match and (l1 == 78 or l1 == 79 or l3 == 71), sprint_evidence}`.

What the mode uses of it: `on_foot` (l14 == 36 mission on-foot; l14 == 3 ship/bridge with flags&6 == 2 and scene 3/4) for `safe`; sprint (fp/locomotion.lua:85-93): flags byte 9 bit 2 set (sprint gait), or `l3 == 71`, or `l14 == 3 and l3 == 68 and (l1 == 75 or l1 == 78)`, all only with resource_match, 31 layers and (l14 == 36 or verified l14 == 3).

The motion sample (fp/locomotion.lua:149-155; built once per frame by motion_runtime while a lease is running) carries: `ads = s.controller_active`, `shoulder_aim = s.shoulder_aim and not s.controller_active`, `stance_flags = s.flags`, `action_resource`, `action_verified = action.resource_match`, `sprint`, `reload = s.reloading`, `l1`, `l3`, `l14`.

Action selection `M.select(sample, mapping)` (action_placement.lua:122-138), first match wins:

1. `sample.ads == true` (or a number >= 1) -> `'ads'`.
2. stance from flags (only with resource '4D1C334D294DFA97' and verified, 54-65): `posture = floor(flags byte 9 / 32) % 4 * 32`; posture 0x40 and flags byte 12 bit 5 -> `'dive'`; posture 0x20 and flags byte 10 bit 1 -> `'slide'`; posture 0x20 -> `'crouch'`; posture 0x40 -> `'prone'`. `'slide'` / `'dive'` are returned here.
3. `sample.reload` -> `'reload'`.
4. `sample.shoulder_aim` -> `'aim'`.
5. `sample.sprint` -> `'sprint'`.
6. stance `'crouch'` / `'prone'`.
7. optional user mapping `config.action_state_mapping[resource][l1 .. ':' .. l3]` (shipped `{}`).
8. `'idle'`.

(byte numbers are 0-based offsets into the 24 flag bytes; the Lua uses `f:byte(10)`, `f:byte(13)`, `f:byte(11)`.)

Action list (66-67): idle "Idle", aim "Normal aim", ads "ADS", sprint "Sprinting", reload "Reloading", crouch "Crouching", prone "Prone", slide "Sliding", dive "Diving".

Weapon key: lower-case hex of the 8 resource-id bytes at weapon unit object+0x358 in memory order (backend.lua:845-847). `configure_catalogue(gear)` (25-41; called once at enable with `src/loadout_catalogue.lua`.gear, fp/runtime.lua:650) maps every catalogue row that has a family and a 16-hex `type` to that key by byte-reversing the catalogue's `type` string; a key that appears with a different name or family is marked ambiguous (`false`) and gets no family.

Family of a catalogue row (13-24): name not a string or starting 'CQC', 'Item ', 'Unidentified', 'B/' or equal 'Breaching Hammer' -> none; `kind == 3` -> sidearm; `kind == 2` -> support; `kind ~= 1` -> none; name starts 'SMG', 'MP-', 'M7S ', 'StA-11 ' -> compact; 'SG-', 'DBS-', 'M90A ' -> shotgun; 'R-', 'R/' -> marksman; 'LAS-', 'PLAS-', 'ARC-', 'FLAM-' -> energy; 'AR', 'BR-', 'MA5C ', 'StA-52 ' -> rifle.

Family baselines (7-12), x / y / z, angles 0: rifle .15 / 0 / -.12; compact .12 / 0 / -.09; sidearm .105 / 0 / -.08; marksman .14 / .015 / -.115; shotgun .14 / .005 / -.11; energy .15 / 0 / -.115; support .16 / .015 / -.14.

`resolve(config, key, action)` (79-102) -> placement, is_explicit:

1. `config.action_placements[key][action]` if valid; else `config.action_placements.__all[action]` if valid -> that, true.
2. action `'ads'` -> `{x=0,y=0,z=0}`, false.
3. any other non-idle action -> `resolve(config, key, 'idle')`, false.
4. idle: legacy `config.weapon_placements[key]`, else `weapon_placements.__all` if valid.
5. `config.placement` (shipped .15 / 0 / -.12): if `config.family_placement_defaults == true` and the placement is missing or exactly the standard .15 / 0 / -.12 with zero angles -> the family baseline of the key when there is one.
6. `config.placement` if valid, else `{x=.15,y=0,z=-.12}`.

`valid(p)`: x,y,z numbers with abs <= .5; yaw,pitch,roll (default 0) abs <= 90. `default_placement(config,key,action)`: ads -> zeros; else family baseline (if enabled) or .15/0/-.12. `saved(config,key,action,value,all)` builds the new `action_placements` table (with `all` it writes under `__all` and removes that action from every weapon row). `weapon_info(key)` -> `{key,name,family,kind,item}`.

Who consumes it: the backend's entry offset is always the IDLE placement (`placement_resolver(key)` -> `resolve(config,key,'idle')`, backend.lua:848). Per-action placements are applied afterwards by fp/motion_runtime.lua:318-345 through hooks `backend.action_placement(key, action)`, `backend.action_mapping()`, `backend.action_placement_revision` (bumped on every save / reset / family toggle) and `set_ads_placement`.

##### recovery / transition / diagnostics

Recovery (`recovery.lua`, instance with defaults: `max_attempts = 4`, `base_delay = .25`, `max_delay = 2`, `stable_seconds = 2`):

- Policies wrapped by `managed_update` (fp/runtime.lua:278-311, 624-630): `fov`, `blur`, `camera`, `head`, `movement`. Core policies (line 7): `camera`, `fov`.
- `classify(name, state, clean, proof)` (11-22): `clean ~= true` -> `'cleanup'`; `state.failure_kind == 'temporary_read' and state.failure_native_error == 3` and positive proof (`fresh`, `safe`, `same_player` all true, `foreign == false`) -> `'retry'`; non-core -> `'unavailable'`; else `'fatal'`.
- `failure(name, state, clean, proof, now, reason)` (46-61): `attempts + 1`; `proof_deadline = now + 10`; phase: not clean -> `'cleanup'`; retry with attempts < 4 -> `'waiting'`, `next_retry = now + min(2, .25 * 2^(attempts-1))` (0.25, 0.5, 1.0 s); retry at the 4th failure -> `'exhausted'`; `'fatal'`; `'unavailable'`.
- `cleanup(name, done, now)`: leaves phase `'cleanup'` once the policy is stopped and clean, then as above.
- `action(name, now)` (75-87): no row / healthy -> `'update'`; cleanup -> `'cleanup'`; waiting / waiting_proof before `next_retry` -> `'wait'`, after -> phase `'retrying'`, `'update'`; retrying -> `'update'`; otherwise the phase name (`exhausted`, `fatal`, `unavailable`).
- `revalidate(name, proof, now)`: a retrying row without positive proof -> `'waiting_proof'` with `next_retry = now + .25`, or `'exhausted'` once `now >= proof_deadline`.
- `success(name, now, active)`: only when the policy reports `phase == 'active'` and armed: phase `'healthy'`; after 2 s healthy the row is deleted.
- `blocks_view(now)` (110-116): true (plus name and wanted action) when any CORE row's action is not `'update'`. While true, fp/runtime.lua:608-609 does not call `controller:update()` (only `retry_cleanup` if a lease is held), the diagnostics mark `recovering`, the aim-view keeper is disabled, and the menu shows 'First person suspended: NAME (PHASE)'.
- On a managed failure (fp/runtime.lua:296-310): the policy is stopped; core + fatal -> `controller:stop('First-person NAME fault: WHY')`; core otherwise -> `controller:suspend('Recovering NAME: WHY')`; non-core -> nothing more (that policy stays off for the session).
- `reset()` on every enabled/disabled edge and on start/stop; `owner_reset()` after a new body was drained (keeps `'unavailable'` rows).
- New-owner drain (`recover_new_owner`, fp/runtime.lua:325-362): only when the ledger has rows and the snapshot shows another player_identity with another body or world: suspend, stop every policy each frame until all are clean, then one more frame with `safe` and no foreign camera -> `recovery:owner_reset()`, `presentation:reset('local body changed')`.

Transition (`transition.lua`, `new({duration=.18, max_gap=.25})`; `max_step = duration/3 = .06`, `merge_until = .5`):

- Fed once per frame by `observe_presentation` (fp/runtime.lua:364-387) with a copy of the full snapshot (fallback pin_player_snapshot), plus `zoom_identity = depth.zoom_index` from the pose helper's `depth_stats` when supported and armed, and `controller_active` REPLACED by `native_ads(value)`: controller byte 4 non-zero, byte 5 zero, not seated, and mask u32 == 0 or 2 (9-15). `enabled = controller:is_enabled()`, `blocked = foreign_camera()`.
- `update(s, now, enabled, blocked)` (46-78): not enabled or blocked or no usable snapshot -> packet retired (nil). If `now < previous.time`, `now - previous.time > 0.25`, or player_identity changed -> retire, then treated as `'initial'`.
- Edge reasons in priority order: `'initial'` (no previous), `'weapon'` (identity changed), `'ads_enter'` / `'ads_leave'`, `'zoom'`, `'shoulder'`, `'action'`, `'seat'`, `'camera'` (camera kind changed, except between 4 and 6).
- A new edge starts a new packet: `epoch + 1`, `progress = 1` for `'initial'` else 0, `ads`, `reason`, `identity`, `player_identity`, `started = now`, `duration = .18`, `continuous = (reason is not initial and not weapon)`. Exception (merge): reasons zoom / shoulder / action arriving while the current packet is continuous, has the same `ads` and `progress < .5` only advance it and set `packet.merged`.
- Without an edge the packet advances: `progress = max(progress, min(1, progress + clamp(now - previous.time, 0, .06) / .18))`: linear, at most a third of the blend per frame.
- `action(action, now)` (82-93): called by motion_runtime through `backend.presentation_action` after it selected the action; a changed action starts (or merges into) an `'action'` packet. This is the only source of action edges: the snapshot passed to `update` has no `action` field.
- `resume(identity, player_identity, now)` (94-101): called by motion_runtime (line 261) through `backend.presentation_resume`; starts a `'reacquire'` packet at progress 0 when the current packet belongs to the same identity.
- `epoch` = integer counter of presentation changes (never reused within a session of the module); `progress` = 0..1 position of the 0.18 s blend. Consumers: `controller.publish_transition` -> `backend:set_pose_transition` -> pose helper `NADSE_SetTransition(generation, epoch, progress, ads)`; `backend:set_pose_motion` (publishes first); fp/motion_runtime.lua:133-134 (`shared_phase`, placement blends); fp/fov.lua (2 refs); camera reports its own `transition_epoch`; diagnostics compares fov/camera/pose epochs (`mixed_publication_frames`).

Diagnostics (`diagnostics.lua`, `new({capacity=720})`, default 360, allowed 16..4096):

- `record(frame, sample)` once per frame from `record_presentation` (fp/runtime.lua:513-534) with `frame = ready_context.tick`; rejects duplicate / out-of-order frames; stores only whitelisted scalar fields (lines 4-14): time, enabled, player, weapon, request, camera_kind, ads, foreign, editor, phase, transition_epoch, transition_progress, pinned, pin_expected, camera_retired, view_expected, view_visible, recovering, fov_native, fov_applied, camera_blend, pose_blend, camera_epoch, fov_epoch, pose_epoch, look_sequence, look_samples, look_skipped, cleanup_pending, reason, policy, event, placement_x/y/z/yaw/pitch/roll, motion_x/y/z, look_yaw, look_pitch, ads_rebases, control_mask, control_guard, safe, aiming, snapshot_reason, weapon_ready, entry_block, action, softened, head_calls, camera_gate, head_phase, cam_x/y/z, cam_fx/fy/fz, vm_lx/ly/lz, vm_cx/cy/cz, gun_cx/cy/cz, gun_turn, head_distance. Strings are cut to 200 characters.
- `event(frame, name, sample)`: extra row (`policy_failure` from managed_update).
- Counters: frames, duplicate_frames, out_of_order_frames, events, overwritten, player_changes, weapon_changes, request_changes, camera_changes, transition_restarts (epoch changed while previous progress < 1), mixed_publication_frames, unexpected_view_frames (`view_expected and not view_visible` and not foreign/editor/recovering), look_duplicates, cleanup_frames.
- `snapshot()` -> `{capacity, count, counters, frames}`; `clear()`. Read only by `NativeADSCameraTest.diagnostics()` / `.clear_diagnostics()`; nothing in the mode acts on it.
- The per-frame record itself costs memory reads (fp/runtime.lua:456-512): camera world matrix, rig node 5 local + world position, weapon root world matrix; and one extra full `snapshot()` whenever the session is on but not running (447-451).

##### kit frame

`main.lua` on_update(dt) (53-63), skipped when there is no controller or `stopping`:

1. `backend.mem:begin_frame()` (kit `src/memory.lua:262-265`: drops its read snapshot cache and increments `_frame`, which lets the kit memory reuse its module validation for one frame). This is the KIT backend, unrelated to the mode's reads.
2. `menu:update(dt)` (kit MCM; registers the pages, among them the first-person pages obtained through `first_person.pages()`, and calls `FirstPerson.attach(handle)`).
3. `controller:update(dt)` (kit unlock controller).
4. `equipment:update(dt, controller:equipment_ready(), controller.categories)`.
5. `first_person.update()`.
6. `spawner:update(dt)`, `mission:update(dt)`, `menu:refresh_status()`.

`first_person.update()` (src/first_person.lua:133-145): no runtime -> return; not active -> `poll_cleanup()`; `pcall(runtime.on_update)`; on a Lua error: `active = false`, `last_error`, log 'First person stopped: ERROR', and from then on only `poll_cleanup()` runs each frame (the mode stays off until the mod is re-enabled); finally `pcall(M.sync, DBFMCM.mods['goose_temporary_unlocks'])` to copy text labels and disabled flags to the kit's registered control copies.

`first_person.enable()` (119-132): requires fp/runtime.lua, `pcall(runtime.on_enable)`; failure -> `runtime.on_disable`, log 'First person initialization: ERROR'. `shutdown()` / `poll_cleanup()` call `runtime.on_disable` / `runtime.on_cleanup_poll` and report pending restoration. `main.lua` shutdown (4-28): kit memory begin_frame, equipment/spawner/mission shutdown, menu begin_shutdown, first-person shutdown (or `poll_cleanup` on the loader's cleanup poll), kit controller shutdown, menu shutdown; returns false + reason while anything is pending.

Mode frame (`fp/runtime.lua:1205-1225`): `qualified_frame` = `backend:begin_frame()`, `ready_tick + 1`, `ready_context = {phase='qualified_update', tick}`, `pcall(update_frame)`, `ready_update()`, `aim_markers:update()`, `record_presentation`, then `ready_context = nil`, `backend:end_frame()`.

Mode backend frame semantics (backend.lua:33-42): the frame caches ONLY immutable image validation: `_module(scope)` (PE header + all pins) once per scope per `frame_epoch`, and `_alignment_executable(profile)` once per profile table per epoch. `frame_epoch` is bumped (cache dropped) before every game function call and when a module base changes. "Ownership reads remain fresh": every `snapshot()` / `player_snapshot()` re-reads the whole pointer chain with `ReadProcessMemory`, and one frame calls them many times (controller, recovery proof, presentation, every setter, every policy, diagnostics). Outside a frame (`frame_active == false`) nothing is cached, and `set_pose_transition` refuses ('Transition publication requires the qualified Update').

#### Pose helper (Lua side)

##### Lua adapter and backend, per event

**Preparation (F11 pressed, or the manual controller re-acquiring; `backend.lua:1023-1053`).** Snapshot must be a safe normal camera (kind 4) with aiming released; `_pose_context` resolves rig unit and weapon unit through the unit registry; `_pose_config` builds the config (`backend.lua:839-857`): `duration_ms = remaining_ms or 30500`, `blend_ms=150`, offsets from the saved per-weapon placement (`placement_resolver(key)`, key = 8 bytes at `weapon_object+0x358` as hex) or the defaults 0.15 / 0 / -0.12. `pose.lua:prepare` (`212-222`) calls `NADSE_ReadStats(0)`, `NADSE_ValidateInput`, `NADSE_ReadStats(0)` again and returns a token. Nothing is written to the game.

**Begin (`backend.lua:567-659`, `pose.lua:224-249`).** `controller:start(0,'pose_offset')` calls `enter_pose(current, token, 0)` (`controller.lua`, `math.min(30500, math.ceil(0*1000))` = 0). So `duration_ms = 0` and `steady_entry = true`: the DLL call is `NADSE_BeginSteadyInput`. The helper is armed before the native camera entry call (`backend.lua:627`), and `lease.alignment = {kind='pose', helper, handle, context}`. If Begin returns 2 or throws after publishing a generation, the lease is kept as `cleanup_only` so `End` is still called.

**Per kit update, values sent to the DLL.**
- `controller` manual update: `publish_transition` -> `NADSE_SetTransition(generation, epoch, progress, ads)` once per update (deduplicated within one update by `backend.lua:960-966`); then `pose_status` -> `NADSE_ReadStats` (`backend.lua:1113-1153`). The runtime calls `pose_status` again (`runtime.lua:984`) and `depth_stats` -> `NADSE_ReadDepthStats` in its presentation observation (`runtime.lua:378`).
- Placement editor (F8) while dragging: `controller:set_placement{x,y,z,yaw,pitch,roll}` -> `NADSE_SetPlacement(x,y,z)` only; yaw/pitch/roll are kept in Lua (`backend.pose_placement`) and reach the DLL as part of the motion quaternion (`motion_runtime.lua:287-295` while editing: phase `editor_frozen`, translation 0, rotation from the draft angles, sent directly without the pop guard).
- `motion_runtime:update` (`motion_runtime.lua:220-405`): on a new lease or new placement revision `NADSE_SetADSPlacement(ads.x, ads.y, ads.z)` (`:326-328`); when the action-specific placement target changes it eases `x,y,z,yaw,pitch,roll` toward it and calls `controller:set_placement` -> `NADSE_SetPlacement` (`:343-362`; blend = shared phase step, or `1-exp(-min(dt,0.1)/0.10)`); then the motion model output (bob, sway, sprint, shoulder) plus the placement angles is converted to node 5's parent space (`to_parent`) and sent as `dx,dy,dz,qx,qy,qz,qw` through `backend:set_pose_motion` -> `NADSE_SetTransform`, or `NADSE_SetAuthoredTransform` when `authored_ads` (`:396`, `:173`). Identical consecutive values are not re-sent (`:113-117`). `set_pose_motion` re-publishes the transition first (`backend.lua:1012-1016`).
- Every setter first re-checks in Lua that the camera is kind 6 on the leased record and the pose context is unchanged (`backend.lua:890-897,1004-1011`), and in C re-checks ownership, graph, dispatch and the sight node; a C-side failure disarms the lease (`editor.cpp:1544-1551`).
- Bounds: placement each axis within 0.5 m, angles within 90 degrees (Lua only); motion each axis within 0.15 m; quaternion |norm^2-1| <= 0.002 and |w|/norm >= 0.5 (Lua `pose.lua:279-293`, `backend.lua:985-999`; C `editor.cpp:1523-1535`, which also normalises and flips to w >= 0).

**What the DLL does during the game's own update.** Setters only store values under `policy_lock`; "the next matching native call applies them to its fresh baseline" (`editor.cpp:1552-1553`). On each run of the game's first-person camera evaluation for the owned kind-6 request the hooks, in order: optionally swap the look-gain preset index (hip/normal aim only) and restore it when the evaluator reaches the camera-node getter; record the lens/distance observation; add `(placement + motion) * weight` (+ ADS placement in full ADS, + zoom depth term, - draw-animation offset) to the game's fresh node 5 position; shift the rig root node 0 back along its forward axis by the depth term; multiply the stored rotation onto the game's fresh node 5 rotation (or substitute the settled rotation during the 300 ms entry guard). The game's own arm/weapon animation value is consumed fresh every call; nothing is filtered or replayed (`editor.cpp:1073-1074`). Ordering against animation and render is only established as: Lua `Begin` on the arming frame runs before that frame's node 5 write, and that frame can render with the previous weapon's node 5 position unless the remembered entry position is written (`editor.cpp:1171-1174`).

**Lease lifetime, expiry, renewal.**
- Shipped F11 path: `duration_ms = 0` -> `policy.deadline = 0`: no timer, no expiry, no renewal. The lease lives until `NADSE_End` or a guard failure in a hook or setter (owner/graph/dispatch/sight node/invalid pose), which sets `armed=0` and an error code.
- `profile.pose.duration_ms = 30500` is only the upper bound checked by `enter_pose` (`backend.lua:666-669`) and the default when `remaining_ms` is nil. Timed leases (1..30500 ms) exist only for the test modes `controller:start(seconds>0,'pose_offset')`, where the controller re-enters with the remaining time after an ADS handoff; not on the F11 path.
- Lua notices a dead lease on the next `pose_status` (`armed ~= true` or `expired` or `error ~= 0`), calls `finish` -> `NADSE_End`, and the manual controller goes to "waiting" and re-acquires when the camera is back to normal kind 4, aim released, weapon ready, at most one entry attempt per 0.25 s and not within 0.1 s of start. Re-acquisition after an ADS return uses `blend_entry=true` -> `NADSE_BeginInput` (placement fades in over 150 ms or the shared phase); every other acquisition (weapon swap, action recovery) uses `NADSE_BeginSteadyInput` (weight 1, entry position, 220 ms draw animation).
- Timers inside the DLL: shared phase validity 250 ms after publication (`phase.h:20`); own blend `blend_ms` 150 ms; entry rotation guard window 300 ms; draw animation 220 ms; depth data re-validation 500 ms, retry 500 ms; input pin retry 250 ms. Lua: stats counter conversion every 0.1 s at phase offset 0.085 s (`pose.lua:130`); motion continuity across a lease change 0.25 s; ADS handoff 0.15 s; pop guard 0.12 s (`motion_runtime.lua:44,49,253`).
- End: `pose.lua:finish` reads stats, calls `NADSE_End`, marks the handle disarmed and caches the final stats. `CLEANUP_PENDING` leaves the handle owned for a retry.

#### Camera helper (Lua side) and alignment adapter

##### camera.lua - what runs in the Lua kit update

Call site: `managed_update('camera',camera,controller)` (`runtime.lua:626`), third of the managed updates (after fov and blur, before head and movement); when a foreign camera (Photo Mode) owns the view the runtime calls `camera:stop()` instead (`runtime.lua:618-622`). `managed_update` may replace the update by a `stop` ("cleanup") or skip it while the recovery ledger waits (`runtime.lua:278-292`).

`Camera:update(controller)` (`camera.lua:426-460`), in order:
1. Clear `helper.last_failure_code`, `failure_native_error`, `failure_kind` (427).
2. `phase=='cleanup_pending'` -> retry `stop()` and return (428).
3. If `self.closing`, or the controller is not enabled, or `controller:result().mode ~= 'pose_offset'` -> `stop()` (430). (So the 30-second "Compare native ADS camera" mode `'native'` runs with the head-camera helper off.)
4. No handle: `backend:camera_config()`; if it fails or `camera_kind` is not 4 or 6 -> `phase='waiting'`, return true; else `prepare()` (431-437).
5. With a handle: `collect()` = `NADSC_ReadStats` (438). Display counters are converted at most every 0.1 s (grid offset .0333 s, `camera.lua:124-131`); `installed`, `armed`, `error` are fresh every call.
6. If `armed ~= true` (the DLL disarmed itself in the hook): remember `error`; `stop()`; if the error is a number other than 0 and 7 -> `problem(...)` with `failure_kind='temporary_read'` for code 3, `'unsafe'` otherwise (273-278, 439-446); if code 0 or 7 -> `phase='waiting'`, fault cleared, message "Head-camera player changed; waiting for a fresh normal camera" (448-449). The next update re-arms through step 4.
7. Weapon key: `backend:snapshot().weapon_unit` (number, else 0); when it differs from the last value sent, `NADSC_SetWeaponKey50(generation,key)` (452-456).
8. `phase='active'`, return true. Per-frame gate errors 14/15/16 do not stop anything (457-459).

Nothing else is passed to the DLL per frame: no positions, no time, no input. The DLL reads everything it needs from game memory inside the hook.

`Camera:prepare()` (374-424), also reachable as `prearm`: refuse when closing or cleanup pending; `backend:camera_config()` must give kind 4 or 6; if a handle exists: `collect()`, keep it when still armed and the nine identity fields (`game, exe, context, camera, avatar_owner, avatar_index, avatar, body, camera_unit`) are unchanged, otherwise `stop()`; `helper:begin(config)` -> `NADSC_Begin`; on failure adopt any owned handle, `stop()`, `problem()`. Then: `set_offsets` only if `config.right` (shipped: nil); if `config.presentation_enabled==true` (shipped: true, `backend.lua:390`): `NADSC_SetPresentation47` (failure stops), `NADSC_SetHandoff49(entry_ms,rotation_ms)` from `self.handoff` or `profile.handoff` = `(0,0)` (refusal tolerated, stored in `handoff_issue`), hold mode from `self.cut_hold` or the profile = `1+2+4+8 = 15` -> `NADSC_SetCutHold50` (refusal tolerated, `cut_hold_issue`); `set_kick(true)` only if `config.kick_enabled` (shipped false); `weapon_key=nil`; bind identity; `phase='active'`; `collect()` and require `armed==true`.

Config handed to `NADSC_Begin` on the shipped path (`backend.lua:374-396`, `profile.lua:71-73`): `game` = game.dll base, `exe` = verified helldivers2.exe base, `context`, `camera`, `avatar_owner`, `avatar_index`, `avatar`, `body`, `camera_unit = u32 at context+8`, `forward=.18`, `up=.04`, `smoothing_ms=100`, `blend_ms=120`, `max_gap_ms=250`. `camera_config` itself refuses when Photo Mode owns the view, when the request kind is not 4/6, when seated, or when the backend's own on-foot check fails.

When prepare runs: (a) from `backend.camera_before_entry` (`runtime.lua:733-737`) inside `Backend:enter_pose` (`backend.lua:674-675`), i.e. before every native first-person entry while the game camera is still kind 4; (b) from `update` step 4 after a stop.

`Camera:stop()` (359-365): no handle -> `bound=nil`, `phase='idle'`; else `phase='cleanup_pending'`, `helper:finish(handle)` = final `NADSC_ReadStats`, `NADSC_End(generation)`, mark the handle disarmed, read stats once more with generation 0 (246-256); failure -> `problem()` and the phase stays `cleanup_pending` (retried next update); success -> `handle=nil`, `bound=nil`, `phase='idle'`. Called on F11 off (`runtime.lua:881`), on a failed start (845,853), under a foreign camera (619), and by recovery cleanup.

`Camera:shutdown()` (366): `closing=true` then `stop()`; called from the runtime's shutdown (`runtime.lua:213`). After it `prepare` and `update` refuse/stop for good.

Phases: `idle` (no lease), `waiting` (wanted but no usable camera/owner yet), `active` (lease armed), `cleanup_pending` (End not yet acknowledged). `self.fault` holds the last problem text. There is no `armed`/`retired` field on the Lua object: `armed` comes from the DLL stats (`self.last.armed`); `camera.retired` is read by the runtime (`runtime.lua:435,522,789,927,936`) but never set, so always nil. `camera.is_pinned` and `camera.look_stats` do not exist either (`runtime.lua:46,433,435` test for them and get false).

##### What runs in the DLL instead

All per-frame camera work is in `setter_hook`, executed when the game calls CameraAPI+0x20 from game+0x110bff5 during its own camera update, once per camera apply: classify the request (kind 4 / 6), record native-view history on kind 6, replace the position on kind 4 (head smoothing, blend, eye hold, correction, anchor, seated view, exit hold), then call the game's setter. Timing uses the DLL's own QPC clock, not Lua's clock. Where the code states frame order: the camera world matrix read in the hook can still be last frame's (cpp:474-475); the rig pose is not yet final (cpp:669-670); the game calls CameraAPI+0x28 (rotation) immediately after this setter (cpp:677-679, bytes at cpp:1220) and then copies the rendered pose into `context+0x0C..0x3C` (`SOURCE.md:29`). The relative order of the Lua kit update and this hook within a frame is not stated in these files.

Related Lua acts done elsewhere but depending on the camera helper's modes (`runtime.lua:537,541`, `backend.lua:1340-1396`): on aim release the runtime calls `backend:keep_aim_view(s)` once (writes 0 to `controller+4` and 0 to the float at `request+0x98`), which keeps the kind-6 request so the DLL's exit/anchor paths are not entered; when a seated state begins it calls `backend:detach_seated_camera()` once (calls the function at `*( *( *(game+0x2ce14c8) + 8 ) + 0xF0 )` with `world=*(context+0x180)`, `unit=u32 at context+8`), after which the DLL's seated-head branch sees an unparented camera.

##### alignment.lua

Not part of any frame on the shipped path (never required). If mode `unlinked_sights` were started: `Backend:prepare_alignment` (`backend.lua:1069-1098`) -> `Alignment:prepare(config)` = `NADS_ReadStats`, refuse if armed, `NADS_Validate`, `NADS_ReadStats` again and require unchanged `installed` (`alignment.lua:78-91`); `Backend:_enter(...,'alignment')` -> `Alignment:begin` = `NADS_Begin` (result 2 keeps ownership for cleanup, 93-109); each controller update `Backend:alignment_status` -> `Alignment:stats` (`controller.lua:568-572`: stop when not armed or expired); stop -> `Backend:_stop_alignment` -> `Alignment:finish` = `NADS_End` then stats (113-124). Config would be `{game, exe, context, request = context+0x200+tail*0xe8, avatar_owner, avatar_index, avatar, weapon, body, viewmodel, duration_ms=10500}` (`backend.lua:1063-1067,1088`). The skipping itself would happen inside the DLL hooks during the game's own calls.

#### FOV, blur, near clip, native ADS option, kit memory

Order inside one kit update (runtime.lua:560-646): `compat:update`; `native_aim:update(controller)` (runtime.lua:568); controller/editor/motion; then, if a foreign camera owns the view, `fov:stop`, `blur:stop`, `camera:stop`, `head:stop`, `movement:stop`, `near:stop` and return; otherwise `managed_update('fov')`, `managed_update('blur')`, camera, head, movement, then `attempt(near,'update',controller)`, dof, menu. `managed_update` (runtime.lua:278-300) wraps fov and blur in the recovery policy (cleanup / wait / revalidate / retry); near is not wrapped. At mode start `fov` then `blur` are also updated once right after `controller:start(seconds,'pose_offset')`, and a failure there stops the controller with "FOV policy unavailable" (runtime.lua:845-852).

##### fov.lua
State: `phase` ∈ `idle`, `active`, `cleanup_pending`; `handle` (owned generation), `bound` (lease), `bound_request`, `bound_weapon`, `bound_player`, `fault`, `failure_native_error`, `failure_kind` (`temporary_read` when the native code is 3, else `unsafe`) (fov.lua:169-177).
Per update (`self:update(controller)`, fov.lua:233-312):
1. `cleanup_pending` → retry `stop()`.
2. If closing, controller disabled, or `result.mode ~= 'pose_offset'` → `stop()`.
3. If there is no lease or `result.phase ~= 'running'` (weapon gap, helper suspended): keep the handle ("The camera policy survives helper suspension and weapon gaps"); read stats; if the DLL reports `armed==false`: error code other than 0 or 7 → fault and stop, else quiet stop; otherwise publish the presentation phase (`present()`).
4. If the lease's `origin.player_identity` differs from the bound player → stop first.
5. Same lease, same request address, same weapon → read stats; `armed~=true` or error ≠ 0 → fault + stop; else `present()`; done.
6. Otherwise `backend:fov_config(lease)` (requires the current owned pose lease, camera kind 6, `s.record==lease.request`; backend.lua:397-419) → `{game, exe, context, camera, avatar_owner, request, avatar_index, avatar, body, weapon}`. With an existing handle: read stats (must be armed, error 0), `present()` first, then `NADSF_Bind(generation, request, weapon)` unless request and weapon are unchanged. Without a handle: `NADSF_Begin`. On refusal: fault, stop.
7. After success: `phase='active'`, remember lease/request/weapon/player, read stats again (must be armed), and on a fresh Begin call `present()`.
`present()` (fov.lua:186-195): `backend.presentation_transition()` → token `{epoch, progress, ads}` (nil when no packet) → `NADSF_SetTransition(generation, epoch, progress, ads and 1 or 0)`; token validity checked in Lua (epoch integer 1..2^53−1, progress 0..1, ads boolean; fov.lua:132-137).
Sent to the DLL every update: one `NADSF_ReadStats` (health is always fresh; the display counters are converted at most every 0.1 s on a 10 Hz grid, fov.lua:77-95) and, when a transition packet exists, one `NADSF_SetTransition`. Begin/Bind only on change.
What the DLL does on its own: computes and substitutes the lens in the game's FOV setter call each camera evaluation (hip = third-person FOV, +5 % magnification in normal aim, eased ADS handoff), and disarms itself with a typed error when the owner check fails.
Stop (`self:stop`, fov.lua:213-222): `phase='cleanup_pending'`; `Adapter:finish` reads stats, calls `NADSF_End`; on success clears everything and `phase='idle'`; on failure stays `cleanup_pending` and retries on later updates. `shutdown()` sets `closing` and stops. `preflight()` calls `NADSF_Validate` with `fov_config()` (no lease: needs a safe kind-4 camera with aim released).

##### blur.lua
State: `phase` ∈ `idle`, `waiting`, `unavailable`, `active`, `cleanup_pending`; `retiring`; `source_checked_at`, `source_retry_at`; constants `NO_SOURCE=18`, `RETIRED={[7]=true,[8]=true}`, `SOURCE_RECHECK_SECONDS=.5` (blur.lua:7-9).
Per update (blur.lua:218-285):
1. `cleanup_pending` → `retire()` if retiring else `stop()`.
2. Closing / disabled / mode not `pose_offset` → `stop()`.
3. No lease or phase not `running` → `stop()` and `phase='waiting'` (unlike fov, the blur lease does NOT survive weapon gaps).
4. `same` = same lease, request, weapon and player. If `same` and `phase=='unavailable'` and less than 0.5 s since the last check → return (no native call).
5. If `same` and a handle exists → `NADSB_ReadStats`; if not armed or error ≠ 0: codes 7/8 with `armed==false` → `retire()` (stop, `phase='waiting'`, no fault; returns true even while End is pending); other → stop + fault.
6. Otherwise always `stop()` first ("End before resolving a replacement"), then `backend:blur_config(lease)` and `NADSB_Begin`. Code 18 without an owned handle → `phase='unavailable'`, schedule recheck at now+0.5 s, return true. Codes 7/8 → `retire()`. Other failures → stop + fault.
7. Success → `phase='active'`, read stats, must be armed.
Sent each update while active: one `NADSB_ReadStats` (display counters on a 10 Hz grid offset by 0.0667 s, blur.lua:100). No per-frame data: the DLL captured the weapon's sources at Begin and zeroes the three values inside the game's own setter calls; a weapon/request change means End + Begin.
Also stopped by `backend.camera_before_entry` (runtime.lua:733-737). Stop/shutdown as fov (`NADSB_End`). `preflight()` treats code 18 as ready ("Weapon has no scoped blur source", blur.lua:215).

##### near_clip.lua
State: `phase` ∈ `idle`, `active`, `cleanup_pending`; `hip`, `aim` (last sent metres), `retry_at`, `next_collect`, `last_collect`, `bound_player`.
Per update (near_clip.lua:163-223):
1. `cleanup_pending` → retry `stop()`.
2. `hip,aim = wanted()` from the options callback (each clamped to 0..1, else 0). `pull = profile.pull_sight_distance==true`.
3. Closing / disabled / mode not `pose_offset` / (`hip==0 and aim==0 and not pull`) → clear `retry_at`, `stop()`. With the shipped profile `pull` is true, so the helper is armed whenever the mode runs.
4. No lease or phase not `running`: keep the handle; read stats; `armed==false` → stop.
5. Player changed → stop first.
6. With a handle: if hip or aim changed → `NADSN_Set(generation, hip, aim)`; then at most every 0.25 s `NADSN_ReadStats`; not armed → stop + log "Native near clip disarmed (code N)".
7. Without a handle: at most one Begin attempt per 1 s (`retry_at=now+1`); `backend:fov_config(lease)` plus `config.hip/aim` → `NADSN_Begin`; success → `phase='active'`, read stats, must be armed.
Sent each update: nothing unless the distances changed (`NADSN_Set`) or the 4 Hz poll is due. The DLL on its own rewrites the game's near-distance writes with easing and disarms itself when the camera/owner is lost.
Stop: `NADSN_End` (which also puts the game's last distance back if ours is still on the camera). Refusals are logged once per distinct message and are non-fatal ("Optional: a refused near clip logs and leaves the game's distance", runtime.lua:632).

##### native_ads_option.lua
What the option is: menu toggle `true_native_ads`, label "True First Person: native ADS", default off (menu.lua:369-370). Enabled predicate: `menu:native_ads() and not fird enabled and not fird_entry enabled` (runtime.lua:721-723).
Per update (`M.new(...).update`, native_ads_option.lua:75-89):
1. If closing, option off, controller disabled or mode not `pose_offset` → `stop()`.
2. If a foreign camera owns the view → `phase='suspended'`, nothing else.
3. `source:probe()` (native_ads_option.lua:27-36): `backend:snapshot()` must be safe with camera kind 6 or 4; three byte pins must match: game+0xa5cf03 = `488b05d64f2802488b4840f781f0000000fdffffff`, game+0xa5cff5 = `8bc8498b42500fb60448`, game+0xa5d0b3 = `486bc138420fb68408ec020000`; then resolve the preference byte (`context`, native_ads_option.lua:8-26): `settings=[avatar_owner+0x40]`, `mode = u32 [settings+0xf0]` (must be ≤ 3).
   - mode 1 or 3 (per weapon): `pool=[game+0x2ce1ea0]`; `index = map(pool+0x30, weapon)`; `descriptor=[[pool+0x48]+index*8]` with `u32 [descriptor+8]==weapon`; byte address `at = [pool+0x50] + index*2`.
   - mode 0 or 2 (per input): `player=[game+0x2ce1618]`; `u32 [player+0x88] > 0`; `selected=[player+0xe8]`; `entity = u32 [selected+8]`; `index = map(player+0xd0, entity, limit 8)`; `at = player + 0x2ec + index*0x38`.
   - the byte must be 0 or 1; a second snapshot must resolve to the same key/address/descriptor.
4. If the resolved target differs from the held lease → stop (restore) the old one first.
5. If the byte is 0: take the lease with `before=0` and write 1 (`source:write(lease,0,1)`: re-read must equal 0, no foreign camera, `Memory:copy(at, '\1')`, read back must equal 1); `writes++`. If the byte is already 1, nothing is written and no lease is taken.
6. `phase='active'`; any error → `phase='waiting'` with the message (update still returns true).
Effect (as the header comment says, native_ads_option.lua:1-2): "Lease the same aim-mode byte the native input handler reads. The game owns button handling, ADS entry/exit, sights and zoom; no input is synthesized." i.e. while the option is on, the game's own aim preference for the current weapon/input is forced to 1 (first-person sights) and the game then enters its native ADS on aim.
Stop/restore (native_ads_option.lua:62-73): requires no foreign camera; `source:read(lease)` (follows the local body via `player_snapshot`, or `pin_player_snapshot` when `backend.pin_first_person==true`; returns `nil,'retired'` if the player/module/target changed); if the byte still reads 1 → write `lease.before` (0) back, `restores++`; if retired → drop the lease without writing; failure → `phase='cleanup_pending'`. `shutdown()` sets closing then stops (runtime.lua:188, 870).

##### memory.lua
No per-update work of its own inside the mode. For the kit: `main.lua:5,55` call `backend.mem:begin_frame()` once per update, which drops any read snapshot and bumps the frame counter so `GGR_module()` validation of game.dll is repeated at most once per frame (memory.lua:262-275). Reads: `read()` → `ReadProcessMemory` into a reused 8192-byte buffer (max 8192 bytes; memory.lua:103-121), optionally memoised between `begin_snapshot()` and `end_snapshot()`. `ptr/u8/u32/hashmap/roster_has` go to the guarded-reads DLL when it loaded, else to the Lua path. `copy()` always ends the snapshot first, checks the target region, writes, and reads back. The instance used by native_ads_option is separate (`Memory.new()` with no injected io), never has `begin_frame` called on it, never calls `_module` (so `_game` stays nil and the "module image writes are forbidden" test at memory.lua:172 is inactive; the private-RW region test at memory.lua:150-158 still applies).

#### Head visibility, movement, depth of field, vignette, camera sharing, model shadows, arm pieces, grip hand

##### src/first_person/head_visibility.lua

**Call sites.** `head:update(controller)` as a managed update each kit update (rt:626); `head:stop()` when a foreign camera owns the view (rt:619) and on manual stop (rt:882); `head:sdk_handles()` read by record_presentation for arm_pieces/grip_hand (rt:551, 557); `head:result().phase=='active'` feeds DoF's unused `fallback_ready` (rt:710-712).

**What is owned.** Only two pass bits of the per-mesh visibility byte at `mesh+0xDC` (hv:183): bit 1 = pass `'default'` (camera), bit 2 = pass `'shadow_caster'` (hv:7-9). Every write is `Unit.set_mesh_visibility(handle, mesh_index_1_based, visible, pass_name)` (hv:184-188) followed by a native readback of the byte (hv:750). Three pass plans (hv:7-10):
- `PASSES`: default -> hidden, shadow_caster -> visible. Used for head / torso / shoulder customization units, the backpack and, without model shadows, the two rig shoulder pads. For the body unit (`body_only`) only the shadow_caster half applies (hv:706, 718): the body's camera pass is never touched, its shadow pass is forced on.
- `SHADOW_ONLY`: shadow_caster -> hidden. Used for `model_shadow` rows (arm pieces on the rig, weapon tree) and for rig meshes other than the two pads when `shadow_rig` (hv:354).
- `HIDDEN_NO_SHADOW`: default -> hidden and shadow_caster -> hidden. Used for the two rig shoulder pads when the rig spec is `shadow_rig` (hv:353, 10).

**update(controller) step by step (hv:663-764).**
1. If phase is `cleanup_pending` -> `stop()` (hv:664).
2. If closing, controller not enabled, controller mode ~= `'pose_offset'`, or `backend.foreign_camera()` is not exactly false -> `stop()` (hv:666, 278-281).
3. `view_snapshot()` = `backend:player_snapshot()` (the `pin_player_snapshot` branch at hv:58-61 needs `deps.pinned_ready` to be a function, which it is, so it IS attempted; it is `_snapshot(true,nil,true)`, be:338-342; if it returns a table that table is used). NOTE: unlike runtime/movement this is not gated on `backend.pin_first_person`; see SURPRISES.
4. `available(s)` (hv:284-291): `player_identity` non-empty string and (pinned_ready [dead] or (camera_kind 4 or 6, not seated, and (`s.safe` or `s.weapon_transition` or `s.head_camera_transition`)) or (camera_kind 4 and seated and `seated_head()`)). Else `stop()`.
5. `first_person(s)` (hv:292-303): kind 6 -> true. Kind 4 -> `deps.fallback_ready(s)` must be true (camera module phase `active`, `armed`, `error==0`, rt:687-690) AND `backend:action_state(s)` must name the same owner/body AND `backend:on_foot(s,state)==true` (be:351-372). Else `stop()`.
6. If the remembered `identity` differs from this player -> `stop()` first (hv:669).
7. Steady fast path (hv:671-683): if a `steady` record exists, `observe_steady(s)` re-proves everything read-only; success -> `steady_observations+1`, phase `active`, return with zero SDK calls except `Application.main_world()`. Any failure drops `steady` and forces a full observation with `refresh=true`.
8. Full observation `resolve(s,refresh)` (hv:386-542), failure -> `stop()` then phase `unavailable` with the error text (hv:686-689).
9. `update_serial+1`; `new_epoch(s)` (hv:543-554): a new epoch starts when any of player_identity, camera_kind, `s.record`, `s.head`, `s.tail`, `s.request_weapon`, `s.controller_active==true`, `s.aiming==true` changes; a new epoch clears the `ignored` table (hv:692-693).
10. Apply (hv:694-756): rows owned but no longer wanted are restored; for each wanted row: (a) if the mask reads 255 after we last saw the default bit off, ownership is dropped and, unless it is a native reset inside the epoch window, the row is put on the ignore list (hv:701-704); (b) a pass that no longer has our value while we own it (or last saw it as wanted) releases the bit and, unless native reset or a shadow-suppressed shadow bit, counts as `foreign_masks` and is ignored for this epoch (hv:705-715); (c) each still-wrong, un-owned, un-ignored pass is written after re-proving body generation, parent link (`object+0x1D0`), not-foreign, backpack identity, rig resource and pad identity, model-shadow validity (`model_shadows:valid`), customization slot still holding the ref, mesh index (hv:717-752). Ownership (`row.bits`, `owned[key]`) is recorded BEFORE the SDK call (hv:747).
11. Failure during apply -> `stop()` then `unavailable` (hv:757-760). Success -> phase `active`, try `retain_steady` (hv:761-762).

**Phases** (`self.phase`): `idle` (clean), `active`, `unavailable` (resolve/apply failed, everything restored), `cleanup_pending` (a restore failed; `stop()` retried at the top of every update, hv:254, 664).

**Timers and thresholds.**
- `TORSO_SETTLE = .6` s (hv:11): show-torso option only applies after the rig-less state has been steady this long (hv:403-409).
- Native-reset window: `.25` s after an epoch start (hv:557); only masks 0, 1, 254, 255 qualify (hv:556).
- Handle enumeration retry: at most once per `.5` s for an unchanged failed request (hv:482).
- Optional rig spec retry: `clock()+.5` after a failed rig collect; within the last 0.5 s before `retry` the old layout is returned unchanged (hv:320-323, 498).
- Limits: mesh count per unit <= 128 (hv:149); `World.units` list <= 131072 (hv:374); registry capacity <= 0x400000 (hv:125); avatar_index 0..7 (hv:392); read size <= 256 bytes, address 65536..2^47 (hv:79).
- Clock: `GetTickCount64()/1000` (hv:46-52).

**How the player unit becomes an SDK handle.** (1) Unit registry: `ptr(exe+0x1ab76f8)`, fields at `+0x88` (32 bytes): objects pointer at +0, capacity u32 at +16, generations pointer at +24 (hv:122-127). (2) A 32-bit unit ref splits into index = `ref % 0x400000` and generation = `floor(ref/0x400000) % 256`; the generation byte at `generations+index` must match, the object is `ptr(objects+index*8)`, re-read after the header; object header: vtable at +0, self ref u32 at +8 (must equal ref), world pointer at +24 (hv:128-146). The vtable is qualified once: `ptr(vt+0xC8+48)==exe+0x28d3e0` and `ptr(vt+0xC8)==exe+0x293600` (hv:140-143). (3) SDK world = `Application.main_world()`; its userdata address dereferenced once must equal the unit's world pointer (hv:51, 391). (4) Handles: `World.units(world)` is enumerated and each handle's ref is `floor(userdata_address/4)` (hv:49, 373-385); only refs in the wanted set are kept; a duplicate is fatal except for the rig. Enumeration happens only when a required handle is missing (hv:470-489), there being no unit-created event (comment hv:477-478). The cache is dropped when the native or SDK world changes (hv:467-469).

**Which refs are collected (resolve, hv:392-466).** Appearance block: `base = s.avatar_owner + 0x546ab0 + s.avatar_index*0x1B8`; 120 bytes at `base+0xDC` = 3 layers x 40 bytes, each layer 10 slots of u32 unit refs (hv:393-394, 431-443). Shipped (not full_body):
- slot 0 -> part `head`: always hidden (hv:5, 433).
- slot 2 -> part `torso`: hidden unless `show_torso` is in effect (hv:433).
- slots 8 and 9 -> part `shoulder`: hidden (hv:5-6).
- slots 3,4,5,6,7 -> "kept" limbs (hv:415): never hidden; a wanted mesh shared with a kept renderer is an assertion (hv:448, 530). A kept limb's parent (`object+0x1D0`) must be the body or the rig (hv:512-516).
- slot 1 is never read by this file.
- backpack (`backend:backpack(s)`, be:301-315: inventory at `game+0x2ce18e8`, slot row +12) -> part `backpack`: camera pass hidden, shadow kept (hv:453-456).
- body unit `s.body` -> part `body`, `body_only`: shadow pass forced on only (hv:457).
- rig `s.viewmodel` -> part `shoulder`, `rig=true` when the rig-capable SDK functions exist (hv:416-418, 458-461): if the rig's resource id (8 bytes at `object+0x358`) equals `RIG_RESOURCE` its mesh count must be 5 and the two named meshes `g_l_shoulderpad` (name hash 0x44E19D12, node 57) and `g_r_shoulderpad` (0xCB0E1712, node 58), each with exactly 1 material and material `m_armor_standard`, are selected (hv:13, 189-204, 338-345); a rig with another resource contributes nothing (hv:315). Rig collection is optional: failure only sets a retry (hv:493-499).
- model-shadow rows from `deps.model_shadows:probe(s, appearance)` (hv:411-414, 462-465): replace the rig spec by `{part='shoulder',rig=true,shadow_rig=true}` and add one `model_shadow` row set per arm piece / weapon unit.

**Views.**
- Kind 6 (native rig): head, torso, shoulders, backpack hidden from the camera with shadows kept; body shadow on; rig pads hidden with no shadow; all other rig meshes, the arm pieces on the rig and the whole weapon tree lose their shadow pass. Torso is ALWAYS hidden here (`steady` at hv:403 requires kind 4; observe_steady asserts `show_torso==false`, hv:611).
- Kind 4 head-camera gap/fallback: needs fallback_ready + on_foot. `model_shadows:probe` fails (it requires kind 6 and `safe`, ms:21), so `model_shadow_error` is set (hv:413) and no shadow-only rows exist; the previously owned shadow-only rows are restored because they are no longer wanted (hv:695-698). Head, shoulders, backpack hidden; torso hidden unless the menu option is on AND controller bytes 1,2,3,4 and 6 are all 0 and that has held for 0.6 s (hv:402-409). The rig, if still present with handles, keeps only the two pads under the plain `PASSES` plan (default hidden, shadow visible).
- Seated (valve): kind 4 + `s.seated==true` + `seated_head()` passes `available` (hv:290), but `first_person` still requires fallback_ready and `on_foot` (hv:288-289 comment, 295-302).
- Anything else (vehicle, death, unsafe without transition flags, foreign camera): `stop()` restores.

**sdk_handles()** (hv:264): returns the live `handles` table `{[unit_ref] = SDK Unit handle}` for every ref in the last resolve: body, rig, head/torso/shoulder customization units, backpack, and in kind 6 the arm pieces on the rig and every unit of the weapon tree. Comment: "Consumers check them alive". The table object is replaced wholesale when an enumeration runs or the world changes (hv:468, 487); arm_pieces and grip_hand use table identity as a change trigger. It is NOT cleared by `stop()`.

**Steady path (hv:575-662).** Kept only in kind 6, `safe`, 24-byte `s.flags`, no model-shadow error, all rows seen at least once. `observe_steady` re-reads: same epoch fields, same flags, 20 SDK bindings identical (hv:569-574, 613-614), body/world, appearance bytes, backpack identity, `model_shadows:unchanged`, every renderer's world, resource (`+0x358`), parent (`+0x1D0`), mesh array bytes, handle refs, every row's mask byte == `row.last`; then a second snapshot and registry comparison.

**Restore on stop (hv:205-256).** For each owned row: locate the mesh again (unit generation, world, rig resource, pad identity, mesh index); if gone -> `retired+1`, dropped. If the default bit is now on although we wrote it off and (mask == 255, or the exiting snapshot is kind 4 for the same player/body) -> the native publication supersedes, drop without writing (hv:209-214). Otherwise for each owned pass: if the bit no longer has the value we wrote -> `foreign_masks+1`, ignored; else write the `before` value and read back. If anything remains owned -> phase `cleanup_pending`, return false. `shutdown()` sets `closing` then stops.

**Guards before SDK use.** Build fingerprint (hv:97-121) once per game base: `exe+0x3f4ca0` 8 bytes `488bd1e9a8f8ffff`; `exe+0x51fe80` 14 bytes `40574883ec30440fb689dc000000`; `exe+0x3f4c23` 24 bytes; `exe+0x51feaa` 26 bytes; `u32(exe+0x2875ac0)==0xF2760503`; `u32(exe+0x2875ac8)==0x3100C3D2`; `exe+0xa06d0` 22 bytes. `Unit.set_mesh_visibility` function identity is pinned for the lifetime of any owned row (hv:101, 185-187). There is no `num_scene_graph_items` guard here (this file never reads transforms); liveness is the registry generation byte.

**Dead on the shipped path.** `pinned_ready` true branches (hv:64-68, 286, 293); `full_body` / `hide_torso` (hv:395-396, 415 second list, 433 last clause, 447, 450-452, 524, 526-529, 609-610); `hidden_body` counter only counts the body row's shadow bit.

**Kit Lua vs game.** Writes are pass-visibility commands published by the SDK; the game itself republishes masks on camera/weapon/aim changes (the "native reset" handling, hv:555-563) and may publish a full 255 mask on exit (hv:209-211). No frame-order statement beyond that.

##### src/first_person/model_shadow_source.lua

No update of its own; three methods called by head_visibility.
- `probe(expected, appearance)` (ms:18-83): takes `backend:snapshot()` (full snapshot), requires `safe`, `camera_kind==6`, same player/body/viewmodel, `foreign_camera()==false`. Resolves body and rig through `backend:_alignment_context` (registry lookup, be:783-802). Rig must have `u32(object+0x70)==63` nodes and resource bytes `RIG` at `+0x358`, and share the body's world (`+0x18`) (ms:31-32). Appearance re-read at `avatar_owner+0x546ab0+avatar_index*0x1b8+0xdc` must equal the caller's (ms:33-34). Weapon unit `s.weapon_unit` must have the rig as parent (`+0x1d0`) (ms:35). Rows: for layers 0..2, slots 6..9, any unit whose parent is the rig (ms:44-46); then the weapon tree walked from `s.weapon_unit` through first child at `object+0x1d8` and next sibling at `child+0x1e0`, depth <= 8, total <= 32 units (ms:47-60). Each row is `{part='model', model_shadow=true, model_parent=..., model_owner={...}}`; the rig row is `{part='shoulder', rig=true, shadow_rig=true}` (ms:61). A closing snapshot and re-read of every parent/world/link field ends the probe (ms:62-69). A fingerprint for `unchanged` is stored only when `s.weapon` is a valid ref and the 20-byte weapon descriptor is available (ms:73-78). Returns `rows, signature` or `nil, error`.
- `has_observation()` / `unchanged(s, appearance, inspect)` (ms:86-112): read-only comparison of owner, body, rig, world, record, `request_weapon`, appearance, weapon descriptor (`_descriptor(s.world, weapon):sub(1,20)` and 20 bytes at the component address), each inspected unit's object/world/parent/links, rig node count 63 and resource.
- `valid(expected, owner)` (ms:113-126): fresh snapshot (safe, kind 6, same identity/rig/weapon), rig and gun and target re-resolved, parents and world equal, not foreign. Called before every shadow-pass write (hv:736-739).

##### src/first_person/first_person_movement.lua

**Call sites.** `movement:update(controller)` each kit update (rt:630); `movement:stop()` instead while the placement editor is active (rt:629), when a foreign camera owns the view (rt:619) and on manual stop (rt:883).

**What it changes.** One byte: the local avatar's "focused locomotion" flag at `rows + index*0xA0 + 0x94` in the locomotion pool `ptr(game+0x2ce1eb8)` (mv:47-69). It is written to 1 while S is held and restored to its previous value (always 0, see step 9) afterwards. Header comment (mv:2-3): "Native focused locomotion: S-only for manual FPV ... No input, aim or bone writes." It sets no animation event or variable; it changes no gait easing or turn-in-place; the effect (view-aligned backpedal instead of turning to face the camera) is the game's own focused-locomotion behaviour.

**update(controller, skip=nil) step by step (mv:172-244).**
1. closing or skip -> `stop()`. Owned and `cleanup_pending` -> `stop()`.
2. Controller not enabled or mode ~= `'pose_offset'` -> `stop()` (mv:177).
3. `backend.foreign_camera()` must be boolean; true -> `stop()` (mv:179-183).
4. `backward()` (mv:158-171): `Keyboard.button_index('s')` (or `button_id`) resolved once per Keyboard table, index 0..511; `input_focus()` = `Window.mouse_focus()==true` and `Window.show_cursor()==false` (mv:150-157); `Keyboard.button(key)` must be 0 or 1. Not held -> `draining=false`, `stop()` (mv:188).
5. `draining` (set when a foreign change was seen) -> phase `foreign`, message "Focused locomotion changed externally; release S before retrying" (mv:189-192).
6. `backend:player_snapshot()` (pin branch dead: `pin_first_person` is false, mv:113). Not `safe`, seated, or kind not 4/6 -> `stop()` (mv:194).
7. `backend:on_foot(s, backend:action_state(s))` must be true (mv:201-204). In kind 4 `deps.fallback_ready(s)` must be true (mv:205-207).
8. `location(s)` (mv:43-70): fingerprints (6 anchors) validated once per game base; pool = `ptr(base+0x2ce1eb8)`; active = `u32(pool+0xC)` in 1..16384; hash header 20 bytes at `pool+0x20`: capacity u32 at +8 (power of two <= 2^16), empty id at +12, multiplier at +16; buckets = `ptr(pool+0x20)`; probe at most `min(cap,128)` buckets `((avatar*mult mod 2^32)+n) % cap`, 8-byte rows {id u32, index u32}; descriptor = `ptr(ptr(pool+0x38)+index*8)` must equal `ptr(avatar_owner+0x110+avatar_index*8)` and its 20 bytes must equal `s.avatar_descriptor`; rows = `ptr(pool+0x50)`.
9. If owned and the location differs -> `stop()`, do not acquire in the same update (mv:209-213). Read the byte: must be 0 or 1 (mv:214-215). Owned and it is no longer 1 -> `foreign_changes+1`, `draining=true`, drop ownership (mv:216-222). Not owned and already 1 -> phase `native` ("Focused locomotion is already native"), nothing written (mv:223). Otherwise re-snapshot, re-locate, re-read == 0, record `before`, set `owned`, write 1 (`WriteProcessMemory` after `VirtualQuery` says committed 0x1000, private 0x20000, protection 4 or 8; mv:82-107), readback, third snapshot check, `writes+1`, phase `active` (mv:224-233).
10. Any error -> `stop()` then phase `unavailable` (mv:235-240).

**stop() (mv:125-148).** Not owned -> `idle`. Owned: fresh snapshot; a different player or a different location retires the lease without writing; byte no longer 1 -> `foreign_changes+1`, `draining=true`, no write; else write `before` back, `restores+1`. Failure -> `cleanup_pending` (retried on the next update).

**Phases:** `idle`, `active`, `native`, `foreign`, `unavailable`, `cleanup_pending`. No timers. `state.scope` is `'manual'` on this path.

**Dead:** `update_fird`, every `focused` branch (`focus_owned`, mv:186-187, 195-200), `deps.writable` / `deps.write` (test seams).

**Kit Lua vs game.** The kit writes the flag during its update; the game's locomotion/animation reads it afterwards. No frame-order comment in the file.

##### src/first_person/dof.lua

**Structure.** `Dof.new` builds a `Setting` for `dof_enabled` and, because `deps.vignette_factory` is given, a vignette.lua object for the vignette (dof.lua:231-237). `update/stop/shutdown` call both; `is_suppressed()` reports the DoF setting only (dof.lua:240).

**Setting.update(controller, camera, compat) (dof.lua:170-217).**
1. `foreign()` = `compat:foreign()` (camera_compat) (dof.lua:42-49).
2. `wanted = not closing and not held and deps.active_view(controller,camera)==true` (dof.lua:141, 174). `active_view` = `first_person_view(fallback_ready)` (rt:48-79): false if foreign; snapshot kind must be 4 or 6; if not `safe`, true only in kind 4 while the controller is enabled in `pose_offset`, the player is the one the session entered with (`fp_dof_player`) and the camera fallback is ready (so a weapon swap, vault or dive does not flash the effects, rt:54-64); if safe: controller enabled, `pose_offset`, same player, and (kind 6 or fallback ready).
3. Not wanted: reset `session/preserved/yielded/next_attempt`; if a record exists set `cancel` and `advance(time, not held and first)` (immediate restore attempt on the first not-wanted update unless foreign); else phase `foreign` or `idle` (dof.lua:175-185).
4. Wanted with a record: `advance(time,false)` (dof.lua:186).
5. Wanted without a record: need `Application.render_setting` and `set_render_setting` (else phase `unavailable`, retried every `INTERVAL`); at most one acquisition attempt per `INTERVAL`; `check(c)` proves the build once (dof.lua:50-66); read the value: false -> `preserved=true`, phase `already_disabled`, nothing owned (for this mandatory setting it keeps polling, using the cached observer, and takes over if the value becomes true, dof.lua:197-202); true -> re-check foreign and view, create the record (`before=true`), `command(c,false,time)` (dof.lua:203-208).

**command(r, value, time) (dof.lua:82-99).** Stage `applying` (value false) or `restoring` (value true); `next_poll = time+INTERVAL`; calls `Application.set_render_setting(key, 'false' or 'true')` (string arguments); a throw or a `false` return sets `cancel` and an issue; if foreign now -> `cancel`; reads back with the SDK getter: equal -> `active` (false) or the record is retired (true).

**advance(time, force) (dof.lua:100-137).** Foreign -> `cancel`, phase `cleanup_pending`, NO write (the setting stays as it is while Photo Mode owns the view). Not forced and before `next_poll` -> report only. Else read (cached observer `backend:renderer_dof_value` only when stage `active` and not cancelled; otherwise the SDK getter, which fences the renderer queue), `next_poll=time+INTERVAL`. Stage `restoring` and value true -> retire. Stage `applying` and value false -> `active`. Stage `active` and value not false -> `foreign_changes+1`; mandatory and not cancelled -> `reassertions+1` and command false again (comment: "commonly a native optic/zoom reset"). `cancel` and stage `active` -> command true. If `time - r.sent > 2` and stage is not `active` -> reason "Renderer DoF command acknowledgement pending".

**Stages/phases:** `idle`, `unavailable`, `already_disabled`, `applying`, `active`, `restoring`, `cleanup_pending`, `foreign`.

**Timers.** `INTERVAL = .5` s (dof.lua:5): acquisition retry, poll spacing. `2` s acknowledgement warning (dof.lua:134). Clock going backwards resets `next_attempt` and the record's poll times (dof.lua:26-33).

**Guards.** `exe+0x3d43d0` 8 bytes `48 8B D1 E9 08 FD FF FF` (getter thunk) and `exe+0x3d3ff0` 8 bytes `48 8B D1 E9 88 FA FF FF` (setter thunk) (dof.lua:62-63). Cached observer (be:731-781): function addresses must be those thunks, plus `exe+0x3d419a` 7 bytes and `exe+0x3d420e` 9 bytes; table pointer at `exe+0x1ab78c0`, header {count u32, rows ptr at +8}, rows of 24 bytes sorted by key at +16, type u32 at +0 must be 4, Boolean byte at +8; key `dof_enabled` = 0x06f1d0d3.

**Restore.** `stop()` (dof.lua:160-168): cancel + forced advance -> `set_render_setting('dof_enabled','true')` (always the literal true, which equals `before` since a record only exists when the value was true); returns false until the readback shows true. `shutdown()` sets `closing`. Runtime treats DoF as not clean until `result().owned==false` (rt:638).

**Dead:** `mandatory` is true for both keys but the `vignette_enabled` Setting is never built on the shipped path; `deps.verify`; the controller-based fallback in `actual_view` (dof.lua:142-148).

**Kit Lua vs game.** Comment dof.lua:2-3: renderer Boolean settings are "process-global and asynchronous"; the command is acknowledged by a later readback, not in the same call.

##### src/first_person/vignette.lua

**update(controller, camera, compat) (vignette.lua:76-118).**
1. `active()` (vignette.lua:70-75): false if closing or foreign (`compat:foreign()`); else `deps.active_view()==true` (the same `first_person_view(fallback_ready)` as DoF). Not active or error -> `stop()`.
2. `api()` binds `Application.main_world`, `World.entities`, `EntityManager.render_data_component`, `EntityManager.alive`, `RenderDataComponent.instances/get_property/set_property` (vignette.lua:22-29). World changed -> forget rows/context/scan. Context = `{api, world, manager = EntityManager.render_data_component(world)}`.
3. Discovery: when `now >= next_scan` and no scan is running, `World.entities(world)` (<= 32768) starts a scan and `next_scan = now+1` (vignette.lua:85-88). Each update processes up to 96 entities (vignette.lua:90): alive entities' `RenderDataComponent.instances(manager, entity)` (<= 64 ids); an instance whose id modulo 2^32 equals `CLASS = 0x35b0f6c2` and whose property `{'shading_environment_mapping'}` equals `'core/stingray_renderer/shading_environment_components/vignette'` is added to `rows` (<= 32 rows) (vignette.lua:92-97).
4. Poll every `.1` s (vignette.lua:101-113): invalid rows removed; a row whose `{'vignette_enabled'}` property reads true is acquired (`before=true`, `owned=true`; if already owned `reassertions+1`), foreign re-checked, then `set_property(manager, entity, id, {'vignette_enabled'}, false)` with readback, `writes+1`.
5. Phase: `discovering` while scanning, else `active` if rows exist, else `unavailable` ("No vignette components found") (vignette.lua:114).

**stop() (vignette.lua:53-68).** No context -> `idle`. World replaced -> forget rows (handles from a destroyed world are never reused). Foreign camera with owned rows -> error -> `cleanup_pending` (nothing written while another camera owns the view). Otherwise each owned row that is still valid and still false is written back to `before` (true), `restores+1`. Then rows/context cleared, `idle`.

**Timers:** rescan every `1` s; `96` entities per update; poll every `.1` s.

**Flash guard:** the `active_view` gate keeps the vignette off through the unsafe kind-4 stretch (rt:54-64 comment: restoring "made them flash between weapons").

**Dead:** the snapshot branch vignette.lua:73-74 (active_view always supplied).

##### src/first_person/camera_compat.lua

**Observed globals (provider, cc:32-57).**
- `DBF_PHOTO_CAMERA_OWNERSHIP` (modern Photo Mode): must be a table with boolean `held`, `available==true`, `disabled~=true`; kind `'local'`: observed only, nothing installed. Malformed -> "Photo camera ownership interface unavailable" (treated as foreign).
- `DBF_PHOTO_ACTIONCAM_COMPAT` (legacy): table with function `set_input_blocked`, boolean `held`, not disabled, `available~=false`; kind `'legacy'`.
- Neither, but `package.loaded['mods/actioncam/actioncam']` is set -> "ActionCam camera-sharing bridge unavailable" (foreign). Neither and no ActionCam -> absent.

**foreign() (cc:76-90).** true when: a conflict is latched; the provider is malformed; an installed record no longer verifies; `api.held` is true ("Photo Mode owns the camera"); or the legacy setter's `input_reader` upvalue cannot be inspected. Runtime wraps it: `foreign_camera = not handoff_ready or compat:foreign()` and publishes it as `backend.foreign_camera` (rt:718-719).

**prepare() (cc:92-133).** Called before the mode starts (rt:734). Closing/conflict -> false. Provider error or failed verify -> `reject` (latches `conflict`, phase `conflict`). `held` -> phase `foreign`, false. No api -> phase `absent`, true. Kind local -> phase `observing`, true. Legacy: reads the current `input_reader` upvalue of `api.set_input_blocked` through `debug.getupvalue` (first 32 upvalues, cc:17-31), builds a shim that first calls the previous reader (error or non-false = blocked) and otherwise returns `blocking and controller enabled and provider.held ~= true`, stores the record BEFORE calling `api.set_input_blocked(shim)`, verifies the upvalue is now the shim; phase `active`.

**update() (cc:152-166).** Called first in every kit update. Closing or controller not enabled -> `stop()`. Conflict -> false. Provider error / verify failure -> reject. `held` -> `record.blocking=false`, phase `foreign`, returns true. Else `prepare()`.

**stop() (cc:134-150).** No record -> clears `conflict`, `idle`. With a record: `blocking=false`; if the upvalue is still our shim call `setter(previous)` and require the upvalue to no longer be the shim (else `cleanup_pending`); a foreign replacement is left in place.

**Phases:** `idle`, `absent`, `observing`, `active`, `foreign`, `conflict`, `cleanup_pending`. No timers. No engine calls.

##### src/first_person/arm_pieces.lua

**Activation.** `arm_pieces:update(s, active, handles)` from record_presentation (rt:548-552); `active` = controller enabled, phase `running`, `s.safe`, `camera_kind==6`, not foreign, menu "Fird Person Arms" on, `profile.camera.body_sleeves==true`. It stays on in the placement editor (rt:547 comment). `stop()` on manual stop (rt:887).

**update step by step (ap:205-272), all inside one pcall.**
1. `bind()` asserts the SDK tables/functions exist (ap:41-48).
2. Not active, or no handle for `s.body` or `s.viewmodel` -> `release()`, `quick=nil`, reason `waiting` (ap:208-211). `release()` (ap:82-94): if hung, every placed bone of every still-usable piece gets `Unit.set_local_pose(piece, bone.index+1, Matrix4x4.identity())` and `placed` is cleared; links are NOT handed back here (comment ap:85-86: the game links these bones itself when it moves the pieces but does not reset the local pose).
3. `quick = now+.6` once per activation (ap:215).
4. Identity change (body ref, rig ref, the handles table object, body handle, rig handle) -> `release()` + `forget()` (ap:216-218). Any piece no longer usable -> `hung=false`, `forget()` (ap:219-221).
5. Lookup cadence (ap:224-233): a scan is considered when there is no identity, no pieces, or `confirm_at` is due. It runs if (no pieces and `now < quick`, i.e. every update for the first 0.6 s) or `next_scan` is unset/due or the clock went back by more than `RESCAN`. After a scan `next_scan = now+RESCAN` (0.25 s). First scan that finds pieces sets `confirm_at = now+.5`; the confirming scan clears it. A changed piece count sets `hung=false`.
6. No pieces -> reason `no pieces on the rig`. Body or rig not usable (`Unit.alive` and `num_scene_graph_items == count read from memory`) -> reason `body or rig unavailable` (ap:234-237).
7. Re-link check (ap:238-246): `stale = not hung`; for each piece with a stored placement, expected chest position = translation of `placed * world_pose(rig, rig_shoulder+1)`, actual = translation of `world_pose(piece, chest+1)`; more than `TOLERANCE = .02` m apart -> stale. Stale -> `hang()`.
8. `hang()` (ap:162-171): for every piece bone `World.link_unit(main_world, piece.handle, bone.index+1, rig, side.rig_shoulder+1)`; `placed=nil`; `hung=true`; `links+1`.
9. `torso_turn()` (ap:184-203); nil -> reason `torso frame unavailable`.
10. Per side: body shoulder world position, rig shoulder world position, inverse of the rig shoulder world pose (ap:249-255).
11. Per piece bone (ap:256-267): `at = rig_shoulder_pos + rotate(turn, body_bone_pos - body_shoulder_pos)`; `facing = Quaternion.multiply(turn, body_bone_world_rotation)`; `pose = Matrix4x4.multiply(from_quaternion_position(facing, at), inverse(rig_shoulder_world_pose))`; `Unit.set_local_pose(piece, bone.index+1, pose)`; the chest bone's pose is boxed as `placed`.
12. `frames+1`, reason `active`. Any error -> reason = error text, `errors+1`.

**scan (ap:113-161).** Rig and body objects through `backend:_alignment_context`. Bone name table: count = `u32(object+0x70)` (1..255), names = u32 array at `ptr(object+0xA0)` -> {hash -> zero-based index} (ap:60-70). For both sides the shoulder bone index is looked up on rig and body by hash (left `0x03752C98`, right `0x1620B2CE`). Candidates: every handle in `handles` other than body/rig whose parent pointer (`object+0x1D0`, low 6 bytes) is the rig object; the piece's side is the (last) side whose collar hash it carries (left `0xC4787B4E`, right `0xB2BCD839`) provided both shoulder indices exist; the handle must be usable. Bones, in this order: CHEST `0x37EF0820`, the side's collar, SPINE_A `0x58D886FB`, SPINE_B `0x5A4BBAA2`, GROUP `0x315CD161`, each only if present on both the piece and the body; `source` = the body's bone index of the same name; `home` = the rig bone the game had linked it to (link table at `ptr(object+0x98)`, u32 per bone: low 16 bits == 2 means linked, high 16 bits = parent bone index, ap:72-80), remembered across re-scans in `identity.homes` because after hanging the table shows this module's link (ap:138-141). A piece is accepted only if its first bone is the chest (ap:145). Identity also stores the body's CHEST and SPINE_B indices and the node counts.

**torso_turn math (ap:175-203).** `body_line` = body right shoulder - left shoulder; `rig_line` likewise on the rig; `spine` = body chest - body SPINE_B. Lengths under `.05`, `.05`, `.02` m -> nil. `turn = arc(body_line, rig_line, half-turn axis (0,0,1))` (shortest rotation; `sine < 1e-6` -> none, or pi about the given axis when opposite). `body_up` = spine minus its component along body_line, `rig_up` = world up (0,0,1) minus its component along rig_line; if either is shorter than `1e-4` return `turn`. `body_up` is rotated by `turn`; `roll = arc(body_up, rig_up, rig_line)`; result `Quaternion.multiply(roll, turn)` (multiply(second, first), "checked live").

**stop() (ap:273-277).** If hung: `restore()` = `release()` then, for every usable piece while the rig is usable, `World.link_unit(world, piece, bone.index+1, rig, bone.home+1)` for bones with a known home (ap:96-108); then `forget()`. Always returns true.

**States:** `hung` false/true; reasons `idle`, `waiting`, `no pieces on the rig`, `body or rig unavailable`, `torso frame unavailable`, `active`, or an error string.

**Kit Lua vs game (header ap:4-24).** While the rig is up the game moves the body's own arm pieces onto the rig; arm, hand and finger links stay the game's. A linked bone keeps the local pose it is given as an offset from its link target, so the placement is rewritten every update because "the rig's shoulders move every frame". Measured: rig shoulders 29 cm apart, body's 41 cm.

##### src/first_person/grip_hand.lua

**Activation.** `grip_hand:update(s, active, handles)` (rt:554-558); `active` adds `control_mask==0` (controller bytes 1-4 all zero, rt:445) and `profile.camera.grip_hand==true` to the arm_pieces conditions. `stop()` on manual stop (rt:888).

**Fixed rig bones (zero-based, SDK index +1; gh:101-102).** Left: shoulder 10, elbow 12, hand 13. Right: shoulder 30, elbow 32, hand 34. Rig node count `RIG_NODES = 63`. Weapon bone on the rig: found by name `ATTACH = 0x4FDD3337` (header: rig bone 8; body bone 84). Body bones by name: left hand `HAND_NAME = 0x5C0C68D3`, right hand `RIGHT_NAME = 0x248004F5`, weapon bone `ATTACH`. Grip bone on weapon units: `GRIP = 0x67C94E4B`.

**update step by step (gh:616-664), one pcall.**
1. Inactive (gh:618-624): if anything was written `release()` (identity local pose on every offset bone of both sides); `gun_release()`; clear the motion trackers (`rel_at, rel_time, ref_at, aside, aside_time, rig_at, rig_time, rig_until`); un-hide the rig mesh; `was_active=false`, `last=nil`, `soon={}`; reasons `waiting`. NOT cleared here: `current`, `on`, `pin`, `shapes`, `known`, `candidates`, `pieces` (what is known about a weapon outlives the arm view, header gh:41-45).
2. `rig = handles[s.viewmodel]` must be usable with exactly 63 scene-graph items, else release everything, reason `rig unavailable` (gh:626-627).
3. `lost` = any candidate or piece no longer usable. `missing` = no candidates or left hand bone not among the pieces (`complete`) or `right_partial` (right armour found without the right hand) (gh:629-632).
4. Aim edge: `aiming = s.aiming==true or s.shoulder_aim==true`; a change while active = `turned` (gh:633-635).
5. View begin: `began = now`; on begin or `turned`: `confirm = now+CONFIRM` (0.5 s) (gh:636-637).
6. Weapon edge: `id = tostring(s.weapon or s.request_weapon) .. (' moving' if s.weapon_transition==true)`; changed while active = `swapped`: `soon = {now+.1, now+.25, now+.5}`, `current,on,pin = nil,false,0`, `gun_release()`, `swaps+1` (gh:638-644). Each due `soon` entry forces one more lookup (gh:645).
7. `again` = confirm due, or `turned`, or `swapped`, or `periodic` due (`PERIOD = 1` s) (gh:646-648).
8. Lookup when: view just began, the handles table object changed, `lost`, `again`, or `missing` and (within `QUICK = .6` s of `began` -> every update; else `next_scan` due, `RESCAN = .25` s) (gh:649-653). Before the scan `release()` clears all offsets (they are rewritten in the same update; header gh:73-74). After: `next_scan = now+RESCAN`, `periodic = now+PERIOD`.
9. `calm = 1` (constant). `self.reason = left_arm(rig, now, 1)`; `self.right = right_arm(rig, now, 1, s.aiming==true)`; `left_aside(rig, now)`; rig screen edge: if `drawn.left ~= (hidden~=nil)` or the hidden rig is another handle -> `rig_screen(rig, drawn.left)`; `frames+1` when either arm is active or the left hand is moved aside (gh:655-660).

**scan (gh:230-307).** Rig object via `_alignment_context`; `rig_segments` (gh:212-229) walks each of the 63 rig bones up its own parents (link word low16 == 1, high16 = parent) until it meets one of the six arm bones and labels it part `shoulder`/`elbow`/`hand` and side. Rig name table (u32 at `ptr(rig+0xA0)`) gives `rig_attach` (1-based) and finger names. Body (in a pcall): node count `u32(body+0x70)` 1..255, names -> `on_body[hash] = index+1`; `body, body_hand, body_count` set if the left hand name exists; `body_right`, `body_attach`; `body_ref` = the parent bone of the body's bone named like rig bone 30 (link mode 1), else that bone itself (gh:249-254). For every other alive handle: a unit carrying a `GRIP` bone becomes a grip candidate `{handle, node=index+1, count}` (first such bone only); if its parent (`+0x1D0`) is the rig object: the unit whose bone 0 is linked (mode 2) to the rig's weapon bone is `hung` = the in-hand weapon (`gun`); every bone linked (mode 2) to a labelled rig arm bone and not one of the six `SLEEVE` hashes becomes an offset bone `{node, rig=parent+1, part, side, finger}`; `finger` = the body's bone of the same name, for bones linked under the hand but not to the hand bone itself (gh:270-279). `complete`/`whole.left` = some offset bone is linked to rig bone 13; `whole.right` likewise for 34; `right_partial` = right-side bones exist without the hand. A candidate found again keeps its last position `at`. If the weapon carrying a written root offset is no longer the hung one its root gets identity (gh:302-305).

**Two-bone solve `M.solve(V,S,E,H,T)` (gh:137-150).** `upper=|E-S|`, `lower=|H-E|`; `limit = min(upper+lower-.001, max((upper+lower)*STRETCH, |H-S|))` with `STRETCH = .95`; `reach = max(|upper-lower|+.001, min(limit, |T-S|))`; nil if `|T-S| < 1e-5` or `|cross(T-S, E-S)| < 1e-5`; elbow = `S + dir*along + side*out` with `along=(upper^2-lower^2+reach^2)/(2*reach)`, `out=sqrt(max(0,upper^2-along^2))`, `side` in the current bend plane on the side the elbow is now; returns the elbow and the reached hand point `S+dir*reach` (short of T when T is beyond the limit).

**Weight `M.weight(distance)` (gh:153-157).** 1 at or below `FULL = .20` m, 0 at or above `NONE = .32` m, linear between.

**pose_arm(name, arm, rig, S, E, H, T, turn, hand_old, shape_of) (gh:423-473).** Returns false if the arm's own hand bone is not among the pieces (`partial+1`) or the solve fails. `swing = arc(E-S, E2-S)`; `bend = arc(rotate(swing, H-E), T-E2)`; `carried = bend*swing`; forearm roll: `extra = turn * inverse(hand_old) * inverse(carried)`, its twist about `axis = normalize(T-E2)` (swing-twist split: `along = xyz.axis`, normalised with `w`, sign flipped so `w>=0`) is `roll` (gh:435-446). Rigid moves (old pose inverse times new pose): shoulder part `(swing*shoulder_old, S)`, elbow part `(roll*carried*elbow_old, E2)`, hand part `(turn, T)`. For every offset bone of that side: with a finger shape `set_local_pose(piece, node, shape * hand_new * inverse(base))`, otherwise `set_local_pose(piece, node, base * move[part] * inverse(base))`, `base = world_pose(rig, bone.rig)` (gh:458-470). Sets `drawn[name]`.

**left_arm (gh:496-558).** For each candidate: `d` = distance from its grip bone to the body's left hand world position, also against the bone's previous-update position (the smaller) (gh:501-513). Choice: nearest candidate; the bone in use (`current`) is kept unless it is farther than `PINNED = .02` m while another is within it (gh:515); if nothing is kept-and-on and nothing is within PINNED, a candidate with a matching `known[handle]` entry (same node and node count) is taken with its stored finger shapes (`on=true, pin=1`, `recalled+1`) (gh:516-526). No candidate -> reason `no grip bone`. A new bone without memory starts `on=false`, empty shapes. `pinned = off <= PINNED` -> `on=true` and `known[handle] = {node, count, shapes}` (weak-keyed by handle, gh:180, 528-533). `distance` = grip bone to the rig's animated left hand. No pieces -> `no arm pieces on the rig`. `pin` eases 0..1 by `(now-last)/EASE` (`EASE = .15` s; first call jumps) toward 1 when `on`, toward 0 otherwise. `weight = M.weight(distance) * pin * calm`; <= 0 -> release left; reason `hand away from the grip` (on) or `third-person hand is not on a grip`. Target position `lerp(H, grip_pos, weight)`, rotation `Quaternion.lerp(hand_old, grip_rot, weight)`. Fingers: while pinned, each finger's body pose relative to the body's hand (`world_pose(body, finger) * inverse(world_pose(body, body_hand))`) is stored every frame; the stored shape is used whether pinned or not. pose_arm false -> release left, reason `arm is straight`. Else `active`.

**right_arm (gh:559-590).** Needs `rig_attach`, `body_right`, `body_attach` and a usable body, else `no third-person right hand`. `one_handed` = the left reason of this update is `third-person hand is not on a grip` or `no grip bone`. `held, spinning = in_hand(now, aimed or not one_handed or rig_moving(rig, now))` (aimed = `s.aiming==true` only). Target pose = `held * world_pose(rig, rig_attach)`; `right_distance` = its position to the rig's right hand; `weight = M.weight(distance)`; <= 0 -> release right, `gun_release()`, reason `hand away from the weapon`. Rotation `lerp(hand_old, target_rot, weight)`, position `lerp(H, target, weight)`; fingers: body finger relative to the body's right hand, every frame, nothing latched. Solve failure -> `arm is straight`. If spinning: weapon root offset `set_local_pose(gun, 1, inverse(rel) * from_quaternion_position(turn, reached) * inverse(attach))`, `gun_turned=true`, `spun+1`.

**in_hand(now, aimed) (gh:348-395), the spin detector.** `rel = world_pose(body, body_right) * inverse(world_pose(body, body_attach))`, sampled each update; with `0 < elapsed < .25` s: if a hung weapon exists, not `aimed`, and it turned faster than `TURNING = rad(400)`/s or slid faster than `SLIDING = 1.5` m/s -> remember `rest` (the pose before) and `rest_ref` (the hand as the arm-root bone saw it), `rest_keep=1`, `spin_until = now+CALM` (`CALM = .25` s). While `rest` exists: `stirred = now` whenever weapon or hand moves faster than `STIR_TURN = rad(30)`/s or `STIR_SLIDE = .1` m/s; "away" = more than `AWAY_TURN = rad(12)` or `AWAY_SLIDE = .03` m from the rest (weapon-in-hand, or hand-from-arm-root); away and `now - stirred < SETTLE` (3 s) keeps the spin going. If `aimed` -> `gun_release()` and plain `rel`. Outside the spin window `rest_keep` decays by `min(EASE, elapsed)/EASE`; at 0 or with no usable gun -> `gun_release()`. Returned hand place = `rest`, carried by the hand's motion since (`seen * inverse(rest_ref)`), blended toward `rel` by `rest_keep`; second return value `rel` marks spinning.

**rig_moving(rig, now) (gh:332-347).** The rig's weapon bone and left hand as its right hand sees them; faster than `RIG_TURN = rad(45)`/s or `RIG_SLIDE = .15` m/s (sample gap < .25 s) -> true for `RIG_CALM = .5` s. Only evaluated when not aimed and one-handed (short-circuit).

**left_aside (gh:596-615).** `free` = left reason is exactly `third-person hand is not on a grip`; `spinning` = free and a spin is running. `aside` eases 0..1 over `ASIDE = .2` s (step ignored when the sample gap is 0 or >= .25 s), smoothstep `a*a*(3-2a)`. Target = the body's left hand as the arm-root bone sees it, relative to the remembered `rest_ref`, applied to the remembered rest on the rig's weapon bone: `(body_hand * inverse(body_ref)) * inverse(aside_ref) * (aside_rest * world_pose(rig, rig_attach))`; fingers from the body every frame; reason becomes `moved aside`.

**rig_screen(rig, hide) (gh:475-494).** On the edge "left arm correction starts": every rig mesh (index 1..`Unit.num_meshes(rig)`) with `Mesh.visibility(mesh)==true` and `Mesh.num_materials(mesh)==2` gets `Unit.set_mesh_visibility(rig, index, false)`; indices remembered. On the edge "ends" (or rig handle changed): `set_mesh_visibility(rig, index, true)` if the rig is still usable with 63 nodes.

**gun_release (gh:309-315).** Weapon root local pose back to identity if it was written; spin state cleared.

**stop() (gh:665-681).** Offsets cleared, weapon root cleared, rig mesh shown, every state including `known` reset; reason `idle`.

**Reason strings.** Left (`reason`): `idle`, `waiting`, `rig unavailable`, `no grip bone`, `no arm pieces on the rig`, `weapon is being handled` (unreachable, calm is 1), `hand away from the grip`, `third-person hand is not on a grip`, `arm is straight`, `active`, `moved aside`, or an error text. Right (`right`): `waiting`, `rig unavailable`, `no third-person right hand`, `weapon is being handled` (unreachable), `hand away from the weapon`, `arm is straight`, `active`.

**result() fields (gh:683-688).** `frames, scans, weapons, pieces, bones, fingers, distance, off, errors, reason, right, rig_screen_hidden, steady` (always 1), `handled, turning, sliding` (always 0), `swaps, recalled, partial, spun, gun, arm_root, gun_turned, right_distance`.

**Kit Lua vs game (header gh:10-15, 38-39).** The rig's own bones are never written: "the game's animation runs after the kit's update and overwrote every such write before it was drawn" (smooth.41). What is drawn is the arm pieces linked bone by bone onto the rig, and a linked bone keeps its local pose as an offset from its rig bone; so the kit reads the rig's animated pose of the previous evaluation at update time and writes offsets that the game applies on top of the next animated pose. The body's left hand is pinned to the grip bone by the game's own solve and "trails the bone's position at the kit's update by up to a frame of travel".

#### Motion, aim markers, menu, placement editor, head placement, input

##### motion_runtime.lua / motion.lua / locomotion.lua

Called once per kit update as `motion:update()` (runtime.lua:614), after `controller:update()` and `editor:update()`.

**Gate (motion_runtime.lua:232-249).** Runs only when the controller is enabled, `result.mode=='pose_offset'`, `result.phase=='running'`, and `controller.lease` is `backend.alignment_lease`, not `done`, not `cleanup_only`. Otherwise state is cleared and phase is 'idle'. While still enabled in pose_offset but not running, the last sent modifier is kept as "continuity" for at most .25 s, keyed by `lease.origin.identity` and `player_identity`; a new lease with the same identities within .25 s starts from that modifier with a handoff fade and calls `backend.presentation_resume` (250-263).

**Frame step (223-231).** `frame_step = clamp(elapsed, 1/240, .05)` from the runtime clock (`stingray.Application.time_since_launch`, runtime.lua:160-173); first update uses 1/60; a held or rewound clock keeps the previous step. `max_step = .05` (51).

**Options (264-280).** From `menu:motion_options(defaults)`. Scale must be finite and within 0..2, else neutral 'Motion scale is invalid'. When any of `enabled, scale, bob_strength, bob_speed, sway_strength, sway_speed, sprint_strength, camera_bob_strength, camera_bob_speed, camera_sway_strength, camera_sway_speed` changed, `model:configure` runs (which resets the model but keeps `previous`, `shoulder` and `aim`, motion.lua:46-52), `last_time` is cleared and the next value is resent.

**Editor open (281-296).** Motion is frozen: if the editor's preview rotation is all zero, an identity modifier is sent (phase 'editor_frozen'); otherwise only the draft yaw/pitch/roll, converted to parent space, is sent directly (no pop guard). No bob, sway or action blending while the editor is active.

**Sample (locomotion.lua:120-159).** All reads go through the backend's guarded reader:
- `backend:player_snapshot()` must be `safe` with `camera_kind==6`; lease must match player identity and `s.record`.
- Fingerprint check: bytes at exe + `profile.motion.world_pose.rva` (0x1f30e0) must equal `profile.motion.world_pose.hex` (fp/profile.lua:69), "Motion world-pose getter changed" otherwise.
- Unit lookup (103-115): registry = ptr(exe + `profile.pose.executable.unit_registry`); index = ref % 0x400000; generation = floor(ref/0x400000) % 256; capacity u32 at registry+0x98; generations ptr at +0xA0; objects ptr at +0x88; object's vtable slot +0xE8 must equal exe+0x28d3c0; scene graph = object+0x60; node count u32 at graph+0x10 (1..512); world matrix array ptr at graph+0x28, stride 0x40.
- Root position = body unit (`s.body`) world matrix 0, floats 13, 14, 15 (x, y, z in world metres).
- Parent basis = first-person rig (`s.viewmodel`, node count > 50, must be the lease's rig object) world matrix index 4 (array + 4*0x40).
- Camera basis = 16 floats at `s.context+0x0C`; forward = floats 5..7.
- yaw = atan2(-forward.x, forward.y); pitch = atan2(forward.z, horizontal length).
- `ads = s.controller_active`; `shoulder_aim = s.shoulder_aim==true and not s.controller_active`; `reload = s.reloading==true`; `stance_flags = s.flags` (24 bytes); action layers l1, l3, l14 from `backend:action_state(s)`.
- A second `player_snapshot` must show the same player, record and viewmodel.
- Sprint (85-93): action resource matches, 31 layers, and (l14==36, or l14==3 with verified on-foot lifecycle), and (flags byte 10 has bit value 4 set, or l3==71, or l14==3 and l3==68 and l1 is 75 or 78).
- Basis validation (9-20): each axis squared length in (.25, 4); axes orthogonal within .03; handedness > .95 for rotations (39).

**Per-action placement blend (motion_runtime.lua:318-363).** Live because `backend.action_placement` is always set.
- Action = `Actions.select(sample, mapping)` (action_placement.lua:122-138), priority: ads; slide / dive; reload; aim (shoulder aim); sprint; crouch / prone; optional mapping; idle. Posture comes from stance flags: posture = floor(byte10/32)%4*32; 0x40 with byte 13 bit 32 = dive; 0x20 with byte 11 bit 2 = slide; 0x20 = crouch; 0x40 = prone; only when the action resource is '4D1C334D294DFA97' and verified (action_placement.lua:50-65). The action is published through `backend.presentation_action(action, timestamp)`.
- On a new lease or a changed `backend.action_placement_revision`: the saved 'ads' placement x, y, z is sent with `backend:set_ads_placement` (-> `NADSE_SetADSPlacement`).
- On a new lease, revision or action: target = saved placement for that action; for 'ads' the target is the placement already reached (`controller:get_placement()`) with only yaw/pitch/roll replaced by the saved ads rotation (so the weapon does not move back toward idle as native ADS takes over). Start value = `controller:get_placement()`.
- Blend factor per update: if a shared presentation packet exists (`backend.presentation_transition()`, duration .18 s, max gap .25 s, runtime.lua:670-672) and it is not already spent (or this is the lease's first profile), `phase_step`: e = p*p*(3-2p), step = (e - used)/max(.000001, 1 - used), step = 1 when p==1; otherwise `1 - exp(-clamp(dt, 0, .1)/.10)`.
- Each of x, y, z, yaw, pitch, roll moves `before + (target-before)*blend`, snapping when within .0001; for 'ads' the three angles jump to the target at once. Changed values go to `controller:set_placement`.
- The blended yaw/pitch/roll (degrees -> radians) are added to the motion model's rotation output before conversion (384-388); translation of the placement is carried by `controller:set_placement`, not by the motion channel.

**Bob / sway model (motion.lua:55-158).** `dt` = time since the last update that advanced (first is 1/60).
- No output (zeros, flagged reset) on: invalid sample, dt <= 0, first sample, owner change, dt > 1 s.
- Rebase (hitch handling, 76-79): if dt > .25 ('time_gap'), or distance > 2 m, or distance/dt > 20 m/s, or yaw change > 90 degrees, or pitch change > 90 degrees ('discontinuity'): this sample's travel and turn are zero and dt = min(dt, .05); stance weights and remaining bob keep easing.
- `step = min(dt, .05)` is used for all smoothing; velocities use the real dt.
- speed = horizontal travel / dt. right = (wx*cos(yaw) + wy*sin(yaw))/dt; forward = (-wx*sin(yaw) + wy*cos(yaw))/dt.
- Bob phase += 2*pi * travel / 3.4 * bob_speed (one cycle per 3.4 m), modulo 2*pi.
- amplitude = damp(amplitude, clamp(speed/4, 0, 1), step, .10). `damp(v, t, dt, tau) = v + (t - v)*(1 - exp(-dt/tau))`.
- ads = 1 when `sample.ads==true` (number values clamp 0..1).
- Eased aim gait: when ads == 1, `clear_ads` (sprint_hold, sprint, shoulder = 0) and aim = shoulder and 1 or 0; otherwise aim = damp(aim, shoulder and 1 or 0, step, .10 entering / .12 leaving), snapped to 0 or 1 within .00001. `shoulder` (pose aim) stays 0 because `shoulder_aim_enabled` is never configured.
- Sprint: active when motion is on (enabled and scale > 0), `sample.sprint`, ads < 1 and not shoulder aim. `sprint_hold` = .10 s while sprinting, counts down by dt afterwards, 0 on ADS or shoulder aim. sprint weight = damp(sprint, target, step, tau) with tau .12 rising, .22 falling, .15 falling while shoulder aiming; snapped at .00001.
- right_speed, forward_speed = damp(..., step, .12).
- look_x = damp(look_x, clamp(turn/dt*.0015, -.012, .012), step, .065/sway_speed); look_z = damp(look_z, clamp(-pitch_turn/dt*.001, -.008, .008), step, .065/sway_speed).
- stance_weight = 1 - ads; weight = stance_weight * scale (0 when motion is off).
- walk = (1 - sprint)*(1 - aim); gait = walk + sprint*sprint_strength*2.5.
- bob_x = sin(phase)*.008*amplitude*gait*bob_strength; bob_y = cos(2*phase)*.003*amplitude*gait*bob_strength; bob_z = sin(2*phase)*.006*amplitude*gait*bob_strength.
- dx = settle(clamp((bob_x + (look_x - clamp(right_speed*.001, -.008, .008))*walk*sway_strength)*weight, -.15, .15)); dy = settle(clamp((bob_y - clamp(forward_speed*.0006, -.006, .006)*walk*sway_strength)*weight, -.15, .15)); dz = settle((bob_z + look_z*walk*sway_strength)*weight). `settle` zeroes magnitudes below .00001 m.
- Sprint pose rotation: pitch = -12 degrees, yaw = +18 degrees, roll = -10 degrees, each times sprint * sprint_strength * stance_weight.
- Per action: idle = full bob and sway; normal aim = bob and sway faded out by `aim` over about .10 s; ADS = stance_weight 0, everything zero ("ADS remains steady"); sprint = walking bob/sway replaced by `sprint*sprint_strength*2.5` gait bob plus the lowered rotated pose; reload has no term in the model (it only selects the saved 'reload' placement).
- Camera outputs (147-152; strengths .006, .004, look gains .002/.0015, clamps .015/.01, tau .08) are computed but multiplied by `camera_bob_strength` / `camera_sway_strength` = 0 and never read: the mode adds no camera bob, sway or kick here. `camera_kick_enabled` is not read anywhere in `src/`.
- Ranges accepted by `configure` (32-45): strengths 0..3, speeds .25..2, scale 0..2. Model default `sprint_strength=0` (24) is overridden by the menu value.

**Held clock (motion_runtime.lua:369-376).** dt == 0: nothing is advanced; the update returns without sending unless the ADS state changed, in which case a zero output is run through the handoff.

**Conversion (locomotion.lua:34-79).** Translation: camera-local (dx right, dy forward, dz up; each within .15) -> world along the normalized camera axes -> parent-local by projection divided by the parent axis squared length; scaled down uniformly if any component exceeds .15. Rotation: quaternion = yaw about camera axis 3, then pitch about axis 1, then roll about axis 2, each axis expressed in the parent basis; normalized, qw >= 0, total angle capped at 120 degrees - .00001 rad; each input angle must be within 90 degrees. Result `{dx,dy,dz,qx,qy,qz,qw}` must pass `vector` (motion_runtime.lua:12-24): translations within .15, unit length within .0001, qw >= cos(60 degrees).

**ADS handoff (146-176).** A fade starts when the shared packet's epoch changes (packet.ads matching), or the ADS state flips, or on the first update of a lease if ADS or `lease.pose_blend_entry`. It blends from the last sent modifier to the new one with smoothstep t*t*(3-2t); t is the shared packet progress when available, else an own timer of `handoff_seconds = .15` s that advances at most `max_step` (.05 s) per update. While ADS and fading with a non-identity rotation, `authored_ads=true` is set, which selects `NADSE_SetAuthoredTransform` instead of `NADSE_SetTransform`. `parent.authored_ads` is also set whenever the rotation is non-identity before the handoff (396).

**Weapon offset pop guard, "softened" (48-49, 92-108).** For non-direct submissions on the same lease, if no guard and no handoff is running: distance from the last accepted modifier > max(.002 m, 1 m/s * frame_step) or rotation > max(1 degree, 360 degrees/s * frame_step) starts a guard (counter `softened` + 1). The guard crossfades from the last accepted value over `guard_seconds = .12` s with smoothstep, advancing by frame_step/.12 per update.

**Apply (109-131).** Identical consecutive submissions (same lease, phase, ads flag, authored flag and all seven numbers) are coalesced and not sent. Otherwise `backend:set_pose_motion(lease, value)` (backend.lua:983-1021): revalidates bounds, lease, a fresh snapshot (`camera_kind==6`, same identity and record) and the pose context, publishes the presentation transition token (`set_pose_transition` -> helper `transition`), then `helper.set_motion(handle, dx, dy, dz, qx, qy, qz, qw, authored_ads)`. A refusal clears everything, phase 'unavailable', logs 'FPV motion: <reason>'. This happens once per update at most.

**neutral (177-184).** Resets the model and sends the identity modifier (through the pop guard); used for errors ('Motion sample unavailable', 'Motion clock moved backwards', 'Converted motion exceeds native bounds', ...) and counts `read_failures`.

**reset (185-188).** `clear()`: drops continuity, profile state, guard, handoff, model state, sent/shown records; phase 'idle'. Called by `stop_pose` (runtime.lua:889).

**capture_neutral (195-219).** Parked (Fird calibration only): sets the idle placement (zeros when native ADS), sends only its rotation directly, phase 'capture'.

**result (189-192).** Copy of counters and state: phase, applied, coalesced, softened, read_failures, write_refusals, time_held, dx..qw, action, ads, shoulder_aim, sprint_raw, l1, l3, l14, sprint_evidence, speed, gait_phase, reset, rebased, sprint_weight, sprint_target, sprint_held, bob_weight, shoulder_weight, ads_handoff, capture_ready.

##### aim_markers.lua

The "markers" are two groups of the game's own aiming HUD: the fixed crosshair group (widget at crosshair+0x110) and the moving aiming-circle group (crosshair+0x220). Hit markers and scope markings are not touched (menu.lua:348).

- Called after every qualified update: `aim_markers:update()` when the frame succeeded and the owner is not draining, else `aim_markers:stop()` (runtime.lua:1216). Also stopped by `stop_pose` (871) and shut down with the mod (197).
- Source (`M.source`, 68-109): nil when a foreign camera owns the view; uses `backend:snapshot()`; requires `safe`; first person when `camera_kind==6` (this includes ordinary native ADS with F11 off, comment at 65), or `camera_kind==4` while the controller is enabled in pose_offset and the camera fallback is ready. Weapon key = hex of 8 bytes at `weapon_object+0x358`, cached per snapshot identity. `aiming = shoulder_aim or controller_active or aiming`.
- Decision (195-211): suppress only while aiming and (global setting is false, or global is unset and the weapon is not listed true in `aim_markers`). Global true never suppresses. Hip view always keeps the game's markers.
- HUD probe (21-47): validates four byte fingerprints, then state = ptr(game+0x2ce1508); when the global setting is not explicitly false the probe is mission-only (u32 at state+0xac29c must be 4); root = ptr(game+0x2e28788); u8(root+0x24e334) must be 1; crosshair = root+0x24e340+0x19a678; ptr(crosshair+0xf0) must equal root+0x820; context = ptr(crosshair+0xf8) with u8(context)==1; world = ptr(context+0x10); each widget's +0xf0 must point to crosshair and +0xf8 to context, and its u32 must be non-zero.
- Visible = bit 16 (0x10) of the widget's first u32 (49). Hide/show calls the game's setter at game+0x12e4e20 as `void(uintptr_t widget, uint8_t value)` after checking with `VirtualQuery` that the page is committed (0x1000), image (0x1000000) or mapped under Wine (0x40000 with matching allocation base), and executable (protect 0x20, 0x40 or 0x80); then reads back the bit (50-63).
- Lease: per weapon identity + player identity + HUD identity. Each update, any of the two widgets currently visible is recorded (`before=true`) and hidden, so a group the game re-shows is hidden again next update.
- Restore (`stop`, 176-194): if the live HUD is still the same, every recorded widget that is still hidden is shown again; a rebuilt HUD is left alone.
- `setting()` (149-160): the global value when set; otherwise, while aiming, whether the current weapon is listed; otherwise true. `set_setting(value)` saves `show_aim_markers` and updates at once.
- `toggle(key, identity)` (239-256) is the per-weapon switch; it refuses with 'Use Show aim markers to change all weapons' once a global value exists, and no menu control calls it.
- Faults are logged once as 'Aiming markers: <reason>' and shown as the status line.

##### menu.lua update cycle

`menu:update()` is the last step of every update path (runtime.lua:574-639) and once at enable (1202).
1. Closing: only `refresh_status`.
2. `Integration.menu_api()` returns the facade (rebuilt when the `DBFMCM` global or its `shared_input_service` changes). If the owner changed or the record is gone, unregister.
3. If not registered: build the spec and `api.register` it; the facade stores it and bumps its revision, which makes the kit's `src/mcm.lua` re-register on its next update (mcm.lua:81).
4. `refresh_status`: `refresh_weapon_controls` (family checkbox sync, per-weapon clipping drafts and disabled flags, weapon text), marker checkbox sync through `handle.set` with the `syncing_markers` guard so no global override is saved, then status label and disabled flags only when something changed.
5. `poll_hotkeys(api)`.
`begin_shutdown` sets closing and clears hotkey state; `shutdown` refuses while the controller is not clean or the editor is active, then unregisters and unsubscribes.

##### placement_editor.lua state machine

Phases: idle -> queued -> handoff -> active -> closing -> idle.
- `request_open(kind)` (197-218): selects the mode; refuses when not idle ('Placement editor or its cleanup is already active'), when `store.can_start` refuses (cleanup pending for FOV, blur, head camera, head visibility, movement, DoF, or 'Photo Mode or another camera owns the view', runtime.lua:753-768), when the Epic LUT editor is active, when the controller is neither enabled nor clean, or when the DBFMCM provider lacks the required API ('DBF-MCM captured placement input is unavailable'). Then phase 'queued', action index 1, 'Opening placement editor'.
- queued (234-239): `api.close()` closes MCM; next update continues.
- handoff (240-300): requires MCM closed and no input owner; game cursor must have mouse focus and be hidden; reads the current placement (controller when enabled, else the saved one) as `original`; if first person is off it calls `controller:start(0,'pose_offset')` directly; requires a weapon target ('Equip a firearm and wait for first person before editing'); previews the selected action's saved placement; rechecks input ownership; `input.acquire()`, `input.set_focus(true)`, `api.create_photo_view()`, `view.refresh_font()`; phase 'active', 'Drag to place arms and weapon; save to keep changes'.
- active, each update (301-393): closes with a reason if the weapon target changed (no restore), the controller is no longer running/reacquiring/waiting/suspended/suspend_pending, the input provider changed, MCM opened, game focus was lost, the input sample or resolution is unavailable, Epic LUT took the cursor, or the provider changed. Otherwise processes input as in (c), applies the draft with `controller:set_placement` only when it changed, and draws the panel.
- `cancel(reason)` / `shutdown()` / `finish(all)` -> `close` -> `release_all` (107-136): restore `original` unless saved or the target changed; `input.release()`; `view.release()`; any failure keeps phase 'closing' and retries every update.
- Effect on the view while open: first person stays on; the cursor is captured in menu mode; first-person movement handling is stopped (`movement:stop`, runtime.lua:629); bob, sway and action blending are frozen; the rig shows the draft placement and rotation. Closing never turns first person off; turning first person off cancels the editor ('FPV switched off', runtime.lua:877).

##### head_placement.lua

A small controller-shaped object for the Fird head-camera offset: `get_saved` (saved `fird_head_camera` or `{x=0,y=0,z=.04}`), `get_placement`, `set_placement` (within .5 m; calls `camera:set_offsets` only while Fird is enabled), `placement_target` ('head_camera', player identity), `save`. On the shipped path its only effect is setting `backend.fird_camera_offset` at construction (12).

##### shared_input.lua / input_adapter.lua

Used only when the facade had to build its own `camera_input` (first_person.lua:33-35).
- Idle (no token): `frame()` records `stingray.Window.has_focus()`; `down(vk)` reads the engine keyboard by name; mouse codes (1, 2, 4, 5, 6) return false; nothing is captured.
- Editor open: `acquire()` -> `set_mode('menu')` -> `service.acquire('goose_first_person_editor', 'menu', nil, {keyboard=true})` when the service offers keyboard capture with a 32-byte canonical mask, else without keyboard. If keyboard capture is refused with 'Keyboard request refused (0x00000005)', it releases, remembers the denial and retries without keyboard (engine keys). `set_focus(true)` keeps 'menu' mode; `set_focus(false)` would select 'camera' (unused by the editor).
- `sample()` once per frame: validates ownership and the snapshot; with captured keyboard requires state 2, active, matching mask and generation (state 1 = arming, returns a pending sample); pointer x scaled to `stingray.Gui.resolution()` width, y flipped to y-up; buttons and wheel are zeroed until all buttons were released once after acquiring; `click` = bit 1, `right` = bit 2; in 'camera' mode raw dx/dy are required.
- `down(vk)`: mouse codes from the sample's button bits (1 -> 1, 2 -> 2, 4 -> 4, 5 -> 8, 6 -> 16); keys from the captured bitmap when the mask covers them (Shift 16, Ctrl 17, Alt 18 also match their left/right codes 160-165), else the engine keyboard.
- `menu_sample()` (adapter): `{x, y, click, wheel, dt}` with dt = clamp(now - previous, 0, .1) from `stingray.Application.time_since_launch`, 0 on the first sample; wheel is consumed once per frame.
- `release()`: releases the token; on failure marks restoration pending so the editor stays in 'closing' and retries.

## 3. Helper DLLs

| profile entry | library | exports | hook | loaded on the shipped path |
|---|---|---|---|---|
| `camera` | `native/native_ads_camera_26568081_e88ff8dc46b2.dll` | `NADSC_*` | CameraAPI `+0x20` (position setter; expected target `exe+0x2dd5a0`, caller `game+0x110bff5`) | yes |
| `pose` | `native/native_ads_editor_26568081_4940b83e17ad.dll` | `NADSE_*` (23) | Unit API `+0x38`, `+0x48`, `+0x40`, `+0x00`, `+0x90` | yes |
| `fov` | `native/native_ads_fov_26568081_764664986628.dll` | `NADSF_*` | CameraAPI `+0x80` (target `exe+0x2dd080`, caller `game+0x110bfda`) | yes |
| `blur` | `native/native_ads_blur_26568081_c18c221e2cc6.dll` | `NADSB_*` | parameter setter table `exe+0x2873b18` (target `exe+0x2df740`, caller `game+0xe9ac4b`) | yes |
| `near` | `native/native_ads_near_26568081_14a3c38c08f8.dll` | `NADSN_*` | CameraAPI `+0x68` | yes |
| `alignment` | `native/native_ads_alignment_26568081_8e0af36233bd.dll` | `NADS_*` | two call sites (`position_return`, `rotation_return`) | no (mode `unlinked_sights` is never started) |
| `held_fire` | `native/native_ads_heldfire_26568081_4553a768a134.dll` | `NADSHF_*` | body animation event filter | no (Fird Person only) |
| (none) | zoom: only `native_ads_zoom_26251122_4d5d246dfecb.dll` exists | | MinHook inline hook | no (`zoom.lua` required nowhere) |
| kit | `native/guarded_reads_*.dll` | `GGR_*` | none | not by the mode |

Build provenance is uneven and should not be relied on for the rewrite: the shipped camera and editor file names carry the SHA-256 prefix of the binary, the fov and blur names a hash of the source folder, and the per-helper `build_*.py` scripts are older MSVC builders with other output names. The command that built `native_ads_editor_26568081_4940b83e17ad.dll` is not recorded in the tree.

### Camera helper: `native/native_ads_camera_26568081_e88ff8dc46b2.dll` (exports `NADSC_*`)

**Sources.** `native/first_person/native_ads_camera.cpp` (1304 lines) including `native_memory.h`, `native_ads_camera.h`, `free_aim_math.h`, `camera_correction.h`, `camera_handoff.h` (cpp:6-14). Not included by this DLL: `camera_transition.h`, `pin_look_math.h`, `presentation_phase.h` (leftovers of the removed pin/look path; `build_camera.py:29` still hashes `camera_transition.h` as metadata). `native_ads_input.S` / `.asm` belong to the **editor/pose** helper (`build_editor.py:26-27`, symbols `NADSE_Input90Thunk`, `NADSE_Input00Thunk`, `NADSE_InputCompareExchange`), not to either DLL of this part.

**Build.** Two scripts exist:
- Current (what produced the 26568081 files): `tools/build_first_person.py:5-13` = `zig c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -O2 -shared native_ads_camera.cpp -o native_ads_camera_26568081_<stamp>.dll` (zig 0.14.1, MinGW target; one translation unit, nothing else linked), then the matching `camera_fixture.cpp` is built as an exe and run under wine (15-18). The script computes `<stamp>` = first 12 hex of sha256 over the concatenation of all `.cpp` and `.h` files of `native/first_person` in sorted order (line 9), copies the DLL into `native/` and rewrites the name in `profile.lua` (24-29). Also stated in `SOURCE.md:71`.
- Legacy MSVC: `native/first_person/build_camera.py:14-22` = `cl /nologo /c /MT /EHsc /std:c++17 /O2 /W4 /WX` + `link /nologo /DLL /DYNAMICBASE /NXCOMPAT ... kernel32.lib`, output `native_ads_camera_0_1_48.dll` (line 10).
- Observed: the shipped file's 12-hex suffix `e88ff8dc46b2` equals the first 12 hex of the sha256 **of the DLL file itself** (checked with sha256sum), not the source-concatenation stamp the script describes. Same for the alignment DLL (`8e0af36233bd`). So the installed names were derived from the binary hash by a step not in these scripts.

**Exports** (header `native_ads_camera.h`; all return `uint32_t`, 1 = ok, 0 = refused with the code in `NADSC_Error`, unless noted):

| export | signature | what it does | Lua on shipped path |
|---|---|---|---|
| `NADSC_Validate` | `uint32_t NADSC_Validate(const NADSC_Config*)` (h:26, cpp:1080-1086) | Read-only: config bounds, owner proof, graph/head node, contract (images, code bytes, slot), no writes | debug-API-only (`camera:preflight`, `runtime.lua:1196`) |
| `NADSC_Begin` | `uint32_t NADSC_Begin(const NADSC_Config*,uint64_t* generation)` (h:34, cpp:1087-1121) | Installs the CameraAPI+0x20 slot hook and arms a generation; returns 1 armed, 0 rejected (generation 0), 2 cleanup still owed | LIVE (`camera.lua:147`) |
| `NADSC_End` | `uint32_t NADSC_End(uint64_t generation)` (h:35, cpp:1122-1129) | Disarms, drains the hook, restores the slot | LIVE (`camera.lua:253`) |
| `NADSC_ReadStats` | `uint32_t NADSC_ReadStats(uint64_t generation,NADSC_Stats*)` (h:39, cpp:1130-1138) | Counters + health; generation 0 accepted only while nothing is reserved | LIVE, every kit update (`camera.lua:108`) |
| `NADSC_Error` | `uint32_t NADSC_Error(void)` (h:40, cpp:1139) | Last error code | LIVE on failures (`camera.lua:87`) |
| `NADSC_SetOffsets` | `uint32_t NADSC_SetOffsets(uint64_t generation,float right,float forward,float up)` (h:43, cpp:1141-1158) | Live change of eye offsets (each abs <= .5); re-seeds the 150 ms correction from the last presented eye | CONDITIONAL: Fird head-camera editor only -> never on shipped path |
| `NADSC_FreeAim` | `uint32_t NADSC_FreeAim(uint64_t generation,uint32_t milliseconds)` (h:48, cpp:1160-1168) | Retired: any nonzero value returns INVALID_CONFIG; 0 only clears state | menu-button-only (called with 30000 -> always refused) and debug `stop_free_aim` |
| `NADSC_FreeAimStats` | `uint32_t NADSC_FreeAimStats(uint64_t generation,NADSC_FreeStats*)` (h:49, cpp:1169-1179) | Counters that nothing increments any more | debug-API-only |
| `NADSC_ObserveParent` | `uint32_t NADSC_ObserveParent(uint64_t generation,uint32_t milliseconds)` (h:52, cpp:1181-1189) | Opens a <= 3000 ms read-only window comparing rig node 50 rotation with the camera world/local matrices on native6 frames | menu-button-only (Free aim button, 3000 ms) + debug |
| `NADSC_ParentStats` | `uint32_t NADSC_ParentStats(uint64_t generation,NADSC_ParentStats45*)` (h:53, cpp:1190-1196) | Reads that window's counters | debug-API-only |
| `NADSC_SetKick46` | `uint32_t NADSC_SetKick46(uint64_t generation,uint32_t enabled)` (h:57, cpp:1198-1204) | Retired: nonzero refused; 0 clears | never called (needs `kick_enabled`, shipped false) |
| `NADSC_KickStats46` | `uint32_t NADSC_KickStats46(uint64_t generation,NADSC_KickStats46Data*)` (h:58, cpp:1205-1211) | Dead counters | debug-API-only |
| `NADSC_SetPresentation47` | `uint32_t NADSC_SetPresentation47(uint64_t generation)` (h:61, cpp:1213-1223) | After owner + dispatch proof and a 43-byte code check at game+0x110bfe7, sets `presentation_enabled` (gate for correction, eye hold, exit hold, anchor, seated view, native sampling) | LIVE (`camera.lua:396`) |
| `NADSC_SetCameraMotion47` | `uint32_t NADSC_SetCameraMotion47(uint64_t generation,float x,float y,float z,float pitch,float yaw,float roll)` (h:63, cpp:1225-1230) | Retired: any nonzero value refused; stores zeros that nothing reads | never called |
| `NADSC_Profile47` | `uint32_t NADSC_Profile47(uint64_t generation,uint32_t count)` (h:66, cpp:1232-1236) | Times the next `count` (1..1000; Lua passes 400) hook calls with QPC | debug-API-only |
| `NADSC_ProfileStats47` | `uint32_t NADSC_ProfileStats47(uint64_t generation,NADSC_Profile47Data*)` (h:67, cpp:1237-1244) | mean / p99 / max microseconds | debug-API-only |
| `NADSC_SetHandoff49` | `uint32_t NADSC_SetHandoff49(uint64_t generation,float entry_ms,float rotation_ms)` (h:82, cpp:1249-1257) | Sets the two opt-in eases (0..500 ms each; nonzero needs presentation) | LIVE but with `(0,0)` = both off (`camera.lua:399-403`, `profile.lua:76`) |
| `NADSC_HandoffStats49` | `uint32_t NADSC_HandoffStats49(uint64_t generation,NADSC_Handoff49Data*)` (h:83, cpp:1258-1267) | Ease counters | debug-API-only |
| `NADSC_SetCutHold50` | `uint32_t NADSC_SetCutHold50(uint64_t generation,uint32_t mode)` (h:92, cpp:1270-1278) | mode bits: 1 exit hold, 2 eye hold, 4 aim-release anchor, 8 seated head; mode <= 15, nonzero needs presentation | LIVE with 15 (`camera.lua:404-412`; `profile.lua:78-84`) |
| `NADSC_EntryTrace50` | `uint32_t NADSC_EntryTrace50(NADSC_EntryTraceData*)` (h:93, cpp:1279-1286) | Copies the 4-row entry trace, exit-hold count and mode bits; no generation | never called from this Lua (no cdef in `camera.lua`) |
| `NADSC_SetWeaponKey50` | `uint32_t NADSC_SetWeaponKey50(uint64_t generation,uint32_t key)` (h:97, cpp:1289-1293) | Stores the weapon unit reference Lua sees (0 = unknown) | LIVE, on change (`camera.lua:452-456`) |
| `NADSC_AnchorStats51` | `uint32_t NADSC_AnchorStats51(NADSC_AnchorData*)` (h:100, cpp:1296-1304) | Anchor counters | never called from this Lua (no cdef) |

Structs: `NADSC_Config` 88 bytes `{uint32 size,abi; uint64 game,exe,context,camera,avatar_owner; uint32 avatar_index,avatar,body,camera_unit; float forward,up,smoothing_ms,blend_ms; uint32 max_gap_ms,reserved;}` (h:11-18,24); `NADSC_Stats` 80 bytes (h:19-23,25). Config bounds (cpp:1043-1050): `size==88`, `abi==1`, `reserved==0`, `avatar_index<8`, refs nonzero and != 0xffffffff, `|forward|<=.5`, `|up|<=.5`, `0<=smoothing_ms<=500`, `0<=blend_ms<=1000`, `16<=max_gap_ms<=1000`.

**Lua cdef vs C.** `camera.lua:19-74` declares the same layouts and asserts `sizeof(NADSC_Config)==88`, `sizeof(NADSC_Stats)==80` (75). Differences: (a) `NADSC_ParentStats45` and the two ObserveParent functions are declared twice in separate `pcall(ffi.cdef…)` blocks (20-22 and 28-30); the second block's duplicate struct comes after its `NADSC_FreeStats` / `NADSC_FreeAim` / `NADSC_FreeAimStats` lines, so a redefinition error there (hidden by pcall) should leave those earlier declarations in place (LuaJIT applies a cdef declaration by declaration; my reading, not run). (b) `NADSC_EntryTrace50`, `NADSC_AnchorStats51`, `NADSC_EntryTraceData`, `NADSC_AnchorData` are not declared. (c) `Adapter:set_kick` passes `enabled and 1 or 0` (222) and `prepare` would pass `true` -> 1, which the DLL always refuses (cpp:1199). (d) `Adapter:free_aim(…,30000)` is always refused (cpp:1163). (e) `Camera:set_motion` accepts values up to .03 (302) but the DLL refuses anything nonzero (cpp:1226). (f) `handoff_stats` reads `native_mismatches`, which C always writes as 0 (cpp:1264).

**The one hook.** Function-pointer slot swap, no code bytes written.
- Location: `root = *(game.dll + 0x2ce14c8)`, `api = *(root + 0x20)` (the "CameraAPI" table), slot = `api + 0x20` (cpp:1033-1035). The slot must hold `helldivers2.exe + 0x2dd5a0` (`SETTER`, cpp:23,1039).
- Verification before install (`contract`, cpp:1028-1042): game.dll PE `TimeDateStamp==0x6af322c1`, `SizeOfImage==0x3382000`; exe `TimeDateStamp==0x6af2d5e6`, `SizeOfImage==0x29fe000` (cpp:1029, `image` 213-219); bytes at game+0x110bff5 == `41ff5120` (`call qword [r9+0x20]`); 47 bytes at exe+0x2dd5a0 == `48895c2408574883ec60488bd9498bf88bcae81931dcff4c8b4b184c8bd04c6343208b4708498bd048c1e206498b4920`; 47 bytes at exe+0x1f30e0 == `40534883ec204863dae8e2d5eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3`; exe+0x28d3c0 == `488d4160c3`; exe+0x28d510 == `488b8178010000c3` (cpp:1030-1032). Slot must be 8-aligned, committed, `PAGE_READWRITE` or `PAGE_WRITECOPY`, in a PE-section mapping whose AllocationBase is the exe (`writable_slot` cpp:432-445; `native_memory.h:4-13` treats Wine `MEM_MAPPED` as image). Target must be in an executable PE page (cpp:1041, 228-232).
- Install (`NADSC_Begin`, cpp:1087-1121): config check; refuse `BUSY` if reserved; `prepared()` (owner capture, `p.safe`, request kind 4 or 6, body graph + head node, head distance, for kind 4 camera unparented, context basis orthonormal; cpp:1051-1058); `contract()`; pin this DLL and the exe with `GetModuleHandleExW(FROM_ADDRESS|PIN)` (1059-1062,1094-1097); bump generation and write it out with `WriteProcessMemory` (1099-1100); `InterlockedCompareExchangePointer(slot, setter_hook, exe+0x2dd5a0)` inside `__try` (452-464,1107); under `policy_lock` re-prove owner/kind/safe/graph/dispatch/unparented, then copy the policy, zero counters, store `return_address = game + 0x110bff5 + 4` (= game+0x110bff9), publish `callback_generation`, set `armed=1` (1110-1119). Any failure after the swap runs `failed_begin` -> `restore_slot` (1076-1079); return 2 when the slot could not be restored.
- Removal (`NADSC_End`, cpp:1122-1129; `restore_slot` 1063-1072): `armed=0`, `callback_generation=0`, take and release `policy_lock` exclusively (drains a hook in flight because the hook holds it across the forwarded call, cpp:756,1019-1020), `reset`, then CAS the slot back from `setter_hook` to the original only if it still holds `setter_hook` (a foreign value is left alone). The DLL stays pinned for the process lifetime. A retired generation may be Ended again (returns 1).
- Hook signature: `void setter_hook(void* camera, uint32_t unit, const float* native)` (cpp:747), same as the original `void(*)(void*,uint32_t,const float*)` (cpp:15). `native` = position argument (3 floats used, 16 bytes copied); `native+64 bytes` (float index 16) = the quaternion the game passes to CameraAPI+0x28 right afterwards (cpp:471, 685, 704, 1006-1010). The witnessed call pair at game+0x110bfe7: `lea r8,[rbp-0x40]; mov edx,[rbx+8]; mov rcx,[rbx]; mov r9,[rax+0x20]; call [r9+0x20]; mov rax,[rip+…]; lea r8,[rbp+0]; mov edx,[rbx+8]; mov rcx,[rbx]; mov r9,[rax+0x20]; call [r9+0x28]` (bytes at cpp:1220).
- Thread / frame point: the code does not name the thread. It runs wherever game.dll executes the call at game+0x110bff5 (the game's per-frame camera apply). It is protected against the Lua thread with two SRW locks (cpp:16), which implies the authors did not assume the same thread. Stated ordering facts: at this point the camera's own world matrix "can still belong to the preceding frame" (cpp:474-475); the rig pose read here "is not final" for the frame (cpp:669-670); the game copies the rendered camera pose into `context+0x0C..0x3C` after CameraAPI+0x20/+0x28 (`SOURCE.md:29`); Lua's kit update is a separate, earlier or later "Lua Update" step (`presentation_phase.h:5-6` calls the hook a "renderer callback").

**Hook body, step by step** (`setter_hook`, cpp:747-1026).
1. `seen++`. If not armed, or `_ReturnAddress() != game+0x110bff9`: `forwarded++`, call original unchanged, return (750-752).
2. Take `policy_lock` exclusive (held until after the original is called). Continue only if the generation is unchanged, still armed, `camera == config.camera` and `unit == config.camera_unit` (759-760); otherwise forward unchanged.
3. `tick = now()` = QueryPerformanceCounter in ms as double, fallback GetTickCount64 (139-151). `native6_seen` expires when `tick - native6_at > max_gap_ms` (763).
4. `top()` (316-323): read `u32 head,tail` at `context+0x1F8`; both must be < 32; record = `context+0x1F00` when head==tail, else `context+0x200+((tail+31)%32)*0xE8`; read 4 u32: `[1]` must equal the avatar; `kind = [0]&0xffff`; for kind 6 `current_weapon=[2]` and it additionally needs head!=tail and `[3]==body`. Supported kinds: 4 ("normal4") and 6 ("native6").
5. **Kind 6 (native first-person request): always forwards the game's frame** (765-823). Reads only: `remember_native` (574-637) with `capture=presentation_enabled` and a sample when `handoff = entry_ms>0 || rotation_ms>0 || anchor_ads` (770) - shipped: true because of the anchor. It reads the position argument (rig-node-local), the body root matrix (`*(body+0x88)`, 64 bytes), the strict rig proof `direct_camera_parent` (493-518), then a full `owner_status` + `dispatch_owned`; failure of those two **disarms** (`armed=0`, error set; 777). With a full proof it converts: `offset = local · parent_basis`, `relative[axis] = (parent_position + offset - root_pos) · root_axis[axis]` (626-630) = native eye in body-root coordinates; stores `native_eye`, `native_eye_tick` and feeds `correction.remember(relative, current_weapon, tick)` (631-635). Then: `reset(policy,true)` (clears tracking/blend, keeps native history), `native6_seen=true`, `native6_at=tick`, `relative_valid=false`, `eye_delta_active=false`, `native_full_ads=full_ads`, `ads_gap_active=false` (778-780); ADS-anchor learning (781-795, below); weapon-key settling (797-798); entry trace rows (800-811); `last_native` = 16 bytes of the argument, `last_native_tick`, `last_view_tick` (812-814); with `entry_ms>0` only: `ease_native_entry` may substitute an eased local position (821) - shipped off; `observe_camera_parent` (823).
6. **Not kind 6**: `owner_status` + `dispatch_owned` again; failure disarms (824-825). Unsupported kind -> reset, forward (826). Not `fallback_safe` and not within guard grace -> reset, forward (827-828). `on_foot()` false -> reset, error 18, forward (829).
7. **Kind 4 (normal camera) replacement** (830-1015): see state machine. On success `output=adjusted` (4 floats: new world position + the game's 4th float), `replaced++`.
8. `forwarded++`; call original with `output` (1020); publish diagnostics (1021); profiling sample (1022-1025).

**Ownership proof used everywhere** (`owner`, cpp:259-308). `ctx=*(game+0x2e287d0)` must equal config.context; `*(ctx+0)` = camera pointer, `*(ctx+8)` u32 = camera unit; `*(game+0x2ce1ee0)` = avatar_owner; `controller=*(game+0x2e381a8)`, 6 bytes: u32 `mask` (bytes 0-3), byte 4 = aim active ("full_ads"), byte 5 = guard; `count=*(avatar_owner+0x6C)` in 1..8; `descriptor=*(avatar_owner+0x110+index*8)`, 20 bytes, avatar at +8, body at +12; 24 flag bytes at `avatar_owner+0x53e888+index*0x1240`; `registry=*(exe+0x1ab76f8)` with a 32-byte block at `registry+0x88` = `{units ptr @+0x88, capacity u32 @+0x98, generations ptr @+0xA0}` (253-258), capacity in 1..0x400000. Unit reference: `index=ref&0x3fffff`, `generation=uint8(ref>>22)`, checked before and after reading `units[index]` (245-252). Derived flags (292-306): `fire=flags[12]&0x40`; `full_ads=controls[4]!=0`; `shoulder=!full_ads && (flags[10]&4) && (flags[15]&32)`; `seated=flags[14]&4`; `available=controls[5]==0 && !seated`; `safe=(mask==0||mask==2)&&available`; `fallback_safe=(mask==0||mask==2||mask==0x20)&&available`; with seated-head on: `fallback_safe=true` when `seated && mask==0 && controls[5]==0`; `guard_only = mask==0 && controls[5]!=0 && !seated`. `owner_status` returns 3 (READ_FAILED) only for a failed read whose span is committed and readable, otherwise 7 (311-315).

**Body graph / head node** (`graph`, 324-344; `points`, 345-352). Body vtable slot `+0xE8` must be `exe+0x28d3c0`; header at `body+0x70`: node count u32 (1..512) at +0x70, world matrices pointer at +0x88, links pointer at +0x98, name hashes pointer at +0xA0. Head node = the single node with hash `0x8C5570C9`; its link must have low word 1 and a parent index whose hash is `0xE9ECCA21`. `root` = matrix 0 (rows right/forward/up, position at +0x30, i.e. floats 12-14), `head` = position of the head node (`matrices + node*0x40 + 0x30`); head distance must be in .05..3.0 m; root basis must be orthonormal within .02 and right-handed (`orthonormal`, 237-244).

**On-foot proof** (`on_foot`, 357-405). Body vtable `+0x1B0` == `exe+0x28d510`; `*(body+0x358)` == `0x4D1C334D294DFA97`; `instance=*(body+0x178)`; `g=*(instance+0x28)`; `*(g+4)==31` layers; table at `g+*(g+8)`; layer 13: `definition = table + *(table+4+13*4)`; state count `*(definition+8)` in 37..2048; current state pointer `*( *(instance+0x50) + 13*8 )` must equal `definition + *(definition+12+36*4)` (state 36 = on foot), or state 3 (bridge) together with `(role&6)==2` where role = u32 at `avatar_owner+0x53e888+index*0x1240`, and `*( *(game+0x2ce1508) + 0xac29c ) == 3` (scene). First-index uniqueness and a full re-read for stability follow (380-394).

**Camera space checks.** `unparented` (407-415): `*(camera_object+0x1D0)==0`, `*(camera+0x18)==camera_object+0x60`, node index `*(camera+0x20)` < count `*(g+0x10)` (<=512), local position (`*(g+0x20) + index*0x40 + 0x24`) within .01 m of world position (`*(g+0x28) + index*0x40 + 0x30`). `direct_camera_parent` (493-518): camera node index 0; `*( *(camera_object+0x98) ) == 0x00320002` (linked to parent node 50); `parent=*(camera_object+0x1D0)` equals the unit resolved from the rig reference at `avatar_owner+0x53e9c0+index*0x1240`; parent header at +0x70 has count 63; `hashes[50]==0x60ED8C39`; `parent_links[50]==0x40001`; `*(camera_object+0x78) == parent poses (*(parent+0x88))`; node 50 matrix = `poses + 50*64`. `basis` (420-425): 11 floats at `context+0x0C`: right = [0..2], forward = [4..6], up = [8..10].

**State machine of the kind-4 replacement** (shipped values: forward .18, up .04, side 0, smoothing_ms 100, blend_ms 120, max_gap_ms 250, presentation on, hold mode 15, handoff 0/0).

- **Native kind-6 pass-through (ON).** Position and rotation forwarded untouched (shipped, because `entry_ms=0`); only history is recorded (step 5).
- **Seated head (ON, bit 8).** `seated_view = hold_seated && seated_now && presentation_enabled` (840). On its first frame `seated_q` = normalised rotation of the camera's current world matrix (`rendered_rotation`, 661-666; 842-845). `held_view = seated_view && seated_q_valid`. In the held view the pose gate uses `held_axes(seated_q)` (forward = q·(0,1,0), up = q·(0,0,1); 416-419) instead of the context basis, the 8 m / 10 m distance gates are skipped, `blend=1` and the start position is the head itself (868). After commit (1001-1011): read the game's quaternion at `native+64`; if valid and `dt>0`: `seated_q = slerp(seated_q, normalised(wanted), 1-exp(-dt/.18))`; write `seated_q` (16 bytes) into the caller's argument at float index 16; `seated_frames++`.
- **Graph gate.** `points()` fails -> reset, error 16, forward (847).
- **Exit hold (ON, bit 1).** Request is kind 4 but the camera is still parented (`!unparented`): if `hold_exit && native6_seen && last_native_valid && tick-last_native_tick <= max_gap_ms (250)` and the strict rig proof passes, forward a copy of the argument whose first 12 bytes are the last native6 position (the camera is still in rig space), `exit_holds++`, refresh `last_native_tick`; in all cases `reset(policy,true)` and error 15 (848-861).
- **Pose gate.** Argument unreadable/non-finite, context basis not orthonormal, game position > 8 m from the head or > 10 m from the root -> reset, error 14, forward (862-865).
- **Head smoothing (ON, 100 ms).** `local_head[axis] = (head-root)·root_axis[axis]` (871-873). `dt = (tick-previous)/1000` s when tracking and gap <= 250 ms, else `reset(policy, native6_seen)` and seed `relative_head = local_head` with `dt=0` (874-877). `k = 1-exp(-dt*1000/smoothing_ms)` (k=1 when smoothing_ms<=0); `relative_head += (local_head-relative_head)*k` (878-879). `lag = root - head + Σ relative_head[axis]*root_axis[axis]`, clamped to length .06 m (880-886).
- **Blend from the game's camera (ON, 120 ms).** `blend = min(1, blend + dt*1000/blend_ms)`; if the previous frame was native6 (`native6_seen`): `blend=1` and `native6_seen=false` (887-891); `weight = blend²(3-2·blend)` (892). So after a non-native gap the first frame shows the game's own position (blend 0) and reaches the head eye over 120 ms; straight after native6 there is no blend.
- **Target.** `horizontal` = context forward with z removed, normalised; if its length < .0001 use the body root's forward axis (x,y) instead; if still < .0001 -> reset, error 14 (895-901). `target = head + lag + horizontal*forward(.18) + (horizontal.y*side, -horizontal.x*side, up(.04))` (side = 0 shipped; "up" is world Z); `adjusted += (target-adjusted)*weight` (904-907). `desired_relative` = adjusted in body-root coordinates (908-910).
- **Eye hold (ON, bit 2)** (912-955). On the first kind-4 frame after native6 (`from_native`): if `native_eye` is valid and not older than 250 ms: `eye_delta = native_eye - desired_relative`; accepted when `|eye_delta|² <= 1.0`; then `eye_delta_active=true`, `eye_hold_started = eye_swap_tick = tick`, and the delta is stored in the 16-entry per-weapon table under `native_weapon_key` (97-99, 921). While active: `control_mask==2` refreshes `eye_swap_tick`; `keep = (tick-eye_hold_started <= 2500.0) && (mask==2 || (mask==0 && tick-eye_swap_tick <= 300.0))` (930-932). If kept and Lua's `weapon_key` differs from `native_weapon_key` and that weapon's delta is known: glide `eye_delta += (incoming-eye_delta)*(1-exp(-dt/.07))` (934-942). If not kept: `eye_delta *= exp(-dt/.08)` and it ends when `|eye_delta| < .002` (943-947). While active: `relative = desired_relative + eye_delta`, `adjusted` rebuilt from the root, `correction.reset()`, `eye_hold_frames++` (949-954).
- **150 ms exit position correction (ON, but only when the eye hold is not active)** (957-960; `camera_correction.h`). `correction.apply(desired_relative, current_weapon, tick, relative, 150, max_gap_ms)`: if a native eye was remembered for the same weapon, not older than 250 ms and within 1.0 m (`MAX_RESIDUAL_METRES`), start at the native eye and move to the fresh target with a rebased smoothstep: `fraction=min(1,(tick-started)/150)`, `current=fraction²(3-2·fraction)`, `step=clamp((current-used)/max(1e-12,1-used),0,1)`, `out = last_presented + (desired-last_presented)*step`; dropped on weapon change, gap > 250 ms, clock going back, or target > 1.0 m from the last presented value; components limited to |16| (`MAX_RELATIVE_COMPONENT`). Because the eye hold resets the correction on every frame it is active (953) and the hold starts whenever the native eye is valid, this correction runs only when the eye hold could not start (no fresh native eye, or > 1 m apart) or after `NADSC_SetOffsets`.
- **Aim-release anchor (ON, bit 4)** (781-795 learning; 961-993 use). Learning on native6 frames in full aim with a fully proven sample and `native_weapon_key == weapon_key != 0`: weapon root world position (`*(object+0x88)+0x30`) expressed in the rig camera node's axes -> `seen`; a candidate within .005 m of the previous one counts frames; at >= 5 frames and `|seen|² <= 1.0` it becomes `ads` (right, forward, up) for that key. On every kind-4 frame: remember the body node the weapon hangs from (`weapon_body_node`: `*(object+0x1D0)==body`, link low word 2, node = link>>16; 553-558, 964-967). On the first kind-4 frame after native6: `ads_gap_active = native_full_ads && known`, `ads_gap_started=tick` (968-971). While active: `age = tick-ads_gap_started`; `weight = 1` for age <= 50 ms, then `exp(-(age-50)/50)`; ends at weight < .02 or when unknown; weapon position = body node position when known (same matrices table as sampled) else weapon root (560-567); `right = forward × up` of the context basis; `anchored = weapon - (right*ads[0] + forward*ads[1] + up*ads[2])`; refused when > .8 m from the current eye; else `adjusted += (anchored-adjusted)*weight`, `ads_gap_frames++` (972-992).
- **Weapon key settling.** `native_weapon_key` becomes Lua's `weapon_key` after it stayed unchanged for 5 native6 frames (797-798).
- **Commit** (`commit_current`, 712-732; 996-1013). Re-reads the on-foot instance pointers, full owner proof, `fallback_safe || (guard_grace && guard_only)`, dispatch slot, request kind, graph header identity, head/parent hashes and `*(camera_object+0x1D0)==0`. Failure: reset, error set, forward the game's frame; READ_FAILED also disarms. Success: store `last_relative` (+tick, `relative_valid`), `last_head_world`, refresh `last_view_tick` when `fallback_safe` else `guard_grace_frames++`.
- **Guard grace (ON, fixed).** If `fallback_safe` is false only because controller byte 5 is set with mask 0 and not seated, the head camera is kept for `250.0` ms after the last ordinary first-person frame (`last_view_tick`) (827-828; comment 301-306).
- **Entry ease into native6 (OFF, `entry_ms=0`).** `ease_native_entry` (642-660) + `pending_camera_parent` (526-544) + `CameraHandoff::Position` (max residual 1.0 m). Dead with shipped values.
- **Rotation ease out of native6 (OFF, `rotation_ms=0`).** `ease_normal_rotation` (681-708) is called on every committed kind-4 frame but returns at its first line when `handoff_rotation_ms<=0` (682). Would need `CameraHandoff::Proof`: 12 consecutive frames where the forwarded quaternion (|w| <= .98) matches the camera world rotation within .25 degrees; turn limit 120 degrees. `track_native_rotation` (672-676) runs only when rotation_ms > 0.
- **Free aim (OFF, retired).** `FreeAim::State` (`free_aim_math.h:17-31`: 5 deg yaw / 3 deg pitch dead zone, .22 range while aiming, .12 s range ramp) is only reset in the hook (826,828,831); `apply` is never called. Activation refused (cpp:1163).
- **Kick (OFF, retired).** Fields `kick_*` are zeroed (831); no kick math remains. Activation refused (cpp:1199).
- **Camera motion (OFF, retired).** `policy.motion` is stored but never read.
- **Pin / look (REMOVED).** `pin_look_math.h`, `camera_transition.h`, `presentation_phase.h` are not compiled into this DLL; `NADSC_Config.reserved` must be 0 (cpp:1044).

Error codes (cpp:24-27): 0 OK, 1 INVALID_CONFIG, 2 BUSY, 3 READ_FAILED, 4 IMAGE_MISMATCH, 5 CODE_MISMATCH, 6 API_MISMATCH, 7 OWNER_MISMATCH, 8 UNIT_MISMATCH, 9 PIN_FAILED, 10 SLOT_NOT_WRITABLE, 11 CAS_MISMATCH, 12 CAS_FAULT, 13 CLEANUP_PENDING, 14 POSE_INVALID, 15 SPACE_INVALID, 16 GRAPH_MISMATCH, 17 TOKEN_MISMATCH, 18 ON_FOOT_REQUIRED. 14/15/16/18 are per-frame gates that leave the lease armed; owner loss (3 or 7) disarms.

Fixtures (not read in detail): `camera_fixture.cpp` drives the DLL against synthetic buffers (`NADSC_FIXTURE` seams: CAS failure, foreign slot value, RPM faults, mutations between proofs, lock ordering); `camera_correction_fixture.cpp` and `camera_handoff_fixture.cpp` are pure arithmetic tests of the two headers. `docs/first-person-camera-remake.md` reports 54 camera groups and 14 correction groups passed.

### Alignment helper: `native/native_ads_alignment_26568081_8e0af36233bd.dll` (exports `NADS_*`) - not loaded on the shipped path

**Sources.** `native/first_person/native_ads_alignment.cpp` (294 lines) + `native_ads_alignment.h` + `native_memory.h`. Build: `tools/build_first_person.py` pair `('alignment','fixture')` (zig c++ as above, single file); legacy MSVC `native/first_person/build.py:20` (`cl /nologo /c /MT /EHsc /std:c++17 /O2 /W4 /WX`, `link /DLL /DYNAMICBASE /NXCOMPAT … kernel32.lib`, output `native_ads_alignment_0_1_5.dll`). File-name suffix = first 12 hex of the DLL's own sha256 (checked). `README.md` describes this helper ("Owned native dispatch-pointer helper", version 0.1.5).

**Exports** (h:20-25): `uint32_t NADS_Begin(const NADS_Config* config)` (1 armed, 0 rejected, 2 owned cleanup required; acpp:237-274); `uint32_t NADS_Validate(const NADS_Config* config)` (read-only; acpp:226-236); `uint32_t NADS_End(void)` (acpp:275-282); `uint32_t NADS_ReadStats(NADS_Stats* stats)` (acpp:284-294); `uint32_t NADS_Error(void)` (acpp:283). Lua calls: none on the shipped path, none from the menu, none from the debug global (mode `unlinked_sights` has no starter). Structs: `NADS_Config` 72 bytes `{uint32 size,abi; uint64 game,exe,context,request,avatar_owner; uint32 avatar_index,avatar,weapon,body,viewmodel,duration_ms;}`; `NADS_Stats` 72 bytes `{uint32 size,abi,installed,armed,expired,error; uint64 seen_position,seen_rotation,skipped_position,skipped_rotation,forwarded_position,forwarded_rotation;}` (h:8-19). The Lua cdef (`alignment.lua:16-34`) matches, sizes asserted (35). Config bounds (acpp:220-225): `size==72`, `abi==1`, `1<=duration_ms<=10500`, `avatar_index<8`, avatar/weapon/body/viewmodel nonzero, viewmodel != 0xffffffff.

**Hooks: two function-pointer slots**, same technique as the camera helper.
- Location: `root=*(game+0x2ce14c8)`, `api=*(root+0x18)`, slots `api+0x38` (must hold `exe+0x1f25e0`, "POSITION") and `api+0x48` (must hold `exe+0x1f2740`, "ROTATION") (acpp:25,146-158).
- Verification (`contract`, acpp:143-161): same two image stamps/sizes; game+0x111a809 == `41ff5138`, game+0x111a829 == `41ff5148`; 48-byte prefixes at the two targets (acpp:27-29); slots writable image data of the exe; targets executable.
- Ownership at Begin (`ownership(entering=true)`, acpp:72-99): same context/owner/controller/descriptor chain as the camera helper; rig reference at `avatar_owner+0x53e9c0+index*0x1240` must equal `config.viewmodel` and resolve in the registry; `flags[6]&1==0`, `flags[14]&4==0`; all 6 controller bytes zero; top camera request kind 4 for this avatar; `config.request == ctx+0x200+tail*0xE8` (the slot the native request will be pushed into).
- Install: pin DLL and exe; store originals; CAS each slot original -> `position_hook` / `rotation_hook`; re-prove; then `deadline = GetTickCount64()+duration_ms`, `returns[0]=game+0x111a809+4`, `returns[1]=game+0x111a829+4`, `armed=1` (acpp:246-273).
- Replacement (`position_hook`/`rotation_hook(uint32_t unit,uint32_t node,const float* pose)`, acpp:179-188; `should_skip`, 168-178): count `seen`; **drop the call** (return without forwarding) only when armed, the return address is the expected game call site, `node==0`, `unit==config.viewmodel`, the deadline has not passed, `ownership(entering=false)` holds (top request == `config.request`, kind 6, weapon and body match, same camera/controller/descriptor/object as captured, controller bytes still all zero) and both slots still hold the callbacks; otherwise forward. Deadline passed -> `armed=0`, `expired=1`; ownership lost -> `armed=0`. Runs on whatever thread calls game+0x111a809 / +0x111a829 (not stated).
- Removal (`NADS_End` -> `restore_slots`, acpp:197-213,275-282): disarm, drain through `policy_lock`, CAS each slot back only if it still holds the callback; unreadable/non-writable slots keep the reservation and return error 13.

Fixture: `fixture.cpp` (synthetic images, descriptors, registry and call sites in its own process, per `README.md`).

### Editor/pose helper: `native/native_ads_editor_26568081_4940b83e17ad.dll` (exports `NADSE_*`)

**What is shipped.** `profile.lua:40` names `native/native_ads_editor_26568081_4940b83e17ad.dll`. Its SHA-256 is `4940b83e17addbd9521005119b985b6006e45d2ff5415ce76e66a86c7472bcc0` (`PORT-PROVENANCE.json:24`; I re-hashed the file and it matches), so the 12 hex digits in the file name are the first 12 of the DLL's own SHA-256. Its export table holds exactly the 23 `NADSE_*` names of `editor.h:43-90` (read with `objdump -p`), so it is built from `native_ads_editor.cpp`, not from `native_ads_pose.cpp`.

**Sources.** `editor.cpp` (1707 lines) + `editor.h` (90) + `native_memory.h` (13) + `phase.h` (30) + `gain.h` (144) which includes `zin.h` (108) + the assembly thunks `native_ads_input.S` (GNU syntax, 57 lines) or `native_ads_input.asm` (MASM twin, 48 lines). `zin.h`'s own `prepare()` (`zin.h:53-107`, the older "rewrite raw input" inverse) is not called by `editor.cpp`; only its helpers (`mul`, `add`, `sub`, `divide`, `narrow`, `filtered`, `neighbour`, `ordinary`, `bounded`, `bits`, `PreserveEnvironment`, `RAD_TO_DEG`) are used through `gain.h`.

**Build scripts (three exist; none was run by me).**
- `native/first_person/build_editor.py:11-29`: MSVC. `ml64 /nologo /c native_ads_input.asm`; `cl /nologo /c /MT /EHsc /std:c++17 /O2 /fp:precise /W4 /WX native_ads_editor.cpp`; `link /nologo /DLL /DYNAMICBASE /NXCOMPAT ... obj thunk kernel32.lib`. Output name is the fixed `native_ads_editor_0_1_43.dll`. Fixtures (`editor_fixture.cpp`, `input_fixture.cpp`, `input_fixture_thunks.asm`) need `--capture-dir` with build-26251122 captures (`build_editor.py:22-23,76`). The shipped DLL imports `api-ms-win-crt-*` DLLs, which a `/MT` MSVC build would not, so this script did not produce the shipped file.
- `tools/build_first_person.py:5-29`: zig 0.14.1 `zig c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -O2 -shared native_ads_editor.cpp -o native_ads_editor_26568081_<stamp>.dll`, where `<stamp>` = first 12 hex of SHA-256 over all `.cpp` and `.h` files in `native/first_person` (`:9`). It then copies the DLL to `native/` and rewrites `profile.lua` (`:24-29`). As written it does not pass `native_ads_input.S`, and its stamp rule does not give `4940b83e17ad` (that is the DLL hash), so the shipped file was not named by this script as it stands.
- The command recorded for the input-enabled candidate, `docs/first-person-zoom-input-validation.json:27`: `zig c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -O2 -shared native_ads_editor.cpp native_ads_input.S -o ...`, and that file's line 562 shows the same "name = first 12 hex of DLL SHA-256" rule (`native_ads_editor_26251122_cde915c48107.dll`). This is the only recorded command consistent with the shipped file (UCRT imports, thunks linked, hash-named). The exact command used for `4940b83e17ad` is not recorded in the tree.

**Lua cdef (`pose.lua:42-78`, `158-165`, `188-195`) versus the header.**
- `NADSE_Config` 104 bytes, `NADSE_Stats` 80 bytes, `NADSE_DepthStats` 104 bytes (ABI 1), `NADSE_InputStats` 96 bytes (ABI 1): layouts match `editor.h:4-35` and are size-asserted on both sides (`pose.lua:79,166,196`).
- Declared in Lua and present: all 18 exports listed LIVE/conditional/never in the table below.
- Not declared in Lua: `NADSE_RotationTrace49`, `NADSE_SetRotationGuard49`, `NADSE_EntryPositionStats50`, `NADSE_SetEntryPosition50`, `NADSE_SetEntryAnimation50` (`editor.h:79-90`). Their C defaults are therefore what runs.
- Stale text: `editor.h:60` says rotation "is limited to 35 degrees"; the code limit is w >= 0.5, i.e. 120 degrees (`editor.cpp:1534`, `pose.lua:291`). `build_editor.py:51` repeats the 35.
- `pose.lua:151-152` calls `depth_stats` "Optional console diagnostics only"; `runtime.lua:373-383` calls it from the runtime's presentation observation.

**Config fields (`editor.h:4-13`, filled by `backend.lua:839-857,1063-1067`).** `size=104, abi=2, game, exe, context` (camera context object), `request` (address of the camera-ring record this lease owns = `context+0x200+tail*0xE8`, `backend.lua:589`), `avatar_owner, avatar_index, avatar, weapon, body, viewmodel` (UnitRef of the first-person rig unit), `duration_ms` (0 = no deadline; 1..30500 bounded, `editor.cpp:1399`), `weapon_descriptor, weapon_unit, blend_ms` (1..1000), `offset_x/y/z` (each within 0.5), `reserved=0`. Validation: `copy_config`, `editor.cpp:1397-1405`.

**Exports.**

| Export (C signature) | What it does | Shipped path |
|---|---|---|
| `uint32_t NADSE_Validate(const NADSE_Config*)` (`editor.cpp:1406-1418`) | Read-only: config, `reserved==0`, `ownership(entering)`, `graph(capture)`, `contract`, then ownership and graph again. Returns 1/0. | CONDITIONAL: only if `normalize_zoom_input` were false (`pose.lua:217`). Reached through `NADSE_ValidateInput`. |
| `uint32_t NADSE_ValidateInput(const NADSE_Config*)` (`:1509`) | Plain alias of `NADSE_Validate`; does not check the input slot or input pins. | LIVE (`pose.lua:217-218`, from `backend.lua:1043`) |
| `uint32_t NADSE_Begin(const NADSE_Config*,uint64_t* generation)` (`:1507`) | `begin_pose(steady=false, input=false)`. | Never with shipped profile |
| `uint32_t NADSE_BeginSteady(const NADSE_Config*,uint64_t*)` (`:1508`) | `begin_pose(steady=true, input=false)`. | Never with shipped profile |
| `uint32_t NADSE_BeginInput(const NADSE_Config*,uint64_t*)` (`:1510`) | `begin_pose(steady=false, input=true)`: placement weight starts at 0 and blends in. | LIVE: re-entry after an ADS return (`controller.lua` manual update passes `blend_entry=true`; `backend.lua:855`; `pose.lua:234`) |
| `uint32_t NADSE_BeginSteadyInput(const NADSE_Config*,uint64_t*)` (`:1511`) | `begin_pose(steady=true, input=true)`: weight 1 at once, remembered entry position written, draw animation. | LIVE: F11 start and every other (re)acquisition, since `remaining_ms==0 and blend_entry~=true` (`backend.lua:855`) |
| `uint32_t NADSE_SetPlacement(uint64_t generation,float x,float y,float z)` (`:1560`) | Replaces `policy.c.offset_x/y/z` (limit 0.5 each). | LIVE (`backend.lua:899`, from `controller:set_placement`: placement editor drags and `motion_runtime.lua:359` action-placement blending) |
| `uint32_t NADSE_SetADSPlacement(uint64_t,float x,float y,float z)` (`:1563`) | Sets `policy.ads_placement` (limit 0.5), added unweighted only in full ADS. | LIVE (`motion_runtime.lua:326-328` once per lease/revision, since `backend.action_placement` is set at `runtime.lua:740`) |
| `uint32_t NADSE_SetMotion(uint64_t,float dx,float dy,float dz)` (`:1566`) | Sets `policy.motion` only (limit 0.15). | Never (no Lua caller) |
| `uint32_t NADSE_SetTransform(uint64_t,float dx,float dy,float dz,float qx,float qy,float qz,float qw)` (`:1569`) | Sets `policy.motion` and `policy.rotation` together, `authored_ads_rotation=false`. | LIVE (`pose.lua:295`, per kit update when the value changed) |
| `uint32_t NADSE_SetAuthoredTransform(uint64_t,float,float,float,float,float,float,float)` (`:1572`) | Same, with `authored_ads_rotation=true` (rotation also applies in full ADS). | LIVE (`pose.lua:295`, when `value.authored_ads==true`, set at `motion_runtime.lua:173,396`) |
| `uint32_t NADSE_SetZoomPolicy(uint64_t,uint32_t active)` (`:1575-1605`) | `zoom_policy_active`; when on, `depth_delta` returns 0 with reason 10. | Never (`zoom.lua` is not required by anything) |
| `uint32_t NADSE_SetTransition(uint64_t,uint64_t epoch,float progress,uint32_t ads)` (`:1512-1518`) | Publishes the shared presentation phase into `policy.shared` with the current tick. | LIVE (`backend.lua:970`, from `controller.lua` `publish_transition` and `backend.lua:1012-1016`) |
| `uint32_t NADSE_End(uint64_t generation)` (`:1606-1627`) | Disarm, restore cached native poses if still owned, abort a pending input index, restore the dispatch pointers. | LIVE (`pose.lua:329`) |
| `uint32_t NADSE_ReadStats(uint64_t,NADSE_Stats*)` (`:1629-1647`) | Health and counters; also performs timed expiry (not used with duration 0). | LIVE (`pose.lua:108`; several times per kit update) |
| `uint32_t NADSE_ReadDepthStats(uint64_t,NADSE_DepthStats*)` (`:1648-1659`) | Copy of `policy.depth.observed` plus refresh/check counts. | LIVE (`runtime.lua:378`) and debug API (`runtime.lua:1132-1142`) |
| `uint32_t NADSE_ReadInputStats(uint64_t,NADSE_InputStats*)` (`:1660-1673`) | Copy of `input_state.stats`. | Debug API only (`runtime.lua:1143-1153`) |
| `uint32_t NADSE_Error(void)` (`:1628`) | Last error code (enum at `:83-86`). | LIVE (`pose.lua:93`, on failures only) |
| `uint32_t NADSE_RotationTrace49(NADSE_RotationTraceData*)` (`:1677-1685`) | Copies the 64-row entry rotation trace. | Never |
| `uint32_t NADSE_SetRotationGuard49(uint32_t enabled)` (`:1686-1688`) | Switches the entry rotation guard. Default 1 (`:1144`). | Never (default stays on) |
| `uint32_t NADSE_EntryPositionStats50(NADSE_EntryPositionData*)` (`:1690-1697`) | Entry position counters. | Never |
| `uint32_t NADSE_SetEntryPosition50(uint32_t enabled)` (`:1698-1700`) | Switches the remembered entry position. Default 1 (`:1180`). | Never (default stays on) |
| `uint32_t NADSE_SetEntryAnimation50(uint32_t milliseconds,float drop,float back)` (`:1703-1707`) | Draw animation settings; limits ms <= 1000, drop and back 0..1. Defaults 220 ms, drop 0.35, back 0.06 (`:1018-1020`). | Never (defaults stay) |

Return convention: 1 = accepted, 0 = refused (`NADSE_Error` has the reason); `Begin*` additionally returns 2 = "generation owned, cleanup required" (`editor.cpp:1391-1396`).

**Hook mechanism (all hooks).** No game code bytes are written. The helper swaps function pointers in the engine's Unit API table: `root = *(game+0x2ce14c8)`, `api = *(root+0x18)` (`editor.cpp:313`). Slot addresses are `api+0x38`, `api+0x48`, `api+0x40`, `api+0x00` (`API_OFFSETS`, `:76`) and, with input normalisation, `api+0x90` (`:629`). Each slot must be 8-byte aligned, committed, `PAGE_READWRITE` or `PAGE_WRITECOPY`, inside the mapping whose allocation base is the exe (`writable_slot`, `:267-282`; Wine `MEM_MAPPED` accepted, `native_memory.h:4-13`). Install is `InterlockedCompareExchangePointer(slot, callback, verified_original)` (`cas`, `:289-306`; `:1452-1465`); removal is the reverse compare-exchange and leaves a foreign pointer alone (`restore_slots`, `:1350-1386`). The DLL and the exe (and `game.dll` when input is on) are pinned with `GET_MODULE_HANDLE_EX_FLAG_PIN` so cached pointers stay callable (`:1432-1435`).

| Slot | Engine function replaced (exe RVA, signature) | Replacement | Native call site that is acted on |
|---|---|---|---|
| `api+0x38` | `0x1f25e0`, `void(uint32_t unit,uint32_t node,const float* pos)`: set node local position | `position_hook` (`:1137-1141`) | return to `game+0x111a464` (call at `0x111a460`, "pose_branch_position_call") for node 5; return to `game+0x111a80d` (call at `0x111a809`, "root_position_call") for node 0 |
| `api+0x48` | `0x1f2740`, same signature: set node rotation | `rotation_hook` (`:1234-1247`) | none changed; only traces node 5 writes in the first 300 ms |
| `api+0x40` | `0x1f2660`, same signature: "branch rotation" / immediate local rotation | `branch_rotation_hook` (`:1337-1340`) | return to `game+0x111b11f` (call at `0x111b11b`) for node 5 |
| `api+0x00` | `0x1f2310`, `const float*(uint32_t unit,uint32_t node)`: local position getter | `NADSE_Input00Thunk` -> `NADSE_Input00Call` when input mode is on, otherwise `position_getter_hook` (`callback`, `:1345-1348`) | return to `game+0x1119a84` (call at `0x1119a81`, `41ffd0`) for unit = rig, node 50 |
| `api+0x90` (optional) | `0x1f30e0`, `const float*(uint32_t unit,uint32_t node)` ("world_pose" in `profile.lua:69`) | `NADSE_Input90Thunk` -> `NADSE_Input90Call` (`:809-873`) | return to `game+0x111863e` with R15 = the owned request |

**Verification before install (`contract`, `editor.cpp:307-329`; `Begin` also `:1426-1472`).**
- Images: `game.dll` TimeDateStamp `0x6af322c1`, SizeOfImage `0x3382000`; exe TimeDateStamp `0x6af2d5e6`, SizeOfImage `0x29fe000` (`:308`).
- Call-site bytes: `game+0x111a809` = `41ff5138`, `game+0x111a829` = `41ff5148`, `game+0x111a460` = `41ff5138`, `game+0x111b11b` = `41ff5140`, `game+0x1119a81` = `41ffd0` (`:309-311`).
- Each slot must currently hold `exe+WRAPPERS[i]` and that function must start with `PREFIX[i]` (`:78-82`, `:323-326`) and be on an executable image page:
  - `0x1f25e0`: `48895c24084889742410574883ec20498bf08bfa8bd9e8d5e0eaff4c8b5018b801000000f0410fc182380017008bc04c`
  - `0x1f2740`: `48895c24084889742410574883ec20498bf08bfa8bd9e875dfeaff4c8b5018b801000000f0410fc182380017008bc04c`
  - `0x1f2660`: `48895c24084889742410574881eca0000000440f29b42490000000498bd84863fae84ae0eaff488bc8488bf04c8b0841`
  - `0x1f2310`: `40534883ec204863dae8b2e3eaff488bc84c8b0041ff90e8000000488bcb48c1e1064883c124488b40204803c14883c4205bc3`
- Input slot (`input_sdk`, `:626-640`): `api+0x90` must hold `exe+0x1f30e0` starting with `40534883ec204863dae8e2d5eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3`. If this fails the pose lease still arms, without input normalisation (`:1430`, reason `INPUT_SDK_UNAVAILABLE`).
- Ownership (`ownership`, `:191-228`), entering form: `*(game+0x2e287d0)==context`; `camera=*context`; `*(game+0x2ce1ee0)==avatar_owner`; `controller=*(game+0x2e381a8)`, its bytes 0,1,2,3,5 must be 0; `count=u32(owner+0x6C)` in 1..8 and `avatar_index<count`; `descriptor=*(owner+0x110+index*8)`, 20 bytes, `u32@+8==avatar`, `u32@+12==body`; 24 flag bytes at `owner+0x53e888+index*0x1240`, `(flags[14]&4)==0`; `u32(owner+0x53e9c0+index*0x1240)==viewmodel`; both UnitRefs resolve through the unit registry (`unit_object`, `:183-190`: `registry=*(exe+0x1ab76f8)`, capacity `u32@+0x98`, generations `*+0xA0` byte per index, objects `*+0x88`; ref = index in bits 0..21, generation in bits 22..29); weapon descriptor 20 bytes with `u32@+8==weapon`, `u32@+12==weapon_unit`; ring `head=u32(context+0x1F8)`, `tail=u32(context+0x1FC)`, both < 32; top record = `context+0x1F00` when empty, else `context+0x200+((tail+31)%32)*0xE8`; top record `u32[1]==avatar`. Entering additionally requires top kind `(u32[0]&0xffff)==4`, `request==context+0x200+tail*0xE8`, and neither `native_aiming` nor `full_ads`. Derived state: `native_aiming=(flags[6]&1)!=0` ("bit 48"), `shoulder_aiming=(flags[10]&4)&&(flags[15]&32)` ("bits 82/125"), `full_ads=controller byte 4 != 0`. Running form (every hooked call): top record is exactly `request`, kind 6, `u32[2]==weapon`, `u32[3]==body`, and camera/controller/descriptor/unit objects and both 20-byte descriptor copies are unchanged.
- Rig graph (`graph`, `:232-258`): rig object vtable slot `+0xE8` must be `exe+0x28d3c0` whose bytes are `488d4160c3`; node count `u32(object+0x70)==63`; link array `*(object+0x98)`, hash array `*(object+0xA0)`; these 15 rows must match (node, name hash, link word):
  `0:4A182741:0`, `1:7F30E61C:1`, `2:32623EB2:0x10001`, `3:975CEBBD:0x20001`, `4:BCCF91E5:0x30001`, `5:9B115563:0x40001`, `7:6F3BD021:0x50001`, `8:4FDD3337:0x70001`, `10:03752C98:0x70001`, `12:1CA2CD66:0xA0001`, `13:5C0C68D3:0xC0001`, `30:1620B2CE:0x70001`, `32:31E34DAC:0x1E0001`, `34:248004F5:0x200001`, `50:60ED8C39:0x40001`. The link word's high 16 bits are the parent index: node 5 and node 50 (camera) both have parent 4; 7 has parent 5; 8 (weapon attachment), 10, 30 have parent 7; 13 (l_hand) is under 12 under 10; 34 (r_hand) is under 32 under 30.
- "All 16 code pins" (`profile.lua:43`) are the input-normalisation fingerprints `INPUT_PINS` (`editor.cpp:585-601`): FNV-1a 64 (offset basis `0xcbf29ce484222325`, prime `0x100000001b3`, `:604-611`) over whole byte ranges, checked lazily on the first eligible callback and retried every 250 ms until they pass (`input_check_code`, `:619-625`). The source holds only hashes, not bytes:

  | # | module | RVA | length | FNV-1a 64 |
  |---|---|---|---|---|
  | 1 | game | 0x1118370 | 9688 | 0xac98356760db02c9 |
  | 2 | game | 0x111a948 | 24 | 0xded70c3fd0eba95c |
  | 3 | game | 0x1114950 | 1724 | 0x3690803fb2e8bced |
  | 4 | game | 0x1f8ff70 | 1013 | 0xe4a380a8807b6ec2 |
  | 5 | game | 0x1109110 | 1328 | 0xabb508e82158f3f1 |
  | 6 | game | 0x1109640 | 0x1a0 | 0xf17ad9ae58e57ab0 |
  | 7 | game | 0x110b830 | 0x5b0 | 0xd77b549041450f76 |
  | 8 | game | 0x111f860 | 0x124 | 0x23ae2b71a5e0f871 |
  | 9 | game | 0xe69ca0 | 0xdc | 0x91b780e54d5bb826 |
  | 10 | game | 0x110fcf0 | 0xbb0 | 0x32dc74e3dc6a26e9 |
  | 11 | game | 0x11108a0 | 0x1dd0 | 0x1a26ca7b7bbba077 |
  | 12 | exe | 0x1f3050 | 51 | 0xdafbc85ae8dffa23 |
  | 13 | exe | 0x1f3090 | 76 | 0x9f25c14a58784f1f |
  | 14 | exe | 0x1f30e0 | 47 | 0xd34dcd5634fed114 |
  | 15 | exe | 0x1f2310 | 51 | 0x542e94a63b7ebae6 |
  | 16 | exe | 0x28d3c0 | 5 | 0x86a7f411c046a30c |

  Plus constants in `game.dll` data (`:613-617`): `0x22465c4`=`0x3f800000` (1.0f), `0x2247894`=`0xbf800000` (-1.0f), `0x2246fb0`=`0x42652ee0` (57.2957763671875f), `0x22478c8`=`0xbfc90fdb` (-pi/2), `0x2246774`=`0x3fc90fdb` (+pi/2), `0x2247ab0`=`0xc2960000` (-75.0f), and the double at `0x2246900`=`0x3ff0000000000000` (1.0).
- Depth compensation pins (`depth_code`, `:446-458`, checked once per lease on first use): `game+0x5120b2`=`4869c0d8040000490383b00000004883c428c3`; `0x511b3f`=`4c8b90a046f10048b80fdf6f49a121b3af48f7e148c1ea09`; `0x511b57`=`69c2ea020000442bc0`; `0x511bb1`=`4869c1d80400004805a02e00004903c2c3`; `0x11193f3`=`f30f1044245cf30f10159f241301`; `0x1119419`=`f30f5c4424600f54c2f30f5ec1`; `0x1119ad1`=`f3450f58fb`; `0x1119b83`=`f30f59faf3410f59cff3`; `0x1119c44`=`f3410f11774cf3450f114750`.
- Carrier (node 0) pins (`:973-978`, once per lease): `game+0x1118375`=`5556574154415541564157488dac24f0fcffff4881ec10040000`; `0x111a7cb`=`4c8d8540010000f2410f11475433`; `0x111a814`=`4c8d8530010000`.

**Arming (`begin_pose`, `editor.cpp:1419-1506`).** Under `control_lock`: zero the caller's generation, validate, refuse if another lease is reserved (`BUSY`), verify, pin modules, reset input state, store originals, issue `generation_counter+1` (never 0) and write it to the caller before any pointer changes, mark reserved, compare-exchange the four slots (then `api+0x90` if input), re-check ownership, graph and that every slot still holds our callback. Then under `policy_lock`: copy the policy; for steady leases set `weight=start_weight=target=1`; if steady and entry position is enabled and a remembered pose exists for (viewmodel, weapon), call the original position setter for node 5 with that pose, lowered by the full draw-animation offset (`pose[1]-=back; pose[2]-=drop`) when the animation is on (`:1484-1493`); set `deadline = duration_ms ? tick+duration_ms : 0`, `blend_start = armed_at = tick`; zero counters; publish the five return addresses and `watched_viewmodel`; `armed=1`. Time source everywhere is `GetTickCount64()` (`:265`).

**Hook 1: node 5 position (`position_call`, `editor.cpp:1031-1136`).** This is the pose offset.
1. Fast paths with no lock and no reads: any call that is not (armed and return address `game+0x111a464`) is forwarded unchanged, except node 0 of the watched rig or a return to `game+0x111a80d`, which go to the carrier path (hook 3).
2. Under `policy_lock`: unit must be `viewmodel` and node must be 5 (`RIG_NODE`), else forward. The source pointer must be exactly `request+0x48`; another pointer (a fading ring record using the same call site) is forwarded and only clears `rig_shifted` (`:1050-1055`).
3. Guards: `ownership`, `graph`, `dispatch_owned` (all slots still ours), `u32(request+0x18) != 0` (sight node), 16 bytes readable at `pose` with finite xyz. Any failure, or `deadline!=0 && now>=deadline`, disarms (`armed=0`, error code or `expired=1`) and forwards the game's value (`:1056-1072`).
4. Weight (`:1075-1084`): `target = full_ads ? 0 : 1`. If a shared phase is current (`policy.shared.matches(full_ads, now)`: epoch non-zero, same ADS flag, published at most 250 ms ago, `phase.h:20`): `weight += (target-weight)*shared.step(full_ads)`, where `step` spends the smoothstep `p*p*(3-2p)` of the published progress incrementally: `amount = progress>=1 ? 1 : clamp((eased-used)/max(0.000001,1-used),0,1)` (`phase.h:21-27`). Otherwise the helper's own timer: `u=(now-blend_start)/blend_ms`, `t=clamp(u,0,1)`, `weight = start_weight+(target-start_weight)*t*t*(3-2t)`; when `target` changes, `start_weight=weight`, `blend_start=now`. `blend_ms` = 150 (`profile.lua:46`). Non-steady leases start at weight 0, steady at 1.
5. Position, in node 5's parent space (parent = node 4), on a 16-byte copy; the 4th float is carried untouched (`:1085-1092`):
   `shifted.xyz = native.xyz + (offset.xyz + motion.xyz) * weight`; in full ADS additionally `shifted.xyz += ads_placement.xyz` (unweighted; with weight 0 this is the only term).
6. Depth term (`:1095-1113`): `depth = depth_delta(...)` (below); if non-zero, `shifted.y += depth`. If the depth step re-read weapon data this call, ownership/graph/dispatch are checked again.
7. `policy.baseline = native` (for restore); if a depth term was applied, arm the paired node 0 correction (`policy.carrier`).
8. Entry position memory (`:1121-1123`): the first shifted pose of a lease with `weight==1` outside full ADS is stored for (viewmodel, weapon) in a 16-entry ring (`entry_positions`, `:1175-1191`).
9. Draw animation (`:1124-1131`): steady lease and not full ADS: `remaining = (1-t)^3` with `t=(now-armed_at)/220 ms` (0 once `t>=1`); `shifted.y -= 0.06*remaining; shifted.z -= 0.35*remaining`.
10. If anything changed, the original setter is called with the copy (`weapon_updates++`), else with the game's pointer. `publish_diagnostics` stores weight/target for `ReadStats`.

**Hook 2: node 5 rotation (`branch_rotation_call`, `editor.cpp:1258-1336`).**
1. Unlocked pass-through unless armed, unit is the watched rig and node is 5.
2. Under `policy_lock`, generation re-checked. A different caller than `game+0x111b11f` is forwarded and clears `rotation_shifted`.
3. Entry rotation guard (`entry_rotation`, `:1202-1233`; constants `:1165-1168`): within 300 ms (`ENTRY_GUARD_MS`) of arming, if a settled reference rotation is known for this rig unit, the guard has not yet released in this lease, the guard is enabled, `|dot(reference, q)| < 0.99991` (more than 1.5 degrees away), fewer than 2 consecutive "still" writes (`|dot(previous,q)| >= 0.999998`, 0.25 degrees), and ownership/graph/dispatch hold, then the reference quaternion is forwarded instead of the game's (`guarded++`). The first write that is not held releases the guard for the rest of the lease. Every write forwarded as given feeds a candidate: 8 consecutive writes (`ENTRY_GUARD_STABLE`) within `|dot| >= 0.99999` (`ENTRY_GUARD_SAME`) make it the new reference; the reference is global and survives across leases on the same rig unit. Up to 64 writes in the window are traced (`entry_trace`, `:1192-1200`).
4. If `policy.rotation` is identity and nothing is cached, forward (no memory walks).
5. Guards: ownership, graph, dispatch; failure disarms. Deadline expiry restores and disarms.
6. Rotation is applied only when: this request is the sole ring entry (`sole_request`, `:342-346`: `(tail+32-head)%32==1` and `request==context+0x200+head*0xE8`), `f32(request+0x98)==0.0` ("sight coupling", `:347-350`), and not full ADS unless the transform was sent as authored (`:1314`). Otherwise forward.
7. Math (`:1323-1328`): `shifted = policy.rotation * native` (Hamilton product, our parent-local delta on the left, the game's fresh local rotation on the right; quaternions are x,y,z,w). `native` must have |norm-1| <= 0.02, the result <= 0.025, else disarm with `POSE_INVALID`. `rotation_baseline = native`.

**Hook 3: rig root node 0 position, "carrier" (`carrier_position`, `editor.cpp:957-1011`).** Pairs with the depth term so the weapon does not slide when the game moves the whole rig for scope zoom. Acts only when armed, return address `game+0x111a80d`, unit = rig, node 0, a pending capture exists from the node 5 call of the same evaluation (same caller stack value, request, generation, thread), and the source pointer is exactly `caller_stack+0x240`. Requires the three carrier pins, `sole_request`, ownership/graph/dispatch (failure disarms), not full ADS, and the request bytes `+0x80..+0xA3` unchanged since the observation except `+0x88..+0x8B`. Reads 32 bytes at `pose-0x10`: quaternion q (16 bytes) then position (16 bytes). `forward = (2(xy-zw), w^2+y^2-x^2-z^2, 2(yz+xw))` (rotated +Y) and `shifted.xyz = position.xyz - delta*forward` where `delta` is the depth actually added to node 5's Y (`:1004-1006`). Remembers `carrier_baseline`, `carrier_written`, and the pose-array pointer `*(object+0x80)` for restore.

**Hook 4: local position getter, observer (`position_getter_call`, `editor.cpp:879-906`).** Forwards the original in every case and returns its pointer unchanged. Only for return address `game+0x1119a84`: under `policy_lock` it first completes a pending input swap (`input_complete`, below), then, if armed and `unit==viewmodel && node==50` (the camera node), records the observation used by the depth term: 0x20 bytes at `caller_stack+0x44` and 0x24 bytes at `request+0x80`, plus caller stack value, thread id, generation.

**Depth/zoom compensation (`depth_delta`, `editor.cpp:480-554`; data `:357-445`; reference `:459-471`).** Purpose (file header `:1`, `build_editor.py:49`): with a multi-zoom scope the game places the rig at a distance that depends on the selected zoom; the helper replaces that by the distance for the widest (lowest magnification) authored zoom so the weapon sits the same at every zoom level outside full ADS.
- Returns 0 (no change) with a reason code: 10 zoom policy active (never on the shipped path); 1 full ADS (`ads_bypassed++`); 3 no matching observation; 2 depth pins failed; 4 request mode bytes unsupported (`request+0x8D > 1`, `request+0x8F != 0`, stack bytes `+0x44 != 0` or `+0x45 > 1`, `f32(request+0x98) != 0`); 5 value range; 6 weapon data unavailable; 7 previous reference unknown mid-transition; 8 `|delta| > 1`; 9 data changed.
- Inputs: `current=f32(request+0x90)`, `previous=f32(request+0x9C)`, `prior=f32(request+0xA0)`, `mix=f32(request+0x88)`, `target=f32(stack+0x5C)`, `lens=f32(stack+0x60)`; requires `u32(request+0x84)==u32(stack+0x60)`; target and lens in 0.0002..2.96, prior in 0..2.96, |current| and |previous| <= 4.
- Transition fraction: `span=|target-prior|`; `t = span>1.1920928955078125e-7 ? min(1,|target-lens|/span) : 0`.
- Weapon data is re-read when the triple (`request+0x90`, `+0x9C`, `+0xA0`) changes, or every 500 ms after a failure; while valid it is re-validated every 500 ms (`:516-542`). Lookup (`depth_data`): `pool=*(game+0x2ce1ea0)`, `world=*(game+0x2ce41b8)`, `settings=*(game+0x2ce1508)`; 0x88 bytes at `pool+0x30` hold two open-addressed maps keyed by the weapon entity id (header: buckets u64, capacity u32, empty key u32, multiplier u32; index = `(key*mult)&(cap-1)`, linear probe, at most 64 rows; a row is `entity | index<<32`); primary map at `+0`, override map at `+0x40`; descriptor pointer array at header `+0x18` must give `weapon_descriptor` at the found index (< 4096); if the override map has the weapon, record = `*(header+0x80) + di*0x4D8`, else the weapon resource id (first 8 bytes of the weapon descriptor) is looked up in a 746-bucket table at `*(world+0xf146a0)` (16-byte rows, bucket = `resource % 746`) and record = `table+0x2EA0+di*0x4D8`. From the record: `zoom[3]` = floats at `+0x74`, `coefficient[3]` at `+0x74+0xC8`, `near_term[2]` at `+0x74+0xD4`, `+0x74+0xD8`. Selected zoom index = `(u32(packed)>>4)&3` with `packed = *(header+0x30) + wi*12 + 4`, must be 0..2; the 16 bytes at `*(header+0x28)+wi*0x3F0+0x350` must contain exactly one u32 equal to 1. `degrees = f32(settings+0xac488)` (10..170). Ranges: zoom 0.1..100, coefficient 0.0001..100, |near_term| <= 1.
- Reference distance (`depth_reference`): `rad=0.01745329238474369`; `widest` = index of the smallest zoom value; `angle=((degrees/zoom[selected])*rad)*0.5`, `base=((degrees/zoom[widest])*rad)*0.5`, both in 0.0001..1.48; if `selected==widest`, `reference=distance`; else `u=(degrees-45)/45`, `base_term=(1-u)*near_term[0]+u*near_term[1]`, `reference = base_term + (distance-base_term) * (tan(angle)*coefficient[selected]) / (tan(base)*coefficient[widest])`, |reference| <= 4.
- Result: `native=(1-t)*current+t*previous`; `reference=(1-t)*current_reference + t*previous_reference`; `gain = request[+0x8D] ? mix : 1`; `delta=(reference-native)*gain`, applied to node 5's Y and removed from node 0 along the rig's forward axis.
- `NADSE_ReadDepthStats` reports `zoom_index` (selected index 0..2), `zoom`, `supported`, `reason`, `native_distance`, `reference_distance`, `correction`, `target_fov`, `current_fov`, `gain`, and counters. The runtime uses `zoom_index` as the "zoom identity" of the weapon (`runtime.lua:371-383`).

**Zoom look-input normalisation (`NADSE_Input90Call`, `editor.cpp:809-873`; thunks `native_ads_input.S:8-19,46-56`; math `gain.h:67-143`).** Enabled by `profile.pose.normalize_zoom_input=true` (`profile.lua:44`). Intent (`profile.lua:41-43`, `docs/first-person-zoom-input.md`): with a scoped weapon the held aim request scales look input by the sight's zoom even outside the sights; the helper removes that scaling for hip and normal aim and stands down in full ADS.
- Thunks: on entry R15 is the native request; the thunk passes `r8=R15`, `r9=entry RSP+8` (the caller's stack value), and the return address as the 5th argument, then calls the C function. `NADSE_InputCompareExchange` is a `lock cmpxchg` on a 32-bit field with its own SEH handler (`NADSE_InputCASException`, `editor.cpp:31-44`) that turns an access violation on that one instruction into a "failed" return.
- `NADSE_Input90Call` always calls the original first and returns its pointer unchanged. It acts only when the return address is `game+0x111863e` and R15 equals the leased request. Then, under `policy_lock`: lazy pin check (16 pins above); deadline and request check; `ownership(false)`, and if `full_ads || native_aiming` it does nothing (reason `INPUT_ADS`=12, comment `:834-836`: normalising in the sights left up/down aim dead).
- Sample (`input_sample`, `:674-701`): `u64(stack+0x80)==context`; `u64(stack+0x48)==avatar_index`; `dt_eval=f32(stack+0xa0)`; byte `context+0x1a9` must be 0; `raw[3]` at `avatar_owner+0xa7c24+index*0xa7aec`; `history[3]` at `request+0x3c`; preset index `u32(request+0x80)` (must be <= 255); `lens_radians=f32(request+0x84)`; pitch gate = byte `request+0x8c`; `input_owner=*(game+0x2e38250)`, device `u32(input_owner+0x324)` (1 or 2 = gamepad), 0x5c flag bytes at `input_owner+0xa7af8`; `tpv_degrees=f32(*(game+0x2ce1508)+0xac488)`; `dt_global=f32(*(game+0x2ce14f8))`; `context[2]` floats at `context+0x15c`; steering (`input_steering`, `:653-668`) from `avatar_owner+0x53e9ac+index*0x1240` after resolving the controlled avatar through `*(game+0x2ce1618)` (+0x88 count, +0x3a8 entity), `game+0x2e3fcb8`, `game+0x2e3fa9c`, `game+0x2e3f0e4`, and the world map at `world+0xf24990` / rows at `world+0xf349e8+row*24`. Native preset = 144 bytes at `game+0x2cb2140+index*144`; native gains = two floats at preset `+4` (yaw) and `+8` (pitch). Inversion sign per axis = product of two flag bytes (gamepad: flags[0+axis], flags[4+axis]; mouse: flags[2+axis], flags[8+axis]); device sensitivity = float at flags `+0x4c` (gamepad) or `+0x58` (mouse).
- Math (`ZoomGainMath::prepare`): requires MXCSR default rounding and all exceptions masked. `ratio R = (lens_radians*57.2957763671875)/tpv_degrees`, accepted in 1/4096..16. Filter coefficient `a = 1 - exp(dt_eval*-75)` using the game's own `exp` at `game+0x1f8ff70` (`a=1` when `dt_eval<=0`), `b=1-a`; predicted history `h_i = a*raw_i + b*history_i`. Per axis (0 yaw, 1 pitch): `inverted = sign*h`, `zoomed = device*(R*inverted)`, `unzoomed = device*inverted`, `X = zoomed+steering`, `T = unzoomed+steering`. With zero steering the new gain is `g' = g/R` (one candidate, independent of input). With non-zero steering `g' = g*T/X` (narrowed from double), tried with its two neighbouring floats, choosing the smallest error of the simulated native pipeline; refused when `X==0` or `T==0` (singular), when `|X|` or `|T|` is below 1/4096 of its magnitude scale (ill-conditioned), when `g'` is outside 0.000001..1000000 or `g'/g` outside 1/4096..4096, when the pitch gate outcome differs, or when the increment error exceeds `min(0.000002, 0.0000002+0.000002*|wanted|)`. Simulated pipeline (`gain.h:41-60`): if `dt_eval>0`, `value += context/(dt_eval*gain)`; pitch gate zeroes a positive pitch value; `increment = (axis==0 ? -value : value)*gain*dt_global`, yaw additionally `*0.5`.
- Publication: a private copy of the 144-byte preset with only bytes `+4..+11` (the two gains) replaced is placed in memory allocated above the preset table at a 64 KiB-aligned free region, at an address that is exactly `table + k*144` so that `k` is a valid 32-bit preset index (`input_clone`, `:714-749`; up to 32 clones, 0x10000 bytes each). After re-sampling and proving nothing changed, `u32(request+0x80)` is compare-exchanged from the native index to `k` (`:867`). This is the only engine datum written (`:555-556`).
- Completion: the next local-position-getter call from `game+0x1119a84` with the same R15, stack value, thread and lease generation (`input_complete`, `:793-808`) compare-exchanges `request+0x80` back to the native index, then compares `request+0x3c` history with the prediction; a mismatch with non-zero steering switches the feature off for the rest of the lease (`failed_closed`). A re-entry before completion aborts and quarantines the clone (`:821-827`).
- Stats: `ratio`, `yaw_gain`, `pitch_gain`, `observed`, `normalized`, `unsupported`, `restored`, `foreign_wins`, `quarantined`, `reason` (enum `:557-559`; values `0x100+n` carry a `ZoomGainMath::Reason`).

**ADS / hip / aim phase logic.** There is no state machine in the DLL; each node 5 call re-derives `full_ads` (controller byte 4), `native_aiming` (flag bit 48) and `shoulder_aiming` (flag bits 82 and 125; computed but not read by any branch in this file) from memory. Hip and shoulder aim both use weight 1; full ADS uses weight 0 (`:1075`). The blend between them follows the Lua-published shared phase when one is current (same ADS flag, at most 250 ms old), else the helper's own 150 ms smoothstep. Entering a lease is refused while `native_aiming` or `full_ads` (`:216`).

**Threads and timing as far as the code establishes.** Every hook body runs synchronously on whichever game thread calls the Unit API function; the helper creates no thread and never calls an engine setter on its own except (a) the remembered entry position write inside `Begin*` and (b) baseline restores inside `End`, `Set*` failure paths and `ReadStats` expiry, all of which run on the Lua caller's thread. The acted-on call sites are all inside or next to the game's first-person camera evaluator at `game+0x1118370` (pin 1; carrier pin `0x1118375`), which the comments call the "native postpass" that "visits every kind-6 ring entry" (`:340-341`). Order within one evaluation, from the pairing logic: `api+0x90` call at `0x111863e` (input swap) -> getter at `0x1119a81` for node 50 (input restore + depth observation) -> node 5 position at `0x111a460` -> node 0 position at `0x111a809` (-> node 0 rotation at `0x111a829`, forwarded) -> node 5 rotation at `0x111b11b` (ascending RVAs; the code only proves getter-before-node5-before-node0). Whether this evaluator runs before or after animation evaluation and rendering is not stated in these sources beyond: the game "writes node 5 every frame" (`:1145-1151`), and "the frame on which a lease is armed renders before the game writes node 5 for the new weapon" (`:1171-1174`), i.e. Lua `Begin` runs before that frame's evaluator call. Lua-side comment: arm "before native entry, including its immediate dt=0 evaluation" (`backend.lua:627`).

**Removal (`NADSE_End`, `editor.cpp:1606-1627`).** `armed=0`, `active_generation=0`; under `policy_lock` `restore_rig_if_owned` (`:918-947`): if ownership, graph and dispatch still hold, call the original position setter with the cached native node 5 position; if also sole request, the original branch-rotation setter with the cached native rotation; and the original position setter for node 0 with the cached native root position only if the rig's pose array pointer `*(object+0x80)` is unchanged and the three floats at `poses+0x24` still equal what we wrote. Abort a pending input index. Reset motion to 0 and rotation to identity. Then `restore_slots`: refuses while an input swap is pending (returns `CLEANUP_PENDING`=13, Lua keeps the handle and retries), else compare-exchanges each slot from our callback back to the original; on full success the generation is retired and the reservation released. A second `End` with the retired generation returns 1.

### Older helper: `native_ads_pose` (exports `NADSP_*`), not shipped and not loaded

`native/first_person/native_ads_pose.cpp` (404 lines) + `native_ads_pose.h` (25), built only by `native/first_person/build_pose.py:10-20` (MSVC `cl /MT /EHsc /std:c++17 /O2 /W4 /WX`, output `native_ads_pose_0_1_7.dll`, fixture `pose_fixture.cpp`). `tools/build_first_person.py:6` does not list it, no `native_ads_pose*.dll` exists in `native/`, and no Lua file declares `NADSP_*`. It is the ancestor of the editor helper: ABI 1, 96-byte config with only `offset_x` and `offset_z` (`native_ads_pose.h:4-11`), two slots (`api+0x38`, `api+0x48`), the same node 5 position call site `0x111a460`, same image stamps, same graph rows, a mandatory deadline of 1..10500 ms (`pose.cpp:322`), 150 ms-style smoothstep with target 0 when `flags[6]&1` or controller byte 4 (`pose.cpp:102,263-269`), no generation handle, no rotation, no depth, no input, no steady entry. Exports: `NADSP_Validate`, `NADSP_Begin(const NADSP_Config*)`, `NADSP_End(void)`, `NADSP_ReadStats(NADSP_Stats*)`, `NADSP_Error(void)` (`native_ads_pose.h:20-24`); all "never" on the shipped path. Its constants were updated to 26568081 values but `POSE-README.md:7` still quotes the old-build site `0x1288424`. The only thing "pose" in the live mode is the Lua adapter's name and the `kind='pose'` lease label.

Common design of the fov / blur / near / heldfire helpers (all four): no code patching and no `VirtualProtect`. Each swaps exactly one function pointer in a writable data table of `helldivers2.exe` (an engine "API" table the game DLL calls through) with `InterlockedCompareExchangePointer` under SEH, after checking both PE images (`game.dll` TimeDateStamp `0x6af322c1`, SizeOfImage `0x3382000`; `helldivers2.exe` TimeDateStamp `0x6af2d5e6`, SizeOfImage `0x29fe000`) and exact byte strings at the call site and at the original setter. The slot must be 8-aligned, committed, `PAGE_READWRITE` or `PAGE_WRITECOPY`, in a PE section mapping (`MEM_IMAGE`, or `MEM_MAPPED` when ntdll exports `wine_get_version`) whose AllocationBase is the exe (`native/first_person/native_memory.h:4-12`). Both the helper DLL and the exe are pinned with `GetModuleHandleExW(...FROM_ADDRESS|PIN)`. A "generation" token (uint64, never 0) is written into the caller's buffer with `WriteProcessMemory`; Begin returns 0 = refused/no lease, 1 = armed, 2 = token owned but cleanup needed. End disarms, drains in-flight callbacks by taking and releasing `policy_lock` exclusively, then CAS-restores the slot only if it still holds the helper's callback (a foreign replacement is left alone). All game memory reads go through `ReadProcessMemory(GetCurrentProcess(),…)` with address bounds `[65536, 0x800000000000)`.

Owner check shared by fov, blur and zoom (`native_ads_fov.cpp:265-293`, `native_ads_blur.cpp:133-159`, `native_ads_zoom.cpp:71-99`), performed at Begin (capture) and on every hook call (compare):
- `ctx=[game+0x2e287d0]` must equal config.context; `[ctx+0]` must equal config.camera.
- `avatar_owner=[game+0x2ce1ee0]` must equal config.avatar_owner; `u32 [avatar_owner+0x6C]` = count, 1..8, avatar_index < count.
- `controller=[game+0x2e381a8]`; 6 bytes `controls` read at it; `mask`=u32 of bytes 0..3.
- `descriptor=[avatar_owner+0x110+avatar_index*8]`; 20 bytes; u32 at +8 = avatar id, u32 at +12 = body unit id; both must equal the config.
- 24 flag bytes at `avatar_owner+0x53e888+avatar_index*0x1240`.
- unit registry `registry=[exe+0x1ab76f8]`; `u32 [registry+0x98]` capacity (1..0x400000); `generations=[registry+0xA0]`; `units=[registry+0x88]`; unit index = `body&0x3fffff`, generation byte = `body>>22`; `[generations+index]` must equal the generation; `[units+index*8]` = body object; both re-read once to confirm.
- Derived: `full_ads = controls[4]!=0`; `shoulder_aim = !full_ads && (flags[10]&4) && (flags[15]&32)`; `safe = (mask==0 || mask==2) && controls[5]==0 && (flags[14]&4)==0`.
- A read failure of owner data yields error 3 (retryable) only when the failed span is still mapped readable; otherwise error 7 (`native_ads_fov.cpp:296-300`).

Camera request selection shared by all (`native_ads_fov.cpp:304-309`): `head=u32 [ctx+0x1F8]`, `tail=u32 [ctx+0x1FC]` (both < 32); if `head!=tail` the current request is the ring row `ctx+0x200+((tail+31)%32)*0xE8` ("queued"), otherwise the base record `ctx+0x1F00`. Row layout used: u32[0] low 16 bits = camera kind (4 = normal third-person, 6 = weapon first-person/aim camera), u32[1] = avatar id, u32[2] = weapon id, u32[3] = body id.

### fov: `native/native_ads_fov_26568081_764664986628.dll`

Sources: `native/first_person/native_ads_fov.cpp` (748 lines), `native_ads_fov.h` (35), `native_memory.h` (13); fixture `fov_fixture.cpp` (synthetic camera/owner/slot tests).

Build: the shipped name comes from `tools/build_first_person.py` (zig 0.14.1 `zig c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -O2 -shared native_ads_fov.cpp`, single translation unit, no extra link inputs; build_first_person.py:11-13). File name = `native_ads_<name>_26568081_<stamp>.dll` where stamp = first 12 hex of SHA-256 over the concatenated bytes of every `.cpp`/`.h` in `native/first_person/` sorted by name (build_first_person.py:9-10), so it is a folder source hash, not a DLL hash; the script copies the DLL into `native/` and rewrites the name in `profile.lua` (build_first_person.py:24-29). Fixture exe is run under wine. `native/first_person/build_fov.py` is an older MSVC builder (`cl /MT /EHsc /std:c++17 /O2 /W4 /WX`, `link /DLL /DYNAMICBASE /NXCOMPAT … kernel32.lib`, output `native_ads_fov_0_1_23.dll`; build_fov.py:10, 17-18) that does not produce the shipped name; its metadata text documents the mechanism (build_fov.py:32-37).

Exports (`native_ads_fov.h:18-35`):
- `uint32_t NADSF_Validate(const NADSF_Config*)`: read-only preflight; owner + safe + request (kind 4 allowed) + binary contract + readable FOV source; no hook. LIVE (`fov:preflight`, fov.lua:102; runtime.lua:1192).
- `uint32_t NADSF_Begin(const NADSF_Config*, uint64_t* generation)`: install the slot hook for hip/shoulder policy, apply once immediately, arm. LIVE (fov.lua:110).
- `uint32_t NADSF_Bind(uint64_t generation, uint64_t request, uint32_t weapon)`: rebind the armed policy to a new camera request row/weapon without re-hooking; applies immediately. LIVE (fov.lua:123).
- `uint32_t NADSF_ValidateFird(const NADSF_Config*)`, `uint32_t NADSF_BeginFird(const NADSF_Config*, float captured_radians, uint64_t* generation)`, `uint32_t NADSF_BindFird(uint64_t generation, uint64_t request)`: "Fird Person" policy that holds a captured lens on the normal kind-4 camera and CAS-rewrites the request's lens field at +0x3C. Never on the shipped path (only `fird_fov.lua`, created inside `start_fird`).
- `uint32_t NADSF_SetTransition(uint64_t generation, uint64_t epoch, float progress, uint32_t ads)`: publish the Lua coordinator's shared phase (epoch ≥ 1, progress 0..1 non-decreasing within an epoch, ads 0/1). LIVE (fov.lua:138).
- `uint32_t NADSF_End(uint64_t generation)`: disarm, restore Fird request field if any, restore slot; idempotent for the retired token. LIVE (fov.lua:152).
- `uint32_t NADSF_ReadStats(uint64_t generation, NADSF_Stats*)`: counters + last native/applied radians + source degrees; generation 0 valid only with no lease. LIVE (fov.lua:71).
- `uint32_t NADSF_Error(void)`: last error code. LIVE (fov.lua:49).

Structs: `NADSF_Config` 80 bytes `{u32 size,abi; u64 game,exe,context,camera,avatar_owner,request; u32 avatar_index,avatar,body,weapon; u64 reserved}`; `reserved` = 0 (native ADS passthrough) or 80..500 = owned ADS handoff milliseconds (`native_ads_fov.h:4-9`, validated `native_ads_fov.cpp:607`). `NADSF_Stats` 88 bytes (`native_ads_fov.h:10-14`). Lua cdef (fov.lua:18-35) matches byte for byte; it omits the three `*Fird` exports. Lua sets `c.reserved = profile.ads_blend_ms or 0` = 180 (fov.lua:55, profile.lua:57).

Error codes (`native_ads_fov.cpp:23-26`): 0 OK, 1 INVALID_CONFIG, 2 BUSY, 3 READ_FAILED, 4 IMAGE_MISMATCH, 5 CODE_MISMATCH, 6 API_MISMATCH, 7 OWNER_MISMATCH, 8 UNIT_MISMATCH, 9 PIN_FAILED, 10 SLOT_NOT_WRITABLE, 11 CAS_MISMATCH, 12 CAS_FAULT, 13 CLEANUP_PENDING, 14 SOURCE_INVALID, 17 TOKEN_MISMATCH, 18 REQUEST_SOURCE_MISMATCH, 19 REQUEST_FIELD_INVALID.

Hook (one):
- Slot: `root=[game+0x2ce14c8]`; `api=[root+0x20]` (CameraAPI table); slot = `api+0x80` (set vertical FOV); `[api+0x78]` must be the getter `exe+0x2dd070` (`native_ads_fov.cpp:485-499`).
- Original setter `exe+0x2dd080`, signature `void(void* camera, float radians)`; pinned bytes `f30f114934c7415000000000e92f6c1f00` (= `movss [rcx+0x34],xmm1; mov dword [rcx+0x50],0; jmp …`), getter bytes `f30f104134c3` (= `movss xmm0,[rcx+0x34]; ret`), so the camera's FOV is the float at camera+0x34 in radians (`native_ads_fov.cpp:468-469`).
- Call site: `game+0x110bfda`, bytes `ff9280000000` (`call [rdx+0x80]`); the hook only acts when `_ReturnAddress()==game+0x110bfda+6` (`native_ads_fov.cpp:21, 519, 665`). Every other caller of the slot is forwarded untouched.
- Extra pins only for the Fird policy (`fird_input_contract`, `native_ads_fov.cpp:472-484`): game+0x110b4db, +0x111e3ce, +0x111ba5e, +0x111ba70, +0x111ba91, +0x111baa2.
- When it runs: synchronously inside the game's camera evaluation at that call site, once per evaluation of the owned camera. The comments establish only that it is a different execution context from the Lua update ("Diagnostics must never make the update thread wait for an in-flight camera setter", `native_ads_fov.cpp:44-47`; "A callback may have disarmed since the preceding Update", fov.lua:274) and that the "evaluator dispatch precedes the shared CameraAPI setter" (`native_ads_fov.cpp:473`). The thread is not named.

Replacement `setter_hook(camera, native)` (`native_ads_fov.cpp:517-578`), under `policy_lock` exclusive, when armed, caller matches and `camera==policy.c.camera`:
1. Re-run owner check; failure → disarm with error 3/7. `dispatch_owned` (slot still ours, same api) failure → disarm with error 6.
2. (Fird branch, parked.)
3. ADS entry/hold, taken when `reserved!=0 && full_ads && safe` and the current request is queued kind 6 with `r[2]==weapon && r[3]==body`: `applied = handoff_lens(native, ads=true)`; `ads_forwarded++`. The game's own ADS (zoomed) lens is the target; it is only eased.
4. ADS exit, taken when `reserved!=0 && safe && lens_ready && (lens_ads || lens_blending)` and the request is kind 4 or the matching queued kind 6: target = native, but if the current request is kind 6 the target is replaced by `source()` (the live third-person FOV in radians, with no shoulder magnification); `applied = handoff_lens(target, ads=false)`.
5. Normal case, `request_matches && safe` (queued kind 6, same weapon/body, same row address as bound): if `full_ads` → forward native (`ads_forwarded++`); else `applied = shoulder_lens(source())` and `replaced++`; then `applied = handoff_lens(applied, full_ads)`.
6. Otherwise reset the shoulder tween and lens blend state, forward native.
7. Always call the pinned original with `applied`.

FOV source (`source`, `native_ads_fov.cpp:429-434`): `settings=[game+0x2ce1508]`; `degrees = float [settings+0xac488]`; must be finite and 1..179; `radians = degrees*0.01745329251994329577`. This is "the game's current third-person setting" (fov.lua:2-3) read fresh on every call; nothing is cached or written to settings. So hip FOV in first person = the player's in-game third-person FOV slider.

Shoulder (normal aim, not full ADS) lens (`shoulder_lens`, `native_ads_fov.cpp:96-124`): target weight 1 while `shoulder_aim`, else 0.
- With a live shared phase: no local tween; result = hip when not aiming, else `2*atan(tan(hip/2)/1.05)` (5 % focal magnification), the easing being left to `handoff_lens`.
- Without it: weight steps by `min(elapsed,0.12)/0.12` per call (120 ms linear, QPC clock, sampled only during transitions; a gap outside 0..0.25 s rebases instead of integrating), then `w = weight²·(3−2·weight)` and result = `2*atan(tan(hip/2)/(1+0.05·w))`. Settled hip returns the source value bit-exact.

ADS handoff (`handoff_lens`, `native_ads_fov.cpp:158-180`), active because `reserved=180`:
- Invalid target (non-finite, ≤0, ≥3.14159265) → reset and pass through.
- If a shared phase is live, `shared_lens` decides (`native_ads_fov.cpp:129-157`): the phase is dropped when the clock fails or the last `SetTransition` is older than 0.25 s or in the future; if the token's `ads` flag differs from the native state the target is applied at once (native ownership wins); otherwise with `e(x)=x²(3−2x)`, `prior=e(used)`, `current=e(progress)`, `step=clamp((current−prior)/max(0.000001,1−prior),0,1)`, `from=1/tan(last/2)`, `to=1/tan(target/2)`, `result=2*atan(1/(from+(to−from)*step))`; if progress did not advance the last result is repeated; at progress 1 the target is applied exactly.
- Without a shared phase: on a change of the ads flag start a blend from the last output; `t=elapsed/(reserved/1000)`, `w=t²(3−2t)`, `result=2*atan(1/(from+(to−from)*w))` in the same focal-length (1/tan(half-angle)) space; finished when `elapsed>=0.180 s`.
- The Lua coordinator that feeds the shared phase is `transition.lua` created with `duration=.18,max_gap=.25` (runtime.lua:670); `backend.presentation_transition` returns `presentation:result()` only while the frame scope is open (runtime.lua:672).

Begin sequence (`begin_policy`, `native_ads_fov.cpp:633-673`): write generation 0; validate config; refuse if a lease is reserved (2); owner capture + `safe` + `request_matches` (strict: queued kind 6, same weapon/body, same row); binary + API contract; `source()` readable; pin DLL and exe; reserve generation; CAS slot original→callback; verify dispatch; copy policy, zero counters; `apply_immediately` (calls the pinned original once with the computed lens, `native_ads_fov.cpp:507-516`); set `return_address=game+0x110bfda+6`; arm only if everything is still stable; otherwise `failed_begin` restores.

Restore: `NADSF_End` → `armed=0`, drain, `restore_slot` (CAS callback→original only if the slot still holds the callback), clear reservation. No FOV value is written back: the next native setter call writes the game's own lens ("no cached FOV restore", build_fov.py:37).

### blur: `native/native_ads_blur_26568081_c18c221e2cc6.dll`

Sources: `native/first_person/native_ads_blur.cpp` (491), `native_ads_blur.h` (27), `native_memory.h`; fixture `blur_fixture.cpp`. Build: same `tools/build_first_person.py` recipe and naming as fov (pair `('blur','blur_fixture')`, build_first_person.py:6). `build_blur.py` is the older MSVC builder (output `native_ads_blur_0_1_16.dll`, build_blur.py:10); its metadata names the three values "near/far/far-close DoF maxima" (build_blur.py:32-33).

Exports (`native_ads_blur.h:21-27`):
- `uint32_t NADSB_Validate(const NADSB_Config*)`: read-only preflight (kind 4 allowed); error 18 = weapon has no eligible optic source. LIVE (blur.lua:67).
- `uint32_t NADSB_Begin(const NADSB_Config*, uint64_t* generation)`: capture sources, hook, apply zeros once, arm. LIVE (blur.lua:113).
- `uint32_t NADSB_End(uint64_t generation)`: disarm, drain, restore slot. LIVE (blur.lua:135).
- `uint32_t NADSB_ReadStats(uint64_t generation, NADSB_Stats*)`. LIVE (blur.lua:80).
- `uint32_t NADSB_Error(void)`. LIVE (blur.lua:51).

Structs: `NADSB_Config` 80 bytes, same layout as the FOV config, `reserved` must be 0 (`native_ads_blur.cpp:422`); `NADSB_Stats` 80 bytes `{u32 size,abi,installed,armed,error,reserved; u64 seen,forwarded,replaced,immediate_applies; u32 last_hash,last_instance; float last_native,last_applied; u64 reserved_tail}`. Lua cdef (blur.lua:20-37) matches. Error codes as fov except 18 = NO_SOURCE and no 19 (`native_ads_blur.cpp:21-24`).

Hook (one):
- Slot: `root=[game+0x2ce1618]`; `system=[root+0x18]`; `table=[system+0xDE8]`, required to equal `exe+0x2873b18`; the slot is `table+0` and must hold the setter `exe+0x2df740` (`native_ads_blur.cpp:314-340`). Provider identity is checked on every use: `object=[system+0xDC8]` with vtable `exe+0x1715c40`, `[vt+0x38]==exe+0x66a170`, `[vt+0x28]==exe+0x2ad330`, `[system+0xDD8]==exe+0x2873ac0`, `[that+0x28]==exe+0x2df6b0`, `[table+8]==exe+0x2df7b0`, and `[[game+0x2ce14c8]+0xC0]+0x28 == exe+0x2df830`.
- Original setter signature `void(void* provider, uint64_t packed_instance, uint32_t name_hash, const void* variant)`; `packed_instance = class_hash<<32 | entity`; the variant is 24 bytes with u32 type tag at +0 (2 = float) and the float at +8.
- Code pins (`native_ads_blur.cpp:304-313`): game+0xe9ac4b `41ff13` (`call [r11]`), exe+0x2df740 (setter prologue), exe+0x2df6b0, exe+0x66a170, exe+0x2df830 (`e94b9c3800`), exe+0x669480.
- Call site filter: `_ReturnAddress()==game+0xe9ac4b+3` (`native_ads_blur.cpp:20, 367, 464`).
- Which values: class hash `0xF7A3101F` and name hashes `0xA7990477`, `0xF07FC12E`, `0x209F72CF` (`native_ads_blur.cpp:25`).

How the weapon's sources are located at Begin (`source`, `weapon_parameters`, `component`, `fields`; `native_ads_blur.cpp:181-272`):
1. Weapon parameter pool `pool=[game+0x2ce1bf8]`: open-addressed map at `pool+0x278` (buckets ptr), `+0x280` cap (power of two ≤ 1048576), `+0x284` empty key, `+0x288` multiplier; probe `(weapon*mult+n)&(cap-1)`, max 4096 probes, 8-byte rows `{key,index}`; `descriptor=[[pool+0x290]+index*8]` (24 bytes, u32 at +8 must equal the weapon id, bit 0 of byte 20 must be set, otherwise "no source"); `row=[pool+0x298]+index*8` holds two u32 post-effect indices.
2. For each index < 4096: `entity = u32 [system+0x348+index*0x30]`.
3. Component lookup in the provider object: `+0x98` buckets, `+0x90` count, `+0x94` capacity (≤4096), `+0xA8` active, `+0xAC` bucket modulus, `+0xB8` rows, `+0xBC` allocated; 12-byte buckets `{key,index,next}` starting at `entity % modulus`, `next==0xFFFFFFFE` empty, `0x7FFFFFFF` end; then follow the per-entity chain through arrays `ids=[object+0xC8]`, `classes=[object+0xD0]`, `chains=[object+0xD8]` until `classes[index]==0xF7A3101F`.
4. `blob=[[object+0xE8]+row*8]`; `count=u32 [blob+4]` (1..512); key hashes at `blob+0x10`; value i at `blob+0x10+count*4+i*16`; each of the three hashes must occur exactly once with type 2.
5. One or two distinct sources are kept; none → error 18.

Replacement `setter_hook` (`native_ads_blur.cpp:365-394`): when armed, caller matches, `object==policy.provider`, the hash is one of the three and the packed instance is `0xF7A3101F:<a captured entity>`: re-check owner (error 3/7 → disarm), dispatch (6 → disarm), `request_matches` (queued kind 6 with same weapon/body/row; else disarm with 7); if `safe` and the sources still match byte-for-byte: copy the 24-byte variant, and if its tag is 2 and the float is finite replace the float with 0 and forward the copy (`replaced++`). In every other case the original variant is forwarded. When `safe` is false (inhibited action, seated flag) the game's values pass through untouched.

Begin additionally applies at once (`apply_immediately`, `native_ads_blur.cpp:354-364`): for each source and each of the three hashes it calls the pinned original with a float-0 variant.

What this means: the three depth-of-field maxima of the held weapon's optic component(s) are forced to 0 while the first-person weapon camera request (kind 6) is current, at the hip and in ADS alike ("The per-weapon blur filter serves both hip and ADS", `native_ads_blur.cpp:155-156`). Global DoF, FOV, zoom and other effects are not touched (`native_ads_blur.cpp:1`, blur.lua:2-3). Thread/time: synchronous in the game's parameter-push call at game+0xe9ac4b; thread not named.

Restore: `NADSB_End` disarms, drains, CAS-restores the slot. No old value is replayed ("no old effect value replay", build_blur.py:35): the game's next push of those parameters restores them.

### near: `native/native_ads_near_26568081_14a3c38c08f8.dll`

Sources: `native/near_clip/native_ads_near.cpp` (345), `native_ads_near.h` (36), `../first_person/native_memory.h`; fixture `near_fixture.cpp`. Build: `native/near_clip/build_near.py`: `zig c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -O2 -Wall -Werror -shared native_ads_near.cpp` (build_near.py:11-12); stamp = first 12 hex of SHA-256 over this folder's `.cpp`/`.h` plus `native_memory.h` (build_near.py:8-9); runs the fixture under wine, deletes older `native_ads_near_26568081_*.dll`, copies the new one into `native/` and rewrites `profile.lua` (build_near.py:20-24).

Exports (`native_ads_near.h:29-36`):
- `uint32_t NADSN_Begin(const NADSN_Config*, uint64_t* generation)`: hook and arm. LIVE (near_clip.lua:79).
- `uint32_t NADSN_Set(uint64_t generation, float hip, float aim)`: change the two distances (metres, each 0..1). LIVE (near_clip.lua:91).
- `uint32_t NADSN_End(uint64_t generation)`: disarm, restore slot, put the game's distance back if ours is still on the camera. LIVE (near_clip.lua:101).
- `uint32_t NADSN_ReadStats(uint64_t generation, NADSN_Stats*)`. LIVE (near_clip.lua:68).
- `uint32_t NADSN_Error(void)`. LIVE (near_clip.lua:48).
- `uint32_t NADSN_ReadSites(NADSN_Site*, uint32_t capacity)`: diagnostic table of up to 16 distinct return addresses that called the slot. Never called from Lua (not in the cdef).

Structs: `NADSN_Config` 72 bytes `{u32 size,abi; u64 game,exe,context,camera,avatar_owner; u32 avatar_index; float hip,aim; u32 reserved; u64 reserved2}`; `NADSN_Stats` 88 bytes `{u32 size,abi,installed,armed,error,reserved; u64 seen,forwarded,replaced,ads_forwarded,aim_applied,other_kind; float last_native,last_applied,hip,aim}`; `NADSN_Site` 32 bytes. Lua cdef (near_clip.lua:17-34) matches the two used structs; no avatar/body/weapon/request in this config (Lua passes `fov_config` fields and the extra ones are simply not copied, near_clip.lua:54). Errors: 0,1,2,3,4,5,6,7,9,10,11,12,13,17 as above (`native_ads_near.cpp:35-37`).

Hook (one):
- Slot: `api=[[game+0x2ce14c8]+0x20]` (CameraAPI); slot `api+0x68` (set near distance); `[api+0x58]` must be the getter `exe+0x2dd030` (`native_ads_near.cpp:175-189`).
- Original setter `exe+0x2dd050`, `void(void* camera, float metres)`, bytes `f30f114928e9c66b1f00` (= `movss [rcx+0x28],xmm1; jmp …`); getter bytes `f30f104128c3`; so the near distance is the float at camera+0x28.
- Pinned call site `game+0x110ddec`, 34 bytes `488b05d536bd01f30f100d01811301498b0f440f29bc2490010000488b5020ff5268` (loads the API root, loads 0.05 from .rdata, `call [rdx+0x68]`), and the constant at `game+0x2245efc` = bytes `cdcc4c3d` (0.05f, bits `0x3d4ccccd`) (`native_ads_near.cpp:31-32, 167-174`). The comment lists the sites that load that constant: 0x110DE0B, 0x111090F, 0x1107CF0, 0x1144ad1 (`native_ads_near.cpp:3-5`).
- Caller filter: any return address inside game.dll text, `[game+0x1000, game+0x1f99000)` (`native_ads_near.cpp:31, 209, 291`), not one exact site.

Replacement `setter_hook(camera, native)` (`native_ads_near.cpp:196-234`), under `policy_lock` shared:
1. Acts only when armed, `camera==policy.camera`, caller in game text, and the value is either exactly 0.05 (bit compare) or a "sight value" (finite, `0.05 < native ≤ 1.0`). The comment gives live sight values: 0.22 m and 0.35 m for a scope's two zoom levels, 0.115 m for another weapon (`native_ads_near.cpp:204-206`).
2. `observe()` (`native_ads_near.cpp:127-141`): LOST if context/camera/avatar owner/count no longer match → disarm, error 7. OTHER if the current request is not kind 6 or state is unreadable → forward native, reset ease. ADS if `controls[4]!=0`. SHOULDER if `(flags[10]&4)&&(flags[15]&32)`. Else HIP.
3. Target: ADS → the game's value as passed (`ads_forwarded++`). HIP/SHOULDER → `0.05`, or the configured hip/aim distance when it is valid and `>0.05` (`aim_applied++` for aim).
4. Ease (`ease`, `native_ads_near.cpp:98-123`): if the previous output was ≤ 250 000 µs ago and positive, `out = from + (target−from)·(1−exp(−min(elapsed,50 000 µs)/45 000 µs))`, snapping to the target when within 0.002 m; otherwise the target at once. QPC clock.
5. Apply only when the eased value is valid, `≥0.05` and differs from the native argument (`replaced++`); then the pinned original is called with it.
6. Every call is recorded in the 16-row site table.

`pull_sight_distance` (profile.lua:63-65) is a Lua-side switch only: it keeps the helper armed when both menu distances are 0, so that outside full ADS a sight's own near distance (0.22/0.35 m) is pulled back to 0.05 and the viewmodel is not cut away at the hip (near_clip.lua:167-170). The DLL has no such flag; the pull is the "target = 0.05" default in step 3.

Hip vs aim values and per-weapon profiles (menu.lua:144-182): global slider `near_clip_cm` 5..40 cm, default 15; toggle `near_clip_aim` default false → global aim = same cm when on, else 5; a per-weapon row `near_clip_profiles[weapon.key] = {hip_cm, aim_cm}` (each integer 5..40; per-weapon slider defaults 15 and 5) overrides both when present. `near_options()` returns `hip = hip_cm>5 and hip_cm/100 or 0`, `aim = aim_cm>5 and aim_cm/100 or 0`. Shipped defaults therefore send hip 0.15 m, aim 0 m. Full ADS always keeps the game's distance.

Begin (`native_ads_near.cpp:271-300`): config (distances 0..1, pointers ≥ 65536, reserved fields 0); `observe` must not be LOST (kind 4 is accepted here, unlike fov/blur); contract; pin; reserve; copy policy and distances; CAS; verify; arm. No immediate apply.

Restore (`NADSN_End`, `native_ads_near.cpp:309-322`): disarm, drain, CAS-restore the slot, then `restore_distance`: if the last applied value was larger than the last native one and `float [camera+0x28]` still equals it, call the original setter with the last native value, because some callers only run on entry/exit.

### heldfire: `native/native_ads_heldfire_26568081_4553a768a134.dll` (NOT on the shipped path)

Sources: `native/held_fire/native_held_fire.cpp` (342), `native_held_fire.h` (37), `native_held_fire_cas.cpp` (18), `native_held_fire_cas.h` (4), `../first_person/native_memory.h`; tests `held_fire_fixture.cpp`, `held_fire_dll_smoke.cpp`, `held_fire_seh_test.cpp`. Build: `native/held_fire/build_held_fire.py`: zig `c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -O2 -Wall -Wextra -Werror`, main object with `-fno-exceptions -fno-rtti -fno-threadsafe-statics`, CAS object with `-fexceptions -fno-rtti`, linked `-nostdlib -shared -Wl,--entry,NADSHF_DllMain -lkernel32 -lmsvcrt` (no CRT startup) (build_held_fire.py:20-27); asserts the DLL imports no `VirtualProtect`/MinHook/libstdc++ (build_held_fire.py:35); stamp = first 12 hex of SHA-256 over the listed inputs including the build script (build_held_fire.py:7-10); it does not edit profile.lua or install.

Exports (`native_held_fire.h:24-37`), all debug-only (reachable only through `NativeADSCameraTest.start_fird/probe_fird`):
- `uint32_t NADSHF_Begin(const NADSHF_Config*, uint64_t* token)`: hook the slot, arm with a TTL.
- `uint32_t NADSHF_Publish(uint64_t token, const NADSHF_Config*)`: re-validate the same body and refresh the TTL (1..250 ms).
- `uint32_t NADSHF_Pause(uint64_t token)`: disarm without unhooking.
- `uint32_t NADSHF_End(uint64_t token)`: 1 clean, 2 cleanup pending, 0 wrong token.
- `uint32_t NADSHF_ReadStats(uint64_t token, NADSHF_Stats*)`.
- `uint32_t NADSHF_Error(void)`; `uint32_t NADSHF_LastError(void)` (alias; never called from Lua).
- Not exported for Lua: `NADSHF_CasPointer` (SEH wrapper TU), `NADSHF_DllMain` (entry point).

Structs: `NADSHF_Config` 144 bytes, `NADSHF_Stats` 184 bytes (`native_held_fire.h:6-21`); Lua cdef (held_fire.lua:19-39) matches. Errors differ from the others: 8 TARGET_MISMATCH, 14 EXPIRED, 15 TOKEN_MISMATCH (`native_held_fire.cpp:15-18`).

Hook: slot = `[[game+0x2ce14c8]+0x18]` (must equal `exe+0x28703e0`, the unit API table) `+0x370`; original `exe+0x1f77c0`, `void(uint32_t unit, uint32_t event_hash)` (trigger an animation event on a unit) (`native_held_fire.cpp:20, 118-131`). Code pins: exe+0x1f77c0, exe+0x12bf65, game+0x83c37f, game+0x83c782, game+0x83c842, game+0xa6beb1 (`native_held_fire.cpp:108-117`). Replacement (`event_call`, `native_held_fire.cpp:220-238`): for event hash `0x6bd9c5e3` (start) from return addresses game+0x83c3a6, game+0x83c87c or game+0xa6bf00, and hash `0x8bcd9161` (end) from game+0x83c87c, with `unit==policy.c.body`, armed, `GetTickCount64() < deadline`, code pins intact, slot still ours and `owner()` true → the call is dropped (counted); everything else is forwarded. `owner()` (`native_held_fire.cpp:153-210`) requires: registry/units/generations/object as above and `u32 [object+8]==body`; `u32 [object+0x70]==107` nodes; `u64 [object+0x358]==0x4d1c334d294dfa97` (unit resource); `[object+0x178]`=instance, `[instance+0x28]`=graph, `[object+0x18]`=world; `u32 [graph+4]==31` layers; `u32 [graph+0x14]==54` variables; request kind 4 at the bound row; camera inhibit mask 0, `controls[4]==0`, `controls[5]==0`; flag bits `flags[6]&1`, `flags[10]&4`, `flags[15]&32`, `flags[15]&2`, `flags[14]&4`, `flags[9]&4` all clear; active state pointers `[instance+0x50]` (31 entries): layer 4 = state 59 (of 78), layer 24 = state 1 or 6 (of 10), layer 25 = state 1 (of 4), layer 3 ≠ state 71, layer 14 = state 36, or state 3 with `(role&6)==2` and `u32 [[game+0x2ce1508]+0xac29c]` ∈ {3,4} and not (layer 3 state 68 with layer 1 state 75 or 78); a second read of everything must agree. State addresses come from the graph blob (`state_address`, `native_held_fire.cpp:143-152`: `offset=u32 [graph+8]`, table at `graph+offset`, count 31, per-layer def offset, per-state offset). Restore: `NADSHF_End` disarms, drains, CAS-restores, and reports 2 while a callback is in flight.

### zoom: `native/zoom/native_ads_zoom.cpp` (NOT on the shipped path; no 26568081 DLL exists)

Sources: `native/zoom/native_ads_zoom.cpp` (336), `native_ads_zoom.h` (24), vendored MinHook under `native/zoom/vendor/minhook`, tests `zoom_fixture.cpp`, `minhook_smoke.cpp`. Build: `native/zoom/build_zoom.py`: MinHook C sources (`buffer.c`, `hook.c`, `trampoline.c`, `hde/hde64.c`) with `zig cc -O2 -DNDEBUG`, then `zig c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -O2 -shared` (build_zoom.py:27-32); output would be `native_ads_zoom_26568081_<stamp>.dll` with stamp over the two sources, `native_memory.h` and every vendor file (build_zoom.py:14-16). The only DLL in `native/` is `native_ads_zoom_26251122_4d5d246dfecb.dll` (older game build); `profile.lua` has no `zoom` entry; `zoom.lua` is required by nothing and is not in manifest.json. The source itself already carries the 26568081 image stamps (`native_ads_zoom.cpp:109`).

Exports (`native_ads_zoom.h:17-24`), all never called: `uint32_t NADZ_Validate(const NADZ_Config*)`, `uint32_t NADZ_Begin(const NADZ_Config*, uint64_t*)`, `uint32_t NADZ_Bind(uint64_t, const NADZ_Config*)`, `uint32_t NADZ_Update(uint64_t)`, `uint32_t NADZ_End(uint64_t)`, `uint32_t NADZ_ReadStats(uint64_t, NADZ_Stats*)`, `uint32_t NADZ_Error(void)`. `NADZ_Config` 80 bytes and `NADZ_Stats` 88 bytes, same layouts as fov; the cdef in zoom.lua:13-30 matches.

Mechanism (the only helper here that patches code): a MinHook inline hook on `game+0x1608d30`, `uint32_t selector(uint32_t weapon, uint32_t type)` (optic/zoom-level selection getter), left installed and pinned for the process lifetime (End only disarms; the trampoline keeps forwarding). Pins (`native_ads_zoom.cpp:108-123`): selector prologue, call sites game+0x764f29 and game+0x110adcf, magnification getter game+0x764e90, zoom event game+0x110ab80, and game+0x1118891. Hook (`native_ads_zoom.cpp:135-159`): calls the original first; when armed, `type==1`, native ≤ 2, `weapon==policy weapon`, return address game+0x764f2e or game+0x110add4, owner ok, request matches and safe: full ADS → native result; otherwise returns 0 (the base zoom level). `refresh` (`native_ads_zoom.cpp:196-239`): reads the third-person FOV degrees (same `[game+0x2ce1508]+0xac488`), calls the game's zoom event `game+0x110ab80(context, request)` and `float magnification = game+0x764e90(context, weapon)` (0.1..100), computes `lens = (degrees/magnification)*RAD_PER_DEG` (0.0002..2.96), reads `depth = float [request+0x90]` (0..4) and writes with `WriteProcessMemory`: `request+0x84 = lens`, `request+0x9c = depth`, `request+0xa0 = lens`. End re-runs `refresh` with the policy off to rebuild the real zoom state. Error codes add `0x10000/0x20000/0x30000/0x40000 + MH_STATUS` for MinHook stages.

### guarded_reads: `native/guarded_reads_218dbf400f24f282.dll`

Source: `native/memory/guarded_reads.c` (9 lines, plain C), fixture `native/memory/fixture.c` (includes the .c directly). Build: `tools/build_memory.py`: `zig cc -target x86_64-windows-gnu -O2 -shared guarded_reads.c -o dist/memory/goose-guarded.dll` (build_memory.py:7-8), fixture exe run under wine, then the DLL is copied to `native/guarded_reads_<first 16 hex of its own SHA-256>.dll` and that name is substituted into `src/memory.lua` by regex (build_memory.py:12-13). So here the file name IS the DLL's content hash (verified: SHA-256 of the shipped file starts `218dbf400f24f282`).

Variants present in `native/`: `guarded_reads_218dbf400f24f282.dll` (the one memory.lua loads, memory.lua:217; built for stamp `0x6af322c1`), `guarded_reads_2bd3b5c6a73e3e1a.dll`, `guarded_reads_94ffe44f6a19cbcb.dll`, `guarded_reads_b81a5f50ea621a3a.dll`, `guarded_reads_fb18682bc3515a7a.dll`, and `guarded_reads.dll` (byte-identical to the `94ffe44f…` one). Selection is by the single hard-coded path in memory.lua:217; the others are leftovers from earlier builds (their build pins were not inspected). If `ffi.load` fails, memory.lua sets `_fast_unavailable` and uses its pure-Lua `ReadProcessMemory` path (memory.lua:214-221).

Exports (C, `__declspec(dllexport)`; addresses and keys travel as `double`; every failure returns −1):
- `double GGR_scalar(double v, int n)` (guarded_reads.c:6): `n` must be 1, 4 or 8; address must be finite, integral, ≥ 65536, < 140737488355328 (2^47), `address+n` below 2^47; `ReadProcessMemory` of n bytes little-endian zero-extended; for n==8 the value must itself be a pointer in [65536, 2^47) or −1. Used by `M:ptr`, `M:u8`, `M:u32` (memory.lua:230-260).
- `double GGR_hashmap(double v, double key)` (guarded_reads.c:7): reads a 20-byte header `{u64 buckets, u32 cap, u32 empty, u32 mult}`; cap power of two ≤ 1048576; key ≠ empty; probes `(key*mult + p) & (cap-1)` for `p < min(cap,4096)` over 8-byte rows `{u32 key, u32 value}`; an empty row or value `0xffffffff` → −1. Used by `M:hashmap` (memory.lua:322-331).
- `double GGR_roster(double v, double peer)` (guarded_reads.c:8): reads the u64 at `peer`, scans 8 rows of stride 192 at `v` comparing each row's first u64; returns 1/0. Used by `M:roster_has` (memory.lua:223-229; kit squad logic, src/backend.lua:99-101).
- `double GGR_module(void)` (guarded_reads.c:9): `GetModuleHandleA("game.dll")`, checks `MZ`, PE signature `0x4550`, machine `0x8664`, TimeDateStamp `0x6af322c1`, optional magic `0x20b`, SizeOfImage `0x3382000`; returns the base. Used by `M:_module` (memory.lua:266-275).

Lua cdef (memory.lua:216) matches the four signatures. There is no write export: writes are done in Lua (`M:copy`, memory.lua:164-209) with kernel32 `VirtualQuery` + `WriteProcessMemory` + readback, limited to 1..4096 bytes of committed, private (`type==0x20000`), `PAGE_READWRITE` (`protection==4`) memory and never inside the validated game.dll image. Caching: `begin_frame()` (memory.lua:262-265) ends any snapshot and increments `_frame`, which makes `_module` re-validate game.dll at most once per frame; `begin_snapshot()/end_snapshot()` (memory.lua:125-139) cache identical `read(address,size)` results in a table keyed `address..':'..size` (used by freezes.lua:295-297); the scalar fast path is not cached.

Relation to True First Person: none of the first-person adapters uses this DLL. `src/first_person/backend.lua` has its own kernel32 `ReadProcessMemory` reader (backend.lua:65-83). The only first-person use of memory.lua is `native_ads_option.lua:53-54` (`copy`), which never touches the GGR exports.

## 4. Build-specific data

### 4.1 `src/first_person/profile.lua` in full

```lua
local PORT51=mod.require('src/first_person/compat51.lua')
-- Build-specific native camera contract. RVAs are relative to game.dll.
-- tools/trace_native.py verifies these bytes against a supplied offline dump.
return {
    timestamp = 0x6af322c1,
    image_size = 0x3382000,
    globals = {
        camera = 0x2e287d0,
        controller = 0x2e381a8,
        world = 0x2ce41b8,
        player = 0x2ce1618,
        avatar = 0x2ce1ee0,
        wield = 0x2ce1fd0,
        weapons = 0x2ce1ea0,
    },
    functions = {
        -- Wrapper replaces RCX, but forwards EDX/R8D: pass (nil, avatar, weapon).
        enter = 0x110fc80,
        exit = 0x11108a0,
        allowed = 0x1144d60,
    },
    alignment = {
        abi = 1,
        library = 'native/native_ads_alignment_26568081_8e0af36233bd.dll',
        duration_ms = 10500,
        executable = {
            name = 'helldivers2.exe', timestamp = 0x6af2d5e6, image_size = 0x29fe000,
            unit_registry = 0x1ab76f8,
        },
        -- These call instructions stay intact: the native helper filters only
        -- the leased unit and these two callers, forwarding every other call.
        position_return = 0x111a80d,
        rotation_return = 0x111a82d,
    },
    pose = {
        -- Native validation owns this captured rig contract: branch5/9B115563
        -- contains l_hand(13), r_hand(34), weapon attachment8; camera50 is a
        -- sibling under root4. Its copied local vector cannot move that camera.
        abi = 2,
        library = 'native/native_ads_editor_26568081_4940b83e17ad.dll',
        -- On since 0.5.41-smooth.28: with a scoped weapon the held aim request scales look input by the
        -- sight's zoom level outside the sights too. The helper gives hip and normal aim the third-person feel
        -- and stands down in full ADS, where the scope's own scaling applies. All 16 code pins match 26568081.
        normalize_zoom_input = true,
        duration_ms = 30500,
        blend_ms = 150,
        -- Arms/weapon branch parent coordinates; screen-right/down remains live QA.
        offset_x = 0.15,
        offset_y = 0,
        offset_z = -0.12,
        executable = {
            name = 'helldivers2.exe', timestamp = 0x6af2d5e6, image_size = 0x29fe000,
            unit_registry = 0x1ab76f8,
        },
    },
    fov = {
        abi = 1,ads_blend_ms=180,
        library = 'native/native_ads_fov_26568081_764664986628.dll',
        executable = { name = 'helldivers2.exe', timestamp = 0x6af2d5e6, image_size = 0x29fe000 },
    },
    blur = {abi=1,library='native/native_ads_blur_26568081_c18c221e2cc6.dll'},
    -- Near clip: game.dll 0x110DE0B sets near 0.05 through CameraAPI+0x68 each update.
    -- pull_sight_distance: outside full ADS a sight's own near distance (0.22 m / 0.35 m for a scope's zoom
    -- levels) is pulled back to the game's 0.05, so a scoped weapon's viewmodel is not cut away at the hip.
    near = {abi=1,library='native/native_ads_near_26568081_14a3c38c08f8.dll',pull_sight_distance=true},
    -- Owned normal4 body only: skip the native hipfire start/end events.
    held_fire = {abi=1,library='native/native_ads_heldfire_26568081_4553a768a134.dll'},
    motion = {
        world_pose = {rva=0x1f30e0,hex='40534883ec204863dae8e2d5eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3c3'},
    },
    camera = {
        abi=1,library='native/native_ads_camera_26568081_e88ff8dc46b2.dll',
        forward=.18,up=.04,smoothing_ms=100,blend_ms=120,max_gap_ms=250,
        -- Both handoff eases are off: neither worked live on build 26568081 (SOURCE.md, "Live results").
        -- NativeADSCameraTest.camera_handoff(entry_ms, rotation_ms) still switches them for experiments.
        handoff={entry_ms=0,rotation_ms=0},
        -- Repeat the last native view while the camera is still attached to the rig on the way out.
        cut_hold=true,
        -- Keep the head camera at the native eye through a weapon swap or stim, so the returning view barely moves.
        eye_hold=true,
        -- On aim release the game drops the native view for two frames: anchor the camera to the third-person weapon.
        ads_anchor=true,
        -- Keep the head camera through seated interactions such as turning a valve.
        seated_head=true,
        -- Aim-view keeper: clear the game's aim-active byte so an aim release does not exit the native view.
        -- Done ONCE, on the frame the aim button goes up (the game acts on the release one frame later).
        -- Never per frame: that made the game re-run its aim entry every frame, the helpers misread full
        -- ADS, a scope lost its zoom scaling and up/down aim went dull in some directions (live 2026-11-13).
        keep_aim_view=true,
        -- Place the arm pieces' chest, collar and shoulder bones as on the body (arm_pieces.lua).
        body_sleeves=true,
        -- Put the rig's left hand on the weapon's grip bone, as the third-person hand is (grip_hand.lua).
        grip_hand=true,
        executable={name='helldivers2.exe',timestamp=0x6af2d5e6,image_size=0x29fe000},
    },
    -- Optional manual diagnostic only: the paired native exit body event.
    -- Event semantics and its effect on the held weapon pose remain unproven.
    body_pose = {
        event = 0x372071D0,
        unit_api_global = 0x2ce14c8,
        api_offset = 0x18,
        exists_offset = 0x350,
        trigger_offset = 0x370,
        executable = {
            name = 'helldivers2.exe', timestamp = 0x6af2d5e6, image_size = 0x29fe000,
            exists = { rva = 0x1f75d0, hex = '48895c2408574883ec208bfae8ef90eaff488bc8488bd84c8b0041ff90b00100' },
            trigger = { rva = 0x1f77c0, hex = '48895c2408574883ec208bda8bf9e8fd8eeaff448bc38bd7488b4818488b5c24' },
        },
    },
    checks = {
        {name = "enter_wrapper", rva = 0x110fc80, hex = "40534883ec20488b1d438bd101488bcbe85b0000008b83fc0100003983f80100007509488d93001f"},
        {name = "constructor", rva = 0x110fcf0, hex = "4055535657488dac2478ffffff4881ec88010000488b0505533a014833c4488945500f57c033c041"},
        {name = "exit", rva = 0x11108a0, hex = "405541544156488dac24d0feffff4881ec30020000488b0554473a014833c4488945708b81fc0100"},
        {name = "evaluator", rva = 0x1118370, hex = "48895c24205556574154415541564157488dac24f0fcffff4881ec10040000440f299c24b0030000"},
        {name = "controller_enter", rva = 0x11449f0, hex = "48895c2410574883ec20807904008bda488bf90f85010100008339000f85f8000000807905000f85"},
        {name = "controller_exit", rva = 0x1144b20, hex = "4883ec28807904007446c6410400488b0d9b3cce01e866bdfcff488b05c7c9b9"},
        {name = "controller_update", rva = 0x11446e0, hex = "f30f114c24105341574883ec38488b0524cfb9014c8bf983b8880000000074128b90a8030000488d"},
        {name = "controller_aim_gate", rva = 0x11448e3, hex = "48b800000000000001004985842488e853004c8b64243074083b3db6b3cf017508498bcfe814020000"},
        {name = "input_desired_gate", rva = 0xa5c44b, hex = "488b0d56bd3d024c8bb424b80000003a4104741984c074098bd3e886856e00eb0c488b0d35bd3d02e8a8866e00"},
        {name = "allowed", rva = 0x1144d60, hex = "48895c240848896c2410488974241857415641574883ec203b1552a3cf017452488b1d19d1b90145"},
        {name = "allow_fpv_field", rva = 0x1144e3b, hex = "418bc9488b0ccee8c9d13cff80b880010000007482b001eb80"},
        {name = "root_position_call", rva = 0x111a809, hex = "41ff5138"},
        {name = "root_rotation_call", rva = 0x111a829, hex = "41ff5148"},
        {name = "pose_branch_position_call", rva = 0x111a460, hex = "41ff5138"},
    },
}
```

### 4.2 Every address, offset, index, hash and pattern

Columns: name, value, defined at, what it is (as the code or its comments say), live on the shipped path. Rows are grouped by the part of the code they come from; a value used in several places appears once per place.

**Lua: backend, controller, action state, profile entries, runtime diagnostics**

| name | value | defined at | what it is | live? |
|---|---|---|---|---|
| profile.timestamp | 0x6af322c1 | fp/profile.lua:5 | game.dll PE TimeDateStamp (checked at header+8) | LIVE |
| profile.image_size | 0x3382000 | fp/profile.lua:6 | game.dll SizeOfImage (header+80) | LIVE |
| globals.camera | game+0x2e287d0 | fp/profile.lua:8 | pointer to the camera context / request queue | LIVE |
| globals.controller | game+0x2e381a8 | fp/profile.lua:9 | pointer to the native first-person aim controller | LIVE |
| globals.world | game+0x2ce41b8 | fp/profile.lua:10 | pointer to the world / entity manager | LIVE |
| globals.player | game+0x2ce1618 | fp/profile.lua:11 | pointer to the local player object | LIVE |
| globals.avatar | game+0x2ce1ee0 | fp/profile.lua:12 | pointer to the avatar component pool | LIVE |
| globals.wield | game+0x2ce1fd0 | fp/profile.lua:13 | pointer to the wield component pool | LIVE |
| globals.weapons | game+0x2ce1ea0 | fp/profile.lua:14 | pointer to the weapon component pool | LIVE |
| functions.enter | game+0x110fc80 | fp/profile.lua:18 | native camera entry wrapper; "Wrapper replaces RCX, but forwards EDX/R8D: pass (nil, avatar, weapon)"; called as `void(void *context, uint32 avatar, uint32 weapon)` | LIVE |
| functions.exit | game+0x11108a0 | fp/profile.lua:19 | native camera exit; `void(void *context)` | LIVE |
| functions.allowed | game+0x1144d60 | fp/profile.lua:20 | "weapon supports native FPV" predicate; `uint8(void *controller, uint32 weapon)` | LIVE |
| alignment.abi | 1 | fp/profile.lua:23 | alignment helper ABI | PARKED |
| alignment.library | native/native_ads_alignment_26568081_8e0af36233bd.dll | fp/profile.lua:24 | alignment helper DLL | PARKED |
| alignment.duration_ms | 10500 | fp/profile.lua:25 | alignment lease length; also the initial `duration_ms` of `_alignment_config` (overwritten for pose) | PARKED |
| alignment.executable | helldivers2.exe, timestamp 0x6af2d5e6, image_size 0x29fe000, unit_registry 0x1ab76f8 | fp/profile.lua:26-29 | executable identity; DEFAULT profile of `_alignment_context` (used by action_state, camera_only_weapon, runtime diagnostics, arm_pieces, grip_hand) | LIVE (values identical to pose.executable) |
| alignment.position_return | game+0x111a80d | fp/profile.lua:32 | return address after the root position call the helper filters | PARKED (Lua never reads it) |
| alignment.rotation_return | game+0x111a82d | fp/profile.lua:33 | return address after the root rotation call | PARKED |
| pose.abi | 2 | fp/profile.lua:39 | pose helper ABI (NADSE_Config 104 bytes, NADSE_Stats 80 bytes) | LIVE |
| pose.library | native/native_ads_editor_26568081_4940b83e17ad.dll | fp/profile.lua:40 | pose helper DLL | LIVE |
| pose.normalize_zoom_input | true | fp/profile.lua:44 | selects the `...Input` exports (look input scaling outside the sights for scoped weapons) | LIVE |
| pose.duration_ms | 30500 | fp/profile.lua:45 | maximum lease length accepted by `enter_pose`; default when no remaining_ms | LIVE (as bound; shipped lease uses 0) |
| pose.blend_ms | 150 | fp/profile.lua:46 | helper entry blend, sent as `blend_ms` | LIVE |
| pose.offset_x / offset_y / offset_z | 0.15 / 0 / -0.12 | fp/profile.lua:48-50 | last-resort arms/weapon branch offset | LIVE (fallback) |
| pose.executable | helldivers2.exe, timestamp 0x6af2d5e6, image_size 0x29fe000, unit_registry 0x1ab76f8 | fp/profile.lua:51-54 | executable identity for the pose helper | LIVE |
| executable unit registry | exe+0x1ab76f8 | fp/profile.lua:28, 53 | pointer to the engine unit registry | LIVE |
| fov.abi / fov.ads_blend_ms | 1 / 180 | fp/profile.lua:57 | FOV helper ABI, ADS blend | LIVE (fov.lua, other part) |
| fov.library | native/native_ads_fov_26568081_764664986628.dll | fp/profile.lua:58 | FOV helper DLL | LIVE |
| fov.executable | helldivers2.exe, 0x6af2d5e6, 0x29fe000 | fp/profile.lua:59 | - | LIVE |
| blur.abi / blur.library | 1 / native/native_ads_blur_26568081_c18c221e2cc6.dll | fp/profile.lua:61 | blur helper | LIVE |
| near clip writer | game+0x110DE0B | fp/profile.lua:62 (comment) | "sets near 0.05 through CameraAPI+0x68 each update" | comment only |
| near.abi / near.library / near.pull_sight_distance | 1 / native/native_ads_near_26568081_14a3c38c08f8.dll / true | fp/profile.lua:65 | near-clip helper; sight near distances 0.22 m / 0.35 m pulled back to 0.05 outside full ADS | LIVE |
| held_fire.abi / held_fire.library | 1 / native/native_ads_heldfire_26568081_4553a768a134.dll | fp/profile.lua:67 | "Owned normal4 body only: skip the native hipfire start/end events" | PARKED (fird only) |
| motion.world_pose | exe+0x1f30e0, hex 40534883ec204863dae8e2d5eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3c3 | fp/profile.lua:69 | engine world-pose getter; establishes world-matrix array at graph+0x28 with stride 0x40 (pinned by fp/locomotion.lua:129-131) | LIVE |
| camera.abi / camera.library | 1 / native/native_ads_camera_26568081_e88ff8dc46b2.dll | fp/profile.lua:72 | head-camera helper | LIVE |
| camera.forward / up | .18 / .04 | fp/profile.lua:73 | head camera offset from the head (m) | LIVE |
| camera.smoothing_ms / blend_ms / max_gap_ms | 100 / 120 / 250 | fp/profile.lua:73 | head camera smoothing, blend, gap limit | LIVE |
| camera.handoff | entry_ms=0, rotation_ms=0 | fp/profile.lua:76 | both handoff eases off | off |
| camera.cut_hold | true | fp/profile.lua:78 | repeat the last native view while the camera is still attached to the rig on the way out | LIVE |
| camera.eye_hold | true | fp/profile.lua:80 | keep the head camera at the native eye through weapon swap / stim | LIVE |
| camera.ads_anchor | true | fp/profile.lua:82 | anchor to the third-person weapon for the two frames after aim release | LIVE |
| camera.seated_head | true | fp/profile.lua:84 | head camera through seated interactions; gates `detach_seated_camera` | LIVE |
| camera.keep_aim_view | true | fp/profile.lua:89 | gates `keep_aim_view` | LIVE |
| camera.body_sleeves | true | fp/profile.lua:91 | gates arm_pieces | LIVE |
| camera.grip_hand | true | fp/profile.lua:93 | gates grip_hand | LIVE |
| camera.executable | helldivers2.exe, 0x6af2d5e6, 0x29fe000 | fp/profile.lua:94 | - | LIVE |
| body_pose.event | 0x372071D0 | fp/profile.lua:99 | "paired native exit body event" name hash | PARKED |
| body_pose.unit_api_global | game+0x2ce14c8 | fp/profile.lua:100 | pointer to the engine API table set; also used LIVE by `detach_seated_camera` (backend.lua:1379) | LIVE (address) |
| body_pose.api_offset | 0x18 | fp/profile.lua:101 | Unit animation API pointer inside that set | PARKED |
| body_pose.exists_offset / trigger_offset | 0x350 / 0x370 | fp/profile.lua:102-103 | slots of the event exists / trigger functions | PARKED |
| body_pose.executable.exists | exe+0x1f75d0, hex 48895c2408574883ec208bfae8ef90eaff488bc8488bd84c8b0041ff90b00100 | fp/profile.lua:106 | executable fingerprint 1 of `_alignment_executable` (backend.lua:716) | LIVE |
| body_pose.executable.trigger | exe+0x1f77c0, hex 48895c2408574883ec208bda8bf9e8fd8eeaff448bc38bd7488b4818488b5c24 | fp/profile.lua:107 | executable fingerprint 2 | LIVE |
| checks.enter_wrapper | game+0x110fc80, hex 40534883ec20488b1d438bd101488bcbe85b0000008b83fc0100003983f80100007509488d93001f | fp/profile.lua:111 | byte pin at game+RVA, 40 bytes | LIVE |
| checks.constructor | game+0x110fcf0, hex 4055535657488dac2478ffffff4881ec88010000488b0505533a014833c4488945500f57c033c041 | fp/profile.lua:112 | byte pin at game+RVA, 40 bytes | LIVE |
| checks.exit | game+0x11108a0, hex 405541544156488dac24d0feffff4881ec30020000488b0554473a014833c4488945708b81fc0100 | fp/profile.lua:113 | byte pin at game+RVA, 40 bytes | LIVE |
| checks.evaluator | game+0x1118370, hex 48895c24205556574154415541564157488dac24f0fcffff4881ec10040000440f299c24b0030000 | fp/profile.lua:114 | byte pin at game+RVA, 40 bytes; cold packed first-person page (must be readable only for scope full) | LIVE |
| checks.controller_enter | game+0x11449f0, hex 48895c2410574883ec20807904008bda488bf90f85010100008339000f85f8000000807905000f85 | fp/profile.lua:115 | byte pin at game+RVA, 40 bytes | LIVE |
| checks.controller_exit | game+0x1144b20, hex 4883ec28807904007446c6410400488b0d9b3cce01e866bdfcff488b05c7c9b9 | fp/profile.lua:116 | byte pin at game+RVA, 32 bytes | LIVE |
| checks.controller_update | game+0x11446e0, hex f30f114c24105341574883ec38488b0524cfb9014c8bf983b8880000000074128b90a8030000488d | fp/profile.lua:117 | byte pin at game+RVA, 40 bytes | LIVE |
| checks.controller_aim_gate | game+0x11448e3, hex 48b800000000000001004985842488e853004c8b64243074083b3db6b3cf017508498bcfe814020000 | fp/profile.lua:118 | byte pin at game+RVA, 41 bytes | LIVE |
| checks.input_desired_gate | game+0xa5c44b, hex 488b0d56bd3d024c8bb424b80000003a4104741984c074098bd3e886856e00eb0c488b0d35bd3d02e8a8866e00 | fp/profile.lua:119 | byte pin at game+RVA, 45 bytes | LIVE |
| checks.allowed | game+0x1144d60, hex 48895c240848896c2410488974241857415641574883ec203b1552a3cf017452488b1d19d1b90145 | fp/profile.lua:120 | byte pin at game+RVA, 40 bytes | LIVE |
| checks.allow_fpv_field | game+0x1144e3b, hex 418bc9488b0ccee8c9d13cff80b880010000007482b001eb80 | fp/profile.lua:121 | byte pin at game+RVA, 25 bytes | LIVE |
| checks.root_position_call | game+0x111a809, hex 41ff5138 | fp/profile.lua:122 | byte pin at game+RVA, 4 bytes; cold packed first-person page (must be readable only for scope full) | LIVE |
| checks.root_rotation_call | game+0x111a829, hex 41ff5148 | fp/profile.lua:123 | byte pin at game+RVA, 4 bytes; cold packed first-person page (must be readable only for scope full) | LIVE |
| checks.pose_branch_position_call | game+0x111a460, hex 41ff5138 | fp/profile.lua:124 | byte pin at game+RVA, 4 bytes; cold packed first-person page (must be readable only for scope full) | LIVE |
| cold pins | evaluator, root_position_call, root_rotation_call, pose_branch_position_call | fp/backend.lua:85 | FP_COLD_CHECK: packed first-person pages that may be unreadable before they ran; required for scope `full`, optional for `entry` | LIVE |
| warm entry evaluator call | game+0x110FCC2, 11 bytes 0f57d2488bcbe8a3860000 | fp/backend.lua:118 | extra pin for scope `entry` | fird-only |
| PE e_lfanew | module+0x3c (0x40..0x1000) | fp/backend.lua:101, 709 | PE header offset | LIVE |
| address limits | LOW 65536, HIGH 140737488355328 (2^47) | fp/backend.lua:9 | valid pointer range | LIVE |
| read size limit | 1024 | fp/backend.lua:60, 66 | max bytes per `_read` | LIVE |
| hash map header | +0 buckets ptr, +8 capacity u32, +12 empty key u32, +16 multiplier u32; rows 8 bytes (key u32, index u32); probe limit min(cap,4096) | fp/backend.lua:132-150 | entity -> index maps | LIVE |
| camera context: camera | ptr[context+0x0] | fp/backend.lua:164 | render camera object | LIVE |
| camera context: camera unit | u32[context+0x8] | fp/backend.lua:227, 391, 1381 | render camera unit ref | LIVE |
| camera context: view matrix | context+0x0C (64 bytes) | fp/locomotion.lua:136 | camera matrix used by the motion sampler | LIVE (other part) |
| camera context: world for detach | ptr[context+0x180] | fp/backend.lua:1381 | world argument of the camera unlink call | LIVE |
| request queue head / tail | u32[context+0x1f8] / u32[context+0x1fc] | fp/backend.lua:159 | ring indices, each < 32 | LIVE |
| request ring | context+0x200, 32 records, stride 0xe8 | fp/backend.lua:162, 473, 589, 1043 | camera request records; newest = slot (tail+31)%32; next free = slot tail | LIVE |
| fallback record | context+0x1f00 | fp/backend.lua:162 | record used when head == tail | LIVE |
| record +0x0 | u16 | fp/backend.lua:165 | camera kind (4 normal, 6 native first person) | LIVE |
| record +0x4 | u32 | fp/backend.lua:165 | avatar entity | LIVE |
| record +0x8 | u32 | fp/backend.lua:165 | weapon entity | LIVE |
| record +0xc | u32 | fp/backend.lua:264 | body unit ref (kind 6) | LIVE |
| record +0x98 | float (written as u32 0) | fp/backend.lua:1352-1361 | sight distance set on aim entry; zeroed by keep_aim_view | LIVE (write) |
| controller +0x0 | u32 | fp/backend.lua:171-173 | inhibit / action mask (2 = weapon swap, 0x20 = vault, 64 = stim; 0 = free) | LIVE |
| controller +0x4 | u8 | fp/backend.lua:172, 1350 | aim "active" byte; cleared by keep_aim_view | LIVE (read + write) |
| controller +0x5 | u8 | fp/backend.lua:173; fp/controller.lua:391 | guard byte (raised for a frame when an expendable launcher fires) | LIVE |
| player +0x88 | u32 > 0 | fp/backend.lua:175 | local player present | LIVE |
| player +0x3a8 | u32 | fp/backend.lua:176 | local player key ("light") into the player map | LIVE |
| world +0xf24990 | hash map | fp/backend.lua:177 | player key -> descriptor index (limit 1048576) | LIVE |
| world +0xf1c978 | hash map | fp/backend.lua:152 | entity id -> descriptor index (limit 1048576) | LIVE |
| world +0xf349e0 | array, stride 24 | fp/backend.lua:153, 178 | entity descriptors: +8 entity id (for the player row: avatar id), +12 unit ref (backpack), +16 player key, +20 bit 0 = alive; first 20 bytes = generation descriptor | LIVE |
| avatar pool +0xf8 | hash map | fp/backend.lua:185 | avatar id -> slot (limit 8) | LIVE |
| avatar pool +0x6c | u32 <= 8 | fp/backend.lua:187 | slot count | LIVE |
| avatar pool +0x110 + i*8 | ptr | fp/backend.lua:188 | per-slot entity pointer; 20 bytes there = avatar descriptor; +0xc = body unit ref | LIVE |
| avatar pool +0x53e888 + i*0x1240 | 24 bytes | fp/backend.lua:192-193, 363 | packed state flags | LIVE |
| avatar pool +0x53e9c0 + i*0x1240 | u32 | fp/backend.lua:196 | first-person viewmodel (rig) unit ref | LIVE |
| avatar pool +0x546B20 + i*0x1B8 | u8 0/1 | fp/backend.lua:543 | first-person presentation mode byte | fird-only |
| flags bit 48 | byte 6 bit 0 | fp/backend.lua:198 | `aiming` ("tested by the game's native first-person controller at 12B28AD") | LIVE |
| flags bit 82 + bit 125 | byte 10 & 4, byte 15 & 32 | fp/backend.lua:203-204 | `shoulder_aim` (with controller inactive) | LIVE |
| flags bit 114 | byte 14 & 4 | fp/backend.lua:205 | `seated` | LIVE |
| flags bit 121 | byte 15 & 2 | fp/backend.lua:208 | `reloading` | LIVE |
| flags u32 & 6 == 2 | bytes 0..3 | fp/backend.lua:355 | bridge / ship combat-controls role 2 | LIVE |
| flags sprint gait | byte 9 bit 2 (0x84 sprint; walk 0x88, jog 0x90) | fp/locomotion.lua:82-87 | sprint | LIVE (other part) |
| flags posture | byte 9 bits 5-6: 0x20 crouch/slide, 0x40 prone/dive | fp/action_placement.lua:58-64 | stance | LIVE |
| flags slide | byte 10 bit 1 (with posture 0x20) | fp/action_placement.lua:62 | slide | LIVE |
| flags dive | byte 12 bit 5 (with posture 0x40) | fp/action_placement.lua:61 | dive | LIVE |
| flags mirror | "player-state row+8 (pm+547870, stride 0x78)", reload = row byte 23 bit 1 | fp/backend.lua:206-207 (comment) | same 24 bytes mirrored | comment only |
| wield pool +0x30 | hash map | fp/backend.lua:246 | avatar id -> wield slot | LIVE |
| wield pool +0x48 | ptr to ptr array (index*8) | fp/backend.lua:247 | owner descriptor pointers | LIVE |
| wield pool +0x60 | ptr to rows, stride 0x1d0 | fp/backend.lua:249-250 | row +0 = wielded weapon entity id | LIVE |
| weapon pool +0x30 | hash map | fp/backend.lua:255 | weapon entity -> slot | LIVE |
| weapon pool +0x48 | ptr to ptr array (index*8) | fp/backend.lua:256 | weapon component entry; 20 bytes = weapon descriptor; +0xc = weapon unit ref | LIVE |
| inventory global | game+0x2ce18e8 | fp/backend.lua:303 | pointer to the inventory pool | LIVE |
| inventory +40 / +64 / +80 | map / owner ptr array / rows stride 48 | fp/backend.lua:304-307 | per-avatar inventory | LIVE |
| inventory row +12 | u32 | fp/backend.lua:308 | "Inventory slot 3 (byte 12)": equipped backpack entity | LIVE |
| scene state global | game+0x2ce1508 | fp/backend.lua:365 | pointer to game state | LIVE |
| scene id | u32[state+0xac29c], must be 3 or 4 | fp/backend.lua:366-367 | "observed gameplay scene3/4" for the bridge lifecycle | LIVE |
| unit ref split | index = ref % 0x400000; generation = floor(ref/0x400000) % 256 | fp/backend.lua:788-789, 818-819 | engine unit handle | LIVE |
| registry +0x88 | ptr | fp/backend.lua:790-794 | unit object pointer array (index*8) | LIVE |
| registry +0x98 | u32 <= 0x400000 | fp/backend.lua:791, 820 | capacity | LIVE |
| registry +0xA0 | ptr | fp/backend.lua:793 | generation byte array | LIVE |
| unit object +0x358 | 8 bytes | fp/backend.lua:322, 845; fp/action_state.lua:58 | resource id (unit type for weapons = placement key; state-machine resource for the body) | LIVE |
| Breaching Hammer resource | bytes 53 85 BD A2 BD C9 3E 5F | fp/backend.lua:323 | weapon unit resource that gets head camera only | LIVE |
| unit object +0x60 | scene graph | fp/backend.lua:1377; fp/locomotion.lua:112 | camera+0x18 points here; +0x10 node count, +0x20 local array, +0x28 world array | LIVE |
| unit object +0x1D0 | 8 bytes | fp/backend.lua:1378 | camera parent link; zero = not linked | LIVE |
| unit object +0x178 | ptr | fp/action_state.lua:59 | animation state machine instance | LIVE |
| unit vtable +0x1B0 | exe+0x28d510, bytes 48 8B 81 78 01 00 00 C3 | fp/action_state.lua:6, 54-55 | animation interface getter (returns [this+0x178]) | LIVE |
| unit vtable +0x1B8 | exe+0x28d520, bytes 48 8B 81 58 03 00 00 48 89 02 48 8B C2 C3 | fp/action_state.lua:7, 56-57 | resource getter (returns [this+0x358]) | LIVE |
| unit vtable +0xE8 | exe+0x28d3c0 | fp/locomotion.lua:111 | scene-graph getter | LIVE (other part) |
| state machine instance +0x28 | ptr | fp/action_state.lua:60 | state machine definition (graph) | LIVE |
| state machine instance +0x50 | ptr | fp/action_state.lua:68 | active-state pointer array, 8 bytes per layer | LIVE |
| graph header | +4 layer count u32 (1..32), +8 layer table offset u32 (12..0x10000) | fp/action_state.lua:61-65 | - | LIVE |
| layer table | table_at+4: u32 offset per layer; layer def +8 state count (1..2048), +12 u32 offset per state | fp/action_state.lua:67, 82-92 | - | LIVE |
| body state-machine resource | 4D1C334D294DFA97 (bytes 97 FA 4D 29 4D 33 1C 4D), 31 layers | fp/action_state.lua:5, 117; fp/action_placement.lua:50 | the only verified character animation resource | LIVE |
| layer 14 states | 36 = mission on-foot; 3 = ship/bridge lifecycle | fp/backend.lua:353-354 | on-foot test | LIVE |
| layer 1 / layer 3 states | l1 78, 79, 75; l3 71, 68 (secondaries idle/jog 66/65) | fp/action_state.lua:118; fp/locomotion.lua:80-92 | sprint evidence | LIVE |
| support layers | {1,3,10,11,14} | fp/backend.lua:343 | sampled only with `support_grip_tracking` | fird-only |
| unit API set | ptr[game+0x2ce14c8] | fp/backend.lua:1213, 1379 | engine API table set | LIVE |
| camera unlink function | ptr[ptr[ptr[game+0x2ce14c8]+8]+0xF0], `void(void *world, uint32 unit)` | fp/backend.lua:1379-1390 | "UnitAPI+0xF0(world = context+0x180, camera unit = context+8)"; same call as the game's exit at game+0x111093e | LIVE |
| game exit detach site | game+0x111093e | fp/backend.lua:1370 (comment) | where the game's exit makes that call | comment only |
| renderer getter / setter | exe+0x3d43d0 / exe+0x3d3ff0 | fp/backend.lua:740 | `Application.render_setting` getter and setter natives | LIVE |
| getter / setter prologues | 48 8B D1 E9 08 FD FF FF / 48 8B D1 E9 88 FA FF FF | fp/backend.lua:743-744 | pins | LIVE |
| renderer pin 3 | exe+0x3d419a: 48 8B 0D 1F 37 6E 01 | fp/backend.lua:745 | pin | LIVE |
| renderer pin 4 | exe+0x3d420e: 83 F8 04 75 1A 41 38 71 08 | fp/backend.lua:746 | pin | LIVE |
| render settings table | ptr[exe+0x1ab78c0]; header +0 count u32, +8 rows ptr; rows 24 bytes: +0 type (4 = Boolean), +8 value byte, +16 key u32, sorted by key | fp/backend.lua:750-770 | renderer setting store | LIVE |
| setting key dof_enabled | 0x06f1d0d3 | fp/backend.lua:732 | name hash | LIVE |
| setting key vignette_enabled | 0x72e8f18c | fp/backend.lua:732 | name hash | LIVE |
| presentation switch | game+0x833440 `void(void *owner, uint32 avatar, uint8 enabled, uint8 1)` | fp/backend.lua:550-556 | the game's local presentation switch | fird-only |
| presentation pins | game+0x833440, 0x833513, 0x833540, 0x833763, 0x110fffe, 0x11109eb, 0x1110a43; exe+0x1fb540, 0x1fb574 (hex at the cited lines) | fp/backend.lua:530-538 | function contract of the presentation switch | fird-only |
| VirtualQuery page test | State 0x1000; Type 0x1000000, or 0x40000 under Wine with AllocationBase == module; Protect 0x20 / 0x40 / 0x80 | fp/backend.lua:445-449 | executable image page check before each native call | LIVE |
| ORPHAN_POLLS | 4 | fp/backend.lua:1396 | consecutive proofs before exiting a request whose weapon was destroyed | LIVE |
| stim mask | 64 | fp/backend.lua:1436 | controller mask during a stim; orphan exit is not forced | LIVE |
| placement limits | xyz +-0.5 m; angles +-90 degrees | fp/backend.lua:865, 872; fp/controller.lua:115, 122; fp/action_placement.lua:72, 75 | accepted placement range | LIVE |
| motion limits | dx,dy,dz +-0.15 m; quaternion norm within .002 of 1, component <= 1.001, w/len >= .5 (120 degrees) | fp/backend.lua:987-999 | accepted motion transform | LIVE |
| camera object +0x18 -> +0x28 | ptr, ptr | fp/runtime.lua:458, 490 | camera scene graph -> world matrix array; matrix 0 (64 bytes, 16 floats): rows at +0, +16 (forward, recorded as cam_fx/fy/fz), +32, position at +48 (cam_x/y/z) | LIVE (diagnostics, read-only) |
| rig local node array | ptr[rig object+0x80], stride 0x40, node 5, +0x24 position (12 bytes) | fp/runtime.lua:500 | rig node 5 local position (vm_lx/ly/lz) | LIVE (diagnostics) |
| rig world node array | ptr[rig object+0x88], stride 0x40, node 5, +0x30 position (12 bytes) | fp/runtime.lua:502 | rig node 5 world position (vm_cx/cy/cz in camera space) | LIVE (diagnostics) |
| weapon world root | ptr[weapon object+0x88], node 0, 64 bytes | fp/runtime.lua:506 | weapon root world matrix (gun_cx/cy/cz, gun_turn) | LIVE (diagnostics) |
| rig parent node | node 4 of the rig world array (graph+0x28, +4*0x40) | fp/locomotion.lua:135 | "sibling under root4" parent basis for motion | LIVE (other part) |
| rig node count | > 50 | fp/locomotion.lua:133 | rig sanity | LIVE (other part) |
| rig contract (comment) | branch5/9B115563 contains l_hand(13), r_hand(34), weapon attachment8; camera50 is a sibling under root4 | fp/profile.lua:36-38 | captured rig layout | comment |
| GUARD_HOLD | 0.75 s | fp/controller.lua:19 | no entry after the launcher guard | LIVE |
| entry delay / retry | 0.1 s / 0.25 s | fp/controller.lua:210, 456 | entry throttles | LIVE |
| controller default placement | x 0.15, y 0, z -0.12 | fp/controller.lua:22 | fallback placement | LIVE |
| helper error codes | 7, 8 = owner retiring; 14 = native sight node is zero | fp/controller.lua:429; fp/backend.lua:1149 | pose helper `stats.error` values the Lua interprets | LIVE |
| transition | duration .18, max_gap .25, max_step .06 (duration/3), merge_until .5 | fp/runtime.lua:670; fp/transition.lua:18-26 | presentation clock | LIVE |
| recovery | max_attempts 4, base_delay .25, max_delay 2, stable_seconds 2, proof deadline 10 s, proof retry .25 | fp/recovery.lua:25-27, 53, 100 | retry policy | LIVE |
| recovery native error | failure_native_error == 3 with failure_kind 'temporary_read' | fp/recovery.lua:15 | the only retryable failure | LIVE |
| diagnostics capacity | 720 (default 360, range 16..4096) | fp/runtime.lua:668; fp/diagnostics.lua:23-24 | ring size | LIVE |
| family baselines | rifle .15/0/-.12; compact .12/0/-.09; sidearm .105/0/-.08; marksman .14/.015/-.115; shotgun .14/.005/-.11; energy .15/0/-.115; support .16/.015/-.14 | fp/action_placement.lua:7-12 | per-family default placement | CONDITIONAL (family_placement_defaults, off as shipped) |
| facade defaults | placement .15/0/-.12; motion_enabled true; motion_scale 1; fird_head_camera 0/0/.04; camera_kick_enabled true | src/first_person.lua:9-11 | config used when the kit has no `first_person` table | LIVE |
| MCM ids | KIT 'goose_temporary_unlocks', NATIVE 'goose_native_ads_camera_test', control prefix 'first_person_' | src/first_person.lua:3-5 | menu registration names | LIVE |
| mouse buttons | 'right' (aim), 'middle' (aim-mode switch) via Mouse.button_index / button_id | fp/runtime.lua:398-419 | aim release edge for keep_aim_view | LIVE |
| kit C.stamp | 0x6AF322C1 | src/backend.lua:8 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.max_rows | 4096 | src/backend.lua:8 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.record_size | 0xB8 | src/backend.lua:8 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.index_size | 24 | src/backend.lua:8 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.account | 0x2E38230 | src/backend.lua:9 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.account_count | 0x1CE0 | src/backend.lua:9 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.records | 0x1CE4 | src/backend.lua:9 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.index | 0xB9CE4 | src/backend.lua:9 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.account_ready | 0xDDCF8 | src/backend.lua:10 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.player | 0x2CE1618 | src/backend.lua:11 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.network | 0x2E38228 | src/backend.lua:11 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.roster | 0x2E38210 | src/backend.lua:11 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.local_peer | 0xB398 | src/backend.lua:12 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.host_peer | 0xB3A8 | src/backend.lua:12 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.roster_stride | 0xC0 | src/backend.lua:12 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.roster_count | 8 | src/backend.lua:12 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.weapons | 0x2CE1BA0 | src/backend.lua:13 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.weapon_count | 199 | src/backend.lua:13 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.weapon_size | 32 | src/backend.lua:13 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.weapon_starts | 0x1BFC | src/backend.lua:14 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.weapon_counts | 0x1C18 | src/backend.lua:14 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.kits | 0x2CE16A8 | src/backend.lua:15 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.kit_count | 8 | src/backend.lua:15 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.kit_type | 0x28 | src/backend.lua:15 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.kit_read | 44 | src/backend.lua:15 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.stratagem_table | 0x31870E0 | src/backend.lua:16 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.stratagem_count | 154 | src/backend.lua:16 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.stratagem_buffer | 0x2E4A228 | src/backend.lua:17 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.stratagem_span | 81920 | src/backend.lua:17 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.stratagem_first | 0xD1D0C | src/backend.lua:18 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.stratagem_last | 0xD1D10 | src/backend.lua:18 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.category_index | 0xD1D48 | src/backend.lua:18 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.state | 0x14 | src/backend.lua:19 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.amount | 0xB0 | src/backend.lua:19 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit C.max_amount | 0x18 | src/backend.lua:19 | kit catalogue constant (unlocks, session, stratagems) | not used by the mode |
| kit fingerprint | game+0x160A102, hex 8b420c4c896c24784c8d2def5e9ffe418b848500c506028941108b420c894120 | src/backend.lua:22 | kit catalogue byte pin | not used by the mode |
| kit fingerprint | game+0x1079600, hex 48895c2408488b1d9485c60141bb010000004c8d8b001c00000f1f8000000000458b41 | src/backend.lua:23 | kit catalogue byte pin | not used by the mode |
| kit fingerprint | game+0x1206140, hex 48895c2408488bd985d275094c8d15f50df801eb0d8bc24c8d15820ff8014d8b14c2 | src/backend.lua:24 | kit catalogue byte pin | not used by the mode |
| kit fingerprint | game+0x12061c6, hex 418b00488b156020c3014869c8b8000000488d82e41c00004803c1 | src/backend.lua:25 | kit catalogue byte pin | not used by the mode |
| kit fingerprint | game+0x175EA0B, hex 837928017514448bc08bd3498bc9e8224712ff4c8b0d832c5801 | src/backend.lua:26 | kit catalogue byte pin | not used by the mode |
| kit fingerprint | game+0x175EA59, hex 837928027514448bc08bd3498bc9e8c44812ff4c8b0d352c5801 | src/backend.lua:27 | kit catalogue byte pin | not used by the mode |
| kit fingerprint | game+0x175EA9D, hex 3971287514448bc08bd3498bc9e8714a12ff4c8b0df22b5801 | src/backend.lua:28 | kit catalogue byte pin | not used by the mode |
| kit fingerprint | game+0x522B6F, hex 488b0db2569102488b9198b30000 | src/backend.lua:29 | kit catalogue byte pin | not used by the mode |
| kit fingerprint | game+0x1083a67, hex 48891dba67dc014883c304 | src/backend.lua:30 | kit catalogue byte pin | not used by the mode |
| kit gameplay pin (state) | game+0x3A1179, hex 488b0d8803940248bafd138a1d7b3bc55f4881c1d8fa0100 | src/backend.lua:352 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (state) | game+0x3A1AB3, hex 488b0d4efa930248bafd138a1d7b3bc55f4881c1d8fa0100 | src/backend.lua:352 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (state) | game+0x3AB394, hex 488b0d6d619302ba2b0000004881c160aabb044883c4205b | src/backend.lua:352 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (mode) | game+0x386E3B, hex 488b050eaa9502488bd9837808000f84900000008b4040ff | src/backend.lua:353 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (mode) | game+0x38701C, hex 488b052da895028378080074268b4040ffc883f806771c48 | src/backend.lua:353 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (mode) | game+0x38723D, hex 488b050ca69502837808000f84830000008b4040ffc883f8 | src/backend.lua:353 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (director) | game+0x308A0A, hex 488b0dbf949d0283b9800600000076574881c1d436050033 | src/backend.lua:354 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (director) | game+0x308BEB, hex 488b0dde929d0283b9800600000076514881c1d436050033 | src/backend.lua:354 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (director) | game+0x379E8B, hex 488b053e809602488bd983b88006000000740980b84c3705 | src/backend.lua:354 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (world) | game+0xBC783, hex 488b0d2e7ac2028b50084883c4205be97902db0033d2c34c | src/backend.lua:355 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (world) | game+0xBC835, hex 488b0d7c79c2028b5008e8cc01db00eb28488bcfe8d20b41 | src/backend.lua:355 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (world) | game+0xD0EBB, hex 488b0df632c1028b5008e946bbd900c34883ec20c381f97f | src/backend.lua:355 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (health) | game+0x391DF5, hex 488b0d3cfa94024533c04889b42488000000448b89381000 | src/backend.lua:356 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (health) | game+0x4AACBA, hex 4c8b15776b83027507b8ffffffffeb7e458b8a3810000045 | src/backend.lua:356 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (health) | game+0x4B5F25, hex 488b0d0cb98202448b89381000008b99401000000fafd841 | src/backend.lua:356 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (inventory) | game+0x3B0B00, hex 4c8b05e10d930233db8bcb458b4830458b5038440fafd241 | src/backend.lua:357 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (inventory) | game+0x3B3B69, hex 4c8b0d78dd920248896c24304889742440458b4130458b59 | src/backend.lua:357 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (inventory) | game+0x429EAC, hex 488b0d357a8b0241b903000000f30f59e8f3410f116d0849 | src/backend.lua:357 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (magazine) | game+0x3ADC09, hex 488b05e83b9302488b4850393c91400f9fc6b902000000e8 | src/backend.lua:358 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (magazine) | game+0x3ADC30, hex 488b05c13b9302488b4850833c9100400f9fc70f28c70f57 | src/backend.lua:358 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit gameplay pin (magazine) | game+0x3B0B85, hex 488b056c0c9302488b4850833c9100400f9fc6b901000000 | src/backend.lua:358 | kit freeze/gameplay byte pin (any one of a group must match) | not used by the mode |
| kit reserved stratagems | 0x33BABE96, 0xE4B4F558, 0x84DF23BE | src/backend.lua:37 | stratagem ids never unlocked | not used by the mode |
| kit gameplay world | game+0x2CE41B8 | src/backend.lua:382 | same world global as the mode's globals.world | not used by the mode (same address) |

**Camera and alignment helpers (C++ and `camera.lua`, `alignment.lua`)**

| name | value | defined at | what it is | live? |
|---|---|---|---|---|
| game.dll TimeDateStamp | `0x6af322c1` | cpp:1029; acpp:144; `src/first_person/profile.lua:5` | PE header stamp required for game.dll | live (camera) |
| game.dll SizeOfImage | `0x3382000` | cpp:1029; acpp:144; `profile.lua:6` | PE image size required | live |
| exe TimeDateStamp | `0x6af2d5e6` | cpp:1029; acpp:144; `profile.lua:94` (camera), `:27` (alignment) | helldivers2.exe PE stamp | live |
| exe SizeOfImage | `0x29fe000` | cpp:1029; acpp:144; `profile.lua:94`, `:27` | helldivers2.exe image size | live |
| `SETTER` | exe+`0x2dd5a0` | cpp:23,1030,1039 | Original CameraAPI+0x20 target (camera position setter) | live |
| SETTER prefix | `48895c2408574883ec60488bd9498bf88bcae81931dcff4c8b4b184c8bd04c6343208b4708498bd048c1e206498b4920` | cpp:1030 | 47 code bytes checked at exe+0x2dd5a0 | live |
| `CALL` | game+`0x110bff5`, bytes `41ff5120` | cpp:23,1030 | The game's `call [r9+0x20]` camera position call site | live |
| hook return address | game+`0x110bff9` (`CALL+4`) | cpp:1117, 750 | Only calls returning here are processed | live |
| call-pair pattern | game+`0x110bfe7` = `4c8d45c08b5308488b0b4c8b482041ff5120488b05c854bd014c8d45008b5308488b0b4c8b482041ff5128` | cpp:1220 | Position call followed by rotation call (CameraAPI+0x28); proves argument layout (rotation at position arg + 64 bytes) | live (SetPresentation47) |
| SDK root global | game+`0x2ce14c8` | cpp:468,1034; acpp:147,165; `src/first_person/backend.lua:1381` | Pointer to the table of engine API tables | live |
| CameraAPI table | `*(root+0x20)` | cpp:468,1034 | Camera API function table | live |
| CameraAPI set-position slot | api+`0x20` | cpp:468,1034-1035 | Slot swapped by the camera helper | live |
| CameraAPI set-rotation slot | api+`0x28` | cpp:471,677-679 | Called right after; not hooked, its argument is rewritten only in seated view / rotation ease | live (seated) |
| world-pose function | exe+`0x1f30e0`, bytes `40534883ec204863dae8e2d5eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3` | cpp:1031; `profile.lua:69` | Code fingerprint only (uses vtable +0xE8, stride 0x40, +0x28 worlds) | live (check only) |
| `GRAPH_GETTER` | exe+`0x28d3c0`, bytes `488d4160c3` | cpp:23,326-327,1032 | Unit vtable slot +0xE8: returns unit+0x60 (scene graph) | live |
| state-machine getter | exe+`0x28d510`, bytes `488b8178010000c3` | cpp:360-361,1032 | Unit vtable slot +0x1B0: returns `*(unit+0x178)` | live |
| camera context global | game+`0x2e287d0` | cpp:262; acpp:77; `profile.lua:8` | Pointer to the camera context | live |
| context+0x00 | camera pointer | cpp:263 | Engine camera object | live |
| context+0x08 | u32 camera unit ref | cpp:264; `backend.lua:391,1383` | Unit that carries the camera | live |
| context+0x0C | 11 floats: right [0..2], forward [4..6], up [8..10] | cpp:420-425 | Last rendered camera basis | live |
| context+0x180 | world pointer | `backend.lua:1383` | Argument of the detach call | live (seated) |
| context+0x1F8 / +0x1FC | u32 head / tail (< 32) | cpp:318; acpp:84 | Camera request ring indices | live |
| context+0x200, stride `0xE8`, 32 entries | request records | cpp:320; acpp:89,92; `backend.lua:1088` | Camera request ring; top = `((tail+31)%32)` | live |
| context+0x1F00 | default request record | cpp:320; acpp:89 | Used when head==tail | live |
| request record u32[0]&0xffff | kind (4 normal, 6 native first-person) | cpp:321; acpp:92,96 | Camera request kind | live |
| request record u32[1], [2], [3] | avatar, weapon, body | cpp:321-322; acpp:90,96 | Request owner fields | live |
| request +0x98 | float sight distance | `backend.lua:1357-1364` | Zeroed once by keep_aim_view | live |
| avatar owner global | game+`0x2ce1ee0` | cpp:265; acpp:78; `profile.lua:12` | Avatar table | live |
| avatar_owner+0x6C | u32 count (1..8) | cpp:267; acpp:80 | Number of avatars | live |
| avatar_owner+0x110 + i*8 | descriptor pointer (20 bytes; avatar @+8, body @+12) | cpp:268,272; acpp:81,86 | Per-avatar descriptor | live |
| avatar_owner+0x53e888 + i*0x1240 | 24 flag bytes; first u32 = role | cpp:269,371; acpp:82 | Per-avatar action flags | live |
| flags[12]&0x40 | fire | cpp:292 | Firing bit (stored only) | live (unused result) |
| flags[10]&4 and flags[15]&32 | shoulder aim | cpp:292 | Stored only | live (unused result) |
| flags[14]&4 | seated | cpp:293,299,306; acpp:87 | Seated interaction | live |
| flags[6]&1 | alignment refusal bit | acpp:87 | Not named | parked |
| role & 6 == 2 | combat role | cpp:376 | Bridge exception | live |
| avatar_owner+0x53e9c0 + i*0x1240 | u32 first-person rig (viewmodel) unit ref | cpp:485,499,532; acpp:83 | The 63-node rig the native camera links to | live |
| controller global | game+`0x2e381a8` | cpp:266; acpp:79; `profile.lua:9` | Controller record | live |
| controller bytes 0-3 | u32 action mask (0, 2 = swap, 0x20 = vault) | cpp:272,294-295 | Arming needs 0 or 2; fallback also 0x20 | live |
| controller+4 | aim-active byte | cpp:292,577; `backend.lua:1348,1350` | "full_ads"; cleared once by keep_aim_view | live |
| controller+5 | guard byte | cpp:293,306 | Blocks the head camera except during guard grace | live |
| unit registry global | exe+`0x1ab76f8` | cpp:270,402; acpp:67; `profile.lua:28` | Unit registry pointer | live |
| registry+0x88 / +0x98 / +0xA0 | units ptr / capacity u32 (<= 0x400000) / generations ptr | cpp:247,253-258; acpp:67-70 | Registry block | live |
| unit ref layout | index = ref & `0x3fffff`, generation = (ref>>22) & 0xff | cpp:246-248; acpp:66,70 | Generation-checked unit reference | live |
| unit+0x60 | scene graph object | cpp:409,663 | `*(camera+0x18)` must equal camera_object+0x60 | live |
| unit+0x70 | graph header: count u32 | cpp:327-328,501-502 | Node count (body <= 512; rig == 63) | live |
| unit+0x78 | external pose pointer | cpp:483,507 | Camera's link to the parent's pose table | live |
| unit+0x88 | world matrices pointer (stride 0x40, position at +0x30) | cpp:346-347,549 | Node world transforms | live |
| unit+0x98 | links pointer (u32 per node: low 16 type, high 16 parent index) | cpp:328,480,497,556 | Parent links | live |
| unit+0xA0 | name-hash pointer (u32 per node) | cpp:328,489 | Node name hashes | live |
| unit+0x1D0 | parent unit pointer (0 = unparented) | cpp:409,482,498,529,555,731; `backend.lua:1380` | Scene parent | live |
| unit+0x178 | state-machine instance | cpp:362 | Animation state machine | live |
| unit+0x358 | u64 resource id | cpp:361,404; `backend.lua:322` | Body unit resource | live |
| body resource id | `0x4D1C334D294DFA97` | cpp:361 | Known player body | live |
| instance+0x28 / +0x50 | graph definition / current-state array | cpp:362,368-369 | State machine data | live |
| g+4 == `31` | layer count | cpp:362 | Required | live |
| g+8 | table offset (12..0x10000) | cpp:363 | Layer table | live |
| layer index `13` | `table+4+13*4`, `array+13*8` | cpp:365,369 | Layer whose state decides on-foot | live |
| state `36` | `definition+12+36*4` | cpp:367,370 | On-foot mission state | live |
| state `3` | `definition+12+3*4` | cpp:373-377 | Ship bridge state (needs role and scene) | live |
| settings global | game+`0x2ce1508`, field +`0xac29c` == 3 | cpp:377,401 | Scene id for the bridge exception | live |
| head node hash | `0x8C5570C9` | cpp:336,340,728 | Head node of the body | live |
| head parent hash | `0xE9ECCA21` | cpp:342,730 | Parent of the head node; link low word must be 1 | live |
| camera+0x18 | scene graph pointer | cpp:409,663; `backend.lua:1379` | Camera's graph (unit+0x60) | live |
| camera+0x20 | u32 node index (0 when linked) | cpp:410,479,496 | Camera node | live |
| graph+0x10 / +0x20 / +0x28 | count / locals ptr / worlds ptr | cpp:410-413,664 | Camera graph tables (local position at +0x24, world at +0x30, stride 0x40) | live |
| camera link value | `0x00320002` | cpp:497 | Linked to parent node 50, link type 2 | live |
| rig node count | `63` | cpp:481,504,538 | First-person rig | live |
| rig camera node | index `50`, hash `0x60ED8C39`, link `0x40001` | cpp:490-491,505-506,509 | Node the native camera sits on (parent node 4, type 1) | live |
| weapon link type | low word `2` | cpp:556 | Weapon hanging from a body node | live |
| detach function | `*( *(root+8) + 0xF0 )` | `backend.lua:1381-1382` | Engine call that unlinks the camera unit | live (seated) |
| forward / up / side | `.18` / `.04` / `0` | `profile.lua:73`; cpp:905 | Eye offset from the head (m) | live |
| smoothing_ms | `100` | `profile.lua:73`; cpp:878 | Head low-pass time constant | live |
| blend_ms | `120` | `profile.lua:73`; cpp:887 | Blend from the game's camera | live |
| max_gap_ms | `250` | `profile.lua:73`; cpp:763,769,875 | History freshness limit | live |
| lag clamp | `.06` m | cpp:886 | Max smoothing offset | live |
| head distance window | `.05` .. `3.0` m | cpp:351 | Root-to-head sanity | live |
| pose gates | `8` m from head, `10` m from root | cpp:864 | Game position sanity | live |
| unparented tolerance | `.01` m | cpp:414 | Local vs world camera position | live |
| orthonormal tolerance | `.02`, cross·up > `.98` | cpp:241-243 | Basis check | live |
| horizontal minimum | `.0001` | cpp:897,901 | Vertical-look fallback | live |
| correction duration | `150` ms | cpp:957; `camera_correction.h:33,53` | Exit position handoff | live (rare) |
| `MAX_RESIDUAL_METRES` | `1.0` | `camera_correction.h:13` | Correction dropped beyond | live |
| `MAX_RELATIVE_COMPONENT` | `16.0` | `camera_correction.h:14` | Body-relative bound | live |
| eye hold limits | `1.0` m², `2500.0` ms, `300.0` ms, glide `.07` s, fade `.08` s, end `.002` m | cpp:919,931-932,936,944,946 | Eye hold | live |
| eye memory | 16 entries | cpp:86 | Per-weapon eye delta / aim position, kept across leases | live |
| anchor learning | `.005` m, `5` frames, `1.0` m² | cpp:790 | Stable aimed weapon position | live |
| anchor use | `50` ms full, `exp(-(age-50)/50.0)`, end `.02`, reject `.8` m | cpp:975,977,984 | Aim-release anchor | live |
| weapon key settle | `5` frames | cpp:798 | native_weapon_key | live |
| guard grace | `250.0` ms | cpp:828 | Tolerated guard byte | live |
| seated glide | `.18` s | cpp:1007 | Rotation follow in seated view | live |
| cut-hold mode | `15` (1+2+4+8) | `src/first_person/camera.lua:407`; `profile.lua:78-84`; cpp:1275 | exit, eye, anchor, seated | live |
| handoff | `entry_ms=0, rotation_ms=0` (max 500) | `profile.lua:76`; cpp:1250 | Eases off | off |
| rotation proof | `12` matches, `.25` deg, `abs(w)>.98` ignored, `120` deg max turn | `camera_handoff.h:89,108,112-113` | Rotation ease proof | off |
| free aim constants | `5` deg, `3` deg, `.22`, `.12` s, `.25` s | `free_aim_math.h:22-26` | Retired free aim | off |
| ObserveParent limit | `3000` ms | cpp:1182; `camera.lua:232` | Diagnostic window | debug/menu |
| profile count | `400` (max 1000) | `camera.lua:168`; cpp:1233 | Timing samples | debug |
| stats cadence | `.1` s, phase `.0333` s | `camera.lua:128` | Lua display counters | live |
| offsets bound | `.5` | `camera.lua:7`; cpp:1143 | Eye offset limit | conditional |
| motion bound | `.03` | `camera.lua:302` | Lua-side camera motion limit | parked |
| ABI sizes | Config `88`, Stats `80`, FreeStats `64`, Handoff49Data `152`, EntryTraceRow `80` | `native_ads_camera.h:10,24-25,46,81`; `camera.lua:75` | Struct sizes | live |
| address bounds | `65536` .. `0x800000000000`, read length <= `4096` | cpp:180,208 | Pointer sanity | live |
| alignment `POSITION` | exe+`0x1f25e0`, prefix `48895c24084889742410574883ec20498bf08bfa8bd9e8d5e0eaff4c8b5018b801000000f0410fc182380017008bc04c` | acpp:25,28 | Unit set-local-position target | parked |
| alignment `ROTATION` | exe+`0x1f2740`, prefix `48895c24084889742410574883ec20498bf08bfa8bd9e875dfeaff4c8b5018b801000000f0410fc182380017008bc04c` | acpp:25,29 | Unit set-local-rotation target | parked |
| alignment `CALLS` | game+`0x111a809` (`41ff5138`), game+`0x111a829` (`41ff5148`) | acpp:26,145 | The two game call sites filtered | parked |
| alignment returns | game+`0x111a80d`, game+`0x111a82d` | acpp:271; `profile.lua:32-33` | Expected return addresses (profile values are not passed to the DLL) | parked |
| alignment API table | `*(root+0x18)`, slots +`0x38`, +`0x48` | acpp:147-148 | Unit API position / rotation slots | parked |
| alignment duration | `10500` ms (max) | `profile.lua:25`; acpp:222 | Lease lifetime | parked |
| alignment ABI | Config `72`, Stats `72`, abi `1` | `native_ads_alignment.h:18-19`; `src/first_person/alignment.lua:35` | Struct sizes | parked |

**Editor/pose helper (C++ and `pose.lua`)**

| name | value | defined at | what it is | live? |
|---|---|---|---|---|
| game.dll TimeDateStamp / SizeOfImage | 0x6af322c1 / 0x3382000 | editor.cpp:308 | Build identity of game.dll | live |
| exe TimeDateStamp / SizeOfImage | 0x6af2d5e6 / 0x29fe000 | editor.cpp:308; profile.lua:52 | Build identity of helldivers2.exe | live |
| Unit API root | game+0x2ce14c8, then +0x18 | editor.cpp:313,333,628 | Pointer to engine API root; +0x18 = Unit API function table | live |
| Unit API slot: set local position | api+0x38 -> exe+0x1f25e0 | editor.cpp:73,76-77 | Hooked setter `(unit,node,const float*)` | live |
| Unit API slot: set rotation | api+0x48 -> exe+0x1f2740 | editor.cpp:73,76-77 | Hooked, trace only | live |
| Unit API slot: branch rotation | api+0x40 -> exe+0x1f2660 | editor.cpp:73,76-77 | Hooked "immediate local rotation" setter | live |
| Unit API slot: local position getter | api+0x00 -> exe+0x1f2310 | editor.cpp:73,76-77 | Hooked getter `(unit,node)->const float*` | live |
| Unit API slot: input getter | api+0x90 -> exe+0x1f30e0 | editor.cpp:629,634,638 | Hooked getter used by the look-input evaluator ("world_pose", profile.lua:69) | live |
| PREFIX[0..3] | see DLL block (4 hex strings) | editor.cpp:78-82 | First bytes of the four engine wrappers | live |
| CALLS[0] root_position_call | game+0x111a809, bytes 41ff5138; return 0x111a80d | editor.cpp:74,309,1502 | Native call that sets rig root node 0 position | live |
| CALLS[1] root_rotation_call | game+0x111a829, bytes 41ff5148 | editor.cpp:74,309 | Pinned only; calls forwarded | live (pin) |
| RIG_CALL pose_branch_position_call | game+0x111a460, bytes 41ff5138; return 0x111a464 | editor.cpp:75,310,1498; profile.lua:124 | Native call that sets node 5 position from request+0x48 | live |
| ROTATION_CALL | game+0x111b11b, bytes 41ff5140; return 0x111b11f | editor.cpp:75,310,1499 | Native call that sets node 5 rotation | live |
| OBSERVE_CALL | game+0x1119a81, bytes 41ffd0; return 0x1119a84 | editor.cpp:75,311,1500 | Native getter call for camera node 50; observation + input completion point | live |
| input return site | game+0x111863e | editor.cpp:1501 | Return address of the api+0x90 call that is normalised | live |
| GRAPH_GETTER | exe+0x28d3c0, bytes 488d4160c3 | editor.cpp:75,234-235 | Rig object vtable+0xE8 target (scene graph accessor) | live |
| RIG_NODE | 5 (hash 0x9B115563, parent 4) | editor.cpp:75,244 | Arms/weapon branch node that receives all offsets | live |
| node 0 | hash 0x4A182741, link 0 | editor.cpp:243 | Rig root ("carrier"); receives the depth counter-shift | live |
| node 1,2,3 | 0x7F30E61C / 0x32623EB2 / 0x975CEBBD; links 1, 0x10001, 0x20001 | editor.cpp:243-244 | Chain root -> node 4 | live (guard) |
| node 4 | hash 0xBCCF91E5, link 0x30001 | editor.cpp:244 | Parent of node 5 and of camera node 50 | live (guard) |
| node 7 | hash 0x6F3BD021, link 0x50001 | editor.cpp:245 | Child of node 5 | live (guard) |
| node 8 | hash 0x4FDD3337, link 0x70001 | editor.cpp:245; profile.lua:36-38 | Weapon attachment | live (guard) |
| node 10, 12, 13 | 0x03752C98 / 0x1CA2CD66 / 0x5C0C68D3; links 0x70001, 0xA0001, 0xC0001 | editor.cpp:246 | Left arm chain; 13 = l_hand | live (guard) |
| node 30, 32, 34 | 0x1620B2CE / 0x31E34DAC / 0x248004F5; links 0x70001, 0x1E0001, 0x200001 | editor.cpp:247 | Right arm chain; 34 = r_hand | live (guard) |
| node 50 | hash 0x60ED8C39, link 0x40001 | editor.cpp:248,898 | Camera node, sibling of node 5 | live |
| rig node count | 63 at object+0x70 | editor.cpp:235 | Only the 63-node rig is supported | live |
| rig object offsets | +0x70 count, +0x80 pose array, +0x98 links, +0xA0 hashes; vtable+0xE8 | editor.cpp:234-236,935-937,1001 | Scene graph layout of the rig unit object | live |
| pose array root xyz | poses+0x24 (3 floats) | editor.cpp:937 | Node 0 position as stored; checked before root restore | live |
| unit registry | exe+0x1ab76f8; +0x98 capacity, +0xA0 generation bytes, +0x88 object pointers | editor.cpp:186-189; profile.lua:53 | UnitRef (index bits 0..21, generation bits 22..29, max 0x400000) -> object | live |
| camera context global | game+0x2e287d0 | editor.cpp:196,768 | Pointer to the camera context; `*context` = camera | live |
| camera ring | context+0x1F8 head, +0x1FC tail, records context+0x200 + i*0xE8, 32 entries, empty-top context+0x1F00 | editor.cpp:204,217,344-345,770-773 | Ring of camera requests | live |
| request header | u32[0]&0xffff kind (4 normal, 6 first person), u32[1] avatar, u32[2] weapon, u32[3] body | editor.cpp:218-225 | Camera request record | live |
| request+0x18 | u32 sight node, must be non-zero | editor.cpp:1061,1547 | Selected sight node | live |
| request+0x3c | float[3] | editor.cpp:682,802 | Filtered look-input history | live |
| request+0x48 | float[4] | editor.cpp:1050 | Source of node 5 position; identifies the owned record's write | live |
| request+0x80 | u32 | editor.cpp:555-556,682,867 | Look preset index ("DWORD80"); the only game field written by input normalisation | live |
| request+0x84 | float | editor.cpp:683,506 | Lens angle in radians | live |
| request+0x88 | float | editor.cpp:504 | Depth mix; excluded from the stability compare | live |
| request+0x8C | u8 | editor.cpp:683 | Pitch gate | live |
| request+0x8D, +0x8F | u8 <= 1; u8 == 0 | editor.cpp:505 | Optic mode bytes; +0x8D selects gain = mix | live |
| request+0x90 / +0x9C / +0xA0 | float current distance / previous distance / prior lens | editor.cpp:503 | Native scope distance state | live |
| request+0x98 | float, must be 0.0 | editor.cpp:347-350,505 | Sight coupling; non-zero disables rotation and depth | live |
| caller stack +0x44.. (0x20 bytes) | +0x44 u8==0, +0x45 u8<=1, +0x5C target lens, +0x60 current lens | editor.cpp:902,504-506 | Evaluator locals read at the node 50 getter | live |
| caller stack +0x240 | pointer equality | editor.cpp:970 | Evaluator local holding the root position; quaternion at -0x10 | live |
| caller stack +0x80, +0x48, +0xa0 | u64 context, u64 avatar index, float dt_eval | editor.cpp:679-680 | Evaluator locals at the api+0x90 call | live |
| avatar owner global | game+0x2ce1ee0 | editor.cpp:197 | Avatar owner object | live |
| avatar owner offsets | +0x6C count (1..8), +0x110+i*8 descriptor, +0x53e888+i*0x1240 flags[24], +0x53e9c0+i*0x1240 viewmodel UnitRef, +0x53e9ac+i*0x1240 steering float[2], +0xa7c24+i*0xa7aec raw look float[3], +0xf8 avatar map | editor.cpp:199-202,666-667,681 | Per-avatar records | live |
| flags[6]&1 | "bit 48" | editor.cpp:213 | Native aiming (camera lifecycle) | live |
| flags[10]&4 and flags[15]&32 | "bits 82 and 125" | editor.cpp:214 | Shoulder aim (computed, not branched on) | live (unused result) |
| flags[14]&4 | must be 0 | editor.cpp:208 | Blocking flag | live |
| controller global | game+0x2e381a8; byte 4 = full ADS; bytes 0,1,2,3,5 must be 0 | editor.cpp:198,209,215 | Native aim controller | live |
| descriptor layout | 20 bytes; u32@+8 id, u32@+12 unit | editor.cpp:206-207 | Avatar and weapon component descriptors | live |
| weapon resource id | 8 bytes at weapon_object+0x358 | backend.lua:845 | Per-weapon key for saved placement | live |
| depth globals | game+0x2ce1ea0 pool, game+0x2ce41b8 world, game+0x2ce1508 settings | editor.cpp:384-385,396-397 | Weapon data pool, world, settings | live |
| pool block | pool+0x30, 0x88 bytes: map A at +0, +0x18 descriptors, +0x28 parameters, +0x30 packed, map B at +0x40, +0x80 override data | editor.cpp:397-430 | Weapon component pool layout | live |
| parameters row | parameters + wi*0x3F0 + 0x350 (16 bytes, exactly one u32 == 1) | editor.cpp:430,434 | Optic parameter block | live |
| packed row | packed + wi*12 + 4; zoom index = (bits>>4)&3 | editor.cpp:430,435 | Selected zoom index | live |
| scope record size | 0x4D8 | editor.cpp:407,426 | Size of an override / resource scope record | live |
| resource table | world+0xf146a0; 746 buckets of 16 bytes; data at table+0x2EA0 | editor.cpp:410-426 | Weapon resource -> scope record | live |
| scope record fields | +0x74 zoom[3]; +0x13C coefficient[3]; +0x148, +0x14C near terms | editor.cpp:433-441 | Authored zoom values and distance coefficients | live |
| third-person FOV setting | settings+0xac488 (float degrees, 10..170) | editor.cpp:390,433,686 | Used by depth reference and input ratio | live |
| depth pins (9) | see DLL block | editor.cpp:449-457 | Byte pins for the depth path | live |
| carrier pins (3) | game+0x1118375, 0x111a7cb, 0x111a814 | editor.cpp:975-977 | Byte pins for the root counter-shift | live |
| INPUT_PINS (16) | see DLL block table | editor.cpp:585-601 | FNV-1a 64 code fingerprints ("16 code pins") | live |
| FNV-1a 64 parameters | basis 0xcbf29ce484222325, prime 0x100000001b3 | editor.cpp:604,608 | Hash used for the pins | live |
| input float constants | game+0x22465c4, 0x2247894, 0x2246fb0, 0x22478c8, 0x2246774, 0x2247ab0; double game+0x2246900 | editor.cpp:613-617 | 1, -1, 57.2957763671875, -pi/2, +pi/2, -75; 1.0 | live |
| native exp | game+0x1f8ff70 (double(double)) | editor.cpp:842 | Game's own exp, called by the helper | live |
| look preset table | game+0x2cb2140, 144-byte rows, index <= 255; gains at +4 (yaw), +8 (pitch); +0x8C must not change | editor.cpp:692-694,852-854 | Native look presets | live |
| input owner | game+0x2e38250; +0x324 device (1,2 = pad); +0xa7af8 flags[0x5c]; sensitivity float at flags+0x4c (pad) / +0x58 (mouse) | editor.cpp:684-700 | Input settings | live |
| time global | game+0x2ce14f8 -> float dt | editor.cpp:687 | Global frame time | live |
| context+0x1a9 | u8 must be 0 | editor.cpp:680 | Look inhibit | live |
| context+0x15c | float[2] | editor.cpp:688 | Look context offsets | live |
| steering lookups | game+0x2ce1618 (+0x88, +0x3a8), game+0x2e3fcb8, game+0x2e3fa9c, game+0x2e3f0e4, world+0xf24990, world+0xf349e8 + row*24, entity 0x7fff | editor.cpp:653-668 | Resolves the controlled avatar for steering | live |
| clone region | first free 0x10000 block above table, searched up to table+0x9000000000, 4096 tries, 32 clones | editor.cpp:564,723-725 | Private preset copies | live |
| SLOT_COUNT | 4 | editor.cpp:17 | Hooked setter/getter slots (plus optional +0x90) | live |
| Config ABI | size 104, abi 2; Stats 80; DepthStats 104 abi 1; InputStats 96 abi 1 | editor.h:32-35; pose.lua:79,166,196; profile.lua:39 | Structure contracts | live |
| duration_ms limit | 30500; 0 = no deadline | editor.cpp:1399; editor.h:7; profile.lua:45 | Lease duration bound | bound live; shipped value sent is 0 |
| blend_ms | 150 (limits 1..1000) | profile.lua:46; editor.cpp:1402 | Helper's own hip/ADS smoothstep time | live |
| default placement | x 0.15, y 0, z -0.12 | profile.lua:48-50 | Fallback node 5 offset in parent space | live (fallback) |
| placement limit | 0.5 m per axis | editor.cpp:1403,1523; pose.lua:256,267 | Bound for placement and ADS placement | live |
| motion limit | 0.15 m per axis | editor.cpp:1523; pose.lua:280 | Bound for motion translation | live |
| rotation limit | w >= 0.5 (120 degrees); norm tolerance 0.002 | editor.cpp:1529,1534; pose.lua:291 | Bound for the motion quaternion | live |
| native quaternion tolerances | 0.02 input, 0.025 product, 0.002 carrier | editor.cpp:1319,1329,1001 | Norm checks | live |
| placement angle limit | 90 degrees | backend.lua:872 | Yaw/pitch/roll bound (Lua only) | live |
| shared phase validity | 250 ms | phase.h:20 | Max age of a published phase | live |
| ENTRY_GUARD_MS | 300 | editor.cpp:1165 | Entry rotation guard window | live |
| ENTRY_GUARD_COSINE_HALF | 0.99991 (1.5 degrees) | editor.cpp:1166 | Hold threshold | live |
| ENTRY_GUARD_STILL | 0.999998 (0.25 degrees) | editor.cpp:1167 | "Stopped changing" threshold; 2 in a row release | live |
| ENTRY_GUARD_STABLE / SAME | 8 / 0.99999 | editor.cpp:1168 | Writes needed to adopt a settled reference | live |
| rotation trace rows | 64 | editor.h:39 | Trace capacity | live (written, never read) |
| entry position cache | 16 entries | editor.cpp:1176 | Remembered node 5 positions per (viewmodel, weapon) | live |
| entry_animation_ms / drop / back | 220 / 0.35 / 0.06 | editor.cpp:1018-1020 | Draw animation defaults | live |
| depth timers | 500 ms recheck, 500 ms retry | editor.cpp:521,533,535 | Weapon data cache | live |
| depth bounds | lens 0.0002..2.96; distance abs <= 4; delta abs <= 1; angle 0.0001..1.48; rad 0.01745329238474369; epsilon 1.1920928955078125e-7 | editor.cpp:460,465,507-511,549 | Depth math guards | live |
| input retry | 250 ms | editor.cpp:621 | Pin re-check interval | live |
| gain constants | MIN_GAIN 0.000001, MAX_GAIN 1000000, ratio 1/4096..4096, MIN_CONDITION 1/4096, ABS 0.0000002, REL 0.000002, MAX_INCREMENT_ERROR 0.000002, PITCH_ADD_BOUND 0.0000002384185791015625 | gain.h:13-20 | Input math bounds (PITCH_ADD_BOUND is declared, not used) | live |
| input ratio bounds | 1/4096..16; RAD_TO_DEG 57.2957763671875; MAX_COMPONENT 1000000 | zin.h:18-20 | Input math bounds | live |
| filter constant | -75 | gain.h:87 | `a = 1-exp(-75*dt)` | live |
| pitch clamp | +/-1.5707963705062866 | gain.h:63 | `pitch_after_base` (declared, not called from editor.cpp) | not live |
| MXCSR requirement | (csr&0x6000)==0 and (csr&0x1f80)==0x1f80 | gain.h:79 | Round-to-nearest, exceptions masked | live |
| stats throttle | 0.1 s, phase 0.085 s | pose.lua:130 | Counter conversion cadence in Lua | live |
| error codes | 1..18 | editor.cpp:83-86; pose.lua:9-21 | Native error enum | live |
| input reasons | 0..12, 0x100+n | editor.cpp:557-559,848 | Input status codes | debug |
| NADSP constants | duration <= 10500; two slots | pose.cpp:27-29,322 | Old helper | not live |

**FOV, blur, near, held-fire, zoom, guarded reads (C/C++ and their Lua adapters, `native_ads_option.lua`, `src/memory.lua`)**

| name | value | defined at | what it is | live? |
|---|---|---|---|---|
| game.dll TimeDateStamp | 0x6af322c1 | native/first_person/native_ads_fov.cpp:467; native_ads_blur.cpp:305; native/near_clip/native_ads_near.cpp:168; native/held_fire/native_held_fire.cpp:109; native/zoom/native_ads_zoom.cpp:109; native/memory/guarded_reads.c:9; src/memory.lua:12 | PE timestamp required for game.dll (build 26568081) | live |
| game.dll SizeOfImage | 0x3382000 | same lines; src/memory.lua:273,300 | PE image size required | live |
| helldivers2.exe TimeDateStamp | 0x6af2d5e6 | native_ads_fov.cpp:467; native_ads_blur.cpp:305; native_ads_near.cpp:168; native_held_fire.cpp:109; native_ads_zoom.cpp:109; src/first_person/profile.lua:52,59 | PE timestamp required for the exe | live |
| helldivers2.exe SizeOfImage | 0x29fe000 | same lines | PE image size required | live |
| address bounds | 65536 .. 0x800000000000 (140737488355328) | native_ads_fov.cpp:212; guarded_reads.c:4; src/memory.lua:11 | lowest/highest user address accepted for any read | live |
| game+0x2e287d0 | global | native_ads_fov.cpp:269; native_ads_blur.cpp:137; native_ads_near.cpp:130; native_held_fire.cpp:169 | pointer to the camera context (`ctx`); `[ctx+0]` = camera object | live |
| ctx+0x1F8 / ctx+0x1FC | u32 head / u32 tail | native_ads_fov.cpp:306; native_ads_blur.cpp:172; native_ads_near.cpp:133 | camera request ring indices, each < 32 | live |
| ctx+0x200, stride 0xE8, 32 rows | struct | native_ads_fov.cpp:307; native_ads_near.cpp:134 | queued camera request rows; current = `(tail+31)%32` | live |
| ctx+0x1F00 | struct | native_ads_fov.cpp:307 | base camera request record used when head==tail | live |
| request row u32[0]&0xffff | 4 / 6 | native_ads_fov.cpp:313-315 | camera kind: 4 normal, 6 weapon first-person camera | live |
| request row u32[1], [2], [3] | avatar, weapon, body ids | native_ads_fov.cpp:308, 315 | ownership fields of a request row | live |
| request+0x3C | float radians | native_ads_fov.cpp:344, 474-475 | kind-4 request's published lens used by the input pass (Fird policy rewrites it) | parked |
| request+0x84, +0x90, +0x9c, +0xa0 | floats | native/zoom/native_ads_zoom.cpp:223-229 | request lens, authored depth, prior depth endpoint, prior lens endpoint | parked |
| ctx+8 | u32 | src/first_person/backend.lua:391 | camera unit id (seen in `camera_config`, outside this part) | live (other part) |
| game+0x2ce1ee0 | global | native_ads_fov.cpp:270; native_ads_near.cpp:131 | pointer to the avatar owner | live |
| avatar_owner+0x6C | u32 | native_ads_fov.cpp:272 | avatar count (1..8) | live |
| avatar_owner+0x110 + index*8 | pointer | native_ads_fov.cpp:273 | avatar descriptor; 20 bytes; +8 avatar id, +12 body unit id | live |
| avatar_owner+0x53e888 + index*0x1240 | 24 bytes | native_ads_fov.cpp:274; native_ads_near.cpp:138 | per-avatar flag bytes | live |
| flags[10]&4 and flags[15]&32 | bits 82 and 125 | native_ads_fov.cpp:287-290; native_ads_near.cpp:139-140 | "normal aim is the agreeing 82/125 pair" (shoulder aim) | live |
| flags[14]&4 | bit | native_ads_fov.cpp:291; native_ads_blur.cpp:157 | seated/unsafe flag; must be clear for `safe` | live |
| flags[6]&1, flags[9]&4, flags[15]&2 | bits | native_held_fire.cpp:175 | extra action bits refused by the held-fire filter | parked |
| avatar_owner+0x40 → settings; settings+0xf0 | pointer; u32 0..3 | src/first_person/native_ads_option.lua:10 | game aim-mode setting selecting per-weapon (1,3) or per-input (0,2) preference storage | conditional |
| game+0x2e381a8 | global | native_ads_fov.cpp:271; native_ads_near.cpp:136 | pointer to the camera controller; 6 control bytes read | live |
| controls[0..3] (mask) | 0 or 2 allowed | native_ads_fov.cpp:291 | camera inhibit mask | live |
| controls[4] | byte | native_ads_fov.cpp:289; native_ads_near.cpp:137 | full native ADS active | live |
| controls[5] | byte | native_ads_fov.cpp:291 | unknown controller guard; must be 0 | live |
| exe+0x1ab76f8 | global | native_ads_fov.cpp:275; native_held_fire.cpp:158; profile.lua:53 | unit registry pointer | live |
| registry+0x98 / +0xA0 / +0x88 | u32 capacity / generations ptr / units ptr | native_ads_fov.cpp:275-277 | unit registry layout | live |
| body id split | index = id&0x3fffff, generation = id>>22 | native_ads_fov.cpp:268, 276 | unit handle encoding | live |
| game+0x2ce14c8 | global | native_ads_fov.cpp:488; native_ads_near.cpp:178; native_ads_blur.cpp:324; native_held_fire.cpp:120 | engine API root; +0x18 unit API table, +0x20 CameraAPI, +0xC0 hash API | live |
| CameraAPI+0x80 / +0x78 | slot / getter | native_ads_fov.cpp:488-490 | set-FOV slot (hooked) and get-FOV | live |
| CameraAPI+0x68 / +0x58 | slot / getter | native_ads_near.cpp:178-180 | set-near slot (hooked) and get-near | live |
| exe+0x2dd080 (SETTER), bytes f30f114934c7415000000000e92f6c1f00 | RVA | native_ads_fov.cpp:21, 468 | original FOV setter; writes float at camera+0x34 | live |
| exe+0x2dd070 (GETTER), bytes f30f104134c3 | RVA | native_ads_fov.cpp:21, 469 | FOV getter (camera+0x34) | live |
| game+0x110bfda (CALL), bytes ff9280000000; return address +6 | RVA | native_ads_fov.cpp:21, 468, 665 | the only caller of the FOV slot the policy acts on | live |
| game+0x2ce1508 → settings; +0xac488 | float degrees 1..179 | native_ads_fov.cpp:431; native_ads_zoom.cpp:200 | live third-person FOV setting (hip FOV source) | live |
| settings+0xac29c | u32 3 or 4 | native_held_fire.cpp:190 | "scene" value checked by the held-fire filter | parked |
| RAD_PER_DEG | 0.01745329251994329577f | native_ads_fov.cpp:22 | degree→radian factor | live |
| shoulder magnification | 1.05 (1+.05f*w) | native_ads_fov.cpp:102, 123 | 5 % focal magnification in normal aim | live |
| shoulder tween | .12 s step, rebase outside 0..0.25 s | native_ads_fov.cpp:114-115 | local 120 ms tween (used only without a shared phase) | live |
| shared phase staleness | .25 s | native_ads_fov.cpp:132 | a SetTransition older than this is discarded | live |
| reserved (ads_blend_ms) | 180; allowed 0 or 80..500 | profile.lua:57; fov.lua:55; native_ads_fov.cpp:607 | ADS handoff duration in ms | live |
| lens validity | 0 < radians < 3.14159265 | native_ads_fov.cpp:160 | handoff target sanity | live |
| captured lens range | 10..170 degrees | native_ads_fov.cpp:321-323 | Fird captured lens | parked |
| game+0x110b4db, +0x111e3ce, +0x111ba5e, +0x111ba70, +0x111ba91, +0x111baa2 | byte pins | native_ads_fov.cpp:476-481 | Fird input contract (evaluator dispatch, lens publication, input scaling) | parked |
| game+0x2ce1618 | global | native_ads_blur.cpp:316; native_ads_option.lua:9 | blur: root whose +0x18 is the post-effect system; native-aim: local player/input object (+0x88 count, +0xd0 map, +0xe8 selected, +0x2ec + index*0x38 preference byte) | live (blur); conditional (option) |
| system+0xDC8 / +0xDD8 / +0xDE8 / +0x348+index*0x30 | pointers / u32 entity | native_ads_blur.cpp:317-322, 256 | provider object, query table, setter table, post-effect entity rows | live |
| exe+0x1715c40 | vtable | native_ads_blur.cpp:317 | provider vtable; +0x38 = exe+0x66a170, +0x28 = exe+0x2ad330 | live |
| exe+0x2873ac0, exe+0x2873b18 | data tables | native_ads_blur.cpp:320-322 | query table (+0x28 = exe+0x2df6b0) and setter table (slot at +0, +8 = exe+0x2df7b0) | live |
| exe+0x2df740 (SETTER) | RVA + 47-byte pin | native_ads_blur.cpp:20, 307 | original parameter setter (provider, packed instance, hash, variant) | live |
| exe+0x2df6b0, exe+0x66a170, exe+0x2df830, exe+0x669480 | byte pins | native_ads_blur.cpp:308-311 | pack helper, component lookup, hash thunk, hash function | live |
| game+0xe9ac4b (CALL), bytes 41ff13; return address +3 | RVA | native_ads_blur.cpp:20, 306, 464 | the only caller of the parameter setter the filter acts on | live |
| DOF_CLASS | 0xF7A3101F | native_ads_blur.cpp:25 | component class hash of the weapon DoF component | live |
| HASHES | 0xA7990477, 0xF07FC12E, 0x209F72CF | native_ads_blur.cpp:25 | the three DoF maxima parameter hashes (near / far / far-close per build_blur.py:32) | live |
| variant layout | 24 bytes, u32 tag at +0 (2 = float), float at +8 | native_ads_blur.cpp:358, 382-385 | engine Variant passed to the setter | live |
| game+0x2ce1bf8 → pool; +0x278/+0x280/+0x284/+0x288/+0x290/+0x298 | map + arrays | native_ads_blur.cpp:183-186 | weapon parameter pool: buckets, cap, empty, mult, descriptors, rows | live |
| provider +0x90/+0x94/+0x98/+0xA8/+0xAC/+0xB8/+0xBC/+0xC8/+0xD0/+0xD8/+0xE8 | fields | native_ads_blur.cpp:207-211, 245-246 | component map and id/class/chain/blob arrays ("Exact EXE62ADB0 semantics") | live |
| bucket sentinels | 0xFFFFFFFE empty, 0x7FFFFFFF end | native_ads_blur.cpp:215, 217 | component map chain markers | live |
| blob +4 / +0x10 / +0x10+count*4+i*16 | count / keys / values | native_ads_blur.cpp:231, 236 | component property blob layout | live |
| NO_SOURCE | 18 | native_ads_blur.cpp:24; src/first_person/blur.lua:7 | weapon has no scoped blur source | live |
| RETIRED codes | 7, 8 | blur.lua:8 | owner/unit mismatch treated as quiet retirement | live |
| SOURCE_RECHECK_SECONDS | .5 | blur.lua:9 | recheck interval for a weapon without a source | live |
| exe+0x2dd050 (SETTER), bytes f30f114928e9c66b1f00 | RVA | native_ads_near.cpp:31, 171 | original near setter; writes float at camera+0x28 | live |
| exe+0x2dd030 (GETTER), bytes f30f104128c3 | RVA | native_ads_near.cpp:31, 172 | near getter | live |
| game+0x110ddec (SITE) | 34-byte pin | native_ads_near.cpp:31, 170 | pinned near-distance call site (`call [rdx+68]`) | live |
| game+0x2245efc (NATIVE_NEAR), bytes cdcc4c3d | .rdata | native_ads_near.cpp:31, 171 | the game's near constant 0.05 | live |
| NATIVE_BITS | 0x3d4ccccd | native_ads_near.cpp:32 | bit pattern of 0.05f | live |
| TEXT / TEXT_END | 0x1000 / 0x1f99000 | native_ads_near.cpp:31 | game.dll text range accepted as caller | live |
| MAX_METERS / GAME_METERS | 1.0f / .05f | native_ads_near.cpp:33-34 | max accepted distance; game near distance | live |
| EASE_GAP_US / EASE_STEP_US / EASE_TAU_US / EASE_SNAP_METERS | 250000 / 50000 / 45000.0f / .002f | native_ads_near.cpp:98-99 | near-distance easing constants | live |
| near call sites (comment) | game+0x110DE0B, 0x111090F, 0x1107CF0, 0x1144ad1 | native_ads_near.cpp:4-5; profile.lua:62 | sites that load the 0.05 constant | live (informational) |
| camera+0x28 / camera+0x34 | floats | native_ads_near.cpp:312; setter bytes | near distance / FOV radians inside the camera object | live |
| near menu values | global 15 cm (5..40), aim toggle off → 5; per-weapon defaults 15 / 5 | src/first_person/menu.lua:11, 144-149, 398-406 | centimetres; 5 means "game distance" and is sent as 0 | live |
| near retry / poll | 1 s / .25 s | src/first_person/near_clip.lua:205, 196 | Begin retry and health poll periods | live |
| stats refresh grid | .1 s (fov), .1 s offset .0667 (blur) | fov.lua:91; blur.lua:100 | display counter refresh | live |
| game+0xa5cf03, +0xa5cff5, +0xa5d0b3 | byte pins | native_ads_option.lua:29-30 | native aim selection code contract | conditional |
| game+0x2ce1ea0 → pool; +0x30 map, +0x48 descriptors, +0x50 byte pairs (index*2) | global | native_ads_option.lua:13-17 | per-weapon aim preference storage | conditional |
| exe+0x28703e0 (API_RVA) +0x370 | slot | native_held_fire.cpp:20, 120-121 | unit API table and its animation-event slot | parked |
| exe+0x1f77c0 (EVENT_RVA) | RVA + pin | native_held_fire.cpp:20, 110 | original event function (unit, hash) | parked |
| START / END | 0x6bd9c5e3 / 0x8bcd9161 | native_held_fire.cpp:19 | hip-fire start/end event hashes | parked |
| callers | game+0x83c3a6, +0x83c87c, +0xa6bf00 | native_held_fire.cpp:216-217 | return addresses whose events are dropped | parked |
| pins | exe+0x12bf65, game+0x83c37f, +0x83c782, +0x83c842, +0xa6beb1 | native_held_fire.cpp:111-115 | held-fire code contract | parked |
| body object +8 / +0x18 / +0x70 / +0x178 / +0x358 | unit id / world / node count 107 / state-machine instance / resource 0x4d1c334d294dfa97 | native_held_fire.cpp:161-163; held_fire.lua:153-155 | player body unit layout | parked |
| instance+0x28 / +0x50; graph+4 = 31, +8 offset, +0x14 = 54 | fields | native_held_fire.cpp:145-147, 163-164, 184 | animation state machine graph and active-state array | parked |
| state ids | layer 4 state 59/78, layer 24 states 1,6/10, layer 25 state 1/4, layer 14 states 36,3, layer 3 states 71,68, layer 1 states 75,78 | native_held_fire.cpp:185-193; held_fire.lua:160-161 | aimed-target / sprint states | parked |
| TTL | 250 ms max | native_held_fire.cpp:134; held_fire.lua:185 | held-fire lease lifetime | parked |
| game+0x1608d30 (SELECTOR) | RVA + pin | native_ads_zoom.cpp:27, 114 | optic selection getter (inline-hooked) | parked |
| game+0x110ab80 (EVENT), game+0x764e90 (ZOOM) | RVAs + pins | native_ads_zoom.cpp:27, 119-120 | zoom event and magnification getter | parked |
| game+0x764f29, +0x110adcf (return +5: 0x764f2e, 0x110add4), +0x1118891 | pins | native_ads_zoom.cpp:117-121, 151 | selector call sites and sensitivity ratio | parked |
| hashmap header | {u64 buckets, u32 cap, u32 empty, u32 mult}, rows 8 bytes, max 4096 probes, cap ≤ 1048576 | guarded_reads.c:7; src/memory.lua:13, 313-320; src/first_person/backend.lua:132-150 | engine open-addressed id map | live |
| roster | 8 rows, stride 192 | guarded_reads.c:8 | session roster scan | live (kit, not the mode) |
| MAX_READ / MAX_WRITE | 8192 / 4096 | src/memory.lua:13 | read and write size limits | live |
| write region test | state 0x1000, type 0x20000, protection 4 | src/memory.lua:156 | committed private read-write only | conditional (via option) |
| game+0x92F690, game+0x5E1720 | function RVAs + pins | src/memory.lua:354-362, 371-379 | health refresh and objective completion calls | live for the kit, not the mode |
| port51/hd2_nativeinfo_f375fc4798c33ad5.dll | file | src/first_person/compat51.lua:7, 158 | userdata/function address bridge for the cffi host | live only when a userdata is cast |
| Lua 5.1 CClosure layout | type byte 6 at +8, isC byte 1 at +10, entry at +32 | compat51.lua:121-126 | fallback C-function address reader | not reached by these adapters |

**Head visibility, movement, DoF, vignette, camera sharing, model shadows, arm pieces, grip hand**

| name | value | defined at | what it is | live? |
|---|---|---|---|---|
| SELECTED slots | slot 0 = head, 2 = torso, 8 and 9 = shoulder | hv:5-6 | Appearance slots whose renderers are hidden | live |
| kept slots | 3,4,5,6,7 (3..9 with full_body+hide_torso) | hv:415 | Limb slots never hidden; meshes shared with them block hiding | live (second list dead) |
| pass mask default | 1, name `'default'` | hv:7 | Camera pass bit and pass name given to `Unit.set_mesh_visibility` | live |
| pass mask shadow | 2, name `'shadow_caster'` | hv:7-9 | Shadow pass bit and pass name | live |
| TORSO_SETTLE | 0.6 s | hv:11 | Rig-less steady time before the show-torso option applies | live only with menu `show_torso` on |
| RIG_RESOURCE / RIG | bytes ED 8B A0 3C 49 4D CF D0 (`'\237\139\160\060\073\077\207\208'`) | hv:12, ms:4 | Resource id of the known first-person rig, 8 bytes at unit `+0x358` | live |
| RIG_PADS | `g_l_shoulderpad` hash 0x44E19D12 node 57; `g_r_shoulderpad` hash 0xCB0E1712 node 58 | hv:13 | Rig shoulder-pad meshes hidden; must have 1 material and `m_armor_standard` | live |
| rig mesh count | 5 | hv:339 | Known rig mesh layout | live |
| material name | `m_armor_standard` | hv:195 | Material the pads must carry | live |
| exe+0x3f4ca0 | 8 bytes `488bd1e9a8f8ffff` | hv:113 | set_mesh_visibility binding fingerprint | live |
| exe+0x51fe80 | 14 bytes `40574883ec30440fb689dc000000` | hv:114 | Mesh pass-mask layout fingerprint (mask at +0xDC) | live |
| exe+0x3f4c23 | 24 bytes `3b3d970e48027504b101eb363b3d930e48027504b102eb2a` | hv:115 | Camera/shadow visibility context fingerprint | live |
| exe+0x51feaa | 26 bytes `84d27405450ac8eb0641f6d04522c8488b5908448889dc000000` | hv:116 | Same | live |
| exe+0x2875ac0 | u32 0xF2760503 | hv:117 | Visibility context constant | live |
| exe+0x2875ac8 | u32 0x3100C3D2 | hv:117 | Visibility context constant | live |
| exe+0xa06d0 | 22 bytes `48895c24084889742410574883ec20488b351270a101` | hv:119 | Unit registry accessor fingerprint | live |
| exe+0x1ab76f8 | pointer | hv:123; profile.lua:28 (`unit_registry`) | Unit registry | live |
| registry +0x88 | 32 bytes: objects ptr +0, capacity u32 +16, generations ptr +24 | hv:123-126; be:789-793 | Registry fields | live |
| unit ref split | index = ref % 0x400000; generation = floor(ref/0x400000) % 256 | hv:131; be:787-788 | Unit ref encoding | live |
| unit +0x00 | vtable | hv:139 | Renderer type | live |
| unit +0x08 | u32 self ref | hv:138; ms:13 | Unit ref | live |
| unit +0x18 | world pointer | hv:171 (`header,24`); ms:26 | Owning world | live |
| vtable +0xC8 | 56 bytes; [0] == exe+0x293600, [48] == exe+0x28d3e0 | hv:141-142 | Renderer type proof | live |
| unit +0x70 | u32 scene-graph node count | ms:31; ap:61; gh:243 | Bone count (rig = 63) | live |
| unit +0x98 | ptr to u32[count] link table: low16 mode (1 own parent, 2 linked to parent unit's bone), high16 parent index | ap:73-78; gh:213, 225, 268-273 | Bone links | live |
| unit +0xA0 | ptr to u32[count] bone name hashes | ap:63; gh:236 | Bone names | live |
| unit +0xC8 | u32 mesh count (<= 128), +0xD0 ptr to 16-byte mesh entries (mesh ptr at +0) | hv:148-150, 154, 160 | Mesh array | live |
| mesh +0xDC | 1 byte pass visibility mask | hv:183 | Pass bits | live |
| unit +0x1D0 | parent unit object pointer | hv:309; ms:17; ap:56; gh:266 | Link parent | live |
| unit +0x1D8 | 16 bytes: first child ptr (+0) | ms:30, 51-52 | Child list head | live |
| unit +0x1E0 | next sibling ptr | ms:55 | Sibling link | live |
| unit +0x358 | 8-byte resource id | hv:172, 314; ms:31 | Unit resource | live |
| appearance base | `avatar_owner + 0x546ab0 + avatar_index*0x1B8`, block at `+0xDC`, 120 bytes = 3 layers x 40 bytes (10 u32 slots) | hv:393-394, 434; ms:33 | Customization unit refs | live |
| model slots | layers 0..2, slots 6..9 | ms:44 | Arm pieces looked for on the rig | live |
| weapon tree limits | depth <= 8, units <= 32 | ms:48-49 | Weapon model walk | live |
| World.units limit | 131072 | hv:374 | Sanity bound | live |
| epoch reset window | 0.25 s; masks 0,1,254,255 | hv:556-557 | Native republish tolerated after a view/weapon/aim change | live |
| handle refresh | 0.5 s | hv:482, 498, 322 | Retry spacing | live |
| handle ref from userdata | `floor(address/4)` | hv:49 | SDK Unit handle -> unit ref | live |
| game+0x2ce18e8 | pointer | be:303 | Inventory (backpack: +40 map, +64 owners, +80 rows of 48 bytes, entity u32 at row+12; descriptor ref at +12) | live |
| on_foot | layer_count 31; l14 == 36, or l14 == 3 with `flags & 6 == 2` and scene 3 or 4 | be:351-372 | Body on foot test | live |
| flags address | `avatar_owner + 0x53e888 + avatar_index*0x1240` | be:363 | Control flags | live |
| game+0x2ce1508, +0xac29c | pointer, u32 scene | be:365-366 | Scene id | live |
| movement anchors | game+0x9de28c `4c8b1d253c3002`; +0x9de309 `8bc8498b4350488d148948c1e2054488b40294000000415e`; +0x829b92 `8bc3488d0c8048c1e1054280bc31940000000074ceb001`; +0x9dd574 `488b6950488bf18bda488d3c9b48c1e70580bc2f94000000007407b001`; +0x9dc7db `4863d348c1e205420fb6442a0c428b0c2a4388843793000000`; +0x839073 `488bd6498bcde80208ffff84c07448488b4538f3420f10bc281c7c0a00f3420f10b428187c0a00f3460f108428207c0a00` | mv:8-15 | game.dll build fingerprints for focused locomotion | live |
| game+0x2ce1eb8 | pointer | mv:47 | Locomotion component pool | live |
| pool +0xC | u32 active count (1..16384) | mv:48-49 | | live |
| pool +0x20 | buckets ptr (+0), capacity u32 (+8, power of two <= 65536), empty id (+12), multiplier (+16); 8-byte buckets {id, index}; probe <= 128 | mv:50-60 | Avatar -> component index hash | live |
| pool +0x38 | descriptors ptr array | mv:62-63 | | live |
| pool +0x50 | rows ptr; row stride 0xA0; flag byte at +0x94 | mv:66, 69 | Focused locomotion flag (0/1) | live |
| avatar_owner +0x110 + avatar_index*8 | descriptor ptr (20-byte descriptor) | mv:64-65 | Ownership proof | live |
| region test | state 0x1000, kind 0x20000, protect 4 or 8 | mv:87-88 | Writable private data check | live |
| key name | `'s'` (index 0..511) | mv:164-166 | Game keyboard S binding | live |
| INTERVAL | 0.5 s | dof.lua:5 | DoF poll / retry spacing | live |
| ack warning | 2 s | dof.lua:134 | Pending acknowledgement message | live |
| render settings | `'dof_enabled'` (live), `'vignette_enabled'` (parked), values `'true'`/`'false'` strings | dof.lua:10, 14, 88, 236 | Renderer Boolean settings | dof live; vignette setting parked |
| exe+0x3d43d0 | 8 bytes 48 8B D1 E9 08 FD FF FF | dof.lua:62; be:746 | `Application.render_setting` thunk | live |
| exe+0x3d3ff0 | 8 bytes 48 8B D1 E9 88 FA FF FF | dof.lua:63; be:747 | `Application.set_render_setting` thunk | live |
| exe+0x3d419a | 7 bytes 48 8B 0D 1F 37 6E 01 | be:748 | Observer contract | live (cached read) |
| exe+0x3d420e | 9 bytes 83 F8 04 75 1A 41 38 71 08 | be:749 | Observer contract | live (cached read) |
| exe+0x1ab78c0 | pointer to {count u32, rows ptr +8}; rows 24 bytes: type u32 +0 (must be 4), Boolean byte +8, key u32 +16, sorted by key; count <= 4096 | be:753-771 | Renderer setting table | live (cached read) |
| setting keys | dof_enabled 0x06f1d0d3; vignette_enabled 0x72e8f18c | be:732 | Hashed setting names | first live, second parked |
| CLASS | 0x35b0f6c2 | vignette.lua:4 | Low 32 bits of the vignette component instance id | live |
| MAPPING | `'core/stingray_renderer/shading_environment_components/vignette'` | vignette.lua:5 | Value of property `shading_environment_mapping` | live |
| property names | `shading_environment_mapping`, `vignette_enabled` | vignette.lua:41, 44, 49 | RenderDataComponent properties | live |
| vignette limits | entities <= 32768; instances <= 64; rows <= 32; 96 entities per update; rescan 1 s; poll 0.1 s | vignette.lua:35, 86-87, 90, 96, 102 | Discovery budget | live |
| globals | `DBF_PHOTO_CAMERA_OWNERSHIP`, `DBF_PHOTO_ACTIONCAM_COMPAT`, `package.loaded['mods/actioncam/actioncam']`, upvalue name `input_reader`, first 32 upvalues | cc:21, 25, 33, 43, 47 | Other mods' interfaces | live |
| CHEST | 0x37EF0820 | ap:26 | Bone name hash | live |
| SPINE_A | 0x58D886FB | ap:26 | Bone name hash | live |
| SPINE_B | 0x5A4BBAA2 | ap:26 | Bone name hash (also the torso frame's lower spine point) | live |
| GROUP | 0x315CD161 | ap:26 | Shoulder-group bone name hash | live |
| left collar / shoulder | 0xC4787B4E / 0x03752C98 | ap:28 | Bone name hashes | live |
| right collar / shoulder | 0xB2BCD839 / 0x1620B2CE | ap:29 | Bone name hashes | live |
| MAX_NODES | 255 | ap:31; gh:105 | Bone count bound | live |
| RESCAN | 0.25 s | ap:31; gh:105 | Lookup spacing while something is missing | live |
| TOLERANCE | 0.02 m | ap:31 | Chest drift that means the game re-linked the piece | live |
| quick window | 0.6 s | ap:215; gh:131 (`QUICK`) | Lookup every update after the arm view begins | live |
| confirm | 0.5 s | ap:230; gh:108 (`CONFIRM`) | One more lookup after the first find / after view begin or aim edge | live |
| torso frame minimums | 0.05 m, 0.05 m, 0.02 m, 1e-4, 1e-6 | ap:192, 197, 178 | Degenerate-frame guards | live |
| world up | (0,0,1) | ap:194, 196 | Rig torso frame up | live |
| GRIP | 0x67C94E4B | gh:99 | Left-hand grip bone on weapon units | live |
| HAND_NAME | 0x5C0C68D3 | gh:99 | Body left hand bone | live |
| RIGHT_NAME | 0x248004F5 | gh:100 | Body right hand bone | live |
| ATTACH | 0x4FDD3337 | gh:100 | Weapon bone (rig bone 8, body bone 84 per comment gh:54-55) | live |
| RIGHT | shoulder 30, elbow 32, hand 34 (zero-based) | gh:101 | Rig right arm bones | live |
| SHOULDER, ELBOW, HAND | 10, 12, 13 (zero-based) | gh:102 | Rig left arm bones | live |
| ARM_PARENT | 7 | gh:102 | Declared, never used | dead |
| RIG_NODES | 63 | gh:102; ms:31 | Rig bone count | live |
| FULL, NONE | 0.20 m, 0.32 m | gh:105 | Correction weight window (hand to target) | live |
| STRETCH | 0.95 | gh:106 | Largest fraction of arm length the solve reaches (or the animated reach if more) | live |
| solve margins | 0.001 m, 1e-5 | gh:140-143 | Reach clamp and degeneracy guards | live |
| PINNED | 0.02 m | gh:108 | Body hand counts as on a grip bone | live |
| EASE | 0.15 s | gh:108 | Ease in/out of the left pin and of the spin hold | live |
| PERIOD | 1 s | gh:108 | Periodic piece lookup | live |
| TURNING, SLIDING, CALM | rad(400)/s, 1.5 m/s, 0.25 s | gh:112 | Spin start limits (weapon in the body's hand) and hold-on time | live |
| RIG_TURN, RIG_SLIDE, RIG_CALM | rad(45)/s, 0.15 m/s, 0.5 s | gh:114 | Rig playing its own animation: spin handling stands down | live |
| AWAY_TURN, AWAY_SLIDE | rad(12), 0.03 m | gh:119 | Spin lasts until back within these of the start | live |
| STIR_TURN, STIR_SLIDE, SETTLE | rad(30)/s, 0.1 m/s, 3 s | gh:119 | Spin ends this long after the last movement | live |
| SWAP | 0.1, 0.25, 0.5 s | gh:126 | Extra lookups after a weapon id / transition edge | live (memory notes: the edge rarely fires because the view drops during a swap) |
| ASIDE | 0.2 s | gh:132 | Left hand leaving/returning around a one-handed spin | live |
| sample gap bound | 0.25 s | gh:339, 356, 601 | Rate samples older than this are ignored | live |
| SLEEVE set | 0x37EF0820, 0xC4787B4E, 0xB2BCD839, 0x58D886FB, 0x5A4BBAA2, 0x315CD161 | gh:134 | Bones left to arm_pieces | live |
| rig screen mesh rule | visible and exactly 2 materials | gh:488 | Rig's own mesh hidden while the left arm is corrected | live |
| weapon root node | SDK index 1 | gh:303, 312, 586 | Root of the hung weapon unit, carries the spin offset | live |
| profile.motion.world_pose | rva 0x1f30e0 | profile.lua:69 | Not referenced by any of these 8 files | n/a here |
| executable identity | helldivers2.exe timestamp 0x6af2d5e6, image size 0x29fe000 | profile.lua:27, 59, 94; be:688-722 | Build check behind `_alignment_executable` | live |

**Motion, menu, editor, aim markers, input**

| name | value | defined at | what it is | live? |
|---|---|---|---|---|
| toggle key default | 122 (VK F11) | fp/menu.lua:249, 367 | default hotkey for the view toggle | live |
| editor key default | 119 (VK F8) | fp/menu.lua:249, 365 | default hotkey for the placement editor | live |
| shift probe codes | 16, 160, 161 | fp/menu.lua:280 | Shift+F8 test | commented out |
| ID | 'goose_native_ads_camera_test' | fp/menu.lua:4; src/first_person.lua:4 | mode's registration id inside the facade | live |
| KIT | 'goose_temporary_unlocks' | src/first_person.lua:3; src/mcm.lua:5 | kit MCM registration id | live |
| PREFIX | 'first_person_' | src/first_person.lua:5 | control id prefix | live |
| page ids | 'first_person', 'first_person_motion' | src/first_person.lua:40-41 | page ids / names 'First Person', 'Bob and Sway' | live |
| input owner id | 'goose_first_person_editor' | fp/shared_input.lua:82-83; fp/menu.lua:254; fp/placement_editor.lua:286 | LLL input lease owner name | live |
| Epic LUT module | 'dbf.epic_lut.frontend.v1' | fp/placement_editor.lua:63 | `package.loaded` key probed for a foreign cursor owner | live |
| motion preset Comfort | bob .45, bob speed 1, sway .45, sway speed 1, sprint .35 | fp/menu.lua:8 | preset 2 | live |
| motion preset Natural | bob .8, bob speed 1, sway .8, sway speed 1, sprint .55 | fp/menu.lua:9 | preset 3 | live |
| clip range | 5..40 cm, integer | fp/menu.lua:11 | valid near-clip value; 5 = game distance | live |
| global near default | 15 cm | fp/menu.lua:146, 398 | default hip near clip | live |
| editor BASELINE | x .15, y 0, z -.12 | fp/placement_editor.lua:8 | fallback draft placement (metres) | live |
| editor LIMIT | .5 | fp/placement_editor.lua:9 | max abs x/y/z (metres) | live |
| rotation limit | 90 degrees | fp/placement_editor.lua:15, 368, 372-373 | max abs yaw/pitch/roll | live |
| editor VK codes | 2 right button, 4 middle button, 13 Enter, 27 Esc, 16 Shift, 37/39 Left/Right, 38/40 Up/Down, 33/34 PgUp/PgDn | fp/placement_editor.lua:319-321, 366, 384 | editor input | live |
| drag gains | x: 1 m per screen width; z: .6 m per screen height | fp/placement_editor.lua:377-378 | left drag | live |
| rotation gains | yaw 120 deg per width; pitch 90 deg per height; roll 180 deg per width | fp/placement_editor.lua:368, 372-373 | right / middle drag | live |
| wheel gain | .01 m per 120 wheel units | fp/placement_editor.lua:381 | depth | live |
| key speed | .15 m/s, dt clamp 0...1 | fp/placement_editor.lua:382-383 | arrows / PgUp / PgDn | live |
| fine factor | .1 | fp/placement_editor.lua:366 | Shift held | live |
| panel geometry | scale clamp .6..1.5 of h/1080; x 24, y h-280, w min(w-48, 530), h 256; layers 300 rect, 301 text | fp/placement_editor.lua:148-157 | editor panel | live |
| status truncation | 72 characters | fp/placement_editor.lua:168 | status line | live |
| editor colours | panel 15,18,20 a .92; text 234,235,228; accent 244,202,53; text_dim 181,187,188; field 41,44,46; solid 1 | fp/ui_style.lua:70-79 | F8 panel palette | live |
| HellForge tokens | colours and style block | fp/ui_style.lua:14-64 | theme tokens for the unused drawing kit | not used by the mode |
| SEGMENTS | 4 | fp/ui_style.lua:81 | triangles per rounded corner | not used |
| vector bounds | translation .15 m; unit length tolerance .0001; qw >= cos(120 deg / 2) | fp/motion_runtime.lua:12-24 | validation of a submitted modifier | live |
| handoff_seconds | .15 | fp/motion_runtime.lua:44 | unshared ADS handoff fade | live |
| guard_seconds, guard_speed, guard_turn | .12 s, 1 m/s, 360 deg/s | fp/motion_runtime.lua:49 | pop guard | live |
| guard floors | .002 m, 1 degree | fp/motion_runtime.lua:99 | minimum pop thresholds | live |
| frame_step | default 1/60, clamp 1/240...05 | fp/motion_runtime.lua:50, 226-228 | per-update step | live |
| max_step | .05 | fp/motion_runtime.lua:51 | largest step spent per update | live |
| continuity window | .25 s | fp/motion_runtime.lua:246, 253 | modifier carried across a lease change | live |
| placement blend tau | .10 s, dt clamp 0...1 | fp/motion_runtime.lua:350 | action placement ease without a shared phase | live |
| placement snap | .0001 | fp/motion_runtime.lua:355 | snap to target | live |
| presentation transition | duration .18, max_gap .25 | fp/runtime.lua:670 | shared phase used by motion | live |
| stride length | 3.4 m per bob cycle | fp/motion.lua:87 | bob phase | live |
| amplitude | speed/4 clamp 0..1, tau .10 | fp/motion.lua:89 | bob amplitude | live |
| aim tau | .10 in, .12 out | fp/motion.lua:100, 104 | eased aim gait | live |
| sprint hold | .10 s | fp/motion.lua:112 | sprint latch | live |
| sprint tau | .12 rise, .22 fall, .15 fall while aiming | fp/motion.lua:115 | sprint weight | live |
| speed tau | .12 | fp/motion.lua:120-121 | right/forward speed smoothing | live |
| look sway | gain .0015 clamp .012 (x); gain .001 clamp .008 (z); tau .065/sway_speed | fp/motion.lua:122-123 | turn inertia | live |
| strafe / forward lag | .001 clamp .008; .0006 clamp .006 | fp/motion.lua:137-138 | movement inertia | live |
| bob amplitudes | x .008, y .003, z .006 (metres) | fp/motion.lua:132-134 | walking bob | live |
| sprint gait factor | 2.5 | fp/motion.lua:131 | bob during sprint | live |
| sprint pose | pitch -12, yaw +18, roll -10 degrees | fp/motion.lua:144-146 | lowered sprint pose | live |
| shoulder offset | .04 (x), .06 (y) | fp/motion.lua:141-142 | aim stance pull | dead (`shoulder_aim_enabled` never set) |
| camera bob / sway | .006, .004; gains .002 / .0015, clamps .015 / .01, tau .08 | fp/motion.lua:124-125, 147-151 | camera additions | dead (strength 0, outputs unread) |
| settle | .00001 | fp/motion.lua:12 | zero threshold | live |
| rebase thresholds | dt > .25; distance > 2 m; speed > 20 m/s; turn > pi/2; dt > 1 full reset | fp/motion.lua:63, 77-78 | hitch / teleport handling | live |
| integration clamp | .05 | fp/motion.lua:79, 82 | step clamp | live |
| configure ranges | strengths 0..3, speeds .25..2, scale 0..2 | fp/motion.lua:35, 43 | accepted option ranges | live |
| world_pose fingerprint | rva 0x1f30e0, hex '40534883ec204863dae8e2d5eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3c3' | fp/profile.lua:69 (checked at fp/locomotion.lua:129-131) | exe function establishing the world-matrix array and stride | live |
| unit ref split | index = ref % 0x400000; generation = floor(ref/0x400000) % 256 | fp/locomotion.lua:105 | Unit handle encoding | live |
| unit registry offsets | +0x98 capacity (u32), +0xA0 generations ptr, +0x88 objects ptr | fp/locomotion.lua:106-109 | exe unit registry (base from `profile.pose.executable.unit_registry`) | live |
| graph getter | vtable +0xE8 == exe+0x28d3c0 | fp/locomotion.lua:111 | Unit scene-graph getter check | live |
| scene graph offsets | object+0x60 graph; graph+0x10 count (1..512); graph+0x28 world array; stride 0x40 | fp/locomotion.lua:112-113, 134-135 | world matrices | live |
| rig parent node | index 4 | fp/locomotion.lua:135 | rig node used as the parent basis | live |
| rig node count | > 50 | fp/locomotion.lua:133 | sanity check for the first-person rig | live |
| camera matrix | context+0x0C, 16 floats | fp/locomotion.lua:136 | camera basis | live |
| camera kind | 6 | fp/locomotion.lua:123; fp/aim_markers.lua:88 | native first-person camera | live |
| camera kind | 4 | fp/aim_markers.lua:89 | normal camera (fallback view) | live |
| sprint gait bit | flags byte 10, bit value 4 (gait 0x84; walk 0x88, jog 0x90) | fp/locomotion.lua:80-87 | native sprint gait | live |
| sprint layers | layer_count 31; l14 36 or 3; l3 71; l3 68 with l1 75 or 78 | fp/locomotion.lua:88-93 | animation layer states for sprint | live |
| basis tolerances | squared length .25..4; skew .03; handedness .95; axis length .95..1.05; quaternion length .9..1.1 | fp/locomotion.lua:14, 17, 39, 43, 52 | transform validation | live |
| rotation cap | 120 degrees - .00001 rad; inputs within pi/2 | fp/locomotion.lua:36, 56 | parent-space rotation limit | live |
| finite bound | abs < 1e8 | fp/locomotion.lua:6 | float sanity | live |
| HUD setter | game+0x12e4e20, bytes '4883ec28448b014c8bd1418bc0458bc883e0ef4183c91084d2440f44c8448909' | fp/aim_markers.lua:7, 55 | game function that sets a widget's shown bit | live |
| crosshair witness | game+0x1635ccf '4d8da510010000' | fp/aim_markers.lua:8 | fixed crosshair group reference | live |
| aiming-circle witness | game+0x1635d61 '498db520020000' | fp/aim_markers.lua:9 | moving aiming-circle group reference | live |
| HUD witness | game+0x117c88d '498d8f78a61900e887934b00498d9778a61900' | fp/aim_markers.lua:10 | crosshair offset witness | live |
| state pointer | game+0x2ce1508; +0xac29c == 4 | fp/aim_markers.lua:30-31 | game state; value 4 = in mission | live |
| HUD root pointer | game+0x2e28788 | fp/aim_markers.lua:32 | HUD root | live |
| HUD initialized | root+0x24e334 == 1 | fp/aim_markers.lua:33 | HUD ready flag | live |
| crosshair | root+0x24e340+0x19a678 | fp/aim_markers.lua:34 | crosshair container | live |
| parent / context / world | crosshair+0xf0 == root+0x820; crosshair+0xf8; context+0x10 | fp/aim_markers.lua:35-39 | ownership checks | live |
| widgets | crosshair+0x110, crosshair+0x220 | fp/aim_markers.lua:40 | fixed crosshair group, moving aiming-circle group | live |
| visible bit | bit value 16 of the widget's first u32 | fp/aim_markers.lua:49 | shown flag | live |
| VirtualQuery constants | state 0x1000; kind 0x1000000 or 0x40000 (Wine); protect 0x20 / 0x40 / 0x80 | fp/aim_markers.lua:57-59 | executable-page check before the call | live |
| weapon key | weapon_object+0x358, 8 bytes | fp/aim_markers.lua:98 | weapon resource id used as the settings key | live |
| action resource | '4D1C334D294DFA97' | fp/action_placement.lua:50 | animation resource required for posture actions | live |
| head camera default | x 0, y 0, z .04; limit .5 | fp/head_placement.lua:6, 11 | Fird head camera offset | parked |
| key names | VK -> engine key name table (8 backspace ... 222 quote; 48-57 digits; 65-90 letters; 112-135 'f1'..'f24') | fp/shared_input.lua:4-11 | names for `Keyboard.button_id` | conditional |
| mouse_bits | 1->1, 2->2, 4->4, 5->8, 6->16 | fp/shared_input.lua:12 | VK mouse code to button bit | conditional |
| modifiers | 16->{160,161}, 17->{162,163}, 18->{164,165} | fp/shared_input.lua:13 | generic modifier to left/right codes | conditional |
| keyboard mask | 32 bytes | fp/shared_input.lua:14, 77 | captured-keyboard bitmap size | conditional |
| keyboard states | 1 arming, 2 active (valid 1..5) | fp/shared_input.lua:124, 132, 137 | LLL keyboard capture state | conditional |
| denied error | 'Keyboard request refused (0x00000005)' | fp/shared_input.lua:43, 86 | access-denied fallback trigger | conditional |
| service version | 1 | fp/shared_input.lua:18 | required LLL input service version | conditional |
| adapter contract | api=2, isolated_look=true | fp/input_adapter.lua:6 (required at fp/placement_editor.lua:84) | camera_input API level | conditional |
| config defaults | placement .15/0/-.12; motion_enabled true; motion_scale 1; fird_head_camera 0/0/.04; camera_kick_enabled true; empty tables | config/defaults.cfg:12-30; src/first_person.lua:9-11 | `first_person` defaults | live |
| family baselines | rifle .15/0/-.12; compact .12/0/-.09; sidearm .105/0/-.08; marksman .14/.015/-.115; shotgun .14/.005/-.11; energy .15/0/-.115; support .16/.015/-.14 | fp/action_placement.lua:7-12 | opt-in per-family default placements | live when the option is on |
| kit poll interval | .25 s | src/mcm.lua:180 | kit preference polling (not the mode's) | live (kit) |

## 5. Settings and menu

### Settings and menu

Registration chain: `menu.lua` builds a spec with id `goose_native_ads_camera_test`, pages `test` ("First person") and `movement` ("Bob and sway") (menu.lua:335-420). The facade in `src/first_person.lua:37-72` renames the pages to id `first_person` / name `First Person` and id `first_person_motion` / name `Bob and Sway`, prefixes every control id with `first_person_`, and wraps every control's `on_change` so the spec-level `on_change` (marks motion and near caches dirty, menu.lua:337) also runs. `src/mcm.lua:161` appends these pages to the kit registration. Values are read with `handle.get('first_person_'..id)` on the kit's handle; before the kit handle is attached, `get` returns the control default (first_person.lua:56-59).

#### (a) Every control

| control id (with prefix) | page | type | label | default | range/step/choices | config key it persists to | what it does / who reads it | live? |
|---|---|---|---|---|---|---|---|---|
| first_person_start_true_first_person | First Person | button | Enable True First Person | n/a | n/a | none | `start_guard` then `actions.start_true_first_person()` = `start_pose(0)` (menu.lua:87-94, runtime.lua:1048) | live |
| first_person_stop_test | First Person | button | Turn first-person view off | n/a | n/a | none | `actions.stop()` = `stop_pose` (menu.lua:95-96) | live |
| first_person_show_aim_markers | First Person | toggle (checkbox) | Show aim markers | `aim_markers:setting()` at registration (menu.lua:347) | on/off | `first_person.show_aim_markers` via `mod.set` (runtime.lua:729); MCM also stores the checkbox value itself | `aim_markers:set_setting(value)`; read by aim_markers.lua `global_setting` | live |
| first_person_edit_placement | First Person | button | Edit arms and weapon placement | n/a | n/a | none | `toggle_editor(false)` (menu.lua:360-361) | live |
| first_person_start_free_aim | First Person | button | Free aim (30 seconds) | n/a | n/a | none | `actions.start_free_aim()` -> `camera:free_aim(30000)`; refused when `camera.retired`, editor active, or F11 off (runtime.lua:926-934) | button is live; outcome belongs to the camera module (other part) |
| first_person_editor_key | First Person | keybind | Open / close placement editor | 119 (F8) | integer 0..255, 0 = unbound (menu.lua:124-128, 250) | MCM's own preference store only | read each update by `poll_hotkeys` | live |
| first_person_toggle_key | First Person | keybind | Toggle True First Person | 122 (F11) | integer 0..255, 0 = unbound | MCM's own preference store only | read each update by `poll_hotkeys` | live |
| first_person_true_native_ads | First Person | toggle (checkbox) | True First Person: native ADS | false | on/off | MCM store only | `menu:native_ads()` (236) -> native_ads_option (runtime.lua:721-723) | live |
| first_person_show_torso | First Person | toggle (checkbox) | True First Person: show torso | false | on/off | MCM store only | `menu:show_torso()` (237) -> head visibility (runtime.lua:695) | live |
| first_person_fird_person_arms | First Person | toggle (checkbox) | Fird Person Arms | true (unset reads as on, menu.lua:239) | on/off | MCM store only | `menu:fird_person_arms()` -> `arms_wanted` (runtime.lua:545) for arm_pieces / grip_hand | live |
| first_person_family_placement_defaults | First Person | toggle (checkbox) | Use weapon-family placement defaults | `config.family_placement_defaults==true` (false when unset) | on/off | `first_person.family_placement_defaults` (runtime.lua:1066-1079); bumps `backend.action_placement_revision` | read by `Actions.resolve` / `default_placement` (action_placement.lua:98, 105) | live |
| first_person_motion_preset | First Person | choice (dropdown) | Motion preset | 1 | 1 'Custom (keep existing sliders)', 2 'Comfort', 3 'Natural' | MCM store only | `motion_options`: preset 2 = bob_strength .45, bob_speed 1, sway_strength .45, sway_speed 1, sprint_strength .35; preset 3 = .8, 1, .8, 1, .55 (menu.lua:7-10, 157-162) | live |
| first_person_motion_enabled | First Person | toggle | Weapon movement | true | on/off | MCM store; fallback when not boolean: `config.motion_enabled~=false` (menu.lua:154, runtime.lua:818) | `motion_options.enabled` | live |
| first_person_motion_strength | First Person | slider | Weapon movement strength | 100 | 0..200 step 5 | MCM store; fallback when invalid: `(config.motion_scale or 1)*100` (menu.lua:155) | `motion_options.scale = percent/100` | live |
| first_person_motion_bob_enabled | First Person | toggle (checkbox) | Weapon movement: walking bob | true | on/off | MCM store only | off forces `bob_strength=0` (menu.lua:164-166) | live |
| first_person_motion_sway_enabled | First Person | toggle (checkbox) | Weapon movement: sway | true | on/off | MCM store only | off forces `sway_strength=0` | live |
| first_person_motion_sprint_enabled | First Person | toggle (checkbox) | Weapon movement: sprint pose | true | on/off | MCM store only | off forces `sprint_strength=0` | live |
| first_person_near_clip_cm | First Person | slider | Near clip distance (cm) | 15 | 5..40 step 1 | MCM store only | `near_options`: hip = cm>5 and cm/100 or 0 (menu.lua:144-149, 172-182) | live |
| first_person_near_clip_aim | First Person | toggle | Near clip while aiming | false | on/off | MCM store only | global aim distance = on and cm or 5 (menu.lua:147-148) | live |
| first_person_weapon_composition_status | First Person | text | initial 'Weapon: no active firearm' | n/a | n/a | none | rewritten by `refresh_weapon_controls` (menu.lua:230-234) | live |
| first_person_weapon_near_hip_cm | First Person | slider | This weapon: hip clearance (cm) | 15 | 5..40 step 1 | draft in MCM store; saved to `first_person.near_clip_profiles[key].hip_cm` only by the save button | draft; reset to the weapon's profile or global values when the weapon changes (menu.lua:218-229) | live |
| first_person_weapon_near_aim_cm | First Person | slider | This weapon: normal-aim clearance (cm) | 5 | 5..40 step 1 | as above, `.aim_cm` | draft | live |
| first_person_save_weapon_near | First Person | button | Save clipping for this weapon | n/a | n/a | `first_person.near_clip_profiles` | `save_clip(false)` (menu.lua:183-203) | live |
| first_person_reset_weapon_near | First Person | button | Use global clipping for this weapon | n/a | n/a | `first_person.near_clip_profiles` (row removed) | `save_clip(true)` | live |
| first_person_start_native_test | First Person | button | Compare native ADS camera (30 seconds) | n/a | n/a | none | `actions.start_native(30)` -> `controller:start(30,'native')` (menu.lua:409-417, runtime.lua:912-925) | live (test control left in the shipped page) |
| first_person_runtime_status | First Person | text | initial 'Status: First person off' | n/a | n/a | none | rewritten to 'Status: '..label (menu.lua:300-307) | live |
| first_person_weapon_bob_strength | Bob and Sway | slider | Weapon bob strength | 100 | 0..300 step 5 | MCM store only | `bob_strength = n/100` unless a preset is selected (menu.lua:22, 158-162) | live |
| first_person_weapon_bob_speed | Bob and Sway | slider | Weapon bob speed | 100 | 25..200 step 5 | MCM store only | `bob_speed = n/100` | live |
| first_person_weapon_sway_strength | Bob and Sway | slider | Weapon sway strength | 100 | 0..300 step 5 | MCM store only | `sway_strength = n/100` | live |
| first_person_weapon_sway_speed | Bob and Sway | slider | Weapon sway response speed | 100 | 25..200 step 5 | MCM store only | `sway_speed = n/100` | live |
| first_person_sprint_motion_strength | Bob and Sway | slider | Sprint weapon movement | 100 | 0..200 step 5 | MCM store only | `sprint_strength = n/100` | live |
| (first_person_start_test) | First Person | button | Enable Fird Person | n/a | n/a | none | COMMENTED OUT (menu.lua:341-342, handler 78-86); `refresh_status` line for it also commented (313) | hidden |
| (first_person_ads_mode) | First Person | choice (dropdown) | Fird Person: ADS behavior | 1 | 'Third-person aim at the eye camera', 'Native first-person ADS' | none | COMMENTED OUT (menu.lua:375-377). `menu:ads_mode()` still exists (240-242) and returns 'fird' because the stored value is never 2 | hidden |

An out-of-range slider value falls back to the control's default in `motion_options` (menu.lua:160). `motion_options` always returns `camera_bob_strength=0, camera_sway_strength=0` (menu.lua:156).

#### (b) Pages, order, status text, disabled rules

Kit page order (src/mcm.lua:114-161): Unlocks, Gameplay, Spawner (if spawner), Loadout Editor page (if editor), then First Person, Bob and Sway. `tests/mcm.lua:24-25, 40` fix `first_person` as the page after the kit's own pages; before the runtime has registered, the single placeholder page holds one text control `first_person_status` = 'Status: '..`M.status()` (first_person.lua:112-117).

First Person control order (menu.lua:343-418): start_true_first_person, stop_test, show_aim_markers, edit_placement, start_free_aim, editor_key, toggle_key, true_native_ads, show_torso, fird_person_arms, family_placement_defaults, motion_preset, motion_enabled, motion_strength, motion_bob_enabled, motion_sway_enabled, motion_sprint_enabled, near_clip_cm, near_clip_aim, weapon_composition_status, weapon_near_hip_cm, weapon_near_aim_cm, save_weapon_near, reset_weapon_near, start_native_test, runtime_status.

Bob and Sway order (menu.lua:21-27): weapon_bob_strength, weapon_bob_speed, weapon_sway_strength, weapon_sway_speed, sprint_motion_strength.

Status strings:
- Placeholder page (first_person.lua:108-111): 'Unavailable: '..error; 'True First Person ready; F11 toggles'; 'First person is initializing'.
- `first_person_runtime_status` = 'Status: '..label, where label is `actions.dev_status()` when it returns something, otherwise `controller:display_status()` (menu.lua:300-301). `dev_status` returns 'First person suspended: '..name..' ('..phase..')' while recovery blocks the view, or 'Aiming markers: '..fault (runtime.lua:1086-1091). Controller labels are the `report(...)` strings in fp/controller.lua (initial 'Idle; manual start required', controller.lua:11; for example 'First person enabled until manually stopped' at 334, 'Stopped: ...' at 179).
- `first_person_weapon_composition_status`: 'Weapon: '..(label or key)..' ('..family..')' with the family part only when known, else 'Weapon: enable First Person with a firearm to edit its clipping' (menu.lua:232).
- Button refusal texts returned to MCM: 'Mod is stopping', 'Turn the active first-person view off first', 'First-person cleanup or placement editing is pending' (menu.lua:70-74); 'Placement editor is unavailable' (112); 'Weapon changed; reopen its clipping controls' (187); 'Clip distances must be 5 to 40 cm' (191); 'Clipping settings could not be saved' (200); 'Aim-marker setting must be on or off', 'Aim-marker setting is unavailable' (352-353); 'Placement option unavailable' (383); 'Free aim unavailable' (364).
- Menu description (kit-level text is the kit's; the mode's own spec description is 'Use the game\'s first-person arms and weapon view. F11 toggles True First Person.', menu.lua:338, not shown by the kit registration).

Disabled rules (menu.lua:204-217, 308-319), recomputed in `refresh_status` only when something changed (296-299):
- `show_aim_markers`: closing, or the marker getter/setter actions are missing.
- `start_true_first_person`: closing, or view enabled, or not clean, or editor active.
- `start_native_test`: closing, or not clean, or editor active.
- `edit_placement`: closing, or no editor, or editor active, or (view off and not clean).
- `stop_test`: closing only.
- `family_placement_defaults`: closing or `save_presentation` missing.
- `weapon_near_hip_cm`, `weapon_near_aim_cm`, `save_weapon_near`, `reset_weapon_near`: closing, or no current weapon (`controller:placement_target()` nil, so first person must be running with a firearm), or `save_presentation` missing.
- "enabled" = `view_enabled`, "clean" = `view_clean` (runtime.lua:1041-1047).

The MCM draws copies of the controls, so `first_person.lua` `M.sync` (84-106, called every `M.update`, 143-144) copies text labels and `disabled` (as `base_disabled` and `disabled`) from the menu's originals into the kit record's copies.

#### (c) Keys and mouse

| binding | default | action | rebindable | source |
|---|---|---|---|---|
| Toggle True First Person | F11 (VK 122) | `toggle_pose()`: off -> `start_pose(0)`, on -> `stop_pose()` | yes, `first_person_toggle_key` | menu.lua:249, 276-277; runtime.lua:908-911 |
| Open / close placement editor | F8 (VK 119) | `toggle_editor(false)`: open in weapon mode, or cancel when open ('Placement editing cancelled; first person stays on') | yes, `first_person_editor_key` | menu.lua:111-115, 278-281 |
| Shift+F8 | n/a | PARKED: the shift test is commented out (menu.lua:279-280); Shift+F8 behaves exactly like F8 | n/a | README.md:81 |
| Editor: Esc (VK 27) | fixed | cancel, restore the placement from when the editor opened | no | placement_editor.lua:321, 329 |
| Editor: Enter (VK 13) | fixed | save for this weapon, selected action | no | placement_editor.lua:321, 330 |
| Editor: left drag outside the panel | fixed | x += dx_pixels / width * fine (metres); z += dy_pixels / height * .6 * fine | no | placement_editor.lua:352-353, 376-380 |
| Editor: right drag (VK 2) outside the panel | fixed | yaw -= dx / width * 120 * fine; pitch -= dy / height * 90 * fine (degrees, clamp +-90) | no | placement_editor.lua:319, 356-359, 371-375 |
| Editor: middle drag (VK 4) outside the panel | fixed | roll += dx / width * 180 * fine (degrees, clamp +-90) | no | placement_editor.lua:320, 361-370 |
| Editor: wheel | fixed | y (forward) += wheel / 120 * .01 * fine (1 cm per 120 units) | no | placement_editor.lua:381 |
| Editor: Left 37 / Right 39 | fixed | x -/+ step | no | placement_editor.lua:383-386 |
| Editor: Up 38 / Down 40 | fixed | z +/- step | no | same |
| Editor: PgUp 33 / PgDn 34 | fixed | y +/- step | no | same |
| Editor: Shift (VK 16) held | fixed | fine = .1 (otherwise 1) for every drag, wheel and key | no | placement_editor.lua:366 |
| Editor: click '<' / '>' | fixed | previous / next action (wraps), unsaved edits discarded | no | placement_editor.lua:337-342 |
| Editor: click 'Save weapon', 'Save all', 'Cancel [Esc]', 'Reset', 'Reset all' | fixed | see (e) | no | placement_editor.lua:334-351 |

Key step: `step = .15 * dt * fine` metres per update with `dt = clamp(sample.dt, 0, .1)` (placement_editor.lua:382-383), so .15 m/s (.015 m/s fine). After all input x, y, z are clamped to +-.5 m (17, 387). There is no keyboard shortcut for action cycling, Save all, Reset or Reset all.

How F11/F8 are read (menu.lua:243-283), every `menu:update`:
- Input object = `api.camera_input` (the real DBFMCM one, or the adapter from first_person.lua:33-35). `input.frame()` is called if present, then `input.down(code)` with Windows VK codes.
- With the adapter and no token held, `down` resolves the VK code to an engine key name (`names` table, shared_input.lua:4-11; 122 -> 'f11', 119 -> 'f8') and reads `stingray.Keyboard.button(stingray.Keyboard.button_id(name))`, pressed when `==true` or `>.5` (shared_input.lua:177-185). It returns false when `stingray.Window.has_focus()` is not true (160, 102).
- Edge detection is the menu's own: a press fires when down now and not down on the previous eligible update. On any update that is not eligible, or when the provider, input object or either key code changed, or on the first eligible update, the state is only re-primed and nothing fires (menu.lua:265-273). A key held while the block ends therefore does not fire.
- Eligible means all of: not closing; `input.focused()==true`; `api.menu_binding_status()` is a table with `focused==true` and `editing~=true`; `api.is_open()==false` (MCM closed); `api.input_status()` is a table, not `pending_restore`, and either no `owner` and not `active`, or the owner is `goose_first_person_editor` while the editor phase is 'active'; and if global `LiveLuaLoader.input_status` exists it must return a table with no `owner` and no `pending_restore` (menu.lua:252-264).
- There is no explicit chat-open check in these files; chat only blocks the keys to the extent that DBFMCM's `menu_binding_status().focused` or the input leases reflect it.
- A key equal to `binding.global_key` (MCM's own menu key) is ignored. If both keys fire on one update the F11 toggle wins and F8 is dropped (274-282).
- Key code 0 disables that hotkey (250-251).

Editor input (placement_editor.lua:313-321): one `input.menu_sample()` per update gives `x`, `y` (screen pixels, y up), `click` (left button), `wheel`, `dt`; other buttons and keys through `input.down(vk)`. Input is ignored until left, right, middle, Enter and Esc are all released once after opening ('Release the activation click/key to begin', 322-325, 168).

#### (d) Persistence

Two stores exist:
1. DBF-MCM's own preference store holds the value of every control id `first_person_*` under the kit registration `goose_temporary_unlocks` (the mode only calls `handle.get` / `handle.set`; where MCM writes them is outside this source).
2. The kit config key `first_person`: `mod.set(key, value)` in the runtime is `M.host(parent).set`, which copies the defaults table, overlays the current `parent.config.first_person`, sets the one key and calls `parent.set('first_person', config)` (first_person.lua:17-23). `mod.config` reads `parent.config.first_person or defaults` (first_person.lua:13-15). Shipped defaults are `config/defaults.cfg:12-30` (declared as the LLL defaults file in manifest.json `lll.defaults`) and the Lua copy at first_person.lua:9-11. The on-disk location of the saved kit config is decided by the loader's `mod.set` and is not visible in this source.

| key under `first_person` | shape | default | written by | read by |
|---|---|---|---|---|
| placement | `{x,y,z}` metres, optional yaw/pitch/roll degrees | `{x=.15,y=0,z=-.12}` (defaults.cfg:13-17) | only editor 'Reset all' (runtime.lua:806-811) | `Actions.resolve` last fallback (action_placement.lua:95-101) |
| weapon_placements | map weapon key (16 hex chars) or `__all` -> placement | `{}` | only 'Reset all' (cleared); legacy data otherwise | `Actions.resolve` idle fallback (action_placement.lua:89-92) |
| action_placements | map weapon key or `__all` -> map action -> `{x,y,z,yaw,pitch,roll}` | `{}` | editor save through `Actions.saved` (runtime.lua:782-784, action_placement.lua:108-121); 'Reset all' clears | `Actions.resolve` (action_placement.lua:81-86) |
| action_state_mapping | map action resource -> map 'l1:l3' -> action name | `{}` | nothing in this source | `Actions.select` (action_placement.lua:132-136), editor `action_supported` (runtime.lua:772-774) |
| motion_enabled | boolean | true | nothing in this source | fallback only (runtime.lua:818, menu.lua:154) |
| motion_scale | number | 1 | nothing in this source | fallback only (runtime.lua:818, menu.lua:155) |
| fird_head_camera | `{x,y,z}` | `{x=0,y=0,z=.04}` | head_placement `save` (runtime.lua:747), unreachable | head_placement `get_saved` (runtime.lua:746); sets `backend.fird_camera_offset` at start |
| camera_kick_enabled | boolean | true | nothing | nothing in `src/` (dead key) |
| aim_markers | map weapon key -> true (weapons whose markers stay shown while aiming) | `{}` | `aim_markers:toggle` (runtime.lua:727), which no menu control calls | `legacy_allowed` when `show_aim_markers` is unset (aim_markers.lua:140-143) |
| show_aim_markers | boolean or absent | absent (not in defaults) | Show aim markers checkbox (runtime.lua:729) | aim_markers.lua `global_setting` (134-139) |
| family_placement_defaults | boolean or absent | absent = off | checkbox (runtime.lua:1066-1079) | action_placement.lua:98, 105; menu.lua:210, 379 |
| near_clip_profiles | map weapon key -> `{hip_cm, aim_cm}`, integers 5..40 | absent | save / reset clipping buttons (menu.lua:192-199) | `menu:near_options` (menu.lua:138-143, 172-182) |

Weapon key = the 8 bytes at `weapon_object+0x358` as 16 lower-case hex characters (aim_markers.lua:98); the catalogue maps its 64-bit type ids to the same byte order (action_placement.lua:30-32).

#### (e) Placement editor

Modes: `store.select_mode(kind)` (runtime.lua:787-800). 'weapon' is the only reachable mode: title 'WEAPON PLACEMENT', rotation on, actions = `Actions.actions` (Idle, Normal aim, ADS, Sprinting, Reloading, Crouching, Prone, Sliding, Diving; action_placement.lua:66-67), baseline = `Actions.default_placement`. 'camera' (title 'HEAD CAMERA', single action 'Head camera', no rotation, single save, footer 'Camera only; weapon placements are unchanged') requires Fird to be enabled (runtime.lua:755) and no caller requests it.

Drawing: the editor does not call the engine Gui itself. Each update it builds a list of commands and passes it to `view.draw(commands)` on the view from `DBFMCM.create_photo_view()` (placement_editor.lua:147-190, 291). Command shapes: `{type='rect', x, y, w, h, c={r,g,b}, a, layer=300}` and `{type='text', text, x, y, size, c, a, layer=301}`. `ui_style.lua` supplies only `editor_legacy_tokens` (panel 15,18,20 at opacity .92; text 234,235,228; accent 244,202,53; text_dim 181,187,188; field 41,44,46; solid opacity 1). The HellForge kit in the same file (`Gui.rect`, `Gui.triangle`, `Gui.text`) is not used.

Layout (y up, `scale = clamp(height/1080, .6, 1.5)`, placement_editor.lua:148-186): panel x = 24*scale, y = height - 280*scale, w = min(width - 48*scale, 530*scale), h = 256*scale. Inside, x0 = panel.x + 16*scale, top = panel.y + panel.h - 28*scale:
- title (size 17, accent): 'WEAPON PLACEMENT' + '  [<weapon key>]' at (x0, top).
- 'Editing: <action label>   (placement preview)' size 16 at (x0 + 80*scale, top - 30*scale).
- '<' and '>' buttons 32x26 (scaled) at x0 + (i-1)*40*scale, y = top - 36*scale, field colour, text size 15.
- 'Right %+.1f cm   Forward %+.1f cm   Up %+.1f cm' size 16 at top - 66*scale.
- 'Yaw %+.1f  Pitch %+.1f  Roll %+.1f deg' size 15 at top - 94*scale.
- 'L: move  R: yaw/pitch  M: roll  Wheel: depth  Shift: fine' size 15 at top - 118*scale (camera mode text: 'Left drag: move   Wheel: depth   Shift: fine').
- 'Wheel: depth   Arrows: move   PgUp/PgDn: depth' size 14 at top - 142*scale ('Preset only: automatic action mapping needs a live trace' when the action is unsupported; all nine shipped actions are supported, runtime.lua:769-775).
- status line, first 72 characters, size 13, text_dim, at top - 174*scale.
- bottom buttons at y = panel.y + 12*scale, height 28*scale, gap 10*scale, text size 14: 'Save weapon', 'Save all', 'Cancel [Esc]', 'Reset', 'Reset all' (five in weapon mode because `store.reset_all` exists; width = (panel.w - 32*scale - 4*10*scale)/5; with exactly four buttons the width is 112*scale).
- footer (size 13, accent) at (panel.x, panel.y - 20*scale): 'Save weapon / all applies to the selected action only'.

Save semantics (placement_editor.lua:220-225, runtime.lua:776-786, action_placement.lua:108-121):
- Save weapon / Enter: `action_placements[weapon_key][action] = draft`. Refused with 'Weapon changed; reopen placement editor' if the target or a fresh snapshot identity changed.
- Save all: `action_placements.__all[action] = draft` and that action is removed from every per-weapon row, so it applies to all weapons for the selected action only.
- Both bump `backend.action_placement_revision`, close the editor without restoring ('Placement saved for this weapon' / 'Placement saved for all weapons').
- Lookup order when resolving (action_placement.lua:79-102): weapon's action row, `__all` action row, then for 'ads' `{0,0,0}`, for other non-idle actions the idle result, for idle: `weapon_placements[key]`, `weapon_placements.__all`, family baseline (only when opted in and `placement` is invalid or the standard .15/0/-.12), `placement`, `{x=.15,y=0,z=-.12}`.
- Reset: previews the default for the selected action without saving ('Default placement previewed; save to keep it'): `Actions.default_placement` = zeros for 'ads', the family baseline when the option is on, else .15, 0, -.12 with zero rotation (action_placement.lua:103-107). Family baselines (action_placement.lua:7-12): rifle .15/0/-.12, compact .12/0/-.09, sidearm .105/0/-.08, marksman .14/.015/-.115, shotgun .14/.005/-.11, energy .15/0/-.115, support .16/.015/-.14.
- Reset all: immediately writes `weapon_placements={}`, `action_placements={}`, `placement={x=.15,y=0,z=-.12}` with rollback on failure, then makes the current action's resolved value both the draft and the restore point ('All saved placements in this editor reset') (runtime.lua:801-814, placement_editor.lua:343-346).
- Cancel / Esc / F8 / any forced close: restores the placement captured when the editor opened (`original`), unless the weapon target changed (placement_editor.lua:107-119).
- Limits: x, y, z within +-.5 m, yaw/pitch/roll within +-90 degrees (placement_editor.lua:9-16). Module BASELINE `{x=.15,y=0,z=-.12}` (8) is the fallback draft.

#### (f) `NativeADSCameraTest` functions the menu also exposes

`start_true_first_person` (button), `toggle` (= `toggle_pose`, the F11 hotkey), `stop` (= `stop_pose`, 'Turn first-person view off'), `start_native` (button, 30 seconds), `start_free_aim` (button). `start` / `start_pose` / `start_camera_only` are the same `start_pose` the button reaches (runtime.lua:1102-1127). The menu exposes no other function of that global.

## 6. Tests

### (a) How the suite is run

**Main suite: `tools/test_first_person.py` (97 lines).** Run as `python3 tools/test_first_person.py` (`README.md:87`).

- Interpreter: stock PUC **Lua 5.1 through lupa** (`from lupa import lua51`, `tools/test_first_person.py:15`), not LuaJIT. A fresh `LuaRuntime` is created for each fixture (`:64`).
- FFI: the **real cffi-lua library built for Linux** is loaded as `package.loaded.cffi` (`:65`). The shipped `src/first_person/compat51.lua` is then executed for real, its `ffi.os` is forced to `'Windows'`, and the result is published as the global `TEST_PORT51` (`:66`).
- Helper DLLs: `TEST_PORT51.ffi.load` is wrapped so that a fixture which installs `package.loaded.ffi` with its own `load` gets that mock (`:81`). Fixtures therefore pass a Lua table of fake exports (`NADSC_Begin`, `NADSF_ReadStats`, ...) in place of a DLL. No real helper DLL is loaded.
- `mod` / `mod.require`: there is no global mod host. The global `loadfile`/`dofile` are replaced by a loader that edits production source text on the way in (`:67-82`): it deletes `local mod=mod.require('src/first_person.lua').host(mod)`, replaces the compat51 require with `TEST_PORT51`, and replaces the requires of recovery, diagnostics, transition, loadout_catalogue, locomotion, fird_projection and ui_style with `dofile`. `mod.require('src/first_person.lua')` becomes `TEST_INTEGRATION`, a stub whose `menu_api`/`editor_api` return the global `DBFMCM`. Fixtures that need more set `_G.mod` themselves.
- `stingray`: one global with only `Application.time_since_launch` returning 100 (`:81`). Fixtures install their own fake SDK tables where needed.
- Fixture text is rewritten before it runs (`fixture_source`, `:41-55`): it goes through `port_mods.convert` (the LuaJIT-to-5.1 port tool), every hex literal is mapped through three address-migration tables (`docs/first-person-applied-migration.json`, `-26251122-migration.json`, `-26568081-migration.json`, `:17-28`), byte-pin strings are mapped through witness tables and through byte comparison of old and new game binaries (`:29-39`), `/main.lua` becomes `/src/first_person/runtime.lua`, and `/src/x.lua` becomes `/src/first_person/x.lua`. Five fixtures skip `convert` and the constant mapping: recovery, diagnostics, transition, runtime_recovery, runtime_frame (`:86-88`).
- Environment: `TFP_FIXTURE=<file name>` runs one fixture only (`:62`). No other variable is read.
- Output: `RUN <name>` per fixture, `FAIL <name> <first line>` on error, a final `Lua 5.1/CFFI fixtures: N / M`, a receipt at `dist/first-person/lua-fixtures.json` (reset to `[]` at start, `:60,95`), exit code 1 on any failure.
- Size: **55 fixture files**, 16,386 lines, all `tests/first_person/*.lua`. The receipt on disk (2026-11-15 10:19) lists 55 of 55 passed. Run time was not measured (this research did not run anything).
- **Not self-contained.** The runner needs files outside R: the lupa and cffi build under `~/Documents/Codex/2026-10-22/a-2/work/ffi-comparison` (`:10-11,65`), `port-tools/port_mods.py` (`:12-13`), `native-work/evidence/game-pre1020.dll`, `game-26088801.dll`, `exe-26088801.bin` and a file under `~/Applications/HellForge/mods-retired/...` (`:29-30`).

**`tests/first_person/run_tests.py` (153 lines) is the upstream Windows runner and is stale.** It loads the game's `lua51.dll` (LuaJIT) through ctypes with a default Windows path (`:17`, override `--dll` or `DBF_LUA_DLL`, `:143`), expects the old flat layout (`src/backend.lua`, `tests/<name>`, `:43,48-52`) and imports `tools/build.py`, which does not exist in R (`:65`). It lists 23 fixtures. It cannot run against this tree as it stands.

**Three stand-alone Lua 5.1 tests in `tests/`**, run from R with a plain interpreter (`lua5.1 tests/first_person_integration.lua`, `README.md:87`; the other two use the same relative `dofile` paths):

| file | lines | covers | how |
|---|---|---|---|
| `tests/first_person_integration.lua` | 98 | The facade `src/first_person.lua`: namespaced settings (`host.set` writes under `config.first_person`), one shared MCM registration with `first_person_` prefixed control ids, virtual handle `get/set/unregister`, `FP.enable/update/shutdown`, retry of a pending cleanup on the next frame, and `FP.sync(record)` copying status label and disabled state into a menu framework that copied the controls. | Fake `mod` host, fake `DBFMCM.register`, fake runtime table with `on_enable/on_update/on_disable`. `runtime.lua` itself is not loaded. |
| `tests/first_person_input.lua` | 51 | `input_adapter.lua` + `shared_input.lua`: F11/F8/shift polling through `Keyboard.button`, foreground gating, editor token acquire/release through a shared input service, menu sample (y flipped, dt). Second half: `menu.lua` F11 toggle and F8 editor on key edges, no repeat while held, no synthetic edge on focus regain. | Fake `stingray` (Keyboard, Window, Gui, Application), fake shared input service, fake controller and editor. |
| `tests/mcm.lua` | 49 | The kit's own `src/mcm.lua`: three pages (Unlocks, Gameplay, First Person), First Person page only text controls when the runtime is off, gameplay toggles routing, "Complete all objectives" button. Only marginally about first person. | Fake `DBFMCM`, fake controller. |

**`tools/test_native_recovery_bridge.py` (44 lines) + `tests/native_recovery_bridge.lua` (72 lines).** Usage: `--policy camera|fov|blur --log <native fixture log>`. The script parses `RECOVERY_EVENT {json}` lines printed by the native C++ fixture (camera, fov or blur), takes the `callback_read_failed` event, and feeds its numbers into the real Lua `Adapter`/manager of that policy plus the real `recovery.lua`, with a fake export table as the library. It asserts: failure is classified `temporary_read`, cleanup pending blocks the retry, backoff waits 0.25 s, a proof with `same_player=false` is refused, a fresh proof re-arms (second `Begin`), recovery ends `healthy`. Two cases per policy (cleanup immediate, cleanup pending). Receipt: `dist/<policy>-native-recovery-bridge.json`. It is a two-process contract check, not a live fault injection.

`tests/first_person/clock.lua` (30 lines) pulls the `monotonic_clock` upvalue out of the real `runtime.lua` `on_enable` and checks: seconds are not rescaled, equal samples kept, backward/negative/infinite/NaN/non-number samples and a missing engine clock all raise.

`tests/first_person/compat51_bindings.lua` (77 lines) loads `compat51.lua` in a sandboxed environment and checks the three routes to a native function address: stock CClosure header read (40 bytes, type and executable checks), the `hd2_nativeinfo` bridge (address range validation, no fallback after a bridge error), and real LuaJIT `jit.util.funcinfo` when the file is run under LuaJIT. Lua closures are refused on every route.

### (b) Fixtures in `tests/first_person/`

"Fake backend" means a Lua table with the backend methods the module calls. "Fake memory" means a byte-addressed table read through `_read/_u32/_ptr`. "Fake DLL" means a Lua table of export functions plus real cffi structs.

| fixture file | lines | module(s) under test | how inputs are fed | what is asserted (main groups) | module live or parked |
|---|---|---|---|---|---|
| action_placement.lua | 107 | action_placement.lua, loadout_catalogue.lua | Plain config tables and action samples; includes labelled live layer values (dive, prone, bridge crouch) | Profile resolve priority (weapon/action/default), ADS zero, rotation profiles, 36 labelled action-state selections, 10 bridge crouch cases, 8 dive/prone cases, roll round trip, seven catalogue families, unknown/melee refusal | live |
| action_state.lua | 95 | action_state.lua | Fake backend over fake memory (animation state machine layout) | 8 tests: resource + three copied states, null/unmatched fallbacks, bounded layers, body/player change invalidates, getter signature and vtable pins, malformed counts, cache invalidation | live (lazy from backend.lua:347) |
| aim_markers.lua | 244 | aim_markers.lua (policy, HUD binding) | Fake backend, fake HUD binding, saved-settings callbacks | 27 tests: per-weapon exceptions, global toggle, HUD write/restore ownership | live |
| aim_marker_source.lua | 94 | aim_markers.lua `source` | Fake backend + controller + optional fake fird object | Marker state in native ADS, F11 off, True FP, head fallback, Fird, hip fire; ownership | live (one case drives the parked Fird view) |
| arm_pieces.lua | 115 | arm_pieces.lua | Hand-written vector/quaternion/pose classes as fake `stingray` (Unit, Matrix4x4, Quaternion, Vector3), fake memory for bone names, links and parents | Per-side hang, torso-frame placement independent of arm pose and body lean, steady frames only place, re-link detected once, release clears, hand-back to rig bones, dead piece dropped | live |
| backend.lua | 1889 | backend.lua (+ profile.lua, pose.lua, alignment path) | Fake memory image of player, camera, weapon, controller, registry; fake native call thunks; fake helper DLL; byte pins (migrated by the runner) | 130 tests: snapshot ownership and aim derivation, signature pins, native entry/exit and uncertain results, cleanup leases, body event API, alignment and pose tokens, head-camera and FOV config, `keep_aim_view` (`:479-493`), seated camera detach, weapon-destroyed exit, Fird direct camera frame | live (some cases cover parked Fird hooks and the alignment mode) |
| backend_transition.lua | 169 | backend.lua private phase publication | Fake memory + fake DLL | 16 tests: presentation phase packet published to the helper once per change, ownership | live |
| blur.lua | 370 | blur.lua (Adapter + manager), recovery.lua | Fake DLL (`NADSB_*`), fake backend, real cffi structs | 23 tests: lease lifecycle, ABI sizes, partial Begin, cleanup pending, typed failures into recovery, runtime integration | live |
| camera.lua | 399 | camera.lua (Adapter + manager), recovery.lua | Fake DLL (`NADSC_*`), fake backend/controller, injected clock | 28 tests: preflight, arm/rearm across native6, new body, partial Begin, typed read faults, diagnostics cadence, free aim, camera motion queue, handoff easing (`set_handoff`), camera holds (`set_cut_hold`: cut/eye hold, ADS anchor, seated), weapon-in-hand report | live |
| camera_compat.lua | 196 | camera_compat.lua | Fake globals table (other mods' camera/input readers) | 17 tests: detecting and yielding to Photo Mode and other camera owners, restore | live |
| clock.lua | 30 | runtime.lua `monotonic_clock` | Fake `stingray.Application.time_since_launch` | See (a) | live |
| compat51_bindings.lua | 77 | compat51.lua | Sandboxed env, fake `hd2_nativeinfo`, fake jit tables | See (a) | live |
| controller.lua | 1746 | controller.lua, menu.lua, placement_editor.lua, and the real runtime.lua wiring (loaded 9 times with fakes) | Fake backend with scripted snapshots and leases, injected clock, fake DBFMCM | 95 tests: single native entry, alignment mode, pose_offset mode, manual persistent intent, weapon swaps, cleanup failure retention, rate limiting, swap inhibit, placement bounds, reacquisition, body event mode, timeouts, runtime start/stop blocking by FOV/camera/compat/DoF/blur cleanup, Photo Mode yield; last 6 tests drive Fird (diagnostic API, automatic entry, render owner, cached capture) | live; last 6 tests parked code |
| diagnostics.lua | 67 | diagnostics.lua | Plain sample tables | 8 tests: bounded ring (capacity), duplicate/stale frames, legitimate handoffs not view drops, counters, look duplicates, rig/lens epochs, bounded failure events, invalid values filtered | live |
| dof.lua | 349 | dof.lua (+ vignette factory) | Fake renderer settings API and fake memory for the verified SDK bridge, injected clock | 31 tests: disable once, poll cadence, unowned initial false, Photo handoff, async command coalescing, binding replacement, suppression query, DoF and vignette independent ownership | live |
| editor.lua | 527 | placement_editor.lua | Fake DBFMCM/editor API, fake stingray Gui/input, fake controller | 42 tests: open/close, drafts, save/cancel, per-weapon and family reset, bounds, F8 appearance | live |
| fird_aim.lua | 53 | fird_aim.lua | Literal poses | Sight target math, blend endpoints, invalid input refusal | parked |
| fird_auto.lua | 145 | fird_auto.lua | Callback stubs + fake clock | Entry sequencing, retries, cancellation, cached weapons | parked |
| fird_camera_pose.lua | 146 | fird_camera_pose.lua, fird_projection.lua | Fake memory with pins and a camera matrix | Pin drift, mirrored/skewed matrix, epoch mismatch, ownership change | parked |
| fird_capture.lua | 94 | fird_person.lua `capture` | Fake stingray math, fake memory | FP camera/weapon pose capture, scene lens, ADS refusal, capture token bracket | parked |
| fird_capture_ready.lua | 133 | fird_capture_ready.lua | Sample tables + fake clock | Each refusal reason, stabilisation window, tick/clock rollback | parked |
| fird_fov.lua | 117 | fird_fov.lua (over fov.lua) | Fake DLL with `NADSF_*Fird` | Lens hold lifecycle, bind, cleanup | parked |
| fird_handoff.lua | 19 | fird_handoff.lua | Callback stubs | Acknowledge, cancel, timeout, one-shot start | parked (module required nowhere) |
| fird_native_ads.lua | 83 | fird_native_ads.lua | Fake backend | Lease enter/release rules, takeover, blocked identity | parked |
| fird_person.lua | 569 | fird_person.lua, action_placement, fird_projection, fird_aim, fird_retarget, runtime.lua | Fake stingray, fake memory, fake managers | 12 printed groups: linked adapter edits, reattachment waits, grip transform, head-camera edits, weapon lock, scene-FOV owner, failed restoration, resource-bound references | parked |
| fird_presentation.lua | 87 | fird_presentation.lua | Fake backend/memory | Presentation lease start/stop/ownership | parked (module required nowhere, not in manifest) |
| fird_projection.lua | 188 | fird_projection.lua | Literal poses | Camera-relative weapon lock, follow, rebase, lens bounds | parked |
| fird_render.lua | 136 | fird_render.lua | Fake globals with `render` / `HUDRenderBridge` | Bridge vs wrapper install, reentrancy, owner change, restore | parked |
| fird_retarget.lua | 242 | fird_retarget.lua, fird_projection.lua | Literal skeleton tables | Arm solve lengths, poles, twist, finger map, refusal of bad input | parked |
| fird_source_pose.lua | 107 | fird_source_pose.lua | Fake memory with 64-byte locals | FK reconstruction, parent links, bounds | parked |
| fird_warm.lua | 60 | fird_warm.lua | Fake backend + clock | One warm transaction, foreign camera, cleanup | parked |
| fird_weapon_children.lua | 133 | fird_weapon_children.lua | Fake memory graph, fake `World.update_unit` | Child discovery limits, publish, validity | parked |
| first_person_movement.lua | 289 | first_person_movement.lua | Fake backend over fake memory (movement flag byte, code fingerprints), fake Keyboard | 25 tests: S key owns one focus flag and restores exact zero, foreign byte preserved, pin mode, pool relocation, six code fingerprints; last 8 tests are "natural Fird focus" (`update_fird`) | live; last 8 tests parked path |
| fov.lua | 537 | fov.lua (Adapter + manager), recovery.lua, runtime.lua | Fake DLL (`NADSF_*`), fake backend/controller | 30 tests: lease lifecycle, bind to request, transition packet, ABI 80/88, typed failures, runtime integration | live |
| grip_hand.lua | 357 | grip_hand.lua | Hand-written vector/quaternion/pose classes as fake `stingray`; fake memory for names, links, parents; injected clock | `G.solve` (4 calls: bone lengths kept, reach, elbow side, 95 % stretch clamp), `G.weight` (5 calls); manager: lookup cadence, rig mesh hidden with the left correction, hand on grip bone with finger shape from the body, two grip bones, body hand leaving the bone, walking lag, unpinned weapon left alone, forearm roll, right arm via the weapon bone, known grip remembered across view end and weapon swap (`:203-210`), body-only weapon spin incl. one-handed hold, pause, concurrent rig animation, view end mid-spin (`:250-335`), fast re-lookup on weapon change (`:337-357`), dead weapon | live |
| head_placement.lua | 12 | head_placement.lua | Fake backend + camera | Bounds, saved default, ownership refusal, player generation, draft restore | live |
| head_visibility.lua | 820 | head_visibility.lua, model_shadow_source.lua | Fake SDK (`Unit`/`Mesh` visibility and shadow setters), fake memory for mesh arrays, renderer masks, executable pins | 71 tests: hide only originally visible head meshes, exact restore, array reorder, retired generations, foreign masks, SDK bridge pins, three head/torso layers, shared renderer refusal, cache invalidation, fallback gating, shadows for native-hidden parts, shoulder pads, seated head | live |
| held_fire.lua | 276 | held_fire.lua | Fake DLL (`NADSHF_*`) | 23 tests: admission/filter lifecycle | parked |
| locomotion.lua | 115 | locomotion.lua | Literal 4x4 float arrays (real cffi), fake backend for the sampler | 8 tests: camera-local to parent basis, sprint signal (L3 on-foot), shoulder aim forwarding, sprint rotation, scale handling, degenerate input refusal; 6 bridge sprint variants | live |
| menu.lua | 418 | menu.lua | Fake DBFMCM, fake LiveLuaLoader, fake controller/editor | 34 tests: registration, hotkeys, option getters, composition/motion/clipping menus | live |
| motion.lua | 242 | motion.lua | Literal samples `{owner,x,y,z,yaw,pitch}` and `dt` | 20 tests: no fabricated bob, frame-rate independence (30/60/120), turn inertia bound, sprint hold, shoulder aim easing, ADS neutral, resets on owner change/teleport/gap, hitch handling, scale zero | live |
| motion_runtime.lua | 428 | motion_runtime.lua (+ motion, locomotion, action_placement) | Fake controller/backend/editor, injected clock, injected sampler | 30 tests: when samples are taken, coalescing, ADS blend and reversal, shared phase, pop guard at 30/60/144 FPS, failure easing | live |
| native_ads_option.lua | 118 | native_ads_option.lua | Fake source with one option record, fake memory for the selector | 12 tests: lease while enabled, restore, weapon retirement, foreign camera deferral, remember modes, pin mode cleanup | live |
| near_clip.lua | 131 | near_clip.lua | Fake DLL (`NADSN_*`), fake backend, injected clock and options | 10 tests: off/native modes never install, `pull_sight_distance` arms with zero distances, live value changes, 4 Hz health, refusals retried once a second, 72/88-byte ABI | live |
| pose_adapter.lua | 209 | pose.lua | Fake DLL (`NADSE_*`) | 12 tests: adapter ABI, placement/motion/transform setters, diagnostics | live (lazy from backend.lua:807) |
| ready_pose.lua | 445 | ready_pose.lua | Fake source object | Intent, compare-and-restore, retries, filter owner | parked |
| ready_pose_backend.lua | 921 | ready_pose_source, _resources, _timing, _contract | Fake FFI and fake memory | Adapter, resource proof, quiescence reader | parked |
| recovery.lua | 97 | recovery.lua | Plain state/proof tables and times | 8 tests + 2 groups: only typed read failures with full proof retry, unknown faults disable the feature, pending cleanup blocks, finite paced attempts, flapping, status is read-only | live |
| runtime_frame.lua | 238 | runtime.lua `on_update` frame wrapper | Loads the real runtime and swaps upvalues by `debug.setupvalue` (backend, controller, camera, compat, editor, menu, aim_markers, fird, fird_entry, ready_pose, held_fire, foreign_camera, handoff_ready, stopping, update_frame) | begin/end frame balance incl. errors; ready-pose qualified Update; held-fire disarm order; eye camera owning effects through seated/action cameras; effects suppressed through a weapon swap | live wrapper; 3 of 5 groups about parked ready pose / held fire |
| runtime_recovery.lua | 191 | runtime.lua update orchestrator + recovery.lua + camera.lua | Real runtime with upvalues replaced by synthetic managers (`update/stop/is_clean/result`) | Per-policy failure, cleanup, backoff, proof and re-arm inside the real update order | live |
| skinned_placement.lua | 244 | skinned_placement.lua | Fake stingray math + fake memory | 7 printed groups on linked skin placement and ownership | parked |
| support_hand.lua | 23 | support_hand.lua | Tiny fake Vector3/Quaternion/Matrix4x4 | Hold/fire layers, reload yield, bounds, reset | parked |
| transition.lua | 107 | transition.lua | Literal snapshots and times | Sequential script with numeric expectations: ADS enter/leave epochs, progress 0.5 at half duration, native 4/6 handoff, merged zoom/action/seat edges, identity retirement, hitch, immutable copy, frame rates; `native_ads` classification (mask 0/2 only) | live |
| vignette.lua | 63 | vignette.lua | Fake world/level entities with a vignette component, injected clock | 9 tests: disables the actual component and restores once, native reset, retired components, foreign camera, world replacement, staggered discovery | live |
| zoom.lua | 91 | zoom.lua | Fake DLL | 6 tests: zoom lifecycle | parked (module required nowhere, not in manifest) |

### (c) The `*_binary.py` tests

Six files, all upstream Windows scripts: they use `ctypes.WinDLL` and default to old versioned names under `dist/native/` that this tree does not produce (`native_ads_blur_0_1_16.dll`, `native_ads_camera_0_1_27.dll`, `native_ads_editor_0_1_43.dll`, `native_ads_fov_0_1_23.dll`, `native_ads_alignment_0_1_5.dll`, `native_ads_pose_0_1_7.dll`). `ROOT` resolves to `tests/`, so the default paths are also wrong for this layout. Nothing in R calls them.

What they check is a cold load in a separate process, with no game:

| file | DLL | checks |
|---|---|---|
| native_binary.py (58) | alignment | Config and Stats both 72 bytes, field offsets, 5 exports `NADS_Validate/Begin/End/Error/ReadStats`, zero config refused, stats unarmed; writes sha256 receipt |
| pose_binary.py (62) | pose | Config 96 / Stats 80, offsets, 5 exports `NADSP_*`, zero config refused |
| editor_binary.py (83) | editor | Config 104 / Stats 80 / DepthStats 104, 11 exports `NADSE_*`, zero token cannot edit or end, wrong size/ABI/reserved refused |
| fov_binary.py (25) | fov | Config 80 / Stats 88, 6 exports `NADSF_*`, zero token refused |
| camera_binary.py (30) | camera | Config 88 / Stats 80, offsets, 5 exports `NADSC_*`, cold stats zero, sha256 compared with `native-camera-build.json` if present |
| blur_binary.py (36) | blur | Config 80 / Stats 80, 5 exports `NADSB_*`, zero config refused |

They pin struct sizes, a few field offsets and export names. They do not pin DLL bytes; the only hash check is camera's optional comparison with its own build receipt. Note the struct sizes are those of the old upstream DLL versions; current sizes are asserted by the Lua fixtures (for example near 72/88, `tests/first_person/near_clip.lua`).

### (d) Native C++ fixtures

All helper fixtures use the same pattern: define a `*_FIXTURE` macro, `#include` the production `.cpp`, and drive it against private buffers and local callbacks in a separate process. None touches a game process.

Current build path (Linux): `tools/build_first_person.py` cross-compiles each helper and its fixture with `zig c++ -target x86_64-windows-gnu -std=c++17 -fms-extensions -O2`, runs the fixture `.exe` under wine, writes `dist/first-person/<fixture>.log` and `build.json`, copies the DLL into `native/` and rewrites the DLL names in `src/first_person/profile.lua` (`tools/build_first_person.py:6-29`). It covers five pairs: alignment/fixture, editor/editor_fixture, fov/fov_fixture, camera/camera_fixture, blur/blur_fixture. The per-helper `native/first_person/build*.py` scripts are the upstream MSVC (`cl`/`link` via `vcvars64.bat`) builders and only work on Windows.

| fixture | lines | covers | built and run by |
|---|---|---|---|
| native/first_person/fixture.cpp | 161 | Alignment helper: atomic dispatch-pointer install/restore, owner/caller/unit scoping, CAS rollback, foreign preservation, TTL | tools/build_first_person.py (zig + wine); build.py (MSVC) |
| native/first_person/editor_fixture.cpp | 1152 | Editor/pose helper: placement, ADS placement, motion, transforms, depth policy; prints PERF read counts | tools/build_first_person.py; build_editor.py (MSVC) |
| native/first_person/fov_fixture.cpp | 649 | FOV helper incl. the Fird entry points (28 mentions); prints `RECOVERY_EVENT` lines for the recovery bridge | tools/build_first_person.py; build_fov.py (MSVC) |
| native/first_person/camera_fixture.cpp | 909 | Camera helper (head anchor, native6 pass-through, free aim, holds, handoff); prints `RECOVERY_EVENT` | tools/build_first_person.py; build_camera.py (MSVC) |
| native/first_person/blur_fixture.cpp | 331 | Blur (weapon DoF maximum filter) helper; prints `RECOVERY_EVENT` | tools/build_first_person.py; build_blur.py (MSVC) |
| native/first_person/pose_fixture.cpp | 184 | Old shared-rig pose helper (`native_ads_pose.cpp`) | build_pose.py only (MSVC). Not in tools/build_first_person.py; no `native_ads_pose_*.dll` is shipped in `native/` |
| native/first_person/input_fixture.cpp | 190 | Input (zoom/look) path of the editor helper, wraps editor_fixture; needs `TFP_INPUT_CAPTURE_DIR` with mapped game/exe captures; with `input_fixture_thunks.asm/.S` | build_editor.py only (MSVC, `ml64`) |
| native/first_person/input_production_smoke.cpp | 33 | Loads the built editor DLL and checks exports and struct sizes Config104/Stats80/Depth104/Input96 | build_editor.py only |
| native/first_person/zoom_input_math_fixture.cpp | 200 | `zoom_input_math.h`: SSE-exact filter arithmetic against a captured-exp harness; prints case counts and max error | no build script in R references it |
| native/first_person/zoom_gain_math_fixture.cpp | 176 | `zoom_gain_math.h` (includes the fixture above under a renamed main): gain pipeline, accepted vs bypass reasons, max increment and pitch errors | no build script in R references it |
| native/first_person/camera_correction_fixture.cpp | 131 | `camera_correction.h`: residual bounds, easing state; pure host arithmetic | no build script; plain host compiler |
| native/first_person/camera_handoff_fixture.cpp | 61 | `camera_handoff.h`: position ease, rotation slerp, proof; its first line gives the command `g++ -std=c++17 -O2 camera_handoff_fixture.cpp -o /tmp/handoff && /tmp/handoff` | manual, host g++ |
| native/near_clip/near_fixture.cpp | 250 | Near-clip helper | native/near_clip/build_near.py (zig + wine, then installs the DLL into `native/` and edits profile.lua) |
| native/held_fire/held_fire_fixture.cpp | 116 | Held-fire data-slot filter | native/held_fire/build_held_fire.py (zig + wine; `--build-only` skips the run) |
| native/held_fire/held_fire_dll_smoke.cpp | 29 | Loads the production held-fire DLL: seven exports, ABI 144/184, invalid-call cleanup, SEH on a read-only page | build_held_fire.py |
| native/held_fire/held_fire_seh_test.cpp | 20 | Test-only DLL built with production flags that faults on private memory, used by the smoke | build_held_fire.py |
| native/zoom/zoom_fixture.cpp | 151 | Zoom (optic policy) helper | native/zoom/build_zoom.py (zig + wine) |
| native/zoom/minhook_smoke.cpp | 46 | Real MinHook install/restore on a local function | build_zoom.py |

`build_near.py`, `build_zoom.py` and `tools/build_first_person.py` all modify `native/` and `src/first_person/profile.lua` as a side effect of a successful run.

Other tools, one line each:

- `tools/record_first_person.py`: records `NativeADSCameraTest.diagnostics()` frames from the running game through LLtM into `recording.tsv` (read-only).
- `tools/live_first_person.py`: submits explicit diagnostic Lua commands to the installed LLtM addon in the running game.
- `tools/map_first_person.py`: reproduces the address-mapping evidence for the build migration from the binaries in `native-work`; installs nothing.
- `tools/migrate_first_person.py`: applies the reviewed address bindings to the imported upstream source once; refuses a second pass.

### (e) Live pure logic that can be driven from another language

| module | inputs | outputs | reusable vectors in the existing fixture | shim needed |
|---|---|---|---|---|
| motion.lua (163) | `Motion.new(options)`, `configure(value)`, `update(sample,dt)` with `sample={owner,x,y,z,yaw,pitch,...}` | `{dx,dy,dz,pitch,yaw,roll,phase,reset,...}` | Inputs yes (generated sequences at 30/60/120 FPS). Expected outputs mostly relational (bounds, equality between frame rates, exact zero), few absolute numbers. Reuse the input sequences and record the Lua outputs as golden values. | None beyond `PORT51.kind` (= `type`). No engine, no clock. |
| locomotion.lua (162), pure part | `rotation_to_parent(value,camera,parent)`, `to_parent(value,camera,parent)` with 16-float matrices; `sprinting(action,verified,flags)` | 3-vector / quaternion; boolean | Yes: literal matrices and expected vectors compared with 1e-9 tolerance; 6 sprint variants | The `M.new(backend):sample` part reads memory and needs a fake backend; the three functions above need nothing. |
| action_placement.lua (139) | `configure_catalogue(gear)`, `resolve(config,key,action)`, `select(sample,mapping)`, `saved(...)`, `valid(p)` | placement `{x,y,z,yaw,pitch,roll}`; action name | Yes, verbatim: 36 labelled action-state samples, 10 bridge crouch, 8 dive/prone, with exact expected action names and placements | Needs `src/loadout_catalogue.lua` data as input. |
| action_state.lua (129) | `sample(expected_snapshot, layers)` over raw memory | `{resource, resource_match, l1,l3,l10,l11,l14, layer_count,...}` | Partly: the fixture builds a synthetic memory image and expects exact fields | A byte-addressed fake memory with the same layout; the C port must expose its reader behind an interface. |
| recovery.lua (124) | `classify(name,state,clean,proof)`; `failure/cleanup/action/revalidate/success(name,...,now)` | phase rows, action strings (`wait`, `update`, `cleanup`), `snapshot()` | Yes, verbatim: explicit times and expected strings; also `tests/native_recovery_bridge.lua` | None. Pure tables and numbers. |
| transition.lua (105) | `new({duration,max_gap})`, `update(snapshot,now,enabled,blocked)`, `action(action,now)`, `resume(identity,player,now)`, `native_ads(s)` | packet `{epoch,progress,ads,reason,merged,continuous,...}` | Yes, verbatim: sequential script with exact times and expected epoch/progress/reason (for example progress 0.5 +-1e-5 at half duration) | None. The same phase packet is consumed by `presentation_phase.h`. |
| diagnostics.lua (84) | `record(frame,sample)`, `event(frame,name,sample)` | bounded `snapshot()` | Yes: exact counts and ordering | None. |
| grip_hand.lua `solve` and `weight` | `solve(V,S,E,H,T)`: vector class plus shoulder, elbow, hand, target; `weight(distance)` | new elbow and hand positions; 0..1 weight | Yes: 4 solve cases and 5 weight cases with numeric checks | A 3-vector type with `+ - *`, dot, cross, length as the fixture's `vmt` provides. |
| grip_hand.lua manager, arm_pieces.lua manager | `update(snapshot, active, handles)` each frame | Local-pose offsets written to linked piece bones; mesh visibility; `result()` | Yes as scenarios: the fixtures define a complete small rig (63 bones, links, parents) and assert resulting world poses | Largest shim: fake `Unit/Matrix4x4/Quaternion/Vector3` with the engine's row-matrix composition order ("local, then parent"), fake memory for bone names/links/parents, injected clock. The fixtures' own math classes are the reference for the composition order. |
| controller.lua (589) | `Controller.new(backend,log,clock)`; `start/stop/update/set_placement/...` against a backend that returns scripted snapshots and leases | Calls made on the backend (enter, release, prepare, pose), `result()` phases | Yes as scenarios: 95 tests script snapshots and count backend calls | A scripted fake backend and clock in the other language; the call sequence is the comparable output. |
| native/first_person/presentation_phase.h (30) | Phase packet (epoch, progress, ads) and time | Eased phase `p*p*(3-2p)` state | No fixture of its own; exercised inside editor_fixture.cpp / fov_fixture.cpp and by backend_transition.lua on the Lua side | Header-only; can be compiled into both sides unchanged. |
| native/first_person/free_aim_math.h (45) | Quaternions, yaw/pitch, 4x4 matrix | Quaternion ops, `look`, `angles`, `matrix_q` | No own fixture; used by camera_fixture.cpp and camera_handoff_fixture.cpp | Header-only, pure. |
| native/first_person/camera_handoff.h (117) | `Ease`, `Position`, `Rotation`, `Proof` states, positions/quaternions, dt | Eased position/rotation | Yes: camera_handoff_fixture.cpp, host-compilable with plain g++ | None. |
| native/first_person/camera_correction.h (82) | Relative positions, residuals, time | Corrected position, ease state | Yes: camera_correction_fixture.cpp ("%u groups"), pure host arithmetic | None. |
| native/first_person/zoom_input_math.h (108) | Raw input, history, filter coefficients, ratios | Filtered values with SSE scalar exactness | Yes: zoom_input_math_fixture.cpp with max_abs_error report | Needs SSE intrinsics (`_mm_*_ss`) and a captured `exp` function pointer; x86-64 only. |
| native/first_person/zoom_gain_math.h (144) | `Input` (axis values, gain, context, dt), native exp function | `Prepared` gains, `Reason` | Yes: zoom_gain_math_fixture.cpp (accepted, bypass counts, max errors) | Same as above. Included by the editor helper (`native_ads_editor.cpp:13`). |
| native/first_person/camera_transition.h (60) | Pose, phase | Blended pose | None | Included by no `.cpp` in R (only hashed in `build_camera.py`); treat as dead until proven otherwise. |
| native/first_person/pin_look_math.h (30) | Look state | Clamped look | None | Included by no file in R; dead. |

### (f) Live modules or behaviours with no test or only thin coverage

- **Runtime wiring of sleeves and grip** (`runtime.lua:540-559`): the conditions under which `arm_pieces:update` and `grip_hand:update` are active (`state.phase=='running'`, `camera_kind==6`, `control_mask==0`, menu "Fird Person Arms", `profile.camera.body_sleeves` / `grip_hand`) are not exercised. No fixture mentions `body_sleeves`; `arm_pieces` and `grip_hand` appear only in their own module fixtures. The pcall-guarded optional load (`runtime.lua:699-706`) is untested too.
- **Runtime update order as a whole**: `runtime_frame.lua` replaces `update_frame` itself with a stub for the frame-balance tests and otherwise swaps single upvalues; `runtime_recovery.lua` replaces every manager by a synthetic one. No test runs the real `update_frame` with the real modules in order, so the order of controller, fov, camera, head, movement, dof, near, motion, markers, sleeves, grip is asserted nowhere.
- **`detach_seated_camera` call site** (`runtime.lua:540-542`, edge on `seated_view`): the backend function has one test ("the seated camera detach only acts in a seated normal-camera state", tests/first_person/backend.lua); the edge trigger in runtime has none.
- **`keep_aim_view`**: backend function covered (`tests/first_person/backend.lua:479-493`); its caller in runtime is not.
- **Seated head**: covered in head_visibility.lua on the visibility side and by `set_cut_hold(...,seated)` in camera.lua on the Lua side; the helper-side behaviour is only in camera_fixture.cpp. The runtime option closure `seated_head` (`runtime.lua:696`) is untested.
- **Cut hold / eye hold / ADS anchor**: one Lua test ("camera holds follow presentation, stay optional and switch live", tests/first_person/camera.lua) checks that values are forwarded to the fake DLL. The actual holding arithmetic lives in `native_ads_camera.cpp` and is only covered by camera_fixture.cpp.
- **Near `pull_sight_distance`**: one Lua test checks that the helper is armed with zero distances; the effect is in the helper (near_fixture.cpp). No live-value check.
- **grip_hand**: well covered at module level including the spin and the remembered grip; not covered: real engine math (the fixture uses its own matrix classes), the spin thresholds against recorded live data, and anything two-handed-to-one-handed beyond the scripted case. Memory says smooth.66 weapon-swap changes still await a user check.
- **arm_pieces**: one scenario file, 115 lines, a single synthetic rig; no test with a second armour set or with missing collar bones beyond "dead piece".
- **vignette.lua**: 9 tests with a fake world; fine at module level, no integration with dof beyond the injected factory in dof.lua.
- **model_shadow_source.lua**: no fixture of its own; loaded only inside head_visibility.lua (and constructed in controller.lua/fov.lua runtime loads). Thin.
- **ui_style.lua (280 lines)**: no test. It is only pulled in through placement_editor.lua by the loader's text replacement.
- **shared_input.lua / input_adapter.lua**: only `tests/first_person_input.lua` (51 lines, outside the 55-file suite, run by hand).
- **native_ads_option.lua**: 12 tests, adequate at module level; the `mod.require('src/memory.lua')` source path (`native_ads_option.lua:53`) is replaced by a fake.
- **`src/first_person.lua` facade**: `tests/first_person_integration.lua` only (98 lines, run by hand). Covers `host`, `menu_api`, `sync`, enable/update/shutdown; `poll_cleanup` (`src/first_person.lua:158`), `changed`, `revision`, `status` and `pages` are not asserted.
- **profile.lua**: no test; it is data, but the DLL names in it are rewritten by the build scripts.
- **compat51.lua**: only the native-address routes are tested; the cffi number/kind helpers are exercised indirectly.
- The whole 55-file suite runs on stock Lua 5.1 with cffi, while the game runs LuaJIT. Only `compat51_bindings.lua` has a LuaJIT branch, and nothing in R runs it under LuaJIT automatically.

## 7. Behaviours list (acceptance checklist)

One sentence each, in the order a player meets them, with the implementing module. Everything listed is on the shipped path unless the sentence names an option that is off by default. Items 1 to the end are numbered continuously.

**Switching on, keys and menu**

1. F11 (rebindable) turns True First Person on and off, firing once per key press. (fp/menu.lua)
2. Shift+F8 does the same as F8. (fp/menu.lua)
3. Neither hotkey fires while the MCM menu is open, while a key is being rebound, while the game window or MCM binding focus is lost, or while another frontend owns input. (fp/menu.lua)
4. A hotkey that is the same as MCM's own menu key is ignored, and if F11 and F8 are pressed on the same update only the F11 toggle happens. (fp/menu.lua)
5. A key already held when the hotkeys become available again does not fire until released and pressed again. (fp/menu.lua)
6. Setting a keybind to 0 disables that hotkey. (fp/menu.lua)
7. The kit's MCM entry gains a "First Person" page and a "Bob and Sway" page after its own pages. (src/first_person.lua, src/mcm.lua)
8. "Enable True First Person" starts the view and is greyed out while the view is on, while cleanup is pending or while the editor is open. (fp/menu.lua)
9. "Turn first-person view off" stops the view and cancels an open editor. (fp/menu.lua)
10. The status line shows the controller's current state text, or a suspension or aim-marker fault message when one exists. (fp/menu.lua)
11. The weapon line names the current firearm and its family, or asks to enable First Person with a firearm. (fp/menu.lua)
12. All menu values and saved placements persist across sessions. (fp/menu.lua, src/first_person.lua)
13. The first-person menu pages live inside the kit's MCM entry and their status text and greyed-out state follow the running mode (src/first_person.lua menu_api, sync).

**Entering the view**

14. F11 with a firearm held, on foot, not aiming and nothing blocking enters the game's native first-person camera at once and reports "First person enabled until manually stopped" (fp/controller.lua start, fp/backend.lua prepare_pose/_enter).
15. F11 while aiming, mid-action, seated, in another camera or without a firearm still switches the session on and waits ("First person enabled; waiting: ..."), entering by itself when the state clears (fp/controller.lua:242-258, manual_update).
16. F11 is refused outright (session stays off, message "Start refused before camera entry: ...") when the pose preparation itself fails, for example while the packed first-person code pages are not readable yet or the pose helper refuses validation (fp/controller.lua:305-311, fp/backend.lua:1038).
17. The view only enters when the game's own "weapon supports first person" predicate says yes; otherwise it keeps waiting with "Weapon does not support native FPV" (fp/backend.lua:582-583).
18. The native view is entered only from the normal third-person camera with aim released: never while the game's own ADS or aim bit is on (fp/backend.lua:579, fp/controller.lua:452).
19. With the Breaching Hammer no native arms view is requested at all: the session shows the head camera with third-person animations (fp/backend.lua:317-326, fp/controller.lua:282-289, 402-411).
20. A respawn or new world (new player identity) releases the old view and binds the session to the new body (fp/controller.lua:387-389, 475).

**Arms and weapon in the rig view**

21. The first-person arms and weapon are moved as one piece by a per-weapon saved offset (default 0.15 right-axis, 0, -0.12) added to the game's own animated position of rig node 5 every frame, so all native arm and weapon animation still plays (`native_ads_editor.cpp` position_call; `backend.lua` _pose_config).
22. The arms and weapon sit at the saved placement for this weapon and action: per weapon, else "all weapons", else legacy per-weapon, else the global placement (default 0.15 right, 0 forward, -0.12 up), with ADS defaulting to zero offset (fp/action_placement.lua resolve).
23. With "Use weapon-family placement defaults" on and no customised placement, each weapon family gets its own default offset (fp/action_placement.lua:95-100).
24. The placement used is chosen by the current action in this priority: ADS, slide, dive, reload, normal aim, sprint, crouch, prone, custom mapping, idle (fp/action_placement.lua select).
25. Each action (idle, normal aim, ADS, sprint, reload, crouch, prone, slide, dive) can have its own saved placement, and the weapon eases between them. (fp/motion_runtime.lua, fp/action_placement.lua)
26. The offset is fully applied in hip fire and normal (shoulder) aim and fades to zero in full sights, following the mod's shared transition progress or, without it, a 150 ms smoothstep (`native_ads_editor.cpp`, `presentation_phase.h`).
27. In full sights a separate per-weapon "ADS placement" offset is added at full strength (`native_ads_editor.cpp`; `motion_runtime.lua`).
28. When a weapon comes up (start, swap, action recovery) it rises into place over 220 ms from 0.35 lower and 0.06 further back with an ease-out, and its last known placed position is written at arming so it does not appear for one frame where the previous weapon was (`native_ads_editor.cpp`).
29. For the first 300 ms after arming, a wild node 5 rotation written by the game is replaced by the rig's last settled rotation, so arms and weapon do not swing out of view on entry (`native_ads_editor.cpp` entry_rotation).
30. With a multi-zoom scope outside full sights, the weapon stays at the distance of the lowest magnification instead of moving with the selected zoom level, and the rig root is counter-shifted so the view does not slide (`native_ads_editor.cpp` depth_delta, carrier_position).
31. A new weapon's first placement lands at once; later placement changes ease in. (fp/motion_runtime.lua)

**Lens, clipping, blur and post effects**

32. In first person at the hip the field of view equals the player's own in-game third-person FOV setting, read live each camera update, instead of the weapon camera's native lens (native_ads_fov.cpp `source`/`setter_hook`; fov.lua).
33. Changing the in-game FOV slider while in first person takes effect immediately, since no FOV value is cached (fov.lua:2-3).
34. Holding normal (shoulder) aim without full ADS narrows the view by 5 % focal magnification, eased by the shared transition phase or a 120 ms tween (native_ads_fov.cpp `shoulder_lens`).
35. Geometry closer than the chosen hip distance (default 15 cm) is clipped away in first person at the hip (native_ads_near.cpp; near_clip.lua; menu.lua `near_options`).
36. During normal aim the near clip returns to the game's 5 cm unless "Near clip while aiming" is on or the weapon has its own normal-aim clearance (menu.lua:144-149, 172-182; native_ads_near.cpp).
37. Each weapon can have its own saved hip and normal-aim clearance (5..40 cm) that overrides the global slider (menu.lua:184-202).
38. Outside full ADS a scope's own larger near distance (e.g. 0.22 m / 0.35 m) is pulled back so a scoped weapon's viewmodel is not cut away at the hip (native_ads_near.cpp:204-226; profile.lua `pull_sight_distance`).
39. Near-clip changes glide (about 45 ms time constant) instead of popping when switching between hip, aim and ADS (native_ads_near.cpp `ease`).
40. "Near clip distance" and "Near clip while aiming" set the global hip and aim near clip. (fp/menu.lua)
41. Per-weapon hip and normal-aim clearance sliders are drafts that apply only after "Save clipping for this weapon"; "Use global clipping for this weapon" removes the weapon's profile. (fp/menu.lua)
42. Switching weapons resets the per-weapon clipping sliders to that weapon's saved or global values. (fp/menu.lua)
43. The held weapon's sight blur (three depth-of-field maxima of its optic component) is switched off in first person, at the hip and in ADS, while the rest of the game's depth of field and effects stay native (native_ads_blur.cpp; blur.lua).
44. Weapons without such an optic component are left alone and rechecked every 0.5 s in case one appears (blur.lua:235-241, 263-268).
45. Depth of field is switched off while the first-person view is shown, in the rig view and through the head-camera gap, and is switched back on afterwards (dof.lua).
46. If the game switches depth of field back on during the view (optic or zoom change) the mode switches it off again at the next half-second poll (dof.lua).
47. If depth of field was already off when the view began the mode does not take ownership and does not switch it on at exit (dof.lua).
48. The screen vignette is switched off on every vignette shading-environment component while the first-person view is shown, re-checked ten times a second, and switched back on afterwards (vignette.lua).

**Body visibility and shadows**

49. In the native rig view (kind 6) the player's own head/helmet renderer (appearance slot 0) is not drawn by the camera but still casts its shadow (head_visibility.lua).
50. In kind 6 the torso renderer (slot 2) is not drawn by the camera but still casts its shadow, regardless of the show-torso option (head_visibility.lua).
51. In kind 6 and in the head-camera gap view the shoulder renderers (slots 8 and 9) are not drawn by the camera but still cast shadows (head_visibility.lua).
52. The equipped backpack is not drawn by the camera but still casts its shadow while the mode shows first person (head_visibility.lua).
53. The body unit's own meshes are left visible to the camera and have their shadow pass forced on (head_visibility.lua).
54. The limbs in appearance slots 3 to 7 are never hidden, and a mesh shared between a hidden and a kept renderer makes the whole head-hiding stand down with everything restored (head_visibility.lua).
55. The first-person rig's two shoulder-pad meshes (`g_l_shoulderpad`, `g_r_shoulderpad`) are hidden, and in kind 6 they cast no shadow either (head_visibility.lua).
56. In kind 6 the first-person rig, the arm pieces hung on it and every unit of the held weapon tree cast no shadow, so the only player shadow is the third-person body's (head_visibility.lua, model_shadow_source.lua).
57. When the mode is switched off, another camera takes over, the player dies/enters a vehicle or any check fails, every pass bit the mode changed is put back to the value it had, unless the game has meanwhile published its own full visibility (head_visibility.lua).
58. Visibility changes made by the game or another mod to a mesh the mode owns are left alone until the next view/weapon/aim change (head_visibility.lua).

**Sleeves and hands**

59. In the rig view the sleeve tops, collar and chest cloth of each arm piece sit as on the third-person torso, anchored at the rig's shoulder of the same side, with a separate chest per side (arm_pieces.lua).
60. The sleeves are found within the first 0.6 s of the arm view (looked up every update), then every 0.25 s until found, and once more 0.5 s later (arm_pieces.lua).
61. If the game re-links a piece (its chest bone drifts more than 2 cm from where it was put) the piece is linked again in the same update (arm_pieces.lua).
62. When the arm view ends the placed bones get an identity local pose so the sleeves are not shifted on the body, and at mode stop the game's original links are restored (arm_pieces.lua).
63. The left hand of the first-person arms sits on the weapon's left-hand grip bone, with position and rotation blended fully when the animated hand is within 20 cm of it and not at all beyond 32 cm (grip_hand.lua).
64. The grip bone used is the one the third-person body's left hand is on (within 2 cm), and it stays in use while the body's hand wanders off during movement (grip_hand.lua).
65. On a weapon whose grip the body's left hand never reaches, the first-person animation's left hand is left as it is (grip_hand.lua).
66. The left hand's fingers take the shape the body's left hand had while on the grip, and keep the last such shape afterwards (grip_hand.lua).
67. A newly found grip eases in over 0.15 s; a grip remembered for that weapon unit is used at full strength at once when the arm view returns (grip_hand.lua).
68. The arm never stretches beyond 95 % of its length (or the animated reach if longer), the hand stopping short of the grip instead, so the elbow keeps a bend (grip_hand.lua).
69. The forearm takes the part of the hand's turn that goes around the forearm axis, so the wrist screen stays under the forearm armour (grip_hand.lua).
70. While the left arm is corrected the rig's own two-material mesh (its wrist screen) is hidden, and it is shown again when the correction ends (grip_hand.lua).
71. The right hand is placed as the third-person right hand sits relative to the weapon bone, with the body's finger shape every frame, using the same 20/32 cm blend (grip_hand.lua).
72. During a reload or throw, when the animated hand is more than 32 cm from its target, the correction is off and the game's animation shows unchanged (grip_hand.lua).
73. On a one-handed weapon that the body spins in its hand (revolver spin), the weapon on the rig turns, the right hand follows the body's hand motion, and the hold lasts until hand and weapon are back within 12 degrees / 3 cm or 3 s after the last movement (grip_hand.lua).
74. During such a spin the left hand moves aside to where the body's left hand is, easing out and back over 0.2 s (grip_hand.lua).
75. Spin handling never runs while aiming down the sights, on two-handed weapons, or while the rig plays an animation of its own (grip_hand.lua).
76. Arm corrections are off while any of the first four controller bytes is non-zero, in the gap view, when the menu option "Fird Person Arms" is off, and when another camera owns the view; offsets are cleared on that update (grip_hand.lua, arm_pieces.lua, gate in runtime.lua:545-558).
77. Arm pieces are looked up again when aiming starts or stops, on a weapon change edge (at once and 0.1, 0.25, 0.5 s later), once a second, and every update for the first 0.6 s of the arm view while something is missing (grip_hand.lua).
78. An arm is only corrected once its own hand bone is among the pieces found, so armour is never moved apart from its arm (grip_hand.lua).
79. Both arm modules stay on in the placement editor (runtime.lua:547 comment; grip_hand.lua, arm_pieces.lua).

**Weapon motion**

80. "Weapon movement" off removes bob, sway and the sprint pose; "Weapon movement strength" scales them from 0 to 200 percent. (fp/menu.lua, fp/motion.lua)
81. The three checkboxes switch walking bob, sway and the sprint pose off individually. (fp/menu.lua)
82. Motion presets Comfort and Natural replace the five Bob and Sway slider values without overwriting them; Custom uses the sliders. (fp/menu.lua)
83. The Bob and Sway sliders set bob strength, bob speed, sway strength, sway response speed and sprint movement. (fp/menu.lua, fp/motion.lua)
84. While walking, the weapon bobs side to side and up and down with distance travelled, one cycle per 3.4 m, growing with speed up to 4 m/s. (fp/motion.lua)
85. When turning or looking up and down, the weapon lags behind the view by a bounded amount and catches up. (fp/motion.lua)
86. When strafing or moving forward and back, the weapon shifts slightly against the movement. (fp/motion.lua)
87. While sprinting, the weapon lowers and rotates (pitch -12, yaw +18, roll -10 degrees at full strength) and walking sway fades out. (fp/motion.lua)
88. The sprint pose holds for .10 s after sprint input stops and then eases back. (fp/motion.lua)
89. Pressing or releasing normal aim fades bob and sway out or in over about a tenth of a second. (fp/motion.lua)
90. In full ADS there is no added bob, sway or sprint pose. (fp/motion.lua)
91. After a stutter, teleport or launch the weapon does not jump; remaining motion eases from where it was, and gaps over one second start clean. (fp/motion.lua)
92. Bob, sway, sprint and shoulder motion plus the saved yaw/pitch/roll reach the rig as a small translation and a rotation multiplied onto the game's own node 5 rotation; the rotation is skipped in full sights unless sent as authored, and whenever more than one camera request is active or the sight coupling is non-zero (`native_ads_editor.cpp` branch_rotation_call; `motion_runtime.lua`).
93. Entering ADS keeps the placement already reached and only takes the saved ADS rotation at once. (fp/motion_runtime.lua)
94. Entering and leaving ADS crossfades the added motion over .15 s or the shared transition. (fp/motion_runtime.lua)
95. A sudden large change of the added weapon offset is crossfaded over .12 s. (fp/motion_runtime.lua)
96. The camera gets no added bob, sway or kick from these modules. (fp/menu.lua, fp/motion.lua)

**Movement**

97. Holding S while the mode is on (window focused, cursor hidden, on foot, kind 4 or 6, not seated) makes the character backpedal with the game's focused locomotion instead of turning round, and releasing S restores the previous state (first_person_movement.lua).
98. The S behaviour is off while the placement editor is open and while another camera owns the view (first_person_movement.lua, via runtime.lua:629).
99. If the game already has focused locomotion on, or something else changes the flag while S is held, the mode leaves it alone until S is released (first_person_movement.lua).

**Aiming**

100. When the player aims down sights while the view is held, the same camera request is kept and the game's ADS takes over; the lease is not released (fp/backend.lua:1130-1139, fp/controller.lua:412-445).
101. Entering full native ADS eases the lens from the hip view to the sight's own zoomed lens over 180 ms (or along the shared 0.18 s phase) in focal-length space instead of snapping (native_ads_fov.cpp `handoff_lens`/`shared_lens`; profile.lua `ads_blend_ms=180`).
102. Leaving ADS eases the lens back to the third-person FOV the same way (native_ads_fov.cpp branch at lines 544-554).
103. While full ADS is held the game's own sight zoom/FOV is passed through unchanged once the handoff has finished (native_ads_fov.cpp).
104. With a scoped weapon outside full sights, look sensitivity is made independent of the sight's zoom (yaw and pitch gain divided by the lens/third-person-FOV ratio); in full sights and during native aiming the game's own zoom-scaled sensitivity is kept (`native_ads_editor.cpp` NADSE_Input90Call, `zoom_gain_math.h`, `native_ads_input.S`).
105. The selected zoom level index read by the depth path is reported to the runtime as the weapon's zoom identity (`pose.lua` depth_stats, `runtime.lua`).
106. In full ADS the game's (sight's) near distance is always kept (native_ads_near.cpp:216-217).
107. Releasing aim (right mouse button edge, or middle button press) while in the game's full ADS does not drop the first-person view: the aim controller's active byte is cleared once and the request's sight distance zeroed (fp/backend.lua keep_aim_view, fp/runtime.lua:536-538).
108. When the game's ADS was seen during the held view and the view has to be re-entered on the same weapon, the saved arm placement blends back in over 150 ms instead of snapping (fp/controller.lua:383-386, 463-466; fp/backend.lua:855; fp/profile.lua:46).
109. Returning from full sights re-acquires with the placement fading back in instead of the draw animation (`controller.lua`, `backend.lua`, `native_ads_editor.cpp`).
110. If aim is released and the native view really drops, for about 50 ms (then fading) the camera is placed so the third-person weapon appears where the weapon was in the aimed view, per weapon, once that aimed position was learned (cpp:781-795, 961-993).
111. "Show aim markers" off hides the crosshair and the aiming circle while aiming in first person; they stay visible at the hip. (fp/aim_markers.lua)
112. With "Show aim markers" never changed, markers are hidden while aiming in missions except for weapons with a saved per-weapon preference. (fp/aim_markers.lua)
113. Hidden markers come back when aim is released, the view stops, the weapon changes or the mod stops. (fp/aim_markers.lua)
114. The marker rule also applies to ordinary native ADS with F11 off. (fp/aim_markers.lua)
115. With the option "True First Person: native ADS" on (default off), aiming uses the game's native first-person sights and zoom for the current weapon; the game's aim preference is put back when the mode stops (native_ads_option.lua; src/memory.lua `copy`).

**Gaps: weapon swap, stim, vault, dive, launcher**

116. The session stays on across weapon swaps: the old weapon's view is released and the new weapon is entered on a later frame, at most one entry attempt every 0.25 s (fp/controller.lua:447-456).
117. Any blocking action drops the native view while it lasts: non-zero aim-controller mask (weapon swap 2, vault 0x20, stim 64 and others), the guard byte, a seated interaction, a camera kind other than 4 or 6, not being in the verified on-foot animation lifecycle, or Photo Mode or another mod camera owning the view (fp/backend.lua:215, 279-286; fp/controller.lua:394-396).
118. After an expendable launcher fires (guard byte seen) the view is not re-entered for 0.75 s, so the request cannot outlive the destroyed launcher (fp/controller.lua:15-20, 390-393).
119. If the weapon of the held view is destroyed (fired expendable), the view is exited after 4 consecutive checks instead of showing empty hands, except during a stim (fp/backend.lua:1420-1451).
120. With F11 on and the game showing its normal camera (weapon swap, stim, reload gaps, stratagem ball, vault), the view is placed at the character's head: head node position plus 0.18 m along the horizontal look direction and 0.04 m up, instead of the third-person camera (cpp:830-1013).
121. The head eye follows head animation through a 100 ms low-pass in body coordinates, limited to 0.06 m of lag, so head bob is damped while body movement and turning are followed at once (cpp:871-886).
122. Looking straight up or down does not swing the eye into the helmet: the forward offset uses only the horizontal look direction, falling back to the body's facing (cpp:893-903).
123. When the head camera takes over from the game's own normal camera (not from the native first-person view) the position glides over 120 ms with a smoothstep (cpp:887-892, 904-907).
124. While the game's native first-person view is up (weapon in hands) the camera position and rotation are exactly the game's; nothing is filtered (cpp:765-823).
125. When the native view drops for a weapon swap or stim, the eye stays at the native eye position (fixed offset from the head eye) for up to 2.5 s, and for 300 ms of no-action around the swap, so the view does not jump (cpp:912-955).
126. During a swap to a weapon whose native eye is already known, the held eye glides (70 ms time constant) to where that weapon's native view will be (cpp:933-942).
127. In an empty-handed action, vault or dive after the native view, the held eye offset fades out (80 ms time constant) to the plain head eye (cpp:943-947).
128. If no eye hold can start, the eye moves from the last native eye to the head eye over 150 ms instead of cutting (cpp:957-960; `native/first_person/camera_correction.h`).
129. On the frame(s) where the game has already switched to its normal camera but the camera is still attached to the first-person rig, the last native view position is repeated, avoiding a one-frame view of the character's back or a far-away view (cpp:848-861).
130. A one- or two-frame "guard" state (e.g. firing an expendable launcher) no longer flashes the third-person camera: the head camera is kept for up to 250 ms (cpp:301-306, 827-828).
131. During the head-camera gap view (kind 4 while swapping, vaulting, diving) the head stays hidden only while the head camera is active, armed and error-free and the body is on foot (head_visibility.lua).
132. With the menu option "True First Person: show torso" on, the torso is drawn in the gap view only after the rig-less state (kind 4, controller bytes 1-4 and 6 zero) has lasted 0.6 s, and never during a draw/put-away or in the rig view (head_visibility.lua).
133. Depth of field and vignette do not flash back on between weapons, during a vault or a dive (dof.lua, vignette.lua, gate in runtime.lua:48-79).
134. The FOV policy keeps working across weapon swaps and brief gaps without re-installing, by rebinding to the new camera request and weapon (fov.lua update step 3 and `NADSF_Bind`).
135. Every presentation change (weapon, ADS in or out, zoom step, shoulder aim, action, seat) restarts one shared 0.18 s blend that the arms, the camera and the lens follow; a slow frame can advance it by at most a third, and quick successive stance/zoom edges join the running blend (fp/transition.lua).

**Seated interactions and the ship**

136. When a seated interaction (turning a valve) begins, the camera is unlinked from the first-person rig once so the head camera stays correct (fp/backend.lua detach_seated_camera, fp/runtime.lua:540-542).
137. During a seated interaction (turning a valve) the view stays at the head and turns smoothly (180 ms time constant) toward the direction the game's own camera wants, starting from the view the interaction began with (cpp:833-846, 1001-1011; with `src/first_person/backend.lua:1372-1396` detaching the camera once).
138. During a seated interaction (turning a valve) the head stays hidden only if the head camera is ready and the on-foot test passes (head_visibility.lua).
139. On a ship the head camera is allowed only in the bridge combat state; otherwise, and in any non-on-foot state, the game's camera is left untouched (cpp:357-405, 829).

**Placement editor (F8)**

140. F8 (rebindable) opens the arms-and-weapon placement editor and, when it is open, cancels it and leaves first person on. (fp/menu.lua, fp/placement_editor.lua)
141. The placement editor opens only with first person able to run, a firearm equipped, MCM closed and no other cursor owner, and shows a panel at the top left. (fp/placement_editor.lua)
142. Opening the editor with first person off starts first person. (fp/placement_editor.lua)
143. Editor input is ignored until the opening click or key is released. (fp/placement_editor.lua)
144. Left drag moves the rig right/left and up/down; the wheel, PgUp and PgDn move it forward/back; arrow keys move it right/left and up/down. (fp/placement_editor.lua)
145. Right drag changes yaw and pitch; middle drag changes roll. (fp/placement_editor.lua)
146. Holding Shift makes every adjustment ten times finer. (fp/placement_editor.lua)
147. Position is limited to 50 cm per axis and rotation to 90 degrees per axis. (fp/placement_editor.lua)
148. The '<' and '>' buttons cycle the action being edited and discard unsaved edits of the previous action. (fp/placement_editor.lua)
149. "Save weapon" or Enter saves the selected action for the current weapon; "Save all" saves it for all weapons. (fp/placement_editor.lua, fp/runtime.lua)
150. "Cancel", Esc or F8 restores the placement from when the editor opened. (fp/placement_editor.lua)
151. "Reset" previews the default (or weapon-family) placement without saving; "Reset all" clears all saved placements at once. (fp/placement_editor.lua, fp/runtime.lua)
152. The panel shows the weapon key, the action, the offsets in centimetres, the angles in degrees, control hints and a status line. (fp/placement_editor.lua, fp/ui_style.lua)
153. Changing weapon, opening MCM, losing game focus, the view stopping or another frontend taking the cursor cancels the editor. (fp/placement_editor.lua)
154. While the editor is open, bob and sway are frozen and the rig shows the draft. (fp/motion_runtime.lua)
155. Dragging in the placement editor moves the weapon live through the same offset; rotation drags show through the motion rotation while other motion is frozen (`placement_editor.lua`, `motion_runtime.lua`, `backend.lua`, `native_ads_editor.cpp`).
156. "Use weapon-family placement defaults" gives catalogue firearms a family starting placement when no saved or customised placement exists. (fp/menu.lua, fp/action_placement.lua)

**Other cameras, options and test buttons**

157. While Photo Mode holds the camera the mode stops its presentation and does not write depth of field or vignette; both are restored once Photo Mode lets go (camera_compat.lua, dof.lua, vignette.lua).
158. An ActionCam installation without the camera-sharing bridge, or a malformed Photo Mode interface, is treated as "another camera owns the view" and keeps the mode's presentation off (camera_compat.lua).
159. With the legacy Photo/ActionCam interface, ActionCam input is blocked while the mode is enabled and Photo Mode is not holding the camera, and the previous reader is put back at stop (camera_compat.lua).
160. Photo Mode or another foreign camera, turning F11 off, and the 30-second "Compare native ADS camera" mode all remove the head camera (`src/first_person/runtime.lua:618-622,881`; `src/first_person/camera.lua:430`).
161. The menu button "Free aim (30 seconds)" does nothing visible: the DLL refuses activation (cpp:1163; `src/first_person/runtime.lua:926-934`).
162. "Free aim (30 seconds)" and "Compare native ADS camera (30 seconds)" start their timed modes. (fp/menu.lua)
163. "True First Person: native ADS", "True First Person: show torso" and "Fird Person Arms" switch the corresponding behaviours implemented elsewhere. (fp/menu.lua)

**Faults, change of body, leaving**

164. A failing camera or FOV policy suspends the first-person view (the native view is released, the session stays on) and retries after 0.25, 0.5 and 1 s when the failure was a temporary read; a fourth failure or any other fault leaves it suspended or switches the mode off (fp/recovery.lua, fp/runtime.lua:278-311, 608-609).
165. If the player, weapon, rig graph or camera request changes, the offsets stop at once, the last native pose is put back when still owned, and the mode re-acquires on its own when the normal camera is back (`native_ads_editor.cpp`, `backend.lua`, `controller.lua`).
166. If the player, body, camera or weapon rig identity changes (death, respawn, mission change), the helper disarms itself and Lua re-arms it when a normal on-foot camera is back (cpp:777,824-825; `src/first_person/camera.lua:439-450`, 431-437).
167. A Lua error inside the mode's update switches the whole mode off until the mod is re-enabled, logging "First person stopped: ..." (src/first_person.lua:136-141).
168. On leaving, the kit never exits a request that the player's own aiming has taken over, and never exits a request that is no longer exactly its own (fp/backend.lua:1408-1414, 1456-1459).
169. Turning the mode off (F11 again, or mod disable) disarms the pose helper first, then calls the game's own exit for the owned request; if that cannot be confirmed it keeps retrying every frame ("Cleanup pending: ...") (fp/controller.lua stop/retry_cleanup, fp/backend.lua release).
170. Switching the mode off puts the game's own node 5 position, node 5 rotation and root position back and restores the engine function pointers (`native_ads_editor.cpp` NADSE_End; `pose.lua` finish).
171. Turning the mode off, a foreign camera (Photo Mode) taking the view, death/respawn or a player change releases the FOV hook and the game's own lens returns on its next camera update (fov.lua `stop`; runtime.lua:619-622).
172. Leaving the mode puts the game's near distance back at once if the mod's value is still on the camera (native_ads_near.cpp `restore_distance`).

## 8. Surprises and open points

Collected per module group. Items that affect the rewrite most are repeated first.

- The version strings disagree: `manifest.json:5` and `PORT-STATUS.txt:1` say `0.5.42-shear.1-cffi-26568081`; `fp/runtime.lua:1097` reports `seamless_version='0.5.41-smooth.66-cffi-26568081'`. HEAD is `1d0d453` ("True First Person 0.5.41-smooth.66").
- The alignment helper, the held-fire helper and the zoom helper are not on the shipped path; the first-person backend does not use the kit's guarded-reads helper either.
- Several behaviours are live with no Lua switch and exist only as C defaults in the editor/pose DLL: the 300 ms entry rotation guard, the remembered entry position, the 220 ms draw animation, zoom depth compensation. Look-input normalisation is on (`profile.pose.normalize_zoom_input=true`) although the docs in the tree still call it disabled or experimental.
- The F11 lease has no timer (`duration_ms` is sent as 0); `profile.pose.duration_ms=30500` is only a bound.
- `keep_aim_view` depends on the right and middle mouse buttons, not on the game's key binding; with aim on another button or a controller it never runs.
- Entry needs all 14 byte pins readable, including four packed "cold" pages; the code that warms them is Fird-only. Whether a refusal happens in practice is not stated in the source.
- Arm pieces and grip hand only find their units through `head_visibility:sdk_handles()`; if head visibility is unavailable they silently do nothing.
- Hotkeys, the editor's cursor, input and panel, and most menu values depend on DBF-MCM (`DBFMCM` global), not on the engine Gui. The S key and the mouse buttons are read by literal name.
- "Free aim (30 seconds)" is a visible button that does nothing (the DLL refuses activation); "Compare native ADS camera (30 seconds)" is a test mode still on the shipped page.
- The hook thread and the order of the camera evaluator relative to animation are not established by the source.
- The fixture suite is not portable as it stands (lupa on Lua 5.1 with a real cffi build, files outside the kit folder, constants rewritten at load time), 22 of 55 fixtures test only parked code, and the real update order in `runtime.lua` plus the sleeve and grip wiring have no test.

### 8.1 Camera and alignment helpers

- The alignment DLL and `src/first_person/alignment.lua` are dead on every reachable path: only controller mode `unlinked_sights` uses them (`controller.lua:297`), and nothing (F11, menu, debug global) starts that mode. The C port can drop the helper; only the pure-Lua registry lookup that shares its profile block (`Profile.alignment.executable.unit_registry=0x1ab76f8`, `backend.lua:783-803`) is live. `profile.alignment.position_return/rotation_return` are read by nobody.
- "Free aim (30 seconds)" menu button is a dead feature wired to live UI: it calls `NADSC_ObserveParent(3000)` (a diagnostic) and then `NADSC_FreeAim(30000)`, which the DLL always rejects with INVALID_CONFIG (cpp:1160-1163). All free-aim state in the hook is only ever reset.
- Retired-but-exported functions: `NADSC_FreeAim` (nonzero), `NADSC_SetKick46` (nonzero), `NADSC_SetCameraMotion47` (nonzero) always fail; `NADSC_FreeAimStats`, `NADSC_KickStats46` return counters nothing increments. `Policy` fields `free_*`, `kick_*`, `motion`, `fire`, `shoulder`, `side` (0 on shipped path) are vestigial.
- Dead inside the live DLL with shipped values: `ease_native_entry`, `pending_camera_parent`, `CameraHandoff::Position` (entry_ms=0); `track_native_rotation`, the body of `ease_normal_rotation`, `CameraHandoff::Rotation`/`Proof` (rotation_ms=0). Profile comment says neither worked live on 26568081 (`profile.lua:74`, `SOURCE.md:74-90`). `observe_camera_parent` runs only inside a debug/menu window.
- The 150 ms position correction (the documented "exit handoff") is mostly pre-empted: with eye hold on, the first normal frame after native starts the eye hold whenever a fresh native eye exists, and the hold resets the correction each frame (cpp:953). The correction only runs when the hold cannot start.
- The aim-release anchor (bit 4) is live but largely pre-empted by Lua's `keep_aim_view` (clears `controller+4` on aim release so the native request stays; `backend.lua:1340-1367`); it only matters when the native view still drops. It is nevertheless the reason native sampling (`handoff` flag, cpp:770) runs on every native frame.
- The hook writes into the game's caller stack: in seated view it overwrites the 16-byte quaternion at `native+64` through `const_cast` (cpp:1010), relying on the witnessed call pair at game+0x110bfe7 (checked only once, in `NADSC_SetPresentation47`).
- Exit hold forwards a modified position but reports error 15 (SPACE_INVALID) and does not count as `replaced`; Lua treats 14/15/16 as normal (`camera.lua:457-459`).
- `camera.lua` declares `NADSC_ParentStats45` twice in separate pcall'd cdef blocks (20-22, 28-30); the second block ends in a redefinition error that pcall hides. Harmless (the free-aim lines precede the duplicate), but it is leftover layering from older helper versions.
- `NADSC_EntryTrace50` and `NADSC_AnchorStats51` are exported and maintained per frame (entry trace rows, cpp:800-811) but have no Lua declaration or caller in these files.
- Lua fields the runtime tests but `camera.lua` never defines: `camera.retired`, `camera.is_pinned`, `camera.look_stats` (leftovers of the removed pin path); `Camera:set_motion` has no caller; `Camera:set_offsets` / `NADSC_SetOffsets` are reachable only from the Fird head-camera editor (dead on shipped path).
- File-name suffixes of the installed DLLs equal the sha256 prefix of the DLL binary, while `tools/build_first_person.py:9` would stamp them with a hash of all sources; `build_camera.py` / `build.py` (MSVC, names `_0_1_48` / `_0_1_5`) and `README.md` describe an older flow. `build_camera.py:32` metadata still describes "opt-in 30-second free aim" and hashes `camera_transition.h`, which the DLL no longer includes. A second 26568081 camera DLL (`native_ads_camera_26568081_46e3ba382419.dll`) sits next to the live one, unused by the profile.
- `docs/first-person-camera-remake.md` says the fallback "does not … rotate the view" and "does not write the caller's quaternion scratch"; the seated-head branch added later does both (code wins).
- Depends on code outside these files: `backend:camera_config()` (its own on-foot/action check, Photo Mode check), `backend:snapshot().weapon_unit`, `backend.camera_before_entry` call inside `enter_pose`, `backend:keep_aim_view`, `backend:detach_seated_camera`, the recovery ledger in `managed_update`, and `Host.scope.native_ads_camera_libraries` to keep the DLL referenced.
- The thread on which the hook runs and its order relative to the Lua update are not stated anywhere in these sources; the SRW locks and the QPC clock make it independent of both, and the C port must not assume same-thread.
- `manifest.json` in R reports version `0.5.42-shear.1-cffi-26568081`, while `runtime.lua:1097` reports `0.5.41-smooth.66-cffi-26568081`.
- Live acceptance of individual branches is only recorded in comments ("live" notes in cpp:59-75, 296-306, 833-839); the fixtures are synthetic (Wine) and the build metadata says "not installed or live-validated".

### 8.2 Editor/pose helper

- The shipped lease has no deadline: F11 passes `remaining_ms = 0`, so `duration_ms=30500` in the profile is only a bound, and nothing renews or expires on a timer; all timed-expiry code (`policy.deadline`, `expired`, `DEADLINE_EXPIRED`, ReadStats expiry) is reached only by the timed test modes.
- Three features that look experimental are on by default with no Lua switch, declaration or caller: the entry rotation guard (`...49`), the remembered entry position (`...50`) and the 220 ms draw animation (`...50`); their five exports are unreachable from Lua, so the C defaults (`editor.cpp:1018-1020,1144,1180`) are the behaviour to copy.
- Input normalisation is still labelled experimental in the tree: `build_editor.py:69` ("Experimental bounded arithmetic; native source ordering and live behavior unqualified"), `docs/first-person-zoom-input.md` ("No live hook test has occurred", "profile opt-in is not enabled") and `manifest.json:6` ("stays disabled by default"), while `profile.lua:41-44` has it on since 0.5.41-smooth.28 with a live note about full ADS. Docs are stale; code is on.
- Input normalisation writes into the game's camera request (`request+0x80`) and executes the game's `exp` at `game+0x1f8ff70` from inside the hook; it also allocates up to 32 blocks of 0x10000 bytes near the preset table that are never freed.
- `NADSE_ValidateInput` is only an alias of `NADSE_Validate`; a failing input slot or failing input pins never refuse the lease, they silently leave look input unnormalised (visible only through the debug `input_stats`).
- `NADSE_SetZoomPolicy` / `pose.lua:set_zoom_policy` / `backend:set_pose_zoom_policy` have no live caller (`zoom.lua` is required by nothing), so `zoom_policy_active` is always false and the "legacy" depth inversion is the live path; depth reason 10 never occurs.
- `NADSE_SetMotion`, `NADSE_Validate`, `NADSE_Begin`, `NADSE_BeginSteady` are never called with the shipped profile; `motion_runtime.lua:capture_neutral` has no caller.
- `shoulder_aiming` is computed on every ownership check but no branch in `editor.cpp` reads it; hip and shoulder aim are treated the same by the DLL (weight 1).
- `zoom_input_math.h:prepare` (the older raw-input rewrite) and `gain.h:pitch_after_base` / `PITCH_ADD_BOUND` are dead code in the DLL.
- Stale addresses and numbers in comments and docs from older builds: `editor.cpp:877` ("1287F41"), `build_editor.py:49,51,58` ("game1287A41", "game+0x128923B", "12887C9", rotation bound 35 degrees), `editor.h:60` (35 degrees; code allows 120), `POSE-README.md:7` (`0x1288424`), `docs/first-person-zoom-input.md` (`0x11184DE`, `0x1119924`). The code constants listed in DATA are the live ones.
- Build provenance gap: `build_editor.py` is an MSVC script with a fixed output name and fixtures tied to build 26251122 captures; `tools/build_first_person.py` compiles `native_ads_editor.cpp` alone (no `native_ads_input.S`) and names by a source-tree hash; the shipped file is named by its own SHA-256 prefix and imports the UCRT like the zig build. The command that actually produced `4940b83e17ad` is not recorded in the tree (only the earlier candidate's command is, in `docs/first-person-zoom-input-validation.json:27`). I did not build anything to confirm.
- `PORT-PROVENANCE.json` states `"live_activation_verified": false` and `"visual_acceptance_pending": true` for the helper set; `tools/build_first_person.py:22` writes `in_game_tested: False`.
- `pose.lua:151-152` says depth statistics are console diagnostics that the update loop never calls, but `runtime.lua:378` calls `depth_stats` in the runtime's presentation observation (a second locked DLL call beside `ReadStats`).
- `pose.lua:3` says "Camera roots forward"; the rig root node 0 position is in fact modified by the depth counter-shift.
- Dependencies outside the mode: `mod.scope.native_ads_pose_libraries` keeps every loaded library object; the helper pins itself, the exe and `game.dll` in the process for its lifetime; correctness depends on no other mod replacing the same five Unit API slots (a foreign pointer makes Begin fail with code 11 or makes every hook disarm with code 6).
- The helper's guards read process memory with `ReadProcessMemory` on every acted-on call (ownership + two 252-byte graph arrays + dispatch), inside the game's camera evaluation; this is per frame, not event driven.
- The settled-rotation reference and the entry-position cache are process-global and survive leases and mode toggles; a C port must keep them outside the per-lease state.
- The entry position is remembered only on a call where `weight==1` and not full ADS, and it stores the already shifted pose (including depth term, before the draw-animation offset).
- Not established by these sources: which game thread runs the hooks, and where the camera evaluator sits relative to animation evaluation and rendering, beyond the two comments quoted in the FRAME block.

### 8.3 FOV, blur, near, held-fire, zoom, memory

- The guarded-reads DLL is not part of True First Person at all: the first-person backend has its own `ReadProcessMemory` reader (backend.lua:65-83), and the only first-person use of `src/memory.lua` is one `copy()` in the native-ADS option, which uses kernel32 directly. A C port of the mode does not need `GGR_*`.
- Confirmed parked: `held_fire.lua` is required only in `ensure_ready_pose()` (runtime.lua:128), called only from `start_fird` (runtime.lua:943) because `native_animation_default=true` (fird_person.lua:9); `start_fird` is exposed only on the debug global (runtime.lua:1104-1110), the menu entry is commented out (runtime.lua:1056). The held-fire DLL for 26568081 ships anyway.
- Confirmed dead: `zoom.lua` is required by nothing, is missing from manifest.json, has no `profile.zoom` (would raise on `self.profile.library`), and only a 26251122 zoom DLL exists even though `native_ads_zoom.cpp` already carries 26568081 pins. It is also the only helper that patches code (MinHook inline hook) rather than a data slot.
- The FOV DLL carries a complete second policy (`NADSF_ValidateFird`, `NADSF_BeginFird`, `NADSF_BindFird`, request-field CAS at request+0x3C, six extra code pins) that only the parked Fird Person path (`fird_fov.lua`) uses; `fov.lua`'s cdef does not even declare those exports.
- `NADSN_ReadSites` and the 16-row caller table are diagnostics compiled into the live near DLL and recorded on every setter call, but never read from Lua. `NADSHF_LastError` is exported and declared but never called.
- The near-clip hook accepts ANY caller inside game.dll text that passes exactly 0.05 or a value in (0.05, 1.0] on the owned camera, not one pinned site; the pinned site (game+0x110ddec) is only a build check. The profile comment names 0x110DE0B, the source comment four sites.
- `pull_sight_distance` exists only in Lua (keeps the helper armed at zero distances); the DLL has no flag for it and always pulls sight distances to 0.05 outside full ADS when armed.
- Near clip is the only one of the three live helpers not run through the recovery wrapper (`attempt(near,'update')`, runtime.lua:633) and has no preflight; `NADSN_Begin` accepts a kind-4 camera while Lua's `fov_config(lease)` only supplies kind 6.
- Blur and FOV treat weapon gaps differently: FOV keeps its generation while the lease is absent, blur ends it and returns to `waiting` (blur.lua:227-230 vs fov.lua:239-255).
- In the ADS-exit branch the FOV target on a still-current kind-6 request is the raw third-person FOV without the 5 % shoulder magnification (native_ads_fov.cpp:550-553); shoulder magnification only re-enters through the normal branch afterwards.
- With a live shared phase the shoulder tween is bypassed entirely (weight jumps to its target), so normal-aim easing depends on the Lua transition coordinator (`transition.lua`, duration .18) being fed every update; if it stalls for more than 0.25 s the DLL drops the phase and falls back to its own 180 ms / 120 ms timers.
- `profile.fov.executable` (profile.lua:59) is not read by fov.lua; the exe base comes from the pose lease's alignment context or `Profile.pose` (backend.lua:408, 412).
- Build provenance is uneven: fov/blur shipped names follow `tools/build_first_person.py` (zig, stamp = hash of the whole `native/first_person` source folder, so the stamps `764664986628` and `c18c221e2cc6` come from different folder states), while `build_fov.py` / `build_blur.py` are stale MSVC builders with other output names and metadata saying "not installed or live-validated". Every build script records `in_game_tested: False`.
- The guarded-reads name is a hash of the DLL itself, the `native_ads_*` names are hashes of sources: two different conventions. Five unused guarded-reads variants (one duplicated as `guarded_reads.dll`) sit in `native/`.
- `compat51.lua` shows the kit running on a `cffi` module (`require('cffi')`), not LuaJIT's built-in `ffi`; `src/memory.lua` requires `cffi` directly and bypasses the wrapper. manifest.json:5 in this worktree reads `0.5.42-shear.1-cffi-26568081`, not `0.5.41-smooth.66`.
- The native-ADS option writes game heap memory from Lua (`WriteProcessMemory` of one byte) through a private `Memory.new()` instance whose game-image guard is inactive (`_game` nil); protection relies on the private-RW region test only. If the byte is already 1 when the option is switched on, no lease is taken and nothing is restored later.
- `Adapter:now` in blur.lua (kernel32 `GetTickCount64`) is unreachable on the shipped path because the runtime always passes a clock.
- Thread of the hooks is never stated in code; only that the hooks can run concurrently with the Lua update (locks, lock-free diagnostics). Treat "which thread / where in the frame" as unverified for all three live hooks.
- Tests: native fixtures exist for fov, blur, near, held-fire and zoom (synthetic memory, run under wine by the build scripts) and `tests/memory.lua` for the memory module; no Lua test file for fov.lua, blur.lua, near_clip.lua, held_fire.lua, zoom.lua, native_ads_option.lua or compat51.lua was found under `tests/` by name.

### 8.4 Backend, controller, kit modules

- The mode's backend does not use the kit's memory layer at all: `Backend.new(log)` (fp/runtime.lua:665) has no io, so every read is a plain kernel32 `ReadProcessMemory` on the own process through cffi, with an 8-byte-to-1024-byte copy per call and no caching of ownership reads. `snapshot()` is called many times per frame (controller, recovery proof, presentation, each setter, each policy, diagnostics), and each one also runs the animation state reader.
- The two writes are raw pointer stores without any page or protection check: `controller+4 = 0` and `record+0x98 = 0` (fp/backend.lua:1350, 1360). They depend on a hard-wired right/middle MOUSE button edge (fp/runtime.lua:395-422), not on the game's key binding: with aim on another button or a controller the keeper never runs and the game's ordinary two-frame exit happens.
- `pin_player_snapshot` is still live although the pinned paths are declared off: fp/head_visibility.lua:58-61 prefers it for every view snapshot, and fp/runtime.lua:367 falls back to it for the presentation clock. It skips the "camera request is local" test.
- Shipped entry needs scope `full`: all 14 byte pins must be READABLE, including the four packed "cold" pages (fp/backend.lua:1038, 441). The code that warms those pages (`warm_start`, scope `entry`) is fird-only. As written, F11 before those pages have run is refused with 'Waiting for native ADS camera initialization (NAME)'; whether that happens in practice on 26568081 (for example before the first real ADS of a session) is not something the source tells.
- A failed `prepare_pose` at F11 time refuses the start (returns false), while the same failure after the session is on only waits. Two different user outcomes for the same cause (fp/controller.lua:305-311 vs 457-461).
- `weapon_ready == false` is tested in three places (fp/controller.lua:243, 453; fp/runtime.lua:449) but the backend only ever sets `weapon_ready = true` or leaves it nil: those tests are dead.
- `backend.alignment_lease` is never cleared; a finished lease stays referenced and is distinguished only by `.done`.
- `snapshot().reason` is filled only for "not on foot" and "foreign camera". When the view is blocked by the controller mask, the guard byte, seated or the camera kind, the wait text is the generic 'Gameplay action or camera owns the view'.
- The presentation clock's `action` edge never comes from `transition:update` (the snapshot has no `action` field); it only arrives through `backend.presentation_action` from fp/motion_runtime.lua. `backend.presentation_coordinator` and `backend.camera_update_token` are set and read by nothing.
- Never reached on the shipped path: controller modes `release_body_event` and `unlinked_sights` (nothing starts them), the whole timed `pose_offset` branch incl. phase `'reacquiring'` (fp/controller.lua:500-564), `wait_for_pin`, backend `prepare_alignment`, `alignment_status`, `_alignment_helper`, `prepare_body_pose`, `release_body_pose`, `_body_api`, `_invoke_body`, `set_pose_zoom_policy` (fp/zoom.lua is required by nothing), `_invoke_presentation` (fp/fird_presentation.lua is required by nothing), `warm_start`, `warm_finish`, `camera_ready`, `fird_fov_config`, and fp/alignment.lua with its DLL. Mode `native` is reachable only from the menu button "Compare native ADS camera (30 seconds)".
- Experimental-looking live code: the module still calls itself "Native ADS Camera Test" (log prefix, global `NativeADSCameraTest`, error texts like 'Equip a firearm before starting the camera test'); the on-foot test depends on labelled animation state numbers (layer 14 state 36 or 3, scene id 3/4) whose comments call them observations or hypotheses; `sprint_evidence` strings say "current action unverified".
- Per-frame diagnostics are always on and do extra memory reads every frame (camera matrix, rig node 5, weapon root; plus a second full snapshot whenever the session is on but not running), although nothing but the debug global reads the ring.
- Dependencies outside the mode: global `DBFMCM` (menu framework: `register`, `mods`, `shared_input_service`, `camera_input`, `menu_binding_status`, `input_status`, `is_open`) for the menu AND for the F11 / F8 hotkeys (key codes 122 / 119 polled through `camera_input.down`); global `LiveLuaLoader.input_status`; loader fields `mod.require`, `mod.dir`, `mod.scope`, `mod.log`, `mod.config`, `mod.set`; `src/loadout_catalogue.lua`.gear (weapon names, kinds and type ids) for weapon families and names; `fp/camera_compat.lua` gets `_G` to watch other camera mods (Photo Mode); `stingray.Application.time_since_launch` and `stingray.Mouse`; the cffi module `require('cffi')` (Lua 5.1 build, not LuaJIT's ffi); the kit's config table `first_person`.
- The kit modules `src/backend.lua`, `src/core.lua`, `src/controller.lua` are not used by the mode at all; the kit backend's session rule "The local player must host this session" does not gate first person.
- Version labels disagree: `manifest.json` says `0.5.42-shear.1-cffi-26568081`, the mode's own `seamless_version` says `0.5.41-smooth.66-cffi-26568081` (fp/runtime.lua:1097). The folder is being worked on by another session, so line numbers here are those of 2026-11-15.
- Untested from this reading: nothing was run. The `io` fixture hooks (`io.read`, `io.module`, `io.invoke`, `io.write_byte`, `io.write_bytes`, `io.detach_camera`, `io.executable`, `io.function_address`) show that an offline harness exists for the backend, but the Wine-specific page-type exception (MEM_MAPPED accepted when `wine_get_version` exists) and the orphan-exit and keep-aim paths are justified in comments by single live observations ("live 2026-11-13", "three of three releases").

### 8.5 Head visibility, movement, DoF, vignette, camera sharing, arm modules

- head_visibility's `view_snapshot` tries `backend:pin_player_snapshot()` whenever `deps.pinned_ready` is a function (hv:58-61); it is not gated on `backend.pin_first_person` as runtime.lua:31-32 and first_person_movement.lua:113 are, so the "pin" snapshot variant (`_snapshot(true,nil,true)`, be:338-342) is the one actually used by this module even though pinned mode is off. What that variant returns differently was not traced here (`_snapshot` body be:167-290 not read in full).
- Slot meaning is only what the code names it: slots 8/9 are called `shoulder` and hidden as body-parented units (hv:5-6, 308-310), while model_shadow_source looks for rig-parented arm pieces in slots 6..9 (ms:44). If a slot 8/9 unit were on the rig in kind 6 the assert "First-person shadow target overlaps hidden clothing" (hv:463) or "Head customization attachment changed" (hv:308-311) would put the whole module in `unavailable`. Which slots the four arm pieces really occupy is not stated in these files; slot 1 is never read.
- Two modules write mesh visibility on the same rig unit: head_visibility owns pass bits on rig meshes (pads hidden, others shadow-off) and proves them by reading the mask byte, while grip_hand calls `Unit.set_mesh_visibility(rig, index, false/true)` with no pass argument on the two-material mesh (gh:475-494). A mask change by grip_hand breaks head_visibility's steady proof (hv:644) and goes through its foreign-mask arbitration; no test covers the interplay.
- grip_hand's rig-screen rule is "visible and exactly 2 materials" (gh:488), established on one rifle rig (4124 vertices, header gh:84-89); other rigs/armours are unmeasured.
- grip_hand reports `arm is straight` also when the arm's hand bone is simply not yet among the pieces (pose_arm returns false at gh:427, mapped at gh:556 and 582).
- Dead leftovers in live grip_hand: `calm` is the constant 1 (gh:655) so `weapon is being handled` is unreachable; `ARM_PARENT=7`, `held_at/held_time/busy_until/steady`, result fields `handled/turning/sliding/steady` never change.
- grip_hand thresholds are tuned by feel, per its own comments and the memory notes: spin limits 400 deg/s and 1.5 m/s ("lower limits made the hands shift all the time"), RIG_* 45 deg/s / 0.15 m/s, AWAY/STIR/SETTLE, ASIDE 0.2 s, STRETCH 0.95 (the "two arm screens" report was still open after it), FULL/NONE 20/32 cm. smooth.64-66 behaviours (swap lookups, quick lookup, remembered grips) are marked "user check pending" in the notes; the SWAP edge was recorded as never firing in practice (`swaps=0`) because the arm view drops for about 0.7 s during a swap.
- `left_aside` only triggers for the left reason `third-person hand is not on a grip`, while `one_handed` in right_arm also accepts `no grip bone` (gh:568 vs 598): a weapon with no grip candidate gets the spin without the left hand moving aside.
- grip_hand and arm_pieces depend entirely on head_visibility having resolved handles: the arm pieces and weapon units are only in `sdk_handles()` because model_shadow_source put them in the hidden-shadow set in kind 6. If head_visibility is `unavailable` (any assertion) or model shadows fail, the arm modules silently find nothing (`no pieces on the rig`, `no grip bone`). `sdk_handles()` is also not cleared by `head:stop()`, so stale tables can be handed on; consumers guard with `Unit.alive` and `num_scene_graph_items`.
- Unsafe-engine-call surface: arm_pieces and grip_hand call `Unit.world_pose/world_position/world_rotation/set_local_pose` with bone indices read from process memory; the guard is `Unit.alive(handle)` plus `Unit.num_scene_graph_items(handle) == count` checked at lookup and for pieces each update (ap:49-51, 219-221, 235; gh:197-199, 627-631), but body bone indices (`bone.source`, `bone.finger`, `body_hand`, `body_right`, `body_attach`, `body_ref`) are used per frame with only the body's node count re-checked in some paths (gh:499, 560), and grip candidates' `world_position` is called after the `lost` check of the same update only. `World.link_unit` re-parents game-owned bones (ap:104, 166).
- arm_pieces picks the LAST side whose collar a piece carries (ap:128-130); a piece carrying both collars would be treated as right.
- arm_pieces `release()` on a plain view end only resets local poses; the game's links are restored only in `stop()` (ap:82-108), relying on the comment that the game re-links the bones itself when it moves the pieces back to the body.
- first_person_movement writes game memory directly with `WriteProcessMemory` (mv:97-104) and resolves the S key by the literal name `'s'` (mv:164): a rebound "move backward" key is not followed. The pool address `game+0x2ce1eb8` is 0x50 below the weapon-heat manager address recorded elsewhere, i.e. the same family of component pools.
- dof.lua restores with the literal `'true'` rather than a stored value (dof.lua:88, 130); correct only because a record is created solely when the value read true.
- vignette.lua identifies the component by a bare instance-id constant `0x35b0f6c2` plus the mapping string; it re-enumerates all world entities every second for as long as the view lasts (96 per update), even after rows are found.
- camera_compat reaches into another mod's closure with `debug.getupvalue` looking for an upvalue literally named `input_reader` (cc:17-31); it depends on Photo Mode's globals `DBF_PHOTO_CAMERA_OWNERSHIP` / `DBF_PHOTO_ACTIONCAM_COMPAT` and on `package.loaded['mods/actioncam/actioncam']`.
- Other outside dependencies: `package.loaded.hd2_nativeinfo.userdata_address` (LLL native bridge, hv:32-36), `PORT51.ffi` / kernel32, `Host.require` for `action_state.lua` (be:344-349), menu toggles `show_torso` and `fird_person_arms`, `profile.lua` flags.
- Tests: there is no `tests/first_person/model_shadow_source.lua`; `tests/first_person/arm_pieces.lua` (115 lines) has no reference to the link restore, the quick/confirm cadence or the tolerance re-link; `tests/first_person/grip_hand.lua` (357 lines) has no reference to `rig_moving`/RIG_* and one each to the aside, swap and recall paths; `tests/first_person/head_visibility.lua` never mentions `foreign_masks`; `tests/first_person/vignette.lua` is 63 lines.

### 8.6 Motion, menu, editor, aim markers, input

- `ui_style.lua` is mostly unused: the editor takes only `editor_legacy_tokens`; the HellForge token block and every `Gui.*` helper in the file are never called, and the editor draws through DBF-MCM's photo view with command tables, not the engine Gui.
- The editor depends on DBF-MCM beyond a menu: `close`, `is_open`, `input_status`, `menu_binding_status`, `photo_resolution`, `create_photo_view`, and a `camera_input` with `api==2` and `isolated_look==true` (fp/placement_editor.lua:82-92). A C port needs its own cursor capture, input sampling and panel drawing.
- Opening the editor with first person off calls `controller:start(0,'pose_offset')` directly (fp/placement_editor.lua:255-258), bypassing the guards in the runtime's `start_pose`; if no weapon target is ready on that same update it closes with 'Equip a firearm and wait for first person before editing' and leaves first person on.
- Two test controls are still on the shipped page: "Compare native ADS camera (30 seconds)" and "Free aim (30 seconds)".
- "Fird Person Arms" is a live setting on a page whose Fird entries are otherwise commented out.
- Dead inside live files: `motion.lua` camera outputs and shoulder offset; `motion_runtime.lua` branch 297-308 and `capture_neutral`; `aim_markers.lua` `pinned_view`, Fird branches, `toggle` and `target`; `menu.lua` `ads_mode`; the whole camera editor mode and `head_placement.lua`.
- Config keys with no writer or no reader: `camera_kick_enabled` (never read in `src/`), `motion_enabled` and `motion_scale` (fallbacks only), `action_state_mapping` (read, never written), `weapon_placements` and `placement` (legacy; written only by 'Reset all').
- The per-weapon `aim_markers` table cannot be changed from the menu any more; it is only read while `show_aim_markers` is unset. The default is therefore "hidden while aiming, in missions" until the user touches the checkbox, although the checkbox itself displays as on at the hip (fp/aim_markers.lua:149-160).
- With the global setting explicitly off, markers are also suppressed outside missions (ship HUD), because the mission-only probe applies only to the legacy path (fp/aim_markers.lua:210).
- Aim markers call a game function directly through FFI at game+0x12e4e20 after a `VirtualQuery` check: no helper DLL, so the C port must reproduce the fingerprints and the call.
- Most menu values live in DBF-MCM's own preference store, not under `first_person`; only placements, `show_aim_markers`, `family_placement_defaults`, `near_clip_profiles` (and the unreachable `fird_head_camera`, `aim_markers`) go through `mod.set`.
- The menu framework draws copies of controls, so `first_person.lua` `M.sync` copies labels and disabled flags every update; the menu's own spec name and description ('Native ADS Camera Test') are never shown.
- The facade bumps a revision on register, which forces the kit to unregister and re-register its whole MCM entry (src/mcm.lua:81-88), and that path first requires the kit's unlock controller to be clean.
- No chat-open check exists in these files for F11/F8; blocking relies on DBF-MCM's binding focus and the input leases.
- Loadout catalogue dependency: weapon names, families and family baselines come from `src/loadout_catalogue.lua` `gear` (fp/runtime.lua:650, fp/action_placement.lua:13-41); the Epic LUT frontend and the `LiveLuaLoader` global are probed as foreign input owners.
- `manifest.json` reports version 0.5.42-shear.1-cffi-26568081 while the runtime's `seamless_version` is 0.5.41-smooth.66-cffi-26568081 (fp/runtime.lua:1097).
- Tests: `tests/first_person/` has menu, editor, motion, motion_runtime, locomotion, aim_markers, aim_marker_source and head_placement fixtures. I found no test file there for `ui_style.lua`'s drawing helpers; `shared_input.lua` / `input_adapter.lua` are tested only at kit level in `tests/first_person_input.lua`, and the facade / `M.sync` in `tests/first_person_integration.lua`. I did not check line by line which behaviours each fixture asserts, so coverage of the editor-starts-first-person path, the pop guard thresholds and the non-mission marker suppression is unconfirmed.

### 8.7 Tests and parked modules

- **A large share of the suite tests parked code.** 22 of 55 fixtures exercise only parked modules: the 16 `fird_*.lua` fixtures (incl. `fird_capture.lua`), `ready_pose.lua`, `ready_pose_backend.lua`, `skinned_placement.lua`, `support_hand.lua`, `held_fire.lua`, `zoom.lua`. That is 4,312 of 16,386 fixture lines. Further parked coverage sits inside live fixtures: the last 6 tests of `controller.lua`, the last 8 of `first_person_movement.lua` (`update_fird`), 3 of 5 groups in `runtime_frame.lua` (ready pose, held fire), one case in `aim_marker_source.lua`, and Fird cases in `backend.lua`.
- **Tests for modules nothing loads:** `tests/first_person/fird_handoff.lua`, `fird_presentation.lua` and `zoom.lua` test files that no source file requires.
- **Manifest vs live list.** Every live module is in `manifest.json` `lll.modules` (no live module is missing). The other direction: 22 parked files are listed and therefore shipped: fird_auto, fird_warm, fird_capture_ready, fird_aim, fird_native_ads, fird_source_pose, fird_retarget, fird_camera_pose, fird_render, fird_weapon_children, fird_person, fird_projection, fird_handoff, fird_fov, held_fire, ready_pose, ready_pose_contract, ready_pose_resources, ready_pose_source, ready_pose_timing, skinned_placement, support_hand. `fird_handoff.lua` is listed although required nowhere. Only two files on disk are not listed: `fird_presentation.lua` and `zoom.lua`. `alignment.lua` is listed and is reachable only through the backend's alignment path (`backend.lua:684`).
- **Version strings disagree.** `manifest.json:5` and `PORT-STATUS.txt:1` say `0.5.42-shear.1-cffi-26568081`; `runtime.lua:1097` reports `seamless_version='0.5.41-smooth.66-cffi-26568081'`. The manifest description still describes the smoothing release.
- **The natural branch of the parked mode is not self-contained in "fird" files.** It depends on live code paths that exist only for it: `backend.fird_camera_active`, `Backend:fird_fov_config`, `movement:update_fird`, and three extra FOV helper exports (`NADSF_ValidateFird/BeginFird/BindFird`, declared in `fird_fov.lua:19-24`, present in `native_ads_fov.h`). A C port that drops Fird can drop these exports and backend branches, but must keep `fird_camera_offset`, which `head_placement.lua` uses for the live eye offset.
- **`ready_pose_contract.lua:1` still says build 26251122** although the kit targets 26568081; its 11 pins are not passed through the test runner's migration (the fixture loads the file as data), so whether they match the current exe is unverified here.
- **`tests/first_person/run_tests.py` and all six `*_binary.py` are dead upstream Windows scripts**: old flat paths, `tools/build.py` missing, `WinDLL`, old DLL file names. `docs/first-person-upstream-README.md:59-63` is the only place that tells anyone to run them.
- **The real runner is not portable**: it needs lupa, a cffi build, `port-tools` and three game binaries from absolute paths outside R, and it rewrites fixture constants at load time. The numbers in the fixture files are therefore not the numbers that are tested; a C port that reuses fixture vectors must take them after `fixture_source` has run, not from the files.
- **Fixture paths in the files are wrong on purpose**: fixtures say `/src/motion.lua` and `/main.lua`; the runner rewrites them to `/src/first_person/motion.lua` and `/src/first_person/runtime.lua` (`tools/test_first_person.py:53-55`). Some newer fixtures already use the real path, so both spellings occur.
- **Two math headers are included by nothing**: `camera_transition.h` (only hashed in `build_camera.py`) and `pin_look_math.h`. Four math fixtures (`zoom_input_math_fixture.cpp`, `zoom_gain_math_fixture.cpp`, `camera_correction_fixture.cpp`, `camera_handoff_fixture.cpp`) are built by no script in R.
- **`pose_fixture.cpp` / `native_ads_pose.cpp`** have only the MSVC builder and no shipped DLL in `native/`; the editor helper replaced the pose helper, while the Lua module is still called `pose.lua` and talks to `NADSE_*`.
- **Build scripts have side effects**: `tools/build_first_person.py`, `build_near.py` and `build_zoom.py` copy DLLs into `native/` and rewrite `src/first_person/profile.lua`. `native/` holds 50 DLLs, most of them for older game builds.
- **`tests/first_person/held_fire.lua` loads the module by its real path and builds its own compat object** when `TEST_PORT51` is absent, so it is one of the few fixtures that can also run outside the runner.
- The suite receipt `dist/first-person/lua-fixtures.json` (55/55 passed) is 18 seconds older than the last commit time of the branch head; it was not re-verified here.
