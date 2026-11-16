# Editor/pose helper: the C translation

`src/pose_helper.c`, `src/pose_helper.h` and `src/pose_math.h` are `native/first_person/native_ads_editor.cpp` and `native_ads_editor.h` of the retail kit at 0.5.41-smooth.66, with `presentation_phase.h`, `zoom_gain_math.h` and the helpers of `zoom_input_math.h`, in C11, for build 25480438. "cpp:N" is a line of the retail file, "c:N" a line of `src/pose_helper.c`, "m:N" a line of `src/pose_math.h`. `src/camera_math.h` is not used: the editor needs none of its functions.

Both compile checks pass with no warnings: `cc -std=c11 -Wall -Wextra -Werror` and `zig cc -std=c11 -target x86_64-windows-gnu -Wall -Wextra -Werror`; `src/pose_math.h` also compiles as C++17. **Nothing has been run.** No test was written: the translation has not been compared with the original.

All 1,707 source lines are translated. The look-gain normalisation is translated in full but refuses on this build (section 6, Open 1).

## 1. The host interface (`TfpPoseHost`, set once with `tfp_pose_set_host`)

| member | replaces in the retail file |
|---|---|
| `hooks[6]` (values) | the addresses the helper puts in a dispatch slot and recognises there (`callback()`, cpp:1345-1348): `position_hook`, `rotation_hook`, `branch_rotation_hook`, `position_getter_hook`, and the assembly entries `NADSE_Input00Thunk` (slot 3 in input mode) and `NADSE_Input90Thunk` (UnitAPI+0x90). In the game each is a wrapper that calls the matching `tfp_pose_*_from` |
| `read(at, out, length)` | `ReadProcessMemory` in `read` (cpp:156). The bounds stay in C (c:341) |
| `tick()` | `tick()` = `GetTickCount64` (cpp:265) |
| `thread()` | `GetCurrentThreadId` (cpp:496, 795, 823, 863, 900, 970) |
| `writable_slot(at, exe)` | the `VirtualQuery` part of `writable_slot` (cpp:273-277, non-fixture path). The 8-byte alignment test stays in C |
| `executable(at)` | `executable` (cpp:178-182, with `native_memory.h`'s `pe_section_mapping`) |
| `cas(slot, expected, desired, &observed)` | `InterlockedCompareExchangePointer` inside `__try` (cpp:301-305); false = the operation faulted |
| `write(out, data, length)` | `WriteProcessMemory`: the generation (cpp:1389) and the five result structures (cpp:1646, 1658, 1672, 1684, 1696) |
| `pin(address)` | `GetModuleHandleExW(FROM_ADDRESS, PIN)` (cpp:1341-1344); called with the address of `tfp_pose_begin`, then the executable base, then (input mode only) the game base, in retail's order |
| `lock_exclusive`, `unlock_exclusive`, `lock_shared`, `unlock_shared` (lock number) | `AcquireSRWLock*`/`ReleaseSRWLock*` on `control_lock` (0) and `policy_lock` (1) |
| `writable32(at)` | the `VirtualQuery` test in `input_cas32` (cpp:752-754). Alignment and range tests stay in C |
| `cas32(at, expected, desired, &observed)` | `NADSE_InputCompareExchange` of `native_ads_input.S` with its exception handler `NADSE_InputCASException` (cpp:31-44): `lock cmpxchg` on 32 bits, false when the write faulted |
| `region(at, &base, &size, &free)` | `VirtualQuery` in `input_clone` (cpp:726-730) |
| `allocate(at, size)` | `VirtualAlloc(at, 0x10000, MEM_RESERVE\|MEM_COMMIT, PAGE_READWRITE)` (cpp:733). The private record is then written directly, as in retail (cpp:741, 854) |
| `native_exp(address, x)`, optional | nothing in retail: retail calls the game's own `exp` at `game + 0x1f8ff70` directly (cpp:842). NULL = direct call. See section 4 |
| `forward_setter`, `forward_getter`, optional | nothing in retail. NULL: the original setter/getter is called directly, as retail does |

The five hook entries take what the retail hook captured, as parameters:

| C entry | retail | captured |
|---|---|---|
| `tfp_pose_position_from(return_address, unit, node, pose, native_stack)` | `position_hook` (cpp:1137-1141) | `_ReturnAddress()`, `_AddressOfReturnAddress()+8` |
| `tfp_pose_rotation_from(return_address, unit, node, pose)` | `rotation_hook` (cpp:1234-1247) | `_ReturnAddress()` |
| `tfp_pose_branch_rotation_from(return_address, unit, node, pose)` | `branch_rotation_hook` (cpp:1337-1340) | `_ReturnAddress()` |
| `tfp_pose_position_getter_from(return_address, unit, node, native_stack, native_request)` | `position_getter_hook` (cpp:907-913, `native_request` 0) and `NADSE_Input00Call` (cpp:914-916, from the thunk) | return address, caller stack, caller R15 |
| `tfp_pose_input90_from(return_address, unit, node, request, native_stack)` | `NADSE_Input90Call` (cpp:809-873, from the thunk) | return address, caller R15, caller stack |

The fixture seams (`fixture_*`, cpp:57-72, 261-263, 577-581 and their uses) are not translated. Where retail has a fixture path and a game path, the game path is translated: `graph` asks `executable(getter)` (cpp:237-239), `contract` requires each slot to hold `exe + wrapper` and checks the bytes at the slot's value (cpp:323-324), `input_sdk` requires `exe + world matrix` (cpp:634), `begin_pose` pins the executable and the game (cpp:1433-1436), `writable_slot` asks about the executable image, the stack values are the hook's parameters instead of `fixture_depth_frame` (cpp:482, 910, 959), `exp` is the game's (cpp:842), and the start-up values are the non-fixture ones: draw animation 220 ms (cpp:1018), entry position on (cpp:1180).

## 2. Constants

All are in one block at c:12-213. 127 `B_` names: 99 established, the flag `B_INPUT_ESTABLISHED`, and 27 open names that stand for 20 unknown things (a pin without a counterpart has an address name and a hash name). `B_` = build data, `L_` = structure layout with the same number in both builds. Evidence: **map** = `tools/build_map.py --to-here`; **image** = bytes read from `game-pre1020.dll` / `r6_reload_exe_20261017.bin` on 2026-11-16; **input-json** = `docs/research-0.5.34/native-input-evidence.json`; a file name = that file of this repository reads the same field on this build.

### Established (99 `B_` names)

| name | retail | here | evidence |
|---|---|---|---|
| `B_GAME_STAMP`, `B_GAME_SIZE` | `0x6af322c1`, `0x3382000` | `0x6ab3b43f`, `0x4744000` | map; `profile.h` |
| `B_EXE_STAMP`, `B_EXE_SIZE` | `0x6af2d5e6`, `0x29fe000` | `0x6ab382e4`, `0x39e8000` | map; `profile.h` |
| `B_EXE_POSITION` | `0x1f25e0` | `0x1fc720` | map; `native_contract.h` unit_position |
| `B_EXE_ROTATION` | `0x1f2740` | `0x1fc880` | map; image |
| `B_EXE_BRANCH_ROTATION` | `0x1f2660` | `0x1fc7a0` | map; `native_contract.h` unit_branch_rotation |
| `B_EXE_LOCAL_POSITION` | `0x1f2310` | `0x1fc450` | map; `native_contract.h` unit_local_position |
| `B_HEX_POSITION` (48 bytes) | `…e8d5e0eaff…` | `…e88511eaff…` | image; only the call displacement differs |
| `B_HEX_ROTATION` (48) | `…e875dfeaff…` | `…e82510eaff…` | image; only the call displacement differs |
| `B_HEX_BRANCH_ROTATION` (48) | `…e84ae0eaff…` | `…e8fa10eaff…` | image; only the call displacement differs |
| `B_HEX_LOCAL_POSITION` (51) | `…e8b2e3eaff…` | `…e86214eaff…` | image; only the call displacement differs |
| `B_EXE_GRAPH_GETTER`, `B_HEX_GRAPH_GETTER` | `0x28d3c0`, `488d4160c3` | `0x2bd870`, the same | map; image |
| `B_GAME_CARRIER_CALL`, `B_GAME_CARRIER_PAIR` | `0x111a809`, `0x111a829` | `0x12887c9`, `0x12887e9` | map; image `41ff5138`, `41ff5148` |
| `B_GAME_RIG_CALL` | `0x111a460` | `0x1288420` | map; image `41ff5138` |
| `B_GAME_ROTATION_CALL` | `0x111b11b` | `0x12890db` | map; image `41ff5140` |
| `B_GAME_OBSERVE_CALL` | `0x1119a81` | `0x1287a41` | map; image `41ffd0` |
| the five `B_HEX_*_CALL` | | the same | image |
| `B_GAME_CONTEXT`, `B_GAME_AVATAR_OWNER`, `B_GAME_CONTROLLER`, `B_GAME_API_ROOT` | `0x2e287d0`, `0x2ce1ee0`, `0x2e381a8`, `0x2ce14c8` | `0x346d560`, `0x3326d20`, `0x347ce70`, `0x3326308` | map; `profile.h`; the API root also image (`mov rcx,[rip]` at game+0x1288407) |
| `B_EXE_REGISTRY` | `0x1ab76f8` | `0x1a100f0` | map; `profile.h` |
| `B_GAME_WEAPON_POOL`, `B_GAME_WORLD`, `B_GAME_SETTINGS` | `0x2ce1ea0`, `0x2ce41b8`, `0x2ce1508` | `0x3326ce0`, `0x346bf98`, `0x3326340` | map; `profile.h` |
| `B_SETTINGS_DEGREES` | `0xac488` | `0xac408` | map; input-json; `depth.c` |
| `B_AVATAR_FLAGS`, `B_AVATAR_RIG`, `B_AVATAR_STRIDE` | `0x53e888`, `0x53e9c0`, `0x1240` | `0x53e880`, `0x53e9b8`, `0x1238` | map; `profile.h`; the rig also image (game+0x1288418) |
| `B_WORLD_RESOURCES` | `0xf146a0` | `0xf12bd8` | image (game+0x50957f); `depth.c`; `BUILD-DATA-25480438.md` |
| `B_RESOURCE_BUCKETS` | 746 | 730 | image (game+0x5095a3); `depth.c` |
| `B_RESOURCE_RECORDS` | `0x2EA0` | `0x2da0` | image (game+0x5095f1); both are buckets × 16 |
| `B_DEFINITION` | `0x4D8` | `0x4d0` | image (game+0x509ae2, game+0x5095f1); `depth.c` |
| `B_DEPTH_PIN_1` / `HEX` | `0x5120b2` `4869c0d8040000…` | `0x509ae2` `4869c0d0040000…` | image (pattern search: the only such getter with a record within 8 bytes of retail's); `depth.c` |
| `B_DEPTH_PIN_2` / `HEX` | `0x511b3f`, 24 bytes | `0x50957f`, 36 bytes | image; `depth.c`. **Different instructions**: retail divides by 746 with `mul; shr rdx,9`, this build by 730 with `mul; mov; sub; shr 1; add; shr 9`. The pin covers the table load and the whole division, as retail's does |
| `B_DEPTH_PIN_3` / `HEX` | `0x511b57` `69c2ea020000442bc0` | `0x5095a3` `69c0da020000442bc0` | image; `depth.c` |
| `B_DEPTH_PIN_4` / `HEX` | `0x511bb1` | `0x5095f1` `4869c1d00400004805a02d00004903c2c3` | image; `depth.c` |
| `B_DEPTH_PIN_5` / `HEX` | `0x11193f3` `…9f241301` | `0x12873b3` `…df4b1401` | map; image; only the rip displacement differs |
| `B_DEPTH_PIN_6..9` / `HEX` | `0x1119419`, `0x1119ad1`, `0x1119b83`, `0x1119c44` | `0x12873d9`, `0x1287a91`, `0x1287b43`, `0x1287c04` | map; image, bytes the same as retail |
| `B_CARRIER_PIN_1..3` / `HEX` | `0x1118375`, `0x111a7cb`, `0x111a814` | `0x1286335`, `0x128878b`, `0x12887d4` | map; image, bytes the same as retail |
| `B_STACK_CONTEXT`, `B_STACK_INDEX`, `B_STACK_DT` | `0x80`, `0x48`, `0xa0` | the same | input-json static_caller_frame |
| `B_STACK_LENS` | `0x44` | the same | image: the depth pins read `[rsp+0x5c]` and `[rsp+0x60]` |
| `B_STACK_ROOT`, `B_STACK_ROOT_SOURCE` | `0x240`, `0x10` | the same | image: the carrier pins (`lea r8,[rbp+0x140]`, `[rbp+0x130]`, rbp = S+0x100) |
| `B_EXE_WORLD_MATRIX`, `B_HEX_WORLD_MATRIX` (47) | `0x1f30e0`, `…e8e2d5eaff…` | `0x1fd220`, `…e89206eaff…` | map; image; `native_contract.h` |
| `B_GAME_INPUT_RETURN` | `0x111863e` | `0x12865fe` | input-json; image (`ffd0` at game+0x12865fc) |
| `B_GAME_EXP` | `0x1f8ff70` | `0x21087d0` | input-json; image (call at game+0x128662a) |
| `B_GAME_PRESETS` | `0x2cb2140` | `0x32f9990` | input-json; image (`lea r9,[rip]` at game+0x127c056) |
| `B_GAME_INPUT_OWNER` | `0x2e38250` | `0x347cf18` | input-json |
| `B_GAME_CLOCK` | `0x2ce14f8` | `0x3326348` | input-json clock_cell; image (game+0x1289c3e). See Open 5 |
| `B_GAME_PLAYER` | `0x2ce1618` | `0x3326468` | map; `profile.h` |
| `B_WORLD_ENTITY_MAP` | `0xf24990` | `0xf22ec8` | map; `profile.h` |
| `B_WORLD_ENTITY_AVATAR` | `0xf349e8` | `0xf32f20` | map (`0xf349e0` → `0xf32f18`) + 8; `snapshot.c` |
| `B_AVATAR_STEERING` | `0x53e9ac` | `0x53e9a4` | input-json |
| `B_INPUT_RAW`, `B_INPUT_RAW_STRIDE`, `B_INPUT_FLAGS` | `0xa7c24`, `0xa7aec`, `0xa7af8` | the same | input-json; `native_contract.h` look_field, look_stride |
| `B_CONTEXT_INHIBIT`, `B_CONTEXT_LOOK` | `0x1a9`, `0x15c` | the same | input-json; `NATIVE-INPUT-DEEP-DIVE.md` |
| `B_PIN_EVALUATOR` / `LEN` / `FNV` | `0x1118370`, 9688, `0xac98356760db02c9` | `0x1286330`, 9688, `0x925c6f76c4b866ed` | map; input-json (same length, 2051 instructions); hash computed from the image |
| `B_PIN_SOLVER` / `FNV` | `0x1109110`, `0xabb508e82158f3f1` | `0x1277250`, `0xef9a68f3f959a945` | input-json (1328 bytes, equal after masking); image |
| `B_PIN_STEERING` / `FNV` | `0x111f860`, `0x23ae2b71a5e0f871` | `0x128d820`, `0xba858f4fe27538d8` | input-json (292 bytes); image |
| `B_PIN_ENTER` / `FNV` | `0x110fcf0`, `0x32dc74e3dc6a26e9` | `0x127de20`, `0x4bed0e223589e85e` | map; input-json (2992 bytes); image |
| `B_PIN_WORLD_MATRIX_FNV`, `B_PIN_LOCAL_POSITION_FNV`, `B_PIN_GRAPH_GETTER_FNV` | `0xd34dcd5634fed114`, `0x542e94a63b7ebae6`, `0x86a7f411c046a30c` | `0xcca0a362b082795b`, `0xb51d92e7e9e2090d`, `0x86a7f411c046a30c` | image at the three executable addresses above; the unchanged graph getter gives retail's own hash, which checks the hash routine |
| `B_PIN_EXIT` | `0x11108a0` | `0x127e9d0` | map; `profile.h` FP_EXIT (its hash is open) |

### Open (27 `B_` names for 20 unknowns, all 0, all in the look-gain normalisation)

| name | retail | why open |
|---|---|---|
| `B_PIN_2` + `_FNV` | game+0x111a948, 24 bytes | no record of the counterpart |
| `B_PIN_3` + `_FNV` | game+0x1114950, 1724 bytes | no record |
| `B_PIN_EXP_FNV` | game+0x1f8ff70, 1013 bytes | address known (`B_GAME_EXP`); that the function is 1013 bytes here too is not established |
| `B_PIN_6` + `_FNV` | game+0x1109640, 0x1a0 bytes | no record |
| `B_PIN_7` + `_FNV` | game+0x110b830, 0x5b0 bytes | no record |
| `B_PIN_9` + `_FNV` | game+0xe69ca0, 0xdc bytes | no record |
| `B_PIN_EXIT_FNV` | 0x1dd0 bytes | this build's exit block is a different function body (1730 instructions against 1711 in the compared span, input-json); its extent is not established |
| `B_PIN_12` + `_FNV`, `B_PIN_13` + `_FNV` | exe+0x1f3050 (51), exe+0x1f3090 (76) | no record |
| `B_CONST_ONE`, `B_CONST_MINUS_ONE`, `B_CONST_RAD_TO_DEG`, `B_CONST_MINUS_HALF_PI`, `B_CONST_HALF_PI`, `B_CONST_MINUS_75`, `B_CONST_DOUBLE_ONE` | game+0x22465c4, 0x2247894, 0x2246fb0, 0x22478c8, 0x2246774, 0x2247ab0, 0x2246900 | addresses of the seven numbers the evaluator loads were not looked up |
| `B_GAME_NO_AVATAR`, `B_GAME_HOST_AVATAR`, `B_GAME_IDLE_AVATAR` | game+0x2e3fcb8, 0x2e3fa9c, 0x2e3f0e4 | three cells `input_steering` reads; not in the records |
| `B_INPUT_DEVICE` | input owner+0x324 | no reader found on this build |

`B_INPUT_ESTABLISHED` is 0 because of these. With it 0, `input_code` (c:774) returns false at once, which is what it returns in retail when a pin differs: the statistics say reason 2 (`INPUT_CODE_UNAVAILABLE`), `active` 0, and no gain is ever changed. Of the 16 code pins, 7 are established and 9 open.

### Layout (45 `L_` names)

Named in the block with their evidence on each line. The API table offsets (`L_API_*`: +0x18, +0x38, +0x48, +0x40, +0, +0x90) and `L_REQUEST_POSITION` (0x48) are read off the image; the weapon-parameter numbers (`L_PARAMETERS` 0x3F0, `L_PARAMETER_KINDS` 0x350) off the image at game+0x178759d; the rest are the offsets this repository's 0.5.34 sources read (`snapshot.c`, `backend.c`, `scene.c`, `depth.c`). The 15 graph rows (node, hash, link; c:433-439) are the table `scene.c` holds.

Numbers that are retail's policy (timings, distances, thresholds, table sizes, limits) are literals where retail has them. Some small offsets inside blocks already read are also left as literals, as in retail: the 0x24-byte request state (+4, +8, +0xD, +0xF, +0x10, +0x18, +0x1C, +0x20), the 0x20-byte frame (+0, +1, +0x18, +0x1C), the pool header (+0x18, +0x28, +0x30, +0x40, +0x80), the definition block (+0xC8, +0xD4, +0xD8), descriptor words (+8, +12), and the input flag block (0..9, +0x4c, +0x58).

## 3. Dropped

Nothing reachable was dropped. All 23 exports are present.

| dropped | where | why nothing observable changes |
|---|---|---|
| the `NADSE_FIXTURE` seams | cpp:57-72, 152-155, 261-263, 269-272, 284-286, 290-300, 319-322, 481-483, 524-526, 577-581, 631-633, 675-677, 755-761, 843-845, 909-911, 958-960, 1015-1016, 1177-1178, 1268-1272, 1473-1475 | compiled out of the shipped DLL |
| `NADSE_InputCASException` | cpp:31-44 | it is the exception handler of the assembly's compare-exchange; both are `host.cas32` |
| `ZoomInputMath::prepare`, `residual`, `MIN_ALPHA`, its own tolerances and reason enum | `zoom_input_math.h:16-17, 20-21, 50-107` | the editor never calls them (it uses `ZoomGainMath::prepare`) |

Kept although nothing reads them: `Policy.guard_have[0]`, `Policy.shoulder_aiming`, `InputPending.camera`, `InputState.requested`, the unused error and reason numbers, `tfp_gain_pitch_after_base`.

## 4. Where C needed another construct

| retail | C | why the same |
|---|---|---|
| `struct Unlock` guard for `control_lock` in the exports | the body is a `*_locked` function; the export locks, calls it, unlocks (`set_transition`, `set_zoom_policy`, `rotation_trace`, `entry_position_stats` do it inline) | every return path releases at the same point; the check before the lock in `SetZoomPolicy` (cpp:1576) stays before it |
| `struct Unlock` guard for `policy_lock` in the hooks and in `restore_slots`, `set_offsets`, `SetZoomPolicy`, `SetTransition` | the part after the lock is its own function (`input90_locked`, `position_getter_locked`, `carrier_position_locked`, `position_locked`, `branch_rotation_locked`, `restore_slots_locked`, `set_offsets_policy`, `set_zoom_policy_policy`); the caller releases after it returns | one release, after the forwarded call, on every path |
| `struct Publish` in `position_call` (cpp:1047), destroyed before `Unlock` | `position_locked` returns, then `publish_diagnostics`, then the release (c:1368-1370). The early `!armed` return (cpp:1043) is before the guard and does not publish | same order |
| `PreserveEnvironment` in `ZoomGainMath::prepare` | `tfp_gain_prepare` calls `tfp_gain_prepare_guarded`, then `_mm_setcsr(csr)` (m:217-233) | the guard's destructor runs on every return after its construction |
| `std::atomic<Setter>`, `std::atomic<Getter>` | `_Atomic uintptr_t`, cast to the function type at the call | same load/store orders; a default `.load()`/`.store()`/assignment/conversion is `atomic_load`/`atomic_store` (sequentially consistent) |
| `std::atomic<bool> input_mode` used as a plain bool | `atomic_load(&input_mode)` | the implicit conversion is a sequentially consistent load |
| `std::atomic<float>` (display values, draw animation drop and back) | the float's bit pattern in `_Atomic uint32_t`, `ATOMIC_INT_LOCK_FREE == 2` asserted | the float read back is bit for bit the float stored |
| default member initialisers | `policy_init` (zero; rotation w 1; phase progress 1; target 1) and a static initialiser with the same values; `input_sample_init` (zero; signs 1, 1); `input_state_reset`, `memset` 0 for the all-zero ones; `display_cache` {0, 1} | same start values |
| `template value<T>`, `field<T>` | macro `value(at, out)`; `field_u32`, `field_u64`, `field_f32` | |
| default arguments (`position_call(…, native_stack=0)`, `input_abort(p, false)`, `begin_pose(…, normalize_input=false)`, `set_offsets(…, nullptr, false, false)`, `publish(…, now=0)`, `matches(…, now=0)`) | passed explicitly | |
| references to arrays (`float (&)[4]`, `Setter (&)[4]`) | pointers | `input_steering` reads `2*sizeof(float)` where retail has `sizeof out` |
| range-for over `rows`, `INPUT_PINS`, `constants`, `input_clones`, `entry_positions`, `normalized` | indexed loops in the same order | |
| `if(const EntryPosition* entry=…)` (cpp:1485) | declaration, then `if (entry)` | |
| `std::max`, `std::min` in `PresentationPhase::step` | `tfp_std_maxf`, `tfp_std_minf` (m:19-20): `a<b?b:a`, `b<a?b:a` | not `fmaxf`/`fminf`, which answer differently for NaN |
| `std::fabs`, `std::tan`, `std::sqrt` on floats | `fabsf`, `tanf`, `sqrtf`; on doubles `fabs` | the C++ overload chosen for a float argument is the float function |
| `std::numeric_limits<double>::infinity()` | `(double)INFINITY` | |
| `IMAGE_DOS_HEADER`, `IMAGE_NT_HEADERS64` | byte buffers of the same sizes (64 and 264) | the same two reads, same tests in the same order |
| one statement holding two (`if(matches!=1)return false;selected=…`) | two lines | gcc's misleading-indentation warning |
| direct calls of the game's functions (`originals[i](…)`, `verified[0](…)`, `original(unit,node)`, `exp`) | `call_setter`, `call_getter`, `native_exp` (c:986-998): direct call when the host member is NULL | additions for the tester only: with fake memory these addresses are not code |
| `exp` passed to `ZoomGainMath::prepare` as a `double(*)(double)` | `native_exp`, a static function that calls the address stored in `native_exp_address` just before (under `policy_lock`) | the arithmetic header keeps retail's function-pointer parameter |
| `callback(which)` = addresses of the hook functions and thunks | `host.hooks[…]` | the C entries take captured values as parameters, so what sits in a slot is the game build's wrapper |
| `alignas(16)` | `_Alignas(16)` | |

`tfp_pose_set_host` and `tfp_pose_reset_for_tests` are not in retail. The reset keeps the host.

## 5. The assembly files

`native_ads_input.S` (GNU) / `native_ads_input.asm` (MASM, the same code) **is part of the shipped editor DLL's behaviour**. `native/first_person/build_editor.py:26-29` assembles it and links it into the DLL. `tools/build_first_person.py` as written passes only `native_ads_editor.cpp`, which cannot link (the `.cpp` declares `NADSE_Input90Thunk`, `NADSE_Input00Thunk`, `NADSE_InputCompareExchange`, `NADSE_InputCASFault`, `NADSE_InputCASRecovery` as external, cpp:27-30, and uses them in `callback`, `input_dispatch`, `restore_slots`, `begin_pose`, `input_cas32`); the only recorded command that fits the shipped file adds `native_ads_input.S` (`docs/RETAIL-SMOOTH66-INVENTORY.md`, "Build scripts"). `input_fixture_thunks.S`/`.asm` is linked only into `input-fixture.exe` (`build_editor.py:33-39`): it is a test driver that jumps into a private copy of the game's evaluator, and is not part of the DLL.

The assembly is not translated. What it does:

1. **`NADSE_Input90Thunk`**, the value put in UnitAPI+0x90 in input mode. Entered by the game's `call rax` at game+0x12865fc (retail game+0x111863c) with `ecx` = unit, `edx` = node, and the evaluator's request pointer in the non-volatile `r15`. It reserves 56 bytes, leaves `rcx`/`rdx` untouched, sets `r8 = r15`, `r9 = rsp+64` (its own entry `rsp`+8: the caller's stack pointer at the call, the "S" of `B_STACK_*`), stores its own return address `[rsp+56]` as the fifth argument at `[rsp+32]`, calls `NADSE_Input90Call(unit, node, request, native_stack, caller)`, and returns its `rax`. C needs: a wrapper with exactly this capture that calls `tfp_pose_input90_from(caller, unit, node, request, native_stack)`. A C function cannot read `r15` or its caller's stack pointer portably, so this wrapper has to stay assembly (or a naked function).
2. **`NADSE_Input00Thunk`**, the value put in UnitAPI+0 in input mode instead of `position_getter_hook`. The same capture, calling `NADSE_Input00Call` = `position_getter_call(unit, node, native_stack, request, caller)`. C needs: the same wrapper calling `tfp_pose_position_getter_from(caller, unit, node, native_stack, request)`.
3. **`NADSE_InputCompareExchange(at, expected, desired, &observed)`**: `mov eax,edx; lock cmpxchg [rcx],r8d; mov [r9],eax; mov eax,1; ret`, with a language-specific exception handler (`NADSE_InputCASException`, cpp:31-44) registered for the function: an access violation whose address is the `cmpxchg` instruction and whose faulting address is within the 4 bytes at `rcx` resumes at `NADSE_InputCASRecovery` (`xor eax,eax; ret`); any other exception continues the search. C needs `host.cas32` with this meaning: true and the value found, or false when the write faulted.

The three plain hooks need no assembly in retail (`_ReturnAddress()`, `_AddressOfReturnAddress()`); in C they need a wrapper that supplies the return address, and for `position_hook` also the caller's stack pointer (the address of the wrapper's own return address + 8).

## 6. Open

1. **Look-gain normalisation is refused on this build.** c:109 (`B_INPUT_ESTABLISHED 0`), c:774, cpp:585-618. 20 values are not established (table above): 9 of the 16 code pins, the 7 constant addresses, 3 steering cells, the device offset. The code is translated and compiled; with the flag 0 it never passes `input_code`. `tfp_pose_begin_input` still installs the UnitAPI+0x90 hook and the input variant of slot 3 when `input_sdk` passes, exactly as retail does when the code proof fails; a caller that wants no extra hook on this build uses `tfp_pose_begin` / `tfp_pose_begin_steady`. The lengths of the 7 established pins are retail's; the hashes were computed here from the image and have not been compared with a running game.
2. **`B_DEPTH_PIN_2`**, c:77-79, cpp:450. The counterpart is a different instruction sequence (division by 730), so "the same instructions" is a judgement: I took the table load through the end of the division (36 bytes, up to the `imul` that `B_DEPTH_PIN_3` covers). The older port's `depth.c:179` pins only the first 17 of these bytes.
3. **Weapon definition layout**, c:71-74, c:557-622, cpp:382-445. That this build's 0x4D0-byte record keeps the zoom block at +0x74 with coefficients at +0xC8 and near terms at +0xD4/+0xD8 rests on `src/depth.c` (which additionally pins readers at game+0x1278f68/78/95: `+0x14c`, `+0x148`, `+0x13c`; I confirmed those bytes in the image but they are not among retail's nine pins, so the C does not check them). Retail's record is 8 bytes longer; where those 8 bytes sit was not determined.
4. **Layout constants in general.** Their evidence is that this repository's 0.5.34 sources read the same offsets, not a fresh disassembly. Without a counterpart I could point to: `L_REQUEST_SIGHT` (0x18) and `L_REQUEST_COUPLING` (0x98) are read by `pose_policy.c`, which the task says not to trust for behaviour; the request-state bytes at +0x80..+0xA3 used by the depth term (c:673-683, cpp:499-509) are evidenced only by the unchanged pins around them; the controller rule "bytes 0-3 and 5 zero" (c:408, cpp:209) and `L_UNIT_LOCALS`+0x24 for the carrier restore (c:1134-1135, cpp:936-937).
5. **`B_GAME_CLOCK`**, c:117, c:862, cpp:687. `0x3326348` is the research note's "clock cell" and the target of the load at game+0x1289c3e. Retail's cell is 0x10 below its settings cell; here it is 8 above. I did not verify that it is the cell the evaluator reads the global frame delta from. Only the refused input path uses it.
6. **`B_WORLD_ENTITY_AVATAR`**, c:123, cpp:662. Derived (rows base from the records, +8 from `snapshot.c`), not read off the image. Input path only.
7. **Start-up values are the shipped ones, not the fixture's.** c:1224, c:1247, cpp:1015-1020, 1177-1181. A comparison with a fixture build of the original (`NADSE_FIXTURE`: draw animation 0 ms, entry position off) has to set both to the same values first (`tfp_pose_set_entry_animation`, `tfp_pose_set_entry_position`), and has to answer `executable`, `pin`, `writable_slot` and the slot values through the host, as for the camera helper.
8. **Direct dereferences kept from retail.** `entry_rotation` and `rotation_hook` copy 16 bytes from `pose` without `read` (c:1394, c:1431; cpp:1207, 1243), `set_offsets` copies `rotation` (a local array), and the input path writes the private preset record directly (c:919, c:1034; cpp:741, 854). A tester must hand real memory for these.
9. **Float library functions.** `tanf`, `sqrtf`, `fabsf` come from the C library of whichever build; `exp` is the game's. Neither compile command enables FMA. `pose_math.h` uses SSE intrinsics and MXCSR as retail does and must not be built with fast-math.
10. **`tfp_pose_begin` address as the pin argument**, c:1625, cpp:1432. Retail pins the module holding `NADSE_Begin`; here the module holding `tfp_pose_begin`, the same module in the game build.
11. **Hook before `tfp_pose_set_host`** dereferences NULL; a hook before any begin calls a null original, as in retail. No guard added.
12. **`B_HEX_LOCAL_POSITION` is 51 bytes** as in retail; `native_contract.h` holds the first 48 of them.
13. **Not re-read line by line a second time in full.** I re-checked `begin_pose`, the five hooks, `restore_slots`, `set_offsets`, `depth_delta` and the input call against the source after writing; `depth_data`, `depth_map`, `input_sample`, `input_clone` and the statistics exports were checked once while writing. A frame-by-frame comparison is still needed.
