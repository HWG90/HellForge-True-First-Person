# Camera helper: the C translation

`src/camera_helper.c` and `src/camera_helper.h` are `native/first_person/native_ads_camera.cpp` and `native_ads_camera.h` of the retail kit at 0.5.41-smooth.66, in C11, for build 25480438. "cpp:N" is a line of the retail file, "c:N" a line of `src/camera_helper.c`. The arithmetic headers are not translated again: `src/camera_math.h` is used.

Both compile checks pass with no warnings: `cc -std=c11 -Wall -Wextra -Werror` and `zig cc -std=c11 -target x86_64-windows-gnu -Wall -Wextra -Werror`. Nothing here has been run; no tests were written.

## 1. The host interface (`TfpCameraHost`, set once with `tfp_camera_set_host`)

| member | replaces in the retail file |
|---|---|
| `hook` (a value) | the address of `setter_hook` (`callback()`, cpp:1027): what is put in the dispatch slot and recognised there. In the game it is the wrapper that calls `tfp_camera_setter_from` |
| `read(at, out, length)` | `ReadProcessMemory` in `read` (cpp:191), all bytes or false. The bounds on `at` and `length` stay in C (c:251) |
| `readable_span(at, length)` | `readable_span` (cpp:164-178, `VirtualQuery`). Called only after a failed read while an owner proof is open, as retail |
| `now()` | `now()` (cpp:139-151), milliseconds as a double |
| `writable_slot(at, exe)` | the `VirtualQuery` part of `writable_slot` (cpp:437-441). The 8-byte alignment test stays in C |
| `executable(at)` | `executable` (cpp:228-232) |
| `cas(slot, expected, desired, &observed)` | `InterlockedCompareExchangePointer` inside `__try` (cpp:457-463); false = the operation faulted |
| `write(out, data, length)` | `WriteProcessMemory`: the generation (cpp:1074) and the four result structures (cpp:1137, 1266, 1285, 1303) |
| `pin(address)` | `GetModuleHandleExW(FROM_ADDRESS, PIN)` (cpp:1060); called with the address of `tfp_camera_begin` and with the executable base, in retail's order |
| `lock_exclusive`, `unlock_exclusive`, `lock_shared`, `unlock_shared` (lock number) | `AcquireSRWLock*`/`ReleaseSRWLock*` on `control_lock` (0) and `policy_lock` (1) |
| `forward(original, camera, unit, native)`, optional | nothing in retail. NULL: the original setter is called directly, as retail does. See section 4 |

The hook's caller address is a parameter: `tfp_camera_setter_from(return_address, camera, unit, native)` (retail `_ReturnAddress()`, cpp:750).

Plain exchange (`InterlockedExchangePointer`) appears in retail only inside a fixture seam (cpp:455), so the host has none.

The fixture seams (`fixture_*`, cpp:123-138 and their uses) are not translated. Where retail has a fixture path and a game path, the game path is translated: `graph` asks `executable(getter)` (cpp:330-332), `contract` requires the slot to hold `exe + setter` (cpp:1039), `Begin` pins the executable (cpp:1095-1097), `writable_slot` asks about the executable image (cpp:440-441).

## 2. Constants

All are in one block at c:12-114. `B_` = differs per build, `L_` = structure layout, the same number in both builds. Evidence: **map** = `tools/build_map.py --to-here`; **image** = bytes read from `game-pre1020.dll` / `r6_reload_exe_20261017.bin` on 2026-11-16; a file name = that file of this repository reads the same field on this build.

| name | retail | here | evidence |
|---|---|---|---|
| `B_GAME_STAMP` | `0x6af322c1` | `0x6ab3b43f` | map; `profile.h`; image PE header |
| `B_GAME_SIZE` | `0x3382000` | `0x4744000` | map; `profile.h`; image PE header |
| `B_EXE_STAMP` | `0x6af2d5e6` | `0x6ab382e4` | map; `profile.h`; image PE header |
| `B_EXE_SIZE` | `0x29fe000` | `0x39e8000` | map; `profile.h`; image PE header |
| `B_EXE_SETTER` | `0x2dd5a0` | `0x30deb0` | map; `native_contract.h` camera_position |
| `B_GAME_CALL` | `0x110bff5` | `0x127a135` | map; `native_contract.h` camera_position_call |
| `B_EXE_GRAPH_GETTER` | `0x28d3c0` | `0x2bd870` | map; `native_contract.h` graph_getter |
| `B_EXE_ANIM_GETTER` | `0x28d510` | `0x2bd9c0` | map; `native_contract.h` anim_getter |
| `B_EXE_UNIT_WORLD` | `0x1f30e0` | `0x1fd220` | map; `native_contract.h` unit_world_matrix |
| `B_GAME_CALL_PAIR` | `0x110bfe7` | `0x127a127` | map; `native_contract.h` camera_scratch_pair; 14 bytes before the call in both builds |
| `B_HEX_CALL` | `41ff5120` | the same | image at game+0x127a135 |
| `B_HEX_SETTER` (48 bytes) | `…e81931dcff…` | `…e8f9f9d8ff…` | image at exe+0x30deb0; only the call displacement differs |
| `B_HEX_UNIT_WORLD` (47 bytes) | `…e8e2d5eaff…` | `…e89206eaff…` | image at exe+0x1fd220; only the call displacement differs |
| `B_HEX_GRAPH_GETTER` | `488d4160c3` | the same | image at exe+0x2bd870 |
| `B_HEX_ANIM_GETTER` | `488b8178010000c3` | the same | image at exe+0x2bd9c0 |
| `B_HEX_CALL_PAIR` (43 bytes) | `…488b05c854bd01…` | `…488b05c8c10a02…` | image at game+0x127a127; only the rip displacement differs, and here it resolves to game+0x3326308 = `B_GAME_API_ROOT` |
| `B_GAME_CONTEXT` | `0x2e287d0` | `0x346d560` | map; `profile.h` FP_CAMERA |
| `B_GAME_AVATAR_OWNER` | `0x2ce1ee0` | `0x3326d20` | map; `profile.h` FP_AVATAR |
| `B_GAME_CONTROLLER` | `0x2e381a8` | `0x347ce70` | map; `profile.h` FP_CONTROLLER |
| `B_EXE_REGISTRY` | `0x1ab76f8` | `0x1a100f0` | map; `profile.h` FP_REGISTRY |
| `B_GAME_STATE` | `0x2ce1508` | `0x3326340` | map; `profile.h` FP_STATE |
| `B_STATE_SCENE` | `0xac29c` | `0xac21c` | map; `profile.h` FP_STATE_SCENE |
| `B_GAME_API_ROOT` | `0x2ce14c8` | `0x3326308` | map; `profile.h` FP_API_ROOT; image (above) |
| `B_AVATAR_FLAGS` | `0x53e888` | `0x53e880` | map; `profile.h` FP_AVATAR_FLAGS |
| `B_AVATAR_RIG` | `0x53e9c0` | `0x53e9b8` | map; `profile.h` FP_AVATAR_RIG |
| `B_AVATAR_STRIDE` | `0x1240` | `0x1238` | map; `profile.h` FP_AVATAR_STRIDE |

26 build-specific constants, none open.

Layout constants (59, the same number as retail, named in the block with their evidence on each line):

| group | names | evidence |
|---|---|---|
| avatar owner | `L_AVATAR_COUNT` 0x6C, `L_AVATAR_ROWS` 0x110 | `snapshot.c` |
| avatar flag bytes | `L_FLAG_SHOULDER_A` 10, `L_FLAG_SHOULDER_B` 15, `L_FLAG_SEATED` 14 | `snapshot.c` |
| avatar flag byte | `L_FLAG_FIRE` 12 | **none on this build**, see Open 1 |
| controller | `L_CONTROL_AIM` 4, `L_CONTROL_GUARD` 5 | `snapshot.c` |
| unit registry | `L_REGISTRY_BLOCK` 0x88, `L_REGISTRY_CAPACITY` 0x98 | `backend.c`, `probe.c` |
| API table | `L_API_CAMERA` 0x20, `L_API_SETTER` 0x20 | image: `mov r9,[rax+0x20]`, `call [r9+0x20]` in the call pair |
| camera context | `L_CONTEXT_CAMERA` 0, `L_CONTEXT_UNIT` 8, `L_CONTEXT_RING` 0x1F8, `L_CONTEXT_IDLE` 0x1F00, `L_CONTEXT_RECORDS` 0x200, `L_CONTEXT_RECORD` 0xE8 | `snapshot.c`, `backend.c` |
| camera context | `L_CONTEXT_BASIS` 0x0C | the older port (`camera_policy.c:472`) only, see Open 2 |
| unit object | `L_VT_GRAPH_GETTER` 0xE8, `L_UNIT_GRAPH` 0x60, `L_UNIT_HEADER` 0x70, `L_UNIT_EXTERNAL` 0x78, `L_UNIT_WORLDS` 0x88, `L_UNIT_LINKS` 0x98, `L_UNIT_HASHES` 0xA0, `L_UNIT_PARENT` 0x1D0, `L_HEADER_*`, `L_GRAPH_*` | `scene.c` |
| unit object | `L_VT_ANIM_GETTER` 0x1B0, `L_UNIT_ANIM` 0x178, `L_UNIT_RESOURCE` 0x358, `L_ANIM_GRAPH` 0x28, `L_ANIM_ACTIVE` 0x50, `L_ANIM_LAYER_COUNT` 4, `L_ANIM_TABLE` 8 | `action.c` |
| matrices | `L_MATRIX` 0x40, `L_MATRIX_POSITION` 0x30, `L_LOCAL_POSITION` 0x24 | image (`shl rcx,6`; `add rcx,0x24` in unit_local_position); `scene.c` |
| camera object | `L_CAMERA_GRAPH` 0x18, `L_CAMERA_NODE` 0x20 | `scene.c` |
| setter argument | `L_ARGUMENT_ROTATION` 64 | image: `lea r8,[rbp-0x40]` then `lea r8,[rbp+0]` in the call pair |
| body, rig | `L_BODY_RESOURCE`, `L_HEAD_HASH`, `L_HEAD_PARENT_HASH`, `L_RIG_NODES` 63, `L_RIG_CAMERA_NODE` 50, `L_RIG_CAMERA_HASH`, `L_RIG_CAMERA_LINK`, `L_CAMERA_RIG_LINK` | `scene.c`, `action.c` |
| animation states | `L_ANIM_LAYERS` 31, `L_ANIM_LAYER` 13, `L_STATE_ON_FOOT` 36, `L_STATE_BRIDGE` 3, `L_SCENE_SHIP` 3 | `action.c` (see Open 3 for the scene) |

Numbers that are retail's policy, not build data (timings, distances, thresholds, table sizes, the limits 512 / 2048 / 0x400000, hold bits) are left as literals where retail has them.

## 3. Dropped: the retired features

| dropped | where | why nothing observable changes |
|---|---|---|
| `NADSC_FreeAim`, `NADSC_FreeAimStats`, `FreeAim::State free_aim`, `free_*` fields | cpp:34, 37, 1160-1179; resets at cpp:826, 828, 831 | The state is only reset in the hook, never applied, and only the two dropped exports read it |
| `NADSC_SetKick46`, `NADSC_KickStats46`, `kick_*` fields | cpp:36-37, 1198-1211; cpp:831 (`kick_angle=0; kick_fire_previous=fire`) | Written only; read by the dropped stats export |
| `NADSC_SetCameraMotion47`, `motion[6]` | cpp:33, 1225-1230 | Stored, never read |
| `NADSC_ObserveParent`, `NADSC_ParentStats`, `observe_*`, `observe_camera_parent` | cpp:35, 733-746, 823, 1181-1196 | `observe_until` is nonzero only after `NADSC_ObserveParent`; with it at 0 the function returns at its first statement (cpp:734) without a read |
| `NADSC_Profile47`, `NADSC_ProfileStats47`, `profile_*` | cpp:30, 33, 757, 1022-1025, 1232-1244 | `profile_remaining` is nonzero only after `NADSC_Profile47`; with it at 0 the hook takes no timing and writes nothing |
| `NADSC_SetOffsets` | cpp:1141-1158 | It was the only writer of `side` and the only later writer of `c.forward`/`c.up`, and the only caller of `correction.remember` outside the hook |

Kept although only a retired feature could change it: **`Policy.side`** (c:913, cpp:905). It is always 0 now, but the term `horizontal[1]*side` stays in the target expression, because adding a signed zero is not always the same as adding nothing (a target of -0.0 becomes +0.0) and a product with a non-finite `horizontal` would be NaN. Also kept: `Policy.fire` and `Policy.shoulder` (c:363), computed in `owner` as in retail; nothing reads them any more.

I found no place where a retired feature changes an output on the normal path.

## 4. Where C needed another construct

| retail | C | why the same |
|---|---|---|
| `struct Unlock` guard for `control_lock` in every export | the body is a `*_locked` function; the export locks, calls it, unlocks (`tfp_camera_entry_trace` and `tfp_camera_anchor_stats` have no early return and do it inline) | every return path releases at the same point; argument checks that retail does before the lock (`SetHandoff49`, cpp:1250) stay before it |
| `struct Unlock` guard for `policy_lock` in the hook (cpp:756) | one `policy_release()` after the forwarded call and the diagnostics (end of the function); the only earlier `return` is before the lock | the hook has no other exit |
| `OwnerReadProbe` constructor/destructor (cpp:157-163) | written out in `owner_status` (c:373): push, run `owner`, compute the result, pop | `owner_status` is the only user and has one exit |
| `thread_local` | `_Thread_local` | |
| `std::atomic<Setter> original` | `_Atomic uintptr_t`, cast to the function type at the call | same load/store orders |
| `std::atomic<float>` display values with an always-lock-free assert (cpp:105-106) | the float's bit pattern in `_Atomic uint32_t`, `ATOMIC_INT_LOCK_FREE == 2` asserted | C has no lock-free guarantee to assert for `_Atomic float`; the float read back is bit for bit the float stored |
| default member initialisers (`Policy{}`, `EyeMemory{}`, `NativeSample{}`, `PendingParent{}`, `FreeAim::Q{}`) | `policy_init` (zero, then correction progress 1 / duration 150 and four identity quaternions), a static initialiser with the same values for the global, `memset` 0 for the all-zero ones, `pending_parent_default()` (path 2), `tfp_q_identity()` | same start values |
| `template value<T>` | macro `value(at, out)` = `read(at, &out, sizeof out)` | |
| default arguments (`resolve(…, capacity=0)`, `points(…, root_basis=nullptr)`, `reset(p, keep=false)`, `direct_camera_parent`, `remember_native`, `apply(…, 1.0f)`, `apply(…, 120.0f)`) | passed explicitly | |
| comma expressions with `memcpy` inside one condition (`owner`, cpp:262-264) | three consecutive `if`s | same order of reads and tests |
| `else if(const uint32_t current=owner_status(…); …)` (cpp:824) | an `else { const uint32_t current = …; if … }` block | same order |
| `for(key : {weapon_key, native_weapon_key})` (cpp:964) | an array of the two values taken before the loop | the initializer list is also a copy |
| `IMAGE_DOS_HEADER`, `IMAGE_NT_HEADERS64` (cpp:213-219) | byte buffers of the same sizes (64 and 264) with fields at their offsets | the same two reads of the same lengths, same tests in the same order |
| local `seen[3]`, inner `weight`, `basis[9]` shadowing other names | `seen_at`, `anchor_weight`, `rig_basis` | names only |
| direct call of the original setter | `call_original`: direct call when `host.forward` is NULL | An addition, for the tester only: with fake memory, `exe + setter` is not code. The game build leaves `forward` NULL |
| `callback()` = address of `setter_hook` | `host.hook` | the C hook takes the return address as a parameter, so the function that sits in the slot is the game build's wrapper, not this file's |
| `std::exp`, `std::sqrt`, `std::fabs` | by argument type: `expf` at cpp:878, 936, 944, 1007 (float arguments), `exp` at cpp:975 (double), `sqrtf`, `fabsf` | the C++ overload chosen for a float argument is the float function |

`tfp_camera_reset_for_tests` and `tfp_camera_set_host` are not in retail. The reset keeps the host and clears the calling thread's probe pointer only.

## 5. Open

1. **`L_FLAG_FIRE` (flag byte 12, bit 0x40)**, c:57 and c:363, cpp:292. No file here reads that bit on this build. It only sets `Policy.fire`, which nothing reads after the kick was dropped, so a wrong value cannot change an output.
2. **`L_CONTEXT_BASIS` (context+0x0C)**, c:69 and c:486, cpp:422. I did not find this offset in the image. The evidence is the older port's `camera_policy.c:472` reading the camera matrix there, and retail's own notes; `basis()` decides POSE_INVALID and gives `forward`/`up` for the eye, so this one matters. A live read of 11 floats there (orthonormal rows) would settle it.
3. **Scene value**, c:95, c:444, c:463, cpp:377, 401. Retail's helper accepts the bridge state only with scene 3. `action.c` on this build accepts 3 or 4, and `profile.h` comments "scene 3 or 4 on foot". I kept retail's 3. If scene 4 is a real on-foot bridge case on this build, the head camera will refuse there with error 18.
4. **Layout constants in general.** Their evidence is that this repository's 0.5.34 sources (`scene.c`, `action.c`, `backend.c`, `snapshot.c`) read the same offsets, not a fresh disassembly by me. Without any counterpart here: the weapon unit's parent pointer equal to the body object and its link low word 2 (`weapon_body_node`, c:594, cpp:553-558); the bridge role test `(role & 6) == 2` is in `action.c`.
5. **Fixture path versus game path.** The reference build of the retail file is compiled with `NADSC_FIXTURE`, which removes `executable(getter)` from `graph`, compares the slot with `fixture_target` and skips pinning the executable. The C has the game path (section 1). A differential test has to answer `executable`, `writable_slot`, `pin` and the slot value through the host so both agree; the sequence of host calls will not be identical to the fixture's Windows calls, the memory reads will.
6. **Float library functions.** `expf`, `exp`, `sqrtf`, `acosf`, `sinf` come from the C library of whichever build. Natively both sides use glibc. I did not check that zig's C++ `std::exp(float)` and C `expf` resolve to the same MinGW routine; they should. Neither compile command enables FMA, so no contraction; a build with `-march=native` could change low bits.
7. **Hook before `tfp_camera_set_host`.** Calling any function before the host is set dereferences NULL. Retail has no such state; I added no guard.
8. **`_Thread_local` in a DLL built for `x86_64-windows-gnu`.** Compiles; whether zig uses native or emulated TLS for the mod DLL was not looked at.
9. **`B_HEX_SETTER` length.** Retail's string is 48 bytes (the inventory says 47); 48 are checked here, all from the image.
