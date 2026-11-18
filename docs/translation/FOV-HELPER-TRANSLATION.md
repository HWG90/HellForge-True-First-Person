# FOV helper: the C translation

`src/fov_helper.c` and `src/fov_helper.h` are `native/first_person/native_ads_fov.cpp` (748 lines) with `native_ads_fov.h` and `native_memory.h` of the retail kit at 0.5.41-smooth.66, in C11, for build 25480438. "cpp:N" is a line of the retail file; the comment above each C function gives its retail line range. `presentation_phase.h` is not included by the retail file (its phase logic is written out in `shared_lens`, cpp:129-157), so it is not translated, and nothing of `src/camera_math.h` is needed.

Both compile checks pass with no warnings: `cc -std=c11 -Wall -Wextra -Werror` and `zig cc -std=c11 -target x86_64-windows-gnu -Wall -Wextra -Werror`. The translation is compared with the retail source call by call, see section 6.

## 1. The host interface (`TfpFovHost`, set once with `tfp_fov_set_host`)

| member | replaces in the retail file |
|---|---|
| `hook` (a value) | the address of `setter_hook` (`callback()`, cpp:579): what is put in the dispatch slot and recognised there. In the game it is the wrapper that calls `tfp_fov_setter_from` |
| `read(at, out, length)` | `ReadProcessMemory` in `read` (cpp:223), all bytes or false. The bounds on `at` and `length` (1024) stay in C |
| `readable_span(at, length)` | `readable_span` (cpp:193-210, `VirtualQuery`). Called only after a failed read while an owner proof is open, as retail |
| `seconds(&out)` | the non-fixture half of `seconds` (cpp:85-90): false when the frequency is not positive or the counter cannot be read, else ticks/frequency. The `isfinite` test stays in C |
| `writable_slot(at, exe)` | the `VirtualQuery` part of `writable_slot` (cpp:441-445, non-fixture). The 8-byte alignment test stays in C |
| `writable_request_field(at)` | the `VirtualQuery` part of `writable_request_field` (cpp:356-360). The 4-byte alignment test stays in C |
| `executable(at)` | `executable` (cpp:260-264) |
| `cas(slot, expected, desired, &observed)` | `InterlockedCompareExchangePointer` inside `__try` (cpp:461-464); false = the operation faulted |
| `cas32(at, expected, desired, &observed)` | `InterlockedCompareExchange` inside `__try` on a request's lens field (`request_cas`, cpp:366-371) |
| `write(out, data, length)` | `WriteProcessMemory`: the generation (cpp:597) and the statistics (cpp:746) |
| `pin(address)` | `GetModuleHandleExW(FROM_ADDRESS, PIN)` (cpp:580-583); called with the address of `tfp_fov_begin`, then with the executable base, in retail's order |
| `lock_exclusive`, `unlock_exclusive` (lock number) | `AcquireSRWLockExclusive`/`ReleaseSRWLockExclusive` on `control_lock` (0) and `policy_lock` (1). Retail takes neither lock shared |
| `forward(original, camera, radians)`, optional | nothing in retail. NULL: the original setter is called directly, as retail does. See section 4 |

The hook's caller address is a parameter: `tfp_fov_setter_from(return_address, camera, native)` (retail `_ReturnAddress()`, cpp:519).

The fixture seams (`fixture_*`, cpp:69-80 and their uses, including the event pair before arming at cpp:666-668) are not translated. Where retail has a fixture path and a game path, the game path is translated: `contract` requires the slot to hold `exe + setter` and asks `executable(getter)` (cpp:495), `begin_policy` pins the executable (cpp:647-649), `writable_slot` asks about the executable image (cpp:445), `seconds` is the performance counter.

## 2. Constants

All are in one block at the top of `src/fov_helper.c`. `B_` = differs per build, `L_` = structure layout, the same number in both builds. Evidence: **map** = `tools/build_map.py --to-here`; **image** = bytes read from `game-pre1020.dll` / `r6_reload_exe_20261017.bin` on 2026-11-16 (file offset = RVA); a file name = that file of this repository has the same value for this build.

| name | retail | here | evidence |
|---|---|---|---|
| `B_GAME_STAMP` | `0x6af322c1` | `0x6ab3b43f` | map; `profile.h`; image PE header |
| `B_GAME_SIZE` | `0x3382000` | `0x4744000` | map; `profile.h`; image PE header |
| `B_EXE_STAMP` | `0x6af2d5e6` | `0x6ab382e4` | map; `profile.h`; image PE header |
| `B_EXE_SIZE` | `0x29fe000` | `0x39e8000` | map; `profile.h`; image PE header |
| `B_EXE_SETTER` | `0x2dd080` | `0x30d990` | map; `native_contract.h` camera_fov |
| `B_EXE_GETTER` | `0x2dd070` | `0x30d980` | map; `native_contract.h` camera_fov_getter |
| `B_GAME_CALL` | `0x110bfda` | `0x127a11a` | map; `native_contract.h` camera_fov_call |
| `B_HEX_CALL` | `ff9280000000` | the same | image at game+0x127a11a |
| `B_HEX_SETTER` (17 bytes) | `…e92f6c1f00` | `…e90f211c00` | image at exe+0x30d990; only the jump displacement differs |
| `B_HEX_GETTER` | `f30f104134c3` | the same | image at exe+0x30d980 |
| `B_GAME_CONTEXT` | `0x2e287d0` | `0x346d560` | map; `profile.h` FP_CAMERA |
| `B_GAME_AVATAR_OWNER` | `0x2ce1ee0` | `0x3326d20` | map; `profile.h` FP_AVATAR |
| `B_GAME_CONTROLLER` | `0x2e381a8` | `0x347ce70` | map; `profile.h` FP_CONTROLLER |
| `B_EXE_REGISTRY` | `0x1ab76f8` | `0x1a100f0` | map; `profile.h` FP_REGISTRY |
| `B_GAME_STATE` | `0x2ce1508` | `0x3326340` | map; `profile.h` FP_STATE; image: the rip load inside `B_HEX_FIRD_SCALE` resolves to it |
| `B_STATE_FOV` | `0xac488` | `0xac408` | map; image: `f30f5e8008c40a00` (divss xmm0,[rax+0xac408]) inside `B_HEX_FIRD_SCALE` |
| `B_GAME_API_ROOT` | `0x2ce14c8` | `0x3326308` | map; `profile.h` FP_API_ROOT; image: the load right after the call resolves to it |
| `B_AVATAR_FLAGS` | `0x53e888` | `0x53e880` | map; `profile.h` FP_AVATAR_FLAGS |
| `B_AVATAR_STRIDE` | `0x1240` | `0x1238` | map; `profile.h` FP_AVATAR_STRIDE |
| `B_GAME_FIRD_DISPATCH` / `B_HEX_FIRD_DISPATCH` | `0x110b4db` / `e8e0fc0000` | `0x127961b` / `e860fb0000` | image: the call 0xaff bytes before `B_GAME_CALL` (as retail); its target game+0x1289180 holds the next four at retail's distances |
| `B_GAME_FIRD_PUBLISH` / `B_HEX_FIRD_PUBLISH` | `0x111e3ce` | `0x128c38e`, bytes the same | image: only match in the game image |
| `B_GAME_FIRD_INPUT` / `B_HEX_FIRD_INPUT` | `0x111ba5e` | `0x1289a1e`, bytes the same | image: 0x12 before `B_GAME_FIRD_SELECT`, as retail (the 7 bytes occur four times in the image; this is the one in that function) |
| `B_GAME_FIRD_SELECT` / `B_HEX_FIRD_SELECT` | `0x111ba70` | `0x1289a30`, bytes the same | image: only match |
| `B_GAME_FIRD_ROW` / `B_HEX_FIRD_ROW` | `0x111ba91` | `0x1289a51`, bytes the same | image: only match |
| `B_GAME_FIRD_SCALE` / `B_HEX_FIRD_SCALE` (39 bytes) | `0x111baa2`, `…06b51201…535abc01…88c40a00…` | `0x1289a62`, `…c6dc1301…cbc80902…08c40a00…` | image: only match with the three displacements left open; here they resolve to the float 57.29578, to game+0x3326340 and to +0xac408 |

31 build-specific constants, none open. The six FirdPerson addresses are not in the kit's migration records (`build_map.py` answers "added on 26568081"); they come from this build's image alone.

Layout constants (22, the same numbers as retail):

| names | evidence |
|---|---|
| `L_CONTEXT_CAMERA` 0, `L_CONTEXT_HEAD` 0x1F8, `L_CONTEXT_TAIL` 0x1FC, `L_CONTEXT_IDLE` 0x1F00 | `snapshot.c`, `backend.c` |
| `L_CONTEXT_RECORDS` 0x200, `L_CONTEXT_RECORD` 0xE8, `L_REQUEST_LENS` 0x3C | image: `imul r8,r8,0xe8`, `cmp word [rax+r14+0x200],4`, `movss [r12+0x3c]`, `[rax+r14+0x23c]` in the FirdPerson patterns |
| `L_AVATAR_COUNT` 0x6C, `L_AVATAR_ROWS` 0x110, `L_DESCRIPTOR_AVATAR` 8, `L_DESCRIPTOR_BODY` 12 | `snapshot.c` |
| `L_FLAG_SHOULDER_A` 10, `L_FLAG_SHOULDER_B` 15, `L_FLAG_SEATED` 14, `L_CONTROL_AIM` 4, `L_CONTROL_GUARD` 5 | `snapshot.c` |
| `L_REGISTRY_UNITS` 0x88, `L_REGISTRY_CAPACITY` 0x98, `L_REGISTRY_GENERATIONS` 0xA0 | `backend.c` |
| `L_API_CAMERA` 0x20, `L_API_FOV_SETTER` 0x80 | image: `mov rdx,[rax+0x20]`, `call [rdx+0x80]` at the call site |
| `L_API_FOV_GETTER` 0x78 | image: the executable stores the getter and the setter in neighbouring slots (exe+0x30ecf8: exe+0x30d980 to exe+0x27c9898, exe+0x30d990 to exe+0x27c98a0) |

Numbers that are retail's policy, not build data (0.25 s, 0.12 s, 1.05, 0.05, 80..500 ms, 10°..170°, 1°..179°, the limits 8 / 32 / 1024 / 0x400000) are left as literals where retail has them.

## 3. Dropped

Only the `NADSF_FIXTURE` seams (section 1). Nothing else: the FirdPerson policy (`ValidateFird`, `BeginFird`, `BindFird`, the request lens ledger) is never used by the shipped retail Lua, but it shares state and the hook with the live policy, so it is translated in full. `UNIT_MISMATCH` (8) is defined and never raised in retail; the number is kept.

## 4. Where C needed another construct

| retail | C | why the same |
|---|---|---|
| `struct Unlock` guards (`control_lock` in every export; `policy_lock` in `Bind`, `BindFird`, `SetTransition`) | the body is a `*_locked` function; the export locks, calls it, unlocks; where two guards nest, two functions nest (`bind_locked` → `bind_policy_locked`) | every return path releases at the same point and in the same order (policy, then control); the checks retail makes between the two acquisitions stay between them |
| `struct Unlock` guard for `policy_lock` in the hook (cpp:523) | one `policy_release()` after the forwarded call and the diagnostics; the only earlier `return` is before the lock | the hook has no other exit |
| `OwnerReadProbe` constructor/destructor (cpp:186-192) | written out in `owner_status`: push, run `owner`, compute the result, pop | `owner_status` is the only user and has one exit |
| `thread_local` | `_Thread_local` | |
| `std::atomic<Setter> original` | `_Atomic uintptr_t`, cast to the function type at the call | same load/store orders |
| `std::atomic<float>` display values with an always-lock-free assert (cpp:51-52) | the float's bit pattern in `_Atomic uint32_t`, `ATOMIC_INT_LOCK_FREE == 2` asserted | the float read back is bit for bit the float stored |
| default member initialisers (`Policy{}`, `RequestField{}`) | `policy_init` (zero, then `shared_progress = 1`), the same as a static initialiser for the global; `request_field_clear` (zero) | same start values |
| `template value<T>` | macro `value(at, out)` = `read(at, &out, sizeof out)` | |
| default arguments (`request_matches(p, false)`, `config(…, false)`, `normalize_fird_request(p, false, nullptr)`) | passed explicitly | |
| the two lambdas in the hook's conditions (cpp:536-540, 545-549) | `hook_ads_request`, `hook_exit_request`, called at the same place in the same `&&` chain | same reads in the same order |
| `std::min`, `std::max` on floats (cpp:148) | `min_f`, `max_f` with the library's definitions (`b < a ? b : a`, `a < b ? b : a`) | not `fminf`/`fmaxf`, which treat NaN differently |
| `std::tan`, `std::atan`, `std::isfinite` on floats | `tanf`, `atanf`, `isfinite`; `fmin`, `fminf`, `fmaxf` are retail's own calls | the C++ overload chosen for a float argument is the float function |
| `IMAGE_DOS_HEADER`, `IMAGE_NT_HEADERS64` (cpp:245-251) | byte buffers of the same sizes (64 and 264) with fields at their offsets | the same two reads of the same lengths, same tests in the same order |
| `display_cache=Display{0,0,next.source_degrees}` | three assignments | |
| direct call of the original setter (cpp:514, 520, 576) | `call_original`: direct call when `host.forward` is NULL | An addition, for the tester only: with fake memory, `exe + setter` is not code. The game build leaves `forward` NULL |
| `callback()` = address of `setter_hook` | `host.hook` | the C hook takes the return address as a parameter, so the function in the slot is the game build's wrapper |

`tfp_fov_reset_for_tests` and `tfp_fov_set_host` are not in retail. The reset keeps the host and clears the calling thread's probe pointer only.

## 5. Open

1. **Fixture path versus game path.** The reference in the differential test is the retail file compiled with `NADSF_FIXTURE`, which compares the slot with `fixture_target`, leaves out `executable(getter)` and the pin of the executable, and uses the fixture clock. The C has the game path. The three checks the fixture build lacks are tested on the translation alone (section 6).
2. **The wrapper in the slot.** The game's call site passes the lens in `xmm1`; the wrapper that sits in the slot has to hand `rcx`, `xmm1` and its own return address to `tfp_fov_setter_from`, and must not disturb what the game expects after `call [rdx+0x80]`. That wrapper is not part of this file.
3. **Float library functions.** `tanf`, `atanf` come from the C library of whichever build. Natively both sides use glibc and agree bit for bit. I did not check that zig's C++ `std::tan(float)` (the retail DLL) and MinGW's `tanf` (this C on Windows) are the same routine; they should be. Neither compile command enables FMA.
4. **Hook before `tfp_fov_set_host`.** Any call before the host is set dereferences NULL. Retail has no such state; no guard was added. A hook call before the first successful begin calls a null original, as retail does.
5. **`_Thread_local` in a DLL built for `x86_64-windows-gnu`.** Compiles; whether zig uses native or emulated TLS for the mod DLL was not looked at (the same question as in the camera helper).
6. **`B_HEX_FIRD_INPUT`** (`f3410f1044243c`) is short and occurs four times in this build's game image. The address is the occurrence 0x12 bytes before `B_GAME_FIRD_SELECT`, as in retail; the check is as weak or strong as retail's.
7. **Layout constants without an image witness of my own**: the avatar owner, descriptor, flag-byte, controller and registry offsets rest on this repository's 0.5.34 sources reading the same fields on this build.

## 6. Differential test

`tests/diff_fov_helper.cpp` (with `tests/diff_fov_helper_world.h` and `tests/diff_fov_helper_scripts.h`), built and run by `tests/run-diff-fov.sh` (one summary line; non-zero on any difference; "skipped", exit 0, without the retail tree, g++ or Linux). The retail file is compiled natively over `tests/winshim` with `NADSF_FIXTURE`; `src/fov_helper.c` is compiled as its own C object (`cc -std=c11 -O1 -ffp-contract=off -Wall -Wextra -Werror`) and linked in. One script: `tests/run-diff-fov.sh "<part of its name>"`; `--list` names them; `TFP_DIFF_REACHED=1` prints what each script reached; `TFP_DIFF_EXTRA_WALKS=n` adds walks; `TFP_FOV_C=<file>` tests another C file.

**Result: no difference between the original and the translation was found, and no fix was made to `src/fov_helper.c` or `.h` after the first run.** 197 scripts and 67,121 probe runs; 499,032 calls compared, 219,018 of them hook frames; 18,783,128 reads, writes, swaps, lock steps and forwarded calls matched one for one. About 6 seconds on 16 cores. A soak of 600 further random walks (`TFP_DIFF_EXTRA_WALKS=600`, 2.0 million calls) found no difference either.

The only failures on the way were two faults of the test itself (a script that asked the FirdPerson begin for an ease it must refuse, and one probe address named differently in the two worlds); both were corrected in the test.

### How the two are made comparable

- The same state is written into two sets of real pages: one with retail's addresses and offsets (from the retail source), one with this build's (from the table in section 2, typed again in the world header, not taken from the `#define` block). Every region has an unreadable page on each side. All state lives in that memory: a script changes a named place in both worlds at once.
- An address in one world is compared with an address in the other by name and offset ("game:state+0", "owner:flags[0]+0", "context+0x23c").
- The original reads through the stand-in `ReadProcessMemory`; the translation's host `read` makes the same `process_vm_readv` call. Clock: both read `fixture_now` / `fixture_clock_ok`. `readable_span`, the slot's page test and the lens field's page test are answered for the translation by the original's own functions (`VirtualQuery` over `/proc/self/maps`); the lens field's alignment test is taken out of that answer, so it is the translation's own. Both compare-and-swaps are real in both; the original's slot seams (`fixture_cas_fail_at`, `fixture_foreign_cas_at`) are repeated on the translation's own call count.
- The dispatch slot is compared by what it holds: the game's setter (original: the test's function, as `fixture_target`; translation: `exe + setter`), the helper (original: `setter_hook`; translation: `host.hook`), or a raw foreign value.
- The hook is called directly: the original with `_ReturnAddress()` answering `game + CALL + 6` of its world, the translation with that of its own. Forwarding goes to the test's "game setter" in both (`fixture_target`, `host.forward`). A script can make a value change while that setter runs.
- Every script runs in a child process forked from a parent that has built the two worlds and given the translation its host, and has made no other call to either helper.

### What is compared, after every call

The return value; the full ordered record of what the helper did: every memory read (place, length, complete or failed), every write through `WriteProcessMemory`/`host.write`, every slot compare-and-swap (stored, expected, found), every 32-bit compare-and-swap on a lens field, every lock and unlock of the two locks, and every forwarded setter call (camera pointer by name, the lens bit for bit); the number of clock reads; the number of slot swaps attempted; what the slot holds; the camera context from the ring to the idle record, byte for byte (the lens fields the FirdPerson policy rewrites); the generation written by a begin; the error code; then `read_stats` with the right generation, 256 bytes around the structure, and the record of that call itself. The translation's locks must be balanced at each return.

### Scripts

| group | scripts |
|---|---|
| hip: the third-person setting on the weapon camera, at 30, 60, 144 frames a second, the setting moving and at and outside its limits (1, 179, NaN ...), odd game lenses; with and without the ease | 6 |
| shoulder aim: the 120 ms tween in and out, interrupted, a pause, time backwards, the clock failing or answering NaN/infinity, one flag of the pair, other bits, the setting moving, full sights over lingering flags; exact 250 ms and 120 ms steps | 7 |
| the ease into and out of the sight (80, 180, 500 ms; 30, 60, 144 fps): zoom change in mid-ease; out on the weapon camera (target = the setting) and on the third-person request (target = the game's lens); in again on the way out; objections (mask, guard, seat); another weapon on the request, then bound; refused lenses; clock failing at the switch and in the ease, time backwards, a jump past the end; the switch at exactly time 0; the exact end of the ease | 45 |
| full sights passed through (reserved 0); a lease begun in full sights | 1 |
| presentation phase packet: plain at three frame rates; the token naming the old mode; the producer stopping (250 ms exactly and just over); clock back, failing, NaN; two frames per packet; zoom and setting moving; refused packets (regress, epoch back, mode flip, 0, NaN, out of range, wrong token, foreign slot/owner); changes in mid-epoch and the last sliver of an epoch; objections | 15 |
| bind: refusals in every order, a failing setting, changed code and image, values changing during the bind's own setter call, bound in mid-tween and in full sights, after disarm, after the end | 2 |
| everything validate, begin, validate_fird and begin_fird refuse: about 150 single faults of the config, the two images, each code pattern, the API table, the slot, the owner proof, the request ring, the setting, the lens field, pinning, the output; busy | 16 |
| dispatch slot: swap faulting at begin and at end, a foreign value arriving between proof and swap, foreign or restored value under the lease and at the end, unreadable and read-only slot at the end, begin answering 2, values changing during begin's own setter call, pin failing, generation unwritable (first and second write), table or getter moved | 12 |
| FirdPerson policy: held and restored; what the end finds (the game's value, an equal value, a vanished or changed request, a lost owner); several requests with the input source below the bound one; the source moving; aim camera and full sights forwarded; fields it cannot own; a read-only field (cleanup pending); objections; bind_fird and its refusals; the idle record; begins that fail after their own data write; a context that is not 4-aligned | 10 |
| hook arguments (foreign caller, foreign camera, before any begin, after the end) | 1 |
| owner lost under an armed helper: 21 causes on a hip, a full-sights and a FirdPerson frame, then every control call, end, repair, arm again | 63 |
| control calls in right and wrong orders; generations 1, 2, 3 and the retired one | 1 |
| checks only the translation makes; after `tfp_fov_reset_for_tests` | 2 |
| seeded random walks, 4,000 steps each, mixing all of the above with time steps of every kind, damage for a few steps, failing swaps and failing clock | 16 |
| probes (23 calls: six kinds of plain frame, four FirdPerson frames, begin three ways, begin_fird two ways, validate, validate_fird, bind, bind_fird, set_transition, end two ways, read_stats): for every single read the call makes, one run in which that read fails and heals, one in which it stays unreadable to the end of the call, and one for each of 57 values changing just before that read | 67,121 runs |

Scripts check, from the original's state, that they reached what they are for (so many frames replaced, an ease running, a ledger held, a given error).

### Checks only the translation makes

The reference build compiles three production checks out (Open 1). The translation has them; they are answered through the host and asserted separately (script "checks only the translation makes", expected results read off the original's non-fixture lines):

- `executable(getter)` in `contract` (cpp:495): answered true in every compared call; answered false once at validate and once at begin (expected and got: 0, error 6, generation 0, slot untouched).
- `pin(exe)` in begin (cpp:647-649): answered true; answered false once (expected and got: 0, error 9, generation 0, slot untouched, no generation consumed: the compared lease that follows gets generation 1 in both).
- `writable_slot`'s image test (cpp:445): the host checks that the image asked about is the executable, and answers with the original's fixture test (committed, writable, private).
- The slot must hold `exe + setter` (cpp:495): compared through the slot classes above.

### Not compared, and why

- **A hook call before any begin recorded the game's setter.** The original calls a null pointer there. Such frames are skipped and counted.
- **Concurrency.** One thread. The order of lock operations relative to reads, swaps and the forwarded call is compared; a setter in flight during End or ReadStats is not exercised, nor the fixture's pause before arming.
- **The direct call of the original setter** (`host.forward` NULL). The fake `exe + setter` is not code.
- **The clock itself and the page questions' own logic** (`QueryPerformanceCounter`, `VirtualQuery` in `readable_span`, `executable`, `writable_slot`, `writable_request_field`): the fixture clock is used, and the page questions are answered for the translation by the original's functions. The order of clock reads among the other steps is not observable in the original; their number per call is compared.
- **A begin whose recorded original differs from the slot's target** (cpp:645): needs the executable at two bases in one process.
- **A faulting 32-bit swap** (`CAS_FAULT` from `request_cas`): the stand-in never faults; retail has no fixture seam for it either.
- **The Windows build of the C** (MinGW's `tanf`/`atanf`, Open 3). Natively both sides use glibc, with `-ffp-contract=off`.
- **Whether the constants of section 2 are right for build 25480438.** The test proves the translation reads the places the table names; it cannot prove the table.

### Test-only additions

`tests/winshim/fov_extra.h`: `LONG` and `InterlockedCompareExchange` with an optional observer (`shim_on_cas32`). `tests/winshim/windows.h` and `intrin.h` are unchanged. Nothing outside the files of this helper changed.

### Mutation check

Done on 2026-11-16 in scratch copies outside the repository (`/tmp/claude-1000/fov-diff/`): one change at a time in `src/fov_helper.c`, the whole test run against each. **124 mutations, 113 killed, 11 survive.** The first round killed 95 of 109; seven survivors were answered with new scripts (the last sliver of an epoch for the 0.000001 floor; a bind in full sights in mid-tween with no frame between; a bind to the same request before the end; a lens changing between the read and the swap of a bind with a live ledger; the idle record's ledger when the ring fills; an unaligned context; other captured lenses), one mutation that computed the same bits was replaced by a stronger one, and 15 more were added.

Killed, by kind: 21 of 21 timing constants, limits and `<`/`<=` changes (0.25 s, 0.12 s, 1.05, 0.05, the ease length and its two ends, the 250 ms packet age, 1°/179°, 10°/170°, 80/500 ms, pi, the 0.000001 floor); 8 of 8 float/double and reassociation changes; 16 of 20 dropped resets; 6 of 6 dropped counters; 16 of 16 changes to this build's constants, code bytes and layout numbers; 46 of 53 logic changes (among them the policy lock released before the forwarded call, caught by the order of steps; the clock asked once instead of twice at time 0, caught by the count of clock reads; the two translation-only checks).

The 11 survivors:

| survivor | why it cannot be seen |
|---|---|
| `min_f` with the operands' roles exchanged (`a < b ? a : b`) | differs from `std::min` only for a NaN second operand; that operand is the result of `max_f(0, x)`, which is never NaN |
| shared-phase shoulder path without `shoulder_clock = false` | the same statement sets `shoulder_weight = target` and `shoulder_target = shoulder_aim`; the next local tween therefore starts through the "target changed" arm, which sets the clock itself, or after `reset_shoulder`, which clears it |
| hook: ledger of a moved source not cleared before the new one is written (`post_evaluation`) | on success the ledger is overwritten whole; on failure the hook clears it (cpp:562) |
| `failed_begin` without `armed = 0` | `armed` is already 0 on every path into it (it is set only at the end of a successful begin) |
| begin without priming `display_cache` | overwritten by the next `read_stats`, since no writer runs at the same time: visible only to a second thread |
| `tfp_fov_reset_for_tests` without `policy_init` | the policy is replaced whole by the next successful begin before anything reads it |
| `READ_FAILED` although unsafe; `pointer()` not marking unsafe | a proof stops at its first failed read or bad pointer, so transport and unsafe never meet in one proof |
| `normalize_fird_request` without `error(CLEANUP_PENDING)` when the old source cannot be restored | every caller overwrites the error at once (`failed_begin`, `bind_fird`'s `reject`) |
| `armed = 1` although not stable | `failed_begin` follows at once and clears it: visible only to a second thread |
| begin without "the recorded original differs from the target" | needs the executable at two bases in one process (not covered, see above) |
