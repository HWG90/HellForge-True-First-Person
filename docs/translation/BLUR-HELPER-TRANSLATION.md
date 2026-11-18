# Blur helper: the C translation

`src/blur_helper.c` and `src/blur_helper.h` are `native/first_person/native_ads_blur.cpp` and `native_ads_blur.h` (with `native_memory.h`) of the retail kit at 0.5.41-smooth.66, in C11, for build 25480438. "cpp:N" is a line of the retail file, "c:N" a line of `src/blur_helper.c`.

Both compile checks pass with no warnings: `cc -std=c11 -Wall -Wextra -Werror` and `zig cc -std=c11 -target x86_64-windows-gnu -Wall -Wextra -Werror`. The translation is compared with the retail source call by call, see section 6. The older `src/blur_policy.c` was not used as a guide to behaviour, only as evidence for addresses.

## 1. The host interface (`TfpBlurHost`, set once with `tfp_blur_set_host`)

| member | replaces in the retail file |
|---|---|
| `hook` (a value) | the address of `setter_hook` (`callback()`, cpp:395): what is put in the dispatch slot and recognised there. In the game it is the wrapper that calls `tfp_blur_setter_from` |
| `read(at, out, length)` | `ReadProcessMemory` in `read` (cpp:91), all bytes or false. The bounds on `at` and `length` stay in C (c:147) |
| `readable_span(at, length)` | `readable_span` (cpp:61-78, `VirtualQuery`). Called only after a failed read while an owner proof is open, as retail |
| `writable_slot(at, exe)` | the `VirtualQuery` part of `writable_slot` (cpp:279-283). The 8-byte alignment test stays in C (c:362) |
| `executable(at)` | `executable` (cpp:128-132) |
| `cas(slot, expected, desired, &observed)` | `InterlockedCompareExchangePointer` inside `__try` (cpp:299-302); false = the operation faulted |
| `write(out, data, length)` | `WriteProcessMemory`: the generation (cpp:413) and the stats structure (cpp:489) |
| `pin(address)` | `GetModuleHandleExW(FROM_ADDRESS, PIN)` (cpp:397); called with the address of `tfp_blur_begin` and then with the executable base, in retail's order |
| `lock_exclusive`, `unlock_exclusive` (lock number) | `AcquireSRWLockExclusive`/`ReleaseSRWLockExclusive` on `control_lock` (0) and `policy_lock` (1). Retail takes neither lock shared |
| `forward(original, provider, instance, hash, variant)`, optional | nothing in retail. NULL: the original setter is called directly, as retail does. See section 4 |

The hook's caller address is a parameter: `tfp_blur_setter_from(return_address, provider, instance, hash, variant)` (retail `_ReturnAddress()`, cpp:367).

The fixture seams (`fixture_*`, cpp:39-48 and their uses) are not translated. Where retail has a fixture path and a game path, the game path is translated: `contract` requires the slot to hold `exe + setter` and asks `executable` of the pack, lookup and apply functions (cpp:335-336), `Begin` pins the executable (cpp:448-450), `writable_slot` asks about the executable image (cpp:283). Plain exchange (`InterlockedExchangePointer`) appears only inside a fixture seam (cpp:297), so the host has none.

## 2. Constants

All are in one block at c:10-99. `B_` = differs per build, `L_` = structure layout, the same number in both builds. Evidence: **map** = `tools/build_map.py --retail <kit> --to-here`; **image** = bytes read on 2026-11-16 from `game-pre1020.dll` (game) or `r6_reload_exe_20261017.bin` (exe; a live capture, so its pointers are relocated: image base `0x7ff672110000`); a file name = that file of this repository holds the same value for this build.

| name | retail | here | evidence |
|---|---|---|---|
| `B_GAME_STAMP` | `0x6af322c1` | `0x6ab3b43f` | map; `profile.h`; image PE header |
| `B_GAME_SIZE` | `0x3382000` | `0x4744000` | map; `profile.h`; image PE header |
| `B_EXE_STAMP` | `0x6af2d5e6` | `0x6ab382e4` | map; `profile.h`; image PE header |
| `B_EXE_SIZE` | `0x29fe000` | `0x39e8000` | map; `profile.h`; image PE header |
| `B_EXE_SETTER` | `0x2df740` | `0x310050` | map; `feature_contract.h:29`; image: `[exe+0x27cc638]` = base+0x310050 |
| `B_GAME_CALL` | `0xe9ac4b` | `0x100a80b` | map; `feature_contract.h:28`; image: `mov rcx,r10; call [r11]` there, after `mov r10,[rcx+0xdc8]` and `mov r11,[rcx+0xde8]` |
| `B_EXE_PACK` | `0x2df6b0` | `0x30ffc0` | map; `feature_contract.h:30`; image: `[exe+0x27cc5e0+0x28]` = base+0x30ffc0 |
| `B_EXE_LOOKUP` | `0x66a170` | `0x62adb0` | map; `feature_contract.h:31`; image: `[exe+0x1677dc8+0x38]` = base+0x62adb0 (retail's own comment calls it "EXE62ADB0", cpp:203) |
| `B_EXE_HASH` | `0x2df830` | `0x310140` | map; `feature_contract.h:32`; image: a `jmp` to exe+0x62a0c0 |
| `B_EXE_HASH_BODY` | `0x669480` | `0x62a0c0` | map; `feature_contract.h:33`; image: the target of that `jmp` (in retail 0x2df830 jumps to 0x669480 the same way) |
| `B_EXE_VTABLE` | `0x1715c40` | `0x1677dc8` | map; `blur_policy.c:37`; image: +0x28 and +0x38 hold the apply and lookup functions |
| `B_EXE_APPLY` | `0x2ad330` | `0x2dd5e0` | map; `blur_policy.c:39`; image: `[exe+0x1677dc8+0x28]` = base+0x2dd5e0 |
| `B_EXE_QUERY` | `0x2873ac0` | `0x27cc5e0` | map; `blur_policy.c:41`; image: its +0x28 holds base+0x30ffc0 |
| `B_EXE_TABLE` | `0x2873b18` | `0x27cc638` | map; `blur_policy.c:43`; `BUILD-DATA-25480438.md`; image: holds base+0x310050, then base+0x3100c0 |
| `B_EXE_GETTER` | `0x2df7b0` | `0x3100c0` | map; `blur_policy.c:44`; image: `[exe+0x27cc638+8]` |
| `B_EXE_REGISTRY` | `0x1ab76f8` | `0x1a100f0` | map; `profile.h` FP_REGISTRY |
| `B_GAME_CONTEXT` | `0x2e287d0` | `0x346d560` | map; `profile.h` FP_CAMERA |
| `B_GAME_AVATAR_OWNER` | `0x2ce1ee0` | `0x3326d20` | map; `profile.h` FP_AVATAR |
| `B_GAME_CONTROLLER` | `0x2e381a8` | `0x347ce70` | map; `profile.h` FP_CONTROLLER |
| `B_GAME_PARAM_POOL` | `0x2ce1bf8` | `0x3326a38` | map; `blur_policy.c:61`; `BUILD-DATA-25480438.md` |
| `B_GAME_ROOT` | `0x2ce1618` | `0x3326468` | map; `profile.h` FP_PLAYER; `BUILD-DATA-25480438.md` |
| `B_GAME_API_ROOT` | `0x2ce14c8` | `0x3326308` | map; `profile.h` FP_API_ROOT |
| `B_AVATAR_FLAGS` | `0x53e888` | `0x53e880` | map; `profile.h` FP_AVATAR_FLAGS |
| `B_AVATAR_STRIDE` | `0x1240` | `0x1238` | map; `profile.h` FP_AVATAR_STRIDE |
| `B_HEX_CALL` | `41ff13` | the same | image at game+0x100a80b |
| `B_HEX_SETTER` (46 bytes) | `48895c2408…41ff5238` | the same | image at exe+0x310050 |
| `B_HEX_PACK` (13 bytes) | `418bc048c1e0208bca480bc1c3` | the same | image at exe+0x30ffc0 |
| `B_HEX_LOOKUP` (28 bytes) | `405383b9a8…00000000` | the same | image at exe+0x62adb0 |
| `B_HEX_HASH` | `e94b9c3800` | `e97b9f3100` | image at exe+0x310140; only the jump displacement differs |
| `B_HEX_HASH_BODY` (48 bytes) | `…e89abcfaff…` | `…e85ab7faff…` | image at exe+0x62a0c0; only one call displacement differs |

**30 build-specific constants, none open.**

Layout constants (42, the same number as retail, named in the block with their evidence on each line):

| group | names | evidence |
|---|---|---|
| camera context | `L_CONTEXT_RING` 0x1F8, `L_CONTEXT_IDLE` 0x1F00, `L_CONTEXT_RECORDS` 0x200, `L_CONTEXT_RECORD` 0xE8 | `snapshot.c`, `backend.c` (as the camera helper) |
| avatar owner, controller | `L_AVATAR_COUNT` 0x6C, `L_AVATAR_ROWS` 0x110, `L_FLAG_SEATED` 14, `L_CONTROL_GUARD` 5 | `snapshot.c` |
| unit registry | `L_REGISTRY_BLOCK` 0x88, `L_REGISTRY_CAPACITY` 0x98, `L_REGISTRY_GENERATIONS` 0xA0 | `backend.c`, `probe.c` |
| effect system | `L_ROOT_SYSTEM` 0x18, `L_SYSTEM_PROVIDER` 0xDC8, `L_SYSTEM_QUERY` 0xDD8, `L_SYSTEM_TABLE` 0xDE8, `L_SYSTEM_ENTITIES` 0x348, `L_SYSTEM_ENTITY` 0x30 | `blur_policy.c:35-42, 209`; image: `mov r10,[rcx+0xdc8]`, `mov r11,[rcx+0xde8]` before the call site |
| provider identity | `L_VT_LOOKUP` 0x38, `L_VT_APPLY` 0x28, `L_QUERY_PACK` 0x28, `L_TABLE_GETTER` 8, `L_API_HASH` 0xC0, `L_HASH_API_HASH` 0x28 | `blur_policy.c:38-47`; image (the pointers found at those offsets, above) |
| weapon parameter pool | `L_POOL_BUCKETS` 0x278, `L_POOL_CAPACITY` 0x280, `L_POOL_EMPTY` 0x284, `L_POOL_MULTIPLIER` 0x288, `L_POOL_DESCRIPTORS` 0x290, `L_POOL_ROWS` 0x298 | `blur_policy.c:61-64` (through `fp_map`) |
| component map | `L_PROVIDER_COUNT` 0x90, `L_PROVIDER_CAPACITY` 0x94, `L_PROVIDER_BUCKETS` 0x98, `L_PROVIDER_ACTIVE` 0xA8, `L_PROVIDER_DIVISOR` 0xAC, `L_PROVIDER_ROWS` 0xB8, `L_PROVIDER_ALLOCATED` 0xBC, `L_PROVIDER_IDS` 0xC8, `L_PROVIDER_CLASSES` 0xD0, `L_PROVIDER_CHAINS` 0xD8, `L_PROVIDER_BLOBS` 0xE8 | `blur_policy.c:94-100, 180-183`; image: exe+0x62adb0 tests `[rcx+0xa8]`, loads `[rcx+0x98]`, divides by `[rcx+0xac]`, uses 12-byte buckets with `0xfffffffe` / `0x7fffffff`, loads `[rbx+0xd0]` |
| parameter list | `L_BLOB_COUNT` 4, `L_BLOB_KEYS` 0x10 | `blur_policy.c` |

The four name hashes (`DOF_CLASS` `0xF7A3101F`; `0xA7990477`, `0xF07FC12E`, `0x209F72CF`) are hashes of names, not build data; `blur_policy.c:22-23` has the same four on this build. I could not check them against the images (they live in resource data, not in code).

Numbers that are retail's policy, not build data (the limits 8, 32, 512, 4096, 65536, 1048576, 0x400000, the bucket markers) are left as literals where retail has them.

## 3. Dropped

Nothing of retail's behaviour is dropped: the file has no retired exports or unreachable features. Not translated:

| not translated | where | why nothing observable changes |
|---|---|---|
| the `NADSB_FIXTURE` seams (`fixture_*`) | cpp:39-48, 64-66, 83-89, 92-97, 275-278, 284-286, 289-291, 295-298, 332-334, 465-467 | compiled out of the game build |
| `wine_section_mapping`, `pe_section_mapping`, `writable_pe_protection` | `native_memory.h` | they are the inside of `executable` and `writable_slot`, which are the host's now. The host must keep their rule: under Wine/Proton a mapped PE section is `MEM_MAPPED` and writable data is `PAGE_WRITECOPY` |
| `Policy.last_applied` as a computed value | cpp:361, 387 | kept: it is always written 0 and reported, as retail |

## 4. Where C needed another construct

| retail | C | why the same |
|---|---|---|
| `struct Unlock` guard for `control_lock` in the four exports | the body is a `*_locked` function; the export locks, calls it, unlocks (c:512-588) | every return path releases at the same point |
| `struct Unlock` guard for `policy_lock` in the hook (cpp:370) | one `policy_release()` after the forwarded call (c:475); the only earlier `return` is before the lock | the hook has no other exit |
| `OwnerReadProbe` constructor/destructor (cpp:54-60) | written out in `owner_status` (c:229): push, run `owner`, compute the result, pop | `owner_status` is the only user and has one exit |
| `thread_local` | `_Thread_local` | |
| `std::atomic<Setter> original` | `_Atomic uintptr_t`, cast to the function type at the call | same load/store orders |
| `Policy next{}`, `Source s{}`, `Source current[2]{}`, `NADSB_Config c{}`, `NADSB_Stats s{}` | `memset` 0 | all members start at zero in retail |
| `template value<T>` | macro `value(at, out)` = `read(at, &out, sizeof out)` | |
| default argument `request_matches(p, allow_normal=false)` | passed explicitly | |
| `IMAGE_DOS_HEADER`, `IMAGE_NT_HEADERS64` (cpp:114-118) | byte buffers of the same sizes (64 and 264) with fields at their offsets | the same two reads of the same lengths, same tests in the same order |
| the digit lambda in `bytes` | `hex_digit` | |
| `std::isfinite(float)` | `isfinite` | type-generic macro on a float |
| `alignas(16)` | `_Alignas(16)` | |
| direct call of the original setter | `call_original` (c:426): direct call when `host.forward` is NULL | An addition, for the tester only: with fake memory, `exe + setter` is not code. The game build leaves `forward` NULL |
| `callback()` = address of `setter_hook` | `host.hook` | the C hook takes the return address as a parameter, so the function that sits in the slot is the game build's wrapper |
| `reject(capture?SOURCE_INVALID:UNIT_MISMATCH)` in branches where `capture` is already known (cpp:249, 252, 263-264, 270) | written the same way | kept literally |

`tfp_blur_reset_for_tests` and `tfp_blur_set_host` are not in retail. The reset keeps the host and clears the calling thread's probe pointer only.

Retail oddities kept as they are (each is compared in section 6): a begin whose second generation write fails returns 0 with error 1 after the generation was consumed and the original setter recorded (cpp:453); in `apply_immediately` a failed `source` sets error 8 but begin then reports 7 (cpp:355-357, 470); `ReadStats` returns 0 without an error code when its final write fails (cpp:489); validate may be answered with kind 4, begin never.

## 5. Open

1. **Fixture path versus game path.** The reference build of the retail file is compiled with `NADSB_FIXTURE`, which removes the three `executable` questions about the engine functions from `contract`, compares the slot with `fixture_target` instead of `exe + setter`, and skips pinning the executable. The C has the game path (section 1). The differential test answers `executable`, `writable_slot`, `pin` and the slot value through the host so both agree; these are asserted separately (section 6, "Checks only the translation makes").
2. **Name hashes** (section 2): taken as build-independent on the evidence of `blur_policy.c`, not checked in the game.
3. **Layout constants in general.** Their evidence is that this repository's 0.5.34 source (`blur_policy.c`, written on this build) reads the same offsets and that the migration records do not list them as moved; a few are also visible in this build's code (marked "image" above). The pool offsets 0x278-0x298 and the blob layout have no image evidence from me.
4. **Hook before `tfp_blur_set_host`.** Calling any function before the host is set dereferences NULL. Retail has no such state; I added no guard.
5. **`_Thread_local` in a DLL built for `x86_64-windows-gnu`.** Compiles; whether zig uses native or emulated TLS for the mod DLL was not looked at.
6. **The wrapper.** `_ReturnAddress()` in retail is the return address of the game's `call [r11]`, `game + B_GAME_CALL + 3`. The game build's wrapper must pass exactly that (its own return address), and must be what `host.hook` names.
7. **The host's `writable_slot` and `executable`** must reproduce `native_memory.h` (Wine: `MEM_MAPPED` sections, `PAGE_WRITECOPY`); that code is outside this file and not tested here.

## 6. Differential test

`tests/diff_blur_helper.cpp` (with `tests/diff_blur_helper_world.h` and `tests/diff_blur_helper_scripts.h`), built and run by `tests/run-diff-blur.sh` when the retail tree, g++ and Linux are present (otherwise it prints a "skipped" line and exits 0). The retail file is compiled natively over `tests/winshim` with `NADSB_FIXTURE`; `src/blur_helper.c` is compiled as its own C object (`cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror`) and linked in. To run some scripts: `tests/run-diff-blur.sh "<part of the name>"`; `--list` names them; `TFP_DIFF_EXTRA_WALKS=n` adds random walks. No shim file was added or changed.

**Result: no difference between the original and the translation was found, and no fix was made to `src/blur_helper.c` or `.h` after the first compile.** 245 scripts and 525,791 probe runs; 4,094,249 calls compared, 1,831,975 of them hook frames (839,389 replaced a value); 897,776,582 reads, writes, swaps, lock steps and forwarded calls matched one for one. Two to three and a half minutes on 16 cores, depending on what else runs.

### How the two are made comparable

- One logical state (the retail fixture's: weapon 0x444, entities 107 and 108, one or two optic components) is written into two sets of real pages: one with retail's addresses (taken from the retail source), one with this build's (taken from the table in section 2, typed again in `diff_blur_helper_world.h`, not included from the `#define` block). The layout numbers both builds share are written once, as retail and its fixture have them. Every region has an unreadable page on each side.
- An address in one world is compared with an address in the other by name and offset ("provider+0x98", "exe:table+0", "owner:flags[1]+0").
- The original reads through the stand-in `ReadProcessMemory`; the translation's host `read` makes the same `process_vm_readv` call. `readable_span` and the slot's page test are answered for the translation by the original's own functions (`VirtualQuery` over `/proc/self/maps`). The compare-and-swap is real in both, with the original's fixture seams (`fixture_cas_fail_at`, `fixture_foreign_cas_at`) repeated on the translation's own call count; the two counts are compared after every call.
- The dispatch slot is compared by what it holds: the game's setter (original: the test's function, as `fixture_target`; translation: `exe + setter`), the helper (original: `setter_hook`; translation: `host.hook`), or a raw foreign value.
- The hook is called directly: the original with `_ReturnAddress()` answering `game + CALL + 3` of its world, the translation with that of its own. Forwarding goes to the test's "game setter" in both (`fixture_target`, `host.forward`).
- Every script runs in a child process forked from a parent that has built the two worlds and given the translation its host, and has made no other call to either helper.

### What is compared, after every call

The return value; the full ordered record of what the helper did: every memory read (place, length, complete or failed), every write through `WriteProcessMemory`/`host.write`, every compare-and-swap (what was stored, expected, found), every lock and unlock of the two locks, and every forwarded setter call (provider by name, the packed instance, the hash, whether the variant pointer was the game's own, the helper's buffer, null or another place, and the 24 variant bytes bit for bit); 64 bytes of the caller's variant buffer; the slot; the generation written by `begin`; the number of swap attempts; the error code; then `read_stats` with the right generation, 256 bytes around the structure, and the record of that call itself. The translation's locks must be balanced at each return. A complete read outside every region of a world is a fault of the test and stops it.

### Scripts

| group | scripts |
|---|---|
| the story of the retail fixture: validate with the normal request, begin refused on it, begin with kind 6 applying three zeros, each of the three hashes replaced, nine other hashes, five wrong callers, three wrong providers, seven wrong instances, end, frames after end | 1 |
| hip, shoulder, aim replaced for both components; nine masks, mask 2, guard, seat and the other bits of its byte; the same gates at validate and begin | 1 |
| variants: 7 type tags x 15 float patterns (NaNs, infinities, denormals, -0); other bytes of the variant kept; a copy elsewhere, null, an unreadable pointer; the variant page unreadable once and for the whole call | 1 |
| avatar rows 1, 3, 7 with their own flag bytes; row beyond the count; another body unit and generation; registry capacity at the unit's index | 1 |
| refused configurations (size, abi, reserved, row, avatar, body, weapon, small and shifted pointers, shifted images, null and unreadable config, null and unwritable generation); the idle record; all 32 ring positions | 1 |
| control calls in right and wrong orders: tokens 0, stale, retired, future; busy; stats headers and pointers, a stats page that can be read but not written; generations counting on, also over refused begins | 1 |
| the slot: read-only at begin and at end, a foreign value arriving at the swap, the swap faulting at begin and at end, begin answering 2 and end retried, a foreign value left alone at end, the game's setter back under the helper, pin failing, target not executable, helper or foreign value before begin, a really read-only page, the slot unreadable at each of its reads | 11 |
| changes arriving while the game's own setter runs: nine kinds at four of the immediate applies; an owner, source, slot or request read failing right after it; in a frame | 1 |
| reads failing at 13 owner places (error 3 when the page heals, 7 when it stays) and 37 other places (never 3), at validate, begin and in a frame; the second and third read of a place; a span crossing into an unreadable page | 1 |
| parameter lists: the fixture's 22 keys, 512, 513, 3, 2, 1 keys, wrong types, duplicates, missing keys; nine ways to have no source (error 18); two components, the same one twice, one list for two, fewer or more components under an armed helper | 1 |
| pool probing (wrap, empty bucket, full table, the 4096th and 4097th probe, 32-bit product, descriptor 65535), the component map (own bucket, long chain, largest map, chain against the row count), image headers at 64, 200, 4096 | 1 |
| one change of the game's memory, 205 of them (owner, request, provider identity, slot, pool, component map, parameter list, image headers, code bytes first and last, variant): before validate and begin, under an armed helper for three frames, healed, and between two frames with both components | 205 |
| the same content at another address (units, generations, registry, descriptor, controller, parameter rows, descriptor array, descriptor, provider object): each captured address is part of the proof; ring positions 31 and 32 | 1 |
| after `tfp_blur_reset_for_tests`; the translation-only checks below | 2 |
| seeded random walks, 3,000 steps each, mixing frames of every kind, the 205 changes, healing, control calls with right and wrong tokens, reads failing by place and by number, changes during forwarded calls, the swap seams and pin failures | 16 |
| probes (11 calls: validate with either request, begin with one and two components, a replaced frame, end, read_stats, a begin rolled back, a frame with an unsafe mask, end over a foreign slot value, a frame of another parameter): for every single read the call makes, one run in which that read fails and heals, one in which it stays unreadable to the end of the call, and one for each of the 205 changes arriving just before that read; then frames, healing, end and a full healthy round | 525,791 runs |

Scripts check from the original's state that they reached what they are for (so many replaced, armed or not, the expected error).

### Checks only the translation makes

The reference is the fixture build of the original, which compiles three production checks out (Open 1). The translation has them; they are answered through the host and asserted separately (script "checks only the translation makes", expected results read off the original's non-fixture lines):

- `executable` of the pack, lookup and apply functions in `contract` (cpp:335-336): answered true in every compared call; each answered false once at validate and at begin (expected and got: 0, error 6, generation 0, slot untouched).
- `pin(exe)` in begin (cpp:448-450): answered true; answered false once (expected and got: 0, error 9, generation 0, slot untouched). The next compared begin gets generation 1 in both, so nothing was consumed.
- `writable_slot`'s image test (cpp:283): the host checks that the image asked about is the executable, and answers with the original's fixture test (committed, writable, private).
- The slot must hold `exe + setter` (cpp:335): compared through the slot classes above.

### Not compared, and why

- **A hook call before any begin recorded the game's setter.** The original calls a null pointer there. Such frames are skipped and counted (324,404 in the full run).
- **Concurrency.** One thread. The order of lock operations relative to reads, the swap and the forwarded call is compared; End waiting for a setter in flight, and End serialised behind Begin (the fixture's two threaded cases), are not exercised.
- **The direct call of the original setter** (`host.forward` NULL, c:429). The fake `exe + setter` is not code.
- **`readable_span`'s, `executable`'s and `writable_slot`'s own `VirtualQuery` logic, and `native_memory.h`.** They are the host's; the test answers them for the translation with the original's functions.
- **The Windows build of the C.** Compiled, not run. The file has no floating-point arithmetic (one `isfinite`, one stored 0), so no library or contraction question arises.
- **Whether the constants of section 2 are right for build 25480438.** The test proves the translation reads the places the table names; it cannot prove the table.
- **An unaligned slot** (`(at & 7) != 0`, c:362): the slot is `exe + table`, and a shifted `exe` fails the image check first.
- **"The recorded original differs from the target"** (cpp:446): needs the executable at two bases in one process.
- **The generation counter wrapping to 0** (cpp:452).

### Mutation check

Done on 2026-11-16 in scratch copies outside the repository: one change at a time in `src/blur_helper.c`, the whole test run against each. **196 mutations, 183 killed, 13 survive.** In the first round 18 survived. One of those was a mutation of mine that changed nothing, and five others did not compile; those six were written again and are killed. Four survivors were real gaps and were closed with the script "the same content at another address; ring position 32" (the units table, the parameter rows and the provider object copied elsewhere: each captured address is part of the proof; ring tail 32). A fifth gap showed in the last five mutations and was closed with a statistics pointer that can be read but not written.

Killed: all 27 changes to this build's constants and code bytes (among them each of the six retail values put back where the two builds differ in a small way, and two byte strings shortened by one byte); all 13 changed layout numbers and name hashes; 143 of the 156 changes to the logic (comparisons and bounds moved by one, dropped proofs and comparisons, dropped resets, stores and counters, changed error numbers and return values, changed order of checks, the policy lock released before the forwarded call or dropped or held too long, and four changes to `tfp_blur_reset_for_tests`).

The 13 survivors, none of them a gap the test can close:

| survivor | why it cannot be seen |
|---|---|
| `read` refusing address 65536 itself (`at < 65537`) | needs a page mapped at 0x10000 in both worlds at once |
| `read` not marking a bounds failure unsafe; `pointer` not marking an invalid value unsafe; `owner_status` ignoring `unsafe` | a proof stops at its first failed read, and only a failed host read sets `transport`, so `transport` and `unsafe` never meet in one proof (the camera helper has the same three) |
| `pointer` accepting values at or above 0x800000000000; accepting 65535 | such a value is then either compared with a captured address (different) or read (refused by `read`'s own bounds): the same result |
| `owner_status` not restoring the previous probe pointer | the pointer is left on a dead stack frame; nothing reads it before the next proof replaces it. Undefined behaviour without an observable result here |
| `fields`: the field places not compared with the captured ones | with the same count and the same keys in the same order, the places are the same numbers |
| `writable_slot` without the alignment test | the slot is `exe + table`; a shifted `exe` fails the image check first (see "Not compared") |
| `restore_slot` leaving `possible` set | `begin` clears it before its next use; `end` without a lease returns before `restore_slot` |
| `restore_slot` keeping `owned_generation` | read only while a lease is reserved, and `begin` sets it first |
| `failed_begin` not clearing `armed` | `armed` is set only at the end of a successful begin, and a begin cannot start while a lease is held |
| begin without "the recorded original differs from the target" | needs the executable at two bases in one process |
