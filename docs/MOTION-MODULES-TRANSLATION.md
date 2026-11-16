# Motion modules: translation notes

Four retail Lua modules of True First Person 0.5.41-smooth.66 (kit commit `1d0d453`, folder `src/first_person/`) translated to C11, each proved by replaying the Lua file's own answers. The Lua files are the authority; nothing here improves on them.

| Retail file | C | Vectors | Test | Run |
|---|---|---|---|---|
| `motion.lua` | `src/motion_math.c`, `.h` | `tests/vectors/motion_vectors.h` | `tests/motion_test.c` | `tests/run-vectors-motion.sh` |
| `action_placement.lua` | `src/action_select.c`, `.h`, `src/action_placement_data.h` | `tests/vectors/action_placement_vectors.h` | `tests/action_placement_test.c` | `tests/run-vectors-action_placement.sh` |
| `action_state.lua` | `src/action_state.c`, `.h` | `tests/vectors/action_state_vectors.h` | `tests/action_state_test.c` | `tests/run-vectors-action_state.sh` |
| `locomotion.lua` | `src/locomotion.c`, `.h` | `tests/vectors/locomotion_vectors.h` | `tests/locomotion_test.c` | `tests/run-vectors-locomotion.sh` |

Method, the same for all four:

- `tools/retail_ref/run_<m>.lua` loads the retail file unmodified under `/usr/bin/luajit` (LuaJIT 2.1) with a stand-in for `compat51.lua` (`kind = type`, `number = tonumber`, `bit`, real `ffi` where used) and replays a script.
- `tools/retail_ref/gen_<m>.py` writes the script (the retail fixture's own input sequences, hand-made edge cases, seeded random runs), runs the Lua and writes the vectors header with the retail commit and the file's SHA-256. It refuses any retail commit other than `1d0d453`.
- The C test replays every operation. Doubles must be the same double bit for bit (a NaN must be a NaN). Texts and booleans must be equal.
- Coverage: a line hook on the Lua (LuaJIT with the JIT off), against the executable lines `luac5.1 -l` lists. The tools are scratch files, not in the repository.
- Mutation check: single changes to a scratch copy of the C file (comparison and arithmetic operators swapped, every constant changed, statements removed, conditions forced, booleans flipped), each run through the vector test.
- Both compilers pass: `cc -std=c11 -Wall -Wextra -Werror` and `zig cc -std=c11 -target x86_64-windows-gnu -Wall -Wextra -Werror`. The vector tests run natively only (no Wine was used).

A point that holds for all four: the test scripts build with `-ffp-contract=off`. A Lua product followed by a sum is two IEEE operations; a fused multiply-add would give a different last bit. Whatever builds these files for the game must keep that flag.

A second point for all four: under LuaJIT, `assert(false, text)` reports `file:line: text`. The file path depends on where the kit is installed, so the runners cut that prefix and the C answers carry the bare text.

## 1. motion.lua → src/motion_math.c

Named `motion_math` because an older, unrelated `src/motion.c` exists.

### Interface

`TfpMotionMath` is the model's state, owned by the caller. No globals, nothing from outside.

| Retail | C |
|---|---|
| `Motion.new(options)` | `tfp_motion_math_init(m, options, &error)`; `options` NULL = none. Retail raises the refusal text; C returns false with it. |
| `self:configure(value)` | `tfp_motion_math_configure(m, value, &error, &setting)`; `value` NULL = "not a table". |
| `self:update(sample, dt)` | `tfp_motion_math_update(m, sample, dt)` returns `TfpMotionOutput`. |
| `self:reset()`, `self:clear_ads()` | `tfp_motion_math_reset`, `tfp_motion_math_clear_ads` (`motion_runtime.lua` calls `clear_ads` on a held clock). |

All arithmetic is in `double`. Nothing is converted to `float` in this module: the consumer is `to_parent` of locomotion, which also works in doubles.

### Lua-specific points

- **Types a C field cannot hold.** `enabled`, `editor_frozen`, `shoulder_aim_enabled` may be nil, a boolean or another type (refused with a text): `TfpMotionFlag` has all four cases. `scale` and the nine settings may be nil, a number or another type: `TfpMotionNumber`. A sample number that is not a number is passed as NaN; retail refuses both alike (`finite`, line 9).
- **`== true` tests.** `sample.shoulder_aim`, `sprint`, `editor_frozen`, `frozen` count only when exactly `true`; the number 1 does not. The C `bool` is that comparison's result. `sample.ads` is `true`, a number (clamped to 0..1; NaN and infinity count as 0), or anything else (0): `TfpMotionAds`.
- **Owner.** Retail accepts a non-empty string or a finite number and compares with `~=`. C takes `has_owner` and a 64-bit key; the caller must give equal keys to equal owners. On the live path the owner is always the `player_identity` string.
- **Order of refusals in `configure`** is kept: not a table, enabled, scale, editor freeze, aim movement, then the nine settings in the order of line 41. As in retail, `shoulder_aim_enabled` is stored (line 40) before the nine settings are checked, so a refused setting can still change it; nothing else is stored on a refusal.
- **`configure` keeps** `previous`, `shoulder` and `aim` over its reset unless the editor freeze is on afterwards (lines 46-52).
- **Zero answer.** Retail's `zero()` table has no `camera_*` fields (nil); the normal answer has them. `has_camera` says which. `reset` is `false` or a text, `rebased` is `false` or a text: two enums with name functions.
- **`%`** is `a - floor(a/b)*b`, written out (`lua_mod`).
- **`math.min` / `math.max`** are written as LuaJIT computes them (`minsd` / `maxsd`: the second argument wins on a tie or a NaN). This matters in one situation only, see "Open doubts".
- **`math.rad(x)`** is `x * 1.74532925199432957692e-2`, LuaJIT's constant, which is the same double as pi/180.
- **`^`** does not occur in this file.
- No `pcall`, no table iteration that affects a result (`ipairs` over fixed lists only).

### Dropped

Nothing. The camera outputs (lines 147-152) and the shoulder offset (lines 141-142) are unused or zero on the shipped path, but `configure` accepts the settings that switch them on, so they are translated and tested. The fixture's options passed through a metatable (`__index`) are not a live form (`motion_runtime.lua` passes a plain table) and the C options struct has no such thing.

### Proof

- 34,054 operations: 32,360 updates, the rest `new`, `configure`, `reset`, `clear_ads` and 1,092 full state comparisons (every field of the model). 596,676 doubles compared, all equal bit for bit.
- Scripts: all 20 tests of the retail fixture `tests/first_person/motion.lua` as input sequences (at 30, 60, 120 and 144 frames a second as the fixture has them); hand-made edges (every refusal and their order, every invalid sample kind, every threshold from both sides: dt 1 / .25 / .05, distance 2 m, speed 20 m/s, turn and pitch turn pi/2, yaw wrap, huge and tiny coordinates, every `ads` kind); the `.00001` thresholds met exactly by a computed frame time (aim, shoulder and sprint, rising and falling, and `settle`); the output clamps; four seeded random runs of 5,000 operations at 30, 60, 120 and 144 frames a second with hitches, teleports, respawns, invalid samples and option changes.
- **Differences found between Lua and C: none.** The first run matched.
- The same script gives byte-identical answers under `luajit` and `luajit -joff`.
- Coverage: 131 executable lines, 129 hit. The two others are `camera_dy=0,` (148) and `camera_roll=0,` (152): constant fields of the answer table, which LuaJIT builds from a template without a line event. The table is built on every normal update.
- Mutation check: 725 single changes, 82 did not compile, **625 killed, 18 survived**. The first pass left 53; scripts and test checks were added for the real gaps (exact thresholds, a hitch inside the sprint hold, the strafe and forward lag clamps, the dx clamp, `Motion.new` after a frozen model, error and setting answers). The 18 that remain:
  - `lua_min` / `lua_max` with `<=` / `>=` (2): differ only in which of two equal values is returned; only a signed zero could tell, and none reaches an output.
  - the range guard of the two name functions (2): only an invalid enum value could tell.
  - the initial values of `previous` in init (4): never read while `has_previous` is false.
  - `wz = 0` on a rebase (2): `wz` is not used afterwards (retail assigns it too).
  - line 110 of the Lua, `if ads==1 then self.sprint_hold,self.sprint=0,0` (4): redundant in retail, `clear_ads()` on line 98 has already done it in the same update.
  - the +-.15 clamp of `dy` (2): cannot be reached with accepted settings (scale at most 2, strengths at most 3: the largest `dy` magnitude is .135).
  - `out.camera_dy = 0` / `out.camera_roll = 0` removed (2): the field is then uninitialised stack memory, which happened to be zero.

### Open doubts

- **NaN turn.** Two yaw values whose difference overflows (1e308 and -1e308) give a NaN turn that passes every `>` test and stays in the sway forever. LuaJIT lets the NaN through `clamp`; plain Lua 5.1 (`lua5.1`, which the kit's `compat51` layer targets) does not, because its `math.min` / `math.max` keep the first argument on a NaN. The C follows LuaJIT, as the method prescribes. Of the 34,054 operations, 10 differ between `luajit` and `lua5.1`, all in this script (4 more differ only in how the script's literal `-0.0` is read). A camera yaw comes from `atan2`, so the live path cannot produce it.
- **`exp`, `sin`, `cos` on Windows.** The vectors were cut and replayed with glibc. The Windows build links another C runtime, whose last bit can differ. The retail Lua in the game uses the game's runtime, so "bit for bit" is proved for the arithmetic and its order, not for the runtime's transcendental functions.

## 2. action_placement.lua → src/action_select.c

Named `action_select` because older, unrelated `src/action.c` and `src/placement.c` exist. Its enum is `TfpAct` / `TFP_ACT_*` because `TFP_ACTION_UPDATE` and `TFP_ACTION_WAIT` already belong to recovery.

### Interface

| Retail | C |
|---|---|
| `M.configure_catalogue(gear)` | `tfp_action_configure_catalogue(catalogue, rows, count)` |
| `M.weapon_info(key)` | `tfp_action_weapon_info(catalogue, key)` |
| `M.family_baseline(key)` | `tfp_action_family_baseline(catalogue, key, &out)` |
| `M.valid(p)` | `tfp_action_valid(p)` |
| `M.resolve(config, key, action)` | `tfp_action_resolve(config, catalogue, key, action, &out, &is_explicit, &error)` |
| `M.default_placement(config, key, action)` | `tfp_action_default_placement(config, catalogue, key, action)` |
| `M.saved(config, key, action, value, all)` | `tfp_action_saved(config, key, action, value, all, out, capacity, &count, &error)` |
| `M.select(sample, mapping)` | `tfp_action_select(sample, mapping)` |
| `M.actions`, `M.observed_actions` | `tfp_action_name`, `tfp_action_label`, `tfp_action_from_name`, `tfp_action_observed` |

The Lua file's module-level `weapons` table is a `TfpActionCatalogue` the caller owns (caller's storage). The saved configuration comes in as one `TfpActionConfig` struct. No globals. Numbers are doubles and are handed on unchanged; nothing is converted to float here.

### The catalogue data

How the mode feeds it: `src/first_person/runtime.lua:649-650` loads the module and calls `Actions.configure_catalogue(mod.require('src/loadout_catalogue.lua').gear)` once at enable. After that the catalogue is read by `weapon_info` (the presentation menu's weapon label and family, `runtime.lua:1063`) and by `family_baseline` (inside `resolve` and `default_placement`, only with "Use weapon-family placement defaults" on).

`src/action_placement_data.h` is generated by `tools/retail_ref/gen_action_placement.py --data src` from the retail file (`src/loadout_catalogue.lua`, SHA-256 `19f0f58b…`): **151 rows** in the kit's order, the four fields the module reads (`kind`, `name`, `id`, `type`). The C `configure_catalogue` turns them into **105 weapons, keyed by the 16 lower-case hex characters of the unit type id in memory order** (the catalogue's `type` with its eight bytes reversed): rifle 13, compact 8, sidearm 20, marksman 9, shotgun 12, energy 11, support 32; none ambiguous. The other 46 rows have no family (kind 6, melee, `B/` backpacks and so on). The test checks the header row for row against the `gear` table the retail run loaded, and the 105 weapons against the module's own `weapons` table (read through `debug.getupvalue`).

The translation keeps `configure_catalogue` and the family rules as code and carries the raw rows, not a precomputed weapon table, so that the duplicate and ambiguity rules stay live.

### Lua-specific points

- **Keys are strings.** `nil` is NULL. Keys are compared as strings in retail, so the key `'__all'` reaches the shared row; the C lists use `strcmp` and behave the same. An upper-case key does not match a lower-case one.
- **Placement tables.** `TfpActionPlacement` has a bit per field. `p.yaw or 0` makes `false` equal to absent for the three angles; for x, y, z both `nil` and `false` are "not a number". A value of another type (a string, `true`) is passed as NaN: retail refuses NaN and non-numbers alike.
- **Answer shapes.** `resolve` answers `{x,y,z}` without angle fields for the ADS fallback and for the last fallback, and all six fields otherwise; the `present` bits say which. The second result (`true` only for a saved action placement) is `is_explicit`. A non-idle action that falls back to idle answers `false` even when the idle placement was explicit, as retail.
- **`assert` texts**: `'Unknown placement action'`, `'Invalid action placement'`, `'Invalid weapon identity'`, in retail's order (`saved` checks action and value before the key).
- **`saved` builds a table**: its result does not depend on `pairs` order, and the C answer's row order is not part of the answer (the test matches rows by id). Every table-valued profile of the old configuration is kept, also when no valid action is left in it (an empty row); profiles that are not tables, unknown action names and invalid placements are dropped; with `all`, the action is removed from every row.
- **Truth.** `select` tests `reload`, `shoulder_aim`, `sprint` for truth: 0 and `''` are true (tested). `action_verified` must be exactly `true` for the posture and merely true for the optional mapping: three-valued in C. `ads` is `true` or a number >= 1 (NaN is not).
- **Posture bits**: `math.floor(f:byte(10)/32)%4*32` and the like are integer divisions on bytes; `f:byte(10)` is `flags[9]`.
- **Mapping key**: `tostring(l1)..':'..tostring(l3)`; `nil` prints as `nil`. The C takes integers (what action_state produces) and builds the same text.
- **Values that are not tables** where retail expects one (a profile, a legacy placement, a mapping's resource entry) are skipped by retail everywhere; in C they are simply not in the list. `config.action_placements` or `weapon_placements` being a non-table (retail would index a string or raise in `pairs`) is not representable and not translated.
- **`ipairs`** stops at the first hole in `gear`; a row that is not a table is skipped (a C row without a name).

### Dropped

- Nothing of the module. `family_baseline` is off as shipped but a menu checkbox switches it on: kept and tested.
- Not representable, and therefore refused or absent rather than translated: non-integer `l1` / `l3`; catalogue kinds that are not numbers (they never get a family, the C row carries kind 0); `sample` being nil (retail raises an index error; the C needs a sample).
- One addition forced by C: `configure_catalogue` and `saved` write into caller storage and refuse when it is too small (retail's tables grow). `saved` then answers false without a text.

### Proof

- 15,133 operations: 5,110 `select`, 4,864 `resolve`, 2,721 `saved`, 1,153 `default_placement`, 512 `valid`, 23 catalogues (every weapon compared), 749 weapon lookups. All equal; 51,054 doubles bit for bit.
- Scripts: the retail fixture `tests/first_person/action_placement.lua` statement by statement (the 36 labelled action-state samples, 10 bridge crouch, 8 dive/prone, the profile and catalogue checks); hand-made (every family rule in its order over 46 names and 5 kinds, type shapes, every non-hex neighbour at every position, duplicate and ambiguity sequences, every step of the lookup order with a valid and an invalid value, the standard-placement test bit by bit, all posture bit combinations, the mapping); 9,000 seeded random operations over 250 random configurations.
- **Differences found between Lua and C: none.**
- Coverage: 118 executable lines, 116 hit. The two others are bare `end` lines (38 and 117) to which `luac` attributes a jump and for which LuaJIT raises no line event; the blocks they close are executed.
- Mutation check: 688 single changes, 152 did not compile, **512 killed, 24 survived**:
  - buffer and array sizes (`key[17]`, `position[3]`, `angles[3]`, `states[40]`, `l1[16]`, `l3[16]`; 10) and loop bounds one past the end whose extra element has no `present` / `has` bit (3);
  - the unused baseline row of "no family" (3) and the range guard of the family name (1);
  - `lower()` upper bound `'Z'` (1): only hex letters reach it;
  - the default of an absent angle inside `valid` (1): any value within 90 behaves like 0 there;
  - the angle values of an `{x,y,z}` answer (3): those fields are marked absent;
  - `out.y = 0` in `default_placement` (1): already zero;
  - `break` after a matching mapping row (1): a Lua table has one entry per key.

### Open doubts

- The catalogue is the kit's, built for game build 26251122. The type ids are resource-name hashes and should not depend on the build, but this was not checked against build 25480438. It matters only for the menu label and the opt-in family defaults.

## 3. action_state.lua → src/action_state.c

### Interface

| Retail | C |
|---|---|
| `M.new(backend)` | `tfp_action_state_init(reader)`; the backend is passed to each call |
| `self:sample(expected, layers)` | `tfp_action_state_sample(reader, backend, expected, layers, layer_count, &out, error, error_size)` |
| `backend:_read`, `_ptr`, `_u32` | `backend->memory` (`FpMemory`, read with `fp_read` as `src/snapshot.c` does) |
| `backend:player_snapshot()` | `backend->player_snapshot` |
| `backend:_alignment_context(s)` | `backend->alignment_context` |

`TfpActionStateBackend` is the one struct of function pointers. `tfp_action_state_backend(&backend, memory)` fills it with this repository's own two methods: `tfp_snapshot(TFP_SNAPSHOT_PLAYER)` of `src/snapshot.c`, and `tfp_alignment_context`, which is the translation of `backend.lua:783-803` (with `_alignment_executable`, `backend.lua:688-725`, exported as `tfp_alignment_executable`). Those two backend methods are translated in this file because no other file of the repository has them and locomotion needs them too. Retail remembers a passed executable check for the rest of the host frame; the C checks on every call (same answers, more reads).

`TfpActionStateReader` is the sampler with retail's `cache` (about 260 KB: 32 layers of up to 2048 state offsets). The answer keeps the snapshot it was read for (`out.player`; retail returns its `player_identity` as `owner`). `states[layer]` with `has[layer]`: retail's `states` table holds only the layers asked for, so `l1`, `l3`, `l10`, `l11`, `l14` are nil unless asked for.

No floats: the module reads integers and bytes only.

### Build constants (game build 25480438)

All are named in one block at the top of `src/action_state.c`. Evidence: **map** = `tools/build_map.py --retail <kit> --to-here`; **image** = this build's executable image `native-work/evidence/exe-25480438.bin` (PE stamp `0x6ab382e4`, image size `0x39e8000`); **layout** = same number in retail, and a named file of this repository reads the same field on this build. `python3 tools/retail_ref/gen_action_state.py --here-image <image>` re-checks every image claim below; it passed.

| Constant | Retail | Here | Evidence |
|---|---|---|---|
| executable PE stamp, image size | `0x6af2d5e6`, `0x29fe000` | `0x6ab382e4`, `0x39e8000` | `profile.h` `FP_EXE_STAMP`, `FP_EXE_SIZE`; image header |
| unit registry global | exe+`0x1ab76f8` | exe+`0x1a100f0` | map; `profile.h` `FP_REGISTRY`; the same VALUE as `camera_helper.c` `B_EXE_REGISTRY` |
| fingerprint `exists` | exe+`0x1f75d0` | exe+`0x201710` | map; image: the same 32 bytes except the call displacement (`ef90eaff` → `9fc1e9ff`) |
| fingerprint `trigger` | exe+`0x1f77c0` | exe+`0x201900` | map; image: the same 32 bytes except the call displacement (`fd8eeaff` → `adbfe9ff`) |
| animation interface getter | exe+`0x28d510`, `48 8B 81 78 01 00 00 C3` | exe+`0x2bd9c0`, same bytes | map; `native_contract.h` `anim_getter`; image; the same VALUES as `camera_helper.c` `B_EXE_ANIM_GETTER`, `B_HEX_ANIM_GETTER` |
| resource getter | exe+`0x28d520`, `48 8B 81 58 03 00 00 48 89 02 48 8B C2 C3` | exe+`0x2bd9d0`, same bytes | map; image (this constant was not in the repository before) |
| virtual-table slots of the two getters | +`0x1B0`, +`0x1B8` | same | image: exactly one table holds exe+`0x2bd9c0`; it has it at +`0x1B0`, exe+`0x2bd9d0` at +`0x1B8`, and the scene-graph getter exe+`0x2bd870` at +`0xE8`. `0x1B0` = `camera_helper.c` `L_VT_ANIM_GETTER` |
| unit object: state machine instance, resource id | +`0x178`, +`0x358` | same | layout; `camera_helper.c` `L_UNIT_ANIM`, `L_UNIT_RESOURCE`; the two getters' own code reads `[rcx+0x178]` and `[rcx+0x358]` |
| instance: graph, active-state array | +`0x28`, +`0x50` | same | layout; `camera_helper.c` `L_ANIM_GRAPH`, `L_ANIM_ACTIVE` |
| graph header and layer tables | count +4, table +8; table +4; definition +8, +12 | same | layout; `camera_helper.c` `L_ANIM_LAYER_COUNT`, `L_ANIM_TABLE` and its `on_foot` proof, which walks the same tables |
| registry block | +`0x88` units, +`0x98` capacity, +`0xA0` generations | same | layout; `backend.c` `fp_unit_object`; `camera_helper.c` `L_REGISTRY_BLOCK` |
| body state-machine resource, layer count | `4D1C334D294DFA97`, 31 | same | `camera_helper.c` `L_BODY_RESOURCE`, `L_ANIM_LAYERS` |

The values shared with `src/camera_helper.c` (its `on_foot` proof reads the same structures) are reused as values, as asked; the names are local to each file. Nothing is open: no constant had to be guessed.

### Lua-specific points

- **`pcall` boundary.** The whole of `sample` runs inside one `pcall`; any `assert` or failed read ends it with `nil, text`. The C keeps the first refusal's text and answers false. Under LuaJIT each `assert` puts `file:line:` in front; an error that passes through two asserts carries two such prefixes. The runner cuts them all; the C texts are bare.
- **Order of reads.** Every read is in retail's order, with retail's sizes (the state table in 1024-byte blocks), and short-circuits like retail's `and` chains (lines 54-57, 110-112, 114). The test proves it: see "Proof".
- **`_read`'s own refusal.** `'Invalid read'` (address below 65536, or address + size reaching 2^47) comes before the memory is touched; `'Memory unavailable'` after.
- **Pointer text.** `'Pointer unavailable at0x… (value0x…)'`: retail's value has gone through a double (`uint(b)+uint(b,4)*4294967296`) and is printed with `%x`. Above 2^53 it is the nearest double, and all-ones (2^64 as a double) prints as `0`. `lua_hex()` reproduces LuaJIT's conversion. This was the one difference the test found.
- **The cache.** Retail keeps, per layer, the map from state address to index while the layer's definition address and state count are unchanged. A table whose entries change behind an unchanged address and count is not noticed, and the answer is then stale. Kept, and tested. A refused table read leaves the layer's old map. A replaced cache drops all maps.
- **`map.indices[at]==nil`**: the first state at an address wins.
- **`index or 0`**: an active pointer that is in no state answers 0 and the layer is listed in `unknown_layers`, in the order the layers were asked for; a null pointer answers -1 and is not listed.
- **`assert(player(after),problem)`** with `problem` nil would print `assertion failed!`; kept in C, unreachable with this repository's snapshot (it never answers a body of 0).
- **Identity.** Retail compares `player_identity` strings; the C uses `tfp_same_player` (the string's parts). An `expected` table whose `avatar_owner` / `avatar_index` fields contradict its own identity string cannot be expressed in C.
- **Layers argument.** `nil` is NULL; a C `int` list cannot hold a non-number or a fraction (retail refuses both with the selection text).

### Dropped

Nothing. The five-layer selection `{1,3,10,11,14}` is only requested by the parked Fird Person code, but it is an argument, not code: any selection works and is tested.

### Proof

- 10,561 samples (6,840 answered, 3,721 refused with the same text) in sequences on one sampler, and 593 direct calls of the two backend methods; 2,504 memory images. The retail files (action_state.lua over the unmodified backend.lua, through its `io` seam) answered on retail's layout; the C on this build's layout.
- **Read trace.** For every sample the count and a hash of the ordered `(address, size)` reads made outside the player snapshot must be equal (retail's executable addresses rewritten to this build's): 477,155 reads.
- Scripts: every bound from both sides (layer count, table offset, definition and state offsets, state counts, block edges); null, unmatched and invalid state pointers; every layer selection kind; the two getters byte by byte; the executable image field by field and both fingerprints byte by byte; the registry (capacity, generation, pointers, references with generation bits); every record unreadable in turn; an expected snapshot that is fresh, stale or edited; what the cache keeps and drops over 3 to 8 samples; **memory that changes during the read**: each of 35 scene variants swapped in before read 1, 2, … 75 of a sample, and swapped back; 4,000 seeded random samples in sequences with random mutations and swaps.
- The retail fixture `tests/first_person/action_state.lua` is not replayed as it stands (it brings its own fake backend); its eight situations are all among the hand-made scenes, here against the real backend.
- **Differences found between Lua and C: one**, the pointer text above (6 of 9,758 samples in the first run); fixed in the C.
- Coverage: 112 executable lines of action_state.lua, all hit.
- Mutation check: 1,066 single changes, 193 did not compile, **810 killed, 63 survived**. The first pass left 108 (13 of them inside a comment, a fault of the tool); added after it: the two backend methods called directly, the repository's own backend as a second run of every unscripted sample, last-byte differences, block-edge tables, cross-sequence cache scripts, truncated images, missing error buffers. The 63:
  - start values of locals that are always written before they are read (20) and sizes of buffers (20);
  - compare or copy lengths one off where the extra or missing byte is always zero (3);
  - block reading (4): `size > 1025`, `size - at <= 1024`, and the 1024 in the block length changed by one. The last two leave one byte of a block unread, which then holds what the previous call left on the stack (the same table);
  - one state past the end in the lookup (1); the 2^63 edge of `lua_hex` (1);
  - `tfp_action_state_init` (7): clearing `cached` and clearing the maps each make the other redundant, so no single change shows;
  - "first refusal wins" (6): a removed `break` after a refusal, or always overwriting the text, changes nothing while a later check stops the read; and the unreachable `assertion failed!` branch (1).

### Open doubts

- `src/snapshot.c` prints the raw 64-bit value in its `Pointer unavailable` text; retail prints the double (see above). The two differ only for values above 2^53 that a double cannot hold. I did not change that file. The action-state test compares the value part of that text and has such scenes only for its own reads.
- The executable fingerprints `exists` and `trigger` belong to retail's body-pose API. On this build they are the same code at the mapped addresses; whether this build's mode needs them as a gate at all is the backend's decision. They are checked because retail checks them.

## 4. locomotion.lua → src/locomotion.c

### Interface

| Retail | C |
|---|---|
| `M.rotation_to_parent(value, camera, parent)` | `tfp_locomotion_rotation_to_parent(value, camera, parent, q, &error)` |
| `M.to_parent(value, camera, parent)` | `tfp_locomotion_to_parent(value, camera, parent, &out, &error)` |
| `M.sprinting(action, verified_lifecycle, flags)` | `tfp_locomotion_sprinting(action, verified_lifecycle, flags, flags_size)` |
| `M.new(backend)` | nothing to keep: the Lua object holds only its float buffer |
| `self:sample(lease)` | `tfp_locomotion_sample(backend, lease, &out, error, error_size)` |
| `backend:_read`, `_ptr`, `_u32`, `_u8` | `backend->memory` (`FpMemory` / `fp_read`) |
| `backend:player_snapshot()`, `_alignment_executable(profile.pose)`, `action_state(s)`, `on_foot(s, action)` | the four function pointers of `TfpLocomotionBackend` |
| `backend.alignment_lease`, the lease's fields | `TfpLocomotionLease` (what lines 124-126 and 133 look at) |

`tfp_locomotion_backend()` fills the struct with this repository's own methods: `tfp_snapshot`, `tfp_alignment_executable` and the action-state sampler of section 3, and `tfp_on_foot`, which is the translation of `backend.lua:351-372` and lives in this file. The action-state sampler is the backend's one sampler in retail, shared with its other users; the C takes the caller's `TfpActionStateReader`.

**Doubles and floats.** All arithmetic is in double. The engine's matrices are 16 floats in memory; they are widened to double on reading (exact, as retail's `PORT51.number(floats[i])`). Nothing in this module narrows a double to float: `x, y, z, yaw, pitch`, both bases and the `to_parent` answer are doubles. The narrowing happens where the pose backend calls the helper (`set_pose_motion`), outside these four modules.

### Build constants (game build 25480438)

Named in one block at the top of `src/locomotion.c`; the same evidence classes as in section 3, and the image check of `gen_action_state.py --here-image` covers the three executable items.

| Constant | Retail | Here | Evidence |
|---|---|---|---|
| unit registry global | exe+`0x1ab76f8` | exe+`0x1a100f0` | map; `profile.h` `FP_REGISTRY`; = `camera_helper.c` `B_EXE_REGISTRY` |
| scene-graph getter | exe+`0x28d3c0` | exe+`0x2bd870` | map; `native_contract.h` `graph_getter`; image; = `camera_helper.c` `B_EXE_GRAPH_GETTER` |
| world-pose wrapper | exe+`0x1f30e0`, 48 bytes | exe+`0x1fd220`, 48 bytes | map; `native_contract.h` `unit_world_matrix`; image. The bytes differ from retail's in the call displacement (`e2d5eaff` → `9206eaff`) and in byte 48, which lies after the function's `ret`: `c3` in retail, `cc` here. The first 47 = `camera_helper.c` `B_HEX_UNIT_WORLD` |
| registry: objects, capacity, generations | +`0x88`, +`0x98`, +`0xA0` | same | layout; `backend.c` `fp_unit_object` |
| graph getter's virtual-table slot | +`0xE8` | same | layout; `scene.c`; image (the table of section 3) |
| unit scene graph; node count; world matrices; matrix size | object+`0x60`; +`0x10`; +`0x28`; `0x40` | same | layout; `scene.c`; `camera_helper.c` `L_UNIT_GRAPH`, `L_GRAPH_COUNT`, `L_GRAPH_WORLDS`, `L_MATRIX`; the getter is `lea rax,[rcx+0x60]`, the wrapper adds `[rax+0x28]` and shifts the index by 6 |
| camera matrix | context+`0x0C` | same | layout; `camera_helper.c` `L_CONTEXT_BASIS` |
| rig parent node; rig node count | 4; more than 50 | same | the 63-node rig `scene.c` reads on this build (`camera_helper.c` `L_RIG_NODES`) |
| avatar flag row, stride (on_foot) | owner+`0x53e888`, `0x1240` | owner+`0x53e880`, `0x1238` | map; `profile.h`; = `camera_helper.c` `B_AVATAR_FLAGS`, `B_AVATAR_STRIDE` |
| game state, scene number (on_foot) | game+`0x2ce1508`, +`0xac29c` | game+`0x3326340`, +`0xac21c` | map; `profile.h` `FP_STATE`, `FP_STATE_SCENE`; `action.c` reads both on this build; = `camera_helper.c` `B_GAME_STATE`, `B_STATE_SCENE` |
| on-foot / bridge states of layer 14; sprint carry of layer 3 | 36 / 3; 71 | same | `action.c` reads these three numbers on this build; `camera_helper.c` `L_STATE_ON_FOOT`, `L_STATE_BRIDGE` |

**Open (two), and what the C does about them.** No file of this repository and no record establishes these on build 25480438, so by the rule "do not guess" each is behind a named switch that is off, and the test it guards answers "no":

1. `B_SPRINT_GAIT_ESTABLISHED`: **flag byte 9, bit value 4 = the native sprint gait** (retail's `sprint_gait`, labelled on retail on 2026-10-25: gait `0x84` sprint, `0x88` walk, `0x90` jog). The byte itself is the posture byte, which `placement.c` reads on this build; the meaning of its low bits is not known here.
2. `B_BRIDGE_SPRINT_ESTABLISHED`: **layer 14 = 3 with layer 3 = 68 and layer 1 = 75 or 78**, the bridge graph's sprint (labelled on retail).

As shipped, sprint is therefore seen through `l3 == 71` only (retail's comment says why that is not enough: that state is specific to primary and support weapons; secondaries have other states). Switching a `0` to `1` in `src/locomotion.c` restores retail's full test. The logic of both is proven against retail all the same: `tests/run-vectors-locomotion.sh` builds the file a second time with `-DTFP_LOCOMOTION_TEST_OPEN_CONSTANTS` (test only), where everything must equal retail; the shipped build must give retail's sprint answer narrowed to `l3 == 71` and everything else equal.

### Lua-specific points

- **`value.pitch or 0`**: nil and false are zero for the three angles; any other non-number is refused (`'Motion angle out of bounds'`). The angle check comes before any look at the matrices, and with all three angles zero the answer is the identity **without** the matrices being validated (line 37).
- **`ipairs({value.dx, value.dy, value.dz})`** stops at the first nil: with `dx` nil nothing is checked or moved, with `dy` nil only `dx`. Kept (the `present` bits). `false` is not nil there and is refused.
- **Order in `to_parent`**: camera basis, parent basis, the displacement bound, then the rotation. `pairs(q)` merges four distinct keys; its order does not matter.
- **`basis()`** interleaves its checks per axis: axis 1 finite, axis 1 scale, axis 2 finite … then the three skew tests. Kept.
- **A nil matrix entry** is not representable: retail's `ipairs` stops at it and the dot product then raises Lua's own "attempt to perform arithmetic on a nil value". Every matrix on the live path has 16 numbers. This was the one thing the first run showed (36 scripted operations with a nil entry, where my C answered `'Non-finite transform basis'`); those operations were removed and the header says so. An entry of another type (a string) is refused by `finite`, and NaN stands for it.
- **`^2`** is `x*x` in LuaJIT; **`math.min` / `math.max`** as in section 1; **`math.rad(120)`** is `120 * 1.74532925199432957692e-2`.
- **`math.floor(flags:byte(10)/4)%2==1`**: `flags[9] / 4 % 2` on a byte.
- **`sprinting`** returns a boolean also for a non-table action (`false`); `resource_match` and `verified_lifecycle` must be exactly `true`; a layer that was not sampled is nil and equals no number.
- **`pcall` boundary** of `sample`, the stripped `file:line:` prefixes and the pointer text: as in section 3. `assert(s, why)` passes the snapshot's own refusal text on.
- **`on_foot`** runs its reads inside its own `pcall` and answers `false` for any refusal; so does the C, in the same read order.
- **Lease.** `lease==backend.alignment_lease` is `current`; `lease.origin.player_identity==s.player_identity` is `tfp_same_player(origin, snapshot)`. A lease without an `origin` table raises an index error in retail; the C refuses it with the lease text.
- The sample's `action_resource`, `action_verified`, `l1`, `l3`, `l14` are nil when `backend:action_state` answered nil (`has_action` false); the sample itself still succeeds, as in retail.

### Dropped

Nothing of the module. The two open sprint tests are present and proven but switched off (above).

### Proof

- Pure functions: 13,311 operations (the retail fixture `tests/first_person/locomotion.lua` statement by statement; every bound from both sides; every used matrix entry not finite; scale, skew and mirror cases per axis and per matrix; all sign combinations of the largest angles; 40 searched cases whose composed quaternion has a negative w; 7,000 seeded random conversions, half of them with matrices rounded to float as the engine gives them; 2,500 random `sprinting` calls). All doubles equal bit for bit.
- Sampler: 9,350 samples (6,957 sampled, 2,393 refused with the same text) and 787 direct `on_foot` tests over 3,231 memory images; retail locomotion.lua over the unmodified backend.lua and action_state.lua on retail's layout, the C on this build's. 293,184 doubles compared in all; 632,058 memory reads with equal count and hash per sample.
- Scripts for the sampler: flags, aim, sights, reload and the three ways to sprint; the bridge test of `on_foot` field by field; the view and the lease, each condition; the executable and the 48 world-pose bytes one by one; both Unit lookups (capacity, generation, getter, node counts, references with generation bits past eight bits and the largest index); each float of each of the three matrices not finite or out of shape; the root position and the facing limits; every record unreadable in turn; sequences on one backend (its action-state cache); memory that changes during the read: each of 30 scene variants swapped in before read 1 … 85, and back; 3,500 seeded random samples in sequences.
- **Differences found between Lua and C: one** (the nil matrix entry, above; resolved by not scripting what C cannot express).
- Coverage: 136 executable lines of locomotion.lua, 135 hit; the other is the constant field `sprint_evidence=` of the answer table (line 154), which LuaJIT builds without a line event.
- Mutation check: 1,322 single changes, 261 did not compile, **961 killed, 100 survived**. The first pass left 127; added after it: `on_foot` called directly (787 tests, with the scene changing between its reads), the negative-w rotations, references with wide generation bits and the largest index, a registry of one, a flag row with its last byte set. The 100:
  - sizes of local arrays and array parameters (37) and start values of locals that are always written before they are read (15);
  - the two asserts retail can never fail (13): `'Motion rotation axis changed'` and `'Motion quaternion unavailable'`. An axis of one valid basis expressed in another valid basis has length 1 within the skew tolerance, and a product of unit quaternions is a unit quaternion; no input reaches them;
  - the handedness limit `.95` (4): a basis that passes the skew test has handedness above .99 or below -.99;
  - exact-equality boundaries (4): skew exactly at the limit, angle exactly at the cap, facing exactly .00001, w exactly 0;
  - "first refusal wins" (7), as in section 3, including the value of the "read refused" code of the Unit re-check;
  - the read-size guard and the 2^63 edge of `lua_hex` (5); `avatar_index < 8` in `on_foot` (3): an index of 8 or more never matches the snapshot's own flags address, so the answer is "no" either way;
  - `math.min` / `math.max` ties (2); the skew loop bound (2: the extra round has no pair); one element past the end where the memory happens to agree (2); the value of the test switch (2: any non-zero is "on"); the layer-count argument next to a NULL layer list (2); the sign of w when it is negative (1: a negative w always exceeds the 120 degree cap, which then replaces w); clearing the error text at the start of `to_parent` (1: every path sets it again).

### Open doubts

- The two open sprint tests above. This is the largest known gap between the shipped C and retail: on this build the weapon will not show the sprint pose for weapons whose layer-3 sprint state is not 71 until the gait bit is established and its switch turned on.
- `on_foot` accepts scene 3 or 4, as retail's Lua does. `src/camera_helper.c` (the translated camera DLL) accepts scene 3 only, as retail's DLL does. Both are faithful to their originals; they disagree with each other in retail too.
- `atan2`, `sin`, `cos`, `acos`, `sqrt`: as for motion, proven with glibc; the Windows runtime's last bit was not run.
- Other engineers' untracked `src/backend_ops.c` also declares `tfp_ops_alignment_context` and `tfp_ops_on_foot`. Mine are `tfp_alignment_context`, `tfp_alignment_executable` and `tfp_on_foot` (no name clash), and the two modules only use them through `TfpActionStateBackend` / `TfpLocomotionBackend`, so whoever joins the parts can bind either translation.

## Open constants, all four modules

| Where | What | State |
|---|---|---|
| `src/locomotion.c` `B_SPRINT_GAIT_ESTABLISHED` | flag byte 9, bit value 4 = sprint gait | open; the test answers "no" |
| `src/locomotion.c` `B_BRIDGE_SPRINT_ESTABLISHED` | layer 14 = 3, layer 3 = 68, layer 1 = 75 / 78 = bridge sprint | open; the test answers "no" |
| `src/action_placement_data.h` | the catalogue's weapon type ids on build 25480438 | taken from the kit (built for 26251122), not checked on this build; affects the menu label and the opt-in family defaults only |

Everything else these modules read is established by the migration records, this build's executable image, or a file of this repository that reads the same field on this build; the tables in sections 3 and 4 say which.

The posture bits `action_select.c` reads in the flag row (byte 9 bits 5-6, byte 10 bit 1, byte 12 bit 5) are the ones `src/placement.c` reads on this build.
