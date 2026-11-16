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
