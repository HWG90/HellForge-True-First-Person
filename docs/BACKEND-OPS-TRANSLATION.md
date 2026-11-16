# Backend operations: translation notes

`src/backend_ops.c` / `src/backend_ops.h` translate retail `src/first_person/backend.lua` (1,471 lines, 0.5.41-smooth.66, commit 1d0d453) for build 25480438: all 61 `Backend` methods and the 6 file-local helpers, on the path the game runs (`Backend.new(log)` with no fixture `io`, `runtime.lua:665`). `Backend:action_state` hands over to `src/first_person/action_state.lua` (126 lines); its sampler is translated in the same unit because a full snapshot cannot be taken without it. The Lua files are the authority. Nothing was improved, simplified or reordered; where the C differs, it is listed here.

The snapshot part (`_top`, `_map`, `_descriptor`, `_snapshot`) was already in `src/snapshot.c`. It is here again, complete: with the `retiring` and `pin_player` forms, the action-state / on-foot test and the foreign-camera test that the full retail snapshot applies last, the `full` and `entry` validation scopes, and the per-frame memory of what was already proven. `src/snapshot.c` is not touched and answers the same for the cases it covers.

## Counts

| | |
|---|---|
| retail methods translated | 61 of 61 (`_windows` becomes the host, see below), 6 local helpers, the `sample` function of `action_state.lua` |
| C | `backend_ops.c` 1,945 lines, `backend_ops.h` 284 lines; 52 exported functions. gcc and clang `-Wall -Wextra -Werror` clean at -O0 to -O3, also for the Windows target (zig cc, MinGW) |
| reference runs | 372 scripts: 112 hand-written, 260 seeded random walks |
| steps compared | 42,837, of which 22,056 method calls; 2,601 engine calls and 24 memory writes with their arguments |
| retail line coverage | `backend.lua`: 1,020 of 1,034 lines that carry code; `action_state.lua`: 99 of 99 |
| mutation check | 2,894 single changes to the C, 438 do not compile; 2,334 of the other 2,456 are noticed (95.0 %); the 122 survivors are classified below |
| open constants on build 25480438 | 13 names, one thing: the presentation switch (`_invoke_presentation`, not on the shipped path) |

## Interface

Everything from outside comes through `TfpOpsHost`; the only state is the caller's `TfpOps`.

| Lua | C |
|---|---|
| `Backend:_windows()`: `GetModuleHandleA`, `ReadProcessMemory`, `VirtualQuery`, casts to function pointers, raw pointer stores, `wine_get_version` | `host->module`, `host->read`, `host->query`, `host->call`, `host->write`, `host->wine` |
| `log` | `host->log` (may be NULL) |
| a clock | `host->clock`: carried in the struct because the callers share the host; `backend.lua` never asks the time |
| `backend.foreign_camera`, `camera_before_entry`, `placement_resolver`, `presentation_transition` (plain fields `runtime.lua` installs) | `host->foreign_camera`, `->camera_before_entry`, `->placement_resolver`, `->presentation_transition`; NULL = not installed |
| `Host.require('…/pose.lua').new(Profile.pose)`, the alignment one | `host->load_pose`, `host->load_alignment`; or the helpers handed to `tfp_ops_init`, as `Backend.new(log, io, alignment, pose)` takes them |
| the pose / alignment adapter object (`prepare`, `begin`, `owned_handle`, `finish`, `stats`, `set_placement`, `set_zoom_policy`, `set_ads_placement`, `transition`, `set_motion`) | `TfpOpsPoseHelper`, ten function pointers; a NULL `owned_handle` / `transition` is retail's "not a function" |
| `Profile` | build data: the constant block of `backend_ops.c` and `src/profile.h`; the rest: `TfpOpsProfile` (`TFP_OPS_PROFILE_SHIPPED`) |
| `backend.support_grip_tracking`, `fird_camera_active`, `fird_camera_offset` | fields of `TfpOps` with the same names |

How Lua values are spelled:

- **First result**: `TfpOpsResult` = true, false, nil, or `RAISED` when a Lua error leaves the method. Retail does let errors out of some methods: `backpack`, `camera_only_weapon`, `keep_aim_view`, `detach_seated_camera`, `_invoke_presentation`, `set_pose_zoom_policy` / `set_ads_placement` (the helper's), `set_pose_motion` (the packet hook's), `prepare_pose` and `enter_pose` (the pose configuration is built in the argument list of a `pcall`, outside it: a raise there leaves the method **after the token was consumed**), `warm_start` / `warm_finish` (the foreign-camera hook's).
- **Second result**: `TfpOpsText`, present or not. `tostring(called and reason or accepted)` is carried as written: a refusal without a text reads `false` or `nil`.
- **Tables compared by identity** (leases, tokens, handles): the caller owns the storage and the address is the identity. A lease must stay where it is while the backend can name it and its storage must not be reused for another lease of the same backend. Retail's weak token tables become `issued` / `owner` / `kind` in the token.
- **Identity strings** (`identity`, `player_identity`) are compared field by field (`tfp_ops_same_identity`, `tfp_ops_same_player`), exactly the numbers and descriptor bytes retail prints into the string. A player-level identity never equals a weapon-level one (retail: two different string shapes). Where a string travels by value (the presentation phase packet) it is a 64-bit key (`tfp_ops_identity_key`, `tfp_ops_player_key`; FNV-1a over the same fields); 0 stands for "empty or not a string".
- **nil against false** in a lease: `entry_aim_changed` and the four fields like it are -1 / 0 / 1; `orphan_polls` 0 is nil.

## Retail behaviour that looks odd and is kept

| | |
|---|---|
| `camera_ready()` returns `true, "<game base as a number>"` | `return ok,ok and nil or tostring(why)` (:130): `ok and nil` is nil, so the `or` takes `tostring` of `_module`'s result. `tfp_ops_camera_ready` gives TRUE with that text |
| a pose entry looks its token up among the **alignment** tokens until the first pose token exists | `kind=='pose' and self.pose_tokens or self.alignment_tokens` (:573) with `pose_tokens` still nil. Found by the random walks: an alignment token then gets past the token test and is refused two steps later ("Prepared viewmodel or executable changed before entry"). `has_tokens[]` carries it |
| `prepare_pose` / `enter_pose` raise instead of refusing when the weapon's resource cannot be read, is zero, or the resolver hook raises | see "First result" above. In `enter_pose` the token is already marked used and no lease is returned |
| the partial-install lease's error is the text `nil` when the adapter gave no reason | `lease.error=armed and tostring(handle) or …` (:621) |
| a full snapshot fails 'Local avatar retired' on `count > 8` only after `index < count` was read once | the count is read twice (:187) |
| `_enter` logs `camera=nil` and `packed flags unchanged=nil` when the snapshot after the entry fails | as written (:655) |
| `release` counts orphan polls only while neither snapshot form works, and resets the count as soon as one works | (:1437, :1452) |

## What is not in the C, with the reason

| Part | Lines | Why |
|---|---|---|
| the fixture seam `self.io` (`io.read`, `io.module`, `io.invoke`, `io.executable`, `io.function_address`, `io.invoke_presentation`, `io.write_byte`, `io.write_bytes`, `io.detach_camera`) | 69, 90, 442, 546-548, 691-693, 736, 1193-1196, 1226, 1346-1348, 1357-1358, 1382-1384 | test hook of the retail suite; `runtime.lua:665` passes none. Note what this means for retail's own fixtures: with `io` the snapshot skips the action-state and on-foot test (:272). The C is the path without `io` |
| `_windows` (cdefs, `ffi.load`, the 1024-byte buffer) | 43-64 | the host |
| `PORT51.jit_util.funcinfo(fn).addr` in `renderer_dof_value` | 737 | the caller hands over the binding's address (`TfpOpsBinding`); the function identity retail compares is `identity` |
| `Backend.profile`; the plain fields `runtime.lua` sets and other modules read (`presentation_action`, `presentation_resume`, `presentation_coordinator`, `action_placement`, `action_mapping`, `action_placement_revision`, `pin_first_person`, `camera_update_token`); `frame_game_base`, `frame_game_epoch` (set at :124, read by nothing) | | no behaviour in `backend.lua` |
| type tests on arguments (`kind(lease)~='table'` for a value that is neither nil nor a table, a non-string `flags`, a non-number `controller`, a token field of another type) | many | a typed argument cannot hold them. nil is NULL and is tested everywhere retail tests it. `blend_entry` keeps "not a Boolean" as any value other than -1 / 0 / 1 |
| 'Incomplete read' (:76), 'Unknown game validation scope' (:88), 'Windows x64 required' (:46), `error('Unknown native call')` (:456), 'Unknown body event operation' (:1225) | | cannot happen without `io` or with the fixed names the methods pass |
| the loader assert of `action_state` (:346) | | the sampler is linked in. The pose / alignment loader asserts exist: a NULL `host->load_pose` is 'Pose module loader unavailable' |
| a `log` function that raises | 648, 655, 1330, 1446, 1467 | `host->log` returns nothing |

Kept although nothing on the shipped path calls them, because the task is the whole file: `warm_start`, `warm_finish`, `camera_ready`, `fird_fov_config`, `_invoke_presentation` (fird), `enter` without a helper, `prepare_alignment`, `alignment_status`, `set_pose_zoom_policy`, the body event (`_body_api`, `_invoke_body`, `prepare_body_pose`, `release_body_pose`).

Four checks are in the C because the Lua has them, and cannot fire through this interface: 'FOV executable identity unavailable' (a lease's context always holds a valid executable base), 'Initialization generation changed' (a warm lease that is not `warm_lease` is always done), `on_foot`'s avatar-index range and flags-address tests (a snapshot always satisfies them), `keep_aim_view`'s mask and guard tests (`safe` already excludes them).

## Differences from retail a caller can see

1. **Assert texts lose their position.** Lua 5.1's `assert(v, message)` puts `<chunk name>:<line>: ` in front of the message, and so does the game's Lua 5.1; every text retail produces through `assert` therefore starts with the mod loader's name for `backend.lua` and a line number. The C gives the message alone, as the other translations of this project do, and the runner takes the prefix off. This also holds inside composed texts ('Waiting to verify native camera cleanup: …') and in the log line 'Native entry error: …'.
2. **'Pointer unavailable at0x… (value0x…)'** names the address that was read; it is this build's address. The value is printed as LuaJIT's `%x` prints a Lua number (a value of 2^63 or more goes through a double: `0xffffffffffffffff` prints as `0`; scripted).
3. **A refused write.** Retail stores through a raw pointer; a failure would be a crash. `host->write` can say no: the method is then RAISED with 'Memory write unavailable' and nothing is counted (checked in the test without a retail counterpart).
4. **Texts are held in 320 bytes** (`reason` and `action_error` of a snapshot in 160). Retail has no limit; the longest retail text here is under 150.
5. **`host->call` can report a raise** (`pcall` around an FFI call in retail). A real native call cannot; the answer exists so that a guarded call (SEH) has somewhere to go, and so that the Lua paths behind a failed `pcall` are exercised.

## Constants on build 25480438

`B_` = build data, `L_` = structure layout with the same number as retail. Evidence: **map** = `tools/build_map.py` / `docs/BUILD-DATA-25480438.md`; **profile.h**; **image** = bytes read on 2026-11-16 from `native-work/evidence/game-pre1020.dll` and `exe-25480438.bin` (file offset = RVA); a file name = that file of this repository reads the same field on this build; **self-proving** = the code compares the slot's content with a pinned function address before using it, so a wrong offset refuses instead of misleading.

### Established

| what | retail | here | evidence |
|---|---|---|---|
| game.dll stamp / size | `0x6af322c1` / `0x3382000` | `0x6ab3b43f` / `0x4744000` | profile.h; BUILD-DIFFERENCES |
| helldivers2.exe stamp / size | `0x6af2d5e6` / `0x29fe000` | `0x6ab382e4` / `0x39e8000` | profile.h; BUILD-DIFFERENCES |
| globals camera, controller, world, player, avatar, wield, weapons | `0x2e287d0`, `0x2e381a8`, `0x2ce41b8`, `0x2ce1618`, `0x2ce1ee0`, `0x2ce1fd0`, `0x2ce1ea0` | `0x346d560`, `0x347ce70`, `0x346bf98`, `0x3326468`, `0x3326d20`, `0x3326420`, `0x3326ce0` | map; profile.h |
| functions enter, exit, allowed | `0x110fc80`, `0x11108a0`, `0x1144d60` | `0x127ddb0`, `0x127e9d0`, `0x12b2d20` | map; profile.h |
| the 14 code pins (`Profile.checks`) | profile.lua | `fp_witnesses` | profile.h; `tools/verify_profile.py` |
| entry-scope pin, enter wrapper + 0x42 (11 bytes) | `0x110fcc2` `0f57d2488bcbe8a3860000` | `0x127ddf2` `0f57d2488bcbe833850000` | image: same instructions at the same distance into the pinned wrapper; the call lands on game+0x1286330, the `evaluator` pin |
| light map, entity map, entity rows (world) | `0xf24990`, `0xf1c978`, `0xf349e0` | `0xf22ec8`, `0xf1aeb0`, `0xf32f18` | map; profile.h |
| avatar flags, rig ref, row stride | `0x53e888`, `0x53e9c0`, `0x1240` | `0x53e880`, `0x53e9b8`, `0x1238` | map; profile.h |
| scene state global, scene field | `0x2ce1508`, `+0xac29c` | `0x3326340`, `+0xac21c` | map; profile.h; `action.c` |
| engine API root | `0x2ce14c8` | `0x3326308` | map; profile.h; image (loaded at game+0x127ea56) |
| inventory pool | `0x2ce18e8` | `0x3326738` | BUILD-DIFFERENCES (by hand; the First Person Controller reads the pool with the same field offsets). See doubt 1 for the backpack slot |
| unit registry (exe) | `0x1ab76f8` | `0x1a100f0` | map; profile.h; `backend.c` |
| Unit `exists` / `trigger` (exe; the two executable pins) | `0x1f75d0`, `0x1f77c0` | `0x201710`, `0x201900` | map; bytes from the image, 32 each, equal to retail's except the call displacement |
| animation getter / resource getter (exe) | `0x28d510`, `0x28d520` | `0x2bd9c0`, `0x2bd9d0` | map; `action.c`, `native_contract.h`; image: the 8 and 14 bytes are retail's |
| renderer setting getter / setter (exe) | `0x3d43d0` `488bd1e908fdffff`, `0x3d3ff0` `488bd1e988faffff` | `0x3fa3d0` `488bd1e938feffff`, `0x3fa120` `488bd1e998fbffff` | map; `feature_contract.h`; image |
| renderer table load site (exe, 7 bytes) | `0x3d419a` `488b0d1f376e01` | `0x3fa262` `4c8b354f606101` | map; image: `mov r14,[rip+…]` (retail: `rcx`), and the displacement resolves to exe+0x1a102b8, the table global |
| renderer type test site (exe, 9 bytes) | `0x3d420e` `83f804751a41387108` | `0x3fa2f8` `83f80475120fb65708` | map; image: `cmp eax,4` / `jne` / `movzx edx,byte [rdi+8]` (retail: `cmp [r9+8],sil`): the same type-4 test and Boolean at +8 |
| renderer setting table global (exe) | `0x1ab78c0` | `0x1a102b8` | map; `effects.c` |
| camera request ring: queue +0x1f8, ring +0x200, record 0xe8, fallback +0x1f00; record +4 avatar, +8 weapon, +0xc body | same | same | `snapshot.c`; image (exit at game+0x127e9f3: `[rcx+0x1fc]`, `+0x200`, `imul 0xe8`) |
| context +8 camera unit, +0x180 world; API root +8, slot +0xF0 (the camera unlink the game's exit makes) | game+0x111093e | game+0x127ea56..0x127ea6e | image: `mov edx,[r12+8]` / `mov rcx,[r12+0x180]` / `mov r8,[rax+8]` / `call [r8+0xf0]` |
| camera +0x18 = unit object +0x60; unit parent link +0x1D0 | same | same | `scene.c`, `visibility_policy.c`, `native_observer.c` |
| request +0x98 (sight distance) | same | same | `pose_policy.c` |
| controller: mask +0, active +4, guard +5 | same | same | `snapshot.c`; profile.h pins `controller_enter` (`cmp byte [rcx+4],0`) |
| player +0x88, +0x3a8; avatar pool +0xf8, +0x6c, +0x110; wield +0x30, +0x48, +0x60, row 0x1d0; weapons +0x30, +0x48 | same | same | `snapshot.c`, `backend.c` |
| unit registry block +0x88 (units +0, capacity +0x10, generations +0x18), ref = generation << 22 \| index | same | same | `backend.c` |
| unit object: resource +0x358, animation +0x178, vtable slot 0x1B0; instance: graph +0x28, active +0x50; graph: layer count +4, table +8 | same | same | `action.c` |
| vtable slot 0x1B8 (resource getter) | same | same | self-proving: must hold exe+0x2bd9d0 with the pinned bytes |
| Unit API table: root +0x18 | same | same | `pose_policy.c`, `visibility_policy.c` |
| Unit API slots +0x350 (`exists`), +0x370 (`trigger`) | same | same | self-proving: must hold exe+0x201710 / exe+0x201900 |
| renderer table: count +0, rows +8; row 24 bytes: type +0, Boolean +8, key +16; keys `0x06f1d0d3`, `0x72e8f18c` | same | same | `effects.c` |
| inventory: map +40, owners +64, rows +80, row 48 bytes | same | same | BUILD-DIFFERENCES (First Person Controller `native_view.c`) |
| not build data: body event hash `0x372071D0`, Breaching Hammer resource `53 85 BD A2 BD C9 3E 5F`, body animation resource `4D1C334D294DFA97`, 31 layers, state 36 / 3 of layer 14, scenes 3 and 4 | same | same | names and content; `action.c` uses the animation ones on this build |

### OPEN

One thing, 13 names, all 0 in `backend_ops.c`:

| name | retail | why open |
|---|---|---|
| `B_PRESENTATION_FUNCTION` | game+`0x833440` | the game's local presentation switch. Added to retail after it left this build; the migration records give no value (`0x833440`, `0x833513`, `0x833540`, `0x833763`, `0x110fffe`, `0x11109eb`, `0x1110a43`, `0x1fb540`, `0x1fb574` are all in the "no value here yet" list) |
| `B_PRESENTATION_PINS` (9: seven in game.dll, two in the executable) | the nine pins of :530-538 | same; each also needs this build's bytes |
| `B_PRESENTATION_MODE`, `B_PRESENTATION_MODE_STRIDE` | avatar_owner+`0x546B20`, `0x1B8` | not in the records; nothing in this repository reads the field |
| `B_PRESENTATION_ESTABLISHED` | | 0 because of these |

With `B_PRESENTATION_ESTABLISHED` 0, `tfp_ops_invoke_presentation` is RAISED at once with "FP presentation is not established on build 25480438 (function, code pins and mode byte are open)", before any read, call or change of state. The test's production program checks exactly that. Nothing on the shipped path calls `_invoke_presentation` (only `fird_presentation.lua`, which nothing loads).

Where to look when it is wanted: the game's exit calls the switch. Retail's pin `0x11109eb` is exit + 0x14b and its bytes `418b560441b1014889b424600200` are at this build's exit + 0x14b too (game+0x127eb1b, seen while reading the unlink site); the `call` that follows it leads to the function.

For the comparison with retail only, `tests/backend_ops_open.h` (generated, test only) puts the 13 names at places in the test's fake image, at offsets unlike retail's, with retail's code bytes copied there. `tests/run-vectors-backend-ops.sh` builds a second object with `-DTFP_OPS_TEST_OPEN_CONSTANTS='"backend_ops_open.h"'`. They are **not** this build's values; no shipped build defines the macro.

## What `src/backend.c` (the donor) has that this replaces

| donor | here | note |
|---|---|---|
| `fp_read`, `fp_u32`, `fp_pointer` | `tfp_ops_read`, `tfp_ops_u8`, `tfp_ops_u32`, `tfp_ops_ptr` | retail's limits (1 to 1024 bytes; the donor allows 8192) and texts |
| `fp_bytes` | the pin comparison inside `_module`, `_alignment_executable`, `renderer_dof_value`, `_invoke_presentation` | |
| `fp_image` | the PE checks of `tfp_ops_module` and `tfp_ops_alignment_executable` | retail also checks the machine type and says which check failed |
| `fp_prove_profile` | `tfp_ops_module` in its three scopes, `tfp_ops_camera_ready` | |
| `fp_map` | `tfp_ops_map` | |
| `fp_snapshot`, `FpSnapshot` | `tfp_ops_snapshot`, `tfp_ops_player_snapshot`, `tfp_ops_pin_player_snapshot`, `TfpOpsSnapshot` | retail's order and texts (the donor folds every refusal into five); the donor has no action state, on-foot or foreign-camera part and always reads the camera unit |
| `fp_same_player` | `tfp_ops_same_player` | retail also compares the module base and the local entity |
| `fp_unit_object` | `tfp_ops_alignment_context` | the donor additionally requires two vtable slots and the object's own reference; retail's lookup does not, and retail is the authority here |
| `fp_weapon_key` | the key inside `_pose_config` (`placement_weapon_key`) | retail: 16 lower-case hex characters in memory order |
| `fp_weapon_record` | nothing | no counterpart in `backend.lua` |

Outside the donor file, the same retail code is also behind `src/action.c` (`fp_action_read`: here `tfp_ops_action_state` and `tfp_ops_on_foot`, with retail's layer cache and five-layer mode), `src/effects.c` (`cached`: here `tfp_ops_renderer_dof_value`, with retail's contract and index cache) and `src/snapshot.c` (see the top).

Not done here: the glue from these functions to `TfpBackendApi` of `src/controller.h` (a `TfpView` from a `TfpOpsSnapshot`, a `TfpLease` from a `TfpOpsLease`). It is a field-for-field copy, with `tfp_ops_identity_key` / `tfp_ops_player_key` for the two keys.

## The proof

- `tools/retail_ref/run_backend_ops.lua` loads retail `backend.lua`, `action_state.lua` and `profile.lua` unmodified under `/usr/bin/luajit` (interpreter only) and builds the backend the way the game does, without `io`. What that path takes from Windows through the FFI is a Lua table: `ffi.load('kernel32')` gives scripted `GetModuleHandleA`, `ReadProcessMemory` and `VirtualQuery`; a cast to a function-pointer type gives a function that records the call and its arguments; a cast to `uint8_t *` / `uint32_t *` gives an object whose assignment is recorded as a write. So the real reads, page checks, engine calls and stores of the shipped path are what is compared. The pose and alignment helpers and the four hooks are scripted objects.
- `tools/retail_ref/gen_backend_ops.py` refuses any retail commit but 1d0d453 and a modified retail file. A scene is one logical game state (image headers, pins, camera ring, player, avatar, wield and weapon pools, unit registry, body animation graph, inventory, API tables, renderer table, about 150 records). It is written once in retail's layout for the Lua and once in this build's for the C. A script is a list of steps: scene edits, scripted answers (each may carry a scene change that happens inside the call: the game's enter appends its request, a hook moves a module), traps (the scene changes after the n-th read of a record, or the record goes away) and method calls.
- `tests/backend_ops_test.c` replays the scripts with a host, helpers and hooks built the same way. After **every** step the text must be retail's: the ordered trace (engine calls with address and arguments, page queries, writes with address, width and value, helper calls with the whole configuration, hook calls, log lines), the answer (result, text; for a snapshot all 42 fields plus which earlier snapshots it equals in identity and player identity), and the state: frame epoch and flag, which lease is `alignment_lease` / `warm_lease` / `presentation_pose_lease`, placement and weapon key, counters, the renderer contract, and for every lease and token all fields (printed when they changed).
- `tests/run-vectors-backend-ops.sh` builds and runs it twice. Production object: 357 runs (the 15 that use the presentation switch are left out; that it refuses is checked). Test-only open constants: all 372.

Hand-written runs: a play session; the game image and all 14 pins in each scope; frame caches; every snapshot refusal in three snapshot forms; the foreign camera at every place it is asked; action state and on-foot (every refusal of the sampler, layer cache, five-layer mode, bridge and scene); camera and fird configuration; backpack and hammer; entry (every refusal, every way a lease comes back for cleanup only, plain and alignment entry); preparation refusals for both helpers; helper loading; placements; phase packets and motion; pose status; release with the orphan exit; the two one-shot writes; mapped pages under Wine and not; the body event; warm entry; the presentation switch; the renderer table; values exactly at each limit and a scene with no index at zero; what changes or disappears between a method's first and second look (every record a method reads, at its first to fourth read, for the readers; during entry, poll and release; for the body event). The random walks mix all of it, with scripted refusals and traps.

### Differences met while proving

1. **The token table rule** (above): random walk 180 of the first cut. The C refused an alignment token at the token test; retail lets it through until the context comparison. Fixed in the C.
2. **The assert prefix in the log line**: the first long cut showed `Native entry error: B:72: Memory unavailable`. That is where it became clear that Lua 5.1's `assert` adds the position (see "Differences" 1); the runner now takes it off in log lines as it did in answers.
3. Nothing else differed in decisions, order, state or texts. Several of my own scripts did not do what their names said and were corrected after the coverage and mutation checks showed it: an entry that succeeded into a discarded lease and blocked the rest of a run; a "weapon gone" scene the retiring snapshot could still prove; byte changes the backend's own write had already made; and fake-memory records that overlapped, so that taking one away took a neighbour's bytes with it (the generator now refuses to build a scene with overlapping records). In each case the C and the Lua had agreed on the damaged scene; the correction changed what was tested, not a result.

## Coverage

Line hook on the two retail files over all 372 scripts; the lines that carry code are LuaJIT's own (`activelines` of every function).

`backend.lua`: 1,034 lines carry code, 1,020 are reached. The 14 others:

- 547-548, 692-693, 1195-1196, 1347-1348, 1358, 1383-1384 (11 lines): the fixture seam, only run with `io`.
- 150 and 1448: the `end` after an `error(...)` / after a block that always returns. No instruction can reach them.
- 456: `else error('Unknown native call') end`: `_invoke` is only called with `allowed`, `enter`, `exit`.

`action_state.lua`: 99 of 99.

The file packs several statements into a line, so a reached line can hide an unreached part. Known parts that are not exercised: the type tests of the table above; 'Incomplete read'; the `io` half of the mixed lines (69, 90, 442, 736, 1226, 1346, 1357, 1382); a NaN (as opposed to missing) packet epoch or progress; a quaternion whose norm is exactly 0.002 from 1. The mutation check below is the measure for the rest.

## Mutation check

`tools/retail_ref/mutate_backend_ops.py` makes single changes to a scratch copy of `src/backend_ops.c` and runs `tests/run-vectors-backend-ops.sh` (both programs) on each: every `==` / `!=` turned round, every `<` `<=` `>` `>=` moved by one, `&&` for `||`, `true` for `false`, a result swapped (`TRUE`, `FALSE`, `NIL`, `RAISED`), a negation dropped, every number plus one (the build table and the layout offsets included), every text changed, every simple statement removed. Not made: the first value of a local that is always assigned before it is read, array sizes, and the test-only values of the open constants.

2,894 changes; 438 do not compile. Of the other 2,456, **2,334 are noticed (95.0 %), 122 are not.** How the figure was put together: a complete run against the cut of 371 scripts noticed 2,329 of 2,451; the last cut added one script and one exact text comparison, and a retry of the 122 survivors noticed two more (scripts were only added in between); one line was then rewritten because gcc warned about it at -O2, which replaced its one change (a survivor) by six (three noticed).

The first complete run stood at 85.4 % (2,180 of 2,554 as the tool then counted). Its survivors were read one by one and showed real gaps, which the scripts now cover: values exactly at every limit (pointer and read bounds, table capacities, the 4,096-probe bound with the wanted key one slot past it, offsets of the animation graph, the registry capacity, generation bits); a scene with no index at zero; every record a method reads being changed or gone at its second, third or fourth read; the changes that can only happen between two checks of one method (a hook that moves the module, the Unit API changing between the two looks of one dispatch); the frame caches from a clean start; the aim-only and controller-only cases; the orphan count starting again; and the scripts of mine that had not reached what they were named for.

The 122 survivors:

| kind | count | what |
|---|---|---|
| one byte more | 31 | a size or a loop bound plus one: a comparison of 21 bytes where the field has 20, a read of 97 header bytes for 96, one more round over a terminating zero. The answer only changes if the byte behind the field differs, which no retail rule says anything about |
| a value nothing looks at | 47 | zeroing a structure that is then filled completely (16); the unused slots of the four-argument engine call and of the page test (11); the default of a field that is marked absent, the `-1` that stands for nil (any negative does), `present` bits of a stored placement, a flag that is already set, the result code of the inner sampler whose callers only ask "true?" (20) |
| same answer by arithmetic | 16 | `capacity > 1048577` (not a power of two either way); the probe count for a capacity of exactly 4,096; the 1,024-byte chunk rule at exactly 1,024; `value < 1` for an infinity; the hex digits of the pinned strings; a failed second read of the scene state, which fails the next comparison instead; `rows > HIGH`, caught by the next term; the binary search's `<`, where equality was handled the line before; the 2^63 corner of `%x` (the conversion gives the same bits on x86-64) |
| cannot fire through this interface | 14 | the four checks listed under "What is not in the C" (an executable base below 65,536 inside a lease's context; a warm lease that is neither current nor done; an avatar index of 8 or a foreign flags address inside a snapshot; a mask or guard under `safe`), a cached renderer index equal to the row count (the cache is dropped when the header changes), and the weapon descriptor address of a full snapshot being below 65,536 |
| the two identity keys | 6 | the constants of the hash: any constants give keys that are equal exactly when the fields are |
| not scripted | 8 | the text `nan` (a NaN error number cannot be told from a missing one in the scripts); a quaternion norm exactly 0.002 from 1; a helper that answers false, rather than raising, to `owned_handle` (2; the interface has no such answer); `&&` for `||` in front of a snapshot that was not taken (2; the changed code reads memory that was never written); where an action-state error text longer than 159 bytes is cut (2; none is longer than 60) |

None of the 122 is a decision, an order of calls, a text or a state that the vectors leave unchecked on a path retail can take, with the eight "not scripted" ones as the exceptions named.

Both test programs also run clean under `-fsanitize=address,undefined` (built by hand; the run script builds without it, as the other vector runners do).

## Open doubts

1. **Backpack slot.** `backpack` reads the inventory row's dword at +12 ("slot 3"). The pool, its map and the 48-byte rows are used on this build by the First Person Controller; that dword has not been read in the game here (BUILD-DIFFERENCES says the same). Everything after it is checked (the entity must resolve through the world's descriptor table and its unit must not be the body, the rig or the weapon), so a wrong slot gives a refusal or another equipped item's unit, not a wild read. It is marked established-by-layout, not seen live.
2. **The two renderer sites differ in register and addressing from retail's** (table above). They are this build's bytes at the map's addresses and say the same thing; whether the table's row layout is the one `effects.c` already relies on in the game is the stronger evidence.
3. **The assert prefix** (Differences 1) is in every refusal text a player or a log reader can see in retail and not in the C.
4. **Number printing** follows the reference LuaJIT (`%.14g`, `inf`, `-inf`, `nan`): it matters only in 'Pose helper error N' and the duration limit text. A NaN error number is not scripted.
5. **`TfpOpsPoseHelper` is my reading of the adapter's Lua interface** (`pose.lua`, `alignment.lua`), which this unit does not translate. `armed` / `expired` are the helper's numbers (retail accepts `true` or `1`).
6. The scripted helpers and hooks are written twice (Lua runner, C test); the real pose helper was not put behind the backend here.
