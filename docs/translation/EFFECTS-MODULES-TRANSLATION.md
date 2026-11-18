# Effects modules: seven small retail Lua files in C

Translations of seven retail modules of True First Person 0.5.41-smooth.66 (commit 1d0d453): `dof.lua`, `vignette.lua`, `aim_markers.lua`, `first_person_movement.lua`, `camera_compat.lua`, `native_ads_option.lua`, `diagnostics.lua`. The Lua files are the authority. Each C file is checked by replaying the retail file's own answers. No name collided with an existing file in `src/`, so every module is `src/<m>.c|h` (`src/markers.c` is an older, unrelated file).

## Counts

| Module | Lua lines | C (`.c` + `.h`) | Operations / runs | Lua statement lines, not reached | Mutants killed / tried |
| --- | --- | --- | --- | --- | --- |
| `dof` | 266 | 454 + 178 | 37188 / 27 | 313, 12 | 537 / 600 |
| `vignette` | 127 | 323 + 139 | 23254 / 135 | 183, 10 | 556 / 600 |
| `aim_markers` | 262 | 514 + 165 | 13920 / 39 | 291, 14 | 993 / 1050 |
| `first_person_movement` | 248 | 473 + 135 | 16574 / 15 | 292, 15 | 552 / 600 |
| `camera_compat` | 181 | 344 + 121 | 10343 / 26 | 212, 7 | 672 / 750 |
| `native_ads_option` | 93 | 267 + 109 | 6606 / 15 | 127, 1 | 554 / 600 |
| `diagnostics` | 84 | 218 + 95 | 9846 / 8 | 98, 3 | 394 / 450 |

Operations per call:
- dof: new 2970, update 22501, stop 11672, shutdown 45; every operation also calls `result`, `status`, `is_clean`, `is_suppressed`.
- vignette: new 3731, update 13465, stop 5435, shutdown 55, peek 568.
- aim_markers: new 41, update 4452, stop 928, shutdown 28, setting 575, set_setting 484, target 581, toggle 1031, current 2542, probe 1226, same 315, show 1002, visible 715.
- first_person_movement: new 18, update 10331, update_fird 2114, stop 4083, shutdown 28.
- camera_compat: new 103, update 2601, prepare 1362, stop 1539, shutdown 37, foreign 1067, result 1259, status 547, is_clean 496, shim 1332.
- native_ads_option: new 15, update 3398, stop 1170, shutdown 19, probe 659, read 657, write 688.
- diagnostics: new 22, record 7754, event 1424, snapshot 565, clear 81.

All seven compile clean with `gcc -std=c11 -Wall -Wextra -Werror` and with `x86_64-w64-mingw32-gcc`.

## Method (all seven)

- `tools/retail_ref/run_<m>.lua` loads the retail file's bytes unmodified under `/usr/bin/luajit`, under the chunk name the mod requires it by (`src/first_person/<m>.lua`). `compat51.lua` is answered with what it is under LuaJIT (`kind` = `type`, `number` = `tonumber`). Everything else the file reaches is a scripted fake.
- `tools/retail_ref/effects_common.lua` and `tests/effects_fx.h` are the same small machine in Lua and in C. A world of named slots; every fake answers from it. An operation is one public call plus its world changes, each applied before the call or just before host call number N (a change in the middle of the call). A slot `x_<function>` makes that host function raise a text; `x_any` raises at the next host function that can raise. The seeded walks place such raises and changes at random call positions; that is what reaches the error paths.
- `tools/retail_ref/gen_<m>.py` (through `effects_gen.py`) refuses any retail commit but 1d0d453, runs the runner and writes `tests/vectors/<m>_vectors.h` with the commit and the SHA-256 of the retail file.
- The comparison is one text line per operation, built the same way on both sides: the call's results, the module's state (upvalues are read with `debug.getupvalue`, never written), and the ordered trace of every host call with its arguments; log lines sit in the trace at their place. Numbers print as integers when they are small integers and as the 16 hex digits of the double otherwise, so floating-point results (the times in dof and vignette, the copied numbers in diagnostics) are equal only when bit-identical. The run scripts compile with `-ffp-contract=off`, `-Werror`, ASan and UBSan.
- `tests/run-vectors-<m>.sh` is picked up by `tests/run-native.sh`. Each test was first run against a do-nothing stub of the C API and seen to fail.
- Coverage: the retail file is split to one statement per line in a scratch copy and the runner is run under a line hook. The hook never fires for the header line of a multi-line `local function`; those lines are counted as "not reached" by the tool although every body is reached.
- Mutation: single changes to a scratch copy of the C file (comparison and logic operators, constants, offsets, flipped flags, dropped assignments and calls, swapped returns); each must make the run script fail. 150 per seed, three to seven seeds per module; the last seed of each ran on the final sources. The run scripts make every ASan and UBSan finding fatal (`-fno-sanitize-recover=all`).

## Carried over from Lua (all seven)

| Lua | C |
| --- | --- |
| `nil` against `false`, and 0 / "" being true | A `has_*` flag, a small enum, or a NULL text. An empty text stays a text. |
| `pcall(function() … end)` | An inner function that returns false with the error text; each host call that can raise returns false with `const char **error`. |
| `assert(cond,'text')` | LuaJIT raises `src/first_person/<m>.lua:<line>: text`. C builds the same text from `TFP_<M>_LUA_CHUNK` and the retail line. An `assert(v, why)` whose `why` is nil raises the value nil, and `tostring` makes it `"nil"`. Texts raised by the host pass through unchanged. |
| A function that both "returns false, why" and can raise, where retail treats them differently | An enum (`DONE / REFUSED / RAISED`), for example `memory_copy`, `application_set_render_setting`, `save_global`. |
| Tables and functions compared by identity (`a.get==b.get`, `current==r.shim`, `context.world~=world`) | Opaque tokens the host reports; the trace prints them. |
| Addresses as Lua numbers | `uint64_t`. Identical below 2^53. |
| Fields of `backend` and `sr` read at call time (`backend.pin_first_person`, `backend.foreign_camera`, `sr.Keyboard`) | Fields of the host struct, read at each call. A NULL pointer is "not a function". |
| Texts of any length | Capped at 255 bytes plus NUL; identities at 512. |

## Host functions per module

"Raises" = has an error channel in C. Names are the pointers of `Tfp<Name>Host` in `src/<m>.h`.

### dof (`TfpDofHost`)

Engine functions:

| Pointer | Retail | Arguments | Result | Raises |
| --- | --- | --- | --- | --- |
| `application_binding` | `sr.Application`, `.render_setting`, `.set_render_setting` (field reads) | – | available + tokens `{app, get, set}` | no |
| `application_render_setting` | `Application.render_setting(key)` (the retained getter) | binding, key text (`"dof_enabled"`, `"vignette_enabled"`) | other / false / true | yes |
| `application_set_render_setting` | `pcall(Application.set_render_setting, key, 'true'\|'false')` | binding, key text, bool | done / refused / raised | yes |

Mod functions:

| Pointer | Retail | Arguments | Result | Raises |
| --- | --- | --- | --- | --- |
| `clock` | `deps.clock()` | – | double | yes |
| `log` | `log('FPV DoF: …')` / `'FPV vignette: …'` | text | – | no |
| `backend_renderer_dof_value` (NULL: none) | `pcall(backend.renderer_dof_value, backend, get, set, key)` | binding, key | other / false / true | folded into "other" |
| `backend_foreign_camera` (NULL: none) | `pcall(backend.foreign_camera)` | – | `{is_false, value text, why}` | yes |
| `TfpDofCompat.foreign` (argument of update) | `pcall(compat.foreign, compat)` | compat's user | same | yes |
| `backend_profile_pose` (field) | `backend.profile and backend.profile.pose` | – | bool | – |
| `backend_alignment_executable` | `backend:_alignment_executable(profile)` | – | `uint64_t` | yes |
| `backend_read` | `backend:_read(exe+0x3d43d0, 8)`, `(exe+0x3d3ff0, 8)` | address, size | bytes | yes |
| `verify` (NULL: not passed, the shipped case) | `deps.verify(app)==true` | app token | bool | yes |
| `active_view` (NULL: none) | `deps.active_view(controller, camera)==true` | – | bool | yes |
| `fallback_ready` (NULL: none) | `deps.fallback_ready()==true` (the branch without `active_view`) | – | bool | yes |
| `controller_is_enabled` (NULL: no controller), `controller_result` | `controller:is_enabled()`, `controller:result()` (same branch) | – | bool / `{pose_offset, running}` | no |
| `vignette_update` / `_stop` / `_shutdown` | `vignette:update(controller,camera,compat)` etc. | compat / – | nil / false / true / raised + text | yes |
| `vignette_is_clean`, `vignette_result`, `vignette_status` | the same object | – | bool / three tri-states / text | no |

### vignette (`TfpVignetteHost`)

Engine functions. `binding` is the token of the Lua function retail would call at that point (retail keeps the seven functions it first saw and asserts they are still the same):

| Pointer | Retail | Arguments | Result | Raises |
| --- | --- | --- | --- | --- |
| `stingray_api` | field reads of `sr.Application.main_world`, `sr.World.entities`, `sr.EntityManager.render_data_component`, `.alive`, `sr.RenderDataComponent.instances`, `.get_property`, `.set_property` | – | available + 7 × `{kind, token}` | no |
| `application_main_world` | `Application.main_world()` | binding | world handle `uint64_t` (0: nil/false) | yes |
| `world_entities` | `World.entities(world)` | binding, world, buffer of 32768 handles | kind (table / nil / not a table), count | yes |
| `entity_manager_render_data_component` | `EntityManager.render_data_component(world)` | binding, world | manager handle | yes |
| `entity_manager_alive` | `EntityManager.alive(entity)` | binding, entity | bool | yes |
| `render_data_component_instances` | `RenderDataComponent.instances(manager, entity)` | binding, manager, entity, buffer of 64 doubles | count (may exceed 64), ids as doubles | yes |
| `render_data_component_get_property` | `RenderDataComponent.get_property(manager, entity, id, {name})` | binding, manager, entity, `double id`, `"shading_environment_mapping"` or `"vignette_enabled"` | `{kind, boolean, text, length}` | yes |
| `render_data_component_set_property` | `RenderDataComponent.set_property(manager, entity, id, {'vignette_enabled'}, value)` | binding, manager, entity, id, name, bool | refused (retail's `false`) | yes |

Mod functions: `clock` (double, raises), `log`, `compat_foreign` and `backend_foreign_camera` (bool; a raise counts as true, as retail's `not ok or v~=false`), `active_view` (bool; a raise counts as false), and for the branch without `active_view`: `backend_player_snapshot` (`{present, safe, camera_kind}`, raises), `controller_result` (`{pose_offset}`), `controller_is_enabled`.

### aim_markers (`TfpAimMarkersHost`)

| Pointer | Retail | Arguments | Result | Raises |
| --- | --- | --- | --- | --- |
| `backend_module_identity` | `backend:_module('identity')` | – | game base | yes |
| `backend_read` / `_ptr` / `_u32` / `_u8` | `backend:_read(address,size)`, `:_ptr`, `:_u32`, `:_u8` | address (and size) | bytes / `uint64_t` / `uint32_t` / `uint8_t` | yes |
| `backend_windows` | `backend:_windows()` (its `.wine`) | – | bool wine | yes |
| `windows_virtual_query` | `VirtualQuery(address, region, 48)` (retail's `NativeADS_Query`) | address, `TfpAimMarkersRegion *`, size | bytes filled | no |
| `game_set_widget_shown` | the game's own function at `game+0x12e4e20`, `void(uintptr_t widget, uint8_t shown)` | function address, widget, 0/1 | – | channel provided |
| `setter` (NULL on the shipped path) | `Adapter.new(backend, setter)` | widget, bool | – | yes |
| `backend_snapshot`, `backend_pin_player_snapshot` (NULL: none), `backend_pin_first_person` (field) | `backend:snapshot()`, `:pin_player_snapshot()` | – | snapshot or none | no |
| `backend_pose_context` | `backend:_pose_context(s).weapon_object` | snapshot | `uint64_t` | yes |
| `foreign`, `fallback_ready`, `pinned_ready` (NULL: none) | the `options` callbacks of `M.source` | – / snapshot | bool | no |
| `controller_is_enabled`, `controller_pose_offset` | `controller:is_enabled()`, `controller:result().mode=='pose_offset'` | – | bool | no |
| `settings` | `options.settings()` (`mod.config.aim_markers`) | – | not a table, or the keys whose value is true (at most 64) | no |
| `save` | `options.save(values)` (`mod.set('aim_markers', …)`) | key array, count | bool, why | no |
| `global_setting` (NULL: none) | `options.global_setting()` (`mod.config.show_aim_markers`) | – | unset / show / hide | no |
| `save_global` (NULL: none) | `pcall(options.save_global, value)` | bool | done / refused + why / raised + why | yes |
| `log` (NULL: none) | `log('Aiming markers: …')` | text | – | no |

No engine script function is called by this module.

### first_person_movement (`TfpMovementHost`)

Engine functions:

| Pointer | Retail | Arguments | Result | Raises |
| --- | --- | --- | --- | --- |
| `engine_keyboard` (field) | identity of `sr.Keyboard` | – | token, 0: not a table | – |
| `keyboard_button_index` (NULL: none) | `Keyboard.button_index('s')` | name text | nil or a number | yes |
| `keyboard_button_id` (NULL: none) | `Keyboard.button_id('s')`, only without `button_index` | name text | nil or a number | yes |
| `keyboard_button` (NULL: none) | `Keyboard.button(key)` | `double key` | nil or a number (must be 0 or 1) | yes |
| `window_mouse_focus` (NULL: none) | `Window.mouse_focus()` | – | bool (`==true`) | yes |
| `window_show_cursor` (NULL: none) | `Window.show_cursor()` | – | bool (false only for retail's `false`) | yes |

Mod and Windows functions:

| Pointer | Retail | Arguments | Result | Raises |
| --- | --- | --- | --- | --- |
| `backend_player_snapshot`, `backend_pin_player_snapshot` (NULL: none), `backend_pin_first_person` (field) | `backend:player_snapshot()`, `:pin_player_snapshot()` | – | done + snapshot / refused + why / raised | yes |
| `backend_read` / `_ptr` / `_u32` | `backend:_read`, `:_ptr`, `:_u32` | address (and size) | bytes / `uint64_t` / `uint32_t` | yes |
| `backend_action_state` | `backend:action_state(s)` | snapshot | opaque action | yes |
| `backend_on_foot` | `backend:on_foot(s, action)` | snapshot, action | bool (`==true`) | yes |
| `backend_foreign_camera` (NULL: none) | `backend.foreign_camera()` | – | false / true / other | yes |
| `windows_virtual_query` | `VirtualQuery(address, region, 48)` | address, `TfpMovementRegion *`, size | byte count | no |
| `windows_write_process_memory` | `WriteProcessMemory(process, address, byte, 1, &count)` | address, bytes, size, `size_t *written` | BOOL | no |
| `controller_is_enabled`, `controller_result_pose_offset` | `controller:is_enabled()`, `controller:result().mode=='pose_offset'` | – | bool | no |
| `controller_focus_owned` (NULL: none) | `controller:focus_owned(s)` (Fird only) | snapshot | bool | yes |
| `deps_fallback_ready` (NULL: none) | `deps.fallback_ready(s)` | snapshot | bool | no |
| `deps_writable`, `deps_write` (NULL on the shipped path) | `deps.writable(address)`, `deps.write(address, value)` | address (, byte) | bool | yes |
| `log` (NULL: none) | `log('First-person movement: …')` | text | – | no |

### camera_compat (`TfpCameraCompatHost`)

Every look at another mod's Lua state is one host call, because retail re-reads the same fields after other calls:

| Pointer | Retail | Arguments | Result | Raises |
| --- | --- | --- | --- | --- |
| `photo_camera_ownership` | `rawget(G,'DBF_PHOTO_CAMERA_OWNERSHIP')` | – | value `{kind, id}` | no |
| `photo_actioncam_compat` | `rawget(G,'DBF_PHOTO_ACTIONCAM_COMPAT')` | – | value | no |
| `actioncam_loaded` | `package.loaded['mods/actioncam/actioncam']` is truthy | – | bool | no |
| `table_field` | `t.held`, `t.available`, `t.disabled`, `t.set_input_blocked` | table value, field enum | value | no |
| `debug_getupvalue` (NULL: no `debug.getupvalue`) | `pcall(debug.getupvalue, setter, index)` | function value, index 1..32 | raised / end / other name / `input_reader` + value | yes |
| `set_input_blocked` | `pcall(setter, shim)` and `pcall(setter, previous)` | setter value, reader value (our shim's serial, nil, or the older function) | called (+ "first result is false", second result text) / raised + text | yes |
| `previous_reader_call` | `pcall(previous)` | reader value | raised / false / not false | yes |
| `controller_is_enabled` | `pcall(controller.is_enabled, controller)` | – | false / true / not Boolean / raised | yes |
| `log` (NULL: none) | `log('FPV camera sharing: …')` | text | – | no |

`tfp_camera_compat_shim(c, serial)` is the body of the closure retail hands to the other mod; the wiring exposes it as the Lua callback and must report that same function back from `debug_getupvalue`. No engine script function is called.

### native_ads_option (`TfpNativeAimHost`)

| Pointer | Retail | Arguments | Result | Raises |
| --- | --- | --- | --- | --- |
| `backend_snapshot` | `backend:snapshot()` | – | snapshot (base, avatar_owner, player_identity, identity, weapon_descriptor, weapon, safe, camera_kind) or nil + why | no |
| `backend_player_snapshot`, `backend_pin_player_snapshot` (NULL: none), `backend_pin_first_person` (field) | `backend:player_snapshot()`, `:pin_player_snapshot()` | – | snapshot (base, avatar_owner, player_identity) or nil + why | no |
| `backend_read` / `_ptr` / `_u32` / `_u8` | `backend:_read`, `:_ptr`, `:_u32`, `:_u8` | address (and size) | bytes / `uint64_t` / `uint32_t` / `uint8_t` | yes |
| `backend_map` | `backend:_map(address, id[, limit])` | address, `uint32_t id`, optional limit (8 for the input map) | index | yes |
| `backend_foreign_camera` (NULL: none) | `backend.foreign_camera()==false` | – | bool (true unless retail's is false) | no |
| `memory_copy` | `writer:copy(address, string.char(value))` of `src/memory.lua` | address, byte | done / refused + why / raised + text | yes |
| `enabled` | the menu switch `true_native_ads` (second argument of `M.new`) | – | bool | no |
| `foreign` (NULL: none) | third argument of `M.new` | – | bool (true unless retail's is false) | no |
| `controller_is_enabled`, `controller_result` | `controller:is_enabled()`, `controller:result().mode` | – | bool / `{pose_offset}` | no |

No engine script function is called.

### diagnostics

No host functions. `tfp_diagnostics_init / record / event / snapshot accessors / clear / free`.

## Module notes

### dof

Two layers: `TfpDofSetting` (retail's `Setting`, one renderer Boolean; the key is free text and only `dof_enabled` and `vignette_enabled` are re-asserted) and `TfpDof` (the wrapper, with the vignette as nothing, as the vignette module through the `vignette_*` pointers, or as a second Setting). All three constructions, both keys and both branches of `actual_view` are translated and in the vectors.

Dropped or narrowed: `deps.clock or os.clock` (the clock is required; runtime.lua:707 passes it); a clock that returns a non-number (reported as NaN, same assertion); the factory call itself and a factory that returns nil; the `controller` and `camera` arguments (`active_view` as shipped ignores them); controller getters and the vignette getters raising; `renderer_dof_value` raising against returning a non-Boolean (retail cannot tell them apart); error values that are neither text nor nil; counters as `uint32_t`.

Retail behaviour found and reproduced: a record can exist without a stage. If the binding or the camera owner changes between the acquisition read and the first command, `command()` raises before it sets `stage` and `next_poll`; every later update and stop then raises `src/first_person/dof.lua:105: attempt to compare number with nil` and the lease stays `cleanup_pending` until the clock steps backwards. Nothing in a single-threaded frame changes those between the two statements, so this is a latent fault, not a live one.

Coverage: the 12 lines are ten function header lines (tool artefact), `return true` in `advance` without a record (both callers test `record` first) and one `end` after a `return`.

Mutation: real gaps closed: a hand-written session that locked itself out early (so the two thunk comparisons of `check()` could be dropped), a scripted byte equal to the byte already there, the second read of a preserved setting failing, `time-sent>2` at exactly 2.0, a changed binding resetting `session / preserved / yielded` in three situations, and a log text near the cap (the log-line buffer could shrink). Survivors are without effect: initialisers the host overwrites, `memset`s of structs that are fully assigned, a buffer size, `phase = IDLE` after a zeroing `memset`, the unreachable `advance` line, and one redundant condition of `is_suppressed` (retail's own conditions imply each other).

### vignette

The entity table: retail keeps the table `World.entities` returned and walks 96 entries per update. C copies up to 32768 handles into the state when the inventory begins (256 KB inside `TfpVignette`) and never touches the engine's table again. Instance ids stay doubles; `id%4294967296` is computed as Lua does, so a negative id that is `CLASS - 2^32` matches as in retail.

Dropped or narrowed: `deps.clock or os.clock`, a nil `log`, a nil controller; non-callable `compat.foreign` / `foreign_camera` / `active_view` (same result under `pcall`); a non-number clock or id (reported as NaN); nil holes in the instance and entity lists; `row.before` (retail only stores true).

Coverage: the 10 lines are the default logger body and nine function header lines.

Mutation: one real gap closed (no log text over 225 bytes, so the log-line buffer could shrink). Survivors are without effect: initialisers the host overwrites, `row_count` in the world-replaced branch of stop (cleared again right after, as retail's `rows={}` is redundant there), `phase = IDLE` after `memset`, `4294967296.0 → 4294967296.1` (no representable id changes its answer), a buffer size.

### aim_markers

Three layers, each tested directly and through the one above: the adapter (`tfp_aim_markers_probe / same / visible / show`), the source (`tfp_aim_markers_source_current`, with its one-entry key cache) and the lease. `pinned_view` is translated although `backend.pin_first_person` is false on the shipped path; the test `setter` is kept.

Dropped or narrowed: the Fird branches (`options.fird()`, `fird.weapon_snapshot`, `fird.weapon_key`: Fird Person is parked and `fird` stays nil); `global_setting`, `settings` and `save` raising (plain config access); `set_setting` with a non-Boolean (the argument is a `bool`); the saved table is a list of at most 64 keys whose value is true; a caller-supplied adapter table.

The keys handed to `save` come out of `pairs()` in LuaJIT's order; both fakes sort them before printing. No host-call order depends on `pairs()`.

Coverage: the 14 lines are nine definition lines and one `end` (tool artefacts), the two Fird lines, `'Aim-marker setting must be on or off'` (a `bool` argument) and `return false,tostring(global)` in `toggle` (only when `global_setting` raises).

Mutation: real gaps closed: the read-back after the native call raising, an error value of nil, almost-valid keys, a view-only target whose global choice disappears before `update` reads it, a repeated key in the host list, the cap boundary of the saved table, a 243-byte host text. Survivors are without effect: initialisers the host overwrites, buffer sizes, guards that cannot be false (a lease never has a third row, the host count never exceeds the cap).

### first_person_movement

`update_fird` and every `focused` branch are translated (parked in retail, but retail's own tests exercise them), and so are the `deps.writable` / `deps.write` hooks. On the shipped path the page test and the write go through `windows_virtual_query` and `windows_write_process_memory`; the runner gives the retail file LuaJIT's real `ffi` with Lua functions in place of the two kernel calls.

Dropped or narrowed: `backend:_windows()` as a call that can raise (kernel32 is simply there in C); `'Incomplete locomotion integer'` (`backend_read` returns the size asked for or raises); snapshot fields of other types (`avatar` is `uint32_t`, the descriptor 20 bytes, `safe` / `seated` / `controller_active` are `bool`); controller getters and `deps.fallback_ready` raising; a key lookup that returns a value that is neither nil nor a number.

Coverage: the 15 lines are fourteen function header lines and one `end` after a `return`.

Mutation: real gaps closed: a fingerprint mismatch was never asserted (the first vectors moved the base to unmapped memory instead), `same()` never differed in `avatar` or the descriptor bytes alone, line 210 (`assert(clean,problem)`) was never reached, and the fake always set the WriteProcessMemory count. Survivors are without effect: initialisers the host overwrites, buffer sizes, `address` in `same()` (it is `rows + index*0xA0 + 0x94`, both compared before it), `cap < 128 ? cap : 128` at exactly 128.

### camera_compat

Values of the other mod (`TfpCameraCompatValue {kind, id, shim}`) are compared by id; the host must keep an object's id stable while a record holds it. Records are kept until `tfp_camera_compat_free`, because a shim the other mod still holds can be called at any time; an old shim then honours only its own previous reader.

Dropped or narrowed: a field read that raises (only a metatable could do that; retail does not protect it either); a non-string second result or error value of the setter (the host passes text); numbers, strings and userdata as `set_input_blocked` (never equal, not callable).

Retail behaviour found and reproduced: if `set_input_blocked` stops being a function between the provider check and the installation, the record keeps that value; `prepare` rejects with `attempt to call a nil value` and `stop` answers "Photo camera reader cannot be inspected" from then on.

Coverage: the 7 lines are function header lines.

Mutation: real gaps closed: the setter turning into something else exactly at `verify()`'s comparison, and the `reading` flag of a new record (now part of one compound literal). Survivors are without effect: buffer sizes and the growth factor, initialisers the host overwrites, `free` bookkeeping, the guard for a shim serial that was never handed out (no retail counterpart), equality branches for kinds that never meet.

### native_ads_option

Two layers: the source (`tfp_native_aim_source_probe / read / write`) and the lease. `read` has three answers (a value, `nil,'retired'`, a raised error).

The three code witnesses of `probe()` are read in the order LuaJIT's `pairs()` visits retail's table (0xa5cff5, 0xa5cf03, 0xa5d0b3). All three raise the same text, so the order shows only in which read comes first.

Dropped or narrowed: `memory and memory() or mod.require('src/memory.lua').new()` (the writer is the host pointer `memory_copy`; runtime.lua:721 passes no factory); snapshot calls and controller getters raising (the backend snapshots are `pcall`ed inside `backend.lua`); `'Native aim preference unavailable'` (line 67; `read` returns nil only with `'retired'`); identities longer than 512 bytes (the host refuses such a snapshot).

Coverage: one line, the header of `context` (tool artefact).

Mutation, four seeds: 554 of 600 killed. Two real gaps closed on the way: the record comparison could ignore the descriptor or the key (no vector had the same byte under another player, or the same key under another descriptor), and the raises were too thin to reach every memory read (the walks now raise at a random host call in 15% of operations). The 46 survivors (18 distinct) are without effect: initialisers the host overwrites, dummy arguments under a false flag, buffer sizes, `memset` of a record whose compared fields are all assigned, `phase = IDLE` after `memset`, the cap comparison at exactly the cap.

### diagnostics

A value is `TfpDiagnosticsValue`: nil, Boolean, number, text (bytes with a length, so a NUL survives) or "other" (a table, a function: never copied). A sample is one value per field, in the order of retail's `fields` table as written (73 fields; the runner checks its list against retail's table). The ring is allocated at init (`calloc`), each row's texts in one block that grows on demand; `tfp_diagnostics_free` releases them. This is the only module of the seven that allocates besides camera_compat's record list.

`record` has three answers. Retail compares `(previous.transition_progress or 1)<1` and `row.look_samples>previous.look_samples` without checking the kinds, so a Boolean or a text against a number raises (`src/first_person/diagnostics.lua:52: attempt to compare boolean with number`, `…:54: attempt to compare string with number`, `…two boolean values`). At that point the frame is already counted and `last_frame` is set, the row is not kept and `previous` stays. With a Boolean `transition_progress` in `previous`, every later record with another epoch raises again until `clear`. The C returns `TFP_DIAGNOSTICS_RAISED` with LuaJIT's text and leaves the same state. The mod itself only stores numbers in these fields (runtime.lua:513-531), so this is a latent path. Two texts compare as LuaJIT compares strings (bytes, then length).

Kept: every refusal (`frame` not a whole number from 1, a sample that is not a table, a name that is not a text, duplicate and out-of-order frames), the cuts to 200 and 64 bytes, NaN and infinities dropped by `scalar`, -0 kept, capacity nil / false (360) and the capacity assertion (`…:24: assertion failed!`).

Dropped or narrowed: counters are `uint64_t`; `snapshot()` is read in place through accessors instead of returning copies; an allocation that fails answers "not enough memory" (Lua's own text), which no vector can produce.

Coverage: the 3 lines are the headers of `scalar`, `insert` and `row_of` (tool artefact).

Mutation, three seeds: 394 of 450 killed. Two real gaps closed: init relied on what the struct held before (the test now fills it with 0xA5 first), and an out-of-bounds read flagged by UBSan did not fail the run (all seven run scripts now use `-fno-sanitize-recover=all`). The 56 survivors (20 distinct) are without effect: the allocation-failure paths, values under an unused field (`one.text`, the view's NULL), `equal()` for two nils (never asked), boundaries where both sides give the same result (`length < limit` at the limit, `need > text_size` at equality, `order < 0` for a non-zero order), the redundant kind test before the capacity range test (a non-number carries 0, which fails the range), array and buffer sizes, the `memset` in `free`.

## Differences found while matching

None in behaviour in any of the seven: each first C draft matched its vectors, except one text in native_ads_option (a `memory_copy` that raises passes its text through, a refusal gets the assertion prefix of line 54; the host answer became a three-valued enum). All other faults the checks found were in the vectors and are listed under "real gaps" above.

## Open doubts, least sure first

1. **Offsets and fingerprints are the retail build's.** dof's two thunks (`exe+0x3d43d0`, `exe+0x3d3ff0`), the aim-marker setter `game+0x12e4e20` with its four fingerprints, the six locomotion anchors and the pool at `+0x2ce1eb8`, the native-aim offsets and three witnesses are copied from retail. HellForge build 25480438 needs its own values; the translations keep them as named constants.
2. **Assertion prefix.** The texts follow LuaJIT (`<chunk>:<line>: text`). On PUC Lua 5.1 `assert` adds no position, and the chunk name is the host loader's choice. These texts reach the logs, `status` and second results.
3. **Identity tokens in the wiring.** dof (three), vignette (seven) and camera_compat (every table and function) rely on the host handing out stable tokens for Lua objects and calling exactly the retained function. camera_compat also needs the other mod's setter to keep an upvalue named `input_reader`.
4. **Glue between dof and vignette.** dof reaches the vignette through `vignette_*` pointers with a tri-state answer plus "raised"; `tfp_vignette_*` returns `bool`. The adapter between the two is wiring and is not tested here. vignette.lua's `result()` has no `command_pending`, so dof's `result.command_pending` can be nil, as in retail.
5. **Getters without an error channel.** Controller, menu and config getters cannot raise in C. If a HellForge-side getter can fail, the wiring has to decide the folded answer (the tables above say which value retail's `pcall` would yield).
6. **Text caps** (255 bytes, identities 512, 64 saved aim-marker keys). Lua has none.
7. **Engine handle types.** vignette assumes worlds, managers and entities fit a `uint64_t` that is never 0 and that instance ids are Lua numbers.
8. **String order in diagnostics.** `look_samples` compared as two strings follows LuaJIT (bytes, then length); PUC Lua uses `strcoll`. The mod only ever stores numbers there.
