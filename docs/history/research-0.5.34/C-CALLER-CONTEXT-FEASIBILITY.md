# Strict-C SDK caller-context feasibility

2026-11-09. Scope: an isolated Windows x64 fixture, compiled locally and run in
the existing bridge-terminal Wine test prefix. No game attachment, game calls,
game memory reads/writes, SDK pointer replacement, or product source changes.

## Result

**The OS-unwind approach works in the tested optimized C callback.** It recovers
the caller's original R15, exact return PC, and pre-CALL RSP even after the C
compiler has repurposed R15. It works with frame pointers both omitted and
enabled. The capture implementation contains no handwritten assembly or fixed
register variables. This closes the isolated C ABI feasibility gap. It does
**not** qualify an original-build live SDK hook or the sensitivity experiment.

Three build variants passed **432,207 checks across 36,018 callback invocations**,
with zero failures. There were **18,006 witnessed callback-R15 repurposings**.
Each variant includes main-thread tests and two additional Windows threads,
launched before either is joined. Their exact overlap was not instrumented.

| Build | Flags in addition to common C flags | Checks | Callback invocations | Failures |
| --- | --- | ---: | ---: | ---: |
| O2 | `-O2 -fomit-frame-pointer` | 144,069 | 12,006 | 0 |
| O3 | `-O3 -fomit-frame-pointer` | 144,069 | 12,006 | 0 |
| O2-frame-pointer | `-O2 -fno-omit-frame-pointer` | 144,069 | 12,006 | 0 |

Compiler: existing HellForge Zig **0.17.0**, target `x86_64-windows-gnu`, C11,
`-fms-extensions -g -UNDEBUG -Wall -Wextra -Werror -fno-lto`. Runtime: existing
**wine-11.21**, with only
`~/Documents/ChatGPT/Linux HD2/native-work/bridge-terminal/wine-prefix`.
The linker successfully imported the three functions from `ntdll.dll` using
`-lntdll`; no OS tools or libraries were installed.

The full commands, source/object/executable hashes, run environment, raw results,
and stderr are retained in
[c-caller-context-evidence.json](~/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge/docs/research/c-caller-context-evidence.json).
Wine emitted Mesa EGL driver warnings during process startup; all fixture
processes exited zero. The retained timing field includes fixture assertions and
TLS/test work and is **not a game performance or FPS result**.

## Mechanism and independent oracle

Call chain:

```text
test-only assembly oracle (models the native game caller)
  -> optimized noinline C SDK callback(unit, node)
       -> C original-getter analogue; its returned matrix pointer is retained
       -> noinline capture_sdk_caller(expected_pc, callback_address, result)
            -> RtlCaptureContext
            -> RtlLookupFunctionEntry + RtlVirtualUnwind: helper -> callback
            -> RtlLookupFunctionEntry + RtlVirtualUnwind: callback -> native caller
```

`RtlCaptureContext` captures the C helper's context. Reading its raw R15 would
therefore be wrong. The first unwind restores the callback context, whose R15
may also be a compiler local. The second unwind applies the callback's own
Windows exception metadata, restoring its saved native R15 and returning the
native caller context. We stop there; **we do not unwind the native caller**.
No original-game unwind metadata is needed for this two-frame recovery itself.

The independent test oracle sets R15 to a changing 64-bit sentinel, saves its
actual RSP, and records its literal post-CALL instruction address before invoking
the callback. It checks R15 again after the callback returns. That assembly is
in **test-only** `oracle.S:1-31`; it is not part of the candidate capture function
or any product mod. The C wrapper separately records `_ReturnAddress()`, and its
value must equal the oracle's independently recorded PC. The expected PC passed
to the candidate comes from the oracle, so the check is not just a comparison
against the candidate's own captured value.

Implementation sources:

- [capture.c:5](~/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge/work/caller-context-proof/capture.c:5): all-C context capture, stack bounds, two metadata unwinds, exact-PC check.
- [callbacks.c:20](~/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge/work/caller-context-proof/callbacks.c:20): light and register-pressure C callbacks; original getter called once before capture.
- [oracle.S:1](~/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge/work/caller-context-proof/oracle.S:1): independent, explicitly labelled test-only register/stack/PC oracle.
- [main.c:38](~/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge/work/caller-context-proof/main.c:38): equality checks, negative witnesses, per-thread tests and original return-pointer preservation.
- [run_fixture.py:48](~/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge/work/caller-context-proof/run_fixture.py:48): reproducible isolated compile/run commands.

These are Windows C APIs/intrinsics, not portable ISO C. `NtCurrentTeb()` reads
the current thread's own stack bounds through the compiler-supported header
intrinsic. Neither candidate `.c` file uses inline assembly. Disassembling
compiler-generated instructions does not make them handwritten assembly.

## Evidence that register restoration is real

For O2 with frame-pointer omission, object disassembly
`callbacks-O2.asm:72-153` shows:

```text
callback_pressure+0x000: push r15
callback_pressure+0x0D4: mov r15, [TLS fixture_witness]
callback_pressure+0x115: call capture_sdk_caller
```

Thus the native sentinel is saved, then R15 is reassigned before capture. PE
metadata at `pe-metadata-O2.txt:421-432` describes the callback's stack allocation
and saved R15, with **Frame reg: none**. The helper's separate metadata at
`:401-412` also has no frame register. This run does not depend on an RBP chain.

The final O2 witness was:

| Value | Observed |
| --- | --- |
| Oracle's native R15 | `0x0ac2066672d67285` |
| R15 while inside callback | `0x000000000101fa20` |
| R15 captured inside helper | `0x000000000101fa20` |
| R15 after two virtual unwinds | `0x0ac2066672d67285` |
| Oracle's native pre-CALL RSP | `0x000000000101f9c0` |
| Recovered native RSP | `0x000000000101f9c0` |
| Callback RSP at helper call | `0x000000000101f8f0` |
| Helper's captured RSP | `0x000000000101f360` |
| Oracle's exact return PC | `0x000000014000222a` |
| Recovered return PC | `0x000000014000222a` |

In the frame-pointer-enabled build, the helper additionally repurposes R15 to
the expected PC (`0x1400021ee`), while the callback has repurposed it to its TLS
witness pointer. Both virtual unwinds still recover the original sentinel.

The recovered RSP is **the caller's pre-CALL RSP S**. A normal Win64 CALL places
the return address at S-8; returning from that frame restores S. The independently
recorded RSP equality and 16-byte alignment checks establish this in the fixture.
It is neither the C callback's post-prologue stack pointer nor S-8.

Negative cases refuse return PCs displaced by +1 and -1, reject the wrong
callback's runtime-function entry after one unwind, and reject null expected
PC/callback/output. Missing function metadata is a refusal path in the candidate,
but deliberate metadata absence/corruption and stack-bound failure were **not
fault-injected**. `RtlVirtualUnwind`'s returned exception-handler pointer is not
a success flag; the code validates the resulting context instead. This operation
updates a local `CONTEXT` structure, not the executing thread's registers.

## Relevance to original HellForge build 25480438

The previous offline input investigation established an original evaluator
UnitAPI+0x90 call at `game+0x12865FC`, returning at **`game+0x12865FE`**, and a late
UnitAPI+0x00 call returning at **`game+0x1287A44`**. Its request is in R15. Its
native caller locals are context **S+0x80**, avatar index **S+0x48** and evaluator
dt **S+0xA0**. See
[NATIVE-INPUT-DEEP-DIVE.md:67](~/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge/docs/research/NATIVE-INPUT-DEEP-DIVE.md:67)
and the source hashes and byte witnesses in `native-input-evidence.json`.

This fixture offers a C-only way to obtain the **correct S and R15**, while
checking a build-qualified exact return PC, without relying on the C compiler's
current use of R15, a guessed stack size, an assembly thunk, or inline optic-code
patching. The earlier report's “all-C capture remains unqualified” statement is
now narrowed: **private optimized fixture passed; original live callback remains
unqualified**.

Before this could support a live input boundary, the actual product build must
retain these two noninline frames, valid unwind metadata and a non-tail call.
The direct SDK dispatch must reach that C wrapper with the native evaluator as
its immediate caller. An intervening foreign wrapper/thunk or a compiler/LTO
change would alter that chain and must be refused or separately qualified.
The original getter can be called first because it must preserve R15 under the
Win64 ABI, as the fixture's getter analogue does, but the actual SDK getter and
active dispatcher still need their original-build identities checked.

## What remains unproved

1. The live HellForge/Proton process resolves and unwinds the actual mod DLL's
   metadata in exactly the tested manner. The private Wine prefix is not the
   game's Proton runtime, and this executable is not the shipped DLL.
2. The current loaded build/module and SDK slot have the byte-qualified call
   path, exact return PC and immediate caller expected above. No fixture result
   establishes game thread ownership, callback lifetime, slot restore safety,
   or teardown/quiescence.
3. Recovered R15 and S identify the expected live camera request, context,
   avatar, weapon and generation. This fixture contains no game objects and
   never dereferences or writes them. Its stack checks are basic consistency
   guards for trusted current-thread frames, not hardened recovery from corrupt
   unwind metadata or arbitrary addresses.
4. A private preset can be swapped/restored with safe lifetime and concurrency.
   Input history/steering samples can still change after sampling. The prior
   steering-coherence and sensitivity-only gain concerns remain unresolved.
5. The user perceives native-feeling zoom sensitivity, transitions, recoil,
   steering and weapon changes. A context-capture fixture cannot establish any
   of those visual or gameplay outcomes.

The next useful test would be **read-only caller-context observation** in a
separately qualified original-build SDK callback, with original getter return
values unchanged and no preset writes. That test has not been installed,
attached, implemented in product source, or executed by this investigation.

## API semantics checked against primary documentation

- [Microsoft RtlCaptureContext](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtlcapturecontext): context capture semantics.
- [Microsoft RtlVirtualUnwind](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtlvirtualunwind): recovering a preceding frame and using `RtlLookupFunctionEntry` metadata.
- [Microsoft x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170): nonvolatile registers, unwind metadata and stack alignment.
- [Microsoft _ReturnAddress](https://learn.microsoft.com/en-us/cpp/intrinsics/returnaddress?view=msvc-170): intrinsic meaning and inlining caveat.

No production behavior is claimed solely from these documents; the concrete
claims above are bounded by the retained fixture source, binary and run evidence.
