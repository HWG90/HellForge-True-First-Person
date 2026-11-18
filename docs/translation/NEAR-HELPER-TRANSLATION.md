# Near-clip helper: the C translation

`src/near_helper.c` and `src/near_helper.h` are `native/near_clip/native_ads_near.cpp` (345 lines) and `native_ads_near.h` of the retail kit at 0.5.41-smooth.66, in C11, for build 25480438. "cpp:N" is a line of the retail file. Every function in the C file carries its retail line range in a comment.

Both compile checks pass with no warnings: `cc -std=c11 -Wall -Wextra -Werror` and `zig cc -std=c11 -target x86_64-windows-gnu -Wall -Wextra -Werror`. The translation is compared with the retail source call by call, see section 6.

## 1. The host interface (`TfpNearHost`, set once with `tfp_near_set_host`)

| member | replaces in the retail file |
|---|---|
| `hook` (a value) | the address of `setter_hook` (`callback()`, cpp:235): what is put in the dispatch slot and recognised there. In the game it is the wrapper that calls `tfp_near_setter_from` |
| `read(at, out, length)` | `ReadProcessMemory` in `read` (cpp:66), all bytes or false. The bounds on `at` and `length` (cpp:65) stay in C |
| `now_us()` | `now_us()` (cpp:100-110): the fixture clock in the reference build, `QueryPerformanceCounter` scaled to microseconds in the game build (0 when the counter cannot be read). The scaling expression `(t/f)*1000000 + (t%f)*1000000/f` is the host's to keep |
| `writable_slot(at, exe)` | the `VirtualQuery` part of `writable_slot` (cpp:148-155). The 8-byte alignment test (cpp:143) stays in C |
| `executable(at)` | `executable` (cpp:88-92) |
| `cas(slot, expected, desired, &observed)` | `InterlockedCompareExchangePointer` inside `__try` (cpp:162-165); false = the operation faulted |
| `write(out, data, length)` | `WriteProcessMemory`: the generation (cpp:253), a caller-table row (cpp:329), the statistics (cpp:343) |
| `pin(address)` | `GetModuleHandleExW(FROM_ADDRESS, PIN)` (cpp:237); called with the address of `tfp_near_begin` and with the executable base, in retail's order (cpp:280-283) |
| `lock_exclusive`, `unlock_exclusive`, `lock_shared`, `unlock_shared` (lock number) | `AcquireSRWLock*`/`ReleaseSRWLock*` on `control_lock` (0) and `policy_lock` (1) |
| `forward(original, camera, meters)`, optional | nothing in retail. NULL: the original setter is called directly, as retail does (cpp:233, 313). For the tester only: with fake memory `exe + setter` is not code |

The hook's caller address is a parameter: `tfp_near_setter_from(return_address, camera, native)` (retail `_ReturnAddress()`, cpp:198).

The fixture seams (`fixture_*`, cpp:57-63 and their uses) are not translated. Where retail has a fixture path and a game path, the game path is translated: `contract` requires the slot to hold `exe + setter` and asks `executable(getter)` (cpp:185), `Begin` pins the executable (cpp:281-283), `writable_slot` asks about the executable image (cpp:151-152). Plain exchange (`InterlockedExchangePointer`) appears only inside a fixture seam (cpp:160), so the host has none.

Exports: `tfp_near_begin`, `tfp_near_set`, `tfp_near_end`, `tfp_near_read_stats`, `tfp_near_error`, `tfp_near_read_sites` = `NADSN_Begin`, `_Set`, `_End`, `_ReadStats`, `_Error`, `_ReadSites`, same signatures and results. Structures `TfpNearConfig` 72, `TfpNearStats` 88, `TfpNearSite` 32 bytes, asserted. Error numbers unchanged (0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 17; 3 is defined and never set, in retail too). `tfp_near_set_host` and `tfp_near_reset_for_tests` are not in retail; the reset keeps the host.

## 2. Constants

All are in one block at the top of `src/near_helper.c`. `B_` = differs per build (or is tied to this build's bytes), `L_` = structure layout, the same number in both builds. Evidence: **map** = `tools/build_map.py --to-here`; **image** = bytes read on 2026-11-16 from `game-pre1020.dll` / `r6_reload_exe_20261017.bin` (file offset = RVA); a file name = that file of this repository holds the same value for this build.

| name | retail | here | evidence |
|---|---|---|---|
| `B_GAME_STAMP` | `0x6af322c1` | `0x6ab3b43f` | map; `profile.h`; image PE header |
| `B_GAME_SIZE` | `0x3382000` | `0x4744000` | map; `profile.h`; image PE header |
| `B_EXE_STAMP` | `0x6af2d5e6` | `0x6ab382e4` | map; `profile.h`; image PE header |
| `B_EXE_SIZE` | `0x29fe000` | `0x39e8000` | map; `profile.h`; image PE header |
| `B_EXE_SETTER` | `0x2dd050` | `0x30d960` | `native_contract.h` camera_near_setter; image: the executable stores this address into its API table at +0x68 (`lea` at exe+0x1cd0b6, table base exe+0x27c9820, where the position setter exe+0x30deb0 sits at +0x20) |
| `B_EXE_GETTER` | `0x2dd030` | `0x30d940` | `native_contract.h` camera_near_getter; image: stored into the same table at +0x58 (`lea` at exe+0x1cd09a) |
| `B_GAME_SITE` | `0x110ddec` | `0x127bf2c` | `native_contract.h` camera_near_setup; image |
| `B_GAME_NATIVE_NEAR` | `0x2245efc` | `0x23c66d4` | image: the `movss xmm1,[rip+…]` at site+7 resolves to it, and it holds `cd cc 4c 3d` |
| `B_GAME_TEXT` | `0x1000` | `0x1000` | image section table: the first section starts there |
| `B_GAME_TEXT_END` | `0x1f99000` | `0x2111000` | retail's value is the start of its second section (a 26251122 image with retail's image size has first section 0x1000 + 0x1f98000); here the first section is 0x1000 + 0x210fa93 and the second starts at 0x2111000 |
| `B_HEX_SITE` (34 bytes) | `488b05d536bd01f30f100d01811301…ff5268` | `488b05d5a30a02f30f100d99a71401…ff5268` | image at game+0x127bf2c; only the two rip displacements differ; the first resolves to game+0x3326308 = `B_GAME_API_ROOT` |
| `B_HEX_NATIVE_NEAR` | `cdcc4c3d` | the same | image at game+0x23c66d4 |
| `B_HEX_SETTER` | `f30f114928e9c66b1f00` | `f30f114928e9a6201c00` | image at exe+0x30d960; only the jump displacement differs |
| `B_HEX_GETTER` | `f30f104128c3` | the same | image at exe+0x30d940 |
| `B_NATIVE_BITS` | `0x3d4ccccd` | the same | the float those four bytes are |
| `B_GAME_CONTEXT` | `0x2e287d0` | `0x346d560` | map; `profile.h` FP_CAMERA |
| `B_GAME_AVATAR_OWNER` | `0x2ce1ee0` | `0x3326d20` | map; `profile.h` FP_AVATAR |
| `B_GAME_CONTROLLER` | `0x2e381a8` | `0x347ce70` | map; `profile.h` FP_CONTROLLER |
| `B_GAME_API_ROOT` | `0x2ce14c8` | `0x3326308` | map; `profile.h` FP_API_ROOT; image (site bytes) |
| `B_AVATAR_FLAGS` | `0x53e888` | `0x53e880` | map; `profile.h` FP_AVATAR_FLAGS |
| `B_AVATAR_STRIDE` | `0x1240` | `0x1238` | map; `profile.h` FP_AVATAR_STRIDE |

21 build constants, none open. The map has no record for the four near-clip addresses ("added on 26568081"); they come from `native_contract.h` and were confirmed in the images as described.

Layout constants (14, retail's numbers):

| names | evidence |
|---|---|
| `L_API_CAMERA` 0x20, `L_API_NEAR_SETTER` 0x68 | image: `mov rdx,[rax+0x20]`, `call [rdx+0x68]` in the site bytes |
| `L_API_NEAR_GETTER` 0x58 | image: the executable's table stores (above) |
| `L_CAMERA_NEAR` 0x28 | image: setter `movss [rcx+0x28],xmm1`, getter `movss xmm0,[rcx+0x28]` |
| `L_CONTEXT_CAMERA` 0, `L_CONTEXT_HEAD` 0x1F8, `L_CONTEXT_TAIL` 0x1FC, `L_CONTEXT_RECORDS` 0x200, `L_CONTEXT_RECORD` 0xE8, `L_CONTEXT_IDLE` 0x1F00 | `snapshot.c`, `camera_helper.c` read the same fields on this build |
| `L_AVATAR_COUNT` 0x6C, `L_CONTROL_AIM` 4, `L_FLAG_SHOULDER_A` 10 (bit 4), `L_FLAG_SHOULDER_B` 15 (bit 32) | `snapshot.c` |

Retail's policy numbers (0.05 m, 1.0 m, 250 000 / 50 000 / 45 000 µs, 0.002 m, 16 rows, 8 avatar rows, 32 ring entries, request kind 6, the 1024-byte read limit) are unchanged.

## 3. Dropped

Nothing of the helper's behaviour. Not translated: the fixture seams (section 1), and `native_memory.h` (`pe_section_mapping`, `writable_pe_protection`), which is the body of the host's `writable_slot` and `executable`. `site_overflow` is kept although no export reads it.

## 4. Where C needed another construct

| retail | C | why the same |
|---|---|---|
| `struct Unlock` guard for `control_lock` in four exports | the body is a `*_locked` function; the export locks, calls it, unlocks | every return path releases at the same point |
| `std::atomic<Setter> original` | `_Atomic uintptr_t`, cast to the function type at the call | same load/store orders |
| `std::atomic<float>` (`hip_meters`, `aim_meters`, `last_native`, `last_applied`, `shown_meters`, a row's `value`) with the always-lock-free assert | the float's bit pattern in `_Atomic uint32_t`, `ATOMIC_INT_LOCK_FREE == 2` asserted | the float read back is bit for bit the float stored |
| `template value<T>` | macro `value(at, out)` | |
| `reject(code) != 0` as a `bool` result | `{ reject(code); return false; }` | `reject` always returns 0 |
| `IMAGE_DOS_HEADER`, `IMAGE_NT_HEADERS64` | byte buffers of the same sizes (64 and 264) with the fields at their offsets | same two reads, same tests in the same order |
| `enum class View` | `enum View` with `VIEW_` names | |
| `Policy{}` default initialisers (all zero) | `memset` | |
| `std::exp(float)`, `std::fabs(float)`, `std::isfinite` | `expf`, `fabsf`, `isfinite` | the C++ overload chosen for a float argument is the float function |
| direct call of the original setter | `call_original`: direct when `host.forward` is NULL | section 1 |

## 5. Open

1. **Fixture path versus game path.** The reference build of the retail file is compiled with `NADSN_FIXTURE`, which compares the slot with `fixture_target`, leaves out `executable(getter)` and does not pin the executable. The C has the game path. The test answers these through the host and asserts the two extra checks separately (section 6).
2. **`B_GAME_TEXT_END`.** The value follows retail's meaning (end of game.dll's first section). Both builds also have executable `.winlice`/`.boot` sections further up; retail does not count them, neither does this.
3. **Layout constants of the camera context, avatar owner and controller** rest on this repository's 0.5.34 sources reading the same offsets on this build, not on a fresh disassembly by me.
4. **Float library.** `expf` comes from the C library of whichever build. Natively both sides use glibc with `-ffp-contract=off`. Whether zig's C++ `std::exp(float)` and C `expf` are the same MinGW routine was not checked; a build with FMA contraction could change low bits of the ease.
5. **Hook before `tfp_near_set_host`** dereferences NULL; **hook before any begin** calls a null original, as retail does.
6. **The game-side wrapper** (the function whose address is `host.hook`, passing its return address) is not part of this file.

## 6. Differential test

`tests/diff_near_helper.cpp` (with `tests/diff_near_helper_world.h` and `tests/diff_near_helper_scripts.h`), built and run by `tests/run-diff-near.sh` (one summary line; non-zero on any difference; "skipped" and 0 without the retail tree, g++ or Linux). The retail file is compiled natively over `tests/winshim` with `NADSN_FIXTURE`; `src/near_helper.c` is its own C object (`cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror`). One script: `tests/run-diff-near.sh "<part of its name>"`; `--list` names them; `TFP_DIFF_REACHED=1` prints what each reached; `TFP_DIFF_EXTRA_WALKS=n` adds random walks.

**Result: no difference between the original and the translation was found, and no fix was made to `src/near_helper.c` or `.h` after the first compile.** 139 scripts and 6,172 probe runs; about 248,000 calls compared, 184,000 of them hook calls; 7.7 million reads, writes, swaps, lock steps and forwarded calls matched one for one; under two seconds. A soak with 2,000 further random walks (`TFP_DIFF_EXTRA_WALKS=2000`: 13.3 million calls compared, 499 million steps matched) found no difference either.

Differences the test reported while it was being written were all faults of the test (script expectations, and one seam that reset the "slot read-only" answer for the translation only); none was in the translation.

### How the two are made comparable

- One logical state (`Scene` plus pokes) is written into two sets of real pages: one with retail's addresses and offsets (from the retail source), one with this build's (from the table in section 2, not from the `#define` block). Every region has an unreadable page on each side. Each world has a second context, camera, avatar owner, controller, API root, API table and executable image for "something else is there now" cases.
- An address in one world is compared with an address in the other by name and offset ("game:context+0", "owner:flags[1]+0"). Callers (return addresses) are compared by logical number: the pinned site, the first and last byte of the game's code, one byte before and after it, the image base, a foreign address, and 40 further sites.
- The original reads through the stand-in `ReadProcessMemory`; the translation's host `read` makes the same `process_vm_readv` call. Clock: both read `fixture_now_us`. The slot's page test is answered for the translation by the original's own `writable_slot`. The compare-and-swap is real in both, with the original's seams (`fixture_cas_fail_at`, `fixture_foreign_cas_at`) repeated on the translation's own call count.
- The dispatch slot is compared by what it holds: the game's setter, the helper, another image's setter, or a raw value.
- The hook is called directly, with the same logical caller. Forwarding goes to the test's "game setter" in both; it writes the distance to camera+0x28 as the game's does, and reads the caller table at that moment so the order of "note the caller" and "forward" is compared.
- Every script runs in a child process forked from a parent that has only built the two worlds.

### What is compared, after every call

The return value; the ordered record of every memory read (place, length, complete or failed), every write, every compare-and-swap (stored, expected, found), every lock and unlock of the two locks, and every forwarded setter call (camera, distance bit for bit, caller table as seen then); both dispatch slots; the near distance of both cameras; the generation page; the error code; then `read_stats` (88 bytes) and `read_sites` (16 rows), byte for byte. The translation's locks must be balanced at each return.

### Scripts

| group | scripts |
|---|---|
| hip distance at 30, 60, 144, 240 frames a second, with live changes up, down, to the game's own value, to 1.0 and to 0.0501 | 4 |
| hip, normal aim (both flags, lone flags, other bits set), full sights and their transitions; aim 0, aim beyond hip; four frame rates | 4 |
| a sight's distance (0.2214, 0.3521, 0.115): pulled back outside full sights, kept in full sights, zoom change, gliding both ways, hip distance winning; four frame rates | 4 |
| 25 values (0.05 and its two neighbours, sight values, 1.0 and its neighbour, above 1, below 0.05, zero, minus zero, negatives, NaNs, infinities, denormals) on 7 views | 7 |
| the ease against time: 23 step sizes from 1 µs to 1 s including 49 999 / 50 000 / 50 001, 249 999 / 250 000 / 250 001, zero and negative steps; the clock reading 0, small values, wrapping past zero; hip 0.15, 1.0 and 0.052 | 3 |
| other callers (code-range boundaries) and other cameras passed through; more callers than the table holds; the table read with capacities 0 to 2^32-1, into null, low and read-only pointers | 1 |
| requests: every ring head/tail combination, idle record, kind words with high bits, controller pointer bad, moved, or running off its page | 1 |
| owner lost under the armed helper: 12 causes on 4 views, then set, stats, busy begin, end, arm again | 48 |
| control calls in right and wrong orders, wrong, zero and retired generations, bounds of `set`, statistics headers and outputs | 1 |
| refused configurations (26), owner proofs (17), image headers (16), code bytes (15), API table (11), slot holding something else (4), slot not writable (answered, really read-only, unaligned), setter not executable, pin failing; headers at the two `e_lfanew` limits; a lease on another API table; a second executable after the first was recorded, and the reverse | 2 |
| dispatch slot: foreign value at end, slot restored behind the helper, swap faulting at begin and at end, racer between proof and swap, slot unreadable or read-only at end, table / owner / root / slot changing right after the swap, begin answering 2 (two ways), generation page turning read-only between the two writes, slot turning read-only between proof and swap | 17 |
| the distance put back at end: 16 cases (ours still there, overwritten, context or camera moved, mid-ease, sight pulled back, sight above, after another request, after a lost owner, NaN on the camera) and the camera unreadable | 17 |
| avatar rows 0, 1, 3, 7 | 4 |
| checks only the translation makes; after `tfp_near_reset_for_tests` | 2 |
| seeded random walks, 8,000 steps each, mixing all of the above with one-step damage, clock jumps and the test's seams | 24 |
| probes (12 calls: two begins, three ends, five kinds of frame, a frame on row 1, read_stats): for every read the call makes, one run in which it fails and heals, one in which it stays unreadable to the end of the call, and one for each of 42 values changing just before that read | 6,172 runs |

Each script also checks, from the original's state, that it reached what it is for.

### Checks only the translation makes

Answered through the host and asserted separately (expected results read off the original's non-fixture lines): `executable(getter)` false once (0, error 6, generation 0, slot untouched); `pin(exe)` false once (0, error 9, generation 0, slot untouched, no generation consumed); `writable_slot`'s image must be the executable; the slot must hold `exe + setter`.

### Not compared, and why

- **A hook call before any begin recorded the game's setter**: the original calls a null pointer. Skipped and counted (1,066).
- **Concurrency.** One thread. The order of lock steps relative to reads, the swap and the forwarded call is compared; End waiting for a hook call in flight, and two callers racing for one caller-table row, are not exercised.
- **The direct call of the original setter** (`host.forward` NULL) and **`now_us` from the performance counter**.
- **The Windows build of the C** (MinGW's `expf`).
- **Whether the constants of section 2 are right for build 25480438**: the test proves the translation reads the places the table names, not the table.
- **`site_overflow`**: no export reads it, in retail too.
- **Generation counter wrapping** past 2^64.

### Mutation check

Done on 2026-11-16 in scratch copies outside the repository (`/tmp/claude-1000/near-diff/mutate.py`): one change at a time in `src/near_helper.c`, the whole test against each. **231 mutations, 203 killed, 28 survive.** The first round killed 172 of 204 that built; seven survivors were real gaps and were closed with new scripts or seams (a frame at clock zero followed by one within the gap; pointers below 64K for configuration and outputs; a camera at the top of user space named by the configuration too; a retired generation before the reset; the caller table read from inside the forwarded call; the slot turning read-only between proof and swap; a setter that is not executable), and 27 mutations were added.

Killed, by kind: 11 of 11 timing constants and thresholds; 40 of 51 comparison changes; 30 of 37 dropped resets; 13 of 14 dropped counters and stores; 5 of 10 float/double changes; 37 of 37 changes to the constant table; 67 of 71 logic changes.

The 28 survivors (27 of them also run against 300 extra random walks, which killed none):

| survivor | why it cannot be seen |
|---|---|
| ease: `now &&` dropped | with `now` 0 and a time on record, `now >= before` is false anyway |
| ease: `from > 0` → `>= 0`; `isfinite(from)` dropped | the recorded distance is 0 only together with time 0 (both reset together), and is always finite |
| ease: snap `<` → `<=`; snap test in double | both values are at least 0.05, so their difference is exact and a multiple of 2^-28, which 0.002f is not |
| ease: step limit `<` → `<=` | both branches give 50 000 at equality |
| ease: `target - from` written as `from - (from - target)` | bit-identical in IEEE arithmetic |
| ease: elapsed/45000 in double | the division of two exactly representable values rounds to the same float |
| `meters`: `isfinite` dropped; sight value: `isfinite` dropped | NaN and infinities already fail the range comparisons |
| sight value `> 0.05` → `>=`; in double; game value by float compare | 0.05f has one bit pattern, and that case is taken first |
| `meters(wanted)` and `meters(eased)` dropped | the distances are validated when set, and the ease stays between two valid values |
| owner proof: `count == 0` dropped | `index >= 0` is true for an unsigned index |
| ease reset on a lost owner dropped; `ease_reset` keeping either one of its two values; `reset_for_tests` keeping `shown_at` | a lost owner disarms until the next begin, which resets; either value alone at zero disables the ease |
| begin: `possible = false` before the generation; restore: `possible = false`; restore: `owned_generation = 0` | `possible` is already false there / set again by the next begin; `owned_generation` is only read while reserved |
| `site_overflow` not counted | no export reads it |
| hook: `armed` not read again under the lock; begin: policy written before the lock; begin: armed before the last owner proof (the failure path disarms again); caller row claimed without looking at the compare-exchange result | visible only to a second thread |
