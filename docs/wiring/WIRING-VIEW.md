# The view build (0.6.0-view.1): what is wired to what

`bash build.sh --view` builds `dist/true-first-person-view/` (`goose_true_first_person.dll`, `hellforge-mod.json`; id `goose.true-first-person`, version `0.6.0-view.1`, game build 25480438, SDK 16). It was built on 2026-11-16 and **never loaded**: nothing here has run in the game or under Wine.

What it is: F11 toggles the game's own first-person camera through the translated retail units. The per-update order is retail's (`src/tfp_runtime.c`), with the backend, the controller, the camera / FOV / near / blur adapters and their helpers bound for real. Every other module answers "off". No arm or weapon placement, sleeves, hand grips, motion, head hiding, effects, menu or editor.

Files: `src/mod_view.c` (entry, tick, F11, load checks), `src/wire_win.c` (Windows and HellForge: memory, clocks, locks, the four functions that sit in the dispatch slots, native calls), `src/wire_rt.c` (the runtime host bound to the typed units; no Windows), `src/wire_recovery.c`, `src/wire_off.c` (the off answers), `src/wire_log.c` (the session log). The donor `src/mod.c` and the 0.5.34 policy files are not compiled into this build.

## 1. Bound for real

| Runtime host pointers (`src/tfp_runtime_host.h`) | Bound to | Where |
|---|---|---|
| `backend_new`, `_begin_frame`, `_end_frame` | `tfp_ops_init` (with the pose stand-in, section 3), `tfp_ops_begin_frame`, `_end_frame` | `src/wire_rt.c:475-481` |
| `backend_snapshot`, `_player_snapshot`, `_pin_player_snapshot` | `tfp_ops_snapshot`, `_player_snapshot`, `_pin_player_snapshot`; the 19 fields runtime.lua reads; the table's ref is a copy of the snapshot kept for the frame | `src/wire_rt.c:52-105` |
| `backend_ptr`, `backend_read` | `tfp_ops_ptr`, `tfp_ops_read`; a non-number address fails the read (runtime.lua:490, 506) | `src/wire_rt.c:487-505` |
| `backend_keep_aim_view`, `_detach_seated_camera` | `tfp_ops_keep_aim_view` (the snapshot behind the table's ref), `tfp_ops_detach_seated_camera` | `src/wire_rt.c:507-518` |
| `backend_set_pin_first_person`, `_set_camera_update_token` | accepted and ignored (retail writes `false` / nil once; nothing in `TfpOps` reads them) | `src/wire_rt.c:519` |
| `backend_set_action_placement_revision`, getter | one counter in the binder | `src/wire_rt.c:520`, `:531` |
| getters `backend_frame_active`, `_frame_epoch`, `_placement_weapon_key`, `_presentation_pose_epoch` | the `TfpOps` fields | `src/wire_rt.c:521-530` |
| getters `controller_lease`, `backend_alignment_lease`, `_presentation_pose_lease` | the lease flattened to the LEASE table (`tfp_wire_lease_table`); the same lease always has the same ref. `backend_warm_lease`: nil (warm start is Fird Person's) | `src/wire_rt.c:534-566` |
| `controller_new`, `_update`, `_stop`, `_suspend`, `_retry_cleanup`, `_shutdown`, `_is_clean`, `_is_enabled` | `tfp_controller_*` (`src/controller.c`); a refusal's text is the controller's label | `src/wire_rt.c:592-613` |
| `controller_result` | built from `TfpController`: `mode` 'pose_offset' once a session was started, `phase` as retail's text, `player_identity`, `cleanup_pending`, `wait_reason`; `blend` nil (no pose helper) | `src/wire_rt.c:573-595` |
| `controller_start` | `tfp_controller_start` for mode 'pose_offset'; any other mode is refused ("Only the arms/weapon mode is translated") | `src/wire_rt.c:614-621` |
| `controller_get_placement`, `_placement_target`, `_set_placement`, `_status` | written here from retail `controller.lua:102-147` on `tfp_ops_set_placement_defaults` / `tfp_ops_set_pose_placement`. **Not a vector-proven translation** (section 6) | `src/wire_rt.c:622-679` |
| `fov_*` (8) | `tfp_fov_lease_*` (`src/fov_adapter.c`) over `tfp_fov_*` (`src/fov_helper.c`) | `src/wire_rt.c:687-719` |
| `blur_*` (8) | `tfp_blur_lease_*` over `tfp_blur_*` | `src/wire_rt.c:693-746` |
| `near_*` (7) | `tfp_near_lease_*` over `tfp_near_*`; an update that raised (the clock) is a raise | `src/wire_rt.c:696-762` |
| `camera_new`, `_update`, `_prepare`, `_stop`, `_shutdown`, `_preflight`, `_is_clean`, `_result`, `_status`, `_set_handoff`, `_set_cut_hold` | `tfp_camera_adapter_*` (`src/camera_adapter.c`) over `tfp_camera_*` (`src/camera_helper.c`) | `src/wire_rt.c:765-812` |
| `recovery_*` (12) | `tfp_recovery_*` (`src/recovery.c`), policy names and phases as retail's texts; `snapshot` built from the rows; the failing policy's `failure_kind` / `failure_native_error` taken from its `result()` | `src/wire_recovery.c`; `src/wire_rt.c:813-838` |
| `transition_new`, `_native_ads`, `_result`, `_update`, `_reset`, `_action` | `tfp_transition_*` (`src/transition.c`); identities are the snapshot's keys; `action` is "none" (motion is off) | `src/wire_rt.c:840-894` |
| `transition_resume` | answers nil: only the motion module calls it | `src/wire_rt.c:895` |
| `diagnostics_new`, `_event`, `_record`, `_clear` | the session log: an event is a `Refused: policy …` line; the row's camera position and FOV feed the `View:` line. The 720-row ring of `diagnostics.lua` is not kept | `src/wire_rt.c:902-929` |
| `mod_log` | the mod's log | `src/wire_rt.c:932` |
| `clock_now` | the performance counter, seconds (section 6, differences) | `src/wire_rt.c:933`; `src/wire_win.c:175` |
| `mouse_button_index`, `mouse_button` | 'right' / 'middle' and their level from `GetAsyncKeyState`, 0 while the game is not the foreground window | `src/wire_rt.c:939-949`; `src/wire_win.c:338` |
| `global_get`, `global_set` | a flag | `src/wire_rt.c:950-951` |
| `menu_update` | takes the F11 press the tick saw and calls the runtime's `toggle_pose`, where retail's menu polls the key: the last call of the frame, inside the owned frame (runtime.lua:639) | `src/wire_rt.c:954-974`; key poll `src/mod_view.c:164-166`, `:181` |

Behind the units:

| Host | Bound to | Where |
|---|---|---|
| `TfpOpsHost` (backend): `module`, `read`, `write`, `query`, `call`, `log`, `wine` | `hf->module_base`, `tfp_win_read`, `hf->write`, `VirtualQuery`, the six native calls under SEH (allowed, enter, exit, body exists / trigger, camera unlink; the presentation switch refuses, it is open on this build), `hf->log`, ntdll's `wine_get_version` | `src/wire_win.c:283-327` |
| `TfpOpsHost`: `foreign_camera`, `camera_before_entry`, `placement_resolver`, `presentation_transition` | the runtime's entries of the same names (`action_placement` for the resolver) | `src/wire_rt.c:160-229` |
| `TfpBackendApi` (controller): `snapshot`, `player_snapshot`, `camera_only_weapon`, `prepare_pose`, `enter_pose`, `pose_status`, `release`, `set_pose_transition`, `presentation_transition`, `clock` | `tfp_ops_*` of the same names; a `TfpView` is a field-for-field copy of the snapshot with the two keys; each lease is one allocation that is never reused (a lease is compared by identity); the clock is the runtime's entry `clock` | `src/wire_rt.c:231-356` |
| camera adapter: `camera_config`, `snapshot`, `controller_result`, `controller_is_enabled`, `clock`, `log` | `tfp_ops_camera_config`, `tfp_ops_snapshot`, the controller, the runtime clock | `src/wire_rt.c:366-393` |
| FOV / blur / near io: `fov_config` / `blur_config`, `presentation_transition`, `controller_*`, `options`, `clock` | `tfp_ops_fov_config` with the held lease, the runtime's `presentation_transition`, the controller, the runtime's `near_options` (0 / 0 without a menu), the runtime clock | `src/wire_rt.c:395-472` |
| the four helper hosts: `read`, `readable_span`, `writable_slot`, `writable_request_field`, `executable`, `cas`, `cas32`, `write`, `pin`, clocks, locks | retail's own rules, line for line (`native_memory.h`, `native_ads_camera.cpp:139-178`, `:228-232`, `:432-445`, `native_ads_fov.cpp:355-361`) | `src/wire_win.c:56-248`, `:344-375` |

All bindings use the snapshot of `backend_ops`; `src/snapshot.c` is not in this build.

## 2. The dispatch slots

The helpers decide and order the swaps exactly as retail's DLLs (their C is retail's and proven call by call). This build adds no hook of its own and writes no code byte.

| Helper | Slot | Function it must hold (pin) | Hook | Caller it acts on |
|---|---|---|---|---|
| camera | CameraAPI `+0x20` (`*(*(game+0x3326308)+0x20)+0x20`) | exe+0x30deb0 | `camera_hook`, `src/wire_win.c:261` | game+0x127a139 |
| fov | CameraAPI `+0x80` | exe+0x30d990 | `fov_hook`, `:266` | game+0x127a120 |
| near | CameraAPI `+0x68` | exe+0x30d960 | `near_hook`, `:271` | any game.dll caller setting 0.05 or (0.05, 1.0] on the owned camera |
| blur | weapon parameter table exe+0x27cc638 | exe+0x310050 | `blur_hook`, `:276` | game+0x100a80e |

- **Identity before any write.** `host_cas` (`src/wire_win.c:194`) refuses to install a hook unless the value the helper expects in the slot is the pinned function and its bytes, read through `hf->read_code`, equal the pin (`pinned_target`, `:182`). Otherwise nothing is written, the helper reports its "slot changed" error (11), and the log says `Refused: the <name> slot was not swapped: …`. The four pins were compared with the exe image of this build on 2026-11-16 (`r6_reload_exe_20261017.bin`, file offset = RVA): all equal; the table at exe+0x27cc638 holds exe+0x310050 in the image.
- **All reads of code go through `hf->read_code`** (`tfp_win_read`, `:56`: any address in an executable section of game.dll or the exe), so the helpers' and the backend's own code pins are judged on the bytes from before any HellForge hook.
- **Restored on exit.** F11 off runs retail's `stop_pose`: each helper's End clears `armed`, drains its hook through the policy lock and swaps the slot back only if it still holds its own hook.
- **Restored at session end.** `HF_EVENT_SESSION_END` (`src/mod_view.c:205`) puts back every slot that still holds one of this mod's hooks with one compare-and-swap each and nothing else: no lock, no helper state, no game call (it may run under the loader lock). There is no unload of a HellForge code mod; after a crash nothing runs, as in retail.
- **Threads.** The slot functions run on whatever thread the game calls the slot from. They touch only their arguments, two counters and the helper. The helpers' locks are real SRW locks (two per helper, `src/wire_win.c:240-248`), used exactly where retail uses them: a hook that matches takes the policy lock, every export takes the control lock, End drains through the policy lock. No log call, no engine call and no tick state is touched from a hook; the tick reports the first call of each hook afterwards.

## 3. Stubbed off, and with which answer

`src/wire_off.c`, lists in `src/wire_off.h`. "Retail off path" says whether retail's runtime itself has a state in which it gets this answer.

| Module | Answer | Retail off path? |
|---|---|---|
| **Pose (editor) helper**, behind the backend | A stand-in `TfpOpsPoseHelper` (`src/wire_rt.c:107-151`): prepare and begin accept at once, statistics say armed, not expired, no error, weight 1 and blend 1, every placement / motion / transition call accepts and does nothing, finish accepts. The lease therefore exists exactly as in retail (the FOV, blur and near leases bind to it), with no `depth_stats` / `input_stats` function, so no zoom identity | **No.** Retail cannot enter without its pose helper. This answer keeps the controller's session stable: it never sees an unarmed or failing helper. What is lost: the weapon and arms sit where the game puts them, there is no draw animation, no entry rotation guard, no look-gain normalisation |
| Arm pieces, grip hand | `arm_pieces_new`, `grip_hand_new` are NULL: the modules do not exist | **Yes**: runtime.lua:698-706, "a missing or failing sleeve module never stops the first-person view" |
| Menu | `menu_new` true; `update` is the F11 poll (section 1); `shutdown` true; `native_ads` false; `ads_mode` nil; the four option methods (`fird_person_arms`, `show_torso`, `near_options`, `motion_options`) NULL | **Yes** for the options: the runtime tests each method and falls back to arms wanted, torso hidden, near clip 0 / 0, motion defaults. `native_ads` false is the option switched off |
| Editor (F8) | `is_active` false, `update` nothing, `cancel` / `shutdown` true, `request_open` false "The placement editor is not part of the view build", `status` nil | **Yes**: the editor closed |
| Motion | `update` and `reset` accept and do nothing; `result` an empty table (no action, no displacement) | **No** module-level off (retail's `motion_enabled=false` still runs the module). Nothing is published to the pose helper either way, since there is none |
| Head visibility, model shadows | `update` / `stop` / `shutdown` true, `is_clean` true, `result` `{phase='idle'}`, `sdk_handles` NULL | **No.** Retail always hides the head while the head camera or the rig view is active. Here the body stays as the game draws it (section 6, risk 2) |
| Movement | as head | **No** off path; this is what the runtime sees from the policy while it is idle and clean |
| Depth of field, vignette | `update` true, `result` `{owned=false}`, `is_clean` true, `status` "off" | **No** off path. `owned=false` is what lets the runtime's idle shortcut close (runtime.lua:638) |
| Camera sharing (Photo Mode) | `update` / `prepare` / `stop` / `shutdown` true, `foreign` false, `is_clean` true | **Yes**: no other camera mod holding the view. Photo Mode is **not detected**: do not open it while the view is on |
| Native ADS option | `update` / `stop` / `shutdown` true, `is_clean` true | **Yes**: the option off |
| Aim markers | `update` / `stop` / `shutdown` true, `is_clean` true, no fault; `target` / `setting` nil; `toggle` / `set_setting` false "Aiming markers are not part of the view build" | **Yes** in effect: the module idle with nothing hidden |
| Actions (placement profiles) | `resolve` / `default_placement` answer retail's shipped default placement `{x=.15, y=0, z=-.12}`; `saved` nil; `weapon_info` nil; no observed actions | **Yes**: an install with no saved placements |
| Settings (`mod.config`, `mod.set`) | an empty table; every field nil; `mod_set` NULL | **Yes**: a fresh install; a missing `mod.set` is "Placement settings storage unavailable" |
| Profile switches | `body_sleeves` false, `grip_hand` false (never read: the modules are nil), `seated_head` true (retail's value; only the stubbed head asks) | n/a |
| Head positioner | `is_enabled` false, `is_clean` true, the rest nil / refused | **Yes**: only the editor's parked camera mode and Fird Person reach it |
| Fird Person, ready stance, held fire, capture gate (57 pointers, 12 fields) | `fird_module_native_animation_default` false and `render_pose_verified` false, so `start_fird` ends in retail's own "Fird Person render placement is unavailable" before anything is built; the rest refuse with "Fird Person is not part of the view build" | **Yes**: parked in retail (only the debug api reaches `start_fird`) |
| Camera debug calls (`observe_parent`, `profile`, `*_stats`, `free_aim`) | nil; `free_aim` false "Retired in the camera helper" | **Yes**: retired in the helper |
| `backend_alignment_context` (rig and weapon position of the diagnostics row) | nil: runtime.lua:496 raises inside its own pcall and the fields stay nil | Offsets `+0x80`, `+0x88`, node 5 are retail's and not established on this build: refused rather than guessed |
| `backend_prepare_pose` (debug `preflight`), `backend_pose_status` (parked capture), `helper_depth_stats`, `helper_input_stats` | false "Preflight is not part of the view build"; nil; never called (the lease has no such function) | debug only |

**Proof** (`tests/run-vectors-view-off.sh`, `tests/wire_off_test.c`, `tools/retail_ref/gen_view_off.py`): the unmodified retail `runtime.lua` (1d0d453) is run under LuaJIT through one session of the view build with scripted collaborators whose stubbed half answers what `wire_off.c` answers: enable, idle, F11 from inside `menu:update()`, the rig view, aim in and out with the mouse edges, a weapon swap, a seat, 27 debug entries, F11 off, idle, disable (55 operations, 2,031 calls into the collaborators). The C runtime order replayed over the same session gives retail's answers, traces and state character for character, and on each of the **1,137** calls into a stubbed module `wire_off.c` is asked too and answers what the collaborator answered retail. A call into any collaborator that is neither wired nor compared fails the test (0 such calls).

## 4. What refuses, and why

| Refusal (log line starts with `Refused:`) | When |
|---|---|
| `this build … is for game build 25480438` | another game build: the mod does not load |
| `goose.first-person-controller is loaded …` | that mod is in the session and loaded (`hf->mods`, `src/mod_view.c:54`). It hooks game.dll+0x12b2ae0 (`controller_exit`), which this mod pins. Nothing is hooked, the mod stays off for the session |
| `another mod hooks a function this mod pins …` | any of the 39 pins reads differently now than before HellForge's hooks (`src/mod_view.c:81`): catches the same conflict when the mod list does not |
| `a code pin of build 25480438 does not match this game` | a pin differs (read with `read_code`); `Pins: … first mismatch <name>` names it |
| `game.dll or helldivers2.exe is not the image of build 25480438` | PE stamp or size differs |
| `F11 before the game's code pages could be read …` | F11 while some pinned pages are still unreadable (before a player exists). The mode starts by itself once they read |
| `the mode could not start: …` | `on_enable` raised |
| `F11 enter: <text>` / `F11 exit: <text>` | the runtime's `toggle_pose` refused; the text is retail's (for example `Start refused: …`, `Head-camera cleanup is still pending`) |
| `the <camera/fov/near/blur> slot was not swapped: …` | section 2 |
| `fov:` / `blur:` / `camera:` / `near:` `<text>` and `Cleared: <name>` | that lease's `error_message` changed (the helper's error number is in the text) |
| `policy <name> failed (policy_failure): …` | the runtime's recovery recorded a failed update |
| `the frame raised: <text>` | `on_update` raised (retail's facade would switch the mode off here; this build logs once and keeps ticking, section 6) |

Features that cannot work on this build and say so instead of guessing: the presentation switch (`tfp_ops_invoke_presentation`, open constants; nothing on this path calls it), the rig / weapon diagnostics fields (above).

## 5. Session log

`mod-goose.true-first-person.log` in the session folder under the install's `logs/sessions/`. Besides the lines of section 4:

- `True First Person 0.6.0-view.1 …`, `Tick: game-thread callback on thread N`, `Image: …`, `Pins: 39 ok …`, `Reads: N microseconds per guarded 8-byte read`, `Ready. F11 toggles the view. Not in this build: …`, retail's own `Idle. F11 toggles True First Person; F8 edits arms and weapon placement.` (F8 does nothing here).
- `Status: <the controller's label>` on every change (retail logs the same label).
- `State: enter requested (F11)`, `State: entered the native view request=0x… kind=6`, `State: aim in`, `State: aim out`, `State: aim kept (N)`, `State: swap (weapon identity changed)`, `State: seated camera detached (N)`, `State: left the native view: <why>`, `State: exit requested (F11)`, `State: exit complete`.
- The backend's own lines: `Native entry: aim before=… camera=6 …`, `Native entry error: …`.
- `Hook: <name> first fired on thread N, called from game+0x…` once per hook.
- While the mode is on, once a second: `View: t=… frames=N gaps=N on=1 phase=running kind=6 ads=0 safe=1 eye_step=… eye_jump=… fov=… fov_native=… fov_jump=… near=… calls=cam:N,fov:N,near:N,blur:N`. `eye_jump` is the largest part of a frame's camera move that the frame before does not explain (walking gives millimetres, a cut gives its size, in metres); `fov_jump` the same for the applied field of view (radians); `gaps` frames whose camera position could not be read; `calls` total hook calls so far.
- `Session end: N dispatch slot(s) put back`.

## 6. Not proven until it runs in the game, and differences from retail

Differences made on purpose (none changes a number of the view path):

1. **Frame clock.** Retail's is `Application.time_since_launch`; here `QueryPerformanceCounter` in seconds. Both are monotonic seconds; the runtime only takes differences and refuses a clock that goes back. Reason: the engine's script functions need the donor's Lua bridge (`src/engine.c`, hooks on `lua_pcall`), which this first build leaves out.
2. **Mouse buttons** for the aim-release keeper come from Windows, not `stingray.Mouse`. Same two buttons, same levels.
3. **No facade.** Retail's `first_person.lua` switches the mode off when `on_update` raises. Here the raise is logged once and the next tick runs again.
4. **Pins read through `read_code`.** Retail refuses a pin another mod changed; here such a pin still compares equal to its original bytes, and the conflict is refused at load instead (section 4).
5. **No pose helper** (section 3). Decision A' of `docs/wiring/WIRING-INVENTORY.md` (the input thunks of `native_ads_input.S`) does not arise yet: `tfp_pose_begin_input` is not reached.

Unproven, ranked by how likely it is to spoil the first session:

1. **The live path of the binder was never executed.** The host tests run the whole binder only without a game (every read fails). Entry, the lease, the lens leases binding to it, release and re-entry run for the first time in the game. The controller's placement methods (`src/wire_rt.c:622-679`) are a direct reading of 35 Lua lines without LuaJIT vectors; only the bounds rule is tested.
2. **No pose helper, no head hiding.** The rig's weapon and arms are where the game's own first-person evaluation puts them (most likely the sights pose, not the hip). In the head-camera states (waiting, swaps, seats) the player's own head and body are drawn and may cover the view. Expected in this build, not a wiring fault.
3. **Where HellForge's tick falls in the frame** (before the game's update; retail runs inside the script update) and **which thread calls each slot**: both are logged (`Tick:`, `Hook:`). A one-frame lag of the eye or FOV would show as `eye_jump` / `fov_jump` on entry and exit.
4. **Cost of `hf->read`.** The helpers read tens of times per hook call. `Reads:` logs the cost; several microseconds per read would cost frames.
5. **`_Thread_local` in a DLL built by zig for `x86_64-windows-gnu`** (camera, fov, blur helpers) and **`setjmp` / `longjmp`** in `tfp_runtime.c`: compiled, never run on Windows. A fault here would show on the first F11 or the first refused call.
6. **Camera helper open points** (`docs/translation/CAMERA-HELPER-TRANSLATION.md` section 5): the context basis at `+0x0C` (decides "pose invalid" and the eye's forward / up) and the bridge scene test (scene 3 only). Symptom: `Refused: camera: … 14` or `… 18`.
7. **MinGW's math functions** against retail's (last bits of `expf`, `tanf`): eases could differ in the last bit.
8. **Session end under the loader lock**: only compare-and-swaps, but untested.
9. The First Person Controller's zip in the mods folder: detection needs it listed as loaded; the byte comparison (hooked pins) is the second line of defence.

## 7. In-game checklist

Install only on the user's word: game and HellForge closed, First Person Controller and the older `true-first-person` switched off, copy `dist/true-first-person-view` into the install's `mods` folder (it has the same id as the 0.5.34 folder there: replace or disable that one). Then:

| # | Do | Expect in the log |
|---|---|---|
| 1 | Start a session, reach the ship | banner, `Tick: …`, `Image: …`, perhaps `Waiting: …`, then `Pins: 39 ok, 0 mismatched, 0 unreadable`, `Reads: …`, `Idle. F11 toggles …`, `Ready. …`. No `Refused:` |
| 2 | With the Controller left on (once, to see the refusal) | `Refused: goose.first-person-controller is loaded …`; F11 does nothing |
| 3 | In a mission, weapon in hand, standing: F11 | `State: enter requested (F11)`, `Native entry: aim before=false, after=…; camera=6; …`, `Status: First person enabled until manually stopped`, `State: entered the native view request=0x… kind=6`, `Hook: camera first fired … game+0x127a139`, `Hook: fov … game+0x127a120`, then a `View:` line each second with `phase=running kind=6` |
| 4 | Walk and turn for ten seconds | `View:` lines with `gaps=0`, `eye_jump` of a few millimetres, `fov_jump=0.0000`, rising `calls=` |
| 5 | Hold aim (right mouse), release | `State: aim in`, later `State: aim out` and `State: aim kept (1)`; no `State: left the native view` in between; `fov_jump` small in those seconds (the 180 ms ease) |
| 6 | Aim with a scoped weapon | `Hook: blur first fired … game+0x100a80e`; `Hook: near …` at some point; `near=` changes on the `View:` line |
| 7 | Swap weapons | `State: left the native view: …`, `Status: First person enabled; waiting: …`, `View:` with `phase=waiting kind=4`, then `Native entry: …`, `State: entered the native view …`, `State: swap (weapon identity changed)`, `Status: First person enabled; native camera reacquired`. The `eye_jump` of those seconds is the number to judge the eye hold by |
| 8 | Stim, grenade, dive | as 7 without the swap line; the view returns by itself |
| 9 | F11 | `State: exit requested (F11)`, `Status: Stopped: manual stop`, `State: exit complete`; the `View:` lines stop; third person as before |
| 10 | F11 on, then quit to the ship or die | `State: left the native view: …`; after the respawn `State: entered the native view …` again, or a `Refused:` that says what failed |
| 11 | Quit the game with the view on | `Session end: N dispatch slot(s) put back` (N up to 4), if the game delivers the event |

A session is bad when: any `Refused:` other than #2 and a short-lived `F11 … cleanup is still pending`; `gaps` above 0 in the rig view; `eye_jump` above a few centimetres outside entry, exit and swaps; a `Hook:` caller other than the three listed; the view not coming back after #7 / #8.
