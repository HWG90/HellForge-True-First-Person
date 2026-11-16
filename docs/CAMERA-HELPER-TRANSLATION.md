# Camera helper: the C translation

`src/camera_helper.c` and `src/camera_helper.h` are `native/first_person/native_ads_camera.cpp` and `native_ads_camera.h` of the retail kit at 0.5.41-smooth.66, in C11, for build 25480438. "cpp:N" is a line of the retail file, "c:N" a line of `src/camera_helper.c`. The arithmetic headers are not translated again: `src/camera_math.h` is used.

Both compile checks pass with no warnings: `cc -std=c11 -Wall -Wextra -Werror` and `zig cc -std=c11 -target x86_64-windows-gnu -Wall -Wextra -Werror`. When this was written nothing had been run; since then the translation is compared with the retail source call by call, see section 6.

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

## 6. Differential test

`tests/diff_camera_helper.cpp` (with `tests/diff_camera_helper_world.h` and `tests/diff_camera_helper_scripts.h`), run by `tests/run-native.sh` when the retail tree, g++ and Linux are present. The retail file is compiled natively over `tests/winshim` with `NADSC_FIXTURE`; `src/camera_helper.c` is compiled as its own C object (`cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror`) and linked in. To run one script: `work/tests/diff_camera_helper "<part of its name>"`; `--list` names them; `TFP_DIFF_REACHED=1` prints what each script reached.

**Result: no difference between the original and the translation was found, and no fix was made to `src/camera_helper.c` or `.h`.** 192 scripts and 61,073 probe runs; 1,395,379 calls compared, 1,133,557 of them hook frames; 148,190,296 reads, writes, swaps, lock steps and forwarded calls matched one for one. About 15 seconds on 16 cores (roughly three minutes of processor time). A soak of 1,000 further random walks (`TFP_DIFF_EXTRA_WALKS=1000`, 3.9 million frames) found no difference either.

### How the two are made comparable

- One logical scene (`Scene`) is written into two sets of real pages: one with retail's addresses and offsets (taken from the retail source), one with this build's (taken from the table in section 2, not from the `#define` block; the values were also checked against `src/profile.h` and `src/native_contract.h`). The structure layout numbers both builds share are written once, as retail has them. Every region has an unreadable page on each side.
- An address in one world is compared with an address in the other by name and offset ("body+0x70", "game:api_root+0", "owner:flags[1]+0").
- The original reads through the stand-in `ReadProcessMemory`; the translation's host `read` makes the same `process_vm_readv` call. Clock: both read `fixture_time`. `readable_span` and the slot's page test are answered for the translation by the original's own functions (`VirtualQuery` over `/proc/self/maps`). The compare-and-swap is real in both, with the original's fixture seams (`fixture_cas_fail_at`, `fixture_foreign_cas_at`, `fixture_mutate_after_cas`) repeated on the translation's own call count.
- The dispatch slot is compared by what it holds: the game's setter (original: the test's function, as `fixture_target`; translation: `exe + setter`), the helper (original: `setter_hook`; translation: `host.hook`), or a raw foreign value.
- The hook is called directly: the original with `_ReturnAddress()` answering `game + CALL + 4` of its world, the translation with that of its own. Forwarding goes to the test's "game setter" in both (`fixture_target`, `host.forward`); it writes the position into the camera node, and after each frame the test stores the rotation found at argument+64 as the camera's world rotation, as the game's call pair does.
- Every script runs in a child process forked from a parent that has built the two worlds and given the translation its host, and has made no other call to either helper, so both start from their start-up state.

### What is compared, after every call

The return value; the full ordered record of what the helper did: every memory read (place, length, complete or failed), every write through `WriteProcessMemory`/`host.write`, every compare-and-swap (what was stored, expected, found), every lock and unlock of the two locks, and the forwarded setter call (camera pointer, unit, whether the pointer forwarded was the game's own argument or the helper's buffer, and the 16 position bytes bit for bit); the 96 bytes of the caller's argument buffer (position, and the rotation the helper may rewrite); the slot; the generation written by `begin`; the error code; then `read_stats`, `handoff_stats` (while a lease is held), `entry_trace` and `anchor_stats`, byte for byte, and the record of those four calls themselves. The translation's locks must be balanced at each return.

### Scripts

| group | scripts |
|---|---|
| hip view at 30, 60, 144 frames a second, head moving and still, plain and with presentation + mode 15; a jerking head (6 cm lag clamp) | 12 |
| the game's own first-person request forwarded untouched, with and without presentation | 2 |
| exits from it: remembered eye, exit 300 ms late, eye further than 1 m, no eye, eye older than the gap, a full reset between eye record and exit, eye hold switched off and on; modes 15, 1, 2; exit under mask 2 and 0 | 42 |
| weapon swap: known weapon (70 ms glide), unknown, empty-handed (80 ms fade), 2.5 s limit, 300 ms tolerance; two frame rates | 10 |
| aim-release anchor: learned and used, not learned, key changed, too far, no body node, 4.5 mm wobble, link one past the last node, 16 ms gap, switched off and on, body/camera/rig as the weapon, distance swept through 0.8 m; modes 15, 4, 7 | 33 |
| exit hold; seated view (180 ms turn); guard byte for one and many frames, vault mask, other masks, not on foot, foreign camera/unit/caller, null argument, odd request rings | 12 |
| gates: head 8 m, root 10 m, non-finite argument, context and body basis, graph and head node, head distance, vertical look, camera local/world, pointers swapped under the armed helper | 2 |
| owner lost on a hip or a native frame (20 causes), then end and arm again | 40 |
| time: gaps over, at and under the limit, backwards, repeated; limits 16, 250, 1000 ms | 3 |
| control calls in right and wrong orders; `set_presentation` again in mid-lease; refused configurations, images, code bytes and worlds (about 120) | 3 |
| dispatch slot: foreign value at begin and at end, swap faulting at begin and at end, value arriving between proof and swap, read-only page, owner changing right after the swap, begin answering 2, pin failing, target not executable, unaligned slot, unwritable generation | 14 |
| ship's bridge; the opt-in eases (`set_handoff` nonzero: entry, rotation, both); a one-frame native view entered before the camera was linked; second avatar row; 21 weapons in a 16-entry memory; after `tfp_camera_reset_for_tests`; the translation-only checks below | 11 |
| seeded random walks, 4,000 frames each, mixing all of the above with the adapter's calls, time steps of every kind and one-frame damage | 8 |
| probes (17 calls: twelve kinds of frame, begin, validate, end, set_presentation, read_stats): for every single read the call makes, one run in which that read fails and heals, one in which it stays unreadable to the end of the call, and one for each of 34 values (request kind, mask, guard, aim, unit generations, camera parent, slot, graph count, head hash, animation state, world table, seat, registry, rig, descriptor ...) changing just before that read | 61,073 runs |

Each script also checks, from the original's state, that it reached what it is for (so many frames replaced, an eye hold, a glide, an anchor frame, ...).

### Checks only the translation makes

The reference is the fixture build of the original, which compiles three production checks out (Open 5). The translation has them; they are answered through the host and asserted separately (script "checks only the translation makes", expected results read off the original's non-fixture lines):

- `executable(graph getter)` in `graph` (cpp:330-332): answered true in every compared call; answered false once at begin and validate (expected and got: 0, error 16) and once in the hook (error 16, the game's own argument forwarded, still armed).
- `pin(exe)` in begin (cpp:1095-1097): answered true; answered false once (expected and got: 0, error 9, generation 0, slot untouched, no generation consumed).
- `writable_slot`'s image test (cpp:440-441): the host checks that the image asked about is the executable, and answers with the original's fixture test (committed, writable, private).
- The slot must hold `exe + setter` (cpp:1039): compared through the slot classes above.

### Not compared, and why

- **A hook call before any begin recorded the game's setter.** The original calls a null pointer there. Such frames are skipped and counted.
- **A read of the caller's own argument that stays unreadable.** Both helpers then write the rotation to that page themselves and fault; only the failure that heals is compared for reads of the argument.
- **Concurrency.** One thread. The order of lock operations relative to reads, the swap and the forwarded call is compared; End waiting for a setter in flight is not exercised.
- **The direct call of the original setter** (`host.forward` NULL, c:759). The fake `exe + setter` is not code.
- **`now()` from the performance counter, `readable_span`'s and `executable`'s own `VirtualQuery` logic.** The fixture clock is used; the two page questions are answered for the translation by the original's functions.
- **The retired exports** (section 3) and the Windows build of the C (MinGW's `expf`/`exp`, Open 6). Natively both sides use glibc, with `-ffp-contract=off`.
- **Whether the constants of section 2 are right for build 25480438.** The test proves the translation reads the places the table names; it cannot prove the table.
- **Internal state with no export**: `eye_glides`, `eye_hold_frames`, `guard_grace_frames`, `ads_learned` are written and never read by any export, in retail too.

### Test-only additions

`tests/winshim/windows.h`: optional observers `shim_on_read`, `shim_on_write`, `shim_on_cas`, `shim_on_lock`; `shim_pin_fails`; the process id asked once (`shim_pid`); a faster `VirtualQuery` line scan. Nothing outside `tests/` changed.

### Mutation check

Done on 2026-11-16 in scratch copies outside the repository: one change at a time in `src/camera_helper.c`, the whole test run against each. **187 mutations, 164 killed, 23 survive.** The first round killed 91 of 123; each survivor was either explained or answered with a new script (the wobbling weapon, the 16 ms gap, the 512-node graph, the leaning and jerking head that makes the float sums differ, the 21 weapons, the guard-grace probes, the second `set_presentation`, ...), and 64 more mutations were added.

Killed, by kind: 20 of 22 timing constants and thresholds (250, 2500, 300, 70, 80, 180, 150, 50 ms; 8 m, 10 m, 6 cm, 0.8 m, 5 mm, ...); 9 of 9 `<`/`<=` changes; 19 of 21 dropped resets; 9 of 14 dropped counter increments; all 11 float/double and reassociation changes; 19 of 21 changes to this build's constants and the layout numbers; 21 of 25 logic changes (among them the policy lock released before the forwarded call, caught by the order of steps); 56 of the 64 added later (begin/end/slot bookkeeping, the owner and graph proofs, the eases, `tfp_camera_reset_for_tests`).

The 23 survivors, none of them a gap the test can close:

| survivor | why it cannot be seen |
|---|---|
| counters `eye_glides`, `eye_hold_frames`, `guard_grace_frames`, `ads_learned` not incremented | no export reads them (retail neither) |
| `L_FLAG_FIRE` 12 -> 11, `L_FLAG_SHOULDER_A` 10 -> 9 | Open 1: they feed `Policy.fire`/`shoulder`, which nothing reads; the 24 flag bytes are read either way |
| `policy_init` without the identity `seated_q`; without the correction's start values | `seated_q` is assigned before every use; the correction's `progress` is never read and its duration is set when it starts |
| anchor weight `age <= 50` -> `< 50` | at exactly 50 ms the other branch gives exp(0) = 1 too |
| native frame without `eye_delta_active = false` | the next normal frame clears it on both of its paths (fresh start, or coming from native) |
| normal frame without the entry ease reset | every later native frame begins the ease anew or finds the gap exceeded |
| entry trace without the "skipped frame" count | frame and row count always move together; the branch is dead |
| `READ_FAILED` although unsafe; `pointer()` not marking unsafe | a proof stops at its first failed read, so transport and unsafe never meet in one proof |
| failed begin keeping `callback_generation` | it is 0 whenever a begin fails |
| begin without priming `display_cache`; diagnostics published for a foreign camera | the cache is overwritten by the first read, the values republished are unchanged: visible only to a second thread |
| owner loss in `remember_native` keeping the correction | the hook's full reset follows at once |
| `anchor_stats` copying `ads` of an unknown weapon | `ads` is zero until it is known |
| `read` refusing 4096 bytes | no read is longer than 264 |
| eye "further than 1 m": `<=` -> `<` | needs a squared distance of exactly 1.0f |
| begin without "the recorded original differs from the target" | needs the executable at two bases in one process |
| the `side` term dropped | `side` is always 0 (section 3); differs only for a target of exactly -0.0 |

Each survivor was also run against 400 extra random walks. That soak refuted one of my explanations ("exit hold without renewing `last_native_tick`" differs when the clock steps back) and the exit-hold script now has that frame; the 23 above survived it.
