# Read-only native phase observer prototype

Offline result: **421,101 capture checks over 36,111 private callbacks, plus 74 separate fake-memory binding checks; zero failures.** The reusable prototype is entirely C11 in `work/native-phase-observer/phase_capture.c/h` and `phase_bind.c/h`. Private test assembly appears only in `oracle.S`, as an independent caller oracle. No product `src/`, runtime build, current DLL, installed file, native game function or live game process was changed or invoked by this work.

The earlier `work/caller-context-proof` remains intact. Its 432,207 recorded checks supplied the two-frame unwind design; those historical checks are not added to the new total or presented as newly rerun. Its source/receipt hashes are recorded separately.

## What the prototype establishes

`phase_capture_prepare` (`phase_capture.c:72`) snapshots the helper and named callback's loaded-PE function identity: image, RUNTIME_FUNCTION record, bounded unwind information and 16 entry code bytes. Both functions must belong to the same loaded image. The accepted own metadata format is version 1, non-chained, image-backed unwind data. Missing, dynamic, chained, leaf-without-metadata or unsupported records fail closed.

The noinline `phase_capture_take` (`phase_capture.c:144`) uses Windows `RtlCaptureContext`, `RtlLookupFunctionEntry` and `RtlVirtualUnwind` through C. It captures a context in the helper, virtually unwinds exactly that helper and the named noinline callback, and **stops before unwinding the native caller**. The resulting PC, RSP, RBX and R15 are the caller's restored nonvolatile state; it does not assume callback compiler locals still contain incoming RBX/R15.

Before publication it requires:

- The captured helper PC and intermediate callback PC resolve to their expected exact own function/unwind identity; entry code and unwind bytes still match, with a terminal identity check as well.
- Each RSP advances within the current TIB stack interval, by no more than 64 KiB per own frame; the reported establisher frame lies within that interval. The TIB range is bounded to 128 MiB.
- The final PC equals the supplied exact expected SDK return PC, and recovered native RSP has Win64 call alignment.
- The capture is not reentered on the same thread. Exceptions inside the protected walk are caught, the private TLS guard is reset, and failure leaves the published `caller` tuple entirely zero.

The helper exposes diagnostic local/callback register values separately from the successful caller tuple. They are evidence about compiler reuse, not native authority. It performs no speculative leaf-frame popping, native backtrace, naked function, fixed-register local or handwritten product assembly.

## Two callback families and three exact original sites

The fixtures model two ABI families: `(uint32 UnitRef,uint32 node)->const float*` UnitAPI getters, and `(void* Camera,float FOV)->void` CameraAPI FOV setter. The original target profile is explicit:

| Phase | Native call / return | SDK slot / original implementation | Native phase identity |
|---|---|---|---|
| PRE native6 | GAME`0x12865fc` /`0x12865fe` | UnitAPI`+0x90`, EXE`0x1fd220` world-matrix getter | R15 is a candidate actual request, with context/index/dt spills |
| LATE native6 | GAME`0x1287a41` /`0x1287a44` | UnitAPI`+0`, EXE`0x1fc450` local-position getter | Same native frame family; expected current FPS rig/node 50 |
| Composed camera FOV | GAME`0x127a11a` /`0x127a120` | CameraAPI`+0x80`, EXE`0x30d990` FOV setter | RBX is candidate camera context; **not one particular active request** |

`phase_bind.c:7-20` contains the call/return RVAs, exact native call bytes and original SDK implementation prefixes. It also checks original image stamps/sizes, the current native API root/table/slot target and the appropriate native caller prologue. The native6 spill/assignment witnesses are separately guarded:

| Native witness | Exact bytes | Derived spill |
|---|---|---|
| GAME`0x1286370` | `48894d804c8bfa` | Context at nativeS+`0x80`; R15 receives original RDX request |
| GAME`0x128637d` | `f3440f115da0` | float dt at nativeS+`0xa0` |
| GAME`0x1286430` | `4889442448` | **8-byte** avatar-index spill at nativeS+`0x48` |

NativeS is caller RSP before the SDK call, equivalently SDK callback entry RSP+8. The mapped native prologue gives RBP=nativeS+`0x100`. Capturing real compiler-restored RSP closes the previous all-C ABI feasibility gap; it does not establish every native register alias/liveness or active ownership condition in gameplay. The native6 post-rig evaluator is a different context and remains excluded.

## Separate phase/ring/spill binder

`phase_bind_copy` (`phase_bind.c:44`) uses only `FpMemory` protected reads and existing pure snapshot/graph validators. It has no hook, recording loop, SDK call, target setter or retained engine reference.

For PRE/LATE it checks bounded native stack footprint and spills, current player/weapon/Unit identity, expected Unit/node, the recognized FPS rig and actual R15 membership in the current 32-slot request ring. It requires the actual record to be kind 6 for the current avatar/weapon/body, copies the full `0xe8` record, and closes current request/controller/flags, descriptor, Unit objects, rig, SDK API root/table/slot, record bytes and spill values. Index 0 and wrapped index 31 are covered. A request can be valid and **not top**; the output records `actual_index` and `actual_is_top` separately.

For FOV it binds recovered RBX to the current context and the forwarded Camera argument to that context's camera. It marks the result `composed=true`, leaves `actual_request=0` and `actual_index=UINT32_MAX`, and records queue context rather than inventing an “active evaluator request” from R15 or the top snapshot.

The binder tests use synthetic caller tuples and fake protected-memory blocks. They are intentionally separate from the Windows unwind oracle. Their combination has **not** been exercised against a real native game frame. Stable closing values also do not prove one game/render evaluation or rule out asynchronous ABA changes.

## Private verification

The runner builds separate translation units without LTO, with assertions enabled, for O2, O3 and O2 retaining frame pointers. A fourth variant compiles the production helper body **without** the fixture-only observation hook. All use a real optimized Win64 PE and the existing isolated Wine test prefix.

| Variant | Capture checks | Callback calls | Callback RBX reused | Callback R15 reused |
|---|---:|---:|---:|---:|
| O2 |105300 |9030 |9009 |6006 |
| O3 |105300 |9030 |9009 |6006 |
| O2 with frame pointers |105300 |9030 |9009 |6006 |
| Production helper O2 |105201 |9021 |9003 |6002 |

The private assembly oracle independently supplies known native PC/RSP/RBX/R15 and verifies returned nonvolatiles. C callbacks still forward the original getter pointer or exact FOV value once. Pressure callbacks deliberately force compiler nonvolatile reuse; the recovered tuple must match the oracle despite differing callback-local registers. Copied oracle/recovered sample values, command lines, compiler/Wine identities, source/binary hashes, disassemblies and PE unwind metadata are retained in `work/native-phase-observer/fixture-evidence.json` and neighboring files.

Negative checks include wrong return PC±1, wrong callback, corrupted expected helper/callback unwind metadata, wrong helper record, absent metadata/bad arguments, same-thread reentry, a deliberate private exception followed by successful same-thread capture, and a genuine escaped C stack frame larger than 64 KiB. The fixture joins worker threads before reading their count storage; a failed join exits the private process instead of racing live worker data.

The binder passes 37 cases at O2 and 37 at O3, covering non-top versus top identity, wrapped ring, explicit FOV composition, wrong PC/dispatch, insufficient stack, failed capture, unreadable/mismatched spills, nonfinite dt, each changed spill-store code witness, misaligned/out-of-ring request, changed request weapon and mutation during copy. No private game function is executed by these checks.

Independent review of the final capture/binder found no remaining actionable issue within this offline prototype. That review checked the receipt and final source identities; it did not rerun the fixtures or qualify gameplay timing. The earlier worker-join race and missing exception recovery exercise were corrected before the final recorded run.

## Integration contract and remaining qualification

This prototype is not included in `build.sh` and is not installed. Any future read-only integration must satisfy all of these contracts:

1. Policies and input/output buffers are valid, disjoint, caller-owned storage. Policies, own loaded PE code and unwind metadata remain immutable while callbacks run; the DLL is pinned and callback/cleanup lifetime is owned. Initial output clearing and trusted argument reads precede the helper's SEH region. This API does not make arbitrary invalid pointers safe.
2. A successful result is trusted immediate-use data from that callback, on the same thread. It is not an unforgeable capability, across-frame handle, game generation token or authority to write. Any surrounding lifecycle module must separately reject old publication generations, delayed callbacks and changed owners.
3. Exactly the helper and expected callback own frames are present. Extra wrappers, trampolines, tail calls, inlining, split/chained metadata, a different compiler/linker or a different callback body require fresh evidence; unsupported metadata is refused. An expected return address must come from the exact build profile, not from an untrusted sampled value.
4. A future SDK pointer interception requires its own current slot/original-target lease and original ABI/caller witnesses. `expected_dispatch` must match both the captured named callback and the current owned slot. The prototype installs nothing and grants no authority to add a body detour or replace current dispatch.
5. Revalidate the complete native caller/site, static register liveness/aliases, spill layout, modules, VM/player/weapon/rig lifetime and current native ordering before a live read-only capture. The existing short code witnesses do not establish a full function fingerprint or all future native writes.
6. Measure capture cost before selecting a cadence. Per-capture VirtualQuery/ReadProcessMemory and metadata checks are bounded algorithmically, but no per-frame latency/FPS budget is qualified. Do not run this at every unrelated UnitAPI call or hold mod locks across unqualified callbacks.
7. Use bounded copied records to correlate PRE, LATE and composed FOV with actual native evaluation IDs, native request/rig update arguments, queue/fallback blend packets, body/weapon state and root 0/branch 5/camera 50/actual camera world pose. A top snapshot or 20 Hz pre-update sample alone does not prove the same evaluation. Native6 post-rig, actions/vehicle ordering and foreign writer/ABA behavior remain live research questions.
8. Preserve exact native getter/FOV forwarding on every failed qualification. No input scaling, blend-packet write, request lifetime extension, animation replay, look integration or camera pin follows from this offline result.

The offline gap is narrower now: a strict C helper can recover the actual native caller's nonvolatile context through its own validated frames, and a separate pure reader can distinguish an actual queued native6 request from top/composed-camera state. Real-game phase qualification, evaluation pairing, callback drain and safe native blend/input mutation remain unproved.
