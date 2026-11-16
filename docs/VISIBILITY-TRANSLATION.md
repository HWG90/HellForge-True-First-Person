# Head visibility and model shadow source in C

Translations of two retail modules of True First Person 0.5.41-smooth.66 (commit 1d0d453): `src/first_person/head_visibility.lua` (767 lines, sha256 78b8e2c7...) and `src/first_person/model_shadow_source.lua` (129 lines, sha256 fac450ee...). The Lua files are the authority. Each C file is checked by replaying the retail file's own answers.

| File | What |
| --- | --- |
| `src/engine_iface.h` | The engine script functions as one table of pointers (nine so far). |
| `src/head_visibility.c`, `.h` | `Head.new` and everything in it: `update`, `stop`, `shutdown`, `is_clean`, `result`, `status`, `render_appearance`, `sdk_handles`. |
| `src/model_shadow_source.c`, `.h` | `M.new`: `probe`, `has_observation`, `unchanged`, `valid`. Also the types both modules share (snapshot, text, identity). |
| `tools/retail_ref/run_visibility.lua` | Runs the two retail files, unmodified, against a scripted world. |
| `tools/retail_ref/visibility_world.py`, `gen_head_visibility.py`, `gen_model_shadow_source.py` | Build the worlds and scripts, run the reference, write the vectors. |
| `tools/retail_ref/cover_visibility.py`, `mutate_visibility.py` | Statement coverage of the Lua; mutation check of the C. |
| `tests/visibility_fake.h` | The same scripted world in C. |
| `tests/head_visibility_test.c`, `tests/model_shadow_source_test.c`, `tests/run-vectors-head-visibility.sh`, `tests/run-vectors-model-shadow-source.sh` | Replay the vectors. `tests/run-native.sh` picks the two scripts up. |
| `tests/vectors/head_visibility_vectors.h`, `model_shadow_source_vectors.h` | 32,154 and 16,625 operations with the retail answers. |

Regenerate (only when the retail reference is deliberately moved):

    python3 tools/retail_ref/gen_head_visibility.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
    python3 tools/retail_ref/gen_model_shadow_source.py --retail "<Goose Gadget Kit folder>" --out tests/vectors

## Method

- `run_visibility.lua` loads the retail bytes under `/usr/bin/luajit` with the chunk names the mod requires them by. `PORT51.kind` is `type`, `PORT51.number` is `tonumber`, `PORT51.ffi` is LuaJIT's `ffi` with a `load` that answers the tick clock and a `cast` that answers a handle's address. Head visibility is built as `runtime.lua:691-696` builds it: no `deps.memory`, no `deps.clock`, the real `model_shadow_source.lua` as `deps.model_shadows`. Engine handles are `void *` cdata.
- The world is a sparse byte memory, about 45 named numbers and 6 named texts (the snapshot fields, the options, failure switches), the list of SDK Unit handles and a table of named rig meshes. The backend, the engine and the options answer from it. `tests/visibility_fake.h` is the same world in C; both read the same script text, so there is one description of every operation.
- A script line is one operation: changes to the world, changes after `@N` that arrive with the N-th call of the operation (so a unit can retire, a mask can change, a function can be rebound or start failing in the middle of anything), and the call.
- Every call the module makes outside itself is an event: memory reads (`_read`, `_ptr`, `_u32`, `_u8`), handle addresses, `_alignment_context`, `_descriptor`, and by name every engine call with its arguments, every snapshot, backend, option, controller and clock call and every log line. All events go in order into two running checksums (modulo two 32-bit primes); the events that are not plain reads are also kept as text.
- The comparison is one text line per operation, built the same way on both sides: both results of the call, `result()` (18 fields), `status()`, `is_clean()`, `render_appearance()`, `sdk_handles()` and how often that table was replaced, every owned row (pass bits, before, written, last, current, flags), the ignore table, steady record, epoch, identity, update serial, enumeration cache and torso timer (read from the Lua closures' upvalues), the event count and checksums, and the named events. For the model module: the rows with every owner field, the signature, the remembered observation. Any difference fails the test. In the hand-written session and gate runs every part is printed in full; in the other runs a part longer than 64 characters is printed as its length and two checksums.
- On a mismatch the test prints both lines; `VIS_DUMP=<vector>` prints every call of that operation on the C side and `run_visibility.lua ... --dump <script line>` on the Lua side.
- Each test replays its vectors twice: with the retail numbers on the retail layout, and with build 25480438's numbers on a world whose build-dependent places are moved to where that build has them (see "Build data"). The moved world is described in the test a second time, not read from the table under test.

### `pairs` order

Both modules walk tables with `pairs` and make engine calls and memory reads inside those walks. Lua does not define that order, and in LuaJIT it differs from run to run (the string hash is seeded). The reference gives the modules a `pairs` that walks keys in ascending order (numbers, then texts by byte order), through the chunk environment; the file bytes are untouched. The C keeps the same tables sorted and walks them the same way; a walk that removes entries walks a copy of the keys and reads the live value, as the reference `pairs` does. So "same order of engine calls" means: the order the Lua source fixes, and ascending key order where the source leaves it to `pairs`. In the game the order inside one update (which mesh is written first, which of two faults is reported) is whatever the host's table order is.

## Engine functions

Everything the two files call on the engine, for the DLL wiring. `model_shadow_source.lua` calls none. A function returns false when the Lua call raised, with the text in `TfpEngineError`; a NULL pointer is a function the engine lacks.

| Engine function | `TfpEngineIface` field | Arguments | Result | Called at |
| --- | --- | --- | --- | --- |
| `Application.main_world()` | `Application_main_world` | none | World or nil | hv:391 (every full observation), hv:628 (every steady frame) |
| `World.units(world)` | `World_units` | World | array of Unit, at most 131,072 | hv:374, only when a wanted handle is missing, at most twice a second for an unchanged request |
| `Unit.set_mesh_visibility(unit, index, visible, pass)` | `Unit_set_mesh_visibility` | Unit; integer mesh index from 1; boolean; text `"default"` or `"shadow_caster"` | none | hv:186, from hide (hv:748) and restore (hv:225) |
| `Unit.has_mesh(unit, name)` | `Unit_has_mesh` | Unit (the rig); text `"g_l_shoulderpad"` or `"g_r_shoulderpad"` | boolean | hv:190 |
| `Unit.mesh(unit, name)` | `Unit_mesh` | as above | Mesh or nil | hv:193 |
| `Mesh.name(mesh)` | `Mesh_name` | Mesh | number, compared modulo 2^32 with 0x44E19D12 / 0xCB0E1712 | hv:194 |
| `Mesh.node(mesh)` | `Mesh_node` | Mesh | number, compared with 57 / 58 | hv:194 |
| `Mesh.num_materials(mesh)` | `Mesh_num_materials` | Mesh | number, must be 1 | hv:194 |
| `Mesh.has_material(mesh, name)` | `Mesh_has_material` | Mesh; text `"m_armor_standard"` | boolean | hv:195 |

The function identity of `Unit_set_mesh_visibility` is remembered while any pass bit is owned (hv:101, 185-187), and the steady path compares all nine pointers (hv:569-574, 613-614). The wiring must therefore fill the table once and leave the pointers alone.

### The rest of what the modules take from outside

| Retail | C | Notes |
| --- | --- | --- |
| `backend:_read(at,n)`, `_ptr(at)`, `_u32(at)`, `_u8(at)` | `read`, `ptr`, `u32`, `u8` of `TfpHeadIo` / `TfpModelShadowIo` | Addresses are `double`. A failed read is a raised error with the backend's text. |
| `backend:_alignment_executable(profile.pose)` | `TfpHeadIo.alignment_executable` | The executable base. Asked once per game base (hv:106). |
| `backend:_alignment_context{viewmodel=, avatar_owner=, avatar_index=}` | `TfpModelShadowIo.alignment_context` | Unit reference to object: `unit, index, generation, object, units, generations`. |
| `backend:_descriptor(world, id)` | `TfpModelShadowIo.descriptor` | 24 bytes; may be absent. |
| `backend:snapshot()` | `TfpModelShadowIo.snapshot` | Full snapshot. |
| `backend:player_snapshot()`, `pin_player_snapshot()` | `TfpHeadIo.player_snapshot`, `pin_player_snapshot` | `pin_player_snapshot` is asked first on every view snapshot (hv:58-61). |
| `backend:backpack(s)`, `action_state(s)`, `on_foot(s,state)`, `backend.foreign_camera()` | same names | |
| compat51 `ffi.cast('uintptr_t', handle)` (the host's address of an engine userdata) | `TfpHeadIo.userdata_address` | Unit: reference times 4 plus tag bits (hv:49). Mesh: the mesh object (hv:50). World: the address that holds the native world pointer (hv:51). This is the retail host's representation; what a HellForge handle is must be established by the wiring. |
| `kernel32 GetTickCount64()/1000` | `TfpHeadIo.clock` | Seconds. |
| `log`, `controller:result().mode=='pose_offset'`, `controller:is_enabled()` | `log`, `controller_result_is_pose_offset`, `controller_is_enabled` | |
| `deps.fallback_ready(s)`, `pinned_ready(s)`, `full_body()`, `hide_torso()`, `show_torso()`, `seated_head()` | same names | Each may be NULL. |
| `deps.model_shadows` | the `TfpModelShadow *` given to `tfp_head_new` | |

## Build data

The retail file's executable offsets and code pins are those of retail 26568081. They are in one table, `TfpHeadBuild`, with two instances. The vectors are cut from the Lua, so they are in the retail layout; the second replay gives the C build 25480438's table and moves the pins, the registry pointer, the two renderer functions and the appearance rows of the scripted world to that build's places, with that build's pin bytes where the retail bytes are intact. All vectors pass both ways (the `edges` run, 31 operations, is retail-only: it puts the avatar owner at the last readable addresses, where a row that starts 0x40 lower is a different question). The head test also refuses to start unless the four mesh pins of `src/feature_contract.h` and `FP_REGISTRY` of `src/profile.h` are the ones in the table, the two displacements inside `context_a` point at `context_default` and `context_shadow`, and the displacement inside `registry_code` points at `unit_registry`.

| Field | Retail | 25480438 | Source of the 25480438 value |
| --- | --- | --- | --- |
| `binding` (hv:113) | `exe+0x3f4ca0` `488bd1e9a8f8ffff` | `exe+0x4128e0` `488bd1e9e8fbffff` | `src/feature_contract.h` FP_FEATURE_MESH (image-checked there) |
| `mask_layout` (hv:114) | `exe+0x51fe80` | `exe+0x50f620`, same 14 bytes | `feature_contract.h` |
| `context_a` (hv:115) | `exe+0x3f4c23` | `exe+0x412875` `3b3d35bd3b02...3b3d41bd3b02...` | `feature_contract.h` |
| `context_b` (hv:116) | `exe+0x51feaa` | `exe+0x50f64a`, same 26 bytes | `feature_contract.h` |
| `context_default`, `context_shadow` (hv:117) | `exe+0x2875ac0`, `exe+0x2875ac8` | `exe+0x27ce5b0`, `exe+0x27ce5c8` | `docs/BUILD-DATA-25480438.md`; they are also the two addresses `context_a`'s own displacements point at. Values 0xF2760503 / 0x3100C3D2 as in retail and in the kit's own test for this build; not read from the image here. |
| `registry_code` (hv:119) | `exe+0xa06d0` `...488b351270a101` | `exe+0x9d8c0` `...488b351a289701` | `BUILD-DATA-25480438.md` and the kit's own test for this build; its displacement resolves to `exe+0x1a100f0`. Bytes not read from the image here. |
| `unit_registry` (hv:123) | `exe+0x1ab76f8` | `exe+0x1a100f0` | `src/profile.h` FP_REGISTRY |
| `renderer_method_6`, `renderer_method_0` (hv:142) | `exe+0x28d3e0`, `exe+0x293600` | `exe+0x2bd890`, `exe+0x2c3990` | `BUILD-DATA-25480438.md`, the kit's own test |
| `appearance` (hv:393, ms:33) | `0x546ab0` | `0x546a70` | `BUILD-DATA-25480438.md` |

Structure layouts are taken as the same in both builds (unit +0x08, +0x18, +0x70, +0xC8, +0xD0, +0x1D0, +0x1D8, +0x1E0, +0x358; mesh +0xDC; registry +0x88; method table +0xC8; appearance row 0x1B8 with the references at +0xDC), as the kit's own test for 25480438 and the other translated modules have them.

## Carried over from Lua

| Lua | C |
| --- | --- |
| A raised error and `pcall` | `longjmp` to the nearest `protect()`; the text is in the module struct. Callbacks return false with the text. Work tables made inside a protected call are objects on the module's list, so nothing is lost when a call is left that way. |
| `assert(cond, text)` | LuaJIT puts `<chunk>:<line>: ` before the text, and these texts reach the log, `status()` and `result()`. The C builds the same text from the retail line. A nested `assert(not problem, problem)` (hv:235, 487, 532) repeats the prefix, as in Lua. |
| Calling a function the engine does not have (hv:190-195) | The LuaJIT text `...:<line>: attempt to call field 'has_mesh' (a nil value)`; a missing `player_snapshot` gives `attempt to call a nil value` through `invoke`. |
| Garbage-collected tables shared between owners (a row is in `owned`, a spec's `rows`, `wanted`, the steady record and the history) | Objects on one list, freed by mark and sweep at the end of each public call. Table identity (`wanted[key]~=row`, `spec.layout~=layouts[ref]`, `saved.observed==observed`) is pointer identity. |
| `nil` against `false` against 0 | `has_*` flags. `handles[ref]=false` (a reference two units claim) is an entry with `flag` false. |
| Keys `tostring(ref)..':'..tostring(mesh)` | The same text, `"%.14g"` each, so a mesh address of 15 digits loses its last digit exactly as in Lua. |
| Lua numbers | `double` for every address, reference, mask and counter; `%` is `a - floor(a/b)*b`. |
| `sdk_handles()` table identity as a change trigger for arm_pieces and grip_hand | `tfp_head_sdk_handles` gives a number that changes whenever the Lua replaces the table (hv:468, 487). |
| `fingerprint.inspected`, `rows`, signature | Arrays owned by the model struct; valid until the next `probe`. |

## Dropped, with the reason

| Retail | Why it cannot be reached |
| --- | --- |
| `bridge_address` fast path (hv:27-39) | One `userdata_address` callback; the fast path returns the same number. The reference runs it both ways (runs `session_bridge`, `session_bridge_differs`). |
| `deps.memory`, `deps.clock`, `os.clock` (hv:43-54) | `runtime.lua:691` passes neither; the C has the one clock callback. |
| `Head.new` assert (hv:41), default `log` | Constructor precondition; `log` may be NULL. |
| `assert(kind(value)=='string' and #value==n)` (hv:80), `'Customization integer unavailable'` (hv:84), `'Incomplete model field'` (ms:5) | A read callback fills exactly `n` bytes, and every caller passes a buffer of the needed length. |
| `'Named rig shoulder query unavailable'` (hv:191), `'Rig shoulder metadata unavailable'` (hv:196) | The C types cannot hold a result of the wrong type. A wiring that can see one should report it as a failed call. |
| `pcall(memory.handle_ref, handle)` failing in the inventory (hv:377) | `userdata_address` cannot fail. |
| `sr.Application`, `sr.World`, `sr.Unit`, `sr.Mesh` being absent as tables | The C has function pointers only; a missing table is all of its functions NULL. |
| Seven of the twenty steady bindings (hv:569-574): the four `sr` sub-tables and the three `memory` closures | No C counterpart; the nine engine pointers and four backend pointers are compared. |
| `deps.model_shadows==saved.models`, `kind(models.unchanged)=='function'` (hv:580, 612) | The model module is linked in. |
| Option getters and the clock raising; snapshot fields of the wrong type | Callbacks return values. Absent fields are kept (`has_*`), as are a missing identity, flags that are not 24 bytes and controller bytes shorter than 6. |
| `self.preserved_partial` | Always 0 in retail (hv:490, 540); kept as a field. |

Kept although dead on the shipped path: `pinned_ready` true, `full_body`, `hide_torso` (inventory, section on this module); they are exercised.

## Counts

Head visibility: 32,154 operations in 26 runs, each replayed on both layouts (32,123 on the second).

| Run | Operations | What |
| --- | --- | --- |
| `session`, `session_plain` (no log, no model shadows), `session_no_sdk` (no stingray table), `session_bridge`, `session_bridge_differs` | 18 each | Start, steady frames, stop, restart, a new epoch, shutdown. |
| `gates` | 260 | Every condition of "is this first person": controller, foreign camera, kinds 4/5/6/7, seated, unsafe with and without transition flags, both snapshot functions failing or missing, every option missing, the kind-4 fallback with action state and on-foot variants, flags that are not 24 bytes, another player. |
| `armour` | 144 | Clothing changed in the middle; handles missing, late, doubled, tagged; the half-second enumeration limit; a bad clock; meshes none, 128, 129, reordered, replaced, doubled; retired and reused generations; wrong self reference, renderer type, registry capacity. |
| `refusals` | 284 | Every assertion of the appearance table and the attachments; pointer limits; the world missing, changed, moved; each code pin wrong; unreadable memory. |
| `rig` | 920 | The shoulder pads by name: each metadata field wrong, each of the six rig functions failing, missing or rebound (also while pads are owned), other rigs, the retry half second. |
| `torso` | 128 | Kind 4, the show-torso option and its 0.6 s, `full_body` and `hide_torso`. |
| `masks` | 443 | Pass bits changed from outside, inside and outside the 0.25 s after an epoch begins, every epoch field, natively hidden meshes, the remembered history. |
| `restores` | 475 | Leaving: what the game republished, units gone, the setter failing before and after its effect or doing nothing, rebound setters, partial hides given back. |
| `bindings`, `handles`, `edges` | 140, 73, 31 | Rebinding each of 13 functions; handle addresses that move; the avatar owner at the ends of memory. |
| `targeted` | 7,097 | Changes that arrive with one particular call: each `set_mesh_visibility`, each snapshot, handle addresses, each other engine and option call; in apply, steady, stop and the gap view. |
| `sweep_apply_0..3`, `sweep_steady`, `sweep_stop`, `sweep_gap` | 5,808 + 4,116 + 4,851 + 1,294 | 121 different changes, each arriving at every 61st / 23rd / 67th / 97th call of a hiding update, a steady frame, a stop and a kind-4 update. |
| `walk1`, `walk2`, `walk3` (no log, no model shadows) | 2,500 + 2,500 + 1,000 | Seeded sessions: aiming, swapping, clothing and weapon parts changing, sitting, another player, the mode switched, and a change in the middle of about every 17th operation. |

Model shadow source: 16,625 operations in 7 runs, each replayed on both layouts.

| Run | Operations | What |
| --- | --- | --- |
| `basics` | 189 | Every method; every owner field wrong in `valid`; stale and short appearance; arm slots holding the rig, the body, nothing; what an observation needs to be remembered; weapon trees too deep, too many, ringed, pointing back at the weapon; registry faults. |
| `sweep_probe`, `sweep_unchanged`, `sweep_valid` | 8,001 + 1,851 + 1,191 | 50 changes, each arriving at every call of a `probe`, an `unchanged` and a `valid`. |
| `bare` | 393 | No arm pieces, no weapon parts, no descriptor function, the rig's reference below the body's. |
| `walk1`, `walk2` | 2,500 each | Seeded observations with parts and arm pieces changing and changes in the middle of calls. |

## Coverage

`cover_visibility.py` splits a scratch copy of each retail file to one statement per line and replays every script under a line hook.

- `head_visibility.lua`: 747 of 748 statements reached. Not reached: the `return` of hv:595 (`if row.last==nil then return end` in `retain_steady`). It cannot be: `retain_steady` only runs after the hide loop has ended without an error, and that loop sets `row.last` on every wanted row (hv:754).
- `model_shadow_source.lua`: 135 of 135 statements reached.

Statements, not conditions: the halves of an `and` / `or` chain are not counted separately. Known half that no vector takes: a `World.units` list longer than 131,072 entries (hv:374). Asserts whose failing side is dropped in C (see "Dropped") are reached on their passing side only.

## Mutation

`mutate_visibility.py` makes one change at a time in a scratch copy of the C file (comparison and logic operators, constants, `true`/`false`, `+`/`-`, dropped statements), builds it with the vector test and the test script's sanitizers, and runs it.

| File | Possible changes | Tried (two seeded batches) | Did not compile | Killed |
| --- | --- | --- | --- | --- |
| `src/head_visibility.c` | 1,916 | 600 | 1 | 547 of 599 (91.3%) |
| `src/model_shadow_source.c` | 554 | 400 | 1 | 357 of 399 (89.5%) |

Earlier batches with other seeds found real gaps, and each was closed with new operations before these two: pin bytes and offsets of the 25480438 table (the moved world took its numbers from the table under test), the read bounds and the two ends of a valid pointer, 128 meshes, a unit without a mesh array, a mesh shared with the body, registry capacity 1, reference bits above the generation byte, a rig whose reference is below the body's, a handle address that moves, the binding changing during the setter call, the remembered history of a natively hidden mesh, a native world that moves under the same SDK world, exactly half a second between enumerations and exactly 0.6 s of torso settling, flags that are not 24 bytes, `full_body` with an observation still held, the pinned option absent in other views, the backpack being the torso, a renderer swapped under a steady frame, the missing-function texts for all six rig functions, a snapshot that fails on the first call, a weapon part that names the weapon as its child. Two survivors of the last head batch (the edges of the rig's retry half second) were a script that never reached the retry; it was corrected and both changes now fail the test (checked by hand).

The 52 + 42 survivors of the last batches, by kind; none changes what the code does for any input the interface can give:

- Buffer, array and allocation sizes, growth factors, initial values that are always overwritten, `memset` fill of a struct that is then assigned (more than half of them).
- Line numbers and texts of errors nobody reads: everything inside `valid()` (ms:113-126 returns only true or false), inside `observe_steady` (hv:672 drops the text), inside the rig's protected `collect` (hv:341, 342).
- Guards that never trigger: `if (!live) continue` in the stop loop, the NULL-map guard of a lookup, the early exit of the sorted search, a loop bound one past an array that was allocated one longer.
- Conditions that cannot differ: `spec.rig and ref==s.viewmodel` (a rig spec is only ever made for `s.viewmodel`); the second half of hv:342 (two names resolving to one mesh with both pads' metadata; a mesh has one name hash); `assert(f.inspected[f.rig])` (ms:103; the rig is always inspected); the `return` of hv:595; mask 254 in `native_reset` (a mesh at 254 is never out of line with what is wanted on a row that asks); `full_body` without `hide_torso` in the kept-limb removal (no torso spec exists then); the `kind~=6 or not safe` pair of `retain_steady` (an unsafe kind 6 has no model observation, which returns two terms later).
- Not noticed by design of the test: dropped `free` calls and a dropped link in the object list (the scripts run with leak detection off, like the other vector tests); `count_owned()` inside `status()` (the test asks `result()` first, which counts too); a flag set on raise that nothing reads.

## Differences found while matching

- `probe` with a full snapshot that has no `identity` text: the first C draft returned the rows. Retail fails at ms:79 in `table.concat` (`invalid value (nil) at index 1 in table for 'concat'`), after the observation has been remembered, so `has_observation()` is true although `probe` returned nil. Carried over.
- Nothing else in behaviour: the first C draft of `head_visibility.c` answered all vectors that existed then. The same draft had two dead leftovers the mutation check found (an unused flag, a redundant reset) and they were removed.
- One fault was in the reference runner, found by the C: its "function absent" switch bound the alternative function instead.
- A property of retail made visible by the vectors, kept as it is: a rig pad that cannot be identified when the mode leaves (its name query fails or answers another mesh) is counted as `retired` and is not restored; the mesh stays hidden from camera and shadow until something else publishes it.

## Open issues

1. Table order. See "`pairs` order": inside one update the order of mesh writes, and which of several faults is the one reported, is the reference's ascending order, not the game host's.
2. Texts. The `<chunk>:<line>:` prefix of assertion texts and the wording of the "attempt to call" and `concat` errors are LuaJIT's with the required path as chunk name. The kit's own host is Lua 5.1 with cffi, whose `assert` adds no position. These texts go to the log, `status()` and `result().error_message`; nothing branches on them except "same text as last time" in `report()` (hv:75) and the model signature comparison when `probe` fails (hv:428: the same error text twice counts as unchanged).
3. Build 25480438. The table has a value for every field, but three of them rest on the kit's migration records and the kit's own old test, not on the image: the bytes of `registry_code`, and the two context ids. The four pins that matter for the setter are in `feature_contract.h`. `tools/verify_profile.py` does not read `TfpHeadBuild`.
4. Handles. `userdata_address` is the retail host's view of an engine handle (unit reference times 4, a world handle that holds the native world pointer). Whether a HellForge mod can give the same three numbers for a Unit, a Mesh and a World is not established here.
5. Callbacks cannot raise where Lua functions can: the option getters, the clock, the controller. Engine results of the wrong type cannot be represented.
6. Text caps: 512 bytes for an error text, 256 for an identity, 256 for an engine error text. Lua has none. The longest text in the vectors is under 160 bytes.
7. The scripted backend is my reading of `backend.lua` (`_read`, `_ptr`, `_alignment_context`, `_descriptor` and their texts). It decides which paths are reached, not what is compared: every answer is still retail's.
8. Not covered by any vector: a `World.units` list longer than 131,072 (hv:374, second half of the condition).
