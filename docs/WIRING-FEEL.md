# The feel build (0.6.0-feel.1): what is wired to what

`bash build.sh --feel` builds `dist/true-first-person-feel/` (`goose_true_first_person.dll`, `hellforge-mod.json`, `settings.json`; id `goose.true-first-person`, version `0.6.0-feel.1`, game build 25480438, SDK 16). It was built on 2026-11-16 and **never loaded**: nothing here has run in the game or under Wine. It is not installed.

What it is: the view build (`docs/WIRING-VIEW.md`, unchanged and still built by `--view`) with five more parts of retail 0.5.41-smooth.66 bound to their C translations: weapon and arm placement (the pose helper), motion (bob, sway, per-action placements), head hiding with the model shadow source, sleeves and hand grips, and the effects (depth of field, vignette, aim markers, movement, native aim option), over a settings store loaded from the user's own retail settings. No menu, no F8 editor.

Files: `src/mod_view.c` compiled with `-DTFP_FEEL` (the entry; the feel parts are `#ifdef TFP_FEEL`), `src/wire_feel.c` (joins the module binders, the `Feel:` line), `src/wire_settings.c` + `src/tfp_settings.c`, `src/wire_pose.c` + `src/wire_win_pose.c` + `src/pose_input_thunks.S`, `src/wire_motion.c`, `src/wire_engine.c` + `src/wire_win_engine.c` + `src/engine.c`, `src/wire_effects.c` + `src/wire_win_effects.c`. Each binder is called after `tfp_wire_init` and replaces pointers of the runtime host that `src/wire_off.c` answered; `src/wire.h` (end of `TfpWire`) has the extension points.

## 1. Bound for real

Sections 8 to 10 have the per-pointer tables for the controller placement, the pose helper and the look-gain constants.

| Part | Runtime host pointers | Bound to | Where |
|---|---|---|---|
| Controller placement | `controller_get_placement`, `_placement_target`, `_set_placement`, `_status` | `tfp_controller_get_placement` … `_status` (`src/controller.c:66-108`), retail `controller.lua:102-147`, matched on 18,473 operations | `src/wire_rt.c:627-734`; section 8 |
| Pose helper | the backend's `TfpOpsPoseHelper` (10 functions), LEASE fields `depth_stats` / `input_stats`, `controller:result().blend`, `helper_depth_stats`, `helper_input_stats`, `backend_prepare_pose`; the controller's `weapon_updates` / `zero_restores` | `tfp_pose_adapter_*` over `tfp_pose_*`; five Unit API slots on Windows | `src/wire_pose.c:107-362`, `src/wire_win_pose.c`, `src/pose_input_thunks.S`; the counts `src/wire_rt.c` `api_pose_status`; section 9 |
| Settings | `config_table`, `config_field`, `config_action_mapped`, `mod_set`; `actions_configure_catalogue`, `_resolve`, `_saved`, `_default_placement`, `_weapon_info`, `_observed`, `_actions`; `menu_native_ads`, `_ads_mode`, `_fird_person_arms`, `_show_torso`, `_near_options`, `_motion_options`; the binder's `placement_decode` | `src/tfp_settings.c` (mod.config / mod.set and the menu control values), `src/action_select.c` (action_placement.lua) over the saved placements and the catalogue of `src/action_placement_data.h`; the menu methods as `menu.lua:150-182`, `:240-242` compute them | `src/wire_settings.c:83-429` (menu methods `:258-287`) |
| Motion | `motion_new`, `_update`, `_reset`, `_result`, `_capture_neutral`; `transition_action`, `transition_resume` | `tfp_motion_runtime_*` (`src/motion_runtime.c`) with `motion_math.c`, `action_select.c`; the sampler `tfp_locomotion_sample` and `action_state` over the **backend_ops** snapshot (one snapshot per frame); `tfp_transition_action` / `_resume` | `src/wire_motion.c:363-458` (pointers), `:133-198` (sampler), `:209-360` (the motion host), joined `:510-541` |
| Head visibility, model shadows | `head_new`, `_update`, `_stop`, `_result`, `_is_clean`, `_shutdown`, `_sdk_handles`, `model_shadows_new`; `profile_seated_head` | `tfp_head_*` with `tfp_head_build_25480438`, `tfp_model_shadow_init` with `TFP_VIS_APPEARANCE_25480438`; `TfpEngineIface` over the lua51 bridge | `src/wire_engine.c:318-389` (engine half), `:1265-1456` (host), joined `:1584-1638` |
| Sleeves, hand grips | `arm_pieces_new`, `_update`, `_stop`, `_is_clean`, `_result`; the same five of `grip_hand_`; `profile_body_sleeves`, `profile_grip_hand` (retail `profile.lua:84`, `:91`, `:93`: all true) | `tfp_arm_pieces_*`, `tfp_grip_hand_*`; `TfpArmEngine` over the bridge for the 15 unit / mesh / world functions; the 27 value functions computed in C (`tfp_wire_engine_maths`) | `src/wire_engine.c:390-544`, value maths `:545-658` |
| Effects | `dof_*` (7, the vignette behind it), `movement_*` (6), `native_aim_*` (6), `aim_markers_*` (11, with `aim_markers_fault`) | `tfp_dof_*`, `tfp_vignette_*`, `tfp_movement_*`, `tfp_native_aim_*`, `tfp_aim_markers_*` with this build's constant tables | `src/wire_effects.c:923-932` (joined), platform `src/wire_win_effects.c` |

How the engine's script functions are reached: `src/engine.c` (the donor bridge, proven in the game on this build by the 0.5.34 port) hooks `lua_pcall` / `lua_close` of `lua51.dll` from `hellforge_mod_init` and accepts only the Lua state whose `stingray.Unit.set_mesh_visibility` is `exe+0x4128e0`. Engine calls happen on the tick only, inside `eng_temp_begin` / `eng_temp_end` (`src/mod_view.c`, the `TFP_FEEL` tick).

**Function identities.** Before a module may run, every engine function it uses is resolved under `stingray` and its C body compared with this build's address from the census (`docs/research-0.5.34/live-sdk-census-g1-9472b065.tsv`): 41 functions for head / sleeves / grips (`tfp_wire_engine_functions`, `src/wire_engine.c:23-75`), 14 for dof / vignette / movement (`tfp_fx_functions`, `src/wire_effects.c:21-36`). A module with one function missing or elsewhere refuses as a whole: `Refused: <module>: stingray.X.y is …`; its pointers keep answering what `wire_off.c` answers.

**In-game checks** (read-only, once, before the module's first write; `Check: <module>: <check> ok observed=…` or `Refused: <module>: <check> observed=… expected=…`; `src/wire_engine.c:691-1020`):

| Module | Checks |
|---|---|
| head visibility | `unit registry code` (22 bytes at `registry_code`), `default pass id`, `shadow pass id` (the two context ids), `world handle` (the World userdata holds the body unit's own world, object `+0x18`), `unit inventory`, `unit handle` (exactly one handle whose address / 4 is the body's reference), `unit object` (reference at `+8`, node count `+0x70` and mesh count `+0xC8` equal to `Unit.num_scene_graph_items` / `Unit.num_meshes`), `mesh handle` (the Mesh userdata is the mesh object at `+0xD0`) |
| sleeves and grips | `value maths` (the engine asked once for values that tell its conventions apart, agreement within 1e-4), `rig and body objects`, `+0x1D0 parent unit`, `+0xA0 names and +0x98 links`, `+0x98 link words`, `pose scale`; grips: `rig bone count` (63), `grip bone names` (weapon bone on rig bone 8), `arm bone chain` (10 / 12 / 13 and 30 / 32 / 34), `arm bone lengths`; sleeves: `sleeve bone names`, and after the first `World.link_unit` the `link readback` (the link word at `+0x98` must name the bone asked for; undone on mismatch) |
| dof, aim markers, movement, aim option | the units' own code witnesses, with this build's bytes, at their first use (`the code is checked at the first hide` / `when the option is switched on`) |

A failed check leaves the game untouched and the module off for the session.

**Settings** (`dist/true-first-person-feel/settings.json`, made by `tools/convert_settings.py --config import/retail-config-2026-11-16.cfg`): 19 weapon keys under `action_placements` (with `__all`), `grab_offset(s)`, `near_clip_profiles`, `placement`, `fird_head_camera`, the three switches and `motion_scale`. A key the file lacks is retail's default (`src/first_person.lua:9-11`). The **menu control values** (motion strength sliders, show torso, near clip centimetres, native aim, …) were not in the imported snapshot (they live in the menu framework's own file): they are retail's registered defaults; `convert_settings.py --menu <ini>` takes them when that file is given. `mod.set` writes stay in memory; nothing is written to disk.

## 2. Still off, and why

| What | Answer | Why |
|---|---|---|
| Look-gain normalisation | refuses: `Refused: look gain: …`, `look=refused(2)` | `B_INPUT_ESTABLISHED` 0: one of the 20 constants is open (section 10) and none has run in the game. Scoped hip sensitivity is the game's own. Retail's two input hooks are installed and do nothing, as retail does when its proof fails (`docs/BUILD-DIFFERENCES.md`) |
| The two unconfirmed sprint tests | off (`B_SPRINT_GAIT_ESTABLISHED`, `B_BRIDGE_SPRINT_ESTABLISHED` 0, `src/locomotion.c:55`, `:59`) | sprint is seen through animation layer 3 = 71 only; sprinting with a secondary or on the bridge may read as walking |
| F8 editor, menu | `editor_*` off answers; `menu_update` is the F11 poll | not written. Motion's two editor pointers are NULL ("no editor") |
| Photo Mode camera sharing (`compat_*`) | off answers; `Off: camera sharing with Photo Mode …` | nothing to bind to on HellForge; Photo Mode is **not detected**: do not open it while the view is on |
| Fird Person, ready stance, held fire, capture gate, head positioner, camera debug calls | the view build's answers | parked in retail / non-goals |
| `backend_alignment_context` of the diagnostics row, `backend_pose_status` | nil | `+0x80`, `+0x88`, node 5 not established on this build; only the parked Fird capture asks the second |
| Presentation switch (`tfp_ops_invoke_presentation`) | refuses | constants open; nothing on the shipped path calls it |
| Head hiding, sleeves, grips until their checks pass | the off answers (body drawn as the game draws it) | by design: nothing is written before the checks; retail has no off path for the head, so until `Ready: head visibility: the in-game checks passed` the first-person view shows the player's own head as in the view build |
| Final lease statistics at release | not copied to the controller | `lease_sync` (`src/wire_rt.c`) does not copy `ops.pose_stats`; the status line's "hip blend" after a release is the last polled value |
| Frame clock, mouse buttons | `QueryPerformanceCounter`, `GetAsyncKeyState` | unchanged from the view build (its section 6); head visibility's own clock is `GetTickCount64()/1000` as in retail |
| Settings persistence | none | memory only in this build |

**Off-answer proof** (`tests/run-vectors-view-off.sh`): the unmodified retail `runtime.lua` is run under LuaJIT through a session in which the modules this build binds answer as in ordinary play and every module it still stubs answers what `wire_off.c` answers (`tools/retail_ref/gen_view_off.py --feel`, 55 operations, 2,235 calls into the collaborators). The C runtime order gives retail's answers, traces and state, and the **447** calls into a still-stubbed module are answered by `wire_off.c` exactly as the collaborator answered retail; 743 calls went to bound modules; 0 calls unchecked. Which pointers count as bound is `tests/feel_bound.h`, and `tests/wire_feel_test.c` checks that list against the pointers the binders really replace. The view build's own proof (1,137 answers) is unchanged.

## 3. Constants

| Group | State | Evidence |
|---|---|---|
| Look gain: 27 names, 20 values | 19 found and entered, `B_PIN_EXIT_FNV` open, flag 0 | `docs/LOOK-GAIN-CONSTANTS-25480438.md`; `python3 tools/find_look_gain.py --check-c src/pose_helper.c` (26 pass, 1 open); section 10 |
| dof: `get_thunk` exe+0x3fa3d0 `488bd1e938feffff`, `set_thunk` exe+0x3fa120 `488bd1e998fbffff` | verified | image bytes; the thunks jump into functions reading the renderer settings table; census 238, 267 |
| aim markers: setter game+0x144cfb0, checks game+0x17b48df, 0x17b4971, 0x12e98ed, HUD root **game+0x346d538** (the donor file said 0x346d518; the image's witness code resolves to 0x346d538), crosshair +0x19aaf8, state game+0x3326340, scene +0xac21c | verified | image bytes and RIP-relative targets |
| movement: six anchors game+0x9cc2ec, 0x9cc369, 0x81ce72, 0x9cb5d4, 0x9ca83b, 0x82c1e3; pool game+0x3326cf8 | verified | image bytes; anchors 0 and 5 differ from retail's only in a displacement / call target |
| native aim: player game+0x3326468, weapons game+0x3326ce0, witnesses game+0xa427b5, 0xa426c3, 0xa42873 | verified | image bytes |
| vignette class `0x35b0f6c2`, renderer keys | names | equal to the donor's (`src/effects.c:157`, `:299`) |
| engine functions (55) | census addresses | compared in the running game at load |
| head build: `registry_code` bytes, two context ids | bytes from the exe image; ids checked in game | `Check: head visibility: …` |
| unit object offsets `+0x70`, `+0x98`, `+0xA0`, `+0x1D0`, `+0xC8`, `+0xD0`; rig 63 bones; arm bones; bone name hashes | checked in game before any write | `Check: arm pieces / grip hand: …` |
| pose slots exe+0x1fc720, 0x1fc880, 0x1fc7a0, 0x1fc450, 0x1fd220 | image; compared at load (`Ready: pose slots: …`) | section 9 |

All four effect tables: `tools/verify_effects_constants.py` (62 checks against the two image files; part of `tests/run-wire-effects.sh`). The units keep retail's tables for their vector tests and get build 25480438's from the binder.

## 4. Session log (besides the view build's lines)

- At load: `Ready: settings from <path>: N weapon placement profiles …` (or `Refused: settings: …; retail's defaults are used`), `Ready: pose helper bound …`, `Ready: motion bound …`, `Ready: model shadows …`, `Ready: pose slots: …`, `Ready: markers …`, `Ready: aimopt …`, `Off: camera sharing …`; then, when the engine's Lua state is caught (first frames): `Ready: <head visibility | arm pieces | grip hand>: N engine functions are build 25480438's own …`, `Ready: dof …`, `Ready: vignette …`, `Ready: movement …`; any `Refused: <module>: …`.
- First time a player exists / the view is on: the `Check:` lines, then `Ready: head visibility: the in-game checks passed …`, `Ready: arm pieces: the in-game checks passed`, `Ready: grip hand: …`, `Ready: pose: the helper's read-only validation passed …`.
- On each weapon: `Weapon: key=<16 hex> name="…" family=… placement=<imported(own) | imported(__all) | family default | shipped default …> x= y= z= yaw= pitch= roll= near=<imported(own)|global> hip= aim=`.
- On each action change: `Motion: action=<idle|aim|ads|sprint|…> placement=… x= … softened=N phase=…`.
- `Pose: begin generation N …`, `Pose: finish generation N: …`, `Hook: pose <slot> first fired on thread N, called from game+0x…`.
- Once a second while on, after the `View:` line: `Feel: t=… pose=armed blend=1.00 wjump=0.0012m/0.31deg placements=N motion=N err=0 writes=N look=refused(2) motion=on action=idle phase=… move=…m turn=…deg bob_weight= speed= scale= applied=N softened=N refused=N head=active:N sleeves=linked:N grip=on corr=N max=0.004m/1.2deg dof=owned vignette=owned:N markers=hidden move=idle aimopt=off`. `wjump`: the largest change of the commanded weapon / arm shift between two frames that the frame before does not explain (metres / degrees): a snap shows as centimetres. `corr` / `max`: grip corrections that second and the largest one. `sleeves=`: pieces linked and the module's phase.

## 5. Not proven until it runs in the game (ranked)

1. **Nothing of this build has executed on Windows.** The join was run only with no game (every read failing, `tests/wire_feel_test.c`). First F11 exercises the pose helper's five Unit API slots, which carry every unit's calls on unknown threads.
2. **Where HellForge's tick falls in the frame.** The sleeves and grips read the previous animation evaluation and write local poses; retail does this inside the script update. If the tick is on the wrong side of animation, sleeves lag a frame or grip writes are lost: `corr` high every second with visible hand jitter, or `wjump` clean while the arms visibly snap.
3. **`World.link_unit` and `Unit.set_local_pose` were never called on this build.** The link readback guards the argument order; a wrong link is undone and the module refuses.
4. **The pose helper's layout constants** (weapon definition, depth pin) rest on translation notes: `Refused: pose: read-only validation … native code N` before any write if wrong.
5. **Engine handles.** `userdata_address` for Unit / Mesh / World is checked, but against a stand-in Lua state in tests only.
6. **Cost.** `World.units` each head update (up to 131,072 handles), about 55 function identity checks at load, the vignette's `World.entities` scan: watch the frame time; the view build's `Reads:` line gives the read cost.
7. **The two assembly thunks** and `_AddressOfReturnAddress()+8`: assembled and disassembled, never run (section 9).
8. **Movement and the aim option write game memory** (one byte each, with `WriteProcessMemory`, after their code witnesses pass); `Keyboard.*` was never called on this build.
9. **Aim markers call the game's widget setter** at game+0x144cfb0 under SEH; the HUD root differs from the donor's note by 0x20 (image evidence).
10. **Motion with menu defaults.** Strengths are retail's registered defaults, not necessarily the user's sliders. Sprint only by layer 3 = 71.
11. MinGW math last bits, `_Thread_local`, `setjmp` across the runtime: as in the view build.
12. Session end restores up to nine slots with compare-and-swaps only; a mesh hidden or a setting changed at that moment is not restored (retail has the same gap after a crash).

## 6. In-game checklist

Install only on the user's word (game and HellForge closed; First Person Controller and the older `true-first-person` off; copy `dist/true-first-person-feel` into the install's `mods` folder, replacing the view build: same id).

| # | Do | Expect in the log |
|---|---|---|
| 1 | Start, reach the ship | the view build's banner / `Image:` / `Pins:` lines, then `Ready: settings from …settings.json: 19 weapon placement profiles …`, `Ready: pose helper bound …`, `Ready: motion bound …`, `Ready: model shadows …`, `Ready: pose slots: …`, `Ready: markers …`, `Ready: aimopt …`, within seconds `Ready: head visibility: N engine functions are build 25480438's own …`, the same for arm pieces and grip hand, `Ready: dof`, `Ready: vignette`, `Ready: movement`. No `Refused:` except `look gain` later |
| 2 | Mission, weapon in hand, F11 | the view build's entry lines; `Weapon: key=… placement=imported(own) …` for a weapon you placed in retail; `Ready: pose: the helper's read-only validation passed`, `Pose: begin generation 1 …`; the `Check: head visibility: … ok` lines and `Ready: head visibility: the in-game checks passed`; `Check: arm pieces / grip hand …` and their `Ready:`; `Hook: pose …` lines; each second `View:` and `Feel:` |
| 3 | Stand still 10 s | `Feel:` with `pose=armed err=0`, `writes` rising, `wjump` below a millimetre, `head=active:N`, `sleeves=linked:N` steady, `corr` low |
| 4 | Walk, turn, sprint, crouch, prone | `Motion: action=…` on each change; `wjump` a few millimetres, no centimetre spikes; `refused=0` |
| 5 | Aim and release; scoped weapon | view build rows 5-6; `blend` to 0 and back; `wjump` about 1-2 cm during the 150 ms ease only; `dof=owned`, `markers=hidden` while the view is on; once `Refused: look gain: …` |
| 6 | Swap weapons, then stim / grenade / dive | `Pose: finish …`, `Weapon: key=<new> placement=…`, `Pose: begin generation N+1`; sleeves relink (`sleeves=` changes and returns); the `wjump` and `corr max` of those seconds are the snap to judge |
| 7 | F11 off | `Pose: finish`, `State: exit complete`; the `Feel:` lines stop; third person looks as before (head, shadows, depth of field, crosshair) |
| 8 | Quit with the view on | `Session end: N dispatch slot(s) put back` (up to 9) |

Bad session: any `Refused: <module>: <check> observed=…` (send the line: it has the observed values); `err` not 0 while `pose=armed`; `wjump` or `corr max` in centimetres outside entry / swap / aim; `refused=` rising on the motion fragment; a body part still hidden or the crosshair missing after F11 off.

## 7. Tests

`bash tests/run-native.sh` runs everything. New for this build: `tests/run-wire-settings.sh` (store, converter against the defaults and the user's file, retail's answers on 2,903 operations), `run-wire-pose.sh` (201 checks, 78 against the real adapter and helper), `run-wire-engine.sh` (227 checks over a stand-in Lua state; the arm vectors replayed over the C value maths), `run-wire-effects.sh` (294 checks; 62 constant checks against the images), `run-wire-feel.sh` (the join with no game, the motion binder's adapters), the controller vectors (18,473 operations) and the feel session of the off-answer proof. Not covered by a host test: everything in `src/wire_win*.c` and `src/mod_view.c` (Windows), and the motion binder against a live sampler (its units have their own vectors; the binder was run only with failing reads).

## 8. Controller placement and status

### Bound (file:line)

| Host pointer | Binds to | Where |
| --- | --- | --- |
| `controller_get_placement` | `tfp_controller_get_placement` (`src/controller.c:66`, controller.lua:102-105): the backend's `pose_placement` when it has one, else the controller's own; missing angles read 0 | `src/wire_rt.c:666` |
| `controller_placement_target` | `tfp_controller_placement_target` (`src/controller.c:73`, :106-109): nil unless running with a lease and the backend names a weapon. The second result (`lease.origin.identity`) is rendered by the binder from the lease's origin snapshot | `src/wire_rt.c:677` |
| `controller_set_placement` | `tfp_controller_set_placement` (`src/controller.c:79`, :110-135): retail's refusal texts, `set_pose_placement` with a running lease, `set_placement_defaults` otherwise, the controller's own copy replaced only on acceptance | `src/wire_rt.c:716` |
| `controller_status` | `tfp_controller_status` (`src/controller.c:108`, :138-147), with the counts of `accumulate_pose_stats` (:81-95) | `src/wire_rt.c:734` |
| the backend's side (`TfpPlacementApi`, `src/controller.h`) | `TfpOps.pose_placement` / `.placement_weapon_key`, `tfp_ops_set_pose_placement`, `tfp_ops_set_placement_defaults` | `src/wire_rt.c:627-665` |
| `tfp_wire_placement_check` | the same controller function with a backend that accepts (one rule, one place) | `src/wire_rt.c:702` |

Values: a placement arrives as a ref and is read with `tfp_wire_placement_of` (the shipped default, or whatever `placement_decode` knows); a field whose `present` bit is clear is nil. `{}` (`tfp_rt_empty_table`) is a table with every field nil. Anything that is not a ref is "not a table".

Proof: `tests/vectors/controller_vectors.h` (18,473 operations in 9 runs, retail controller.lua run unmodified under LuaJIT): 453 `set_placement`, 389 `get_placement`, 407 `placement_target`, and `status()` compared after every one of the 18,473 operations. `tests/wire_glue_test.c` covers the binder side (23 checks: decode, the defaults route, the lease route, the target, the status text).

### Still off and why

- **"arms/weapon offset updates"** is fed in the feel build from the pose binder's last status poll (`TfpWire.pose_counts` → `tfp_wire_pose_counts`, `api_pose_status` in `src/wire_rt.c`); in the view build (no pose helper) it stays 0. The backend's own statistics (`TfpOpsHelperStats`) still carry no such counts.
- **The lease's final statistics are not read at release.** Retail takes `lease.pose_stats or lease.alignment_stats` once the release is acknowledged (:165). `TfpLease.has_pose_stats` / `.pose_stats` exist for it (`src/controller.h`) but `lease_sync` (`src/wire_rt.c:281`) does not fill them. Effect: after a release, "hip blend" shows the last polled value instead of the helper's final one. Needed in `lease_sync`: copy `ops.pose_stats` (else `ops.alignment_stats`) `blend`, `target_weight` (and the counts once they exist) into `view.pose_stats` and set `view.has_pose_stats`.
- **A table the binder cannot decode** (a ref that is neither the shipped default, `{}`, nor known to `placement_decode`) is refused with the binder's own text "Placement table is not one the binder can read", not a retail text. It can only appear if the actions binder hands out a ref without registering its decoder. Note `on_enable` asserts on `set_placement`.
- **controller.lua:127** ('Placement requires the arms/weapon test') is not in the C: the C controller only ever starts in the arms / weapon mode, so a running lease of another mode cannot exist.
- Retail tells a field that is `false` from one that is nil; the C reads both as nil. The answers are the same for both in these Lua lines (an x/y/z of either kind is refused, an angle of either kind is 0).
- A backend refusal text is cut to 159 characters by the controller (the same buffer size as its other backend texts), and an empty text counts as none.
- `docs/WIRING-VIEW.md` (row `controller_get_placement…`, section 6 item 1) and `docs/RUNTIME-TRANSLATION.md` (controller table, "no placement and no status text") still describe the stand-in.

### Constants and evidence

| Constant | Value | Evidence |
| --- | --- | --- |
| shipped placement | x 0.15, y 0, z -0.12 m, no angles | controller.lua:23; `tfp_controller_init` |
| `TFP_PLACEMENT_REACH` | 0.5 m, inclusive | controller.lua:116; vectors accept ±0.5 and refuse ±0.5000001 |
| `TFP_PLACEMENT_ANGLE` | 90 degrees, inclusive | controller.lua:123; vectors accept ±90 and refuse ±90.0001 |
| refusal texts | "Placement requires XYZ", "Placement must be finite XYZ within 0.5 metres", "Angles must be within90degrees" (no spaces, as retail wrote it), "Placement was not accepted" | controller.lua:111, :117, :123, :132; compared byte for byte in the vectors |
| number text in the status line | Lua's `tostring`: `%.14g`, `nan`, `inf`, `-inf` | vectors with 1/3, 0.1+0.2, 1e15, 1e17, nan, ±inf |

### Unproven until it runs in the game (ranked)

1. **`set_pose_placement` on a live lease.** The route is proven (which call, which values); what the backend and the pose helper then do with a live lease was never run. In the glue test the lease has no game behind it and the backend refuses.
2. **Saved per-weapon placements with angles reaching the controller.** Depends on the actions binder's `placement_decode`; here only a test decoder was used.
3. **`placement_target`'s second result** against what the editor / saved-placement code compares it with: the binder renders the lease origin's weapon-level identity as `W…` text (`tfp_wire_identity_text`), as it does for snapshots; equality with the snapshot identity text of the same weapon is expected, not observed.
4. The status line during a real session (counts stay 0 until the first item of "Still off" is done).

### In-game checklist rows (Do | Expect in the log)

| Do | Expect in the log |
| --- | --- |
| Load the mod, do nothing | no "Placement …" refusal and no raise from `on_enable` (its `set_placement` of the default answered true) |
| F11 on a firearm, wait for the view, read the diagnostics / status row | status ends "…; camera6/aimfalse observations=N; arms/weapon offset updates=N; hip blend=1; aim transitions=N" (hip blend 1 at rest; updates rise while the rig hook writes) |
| Hold aim, release | "aim transitions" goes up by 2; "hip blend" moves away from 1 while aiming and returns |
| With the view up, change a placement in the editor (or the debug `placement` entry) by a few centimetres | accepted (no refusal line); the arms / weapon move; the placement row shows the new x/y/z and yaw/pitch/roll |
| Enter a placement beyond 0.5 m or 90 degrees through the debug entry | "Placement must be finite XYZ within 0.5 metres" / "Angles must be within90degrees"; nothing moves |
| F11 off, then set a placement | accepted through the defaults (no lease); the next F11 entry starts at that placement |
| Swap weapons with the view up | the placement target (weapon key) changes with the weapon; no "Pose placement lease is not active or owned" once the new view is up |

## 9. Pose helper (weapon and arm placement)

The real pose adapter (`src/pose_adapter.c`, pose.lua) and pose helper (`src/pose_helper.c`, native_ads_editor.cpp) replace the view build's stand-in (`src/wire_rt.c`, "the pose stand-in"). Files: `src/wire_pose.c` / `.h` (platform-free binder), `src/wire_win_pose.c` / `.h` (Windows host), `src/pose_input_thunks.S` (retail's three assembly entries), `tests/wire_pose_test.c` + `tests/run-wire-pose.sh` (201 checks). Built and linked for Windows, **never loaded**: nothing here has run in the game or under Wine.

### Bound (file:line)

| What | Bound to | Where |
|---|---|---|
| `TfpWire.pose_helper` (the backend's `TfpOpsPoseHelper`) | ten functions over `tfp_pose_adapter_*`: `prepare`, `begin`, `owned_handle`, `finish`, `stats`, `set_placement`, `set_zoom_policy`, `set_ads_placement`, `transition`, `set_motion`. First results as backend.lua sees them: true / false with pose.lua's text / raised (a failed assertion of `prepare` or `begin`); `stats` answers nil with the text, or the final statistics of a disarmed handle | `src/wire_pose.c:107-276`, joined at `:338-362` |
| the configuration (`backend.lua:839-857` as `pose.lua:141-149` reads it) | `tfp_wire_pose_config`: field for field; numbers to `uint32_t` as the FFI store does; an alignment configuration has the six pose fields missing | `src/wire_pose.c:69-83` |
| handles | one allocation per begin, never reused (a handle is compared by identity, like a lease): the backend's `TfpOpsHandle` first, the adapter's `TfpPoseHandle` behind it. A begin that was refused but took a generation stays owned by the adapter and is handed out by `owned_handle` (`backend.lua:604-625`) | `src/wire_pose.h` `TfpWirePoseHandle`; `src/wire_pose.c:125-158` |
| the helper's 17 exports | `tfp_pose_*` of `src/pose_helper.h` as they are | `src/wire_pose.c:18-24` |
| LEASE table: `depth_stats`, `input_stats` (function fields, `runtime.lua:377`, `:1136`, `:1147`) | true for every lease whose helper is this one | `src/wire_pose.c:279-284` |
| `controller:result().blend` (`controller.lua:94`, `:148-155`) | the controller's own `summary.blend` (`TfpController.has_blend` / `.blend`, set from each status poll and from the final statistics) | `src/wire_pose.c:287-289` |
| host `helper_depth_stats` (`runtime.lua:378`, `:1139`) | `tfp_pose_adapter_depth_stats`; `supported`, `armed`, `zoom_index` to the runtime (this is what gives `zoom_identity` to the transition clock) | `src/wire_pose.c:296-313` |
| host `helper_input_stats` (`runtime.lua:1150`) | `tfp_pose_adapter_input_stats` | `src/wire_pose.c:315-324` |
| host `backend_prepare_pose` (debug entry `preflight`, `runtime.lua:1189`) | `tfp_ops_prepare_pose` with a token of its own | `src/wire_pose.c:326-336` |
| `TfpPoseAdapterIo` | `tick` = `GetTickCount64`, no clock function (`pose.lua:29-36`, `backend.lua:807`) | `src/wire_win_pose.c:204-207` |
| `TfpPoseHost.hooks[0..3]` | C wrappers handing over `_ReturnAddress()` and, for position and getter, `_AddressOfReturnAddress() + 8` | `src/wire_win_pose.c:56-75` |
| `TfpPoseHost.hooks[4]`, `[5]` (input mode) | `tfp_pose_input00_thunk`, `tfp_pose_input90_thunk`: retail's thunks with the calls retargeted | `src/pose_input_thunks.S:25-38`, `:70-83` |
| `read`, `tick`, `thread`, `writable_slot`, `executable`, `write`, `pin`, `writable32`, `region`, `allocate` | `tfp_win_read` (code through `hf->read_code`), `GetTickCount64`, `GetCurrentThreadId`, the view build's slot / page / write / pin rules (shared from `src/wire_win.c:381-397`), `VirtualQuery`, `VirtualAlloc` | `src/wire_win_pose.c:100-122` |
| `cas` | `fp_atomic_pointer`, after the pin check: a value of the helper goes into a slot only when the slot holds `exe + pinned RVA` and the bytes there, read through `hf->read_code`, equal the pin. Otherwise nothing is written, the helper rolls back with error 11, and `tfp_win_pose_hook(i)->refused` is set for the tick to log | `src/wire_win_pose.c:129-154`; pins `:22-30` |
| `cas32` | `tfp_pose_input_compare_exchange` (retail's `lock cmpxchg` with its own exception handler, `tfp_pose_input_cas_exception`) | `src/pose_input_thunks.S:51-63`; `src/wire_win_pose.c:86-97`, `:108-111` |
| the four lock functions | two SRW locks (control 0, policy 1) | `src/wire_win_pose.c:123-126` |
| session end | `tfp_win_pose_restore_slots`: one compare-and-swap per slot that still holds one of the six values | `src/wire_win_pose.c:155-165` |
| load check | `tfp_win_pose_check`: the five pinned functions and what the five slots hold | `src/wire_win_pose.c:167-202` |

The pose slots (while a lease runs; put back by the helper's End, or at session end):

| Slot | Function it must hold (pin) | Value while leased | Caller it acts on |
|---|---|---|---|
| UnitAPI `+0x38` (`*(*(game+0x3326308)+0x18)+0x38`) | exe+0x1fc720 | `position_hook` | game+0x1288424 (rig root), game+0x12887cd (carrier), any write of the owned rig's node 0 |
| UnitAPI `+0x48` | exe+0x1fc880 | `rotation_hook` | entry rotation guard: rig node 5 during the first 300 ms |
| UnitAPI `+0x40` | exe+0x1fc7a0 | `branch_rotation_hook` | game+0x12890df |
| UnitAPI `+0x00` | exe+0x1fc450 | `tfp_pose_input00_thunk` (input mode asked) | game+0x1287a44 |
| UnitAPI `+0x90` | exe+0x1fd220 | `tfp_pose_input90_thunk` | game+0x12865fe (does nothing on this build) |

Every other caller is forwarded untouched. These five slots carry the calls of every unit in the game, so the four C wrappers count nothing: they note their first caller once and call the helper.

### Still off and why

| What | Answer | Why |
|---|---|---|
| Look-gain normalisation | installed as retail installs it, never active: `installed 1, active 0, reason 2` | `B_INPUT_ESTABLISHED` is 0 (`src/pose_helper.c:109`, `:782-783`): one code pin is still open (`B_PIN_EXIT_FNV`, commit fb3957f) and none of the others has run in the game. This is retail's own path for a proof that does not pass (`docs/BUILD-DIFFERENCES.md`, "Look gain in the feel build"). Scoped hip sensitivity stays the game's own |
| host `backend_pose_status` | nil (the view build's answer) | only `sample_capture` of the parked Fird Person entry calls it (`runtime.lua:984`), and that entry refuses before it gets there |
| `helper_depth_stats` / `helper_input_stats` for an alignment lease | not applicable | no alignment helper exists in this mod |
| the alignment helper (`load_alignment`) | raises "The alignment helper is not part of this mod" | not on retail's shipped path |
| `set_zoom_policy` | bound, never called | its only caller in retail is parked |
| the applied rig pose in the log | not read | the helper keeps the pose it wrote to itself; the meter uses what the binder handed over and the helper's reported weight (below) |
| counters for the two input thunks, per-call counters for the four wrappers | none | they sit on every unit's calls; the helper's own statistics (`writes=`) show the matched calls |

### Constants and evidence

| Constant | Value | Evidence |
|---|---|---|
| the five slot functions and their bytes | exe+0x1fc720, 0x1fc880, 0x1fc7a0, 0x1fc450, 0x1fd220 with 48 / 48 / 48 / 51 / 47 bytes | the same numbers as `src/pose_helper.c` `B_EXE_POSITION` … `B_HEX_WORLD_MATRIX` (build map and the exe image, `docs/POSE-HELPER-TRANSLATION.md` section 2). Repeated in `src/wire_win_pose.c:22-30` for the binder's own check; not compared with the running game |
| Unit API table | `*(*(game+0x3326308)+0x18)` | `B_GAME_API_ROOT`, `L_API_UNIT` (image: `mov r9,[rcx+0x18]` at game+0x1288414) |
| slot offsets | +0x38, +0x48, +0x40, +0x00, +0x90 | `L_API_*` (image, input evidence) |
| thunk frame | 56 bytes: return address at `[rsp+56]`, caller's S = `rsp+64` | `native_ads_input.S:9-18`; disassembly of the built object: `sub rsp,0x38; mov r8d,edx; mov edx,ecx; mov rcx,[rsp+0x38]; mov r9,r15; lea rax,[rsp+0x40]; mov [rsp+0x20],rax; call; add rsp,0x38; ret` (0x90), and `…; lea r9,[rsp+0x40]; mov [rsp+0x20],r15; call; …` (0x00). Unwind data: one `ALLOC_SMALL 56` each; the compare-exchange carries the exception handler |
| C wrappers | return address `[rbp+0x28]`, stack `rbp+0x30` with 5 (position: `push ×5; sub rsp,0x30; lea rbp,[rsp+0x30]`) and 4 pushes (getter) | disassembly of `work/obj-pose-wire_win_pose.o`: the value handed over is the address of the wrapper's own return address + 8 |
| look gain | `B_INPUT_ESTABLISHED` 0 | owned by the look-gain work (`src/pose_helper.c` constant block); this binder reads only the helper's input statistics |

### Unproven until it runs in the game (ranked by risk)

1. **The pose helper has never run against the game.** Its C is proven equal to retail's on fake memory in both layouts; that this build's constants are right is not (`docs/POSE-HELPER-TRANSLATION.md` section 6: the weapon definition layout, the layout constants taken from the 0.5.34 sources, `B_DEPTH_PIN_2`). A wrong constant shows as `Refused: pose: read-only validation: … native code N` (4 image, 5 code, 6 API, 7 owner, 15 graph) before anything is written, or as `Refused: pose: helper error N` once a lease runs.
2. **Which thread calls the five Unit API slots, and how often.** They are the unit setters and getters of the whole game. Retail's hooks filter by caller and unit before any lock; the wrappers add one read of a word. If the slots are called from several threads at once, the helper's design covers it (atomics, then the policy lock for a matching call), but this has not been watched here. `Hook: pose …` lines give the first caller and thread.
3. **The two assembly thunks.** Assembled, linked and disassembled, never executed. A wrong register would show as a crash at the first Unit API getter call after entering, or as `Refused: pose: helper error 15 / 16`. They are installed although the look gain refuses (retail does the same); the profile `{2, false}` removes them from the picture if they misbehave.
4. **`_AddressOfReturnAddress() + 8` under zig's clang** is checked in the disassembly, not in a run. The helper pairs the lens observation with the rig write by this stack value: if it were wrong, the zoom depth would report `supported=false` and nothing else would change.
5. **Where the tick falls.** The status poll (blend) is taken on the tick, the rig write happens in the game's update: the meter sees the weight of the frame before. A constant one-frame lag does not show as a jump.
6. **The structured exception handler of the 32-bit compare-exchange** (`.seh_handler` from assembly under zig for `x86_64-windows-gnu`): present in the unwind data, never raised. Only the look-gain path reaches it, which refuses here.
7. **Session end** for the pose slots: compare-and-swaps only, untested, as for the view build's four.
8. **MinGW's `tanf` / `sqrtf`** in the zoom depth term against retail's: last bits.

### In-game checklist rows (Do | Expect in the log)

| Do | Expect in the log |
|---|---|
| Start a session, reach the ship | `Ready: pose helper bound (placement, ADS placement, motion, transitions, draw and entry blend, zoom depth); look gain asked for as retail does (the helper decides)`, and (from `tfp_win_pose_check`) a line that UnitAPI `+0x38, +0x48, +0x40, +0x00 and +0x90 hold the five pinned functions`. No `Refused: pose` |
| In a mission, weapon in hand, standing: F11 | `Ready: pose: the helper's read-only validation passed …` (first time only), `Pose: begin generation 1, steady entry …, placement (0.150, 0.000, -0.120) = 0.1921 m, duration 0 ms, blend 150 ms`, then `Hook: pose position (UnitAPI+0x38) first fired …`, and each second `pose=armed blend=1.00 wjump=0.0000m/0.00deg placements=0 motion=0 err=0 writes=N look=refused(2)` with `writes` rising. The weapon and arms are shifted by the placement instead of sitting where the view build left them |
| Two seconds later | once: `Refused: look gain: the look evaluator's code proof is not established on build 25480438 (input reason 2, code unavailable). Retail's two extra hooks are installed, …` |
| Stand still, then walk and turn | `err=0`; `wjump` small and of the same size second after second (with the motion module bound, `motion=N` is above 0; note the usual value in the first session: it is the baseline a snap is judged against) |
| Aim (right mouse), release | `blend` falls to 0.00 while aiming and returns to 1.00; `wjump` in those seconds about 0.01 to 0.02 m (the 150 ms ease of 19 cm), not 0.19; no `Pose: finish` in between |
| Aim with a scoped weapon, change zoom | `look=refused(2)` stays; the look speed in the scope and at the hip is the game's own |
| Swap weapons | `Pose: finish generation N: … shifted rig writes, … restores, helper error E (meaning)` (0, or an owner / weapon change such as 7 or 8, which is the swap itself), then `Pose: begin generation N+1, steady entry …` for the new weapon. `pose=none` or `unarmed` for the seconds in between |
| Stim, grenade, dive | as the swap: finish, then begin again when the view returns; a `blended entry` after an aim-out return |
| F11 off | `Pose: finish generation N: …`; afterwards `pose=none`; the five slots hold the game's functions again (a second F11 on must begin cleanly) |
| Quit with the view on | `Session end: N dispatch slot(s) put back` counts up to 5 more for the pose slots |

A session is bad when: any `Refused: pose:` other than a single refusal around a swap or death; `err` other than 0 on a `pose=armed` line; `writes` not rising while `pose=armed` in first person (the rig hook does not match its call site); `wjump` above a few centimetres in a second without an edit, a swap or an entry; `Refused: the pose … slot was not swapped`; `look=on` (the gain must not be active in this build).

## 10. Look-gain constants

### Constants and evidence

Full table, bytes and method: `docs/LOOK-GAIN-CONSTANTS-25480438.md`; re-derive with `python3 tools/find_look_gain.py --check-c src/pose_helper.c`. 19 of the 20 values (26 of 27 names) found; all entered in the constant block; `B_INPUT_ESTABLISHED` stays 0.

- `B_PIN_2` + hash: FOUND `game+0x1288908`. The evaluator's jump table, named by its table load at `game+0x1286680`; six entries each retail's + `0x16dfc0`.
- `B_PIN_3` + hash: FOUND `game+0x1282a40`. Only place where the 1724 bytes equal retail's outside relocated targets.
- `B_PIN_EXP_FNV`: FOUND `0xfa3d329f82edb886`. 1013 bytes at `B_GAME_EXP` equal retail's outside relocated targets; this build's function table ends the function at +1013.
- `B_PIN_6` + hash: FOUND `game+0x1277780`. Only match; differs in `B_SETTINGS_DEGREES` and one padding byte.
- `B_PIN_7` + hash: FOUND `game+0x1279970`. Only match; starts 2 bytes before an instruction as retail's does; differs in `B_GAME_PRESETS+0x8c` and its own jump table.
- `B_PIN_9` + hash: FOUND `game+0xfd9ba0`. Only match, and the function the steering pin calls; differs in the two world entity fields.
- `B_PIN_12`, `B_PIN_13` + hashes: FOUND `exe+0x1fd190`, `exe+0x1fd1d0`. Only matches; `0x90` and `0x50` below the world-matrix getter as in retail.
- `B_PIN_EXIT_FNV`: OPEN. This build's exit function is a different body (`0x1d58` bytes, 1683 instructions, against `0x1d9d`, 1696). Candidate `0xab98f35fc8fb95d5` over the `0x1dd0` bytes the C reads; not entered.
- `B_CONST_ONE` `0x23c6d70`, `_MINUS_ONE` `0x23c800c`, `_RAD_TO_DEG` `0x23c7730`, `_MINUS_HALF_PI` `0x23c8040`, `_HALF_PI` `0x23c6f1c`, `_MINUS_75` `0x23c8228`, `_DOUBLE_ONE` `0x23c7098`: FOUND. Each is the address the matched load in the evaluator or solver names; same bytes at both addresses.
- `B_GAME_NO_AVATAR` `0x3484788`, `_IDLE_AVATAR` `0x3483c34`: FOUND, read by the steering pin at `game+0x128d860` and `+0x128d84f`. `_HOST_AVATAR` `0x348456c`: FOUND, read by pin 9 at `game+0xfd9bb9`.
- `B_INPUT_DEVICE` `0x324`: FOUND, the evaluator reads `[rdi+0x324]` at `game+0x128678b` and `+0x128686a` with `rdi` = the input owner, the same bytes as retail.
- Also confirmed by a matched instruction: `B_GAME_CLOCK` `0x3326348` (evaluator, `game+0x12869d7`), `B_WORLD_ENTITY_AVATAR` `0xf32f20` (pin 9, `game+0xfd9c50`).

### Unproven until it runs in the game

- The 15 hashes are of dumps from 2026-10-17 and 2026-10-22; the running game's bytes have not been hashed.
- The exit pin is undecided, so the code proof cannot pass and the normalisation refuses (`B_INPUT_ESTABLISHED` 0).
- The three steering cells are writable data: the images show where they are, not what they hold in play.
- `B_INPUT_DEVICE` meaning (0 mouse, 1 or 2 pad) rests on the evaluator being the same code; not watched while switching devices.
- The normalisation has never run on this build (its two assembly thunks are wired in the feel build and do nothing while the proof refuses).
