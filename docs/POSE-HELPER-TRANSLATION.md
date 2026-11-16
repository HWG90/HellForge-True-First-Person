# Editor/pose helper: the C translation

`src/pose_helper.c`, `src/pose_helper.h` and `src/pose_math.h` are `native/first_person/native_ads_editor.cpp` and `native_ads_editor.h` of the retail kit at 0.5.41-smooth.66, with `presentation_phase.h`, `zoom_gain_math.h` and the helpers of `zoom_input_math.h`, in C11, for build 25480438. "cpp:N" is a line of the retail file, "c:N" a line of `src/pose_helper.c`, "m:N" a line of `src/pose_math.h`. `src/camera_math.h` is not used: the editor needs none of its functions.

Both compile checks pass with no warnings: `cc -std=c11 -Wall -Wextra -Werror` and `zig cc -std=c11 -target x86_64-windows-gnu -Wall -Wextra -Werror`; `src/pose_math.h` also compiles as C++17. Sections 1 to 6 were written before anything had been run. The translation has since been compared with the original call by call: section 7, "Differential test". That test added nine lines to `src/pose_helper.c` after the constant block (a test-only include, section 7), so a "c:N" above 213 in sections 1 to 6 is now nine lines further down.

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

## 7. Differential test

`tests/run-diff-pose.sh` builds and runs three programs and prints one summary line for each; it exits non-zero on any difference, and says "skipped" and exits 0 when the retail tree, g++ or Linux is missing. `tests/run-diff-pose.sh "<part of a script's name>"` runs only those scripts, `--list` names them, `TFP_DIFF_REACHED=1` prints what each script reached, `TFP_DIFF_JOB=mode,index,value` runs one probe run.

1. `tests/diff_pose_math.cpp`: `src/pose_math.h` against `presentation_phase.h`, `zoom_input_math.h` and `zoom_gain_math.h`, bit for bit.
2. `tests/diff_pose_helper.cpp` (with `diff_pose_helper_world.h`, `diff_pose_helper_scripts.h`, `diff_pose_helper_open.h` and `tests/winshim/pose_extra.h`) linked with the **production** object of `src/pose_helper.c` (`cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror`). The retail `native_ads_editor.cpp` is compiled natively over `tests/winshim` with `NADSE_FIXTURE`.
3. The same test source linked with a second object of `src/pose_helper.c` built with `-DTFP_POSE_TEST_OPEN_CONSTANTS='"diff_pose_helper_open.h"'`, in which the look-gain normalisation is not refused (below, "Look-gain normalisation"). This program also needs the captured images of both builds; without them it says "skipped".

**Result: no difference between the original and the translation was found. No fix to the behaviour of `src/pose_helper.c`, `src/pose_helper.h` or `src/pose_math.h` was needed; the only change to the source is the test-only include described below.**

| program | compared |
|---|---|
| arithmetic | 125,203,576 results identical bit for bit: the SSE scalar helpers, `gain_valid`, the gain pipeline, the presentation phase in random walks, and 1.5 million look-gain preparations with every field sometimes out of range, the `exp` misbehaving in eight ways and MXCSR in six states; every one of the twelve result codes reached; every limit also taken exactly and one float step to either side |
| helper, production object | 818 scripts and 38,942 probe runs; 1,249,146 calls compared, 1,090,685 of them hook calls; 63,113,214 reads, writes, swaps, lock steps, allocations and forwarded calls matched one for one |
| helper, look-gain object | 818 scripts and 61,236 probe runs; 1,586,297 calls compared, 1,361,026 of them hook calls; 84,368,214 steps matched one for one |

About 32 seconds for all three on 16 cores (roughly five and a half minutes of processor time). A soak of 600 further random walks in each helper program (`TFP_DIFF_EXTRA_WALKS=600`, 5.07 million calls each) found no difference either. 55,630 hook calls in each helper program were skipped (a hook before any begin: below).

### How the two are made comparable

- One logical scene (`Scene`) is written into two sets of real pages: one with retail's addresses and offsets (from `native_ads_editor.cpp`), one with this build's (the tables of section 2). Structure layout numbers both builds share are written once, as retail has them. Every region has an unreadable page on each side.
- An address in one world is compared with an address in the other by name and offset ("rig+0x70", "game:api_root+0", "owner:flags[1]+0", "game:pin_solver+0x400"). Three kinds of place are named by what they are, because their offsets legitimately differ: a weapon definition record by its index (records are 0x4D8 bytes in retail, 0x4D0 here), a resource row by its distance from the weapon's own bucket (746 buckets against 730), and a read of a whole depth pin (the second pin is 24 bytes in retail, 36 here: Open 2).
- The original reads through the stand-in `ReadProcessMemory`; the translation's `host.read` makes the same `process_vm_readv` call. Clock: both read the original's `fixture_now`. Thread: one test variable. The page questions (`writable_slot`, `executable`, `writable32`, `region`) are answered for the translation by the original's own functions or by the routine the original's stand-in `VirtualQuery` uses. The compare-and-swap of a dispatch slot is real in both, with the original's fixture seams (`fixture_cas_fail_at`, `fixture_foreign_cas_at`, `fixture_fail_rollback`, `fixture_slot_readonly`) repeated on the translation's own call count.
- The five dispatch slots are compared by what they hold: the game's function (original: the test's functions, as `fixture_targets`; translation: `exe + wrapper`), one of the helper's six values (original: its four hook functions and the two thunk addresses; translation: `host.hooks`), or a raw foreign value.
- The hooks are called directly: the original's `position_hook`, `rotation_hook`, `branch_rotation_hook`, `position_getter_hook` with `_ReturnAddress()` answering the call site of its world and `fixture_depth_frame` the evaluator's stack frame, and `NADSE_Input90Call` / `NADSE_Input00Call` with the values the thunks capture; the translation's five `tfp_pose_*_from` entries with the same values of its own world. Forwarding goes to the test's "game functions" in both (`fixture_targets`, `host.forward_setter`, `host.forward_getter`); the position setter on the rig's node 0 writes that node's local position, which the helper reads back before it restores the root.
- **The assembly.** `native_ads_input.S` is not assembled. Its two thunks only capture R15, the stack pointer and the return address and call `NADSE_Input90Call` / `NADSE_Input00Call`; the test calls those two directly, as retail's own `input_fixture.cpp` does, and defines the two thunk symbols only for their addresses (what the original stores in a slot). `NADSE_InputCompareExchange` with its fault handler is one C++ routine (`cas32` in the test) that serves the original and, as `host.cas32`, the translation: the swap and the value found, or 0 when the four bytes cannot be written. Nothing in the original was left out for lack of the assembly.
- The fixture build of the original starts with the draw animation off and the entry position off (cpp:1016, 1178); the test sets the shipped values (220 ms, on) in the original only and leaves the translation at its own start-up values, which is where those are compared.
- Every script runs in a child process forked from a parent that has built the two worlds and given the translation its host, and has made no other call to either helper.

### What is compared, after every call

The return value (for the two getters: that the game's own result pointer came back); the floating-point control word; the full ordered record of what the helper did: every memory read (place, length, complete or failed), every write through `WriteProcessMemory`/`host.write`, every compare-and-swap of a slot (what was stored, expected, found), every 32-bit compare-and-swap of the request's preset selector, every lock and unlock of the two locks, every allocation near the preset table, every call of the game's `exp` with its argument, and every forwarded setter or getter call (slot, unit, node, whether the pointer forwarded was the game's own argument, the helper's buffer or null, and the 16 bytes of the position or quaternion bit for bit); the five slots; the request's preset selector and the 144 bytes of every private preset record; the generation written by a begin; the error code; then `read_stats` (unless the script has switched polling off: that call can itself end a timed lease), `read_depth_stats`, `read_input_stats`, `rotation_trace` (each row's caller by name) and `entry_position_stats`, byte for byte, and the record of those five calls themselves. The translation's locks must be balanced at each return, and both helpers must have attempted the same number of slot swaps.

### Scripts

| group | scripts |
|---|---|
| placement set and changed live: the four begins, three frame steps, the bounds and beyond them | 12 |
| motion: offsets and the rotation delta within and outside their bounds (0.15 m, 120 degrees, length tolerance, negative w); the delta's gates (coupling, a second request, full ADS, authored, other writers, null and invalid rotations, the 0.02 and 0.025 tolerances) | 3 |
| ADS placement and the hip weight fading into the sights and back: blended and steady, two steps, blend 150, 1 and 1000 ms | 12 |
| the presentation phase packet: epochs, progress, ads, repeated, regressing, older, stale at 250/251 ms, published "in the future", a new epoch in mid-fade, wrong generations | 6 |
| entry: blended over 150 ms and steady with the draw animation (default, off, 1000 ms and 1 m, refused settings, no placement, into the sights), the clock stepping back | 24 |
| the entry rotation guard: 70 degrees halving, 1.6 and 1.4 degrees, the limit itself and one float step beyond it, the window's last millisecond, the clock stepping back, seven settled writes, switched off, switched inside the window, owner lost inside the window, a write standing still, a second lease on another rig; the trace filling its 64 rows with callers from both images (their first and last bytes too) and from neither | 13 |
| the remembered entry position: four weapons, nineteen weapons in sixteen places, switched off and on, animation off, armed in the sights, another weapon in between | 6 |
| zoom depth and the rig-root counter-shift: resource and override definitions, widest lens last, near terms, with a placement, mixed by the request, through the sights, collisions, the last record, a row search that wraps, 128-row maps, the 64th probe, a lens on its way when the lease begins; every zoom level and the transitions between them, an overshooting lens, a lens span of one float step, a change of the prior lens alone, the zoom policy switched in mid-transition; two begins, four frame steps (two of them landing on the 500 ms times) | 96 |
| zoom depth refused: 57 causes (definitions, maps, the request's zoom state, the lens locals, the code pins, the observation and what withdraws it), each from the first frame and after valid frames, then lifted; five of them again at 25, 50, 125 and 500 ms steps | 154 |
| the rig-root counter-shift's own gates (pose pointer, frame, thread, another writer, not alone, quaternion, request changed, full ADS, owner lost, pose array replaced, write not applied, consumed once, code changed, withdrawn by a later rig call, getter call or zoom-policy switch, one request in the ring that is not ours) | 25 |
| what retires the three cached baselines, seen by what end, expiry or a refused placement then replays | 42 |
| node 0 / 5 / 50 / other from the game's five call sites and from five other callers, on the rig, another rig, the weapon and no unit, with each pose pointer, armed and unarmed, the four begins | 8 |
| the lease duration (1, 40, 300, 30500 ms) expiring, found by each of nine paths, one millisecond before and at the deadline | 36 |
| control calls in right and wrong orders; statistics with wrong generations, headers, null and read-only pointers; the caller's memory losing write access in mid-call (begin's second write of the generation, the statistics), the four begins | 5 |
| refused configurations, owners, graphs, images, code bytes and slots (about 170 cases, each with the error code it must be refused with), the four begins | 4 |
| the owner lost while a lease runs: 38 causes, found by each of six paths | 228 |
| the five dispatch slots: swap faulting at begin and at end, a foreign value arriving at begin and during the lease, the table read-only at end, the rollback faulting (begin answers 2), the game's value back before end | 35 |
| time: gaps, standstill, steps back, a lease armed at time 0, a clock about to wrap, blend 1 ms | 8 |
| second avatar row; request rows 31 and 0 (a request kind with high bits set); checks only the translation makes; after `tfp_pose_reset_for_tests` | 5 |
| look gain: evaluations at five frame rates with mouse, two pads, inverted axes, steering, look context, pitch gate, and each invert flag of each device on its own with steering; 47 cases around one evaluation (retail's input-fixture cases among them); more presets than private records; the code proof made late in six ways | 88 |
| seeded random walks, 2,500 steps each, mixing all of the above with time steps of every kind and one-frame damage | 8 |
| probes (21 calls: rig position call plain, with a definition refresh and with the revalidation, lens observation, carrier call, branch rotation call, the same inside the entry window and at the deadline, end, set_placement, set_zoom_policy, begin_steady, validate, read_stats at the deadline, read_input_stats; a look-gain evaluation, its completion, end with a published selector, begin_steady_input, a re-entry, the first evaluation): for every single read the call makes, one run in which that read fails and heals, one in which it stays unreadable to the end of the call, and one for each of 45 (look gain: 69) values changing just before that read | see the numbers above |

Each script also checks, from the original's state, that it reached what it is for.

### Look-gain normalisation

`src/pose_helper.c` has, after the constant block, `#ifdef TFP_POSE_TEST_OPEN_CONSTANTS` / `#include TFP_POSE_TEST_OPEN_CONSTANTS` / `#endif`: test only, off by default, never defined in a shipped build. `tests/diff_pose_helper_open.h` redefines the 27 open names and `B_INPUT_ESTABLISHED` with places in the test's fake game image (chosen at offsets unlike retail's) and, for the nine open code pins, the hashes of the retail bytes the test copies there. They are **not** this build's values.

- Retail world: the sixteen pinned ranges hold retail's own bytes, read from the captures the retail input fixture uses (`TFP_INPUT_CAPTURE_DIR`).
- This build's world: the seven established pins hold **this build's own bytes**, read from `game-pre1020.dll` and `exe-25480438.bin` (`TFP_HERE_IMAGE_DIR`), and are proven with the production hashes of section 2, which the test therefore confirms against the image; the nine open pins hold retail's bytes at the test's places. At load the test also checks, in both images, the call sites, the depth pins 5 to 9, the three carrier pins, the two getters and the graph getter against the literals of both builds.
- **Result: with the constants supplied, `input_code`, `input_sample`, `input_same`, `input_steering`, `input_map`, `input_clone`, `input_cas32`, `input_row`, `input_abort`, `input_quarantine`, `input_complete`, the 0x90 entry and the gain arithmetic behave as retail's in every compared call**, including the allocation of the private records (compared by content and as "private record k", since the index a helper stores depends on where the block lies), the 32 records filling up, allocation refused, the selector page losing write access between the query and the swap, and every read failing or value changing in six probed calls.
- The production program runs the same look-gain scripts. There the translation refuses the code proof without reading (`B_INPUT_ESTABLISHED 0`) and the original, whose fake image holds no retail code in that program, hashes its first pin and refuses too; those reads of the original are the only steps without a counterpart and are counted as "retail-only reads of pinned code". Everything else is compared, and after every evaluation the scripts assert on the translation itself: reason `INPUT_CODE_UNAVAILABLE`, not active, nothing normalised, pending or cached, no private block allocated, the request's selector and the game's preset table untouched.

### Checks only the translation makes

The reference is the fixture build of the original, which compiles some shipped checks out (section 1, Open 7). The translation has them; they are answered through the host and asserted separately (script "checks only the translation makes", expected results read off the original's non-fixture lines):

- `executable(graph getter)` in `graph` (cpp:237-239): answered true in every compared call; answered false at validate and begin (expected and got: 0, error 15, generation 0, slots untouched) and in a rig position call (error 15, the game's own argument forwarded, disarmed).
- `pin(exe)` in begin (cpp:1434): answered false once (expected and got: 0, error 9, generation 0, slots untouched). `pin(game)` (cpp:1435): answered false once (expected and got: armed, the 0x90 slot untouched, the plain getter hook in slot 3, input statistics "not installed", reason 3).
- `writable_slot`'s image test: the host checks that the image asked about is the executable and answers with the original's fixture test.
- Each slot must hold `exe + wrapper` (cpp:323, 634): compared through the slot classes.
- `tfp_pose_reset_for_tests`: after it (with the original's statics put back by hand) both agree again from generation 1; right after it the translation hands the host a null original for all five hooks.

### Not compared, and why

- **Concurrency.** One thread. The order of lock operations relative to reads, swaps and forwarded calls is compared; End waiting for a hook in flight, or a re-arm while a hook waits for the lock, is not exercised.
- **The assembly's own instructions**: the register capture of the two thunks and the exception handler of the 32-bit compare-exchange (above).
- **Direct calls of the game's functions** (`host.forward_setter`, `host.forward_getter`, `host.native_exp` NULL). The fake `exe + wrapper` is not code.
- **The host's own page logic.** `writable_slot`, `executable`, `writable32`, `region` and `allocate` are answered from the same routines for both sides.
- **A hook call before any begin recorded the game's functions.** The original calls a null pointer there. Such calls are skipped and counted.
- **A later begin finding another function behind a slot than an earlier lease recorded** (cpp:1431): needs the executable at two bases in one process.
- **The generation counter wrapping past 2^64.**
- **Whether the constants of section 2 are right for build 25480438.** The test proves the translation reads the places the tables name. It confirms the seven established pin hashes and the literals listed above against this build's image; it says nothing about the 27 open names.
- **The Windows build of the C** (MinGW's `tanf`, `sqrtf`): natively both sides use glibc, with `-ffp-contract=off`.
- **Internal state no export or later behaviour shows**: `Policy.shoulder_aiming`, `InputPending.camera`, `InputState.requested`, and what `End` leaves in a policy that the next begin replaces.

### Test-only additions

`src/pose_helper.c`: the nine-line `TFP_POSE_TEST_OPEN_CONSTANTS` block. `tests/winshim/pose_extra.h`: the exception types the original's fault handler names, `GetCurrentThreadId`, `_AddressOfReturnAddress`, a `VirtualQuery` that describes unmapped address space as free, a `VirtualAlloc` that honours the requested address, an allocation observer and a switch that makes allocations fail. `tests/winshim/windows.h`, `intrin.h` and `tests/run-native.sh` are unchanged.

### Things the test showed about the original (the translation does the same)

Not differences; noted because they surprised the test's author.

- The entry rotation guard has one limit, 1.5 degrees (`ENTRY_GUARD_COSINE_HALF`): it holds while the game's write is further than that from the settled rotation and lets go for the rest of the lease the first time it is not, or when two successive writes are within a quarter of a degree, or after 300 ms. There is no separate 12 degree trigger in smooth.66 (the source comment says the 12 degree limit was replaced).
- A begin whose second write of the generation fails has already taken a generation number.
- The selected zoom level and the pool's descriptor row are read at a distance event only; the 500 ms revalidation does not look at them.
- Zero stands for "no previous distance" and is taken as known, so a lens transition that is under way when a lease begins is bridged from 0.
- An evaluation whose 0x90 call is repeated on the same frame without its 0x00 call in between is poisoned for good: the lease then stays reserved until the process ends (End answers 0, `CLEANUP_PENDING`).
- With steering, some combinations of invert flags make the bounded inverse decline (reason 0x100 + a gain code); nothing is published then.

### Mutation check

Done on 2026-11-16 in scratch copies outside the repository: one change at a time in `src/pose_helper.c` or `src/pose_math.h`, the arithmetic program (for the header), then both helper programs run against each. **283 mutations, 260 killed, 23 survive.** The first complete round killed 241 of 274 valid mutations (nine did not compile and were rewritten); each survivor was either explained or answered with new scripts (the retired caches, the limits taken exactly, frame steps landing on the 500 ms times, the 64th probe, the withdrawn observation, sign flags with steering, the caller's memory losing write access, a lease on another rig, the prior lens alone, the form of a trace row's caller, a row the ring has moved past, the single-precision length sum), and the survivors and rewritten mutations were run again. 200 were killed by the plain scripts of the production program, 44 only by the look-gain program's, 9 by the arithmetic program and 7 only by probe runs.

Killed, by kind: 28 of 29 timing constants and thresholds (500, 250, 300, 220 ms; 0.35 and 0.06 m; the guard's three cosines and its count of eight; 0.15, 0.5 m; 0.02, 0.025, 0.002; 4 m, 1 m, 2.96; 30500, 1000; ...); 27 of 28 `<`/`<=` changes; 27 of 36 dropped resets; 21 of 21 dropped counter increments; 14 of 16 float/double and reassociation changes; 47 of 48 changes to this build's constants and the layout numbers; 9 of 13 lock placements (among them the policy lock released before the forwarded call, and the lock taken before the original getter, both caught by the order of steps); 87 of 92 logic changes.

The 23 survivors, none of them a gap this test can close:

| survivor | why it cannot be seen |
|---|---|
| depth angle bound 1.48 -> 1.5 | the widest lens's angle is never smaller than the selected one's, and its own 1.48 bound is tested in the same condition |
| draw animation `t >= 1` -> `> 1` | at exactly 1 the other branch gives (1 - 1) cubed = 0 too |
| End without clearing `motion`; without resetting `rotation`; without clearing the observation and `depth.valid`; without clearing `active_generation` | after End the lease is disarmed and every reader of these tests `armed` first; the next begin replaces the whole policy |
| begin without priming `display_cache` | the cache is overwritten by the first statistics read: visible only to a second thread |
| zoom policy change without clearing `signature_set`; without clearing `retry_at` | each alone still forces the refresh: the other one does |
| slots restored without `owned_generation = 0`; without `input_lease_generation = 0` | read only while a lease is reserved, or while input mode is on; the next begin sets both |
| blend share computed in double | for a blend time of at most 1000 ms the quotient of two small integers rounds the same either way; beyond 1 it is clamped |
| `tanf(x)` -> `(float)tan((double)x)` | equal on every angle the scripts reach with this C library |
| `L_FLAG_SHOULDER_A` 10 -> 9 | feeds `Policy.shoulder_aiming`, which nothing reads (section 3); the 24 flag bytes are read either way |
| diagnostics published after the lock is released; the unarmed re-check forwarding after the release; `watched_viewmodel` cleared under the lock; begin's input state reset before the lock | no read, write, swap or forwarded call moves relative to a lock step, or the path needs a second thread to be reached |
| branch call without the generation re-check after the lock; rotation hook tracing without the unit re-check | the value loaded before the lock cannot have changed without a second thread |
| `input_same` comparing three of the four adjacent floats | the fourth is the device's scope gain, which is part of the flag bytes compared just before |
| a busy private record reused | a record is busy exactly while an evaluation is pending, and a pending evaluation ends the next 0x90 call before any record is chosen |
| steering not zeroed for the "no avatar" id | the sample starts zeroed |

