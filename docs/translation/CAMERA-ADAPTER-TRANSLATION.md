# Camera adapter: translation notes

`src/camera_adapter.c` / `src/camera_adapter.h` translate retail `src/first_person/camera.lua` (464 lines, 0.5.41-smooth.66, commit 1d0d453): the Lua side of the camera helper. The Lua file is the authority. Nothing was improved, simplified or reordered; where the C differs, it is listed here.

The retail file has two layers and both are in the one C unit:

- `Adapter` (lines 12-256): the FFI wrapper. Declares the DLL's functions, loads it, owns the generation handle, converts statistics.
- `Camera.new` (lines 260-462): the lease manager with the phases `idle` / `waiting` / `active` / `cleanup_pending`.

On the shipped path `Camera.new(backend, log, nil, clock)` (`runtime.lua:685`) always builds the real `Adapter`, so the pair is translated as it runs together. `Camera.Adapter` has no other user in the kit.

## Interface

Everything from outside comes through `TfpCameraAdapterApi`; the only state is the caller's `TfpCameraAdapter`.

| Lua | C |
|---|---|
| the DLL's `NADSC_*` functions | `api->helper`, a `TfpCameraHelperApi` of nine function pointers with the signatures of `camera_helper.h`; in the mod `TFP_CAMERA_HELPER_LINKED` |
| `Adapter:_load()` (`ffi.load`, platform and ABI asserts) | `api->load` (may be NULL: helper linked in). Asked before the first helper use and again until it says yes; a refusal carries the raised error's text |
| `backend:camera_config()` through `invoke` | `api->camera_config` → `TfpCameraBackendConfig` (14 helper fields with a presence bit each, `camera_kind`, `presentation_enabled`), or false with an optional text |
| `backend:snapshot()` through `invoke`; `kind(backend.snapshot)=='function'` | `api->snapshot` (may be NULL) → `TfpCameraBackendSnapshot {has_weapon_unit, weapon_unit}` |
| `controller:result().mode ~= 'pose_offset'`, `controller:is_enabled()` | `api->controller_result` → `{pose_offset}`, `api->controller_is_enabled`; called in the same order, `is_enabled` skipped when closing |
| `clock` (may be nil) | `api->clock` (may be NULL), seconds as a double |
| `log` (may be nil) | `api->log` (may be NULL) |
| `profile.lua` `camera.abi`, `.handoff`, `.cut_hold`, `.eye_hold`, `.ads_anchor`, `.seated_head` | `TfpCameraAdapterProfile`; `TFP_CAMERA_ADAPTER_PROFILE_SHIPPED` = `{1, {0,0}, true, true, true, true}` |

Operations: `tfp_camera_adapter_init`, `_update`, `_prepare` (also retail `prearm`, the same function), `_stop`, `_shutdown`, `_preflight`, `_set_handoff`, `_set_cut_hold`, `_is_clean`, `_result`, `_status`. Each returns Lua's first result and writes the second into a `TfpCameraText` (present or nil).

## Dropped, with the reason

Decided from the Lua code, `backend.lua:374-396` (the only maker of the configuration), `runtime.lua` and `profile.lua`.

| Part | Lines | Why nothing on the normal path reaches it |
|---|---|---|
| `offsets`, `Adapter:set_offsets`, `Camera:set_offsets`, the `config.right` branch of `prepare` | 5-9, 156-160, 354-358, 391-394 | `config.right` is `offset and offset.x or nil` with `offset` set only while `backend.fird_camera_active`; that flag is set only by `fird_person.lua`, whose menu entry is parked (`runtime.lua:1056`). `Camera:set_offsets` is called only by `head_placement.lua` under `fird:is_enabled()`. The translated helper has no `SetOffsets` |
| kick: `Adapter:set_kick`, `kick_stats`, `Camera:kick_stats`, the `config.kick_enabled` branch | 220-229, 318-321, 414-417 | `kick_enabled=false` is a literal in `backend.lua:390`; `kick_stats` is debug API only |
| free aim: `Adapter:free_aim`, `free_aim_stats`, `Camera:free_aim`, `free_aim_stats` | 161-165, 240-245, 346-353 | Menu test button and debug API only (`runtime.lua:926-934, 1123-1124`); the DLL refuses it; the translated helper has no such export |
| parent observation: `observe_parent`, `parent_stats` (both layers) | 230-239, 322-325, 342-345 | Free-aim button and debug API only |
| profiling: `begin_profile`, `profile_stats`, `Camera:profile`, `profile_stats` | 166-175, 310-317 | Debug API only (`runtime.lua:1116-1117`) |
| `handoff_stats` (both layers) | 205-215, 338-341 | Debug API only; not in the helper table asked for |
| camera motion: `Adapter:set_motion`, `Camera:set_motion`, `pending_motion` | 216-219, 298-309 | No caller anywhere |
| cdefs of the dropped exports, the duplicate `NADSC_ParentStats45` block | 19-54 | Declarations only |
| `Host.scope.native_ads_camera_libraries` registration | 80-83 | Keeps the loaded DLL referenced by the mod scope; no counterpart for a linked helper, nothing observable |
| asserts of `_load` (Windows x64, struct sizes 88/80, mod directory) | 55, 75, 78 | Sizes are `_Static_assert` in `camera_helper.h`; the rest is the loader's business. A failing load still exists as `api->load` |
| asserts of `Camera.new` (backend is a table, module loader, profile present) | 261-264 | Construction-time programming errors; C takes the struct |

Kept although debug-only, because the task names them and they change what `prepare` sends: `Camera:set_handoff`, `Camera:set_cut_hold` (debug API `camera_handoff`, `camera_cut_hold`), `Camera:preflight` with `Adapter:validate` (debug API `preflight`). `presentation_enabled` is kept as a field although the shipped backend always says true.

### Checks inside live functions that cannot fire

The manager's handle is always the adapter's owned handle, never a retired one (begin adopts whatever the adapter owns; a successful finish clears both in one step). The Lua runner asserts this after every one of the 10,060 operations, with three more invariants used below. These Lua branches are therefore not in the C:

| Lua | Why unreachable |
|---|---|
| `stats`: "Head-camera statistics handle is not owned" (99-101); `return handle.stats` for a disarmed handle (102) | handle = owned, never disarmed |
| `stats`: `if not called then … error(read,0)` (109); `begin`: "Head-camera arm result uncertain: …" (149); `finish`: `before=nil` after a raised read (251); `_error`: `'unavailable'` (89) | an FFI call raising: a C function pointer cannot raise, and `_error` is only reached after the helper answered |
| `validate`, `begin`: "Head-camera generation still requires cleanup" (138, 144) | `preflight` refuses with a handle first; `prepare` stops the old lease first and returns when that fails |
| "Camera not owned" in `set_presentation`, `set_handoff`, `set_cut_hold`, `set_weapon_key` (177, 182, 194, 201) | called only with the owned handle |
| `set_cut_hold`: "Camera hold mode must be 0 to 15" (195) | the mode is a sum of four booleans |
| `finish`: "Head-camera cleanup handle is not owned", `return true,handle.stats`, "Head-camera generation ownership changed" (247-249) | as above |
| `same_identity`: `return false` for a missing table (269) | where it is called, an identity is bound (kept in C as `!has_bound`, never taken) |
| defaults `'Head-camera statistics unavailable'` (281), `'Head-camera cleanup remains pending'` in `stop` (362), `'Head-camera policy was not accepted'` (389), `'Camera handoff ease refused'` (402), `'Camera exit hold refused'` (411) | the value before `or` is always a text there |
| `stats_handle` | a cache exists only for the owned handle; `has_stats_cache` is enough |
| `kind(helper.set_handoff)=='function'`, `kind(helper.set_cut_hold)`, `kind(helper.set_weapon_key)`, `kind(helper.profile)=='table'` (399-409, 452) | always true for the real `Adapter` |

## Lua specifics and how each was carried over

- **nil against false.** Second results are text-or-nil: `TfpCameraText.present`. An empty text is present (it is true in Lua: a backend refusing with `""` gets `""`, not the default text; scripted). The only first result that can be nil is `preflight`'s when validation raised: `tfp_camera_adapter_preflight` returns -1 / 0 / 1.
- **0 is true.** `failure_native_error = native_cause or helper.last_failure_code` and `failure_kind = … and 'unsafe'`: a code of 0 from `NADSC_Error` is a code, so the kind is `unsafe`. Carried with `has_last_failure_code` / `has_failure_native_error` flags (scripted with code 0 at every refusal).
- **pcall boundaries.** `invoke` turns a raised error into `nil, text`. What can raise on the live path: the load (→ `api->load` false with the text), a missing configuration field (`assert(config[name], name..' unavailable')` → presence bits, first missing field in list order), the backend calls (a raise and a `nil, text` return are the same to `invoke`; the Lua runner scripts both), the clock (`pcall(self.clock)` failing leaves `now` nil exactly as a NaN does; the C contract is "a failing clock returns NAN"; the runner scripts a raising clock against NAN in C). `pcall(self.stats, self, handle)` in `finish` swallows a refused read; same in C.
- **Raised assert text.** Lua puts `<chunk name>:95: ` in front of `game unavailable`. The chunk name is whatever the mod loader called the file, so it is not reproducible; the C text is the message alone and the runner takes the prefix off (and stops on any raise other than line 95). This is a difference in a player-visible text (see doubts).
- **Numbers.** Statistics counters become Lua numbers: `(double)uint64` and `(double)float`. `tostring` in `status()` is `tfp_camera_adapter_number`: `%.14g`, `nan`, `inf`, `-inf` as the reference LuaJIT prints (counters past 2^53, 1e15, floats, -0 scripted). Error codes print as unsigned integers. The weapon key and the hand-off values stay doubles until the helper call, with the Lua range and NaN tests (`key ~= math.floor(key)` is what refuses NaN) and the FFI's conversions to `uint32_t` / `float` at the call.
- **Integer against fractional configuration values.** `TfpCameraBackendConfig` carries the identity fields already typed (`uint64_t` / `uint32_t`); `forward`, `up`, `smoothing_ms`, `blend_ms` are doubles converted to float where the FFI did. A fractional, negative or out-of-range value in an integer field cannot be expressed (the backend reads these from memory as unsigned integers).
- **The statistics buffer** is one cdata reused for every read (`self.stats_buffer`); a helper that writes nothing leaves the previous read's values. Same in C (`stats_buffer`), scripted with a helper that returns 1 without writing.
- **The 0.1 s tick.** `(math.floor((now-.0333)/.1)+1)*.1+.0333` is evaluated in the same order with doubles; build with `-ffp-contract=off` (the run script does; ISO C mode already implies it on GCC).
- **Table identity.** `self.last` is the adapter's cache table itself while cached; it is reassigned after every successful read, so a copy behaves the same.
- **Text length.** Texts are held in 256-byte buffers; a longer backend text would be cut. Retail has no limit.

## The proof

- `tools/retail_ref/run_camera_adapter.lua` loads retail `camera.lua` and `profile.lua` unmodified under `/usr/bin/luajit` (interpreter only, see below). `PORT51.ffi` is LuaJIT's real `ffi` (real cdefs, real struct sizes, `os` reported as Windows) whose `load` returns a Lua table of scripted functions that read and fill the real cdata; `PORT51.kind` = `type`; `PORT51.number` = `tonumber` (compat51's `number` without a base is `tonumber` for non-userdata and `ffi.tonumber` for cffi cdata; LuaJIT's `tonumber` covers both). The adapter object is taken from the manager's upvalue to print its state and to assert the invariants.
- `tools/retail_ref/gen_camera_adapter.py` refuses any retail commit but 1d0d453 and a modified `camera.lua` / `profile.lua`, writes the scripts, runs them, and emits `tests/vectors/camera_adapter_vectors.h` with the commit and both SHA-256.
- `tests/camera_adapter_test.c` replays them with a C helper, backend, controller and clock built the same way. Compared after every operation: result and text; `result()` (phase, cleanup_pending, error_message, failure_native_error, failure_kind, all eleven statistics fields or their absence); `status()`; state (handle and generation, closing, weapon key, kept hand-off and hold choices, both issue notes, bound identity, loaded, statistics cache with its due time, stats_at, last_failure_code); and the ordered trace of every helper, backend, controller, clock, load and log call with arguments (the 88 configuration bytes handed to Validate/Begin, generations, float bits, modes, keys, the size/ABI the statistics block went in with, log texts).
- `tests/run-vectors-camera-adapter.sh` builds and runs it: `Camera adapter: 10060 operations in 18 runs … 837 arms, 5736 armed updates, 16146 helper calls`.

Runs: `session` (a play session in order), `refusals` (the helper refusing at every call with codes 0-19 and 4294967295; partial installation; cleanup owed and retried), `settings` (presentation, hand-off, hold, weapon key; set once, re-set after re-arm, switched live), `configs` (every way a configuration can be missing or change), `clocks` (the tick, grid points, time backwards, unusable clock, number printing), `loading` (helper not loadable, wrong orders), seven runs with no clock / no logger / no snapshot / other profiles, two free random walks (1,500 each) and three walks against a helper that answers a generation as the real one does (`follow`: only the live generation is served, generation 0 reads as nothing installed, a second Begin is refused; 2,000 each, about 90 % of the time armed).

### Differences met while proving

1. **Assert prefix** (above): retail returns `…camera.lua:95: game unavailable`; resolved by taking the chunk prefix off in the runner and documenting it.
2. **LuaJIT's compiler against its interpreter.** In a long run the same script gave `9.6411660427711406e+18` for a counter of 2^63 or more where C (and LuaJIT's interpreter) give `9.6411660427711386e+18`: compiled traces convert uint64 with two roundings. The runner switches the compiler off; the interpreter is the reference.
3. No difference in decisions, order, state or texts was found in the C after that. One generator mistake (a Python `True` written as Lua `true` where the Lua compares with 1) was fixed before the first clean run.

## Coverage

Line hook on the retail file over all 18 scripts: 378 lines carry code, 302 are reached. The 76 others are lines 6-8, 157-174, 206-244, 300-357, 392-393, 415-416 (all in the dropped parts above), 81-82 (mod scope registration) and 100 (statistics handle not owned).

Because the file packs several statements per line, the same scripts were also run on a scratch copy split to one statement per line (`/tmp/claude-1000/camera-adapter/cov`; its output is byte-identical to the original's for every script): 692 statement lines, 524 reached, 168 not. 144 of those are in the dropped functions. The 24 in live functions are exactly the unreachable branches of the table above (`_load` early return and scope registration 4, `stats` 4, `validate` 1, `begin` 2, the four "Camera not owned" and the hold-mode check 5, `finish` 4, `same_identity` 1, three `or` defaults in `prepare` 3). Three more `or` defaults share a line with reached code and were counted with a marker function instead: `'unavailable'` (89), `'Head-camera statistics unavailable'` (281) and `stop`'s `'Head-camera cleanup remains pending'` (362) are never evaluated; `'Head-camera configuration unavailable'`, `'Camera handoff preparation refused'` and `'Head-camera native policy disarmed'` are (21, 51 and 28 times).

No executable line of the live path is left unreached without one of these reasons.

## Mutation check

152 single changes to a scratch copy of `src/camera_adapter.c` (every comparison, flag, order, constant, text and conversion; list in `/tmp/claude-1000/camera-adapter/mut/mutate.py`): **143 killed, 9 survive**. The first pass left a tenth (`/ .1` replaced by `* 10` in the tick); clock values where the two round apart were added to `clocks`.

The nine survivors change nothing a caller can see:

| Change | Why equivalent |
|---|---|
| no `invalidate` after a refused statistics read; after an ABI change | every caller then stops the lease, and `finish` begins by invalidating |
| no `invalidate` at the start of `validate`; of `begin` | a cache exists only while a handle is owned (asserted in the runner); both run without one |
| no `invalidate` after End | invalidated just before End; nothing in between touches it |
| `same_identity` true without a bound identity | never called without one (runner assert: a handle outside cleanup has a bound identity) |
| `stop` without a handle not clearing the bound identity | no identity is bound without a handle (runner assert) |
| "player changed" path not clearing `fault` | the arm that produced this handle cleared it and nothing since set it |
| `update` not setting `active` at its end | a handle outside cleanup is always `active` (runner assert) |

These lines are kept because the Lua has them.

## Open doubts

1. **`<field> unavailable`**: retail shows the loader's chunk name and `:95:` in front; the C does not. Only reachable if the backend hands over a configuration with a field missing, which `backend.lua:388-391` does only if `_alignment_executable` or a snapshot field is nil.
2. **`api->load`** is my reading of how a missing DLL carries over (retail: every `validate` / `begin` raises the loader's text until the DLL loads). With the helper linked in it is NULL and the path does not exist.
3. **Number printing and uint64 conversion follow the reference LuaJIT's interpreter**, not the game's Lua 5.1 with cffi; they can differ only for NaN / infinite `blend` (MSVC prints NaN differently) and are the same for every value a real helper reports.
4. **A raising clock is NAN in C.** Same path in Lua; stated as a contract rather than modelled.
5. **256-byte texts** (see above).
6. The `follow` helper is a model of the generation rule only, written twice (Lua runner, C test); the real `camera_helper.c` was not put behind the adapter here.
