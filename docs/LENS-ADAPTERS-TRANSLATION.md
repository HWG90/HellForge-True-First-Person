# Lens adapters: fov.lua, near_clip.lua, blur.lua in C

Translations of three retail modules of True First Person 0.5.41-smooth.66 (commit 1d0d453): the Lua adapter and lease of the field-of-view, near-clip and scoped-blur helpers. The Lua files are the authority. Each C file is checked by replaying the retail file's own answers.

## Method (all three)

- `tools/retail_ref/run_<m>_adapter.lua` loads the retail file's bytes unmodified under `/usr/bin/luajit`, under the chunk name the mod requires it by (`src/first_person/<file>.lua`). `PORT51.ffi` is LuaJIT's real `ffi` (the module's own `cdef`, real struct sizes) with `os='Windows'` and a `load` that returns a table of real C function pointers (FFI callbacks), so argument and result conversions are LuaJIT's. `PORT51.kind` is `type`, `PORT51.number` is `tonumber` (compat51.lua defines them so that cdata is `'cdata'` and converts to a number, which is what LuaJIT's own do). `profile.lua` is the retail file.
- `tools/retail_ref/gen_<m>_adapter.py` refuses any retail commit but 1d0d453, writes the scripts, runs them and writes `tests/vectors/<m>_adapter_vectors.h` with the commit and the SHA-256 of the retail file and of `profile.lua`.
- One operation = one call on the lease (`update`, `stop`, `shutdown`, `preflight`) or straight on the adapter (`a_*`, for the orders a lease never produces), with everything the helper, backend, controller, clock and options answer during it. A scripted helper answer below zero asks a small model of the real helper (generations, ownership, error 17 for a foreign token) that the Lua runner and the C test implement alike; the "calm" runs use it so that they spend their time armed.
- The comparison is one text line per operation, built the same way by the Lua runner and by `tests/<m>_adapter_test.c`: both results of the call, every state field of the lease and of the adapter, `result()`, `status()`, and the ordered trace of every helper, backend, controller, clock and log call with its arguments (all config struct fields). Any difference fails `tests/run-vectors-<m>-adapter.sh`.
- Coverage: `/tmp/claude-1000/lens-adapters/cover.sh` splits a scratch copy of the retail file to one statement per line (so a branch on a shared line counts on its own) and runs every script under a line hook.
- Mutation: `/tmp/claude-1000/lens-adapters/mutate.py` applies single changes (comparison and logic operators, constants, dropped statements and calls, flipped flags) to a scratch copy of the C file; each must make the test fail.

## Carried over from Lua (all three)

| Lua | C |
| --- | --- |
| `nil` against `false`, and 0 / "" being true | Every optional value has its own `has_*` flag (or 0 = nil for identities that are never 0). A text result is a pointer, NULL = nil, so an empty text stays a text. |
| `pcall` in `invoke()`: a raised error becomes `nil, tostring(error)` | The adapter functions return false with the text; the lease only tests `~=true` / "not a table", so a raise and a refusal are the same to it. A backend that raises = a backend that returns false with the text. |
| `assert(config[name], name..' unavailable')` | Lua's `assert` puts `<chunk>:<line>: ` before the message, and the text reaches the log and the status. C builds the same text from `TFP_<M>_LUA_CHUNK` and the retail line number. |
| Tables with identity (`self.bound==lease`, handles) | The pose lease carries an `identity` number; a handle is a struct compared by address. |
| The statistics table shared between reads (fov, blur) | Value copies: `has_cache` + a cached view. Within the lease no table is changed while another reference to it is read (checked path by path), so copies behave the same. |
| `stats_buffer` reused between reads | The buffer lives in the adapter struct: a read the helper does not fill shows the previous values, as in Lua. |
| Numbers: `tonumber(uint64)`, `tostring(number)` | Counters are kept as `double`; texts use `"%.14g"` with `nan`, `inf`, `-inf` as LuaJIT prints them. |
| Number to `float` / `uint64_t` at the FFI call | A C cast at the same place. |

## fov (`src/fov_adapter.c`, `src/fov_adapter.h`)

Interface: `TfpFovHelperApi` (seven pointers with the `tfp_fov_*` signatures), `TfpFovIo` (`fov_config`, `presentation_transition`, the controller's `result`, `is_enabled`, `lease`, the clock, the logger), `TfpFovProfile` (abi 1, ads_blend_ms 180). State: one `TfpFovLease` (holds its `TfpFovAdapter`). Calls: `tfp_fov_lease_update / stop / shutdown / preflight / result / status / is_clean`; the adapter's own `tfp_fov_adapter_*` are public so the test can call them in wrong orders.

Dropped, with the reason:

| Retail | Why it cannot be reached |
| --- | --- |
| `_load`: `ffi.cdef`, `ffi.load`, the Windows / ABI-size / mod-directory asserts, `Host.scope.native_ads_fov_libraries` (13-47) | The helper is compiled into the mod and reached through the pointer table; the struct sizes are `_Static_assert`s in `fov_helper.h`. A DLL that fails to load has no C counterpart. |
| `'unavailable'` in `_error` (51) | Only when `NADSF_Error` itself raises. |
| `pcall` around `NADSF_ReadStats` and `NADSF_Begin`: `error(read,0)`, `'FOV arm result uncertain: …'`, `before=nil` (72, 112, 150) | A C function cannot raise a Lua error. |
| `NADSF_ValidateFird`, `BeginFird`, `BindFird` | Called only by `fird_fov.lua` (Fird Person, parked: `fird_person.native_animation_default=true`, started by nothing on the shipped path). |
| `Fov.new` asserts (164-167) and a caller-supplied `helper` | `runtime.lua:683` passes `backend, log, nil, clock`. |
| Non-table handles, non-string texts, a config that is neither nil nor a table | The C types cannot hold them; the backend returns a table or `nil, text`. |

Kept although the lease alone cannot reach it (reached by the direct adapter calls of the test): the handle ownership checks of `stats`, `bind`, `transition`, `finish`. Kept and unreachable: `collect()` without a handle (178), `return self.library` (39).

Coverage: of the retail file's executable statements the scripts miss 39 (`return self.library`: `library` is never set without `ffi`), 72 / 112 / 150 (helper raising, see above) and 179 (`collect()` is never called without a handle). Nothing else.

Mutation: 180 single changes in three seeded batches, 165 killed. The 15 survivors (11 distinct) are all without effect: dead values under a false `has_*` flag (`stats_handle`, `stats_at`, `bound_request`, `bound_weapon`, the unused `cause` argument), a buffer size, `9007199254740992.0 -> .1` (same double), `text=NULL` initialisers the callback always overwrites, `stats->has` on a failed finish (callers do not read it), the cause passed to "disarmed while waiting" (collect has just stored the same number as the helper's last failure code), `stats_handle ~= handle` in the refresh test (a cache always belongs to the owned handle), `origin && origin_has_weapon` (the second flag is never set without the first), and the invalidation after End (the following read has no clock sample and invalidates anyway). Two real gaps were found and closed on the way: the refusal script shut the lease down in its first round (all later rounds only stopped), and the last counters before End were never read inside a 10 Hz tick with changed values.

Differences found while matching: none in behaviour. Two texts were wrong in the first C draft and corrected from the reference: assertion texts carry Lua's `chunk:line:` prefix, and a nil `request` / `weapon` at Bind raises `src/first_person/fov.lua:123: bad argument #2 to 'NADSF_Bind' (cannot convert 'nil' to 'uint64_t')`.

Open doubts:
- The chunk-name prefix of assertion texts depends on how the host's `mod.require` names the chunk; the reference uses the required path. The retail kit runs on Lua 5.1 with cffi, where the nil-argument text of Bind is cffi's, not LuaJIT's. Both only appear if `backend:fov_config` returns a table with a field missing, which `backend.lua:417` never does.
- Texts are capped at 255 bytes (`TFP_FOV_TEXT`); Lua has no cap. The longest retail text is under 100; a backend text could be longer.
- `tostring` of `nan` / `inf` follows LuaJIT; Lua 5.1 on Windows prints them differently. They only appear if the helper reports such a float.

## near (`src/near_adapter.c`, `src/near_adapter.h`)

Interface: `TfpNearHelperApi` (five pointers with the `tfp_near_*` signatures; `tfp_near_read_sites` is not called by the Lua), `TfpNearIo` (`fov_config`, the controller's `result`, `is_enabled`, `lease`, the menu's `options`, the clock, the logger), `TfpNearProfile` (abi 1, pull_sight_distance true). State: one `TfpNearLease`. Calls: `tfp_near_lease_update / stop / shutdown / result / status / is_clean`, and the public `tfp_near_adapter_*`.

`update` has three answers: true, false, and `TFP_NEAR_RAISED`. Retail calls the clock unprotected (near_clip.lua:194, 203) and the runtime's clock raises on a bad engine sample (`runtime.lua:167-169`), so the update ends at that point with everything done before it kept (a Set already sent, the new distances remembered). The clock callback returns false with the message; `runtime.lua` catches it in `attempt()`.

Dropped, with the reason:

| Retail | Why it cannot be reached |
| --- | --- |
| `_load` (12-46), `'unavailable'` in `_error` (49) | As for fov. |
| `pcall` around `NADSN_Begin` (`'Near clip arm result uncertain: …'`, 81) and around `stats` in `finish` (`before=nil`, 100) | A C function cannot raise a Lua error. |
| `clock or os.clock` and the default `options` (119) | `runtime.lua:713-716` passes both. |
| `Near.new` asserts (114-117) | Constructor preconditions. |
| `options()` raising, a clock returning something that is not a number | The menu getter and the clock are C; the runtime clock returns a finite number or raises. NaN, infinities and a clock stepping back are kept and compared. |

Kept: the profile switch `pull_sight_distance` both ways (shipped: true; the false branch is exercised through an adapter built on a copy of the retail profile with the switch off).

Coverage: the scripts miss 39 (`return self.library`), 81 and 100 (helper raising) and the body of the default `options` function (119). Nothing else.

Mutation: 180 single changes, 163 killed. One real gap found and closed: no script had a distance between 1 and 2, so `aim > 1` could become `aim > 2`. The other 16 survivors (11 distinct) are without effect: initialisers the callbacks overwrite, dead values under a false `has_*` flag (`next_collect`, `hip`, `aim`), `stats->has` on a failed finish, the loop bound over the six `missing` bits (bit 6 is never set), `memset` fill bytes of fully assigned structs, and the NULL-text paths of `stop_keeping` / `refuse` (a failed collect always carries a text).

Differences found while matching: none; the first C draft passed all vectors.

Open doubts: as for fov (chunk name of assertion texts, text cap, `nan` / `inf` texts). One more: `"Native near clip disarmed (code N)"` prints the helper's error as an integer; a `last` table without `error` cannot occur there.
