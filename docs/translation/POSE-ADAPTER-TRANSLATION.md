# Pose adapter: pose.lua in C

Translation of retail `src/first_person/pose.lua` (336 lines, True First Person 0.5.41-smooth.66, commit 1d0d453): the Lua adapter that drives the editor/pose helper (`NADSE_*`, already translated as `src/pose_helper.c`). The Lua file is the authority. `src/pose_adapter.c` is checked by replaying the retail file's own answers. The recipe is the one of `docs/translation/LENS-ADAPTERS-TRANSLATION.md`.

## Files

| File | What |
| --- | --- |
| `src/pose_adapter.h`, `src/pose_adapter.c` | The translation. |
| `tools/retail_ref/run_pose_adapter.lua` | Loads the retail file unmodified under `/usr/bin/luajit` and replays scripts against it. |
| `tools/retail_ref/gen_pose_adapter.py` | Writes the scripts, runs them, emits the vectors. Refuses any retail commit but 1d0d453. |
| `tests/vectors/pose_adapter_vectors.h` | 21,512 operations in 19 runs with retail's answers; records the commit and the SHA-256 of `pose.lua` and `profile.lua`. |
| `tests/pose_adapter_test.c`, `tests/run-vectors-pose-adapter.sh` | Replays the vectors against the C. `tests/run-native.sh` picks the script up by its name. |

Regenerate (only when the retail reference is deliberately moved):

    python3 tools/retail_ref/gen_pose_adapter.py --retail "$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit" --out tests/vectors

Regeneration is byte-identical run to run.

## Interface

- `TfpPoseHelperApi`: 17 function pointers with the signatures of the `tfp_pose_*` exports in `pose_helper.h` (Validate, ValidateInput, the four Begin variants, SetPlacement, SetADSPlacement, SetTransform, SetAuthoredTransform, SetZoomPolicy, SetTransition, End, ReadStats, ReadDepthStats, ReadInputStats, Error). The real exports fit the table as they are (compile-checked). `NADSE_SetMotion` is declared by the Lua but never called, so it is not in the table.
- `TfpPoseAdapterIo`: the time of the statistics display tick. `clock` is the function given to `Pose.new`; `backend.lua:807` gives none, so the shipped path is `tick` (GetTickCount64, milliseconds, divided by 1000).
- `TfpPoseAdapterProfile`: `abi` (2) and `normalize_zoom_input` (true) of `profile.lua`'s `pose` table.
- State: one `TfpPoseAdapter` (owned handle, the shared display counters with their handle, last time and due time, the three reused read buffers). A handle is a `TfpPoseHandle` the caller provides storage for; the prepare token is a `TfpPosePrepared`.
- Calls, one per Lua method: `tfp_pose_adapter_stats`, `_depth_stats`, `_input_stats`, `_prepare`, `_begin`, `_set_placement`, `_set_ads_placement`, `_set_motion`, `_set_zoom_policy`, `_transition`, `_owned_handle`, `_finish`.
- pose.lua writes no log line, so there is no logger.

## Carried over from Lua

| Lua | C |
| --- | --- |
| A raised error against a returned `false,text` | `prepare` and `begin` return `TfpPoseAnswer`: `TFP_POSE_RAISED` carries the assertion's text. `backend.lua:604-625` words its own refusal differently for the two, so they are kept apart. |
| `assert(config[name], name..' unavailable')`, `assert(... 'Pose configuration unavailable')`, `assert(... 'Invalid native pose preparation')` | Lua puts `<chunk>:<line>: ` in front. C builds the same text from `TFP_POSE_LUA_CHUNK` and the retail line (142, 146, 226). A field that is `false` fails the assertion like a missing one: both are a set bit in `TfpPoseAdapterConfig.missing`. |
| `nil` arguments | A handle, a number, the Boolean of `set_zoom_policy` or the packet passed as NULL is `nil`. The packet's fields have `has_*` flags. |
| `ipairs({x,y,z})` in `set_placement` stops at the first nil; `pairs({x=x,y=y,z=z})` in `set_ads_placement` skips a nil and goes on | The two loops differ in the same way, so `(0.6, nil, …)` is "must be finite" in one and `(nil, 0.6, …)` is "requires XYZ" there but "Invalid ADS placement" in the other. |
| `packet.epoch%1~=0` | `epoch != floor(epoch)`: same for every finite value and NaN; an infinity is caught by the bounds next to it. |
| The statistics table shared between reads (`stats_cache`) | Value copies: `has_cache` + a cached view, written back after every read while it is shared. |
| `stats_buffer`, `depth_buffer`, `input_buffer` reused between reads | The buffers live in the adapter struct: a read the helper does not fill shows the previous values, as in Lua. |
| `tonumber(uint64)`, number to `float` / `uint64_t` at the FFI call | Counters are kept as `double`; a C cast at the same place. |
| `(math.floor((now-.085)/.1)+1)*.1+.085` and `norm=norm+value*value` | The product goes through a `volatile`, so no compiler setting can fuse it with the addition (see "Differences met"). |

## Dropped, with the reason

| Retail | Why it cannot be reached |
| --- | --- |
| `_load` (38-90): `ffi.cdef`, `ffi.load`, the Windows / ABI-size / mod-directory asserts, `Host.scope.native_ads_pose_libraries` | The helper is compiled into the mod and reached through the pointer table; the struct sizes are `_Static_assert`s in `pose_helper.h`. The runner still checks that retail registers the library in the scope once. |
| `'Depth statistics ABI layout mismatch'`, `'Input statistics ABI layout mismatch'` (166, 196) | `ffi.sizeof` of a struct the file has just declared; the same `_Static_assert`s. |
| `'unavailable'` in `_error` (96) | Only when `NADSE_Error` itself raises. |
| `pcall` around `NADSE_ReadStats`, the two diagnostic reads, Begin and `stats` inside `finish`: `error(read,0)`, `'Optional depth statistics unavailable: …'`, `'Optional input statistics unavailable: …'`, `'Pose arm result uncertain: …'`, `before=nil` (109, 171, 201, 240, 327) | A C function cannot raise a Lua error. |
| Handles that are not tables, numbers that are strings, a zoom state or `ads` that is neither nil nor Boolean, a packet that is not a table | The C types cannot hold them. Each takes the same refusal as the nil case, which is kept. |
| A clock that returns something that is not a number | Same path as a clock that raises (no time for this read): the callback returns false for both. Both are in the reference scripts. |

Kept although the backend never does it: both profile switches (`normalize_zoom_input` off selects `NADSE_Validate` / `Begin` / `BeginSteady`), a clock given to `Pose.new`, `set_zoom_policy` (its only caller is parked), `depth_stats` and `input_stats` (console diagnostics).

## Proof

`tests/run-vectors-pose-adapter.sh`:

    Pose adapter: 21512 operations in 19 runs answered as retail's pose.lua (1d0d453): same results, texts, state and helper calls; 5416 refused, 190 raised; 680 with Validate, 583 with Begin, 3859 with a setter, 13002 with a read, 672 with End; 31494 helper and clock calls

Compared after every operation, as one text line built the same way on both sides: the result (true / false / raised) and its text or statistics (all 12, 17 or 14 fields); the owned generation and which handle owns it; the shared counters, their handle, last time and due time; every handle of the script (generation, disarmed, final statistics present); and the ordered trace of every helper and clock call with its arguments (all 20 fields of the configuration struct, generations, floats to 17 digits, the size / ABI / reserved the read blocks went in with).

Runs: `session` (prepare, Begin, the poll on its 10 Hz tick, placement, motion, zoom, phase publication, End, a steady entry; 104, in four variants), `refusals` (every helper call refusing with codes 0-19 and 4294967295; partial installation; cleanup owed and retried; statistics of another ABI, unfilled, with extreme values; preparation while armed or with the helper's state moving; 780, two variants), `inputs` (configuration nil / each of the 17 fields missing / false; tokens nil and foreign; every range edge of placement, motion, quaternion and phase packet; nil in every position; 441, two variants), `handles` (every method with no handle, the owned one, a foreign one, one never begun, a disarmed one, in every state of the adapter; 256, two variants), `clocks` (grid points, time backwards, failing clocks, ticks up to 2^64-1; 714, with a clock, with the tick, with no time at all), three random walks (8,000) and three runs against a small model of the real helper's token rule (`calm`, 8,000, about 98 % of the time holding a generation). The variants cover clock / tick / neither and `normalize_zoom_input` on / off.

The test was written first and failed against an empty adapter (`C F|~|… retail T|10,11,20,…` at operation 1). The first full C draft passed all vectors.

### Lua coverage

Line hook on the retail file over all 19 scripts: **237 lines carry code, all 237 are reached.**

Because the file packs several statements per line, the same scripts were run on a scratch copy split to one statement per line (its answers are identical but for the line numbers inside assertion texts): 316 statement lines, 307 reached. The 9 others:

| Statement | Why |
| --- | --- |
| `return self.library` (81) | `library` is never set without `ffi`. |
| `self:_invalidate_stats()`, `error(read,0)` (109) | `NADSE_ReadStats` raising. |
| the two `ABI layout mismatch` returns (166, 196) | `ffi.sizeof` cannot differ. |
| the two `Optional … statistics unavailable` returns (171, 201) | the diagnostic read raising. |
| `'Pose arm result uncertain: …'` (240) | Begin raising. |
| `before=nil` (327) | `stats` raising inside `finish`. |

All nine are in the dropped table. One `or` default shares a line with reached code and is never evaluated: `'unavailable'` (96). Every refusal text of the file other than these appears in the vectors (counted: 27 returned texts, 19 assertion texts, 12 `… failed (…)` operations each with and without a reason, all 18 reasons).

### Mutation

Every single change the rule list produces on a scratch copy of `src/pose_adapter.c` (comparisons, logic operators, flags, constants, arithmetic, dropped statements and calls, swapped helper entries, changed texts and answers): **527 mutants, 506 killed (96.0 %), 21 survive.** Two real gaps were found and closed on the way: no script had a quaternion whose length differs from 1 enough for `|w|/sqrt(norm)` and `|w|/norm` to fall on different sides of 0.5, and no run had neither a clock nor a tick.

The 21 survivors change nothing a caller can see:

| Change | Why equivalent |
| --- | --- |
| `reasons[0]` not NULL | index 0 is never read |
| `stats_at` / `stats_due` not zeroed on invalidate (2), initial values of `now` and `value` (2), fill byte of the view and config `memset` (2), `c->reserved = 0` after the `memset` | dead values: under a false flag, or every field is assigned afterwards |
| `shared = has_cache || …`; two of the `||` in the refresh test | a cache always belongs to the owned handle and exists only with a time (asserted in the runner after every operation) |
| loop bound over the 17 `missing` bits | bit 17 is never set |
| no `a->owned = handle` at the end of `begin` | result 1 already requires a generation, which set it (retail line 247 is redundant too) |
| `identity[1]` for `identity[2]`, `identity[0]` for `identity[1]` | the same value 0 |
| `fabs(norm-1) >= .002` | `norm-1` is exact and a multiple of 2^-53; 0.002 as a double is not, so equality cannot occur |
| `9007199254740991.1` | the same double |
| `ceil` for `floor` in the whole-number test | the same predicate |
| `stats->has` at the start of `finish` (2) | overwritten on success; not read on failure |
| no invalidate after End | the read that follows has no handle, hence no time, and invalidates anyway |

These lines are kept because the Lua has them.

Tools (scratch, not in the repo): `/tmp/claude-1000/-home-goose-Documents-ChatGPT-Linux-HD2--claude-worktrees-wj-weather-control-menu-355402/79c2f14a-8cee-438f-b054-2a5d60974797/scratchpad/pose/mutate.py`, `cover.lua`, `split.py` in the same folder (the last two copied from the lens adapters' tools).

## Differences met while proving

1. None in decisions, order, state or texts.
2. **Fused multiply-add.** Built with `-std=gnu11 -O2 -march=native -ffp-contract=fast` the first draft gave a due time of `10.185` where retail has `10.185000000000002`: writing `due*.1` and `+.085` as separate statements does not stop GCC from fusing them. The product now goes through a `volatile` (also in the quaternion norm); the file passes under those flags, under `-std=c11` with ASan/UBSan, with clang, with mingw and with the mod's own `zig cc -target x86_64-windows-gnu -O2` line.

## What could not be made equal

1. **A helper that raises.** Retail has five `pcall`s for it. With the helper linked in there is no such event; if the host can turn a fault inside the helper into an error, "Pose arm result uncertain" has no C counterpart.
2. **The returned statistics table.** In Lua `stats()` hands out the cached table itself, so a table a caller kept changes at the next read (the backend stores it in `lease.pose_stats`). C returns a copy. Inside pose.lua no path reads an old table after a new read (`prepare` compares two reads, but a cache only exists while armed, and then `prepare` has already refused).
3. **Number conversion of configuration fields.** Lua assigns doubles to `uint32_t` / `uint64_t` struct fields; C takes typed integers. Fractions, negatives and out-of-range values were not modelled; `backend.lua:839` passes addresses, unit numbers and millisecond counts.
4. **Reference runtime.** The reference is LuaJIT's interpreter (`jit.off()`: FFI callbacks, and one rounding for uint64 above 2^53). The kit runs on Lua 5.1 with cffi. They can differ in the chunk prefix of assertion texts (the loader's choice of chunk name) and nowhere else that is reached here; the adapter prints no numbers except the helper's error code.
5. **Texts are capped at 255 bytes** (`TFP_POSE_TEXT`); the longest retail text is 119.
6. **`-ffast-math` is not supported**: it turns `/1000` and `/.1` into multiplications and the tick time moves by one unit in the last place.

## Open doubts

- The small model of the real helper in the `calm` runs (only the live generation is served; generation 0 only while nothing is reserved; a second Begin is busy) is my reading of `pose_helper.c`, written twice (Lua runner, C test). It only decides which paths the long runs spend their time in; every answer is still compared against retail. The real `pose_helper.c` was not put behind the adapter here.
- The handle's storage is the caller's. Lua keeps a handle alive as long as anyone holds it; in C it must outlive its last use (the backend keeps it in the lease).
- `fov_adapter.c` and `blur_adapter.c` compute their staggered tick in separate statements for the same purpose. Under `-ffp-contract=fast` on a CPU target with FMA that is not enough (item 2 above). The mod's build line (`-std=c11`, baseline x86-64) does not fuse, so nothing is wrong today. Not touched: other files.
