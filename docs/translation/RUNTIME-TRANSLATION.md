# Runtime order: runtime.lua in C

Translation of retail `src/first_person/runtime.lua` (1,228 lines, True First Person 0.5.41-smooth.66, kit commit `1d0d453`, SHA-256 `51099af4…`): the file that builds every object of the mode, owns the per-update order, the recovery wrapper around the presentation policies, the diagnostics record, the aim-release and seated one-shots, and hands closures to every collaborator, the menu and the debug api. The Lua file is the authority. The C is checked by replaying the retail file's own answers. The recipe is the one of `docs/translation/MOTION-RUNTIME-TRANSLATION.md`.

## Files

| File | What |
| --- | --- |
| `src/tfp_runtime.h`, `src/tfp_runtime.c` | The translation. Includes no other module's header. |
| `src/tfp_runtime_host.h` | The host struct: one function pointer per collaborator method runtime.lua calls (257, 17 of them optional), the fields it reads (34), the table kinds, the object and entry lists. Generated from the spec (`gen_runtime.py --host-header`), checked in. |
| `tools/retail_ref/runtime_spec.py` | What runtime.lua reaches outside itself, written once. The host struct, the Lua fakes and the C fakes all come from it. |
| `tools/retail_ref/run_runtime.lua` | Loads the retail file unmodified under `/usr/bin/luajit`, every required module a scripted fake, and replays the sessions. |
| `tools/retail_ref/runtime_sessions.py` | The sessions. |
| `tools/retail_ref/gen_runtime.py` | Runs them, adds the copies with one failing call, emits the vectors and the C fakes, measures line coverage. Refuses any retail commit but `1d0d453` and any other file than the frozen one. |
| `tests/vectors/runtime_vectors.h`, `tests/vectors/runtime_fakes.h` | @OPS@ operations with retail's answers (@SIZE@ MB); the generated C fakes. |
| `tests/tfp_runtime_test.c`, `tests/run-vectors-runtime.sh` | Replays the vectors against the C. `tests/run-native.sh` picks the script up by its name. |
| `tools/retail_ref/mutate_runtime.py` | The mutation check. |

**Names.** The task named `src/runtime.c|h` and `tests/runtime_test.c`. Those three files already exist: they are the 0.5.34 port's plumbing (`FpRuntime`, used by `src/mod.c`, `src/ui.h` and `tests/build-windows.sh`). They were not touched; the translation is `tfp_runtime.*` with the prefix `tfp_rt_` / `TfpRt`. The test script has the asked name.

Regenerate (only when the retail reference is deliberately moved):

    python3 tools/retail_ref/gen_runtime.py --retail "$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit" --out tests/vectors

About 40 s. Regeneration is byte-identical run to run and between `luajit` and `luajit -joff`.

## Interface

`tfp_rt_init(rt, host)` is the loaded chunk: runtime.lua's module-level variables at their start values. Everything else goes through one call:

    TfpRtStatus tfp_rt_call(TfpRt *rt, TfpRtEntry entry, const TfpRtArgs *args, TfpRtValue *ret);

`TfpRtEntry` lists the 109 functions runtime.lua hands out (`TFP_RT_ENTRY_LIST` in `tfp_runtime_host.h`, with each one's arguments and number of answers):

- the four of the module table: `on_enable`, `on_update`, `on_disable`, `on_cleanup_poll`;
- the closures `on_enable` gives to its collaborators: `log`, `clock`, the backend's `presentation_transition` / `presentation_action` / `presentation_resume` / `camera_before_entry` / `action_placement` (also `placement_resolver`) / `action_mapping` / `foreign_camera`, the head's `fallback_ready` / `pinned_ready` / `full_body` / `hide_torso` / `show_torso` / `seated_head`, the depth of field's `active_view` / `dof_fallback_ready`, `near_options`, `native_aim_enabled`, the aiming markers' five, the head positioner's three, the placement controller's seven (`pc_*`), the placement store (`store_*`), `motion_options`;
- the menu's callbacks and the debug api (`start_pose`, `toggle_pose`, `stop_pose`, `start_native`, `status`, `preflight`, …);
- the closures of the parked parts, created by `start_fird`: ready stance (`held_fire_authorized`, `native_ready_gate`, …), Fird Person (`fird_*`, `render_fird_pose`) and its capture (`fe_*`).

`tfp_rt_call` answers `TFP_RT_OK` (answers in `ret`), `TFP_RT_RAISED` (the raised value in `rt->error`; Lua would have raised into its caller) or `TFP_RT_ABSENT` (runtime.lua has not created that function: `on_enable` has not got that far, or it is an api function and `api` is nil after shutdown). It may be called from inside a host call, as the Lua collaborators call their callbacks.

The Lua file passes many values on without looking at them and compares others in ways that depend on their type (`==true`, `~=true`, truthiness, `~=false` are all used, deliberately), so what crosses the host boundary is a small tagged value, `TfpRtValue` (nil, false, true, number, text, ref). A Lua table whose fields the file reads is a `TfpRtTable`: `present`, its identity `ref`, and the fields of its kind (`TFP_RT_SNAP_*`, `TFP_RT_STATE_*`, …; 22 kinds in `tfp_runtime_host.h`, and 8 more for the tables runtime.lua builds). A table the file builds and hands over (the diagnostics row with its 71 fields, a recovery proof, …) is an array of values with the same kind of index.

A host call answers `true` when the Lua method returned and `false` when it raised, with the text in `host->raised`. An optional pointer may be `NULL`: those are exactly the methods runtime.lua tests for before calling (`camera.is_pinned`, `controller.suspend`, `menu.near_options`, …; marked in the header).

## Carried over from Lua

| Lua | C |
| --- | --- |
| Module-level variables that hold an object or nil (`controller`, `menu`, …, 33 of them) | `rt->has[TFP_RT_O_*]`. The objects themselves are behind the host. `shutdown` clears exactly the ones retail clears (not `arm_pieces`, `grip_hand`, `Actions`, `probe_update`). |
| Using a nil one (`menu:update()` with `menu` nil after a failed `on_enable`) | The text LuaJIT raises, with the line: `src/first_person/runtime.lua:574: attempt to index upvalue 'menu' (a nil value)`. For `attempt(target, …)` with a nil target it is line 24, `local 'target'`. These texts reach the log and the diagnostics row, so they are compared. |
| `error` / `pcall` | `raise_value` / `rt_pcall`: one `setjmp` frame per protected call (one function holds the `setjmp`). `pcall` of a host method is just its `bool`. |
| `attempt(target, name, ...)` | `ATT` / `OATT`: done and why; a raise is `false` and its text. |
| `assert(v, message)` | LuaJIT puts `chunk:line: ` before a text or a number; any other message is raised as it is (a nil stays nil). `fe_capture`'s assert is a tail call and gets no line. |
| The closures of `on_enable` and their shared locals (`edit_mode`, `probe_deadline`, the clock's `previous`, the store's fields, `candidate` / `hip_capture`) | Fields of `TfpRt`; `rt->made[]` says which closures exist. A new `on_enable` that passes its two asserts starts a new generation: all `made` flags are cleared and set again as it goes, set before the constructor that receives the closure is called, as the constructor could already call it. |
| `owned_*` locals (the objects of this generation, still used by the menu and api closures after shutdown) | The same host pointers, called without a nil check. |
| `ready_context` (a new table each frame, compared by identity) | `TfpRtContext` with a serial that never repeats, and `rt->has_context`. |
| A table kept across frames (`diagnostics_look`, `candidate`, `hip_capture`, `recovery_owner`, `owner_draining`, `fp_dof_player`) | A copy with its texts copied (64 bytes each). |
| `for … in pairs({fov=fov, …})` (`:352`), `pairs(recovery:snapshot().policies)` (`:601`), `pairs(before)` (`:810`) | A fixed order. See "pairs" under open issues. |
| `tostring(number)` | `%.14g`. |
| `a .. b` with a value that is neither text nor number | LuaJIT's error with the variable's name where the Lua has one (`:288`, `:1089`, `:1091`). |
| The f32 decoding of `:462-468` and `:481-486`, `in_camera`, `math.deg(math.acos(math.max(-1, math.min(1, dot))))` | Written in retail's order; build with `-ffp-contract=off`. `math.deg(x)` is `x * 57.29577951308232`. |
| `x^2` (`:997`, `:999`) | `x * x`, as in `motion_runtime.c`; the generator preloads the same exact-square `pow` (see `docs/translation/MOTION-RUNTIME-TRANSLATION.md`). |
| `rawget` / `rawset` of `_G.NativeADSCameraTest` | `global_get` (0 unset, 1 this runtime's api, 2 another value) and `global_set`. |
| Writes to backend fields (`pin_first_person=false`, `camera_update_token=nil`, `action_placement_revision=n+1`, `fird_camera_offset=…`) | Host calls `backend_set_*`, in the trace. Assigning the closures to the backend is not a call: the binder takes them from the entry list. |
| `hip_capture.aim = {…}` and `token.capture = hip_capture` (`:1011-1013`) | Host calls `fird_capture_set_aim(hip, capture)` and `capture_gate_token_set_capture(token, hip)`; `ref_field(token, 'capture')` reads it back. |

## What C does not express

- **Tables kept by reference.** The C keeps copies. A collaborator that changed a table after handing it over (the look statistics, a capture) would show in retail and not here.
- **Values of other types** than nil, boolean, number, string, table: a function or userdata as an answer. `tostring` of a table gives `table: <address>`, which cannot match LuaJIT's.
- **Numeric strings in arithmetic.** Lua adds `'5' + 1`; the C treats a text as not a number at `_ptr` results, the revision, the clock.
- **Texts over the buffers**: 64 bytes for a kept identity, 512 for the remembered diagnostics fault, 1,024 for one built text, 32 KB for all texts of one outermost call (then `(text arena exhausted)` replaces the next one).
- **An old generation's closures.** After a later `on_enable`, Lua's old closures still exist with their own locals; the C has one generation. Retail's collaborators are rebuilt by the same `on_enable`, so nobody holds an old one.
- **`mouse.button_id`** as the fallback of `mouse.button_index` is the same host pointer.
- **A log function that raises** propagates, as in retail (`mod.log` is not protected); the test does not script it for the last log line of `on_enable` differently from any other call.

## Proof

- **@OPS@ operations in @SESSIONS@ sessions**; @RAISED@ end in a raised error. Per operation one text is compared, character for character: the called function's answer (or the raised value, or `absent`), the ordered trace of every call into a fake with its arguments, log lines among them, and runtime.lua's own state (which objects are nil, `stopping`, `idle_clean`, `handoff_ready`, `recovery_enabled`, `fp_dof_player`, the two ticks, the capture phase, the diagnostics fault and look cache, the recovery owner and the draining owner, the mouse buttons and their held flags, `seated_view_held`, the clock's `previous`, `edit_mode`, `probe_deadline`, the capture candidate, the debug global). **@CALLS@ calls into the fakes** are in the traces. A number is decimal when whole and below 2^31, else the sixteen hex digits of the double: **@DOUBLES@ non-whole doubles** compared by their bits.
- The Lua state is read out of the unmodified file's upvalues with `debug.getupvalue`.
- The fakes are the same on both sides: @KNOBS@ knobs (what each method answers, each field of each table, which optional methods exist), set by the script before each operation; a call can be told to raise, to refuse (`false, 'no'` / nil for a table) or to answer nil; a fake can be told to call one of runtime.lua's functions back before it answers (`{name:answer}` in the trace).
- Sessions (`runtime_sessions.py`):
  - **@HAND@ hand-written** (@HANDOPS@ operations): enable / idle / disable / cleanup poll; enable refused (global owned, clock missing, first placement refused, arm modules missing); toggle on, play, toggle off; aim, sights, the zoom identity with every refusal of its eight conditions, mouse edges (right and middle button, each index answer); the memory reads of the record (short, missing and odd answers at each read, NaN / infinite / denormal floats, every form of the controller bytes); view states (camera kinds 4 / 6 / others, safe or not, fallback, pinned, each condition failing) with 24 callbacks per frame; weapon swap and seated interaction; arm pieces and grip hand with each of their conditions; the idle shortcut, a foreign camera, the editor; recovery (each action of the ledger, proof, failure rows core / fatal / with and without `suspend`, for each of the five policies, a blocked view); death and respawn (the owner changes, draining, each thing that keeps it draining); the clock backwards, invalid in seven ways, moving inside a frame; a frame already owned; every one of the 109 functions before enable, while idle, while running, after disable and after a second enable; settings changed mid-session (28 changes x 20 functions); the placement store in both modes; every refusal of start, stop, preflight, shutdown; natural Fird Person with the ready stance and held fire; render-driven Fird Person with the whole capture sequence and each way two captures can differ.
  - **@SWEEP@ short sessions swept for failing calls**: each is replayed once per call its marked operations make into the fakes and per way of failing (raise, refuse, nil), followed by one more frame: **@FAULT@ sessions**, so every call site of those paths fails in each way once, and the frame after it is compared too. `on_enable` is swept with a longer tail (frame, disable, cleanup poll, enable, frame), which walks every partly built state.
  - **6 seeded random sessions** (500 to 1,200 steps, @RANDOMOPS@ operations): random knob changes (odd answers last a few operations), frames at 30 to 144 a second with random callbacks, every function at random, shutdowns and re-enables, Fird Person starts and capture samples, and in four of them random failing calls.
- **Differences found between Lua and C while making it pass**, all in the C or the runner, none left: LuaJIT's `assert` prefixes a text message with chunk and line (and does not in a tail call, and raises a nil message as nil); the second `_ptr` of the rig read is handed to `_read` unchecked (`:490`, `:506`), only the first is used in arithmetic; `api.status` on a nil result fails on line 1157, not 1156; the runner lost knob settings behind a nil in a Lua list.
- **Coverage**: 1,027 executable lines of `runtime.lua` (`luac5.1 -l`), **1,023 hit** under a line hook. Not hit: 605 (an `end`: luac5.1 puts a jump there, LuaJIT's bytecode has no instruction on that line; 602-604 inside it are hit), 643-644 and 1097 (constant fields of two table constructors, `live_lua_api = 1, name = …, author = …` and `seamless_version = …`, for which LuaJIT raises no line event; both tables are built, the second one's field is read and compared by the `seamless_version` entry).
- **Mutation check**: @MUTATION@
- Compilers: `cc -std=c11 -Wall -Wextra -Werror`; the vectors pass with gcc at `-O0`, `-O1` with the address and undefined-behaviour sanitizers (`-fno-sanitize-recover=all`), `-O2`, `-O3` and with clang `-O2`. `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror` compiles the file; the vectors were run natively only (no Wine). The file uses `, ##__VA_ARGS__` (GNU), so `-Wpedantic` complains.

## Binding table

Every pointer needs a thin adapter for the calling convention (the `TfpRtHost *`, results as `TfpRtValue` / `TfpRtTable`, a refusal's text as the second value, a raise as `false` + `host->raised`). The table says what each binds to and flags (**!**) where more than that is needed. "none" means this repository has no C for it; such a pointer gets a stub that answers nil / true as noted, or stays `NULL` where it is optional.

### backend (`src/backend_ops.h`, still being finished)

| Host pointer | Binds to | Mismatch |
| --- | --- | --- |
| `backend_new` | `tfp_ops_init(b, host, alignment, pose)` | The `TfpOpsHost` callbacks `foreign_camera`, `camera_before_entry`, `placement_resolver` bind back to the entries of the same names. **!** `placement_resolver` wants a `TfpOpsPlacement`; `action_placement` answers the resolver's value as a ref, so the adapter calls `tfp_action_resolve` itself. |
| `backend_snapshot`, `backend_player_snapshot`, `backend_pin_player_snapshot` | `tfp_ops_snapshot`, `tfp_ops_player_snapshot`, `tfp_ops_pin_player_snapshot` `(b, &out, &why)` | **!** `TfpOpsSnapshot` to the 19 `SNAP` fields; identities are 64-bit keys there and texts here (render the key as text, or keep the string). `ref` must be the snapshot the other modules get (`arm_pieces_update`, `transition_update` and `keep_aim_view` receive it back). `TfpOpsResult` NIL is `present = false`. |
| `backend_begin_frame`, `backend_end_frame` | `tfp_ops_begin_frame`, `tfp_ops_end_frame` | They are `void`: never raise. |
| `backend_ptr` | `tfp_ops_ptr(b, at, &out, &why)` | **!** address `double` here (Lua arithmetic), `uint64_t` there. |
| `backend_read` | `tfp_ops_read(b, at, out, size, &why)` | **!** the address is a value: retail hands the result of `_ptr` to `_read` unchecked (`:490`, `:506`); anything but a number must fail the read. Bytes come back as a text of `size` bytes. |
| `backend_alignment_context` | `tfp_ops_alignment_context(b, unit, has_owner, owner, has_avatar_index, avatar_index, …)` | **!** `ACTX` values to those typed arguments; the answer's `object` as a number. |
| `backend_keep_aim_view`, `backend_detach_seated_camera` | `tfp_ops_keep_aim_view(b, s, &why)`, `tfp_ops_detach_seated_camera(b, &why)` | Results are ignored by runtime.lua (`pcall`). |
| `backend_prepare_pose` | `tfp_ops_prepare_pose(b, expected, &token, &why)` | **!** extra token out; used only by `api.preflight`. |
| `backend_pose_status` | `tfp_ops_pose_status(b, lease, &stats, &why)` | Only the parked capture reads it, as an opaque value. |
| `backend_set_pin_first_person`, `backend_set_camera_update_token` | none found | **!** no such field in `TfpOps`; retail writes `false` / nil once in `on_enable`. Stub. |
| `backend_set_action_placement_revision`, getter `backend_action_placement_revision` | none found in `TfpOps` | **!** `motion_runtime.h`'s host reads `backend.action_placement_revision`; the binder needs one shared counter. |
| `backend_set_fird_camera_offset` | `TfpOps.fird_camera_offset[3]`, `has_fird_camera_offset` | **!** a ref here (the head positioner's saved value). Parked. |
| getters `backend_frame_active`, `backend_frame_epoch`, `backend_placement_weapon_key`, `backend_presentation_pose_epoch` | the `TfpOps` fields of the same names | `has_*` flags there are nil here. |
| getters `backend_warm_lease`, `backend_alignment_lease`, `backend_presentation_pose_lease`, `controller_lease` | `TfpOps.warm_lease`, `.alignment_lease`, `.presentation_pose_lease`, `TfpController.lease` | **!** `LEASE` is flat: `origin` (+ identity, player, body, world), `alignment` (+ ended, kind, helper, depth_stats, input_stats, handle). `controller.h`'s `TfpLease` has none of these; they come from `TfpOpsLease`. `ref` must be equal for the same lease and different for another. |
| getters `backend_pin_first_person`, `backend_camera_ready` | none / `tfp_ops_camera_ready` exists | `pin_first_person` is always false in retail (answer false). `camera_ready` is only tested for being a function (parked `fird_warm`): answer nil. |
| `helper_depth_stats`, `helper_input_stats` | `tfp_pose_adapter_depth_stats(a, handle, &view, &why)`, `tfp_pose_adapter_input_stats` | **!** called with the lease; the adapter finds the lease's helper and handle. `DEPTH` needs `supported`, `armed`, `zoom_index`. |

### controller (`src/controller.h`)

| Host pointer | Binds to | Mismatch |
| --- | --- | --- |
| `controller_new` | `tfp_controller_init(c, backend_api)` | `TfpBackendApi.presentation_transition` binds to the entry; `.clock` to the entry `clock`. |
| `controller_update`, `controller_stop`, `controller_suspend`, `controller_retry_cleanup`, `controller_shutdown` | `tfp_controller_update`, `_stop(c, reason)`, `_suspend(c, reason)`, `_retry_cleanup`, `_shutdown` | They answer `bool` without a text: why is nil. `update`'s answer is ignored. |
| `controller_is_clean` | `tfp_controller_is_clean` (inline) | |
| `controller_is_enabled` | `TfpController.enabled` | **!** no function. |
| `controller_result` | none | **!** no `result()`: build `STATE` from the struct (`mode` 'pose_offset', `enabled`, `phase` as retail's text, `wait_reason`; `player_identity` from `initial`; `blend`, `cleanup_pending` are not in the struct). |
| `controller_start` | `tfp_controller_start(c)` | **!** no `(seconds, mode)`: the C has only the manual pose mode. `start_native` (mode 'native', debug api) has nothing to bind to. |
| `controller_get_placement`, `controller_set_placement`, `controller_placement_target`, `controller_status` | none | **!** the C controller has no placement and no status text (`label` is the closest). Needed by `on_enable` (`set_placement` must answer true or `on_enable` raises), `start_pose`, the diagnostics row and the editor. |
| `TFP_PHASE_*` | | **!** `controller.h` (`TfpPhase`) and `recovery.h` (`TfpRecoveryPhase`) both define `TFP_PHASE_WAITING`; one binder file cannot include both as they are. |

### the five managed policies and the other per-frame modules

| Host pointer | Binds to | Mismatch |
| --- | --- | --- |
| `fov_new`, `fov_update`, `fov_stop`, `fov_shutdown`, `fov_preflight`, `fov_is_clean`, `fov_result`, `fov_status` | `tfp_fov_lease_init`, `_update(l, &why)`, `_stop`, `_shutdown`, `_preflight`, `_is_clean`, `_result(l, &out)`, `_status(l, buf, size)` | `FOV` fields from `TfpFovResult`: `phase` as retail's text ('active'), `armed`, `last_native`, `last_applied`, `transition_epoch`. |
| `blur_*` (the same eight) | `tfp_blur_lease_*` | `preflight` answers `TfpBlurAnswer` (three-valued). |
| `camera_new`, `camera_update`, `camera_prepare`, `camera_stop`, `camera_shutdown`, `camera_preflight`, `camera_is_clean`, `camera_result`, `camera_status`, `camera_set_handoff`, `camera_set_cut_hold` | `tfp_camera_adapter_init`, `_update(a, &why)`, `_prepare`, `_stop`, `_shutdown`, `_preflight` (int), `_is_clean`, `_result`, `_status`, `_set_handoff(a, entry_ms, rotation_ms, &why)`, `_set_cut_hold(a, exit, eye, anchor, seated, &why)` | `why` is a `TfpCameraText`. **!** `set_handoff` / `set_cut_hold` take numbers / booleans, values here. `CAM` needs `phase`, `armed`, `error`, `head_distance`, `replaced`, `blend`, `transition_epoch`. |
| `camera_is_pinned`, `camera_look_stats`, `camera_observe_parent`, `camera_free_aim`, `camera_profile`, `camera_profile_stats`, `camera_kick_stats`, `camera_parent_stats`, `camera_handoff_stats`, `camera_free_aim_stats`, getter `camera_retired` | none in `camera_adapter.h` (`tfp_camera_handoff_stats` exists in `camera_helper.h`) | **!** `is_pinned` and `look_stats` are optional: `NULL` (the record then has no look fields and `pinned` is false). The rest are debug api: stubs. `retired`: nil. |
| `compat_new`, `compat_update`, `compat_foreign`, `compat_prepare`, `compat_stop`, `compat_shutdown`, `compat_is_clean`, `compat_result` | `tfp_camera_compat_init`, `_update(c, &why)`, `_foreign`, `_prepare`, `_stop`, `_shutdown`, `_is_clean`, `_result` | `foreign` answers `bool`; runtime.lua also tests `== false`, which a `bool` satisfies. |
| `model_shadows_new`, `head_new`, `head_update`, `head_stop`, `head_shutdown`, `head_is_clean`, `head_result` | `tfp_model_shadow_init`, `tfp_head_new(io, engine, models, build)`, `tfp_head_update(h, &why)`, `_stop`, `_shutdown`, `_is_clean`, `_result` | The `TfpHeadIo` callbacks `fallback_ready`, `pinned_ready`, `full_body`, `hide_torso`, `show_torso`, `seated_head` bind to the entries of the same names (`fallback_ready` is handed a snapshot there that the entry does not take). `phase` as text. |
| `head_sdk_handles` | `tfp_head_sdk_handles(h, &count, table)` | **!** retail's one table is a count and an array there; here one value that `arm_pieces_update` / `grip_hand_update` get back: the adapter owns a `TfpArmHandles` and passes its address as the ref. |
| `movement_new`, `movement_update`, `movement_stop`, `movement_shutdown`, `movement_is_clean`, `movement_result` | `tfp_movement_init`, `tfp_movement_update(m, skip, &why)`, `_stop`, `_shutdown`, `_is_clean`, `_result` | **!** `update` has an extra `skip` argument that runtime.lua does not pass. |
| `arm_pieces_new`, `arm_pieces_update`, `arm_pieces_stop`, `arm_pieces_is_clean`, `arm_pieces_result` | `tfp_arm_pieces_init(a, module, backend, engine)`, `tfp_arm_pieces_update(a, s, active, handles)`, `_stop`, `_is_clean`, `_result` | **!** `s` is a `TfpArmSnapshot`, built from the snapshot behind the ref. `new` answers a value: true. `stop` has no text. |
| `grip_hand_*` (the same five) | `tfp_grip_hand_init`, `_update`, `_stop`, `_is_clean`, `_result` | As arm pieces. |
| `dof_new`, `dof_update`, `dof_stop`, `dof_shutdown`, `dof_is_clean`, `dof_result`, `dof_status` | `tfp_dof_init(d, host, setting, vignette)`, `tfp_dof_update(d, compat, &why)`, `_stop`, `_shutdown`, `_is_clean`, `_result`, `_status` | `TfpDofHost` callbacks `active_view` / `fallback_ready` / `clock` bind to the entries `active_view`, `dof_fallback_ready`, `clock`. `update` answers `TfpDofAnswer`. `DOF.owned` must be a boolean: `idle_clean` needs `== false`. |
| `near_new`, `near_update`, `near_stop`, `near_shutdown`, `near_is_clean`, `near_result`, `near_status` | `tfp_near_lease_init`, `_update(l, &why)` (`TfpNearAnswer`), `_stop`, `_shutdown`, `_is_clean`, `_result`, `_status` | The lease's options come from the entry `near_options` (the menu's answer, else hip 0, aim 0). |
| `motion_new`, `motion_update`, `motion_reset`, `motion_capture_neutral`, `motion_result` | `tfp_motion_runtime_init`, `_update(rt, &reason)`, `_reset`, `_capture_neutral(rt, native_ads, &reason)`, `_result` | `update` answers `TFP_MOTION_RUNTIME_RAISED` for a raise: that is `false` + `raised` here (runtime.lua calls it unprotected). Its host's `options` binds to the entry `motion_options`; `presentation_*`, `action_placement`, `action_mapping` to the entries. `MOTION` needs `action` (text), `softened`, `dx`, `dy`, `dz`. |
| `native_aim_source`, `native_aim_new`, `native_aim_update`, `native_aim_stop`, `native_aim_shutdown`, `native_aim_is_clean`, `native_aim_result` | `tfp_native_aim_source_probe`, `tfp_native_aim_init(a, host)`, `_update(a, &why)`, `_stop`, `_shutdown`, `_is_clean`, `_result` | Its `enabled` callback binds to the entry `native_aim_enabled`. |
| `aim_markers_source`, `aim_markers_new`, `aim_markers_update`, `aim_markers_stop`, `aim_markers_shutdown`, `aim_markers_is_clean`, `aim_markers_target`, `aim_markers_toggle`, `aim_markers_setting`, `aim_markers_set_setting`, `aim_markers_result`, getter `aim_markers_fault` | `tfp_aim_markers_source_init`, `tfp_aim_markers_init`, `_update(m, &why)`, `_stop`, `_shutdown`, `_is_clean`, `_target(m, &out, &error)`, `_toggle(m, key, identity, len, &why)`, `_setting`, `_set_setting(m, value, &why)`, `_result` | **!** `toggle` / `set_setting` take texts / a boolean, values here. `target` answers a struct, a ref here (menu only). `fault`: `TfpAimMarkers.has_fault` / `.fault`. Host callbacks `settings`, `save`, `global_setting`, `save_global` bind to the `markers_*` entries. |

### recovery, diagnostics, transition, actions

| Host pointer | Binds to | Mismatch |
| --- | --- | --- |
| `recovery_new`, `recovery_reset`, `recovery_owner_reset`, `recovery_has_policies`, `recovery_needs_proof`, `recovery_success`, `recovery_cleanup` | `tfp_recovery_init`, `_reset`, `_owner_reset`, `_has_policies`, `_needs_proof(r, policy)`, `_success(r, policy, now, active)`, `_cleanup(r, policy, done, now)` | **!** policy names are texts here ('fov', 'blur', 'camera', 'head', 'movement'), `TfpPolicy` there. |
| `recovery_action` | `tfp_recovery_action(r, policy, now)` | **!** answers `TfpRecoveryPhase`; runtime.lua compares with 'cleanup' and 'update' and puts any other answer into a text (`'Presentation fov wait'`): the adapter needs retail's phase names. |
| `recovery_revalidate` | `tfp_recovery_revalidate(r, policy, &proof, now)` | `PROOF` values to `TfpProof` booleans. **!** `fresh` is nil when the backend is nil: `false` there. |
| `recovery_failure` | `tfp_recovery_failure(r, policy, &failure, clean, &proof, now)` | **!** two things. It has no `why` (retail passes the refusal text as the sixth argument). And it takes a `TfpFailure` (`temporary_read`, `native_error`), where runtime.lua passes the target's whole `result()`: the adapter must find the result behind the table's `ref` and derive the two. The answered row needs `core` and `phase` (text). |
| `recovery_snapshot` | none | **!** no `snapshot()`: build `TfpRtPolicies` from `TfpRecovery.rows[]` (the present rows, in enum order, which is the order this C walks anyway) with `core` and `phase` as text. |
| `recovery_blocks_view` | `tfp_recovery_blocks_view(r, now, &name, &wanted)` | **!** name and phase as texts (the menu's status line joins them). |
| `diagnostics_new` | `tfp_diagnostics_init(d, true, capacity, &error)` | |
| `diagnostics_record` | `tfp_diagnostics_record(d, frame, &sample, &error)` | **!** `TfpDiagnosticsSample` has 74 fields (`TFP_DIAGNOSTICS_FIELD_LIST`): the 71 of `REC` under the same names plus `policy`, `event`, `name`; copy by name. `TfpDiagnosticsValue` from `TfpRtValue`. RAISED there is `false` here. |
| `diagnostics_event` | `tfp_diagnostics_event(d, frame, name, &sample, &error)` | **!** the four `EVENT` values (`policy`, `reason`, `phase`, `cleanup_pending`) into the same 74-field sample. |
| `diagnostics_snapshot`, `diagnostics_clear` | none / `tfp_diagnostics_clear` | `snapshot` is debug api only: stub answering nil. |
| `transition_new` | `tfp_transition_init(t)` | The duration .18 and gap .25 are fixed there; the two arguments are those numbers. |
| `transition_native_ads` | `tfp_transition_native_ads(controller_active, seated, controller_bytes)` | **!** from the table's fields. |
| `transition_update` | `tfp_transition_update(t, &input, now, enabled, blocked, &out)` | **!** `TfpTransitionInput` (`player_identity`, `identity` as keys, `controller_active`, `seated`, `shoulder_aim`, `camera_kind`, `action`, `zoom_identity`) from the table: `zoom_identity` and `controller_active` are the two fields runtime.lua adds; `shoulder_aim` and `action` are not among the fields runtime.lua reads, they come from the snapshot behind `ref`. `enabled` and `foreign` are values (retail passes what `is_enabled()` and `foreign_camera()` answered). |
| `transition_result`, `transition_reset`, `transition_action`, `transition_resume` | `tfp_transition_result(t, &packet)`, `_reset(t)`, `_action(t, action, now, &out)`, `_resume(t, identity, player_identity, now, &out)` | **!** `reset` takes no reason; `action` an `int`; `resume` keys. The packet is a ref here except `epoch` and `progress`. |
| `actions_configure_catalogue` | `tfp_action_configure_catalogue(catalogue, rows, count)` | The gear rows are the binder's. |
| `actions_resolve`, `actions_saved`, `actions_default_placement` | `tfp_action_resolve(config, catalogue, key, action, …)`, `tfp_action_saved(config, key, action, value, all, …)`, `tfp_action_default_placement` | **!** typed placements there, opaque values here (runtime.lua only passes them on: to `controller:set_placement`, `mod.set`, the editor). Action names to `TfpAct` with `tfp_action_from_name`. |
| `actions_weapon_info` | `tfp_action_weapon_info(catalogue, key)` | `WINFO.name`, `.family` (text). Menu only. |
| getters `actions_observed`, `actions_actions` | `tfp_action_observed(action)` / none | `actions` is the editor's list of rows: stub. |

### outside milestones 1 to 3: stubs

| Host pointer | Binds to | Mismatch |
| --- | --- | --- |
| `editor_new`, `editor_update`, `editor_is_active`, `editor_cancel`, `editor_shutdown`, `editor_request_open`, `editor_status` | none (the F8 placement editor) | Stub: `is_active` false, `update` nothing, `shutdown` / `cancel` true. **!** `editor_is_active` false is what makes the frame run `movement_update`. |
| `menu_new`, `menu_update`, `menu_begin_shutdown`, `menu_shutdown`, `menu_native_ads`, `menu_ads_mode`; optional `menu_fird_person_arms`, `menu_show_torso`, `menu_near_options`, `menu_motion_options` | none (the settings menu; `src/ui.h` is the 0.5.34 one) | Stub: `shutdown` true, `native_ads` false; the optional four `NULL` (arms wanted, torso hidden, near clip 0 / 0, motion defaults from the config). |
| `head_positioner_*` (9) | none (`head_placement.lua`) | Stub. Only the editor's camera mode and Fird Person reach them. |
| `fird_*`, `fird_aim_new`, `fird_native_ads_new`, `fird_fov_new`, `fird_entry_*`, `fird_warm_*`, `fird_render_*`, `ready_pose_*`, `held_fire_*`, `ready_source_new`, `ready_timing_*`, `capture_gate_*`, getters `fird_module_*`, `fird_*`, `ref_field` (57 pointers, 12 fields) | none (parked in retail: `start_fird` is only on the debug api) | Never called unless the entry `start_fird` / `probe_fird` is. May all be stubs; `fird_module_native_animation_default` answers true as in retail. |
| `mod_log` | the mod's log sink | |
| `mod_set`, getters `config_table`, `config_field`, `config_action_mapped` | none (`src/settings.h` / `preferences.h` are the 0.5.34 ones) | **!** the mod's saved settings: `aim_markers`, `show_aim_markers`, `action_state_mapping`, `fird_head_camera`, `motion_enabled`, `motion_scale`, `weapon_placements`, `action_placements`, `placement`, `family_placement_defaults`, `near_clip_profiles`. `config_action_mapped(action)` is the loop of `:772-774` (does any table of `action_state_mapping` hold this name), moved behind the host because the mapping is an opaque value here. |
| getters `profile_body_sleeves`, `profile_grip_hand`, `profile_seated_head` | the `camera` section of the profile (`src/profile.h`) | Look the three names up there; none was found by name. |
| `clock_now` | the engine's `Application.time_since_launch` (the `clock` of `TfpBackendApi`) | Answers a value: the runtime checks it is a finite number that does not go backwards. Must not become `NULL` after `on_enable`. |
| `mouse_button_index`, `mouse_button` | the engine's `Mouse.button_index` / `Mouse.button` | none in the repository yet. `NULL` for both switches the aim-release one-shot off. |
| `global_get`, `global_set` | the Lua global `NativeADSCameraTest` | A flag in the binder, unless the debug api is exported to Lua. |

## Open issues

- **`pairs` order.** Three loops of runtime.lua walk a table with string keys where the order shows in the calls made: the eleven stops of `recover_new_owner` (`:352`, where a refusal also ends the `is_clean` questions for the rest), the policies of the recovery ledger (`:601`) and the restore after a failed reset (`:810`). LuaJIT's order there follows the string hashes and, in current builds, a per-process seed: retail does not have one order. The C uses the order the Lua source lists them in (fov, blur, camera, head, movement, near, dof, native_aim, aim_markers, arm_pieces, grip_hand; the ledger's own order; weapon_placements, action_placements, placement), and the runner replaces the global `pairs` with one that walks string-keyed tables in that order (`run_runtime.lua`; the retail file is untouched).
- **Lease and snapshot lifetime.** A text or a ref handed to the runtime must stay readable until the outermost `tfp_rt_call` returns. The capture tables of the parked Fird Person calibration are kept across frames by `ref`.
- **`setjmp` / `longjmp`.** A raise unwinds through the runtime's own frames only (a host call reports a raise by returning false; a nested `tfp_rt_call` catches its own). With mingw-w64 `longjmp` unwinds through SEH, which works for gcc-built frames; it was compiled, not run.
- **LuaJIT's error texts** are copied where the vectors reach them: indexing a nil upvalue or local, calling a nil `foreign_camera` or `mod.set`, concatenating a non-text, arithmetic on a boolean revision, the assert prefix. Other type errors (a non-number where the capture arithmetic of `:994-998` needs one, a concatenation error among the six status texts of `:1176`) have a text in the C that was written by analogy and is not compared; the random sessions keep those answers to their type (`FIXED` in `runtime_sessions.py`).
- **The game's Lua** is not LuaJIT 2.1 from `/usr/bin`: the assert prefix, the wording of type errors and the `pairs` order are what this interpreter does. They only show in log lines, in the diagnostics row's `reason` and in the order of cleanup calls.
- **`start_native`, the editor, the menu, Fird Person, ready stance** are translated and proven as call sequences against fakes; nothing of them exists in C to bind to (see the stubs).
- **`config_action_mapped`** moved two lines of runtime.lua behind the host (see the binding table).
- This file reads no game memory itself and has no build constant: the three offsets of the record's camera and rig reads (`+0x18`, `+0x28`, `+16`, `+0x80`, `+0x88`, `5*0x40+0x24`, `5*0x40+0x30`) are retail's for build 26568081 and were not checked against 25480438. They only feed diagnostics fields.
