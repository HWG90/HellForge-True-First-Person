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
