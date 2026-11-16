# Native input and zoom sensitivity: original HellForge build

**Offline investigation only.** No process attachment, live memory reads/writes, hooks, target calls or implementation changes were made. The native-camera pin stays excluded. The Steam 0.5.40 SDK input experiment remains disabled pending its own trial; its source is research input, not a qualified HellForge module.

## Main finding

HellForge’s original native FPS evaluator has the same useful **pre-filter UnitAPI+0x90 boundary** as the later Steam build. The original call is `game+0x12865FC`, returning at `0x12865FE`; the native getter target is `exe+0x1FD220`. The getter returns the node’s native world-matrix pointer. This offers a data-pointer interception point before the game filters/scales player input, without replacing the camera or patching executable optic code. It is a candidate boundary, not an already safe or complete sensitivity fix.

Old `game+0x1277250` matches the later native orientation solver’s **293 decoded instruction records / 1328 bytes** after masking only relative branch/call and RIP relocation operands. No other differences were found in that span. The native6 evaluator has 2051 corresponding instruction records, with 45 concrete layout/table differences; original-build addresses must be used.

## Original-build map

| Role | Original build25480438 | Steam research equivalent |
|---|---|---|
| Native6 evaluator | game+0x1286330 | game+0x1118210 |
| Last SDK getter before filtering | call0x12865FC, return0x12865FE; UnitAPI+0x90 | call0x11184DC, return0x11184DE |
| World-matrix getter implementation | exe+0x1FD220 | exe+0x1F3020 |
| Native filter exp | game+0x21087D0, called0x128662A | game+0x1F8FE10 |
| Target-steering addition | game+0x128D820, called0x12868C4 | game+0x111F700 |
| Native quaternion solver | game+0x1277250, called0x1286A21 | game+0x1108FB0 |
| Late existing getter/completion | call0x1287A41, return0x1287A44 | call0x1119921, return0x1119924 |
| Native preset table / row stride | game+0x32F9990 / 144 bytes | game+0x2CB2140 / 144 bytes |
| Input owner cell | game+0x347CF18 | game+0x2E38250 |
| TPV FOV setting used by input | settings+0xAC408 | settings+0xAC488 |
| Avatar record stride | 0x1238 | 0x1240 |
| Native target steering | avatar+index*0x1238+0x53E9A4/A8/AC | avatar+index*0x1240+0x53E9AC/B0/B4 |

UnitAPI comes from the cell at `game+0x3326308`, root+0x18. The candidate must validate the +0x90 slot and original function bytes/ownership at runtime. It must use a pointer-slot lease, not `hf->hook`, whose documented facility detours function code. Existing hooks unrelated to this input experiment do not prove permission or safety for a new body detour.

## Native processing order

1. Native SDK calls establish current unit/body/weapon transforms. UnitAPI+0x90 returns before look filtering starts.
2. Native smoothing reads three raw axes at avatar owner + index*0xA7AEC +0xA7C24. For positive evaluator dt, it uses `alpha = float(1 - exp(float(dt * -75)))`, combining raw input and request history at+0x3C/+0x40/+0x44. Native zero/inhibit branches remain significant.
3. Native inversion flags apply. Lens ratio `R = request[0x84] * rad_to_deg / settings[0xAC408]` scales the player contribution. The selected mouse/pad **scope** sensitivity D then applies from input-owner+0xA7B50 or+0xA7B44.
4. `game+0x128D820` adds an independent native vector S from the avatar record. It is added after zoom/device scaling.
5. The game copies the complete 144-byte selected preset, whose yaw/pitch gains are+4/+8. For positive evaluator dt, context+0x15C/+0x160 contributes through division by dt and those gains.
6. The native solver applies the pitch gate and native gains/frame time, yaw sign/half-angle, pitch limits and quaternion math. The game retains all later camera/recoil/animation/lens work.

An independent raw-look integrator skips native filtering and native steering/control terms, even if it appears to remove zoom scaling. It therefore conflicts with the requested native-feeling mode. The camera helper must continue to forward native FPS orientation/position exactly.

## Steering is not “just recoil”

The original build has a concrete target-steering producer. At `0x82A0AF/C5/D4`, the avatar update forms its record from the avatar owner, index*0x1238 and base0x53D8B0. It reads the target entity at record+0x1104, gets a world point, and calls **CameraAPI+0xF8** at `0x82A617` to project it. Screen error is normalized; a weight at record+0x1100 is ramped. `0x82A7C1` calls vector smoothing at `0x173CD10`, then `0x82A7D4/DE` publishes record+0x10F4/+0x10FC, the S components consumed by native look.

The invalid-target branch retains prior vector terms for the smoothing path. Neither “not firing” nor “no valid target right now” proves S is zero. This is explicit original-build evidence, not merely an inference from Steam. The full native recoil producer chain remains separate and is not claimed mapped here.

## Why simple gain division is wrong

Before context/gate details, native input is `(R*D*U + S)*g`. Replacing g with g/R gives `D*U*g + S*g/R`, which changes native steering. The intended unzoomed player contribution is `(D*U + S)*g`.

The Steam experiment solves the bounded per-axis ratio `g_prime = g*(D*U+S)/(R*D*U+S)` when S is nonzero, with finite/gain-range, conditioning, sign/gate, rounding and error checks. For S=0 it deliberately uses the input-independent g/R. Native history, raw axes, context, lens, orientation and saved sensitivity settings stay unchanged. The code’s float32 arithmetic and native exp evaluation are relevant; substituting a generic double algebra expression is not equivalent.

**Unresolved semantic limit:** with nonzero S this result depends on sampled U/S. A second sample before publication cannot prove that producers will not change the values before the native consumer reads them. A post-history mismatch can detect changed filtering after the fact, but cannot undo an orientation already evaluated. Do not present this experiment as a universal sensitivity fix.

## Preset ownership and native reader lifetime

The narrow experimental write is request+0x80, temporarily pointing through native uint32*144 table arithmetic to a private full preset clone. Only clone+4/+8 differ. In this captured old image, preset identity fields match indices0..92; that observation is not a supported synthetic-index API or a runtime guarantee.

The evaluator makes nine independent16-byte reads for look and another full preset copy for FOV. The old FOV helper reads other preset offsets (+0x24/+0x28/+0x2C, +0x4C/+0x50/+0x5C/+0x60/+0x64/+0x68), not the patched gain words. The outer dispatcher additionally reads preset+0x8C at `0x1279EEB`. Its first kind6 path skips the earlier +0x8C lookup (`0x12799F6`), but the blended-row consumer remains real. All nongain bytes must therefore remain immutable and readable even after a callback returns.

Old constructor `0x127E0CB/D3` seeds the request preset from the avatar’s native0x53D8F0 field. Old retirement `0x12771E8` clears request kind but leaves+0x80. Exit `0x127EBCC/D3` reloads the native preset into normal4’s context+0x1F0C. These facts help distinguish positive row retirement and normal fallback, but do not prove an already-running reader has drained.

A safe candidate needs exact header/request/thread/generation matching, compare-owned index publication/restoration, queued callback rejection, reentry/End handling, immutable full-template clone identity and pinned/quarantined backing. SDK callback drain alone is insufficient because native consumers run afterward. The original exit block differs substantially from Steam; its whole hash and stack assumptions cannot be copied.

## Original native caller frame

The original evaluator statically preserves the same stack layout as the Steam experiment. Its seven nonvolatile pushes, `LEA RBP,[RSP-0x310]` and `SUB RSP,0x410` give **RBP = S+0x100**, where S is the evaluator RSP at the SDK call (SDK callee entry RSP+8). The original spills are context at **S+0x80** (store0x1286370), avatar index at **S+0x48** (store0x1286430), and evaluator dt at **S+0xA0** (store0x128637D). R15 receives the original request argument RDX at0x1286374. This validates the static old-build layout; it does not validate recovering these values from a newly compiled C callback or show runtime serialization.

## What can be reused entirely in C

- Pure scalar/SSE math, bounded native map reads, numerical refusal rules, finite/gate/error checks and exact native parameter forwarding. Namespace/classes become structs/functions; atomics, SRW locks, checked image/data-slot CAS and ownership ledgers already have C equivalents in this port.
- Original-build exp can be called through its verified native ABI after code validation, preserving its behavior. The old CRT block has not been qualified by the Steam exp fixture results in this audit.
- A separate C input module can lease UnitAPI+0x90 and reuse the existing late getter as a completion boundary while leaving the camera module native-authoritative.
- **ABI work remains:** the Steam experiment uses assembly thunks to capture the caller’s actual nonvolatile R15 request and native RSP. A plain C function cannot assume it still sees the incoming R15 after its compiler prologue. An all-C version needs a verified compiler intrinsic / unwind-based capture or a new argument proof that avoids this register dependency. A Windows context/unwind approach is only an investigation candidate here, not a qualified implementation.
- Keep normalization opt-in/experimental until original-build private fixtures and live trials pass. Do not bundle unqualified sensitivity work into the pin-removal correction.

## Required next evidence

1. Read-only live check: exact old module stamps, UnitAPI root/slot/target, preset table bounds/IDs, native input device and raw/history/steering/context snapshots. Coordinate hip/ADS/zoom/firing samples with the user; do not write settings or activate a new hook.
2. Trace original input/steering producers and scheduling far enough to establish serialized consumption or retain explicit bypass cases. No amount of arithmetic removes this ordering requirement.
3. Prove actual request/caller-frame capture in private Windows C fixtures, using the statically mapped original spills, including native fullADS entry, exit, rapid weapon swaps, queued old callbacks, End/reentry and failed/foreign CAS.
4. Bind all old whole-function/outer-consumer fingerprints and clone-lifetime rules. Then compare native C math against original-build execution privately before a bounded, explicit live trial.

## Evidence files

`native-input-evidence.json` records source SHA256s, exact original code bytes/disassembly at38 witness sites, call edges, all45 evaluator differences, the full293-instruction normalized solver comparison, the steering comparison and remaining limits. All RVAs are module-relative, never live addresses to reuse across launches.

- experimental transaction and lifetime: [native_ads_editor.cpp](/home/goose/Documents/Codex/2026-11-07/tr/work/gadget-kit-fp-input/native/first_person/native_ads_editor.cpp:553).
- experimental gain math: [zoom_gain_math.h](/home/goose/Documents/Codex/2026-11-07/tr/work/gadget-kit-fp-input/native/first_person/zoom_gain_math.h:1).
- register capture thunk: [native_ads_input.S](/home/goose/Documents/Codex/2026-11-07/tr/work/gadget-kit-fp-input/native/first_person/native_ads_input.S:1).
- experimental status and limits: [first-person-zoom-input.md](/home/goose/Documents/Codex/2026-11-07/tr/work/gadget-kit-fp-input/docs/first-person-zoom-input.md:1).
- earlier scoped feasibility audit: [tfp-private-preset-feasibility.md](/home/goose/Documents/Codex/2026-11-07/tr/work/tfp-private-preset-feasibility.md:1).
