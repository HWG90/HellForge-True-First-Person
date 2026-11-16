# Arms: arm_pieces.lua and grip_hand.lua in C

Translation of two retail files of True First Person 0.5.41-smooth.66 (commit 1d0d453): `src/first_person/arm_pieces.lua` (284 lines, the sleeves) and `src/first_person/grip_hand.lua` (691 lines, the hands on the weapon). The Lua files are the authority. Each C file is checked by replaying what the retail file, run unmodified under `/usr/bin/luajit`, answered and asked its engine. The recipe is the one of `docs/POSE-ADAPTER-TRANSLATION.md`, with one addition: these two modules talk to the engine, so the engine's side of the conversation is recorded too.

## Files

| File | What |
| --- | --- |
| `src/arm_engine_iface.h` | The host: `TfpArmEngine` (one pointer per engine function), `TfpArmBackend` (the four backend readers and the clock), the snapshot and the handle table. |
| `src/arm_engine_calls.h` | Private to the two translations: one wrapper per pointer, the `pcall` (setjmp/longjmp) and the texts of Lua's own runtime errors. |
| `src/arm_pieces.h`, `src/arm_pieces.c` | arm_pieces.lua. |
| `src/grip_hand.h`, `src/grip_hand.c` | grip_hand.lua. |
| `tools/retail_ref/arms_common.lua` | The scripted fake engine: a small world for the stateful functions, float value maths, the call recorder. |
| `tools/retail_ref/run_arm_pieces.lua`, `run_grip_hand.lua` | Load the retail file unmodified and replay scripts against it. |
| `tools/retail_ref/arms_gen.py`, `gen_arm_pieces.py`, `gen_grip_hand.py` | Write the scripts, run them, emit the vectors. Refuse any retail commit but 1d0d453. |
| `tests/vectors/arm_pieces_vectors.h` | 16,173 operations in 49 runs; records the commit and the SHA-256 of `arm_pieces.lua`. |
| `tests/vectors/grip_hand_vectors.h` | 13,809 operations in 75 runs; records the commit and the SHA-256 of `grip_hand.lua`. |
| `tests/arms_fx.h` | The C side of the fake engine: hands the recorded answers out and requires the recorded calls. An `engine` operation takes functions away from the engine, or gives them back, in the middle of a run. |
| `tests/arm_pieces_test.c`, `tests/grip_hand_test.c`, `tests/run-vectors-arm-pieces.sh`, `tests/run-vectors-grip-hand.sh` | Replay the vectors against the C. `tests/run-native.sh` picks the scripts up by their names. |

Regenerate (only when the retail reference is deliberately moved):

    python3 tools/retail_ref/gen_arm_pieces.py --retail "$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit" --out tests/vectors
    python3 tools/retail_ref/gen_grip_hand.py --retail "$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit" --out tests/vectors

Regeneration is byte-identical run to run.

## Interface

- `TfpArmEngine`: 40 function pointers, one per engine function either file calls, named `Table_function`. Each returns false when the Lua call raised, with the text. A NULL pointer is a function the engine does not have; a table none of whose functions is set is a missing table. The full list is in the next section.
- `TfpArmBackend`: `object` (`backend:_alignment_context({viewmodel=ref, avatar_owner=..., avatar_index=...}).object`), `read`, `ptr`, `u32` (`backend:_read / _ptr / _u32`) and `clock` (the `deps.clock` that `runtime.lua:701,704` gives both modules).
- `TfpArmSnapshot`: the fields of `s` that are read: `body`, `viewmodel`, `avatar_owner`, `avatar_index` (both files), `aiming`, `shoulder_aim`, `weapon`, `request_weapon`, `weapon_transition` (grip_hand). NULL is "not a table".
- `TfpArmHandles`: the `{[unit reference] = Unit}` table of `head:sdk_handles()`: rows in the order they are to be walked, and `identity`, which stands for the Lua table object (both files compare the table itself, `identity.handles~=handles` / `seen_handles~=handles`, and do not notice new rows in the same table).
- arm_pieces: `tfp_arm_pieces_module_init` (the module table; see "SIDES" below), `tfp_arm_pieces_init` (`M.new`), `_update`, `_stop`, `_is_clean`, `_result`.
- grip_hand: `tfp_grip_hand_init` (`M.new`), `_update`, `_stop`, `_is_clean`, `_result`, and the two module functions `tfp_grip_hand_solve` (`M.solve`) and `tfp_grip_hand_weight` (`M.weight`).
- Neither file writes a log line, so there is no logger. What a caller sees is `result()`: the counters and `reason` (grip_hand also `right`), whose texts are retail's.

## Engine functions

`node` and `index` count from 1 and are Lua numbers (`double`). Unit, World and Mesh are opaque handles; Vector3 (3 floats), Quaternion (4 floats, x y z w) and Matrix4x4 (16 floats) are opaque values the C never looks into. Lines are retail's; "ap" is arm_pieces.lua, "gh" is grip_hand.lua.

| Engine function (Lua) | Pointer in `TfpArmEngine` | Arguments -> result | Called at |
| --- | --- | --- | --- |
| `Application.main_world()` | `Application_main_world` | -> World or nil | ap 100, 163 |
| `World.link_unit(world, child, child_node, parent, parent_node)` | `World_link_unit` | World, Unit, number, Unit, number | ap 104, 166 |
| `Unit.alive(unit)` | `Unit_alive` | Unit -> boolean (compared with `true`) | ap 50; gh 198, 257 |
| `Unit.num_scene_graph_items(unit)` | `Unit_num_scene_graph_items` | Unit -> number | ap 50; gh 198 |
| `Unit.world_pose(unit, node)` | `Unit_world_pose` | Unit, number -> Matrix4x4 | ap 241, 242, 253; gh 333, 334, 335, 349, 353, 461, 548, 551, 570, 578, 581, 606, 607, 610, 612 |
| `Unit.world_position(unit, node)` | `Unit_world_position` | Unit, number -> Vector3 | ap 189, 190, 191, 251, 252, 259; gh 497, 499, 502, 535, 545, 563, 605 |
| `Unit.world_rotation(unit, node)` | `Unit_world_rotation` | Unit, number -> Quaternion | ap 260; gh 432, 546, 547, 577, 609 |
| `Unit.set_local_pose(unit, node, pose)` | `Unit_set_local_pose` | Unit, number, Matrix4x4 | ap 89, 262; gh 303, 312, 412, 464, 466, 586 |
| `Unit.num_meshes(unit)` | `Unit_num_meshes` | Unit -> number | gh 486 |
| `Unit.mesh(unit, index)` | `Unit_mesh` | Unit, number -> Mesh or nil | gh 487 |
| `Unit.set_mesh_visibility(unit, index, visible)` | `Unit_set_mesh_visibility` | Unit, number, boolean (no pass name) | gh 480, 489 |
| `Mesh.visibility(mesh)` | `Mesh_visibility` | Mesh -> boolean (compared with `true`) | gh 488 |
| `Mesh.num_materials(mesh)` | `Mesh_num_materials` | Mesh -> number | gh 488 |
| `Vector3(x, y, z)` | `Vector3_call` | number x3 -> Vector3 | ap 194, 196; gh 385, 387, 507, 604 |
| `Vector3.length(v)` | `Vector3_length` | Vector3 -> number | ap 177, 192, 197, 243; gh 138, 140, 141, 143, 400, 505, 507, 535, 573 |
| `Vector3.dot(a, b)` | `Vector3_dot` | Vector3 x2 -> number | ap 177, 181; gh 148, 400 |
| `Vector3.cross(a, b)` | `Vector3_cross` | Vector3 x2 -> Vector3 | ap 176; gh 142, 147, 399 |
| `Vector3.normalize(v)` | `Vector3_normalize` | Vector3 -> Vector3 | ap 179, 193, 198; gh 144, 147, 398, 402, 438 |
| `Vector3.lerp(a, b, t)` | `Vector3_lerp` | Vector3 x2, number -> Vector3 | gh 392, 545, 580, 611 |
| `Vector3.x(v)`, `.y(v)`, `.z(v)` | `Vector3_x`, `_y`, `_z` | Vector3 -> number | gh 318, 336, 440, 445, 509 |
| `a + b`, `a - b` on Vector3 | `Vector3_op_add`, `Vector3_op_sub` | Vector3 x2 -> Vector3 | ap 189-191, 243, 259, 181; gh 138-142, 148, 149, 433, 434, 438, 505, 507, 535, 573 |
| `v * number`, `-v` on Vector3 | `Vector3_op_mul`, `Vector3_op_unm` | Vector3, number -> Vector3; Vector3 -> Vector3 | ap 181; gh 148, 149 |
| `Quaternion.identity()` | `Quaternion_identity` | -> Quaternion | ap 202; gh 401, 442 |
| `Quaternion.axis_angle(axis, radians)` | `Quaternion_axis_angle` | Vector3, number -> Quaternion | ap 178, 179; gh 402 |
| `Quaternion.multiply(second, first)` | `Quaternion_multiply` | Quaternion x2 -> Quaternion | ap 201, 260; gh 436, 437, 453, 454 |
| `Quaternion.rotate(q, v)` | `Quaternion_rotate` | Quaternion, Vector3 -> Vector3 | ap 199, 259; gh 434 |
| `Quaternion.lerp(a, b, t)` | `Quaternion_lerp` | Quaternion x2, number -> Quaternion | gh 391, 547, 579, 611 |
| `Quaternion.inverse(q)` | `Quaternion_inverse` | Quaternion -> Quaternion | gh 437 |
| `Quaternion.to_elements(q)` | `Quaternion_to_elements` | Quaternion -> x, y, z, w | gh 317, 439 |
| `Quaternion.from_elements(x, y, z, w)` | `Quaternion_from_elements` | number x4 -> Quaternion | gh 385, 387, 445, 604 |
| `Matrix4x4.identity()` | `Matrix4x4_identity` | -> Matrix4x4 | ap 89; gh 303, 312, 412 |
| `Matrix4x4.from_quaternion_position(q, p)` | `Matrix4x4_from_quaternion_position` | Quaternion, Vector3 -> Matrix4x4 | ap 261; gh 385, 387, 391, 449, 450, 457, 586, 604 |
| `Matrix4x4.multiply(a, b)` | `Matrix4x4_multiply` | Matrix4x4 x2 -> Matrix4x4 | ap 241, 261; gh 334, 335, 349, 353, 388, 450, 464, 466, 551, 571, 581, 586, 606, 607, 612 |
| `Matrix4x4.inverse(m)` | `Matrix4x4_inverse` | Matrix4x4 -> Matrix4x4 | ap 253; gh 333, 349, 353, 388, 449, 464, 466, 548, 578, 586, 606, 607, 610 |
| `Matrix4x4.translation(m)` | `Matrix4x4_translation` | Matrix4x4 -> Vector3 | ap 241, 242; gh 317, 335, 392, 572, 611 |
| `Matrix4x4.rotation(m)` | `Matrix4x4_rotation` | Matrix4x4 -> Quaternion | gh 317, 391, 579, 611 |
| `Matrix4x4Box(m)`, `box:unbox()`, `box:store(m)` | none | The C keeps the matrix by value; see "Dropped". | ap 241, 264; gh 552, 554 |

Backend and clock (`TfpArmBackend`):

| Lua | Pointer | Arguments -> result | Called at |
| --- | --- | --- | --- |
| `backend:_alignment_context({viewmodel=ref, avatar_owner=s.avatar_owner, avatar_index=s.avatar_index}).object` | `object` | unit reference, owner, index (each may be nil) -> address | ap 53; gh 209 |
| `backend:_u32(address+0x70)` | `u32` | address -> number | ap 61; gh 243, 260 |
| `backend:_ptr(address+0xA0)`, `_ptr(address+0x98)` | `ptr` | address -> address | ap 63, 73; gh 213, 236, 245, 252, 262, 268 |
| `backend:_read(at, size)` | `read` | address, size (8, 252 or count*4 <= 1020) -> that many bytes | ap 56, 63, 73; gh 201 (through `words`), 266 |
| `clock()` | `clock` | -> seconds | ap 212; gh 628 |

What the wiring has to keep: the engine's own conventions as the Lua comments state them, "checked live" on retail: `Quaternion.multiply(second, first)` applies first, then second (ap 183, gh 431); a bone linked with `World.link_unit` keeps the local pose it is given as an offset from its link target (ap 85, gh 12); the game's animation runs after the kit's update, so writes to the rig's own bones are lost (gh 10).

## Carried over from Lua

| Lua | C |
| --- | --- |
| `pcall(function() ... end)` around `update` and `stop`, and the inner ones of grip_hand's lookup (gh 241, 258) and stop (gh 666, 667, 670) | `arm_protect`: a raised error is a longjmp to it. State written before the error stays written, as in Lua: every assignment is made in retail's order. |
| The order of engine calls inside one expression | Lua evaluates the function value, then the arguments left to right, then calls. Nested calls are written out one per statement in that order. |
| An engine function that is nil | The pointer is NULL. The call fails where Lua's does, after its arguments were evaluated, with LuaJIT's text: `src/first_person/grip_hand.lua:505: attempt to call field 'length' (a nil value)`. Each call site carries its retail line. |
| `assert(... ,'Arm piece SDK unavailable')`, `'Grip hand SDK unavailable'`, `'Arm piece bone count out of range'` | The same texts with `<chunk>:<line>: ` in front, as the reference LuaJIT raises them (43, 191, 62). See "Reference runtime" below: stock Lua 5.1 puts nothing in front. |
| `piece.side.rig_shoulder+1` when the module-level shoulder is nil (ap 166, 241) | `attempt to perform arithmetic on field 'rig_shoulder' (a nil value)` at the same line. Reached when a later lookup fails half way, or when another instance looked up a rig without shoulders. |
| `links[at]` beyond the 63 rig bones (gh 225) | `attempt to index local 'link' (a nil value)`. |
| `nil` | `has_*` flags; a NULL Unit; `aim` is -1 / 0 / 1; `right` of `result()` is NULL until the first update. |
| `SIDES` (ap 27-30): a module-level table that `scan` and `update` write into | `TfpArmPiecesModule`, given to every instance. Two instances of one module share the shoulders found by whichever looked last, as in Lua (run `shared`). |
| `identity.homes[ref][bone.index] = home or false` (ap 152-156) | An array by piece of (index, has_home, home). |
| `known` (gh 180), weak-keyed by weapon unit, and `shapes`, a table shared between `known[handle].shapes` and the upvalue | `known[]` by handle and a pool of `TfpGripShapes`; `shapes` is an index into the pool, so a remembered weapon and the upvalue name the same set, and `shapes={}` takes a set nobody holds. |
| `current` (a candidate table) compared by handle and node; `grip~=kept` by identity | `current` is (handle, node); `grip` and `kept` are indices into the same list. |
| `math.min`, `math.max` | `a < b ? a : b` and `a > b ? a : b`: the reference LuaJIT's, where a NaN in the second place wins. |
| `x^2` (gh 340-342, 357, 366) | The C library's `pow(x, 2)`, called through a volatile pointer. LuaJIT's interpreter calls `pow` too, and glibc's `pow(x, 2)` is not always `x*x` (4,083 of 5,000,000 random values differ); GCC and Clang would fold the call to `x*x`. |
| `a*b + c*d` in doubles (gh 145, 146, 342, 440, 441, 608) | Each product goes through a `volatile`, so no compiler setting fuses it with the addition. Found by the test under `-march=native`. |
| `tostring(s.weapon or s.request_weapon)..' moving'` (gh 638), only ever compared | The same text, `%.14g`. |
| `for index=1,U.num_meshes(rig)` | A loop on a `double`; a NaN or negative count makes no round. |
| `local calm=1` (gh 655) and the `calm<=0` texts | Kept: the parameter is passed and `'weapon is being handled'` is in the C, unreachable as in Lua. |
| `held_at`, `held_time`, `busy_until`, `steady`, `ARM_PARENT`, `self.handled/turning/sliding` | Never change in retail: `result()` reports `steady=1`, the three counters 0. No state is kept for them. |

## Dropped or different, with the reason

| Retail | C | Why |
| --- | --- | --- |
| `sr.Matrix4x4Box` (`bind` asserts it exists; a box holds the chest's last pose and each finger shape across frames) | The matrix is stored by value in the instance. | A box only exists because an engine value does not outlive its frame in Lua. A missing `Matrix4x4Box` therefore cannot fail the SDK check. |
| A table that exists but is empty (`sr.Vector3 = {}`) | Read as a missing table: `SDK unavailable`. | The host is a table of pointers; "all NULL" is the only way to say the table is not there. A table with at least one function is present, as in Lua (runs `only_*`). |
| `sr.Application == nil` | `attempt to call field 'main_world' (a nil value)` instead of `attempt to index field 'Application'`. | Same reason; `Application` has one function. |
| The four Vector3 operators | Must always be set; if one is NULL both modules answer `SDK unavailable`. | They are metamethods of the engine's value, not fields that can be nil. |
| `deps.clock or os.clock`; a backend without `_read` etc. | `clock` and the four readers must be set. | `runtime.lua` always gives a clock. |
| An engine function that returns nil or another type where a value is expected | The typed result cannot say so; the host should report it as a raise. | The text of the error would be Lua's own (`attempt to perform arithmetic on a nil value`), from a line the C cannot know. |
| `s`, `handles`, a handle or a snapshot field of another type | Only table / nil, number / nil, `true` / anything else. | The C types cannot hold more. Each takes the path of the nil case. |
| `backend:_read` returning fewer bytes than asked | Always `size` bytes or a raise. | `backend.lua:65-74` asserts the length. |
| No limits on any table | Limits: 32 pieces (arm_pieces); 32 grip candidates, 32 pieces, 1024 offset bones, 256 hidden meshes, 32 remembered weapons (grip_hand). Beyond them: arm_pieces raises `Arm piece table full` (reason of that update); grip_hand skips the unit that does not fit (the error is inside the per-unit pcall), raises `Grip hand table full` for the meshes, and forgets the oldest remembered weapon. | Fixed storage. Recorded live: 4 pieces, 44 bones, 2 grip bones, one 2-material mesh. |
| `known` is weak-keyed: an entry goes when the Unit userdata is collected | An entry stays until `stop()` or until 32 newer weapons were held. | No garbage collector. A handle value reused by the host for another unit would meet a stale entry only if grip bone index and bone count match too. |
| Order of `pairs(handles)` | The order of the rows. | Lua does not define it. grip_hand's results depend on it in retail too (first of two candidates at equal distance, last unit linked to the weapon bone). The vectors hand the C the rows in the order the reference LuaJIT walked them. |

## Proof

`tests/run-vectors-arm-pieces.sh`:

    Arm pieces: 16173 operations in 49 runs answered as retail's arm_pieces.lua (1d0d453): same results, reasons, counters and engine calls; 14094 updates, 6321 placing, 1348 raised; 483938 engine, backend and clock calls with their arguments, 604159 value-maths calls by checksum

`tests/run-vectors-grip-hand.sh`:

    Grip hand: 13809 operations in 75 runs answered as retail's grip_hand.lua (1d0d453): same results, reasons, counters and engine calls; 11214 updates, 4233 with the left arm corrected, 7310 with the right, 2181 raised; 604 calls of solve; 919131 engine, backend and clock calls with their arguments, 1776929 value-maths calls by checksum

How an operation is compared. The retail file runs against the fake engine of `arms_common.lua`, which has two halves:

1. **Stateful functions** (Unit, World, Mesh, Application, the four backend readers, the clock) answer from a small world (units with bone names, link words, parent pointers, poses, engine-side links, meshes) that the script edits between operations. Every call is recorded: function, every argument, the answer or the error text. The C test has no world. `tests/arms_fx.h` requires that the C's next call is the recorded one, compared as text with every float and double as its bits (Unit ids, node numbers to 17 digits, the 7 floats of a pose in hex), and hands back the recorded answer. An extra call, a missing call, a call out of order or one differing bit fails the operation.
2. **Value maths** (Vector3, Quaternion, Matrix4x4, the operators) is a pure function of its arguments, written twice, in Lua and in C, on 32-bit floats as the engine's values are (a matrix is a rigid pose; `multiply(a, b)` is "a, then b", the model of the kit's own tests). Every call is folded with its function number, its argument bits and its result bits into one running 32-bit checksum per operation, and the count and checksum must agree. So the order and the bits of all 604,159 + 1,776,929 maths calls are compared without storing them. What the maths produced is also visible exactly in the next `set_local_pose`.

After each operation the answer is compared as one text: the return value, every field of `result()` (distances as double bits), `is_clean()`, and the reason texts.

Runs of arm_pieces (16,173 operations): `basic` (every public function; found, hung, placed; the game re-linking a chest; leaving the view; hand-back at stop; no snapshot, no table, no body, no rig; 2 cm tolerance from both sides), `timing` (the 0.6 s window, the quarter-second lookups, the confirming lookup, pieces arriving in the same table, clocks that fail, stand still, run back, are NaN or infinite, the limits to the last bit), `armour_change` (the table refilled in place, another table, pieces leaving, the body's or the rig's handle changing under one reference, one unit under two references, another armour on another rig), `vanish` (pieces, body and rig dying or changing size, in and out of the view, at stop; handles leaving the table; no world), `missing_bones` (each named bone missing from body and rig, a shoulder lost after the pieces were found, names twice, bone counts 0 / 255 / 256 / 2^32-1, link modes, parents that differ in one of six bytes, every memory read failing for every kind of unit, bones gained before the confirming lookup), `geometry` (shoulders or spine too short, either side of every limit, no turn, a half turn, no roll, no up, NaN and infinite poses), `errors` (every engine and backend function failing at every one of its calls in a first update, a steady one, a re-hang, a release, a stop and a confirming lookup; the value maths failing at each of its calls), `shared` (two instances), `vanishing` (every function and table going missing in the middle of a session, before an update, a release and a stop), `without_*` (27 runs: the tour with one engine function or one table missing from the start), `only_*` (9 runs: a Vector3 table with a single function), four random walks of 2,500 operations.

Runs of grip_hand (13,809 operations): `module_functions` (`solve` on 335 argument sets: reach, stretch limit, straight arm, zero-length bones, NaN, infinity, random; every maths call of it failing; `weight` at and around its limits), `basic`, `grip_logic` (easing in, a bone never held, staying on a bone the body's hand left, 2 cm from both sides, the 20 / 32 cm blend, a foregrip, a grip on the back, the remembered grip after leaving the view and after a swap, the hand trailing the bone, a body without a left hand, an arm that cannot bend, clocks that stand still or run back), `weapon_swap` (weapon id and transition edges, the three delayed lookups, a weapon unit arriving later, the old one going to the back, dropped weapon, empty hands, no weapon anywhere), `one_handed` (the spin: start, pauses, return, never returning, hand moving against the arm root, sliding, both sides of 400 degrees a second, aiming, the rig's own animation, long gaps, the weapon unit dying, changing or leaving mid-spin, leaving the view, swap and stop mid-spin, two-handed, no arm root, no right hand or weapon bone), `pieces` (armour before arm, nothing on the rig, the half-second / one-second / aim-edge lookups, every piece and weapon dying or changing size, parents, the hand bone missing, link modes, rig parents that are garbage or loops, fingers the body lacks, bone counts and unreadable memory everywhere), `vanish`, `rig_screen` (hidden and shown, meshes changing while hidden, mesh counts 0 / 2.5 / -1 / NaN, the rig going while hidden, 70 meshes), `errors` (as for arm_pieces, also in the middle of a spin), `precision` (every limit of the spin handling from both sides: 400 degrees and 1.5 m a second, the rig's 45 degrees and 0.15 m a second and its half second, 12 degrees and 3 cm away, 30 degrees and 0.1 m a second of movement, the three seconds, the quarter-second sample gap and the quarter second after the last turn, each clock to the last bit and running back, a negated quaternion, the weapon unit replaced or the arm root found or lost in the very update), `edges` (the lookup times to the last bit with a clock that starts at zero), `memory` (two remembered grips, eighteen weapons held in turn, bone tables that put the first bone to work, rig parents as long chains or under the other arm, straight arms, the forearm roll at its limit), `vanishing` (14 functions or tables going missing while held and while spinning), `without_*` (41 runs), `only_*` (18 runs: a Vector3 or a Unit table with a single function), three random walks of 900 operations that play through holds, wandering hands, reloads, foregrips, one-handed spins and rig animations with every kind of trouble mixed in.

The tests were written first and failed against empty modules (`arm pieces vector 0 (run basic, new) answer differs: C ~;0,F,0,0,0,0;F; retail ~;0,F,0,0,0,0;T;idle`; `grip hand vector 0 (run module_functions, solve) value maths differs: C 0,00000000, retail 24,75197bf0`).

### Lua coverage

Line hook on the retail files over all scripts:

- `arm_pieces.lua`: **193 lines carry code, all are reached.**
- `grip_hand.lua`: **447 lines carry code, all are reached.**

Both files pack several statements per line, so the same scripts were run on scratch copies split to one statement per line (answers identical but for the line numbers inside error texts):

- arm_pieces: 240 statement lines, 239 reached. The other is the `return` of `if not was or not identity then return end` in `restore` (ap 99): `restore` is only called while hung, and nothing is hung without an identity.
- grip_hand: 596 statement lines, 595 reached. The other is `side=-side` in `solve` (gh 148): `side` is `cross(cross(to, E-S), to)` normalized, and its product with `E-S` is `|E-S|^2 |to|^2 - (to . (E-S))^2`, never negative; where rounding could make it so, the line before has already returned nil.

### Mutation

Every single change a rule list produces on a scratch copy of each C file (comparisons, logic operators, flags, constants and thresholds, bone hashes and indices, offsets, arithmetic, dropped assignments and calls, swapped arguments, left for right, the retail line of every engine call, texts):

**`src/arm_pieces.c`: 570 mutants, 508 killed (89.1 %), 62 survive.** None of the 62 changes anything a caller or the engine can see:

| Survivors | Change | Why it cannot show |
| --- | --- | --- |
| 32 | the retail line carried by an engine call | The line only appears in the text of a missing function, and only the first call of that function to be reached can fail that way; these are later calls of it, or calls of a function `bind()` has already required. |
| 9 | `<` for `<=` (or back) at a limit: the 1e-6 sine, the 5 cm / 2 cm / 0.1 mm lengths, the 2 cm tolerance, the clock going back by exactly the quick window | A length is a float and none of these limits is one; the cosine is never 0 when the sine is; resetting the window to the value it has changes nothing. |
| 8 | fill values, an unused table slot, initial values that are assigned before use, array sizes, a loop bound over rows that are not read | dead values |
| 6 | `confirm_at` cleared by `forget`, `hung=false` before `forget`, `confirm_at` set with no piece found, the per-side shoulder check before placing | Retail's own redundancies: each is overwritten or implied by what runs next (the lookup that follows sets `confirm_at`; `forget` clears `hung`; placing is only reached with all four shoulders). Kept because the Lua has them. |
| 5 | the operator check, the NULL row, the text of `Arm piece table full` | C-only guards with no Lua counterpart (see "Dropped or different"). |
| 2 | `restore`'s early return | unreachable: `restore` is only called while hung, and nothing is hung without an identity |

**`src/grip_hand.c`: 1,679 mutants, 1,379 killed (82.1 %), 300 survive.**

| Survivors | Change | Why it does not show |
| --- | --- | --- |
| 126 | the retail line carried by an engine call | as above |
| 56 | fill and initial values assigned before use, array sizes, loop bounds past the data, `: 1` for `: 2` where the result is clamped to 0..1 anyway | dead values |
| 49 | what `stop()`, leaving the view, a swap or `gun_release()` reset (`spin_until`, `rest_ref`, `stirred`, `aside`, `confirm`, `periodic`, `began`, `whole`, `drawn`, `current`, `on`, `pin`, `shapes`, ...) | Retail's own redundancies: the next update looks everything up again, and the next spin sets each of these before reading it. `drawn` after a failed stop is harmless because the pieces are forgotten in the same call. Kept because the Lua has them. |
| 27 | `<` for `<=` (or back) at a limit: 2 cm pinned, 20 / 32 cm blend, the spin's speeds and distances, 1e-5 / 1e-6 lengths, ties in `min` / `max` | A float distance or a measured speed never equals one of these double limits; `weight` gives the same value from either branch at exactly 0.20 and 0.32. |
| 18 | the operator check, the NULL row, the texts and tests of the table limits, the pool of finger-shape sets, forgetting the oldest remembered weapon | C-only storage with no Lua counterpart; the limits are not reached by any vector that retail can answer the same way. |
| 10 | the texts `nil`, `nan`, `inf`, ` moving` of the weapon id | The id is only ever compared with the previous id. |
| 6 | `* calm`, `calm <= 0`, `weapon is being handled` | `calm` is the constant 1 in retail (gh 655). |
| 8 | **not killed, not shown equivalent**: the `1e-6` of `arc` raised to `1e-5` or made inclusive (2; no vector has two directions between one and ten millionths of a radian apart); `has_ref_at && has_seen_before` as `||` (1; the stale sample it would read is the same pose in every vector); one of the four `* elapsed` of the movement test as `/ elapsed` (1); `seen.node ~= grip.node` inverted (1; needs a remembered weapon whose bone count changed while it stayed the bone in use, and no other remembered weapon to mask it, which the `memory` run tries and does not isolate); the three sample-gap tests of `left_aside` (3; its clock is the same as `in_hand`'s, which ends the spin first) | Open. |

Real gaps the survivors showed, closed on the way: the fake engine's link table did not change when `World.link_unit` was called, so the remembered homes of arm_pieces were never needed; unit addresses had zero bytes, so half the parent comparison was untested; no script lost an engine function in the middle of a session (the `bind()` calls of `stop` and of the release path); no script had a Vector3 or Unit table with a single function; no limit of the spin handling was approached from both sides within a few per cent; the first versions of the spin scripts did not end one spin before starting the next test, so later tests ran inside it; `'arm is straight'` was only ever reached through a missing hand bone, never through `solve` returning nil; the forearm-roll limit `size > 1e-6` was never on its small side.

Tools (scratch, not in the repo): `/tmp/claude-1000/-home-goose-Documents-ChatGPT-Linux-HD2--claude-worktrees-wj-weather-control-menu-355402/79c2f14a-8cee-438f-b054-2a5d60974797/scratchpad/arms/mutate.py`, `cover.lua`, `cover.sh`, `split.py`, `flags.sh` in the same folder.

## Differences met while proving

1. None in decisions, order of engine calls, state or texts: the first complete draft of each file passed every vector then cut, except the points below.
2. **NaN signs.** With a NaN pose retail wrote `ffc00000` where the C wrote `7fc00000`: which sign an operation on two NaNs yields depends on the operand order of the CPU instruction, which neither language defines. Every NaN is now written and summed as one canonical NaN on both sides. A NaN is still a NaN in every comparison; only its sign and payload are not compared.
3. **`M.solve` without a vector table.** `solve(nil, ...)` fails in Lua when `V.length` is looked up, before `E-S` is evaluated. The C first evaluated `E-S`. `tfp_grip_hand_solve` now fails first, with Lua's text.
4. **Fused multiply-add.** Built with `-std=gnu11 -O2 -march=native` grip_hand's `solve` gave another elbow (checksum `d0bb616b` against `d0bb6181`). The products now go through a `volatile`. Both files pass with GCC and Clang at `-O2` / `-O3`, `-march=native`, `-ffp-contract=fast`, and compile clean (`-Wall -Wextra -Werror`) with mingw-w64 GCC and with the mod's own `zig cc -target x86_64-windows-gnu -O2` line. The test's own copy of the value maths needs `-ffp-contract=off` (the scripts pass it).
5. **`pow`.** See the table above: `x^2` had to stay a library call to stay equal.
6. **Limits met by my own scripts.** The mesh limit was 64 and the 70-mesh script showed the difference; the remembered-weapon limit was 16 and the script that holds 18 weapons in turn showed it (the 17th pushed the first out, and retail still knew it). They are 256 and 32 now. Both were the C's fixed storage, not a different decision.

## What could not be made equal

1. **The engine's arithmetic.** The reference engine is a fake: float arithmetic written for this test. It proves that the C makes the same calls with the same numbers in the same order; it says nothing about what Stingray's own `Quaternion.lerp` or `Matrix4x4.inverse` return. The retail kit's tests have the same limit (inventory, "not covered: real engine math").
2. **Reference runtime.** The reference is LuaJIT's interpreter; the kit runs on Lua 5.1 with cffi. Known differences that are reached here: (a) LuaJIT's `assert(false, text)` raises `<chunk>:<line>: text`, stock Lua 5.1 raises `text` alone, so on the kit the three assertion reasons carry no prefix; (b) `math.min(1, NaN)` is NaN in LuaJIT and 1 in Lua 5.1 (the turn between two samples when a quaternion is NaN); (c) `x^2` is the C library's `pow` in both, but the game's C runtime is not glibc. None changes a decision on finite input by more than the last bit of a speed that is compared with a threshold.
3. **Raised errors inside the host.** A host function reports a raise by returning false. A fault that the host cannot turn into that (an access violation inside the engine on a stale handle) has no Lua counterpart either: both modules guard every handle with `Unit.alive` and the node count first.
4. **Limits and `known`**: see "Dropped or different".
5. **Order of `pairs`**: see "Dropped or different".
6. **Texts are capped**: an error text at 255 bytes, a reason at 335. The longest text in the vectors is 104.

## To check on game build 25480438

Everything below is data of the retail build 26568081 that the two files hard-code or read from game memory. None of it was checked against HellForge's build 25480438 here (no game, no Wine).

| What | Retail value | Where |
| --- | --- | --- |
| Unit object: scene-graph node count | u32 at object + 0x70, 1..255 | ap 61; gh 243, 260 |
| Unit object: bone name hashes | pointer at object + 0xA0 to u32 per bone | ap 63; gh 236, 245, 262 |
| Unit object: link words | pointer at object + 0x98 to u32 per bone: low 16 bits 1 = child of a bone of its own unit, 2 = linked to a bone of the parent unit; high 16 bits = that bone | ap 73-77; gh 213, 225, 252, 268-273 |
| Unit object: parent unit | pointer at object + 0x1D0, low 6 bytes compared with the rig's object | ap 56; gh 266 |
| Unit reference -> object | `backend:_alignment_context` (unit registry at exe + 0x1ab76f8 on retail) | the backend's translation |
| First-person rig: bone count | exactly 63 (`RIG_NODES`); any other count is `rig unavailable` forever | gh 102, 627 |
| First-person rig: arm bones (zero-based) | left shoulder 10, elbow 12, hand 13; right 30, 32, 34 | gh 101-102 |
| Bone names, arm_pieces | chest 37EF0820, spines 58D886FB and 5A4BBAA2, shoulder group 315CD161, collars C4787B4E (left) and B2BCD839 (right), shoulders 03752C98 (left) and 1620B2CE (right) | ap 26-30 |
| Bone names, grip_hand | weapon grip 67C94E4B, body left hand 5C0C68D3, body right hand 248004F5, weapon bone 4FDD3337 (rig bone 8, body bone 84 on retail), the six sleeve names above | gh 99-100, 134 |
| Finger bones | carry the same name hash on the rig and on the body | gh 235, 275 |
| The rig's wrist-screen mesh | the visible mesh with exactly 2 materials (measured on one rifle rig) | gh 84-89, 488 |
| Game behaviour | in camera kind 6 the game hangs the body's arm pieces on the rig bone by bone; the body's left hand is pinned to the grip bone; the weapon unit's root is linked to the rig's weapon bone; a linked bone keeps a written local pose | file headers |
| Engine functions | all 40 of the table above exist on that build's Lua API with these argument orders; `Unit.set_mesh_visibility` is called with three arguments here (head_visibility passes a pass name as a fourth) | - |
| Tuned numbers | 0.6 / 0.25 / 0.5 / 1 s lookup times, 2 cm tolerances, 20 / 32 cm blend, 0.95 stretch, the spin limits | ap 31; gh 105-132 |

## Open doubts

- The fake world's rules for what a dead unit or a bad node answers (a raise) are mine. They only decide which paths the scripts reach; whatever is reached is compared.
- `tfp_grip_hand_update` keeps about 556 KiB of state (33 finger-shape sets of 16 KiB) and its lookup uses about 20 KiB of stack; `TfpArmPieces` is about 13 KiB.
- Two modules write visibility of the rig's meshes (inventory line 3196): head_visibility by pass, grip_hand without a pass name. This translation keeps grip_hand's call as it is; whether the two agree on build 25480438 is a question for the wiring.
- `stop()` of grip_hand does not reset `whole`, `right_partial`, the weapon id, the delayed lookups or the right arm's reason (retail does not either). After a stop the next update looks everything up again, so none of it is used stale; it is visible in `result().right`.
