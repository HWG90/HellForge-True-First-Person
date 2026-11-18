# Native ADS reentry and camera transition mechanism

Build 25480438, offline investigation,2026-11-09. All addresses below are **game.dll RVAs** in the mapped original capture `~/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll`, SHA256 `642b0aac0b4f577054993a965054827455ef143ced9895b2dfd09d1ecd433a37`. No live attachment, game call, new hook, native invocation, product edit or installed-file change was made. The accompanying `native-lifecycle-reentry-evidence.json` contains 25 procedure disassemblies, explicitly decoded jump tables,51 authored transition rows and bounded caller windows. Indirect calls and gameplay labels remain unknown where stated.

## Answer to the native-feel question

There **is** a native camera transition system: authored request-pair/profile blend settings, per-request timing, easing, position/quaternion/FOV blending, retirement into a captured current camera pose and publication of a saved fallback. This is a more promising native mechanism to investigate than another eye pin or synthetic input integrator.

There is still **no qualified native route that preserves a permanent non-ADS FPS rig while smoothly releasing native ADS**. The game deliberately constructs FPS requests with an immediate blend packet. Native aim release clears its transient desired-camera state; the reentry predicate requires the native aim/FP flag to remain set. Saved aiming-view preference does not grant a hip-FPS camera state.

A concrete possible source of exit discontinuity is now visible in code: **appending a new zero-packet kind 6 can retire an unfinished native exit blend on the next queue update**. That mechanism is proved conditionally by the consumer; it is not yet established as the cause of the user's visible snap. Adding a nonzero packet is also not a drop-in fix: the native queue can evaluate multiple requests, while current mod policy ownership is primarily tied to its top request. Actual evaluator identity and retirement must be proved first.

## The desired-input function and reentry predicate

The containing function for the previously isolated gate at `0xa41c0b` begins at `0xa41100`. Its chained unwind metadata links the interior fragment back to that entry; it is not a separate tiny function beginning at the final gate.

`0xa41100` operates an avatar aim-state subobject. Its caller at `0xa3e735..0xa3e73f` passes avatar-state base+`0x3b4`. The outer update at `0x82a0af..0x82a0d4` derives a row as manager+index*`0x1238`+`0x53d8b0`, and passes row+`0x50` into that avatar-state update at `0x82a497..0x82a4a0`. After independently proving the manager/index/current avatar, the corresponding candidate aim-state block is therefore manager+index*`0x1238`+`0x53dcb4`. Its `+0x10` avatar ID should close against the current avatar before any runtime interpretation. This is an address derivation for read-only observation, not call/write authority.

| Field or helper | Exact observed role |
|---|---|
| Aim-state `+0x0` | Held-weapon bookkeeping ID, compared with the current wielded weapon |
| `+0x4` | Native transition progress clamped to0..1; advanced/decreased using frame delta divided by avatar aiming transition data |
| `+0x8` | Auxiliary input/transition float; exact semantic name remains unproved |
| `+0xc` | Pending transition byte |
| `+0xd` | Transient desired native camera byte consumed by the final controller gate |
| `+0x10` | Avatar entity ID |
| `0xa40b50` | Reads avatar flag 82 (`flags_qword1 >>18 &1`) to select an active aiming-state path |
| `0xa41c80` | Native aiming-state entry helper; sets flag 82, propagates native state-change effects and animation-related calls |
| `0xa41ed0` | Native aiming-state leave helper; clears flag 82 and, when leaving succeeds, clears desired byte `+0xd` at `0xa420aa` and clears flag 48 at `0xa4215a..0xa4218a` |
| `0xaa0aa0` / `0xaa0bb0` | Generic avatar flag-set / flag-clear helpers, respectively. Their semantics were checked from OR/AND writes; names are analyst labels |

The native aiming-view preference getter at `0xa426a0` first checks weapon FPS eligibility, then selects the same storage routes used by current C `src/preferences.c:10-44`: avatar settings pointer+`0xf0` chooses modes 1/3 for weapon rows+index*2, or modes 0/2 for player+index*`0x38`+`0x2ec`. Native toggle helper `0xa42890` changes that preference and its persistence bookkeeping. These are saved **aiming-view** choices. The transient `+0xd` remains subject to native transition/input/action logic and is cleared on native aiming-state leave.

The final gate at `0xa41c0b..0xa41c33` compares that transient desired byte with controller `+4`, entering or exiting only when different. Invalid/unsupported held gear clears desired `+0xd` at `0xa41c04`.

The open reentry predicate at `0x12b2e20` is now resolved to:

```
local avatar available
AND avatar flags_qword0 has bit48
AND avatar flags_qword1 does NOT have bit44
```

The second condition's bit44 is overall avatar flag 108, or `flags[13]&0x10`. This predicate performs no writes and reads no separate persistent-FPS preference. Its bit48 is the same native flag used by controller update. The current mod names that observation `aiming`; the exact authored flag name is not recovered here. Native leave clears it, so holding or restoring the saved view preference does not keep hip FP alive after native aiming ends.

## Request adoption, retirement and action reentry

The native route is:

```
native aim update -> transient desired byte
 -> compare controller+4
 -> controller_enter / controller_exit
 -> native FPS request constructor / native FPS exit
 -> native queue evaluation and post-camera FPS rig update
```

`controller_enter` `0x12b29b0` requires controller `+4` false, mask dword zero, controller `+5` false and an eligible weapon. It sets `+4`, sends native event hash `0x4e02de4f`, calls constructor `0x127de20`, then directly evaluates the selected current request with dt 0.

The constructor permits **only a current top kind 4** (`0x127deb4..0x127deb8`). Existing kind 6 therefore is adopted/evaluated without appending another FPS request. There is no special constructor mode or duration argument for “persistent hip first person.”

`controller_exit` `0x12b2ae0` clears `+4` and calls `FP_EXIT` only if it was set. `FP_EXIT` `0x127e9d0` acts only on current top kind 6. It performs native near/body/rig/presentation restoration, calls request retirement helper `0x12771d0`, copies the saved fallback record at context+`0x1f00` into queue[tail], advances tail modulo32, then calls native queue update `0x1279190` and post-camera rig update `0x127a420` with dt 0.

Retirement helper `0x12771d0` does not simply delete a record: it changes its kind to0, saves the current context position at request+`0xb8`, quaternion at `+0xc8`, and current native camera vertical FOV at `+0xc4`. These become an outgoing pose for the native blender. Request kind 0 here is a retired/current-pose blend source, not a persistent FPS state.

Native action suppression helper `0x12b2ba0` ORs mask bits, clears active controller `+4` and calls FPS exit. Unmask helper `0x12b2c00` removes only its supplied mask bits; when the remaining mask is zero and `0x12b2e20` succeeds, it resolves the held weapon and calls the normal controller entry. Suppression helper `0x12b2b40` sets controller `+5` and exits active FPS. These are explicit native lifecycle owners. Clearing masks or repeatedly entering through them is not a qualified camera solution.

Validated direct-call sites show action families using masks2,8,`0x10`,`0x20`,`0x40`, with corresponding clear paths. The JSON preserves the caller roots and local instruction windows. **Those address families are not sufficient to label a particular mask as vehicle/seat/dive/interact.** Actual vehicle entity/seat identity and the ordering of its mask, camera request and avatar flags remain live observation requirements.

The ordinary native camera-kind selector at `0x1280f30` returns4 for an avatar-manager member,5 for a second entity-manager domain,10 for a third domain, or13 when unsupported. It never returns6. The non-avatar domains are not assigned vehicle class names here without additional evidence. Native first person is an explicit controller/request overlay, not a generic default mode returned for all camera-owning entities.

## Native blend packet producer and exact parameters

`0x1265e20(out, old_request, new_request)` produces20 bytes. It normalizes kind 7 to kind 4, extracts profile IDs from kind 4 `+0xc`, kind 6 `+0x80` or kind 7 `+0x38`, then queries `0x1265d60` using packed16-bit keys:

```
(from_kind <<48) | (from_profile <<32) | (to_kind <<16) | to_profile
```

It tries exact and wildcard combinations (`0xffff`), then returns the default packet if none match. The lookup uses a128-bucket table populated by `0x1265b50` from51 static rows at `0x21c7ae0`, stride36. Each row has four32-bit key fields followed by its20-byte packet. The static bytes and decoded rows are retained in JSON; current live table/data identity has not been sampled.

| Request offset | Type | Consumer-established role |
|---|---|---|
| `+0xa4` | uint32 | Curve selector |
| `+0xa8` | float | Duration in native update-delta units |
| `+0xac` | float | Progress, which may start nonzero |
| `+0xb0` | float | Delay countdown before progress changes |
| `+0xb4` | byte | Reverse/outgoing progress direction; remaining bytes are not assigned names |

Duration seconds are consistent with the native frame-delta contract, but no live time measurement was performed; exact arithmetic uses the native dt passed to the consumer. Default packet at `0x21c7af0` is curve 5, duration 1.5, initial progress 0, delay 0, reverse 0. There are no kind 6 keys in any of the51 rows.

| Hypothetical direct producer lookup | Matching special rule | Packet result | Actual FPS path |
|---|---|---|---|
| 6 ->4, any 16-bit profiles | None | Default curve 5 /1.5 /0 /0 /0 | `FP_EXIT` does not perform this lookup; it copies the saved fallback |
| 4 ->6, any 16-bit profiles | None | Same default | FPS constructor bypasses this producer and writes a zero packet |
| 6 ->6, any 16-bit profiles | None | Same default | Existing kind 6 fails constructor's kind 4 gate; no new request/packet |

The normal constructor `0x127bbd0` calls the producer at `0x127c69b`, stores the returned packet at new request+`0xa4..0xb4` (`0x127c6f4..0x127c6f8`), publishes the request, and saves a complete copy at context+`0x1f00` (`0x127c9bf..0x127ca48`). Other native conditions can override the packet to zero before publication (`0x127c858..0x127c87f`); the report does not assign an invented name to those conditions.

The FPS constructor instead explicitly zeros curve, duration, progress, delay and reverse at `0x127e831..0x127e852`. This selects the immediate native curve. No public caller parameter controls that packet in the inspected wrapper/constructor.

### Is the saved fallback already completed?

**Not generally.** The saved fallback is a copy made when the normal request is constructed. Native queue timing updates operate on context+`0x200`+slot*`0xe8`, not that saved record. A queued normal request can finish while its saved fallback still retains its original packet progress. `FP_EXIT` copies the saved record without recalculating a6->4 transition or resetting the packet.

Some authored rows intentionally have initial progress equal to duration: rows 40..44 use duration 0.5 and progress 0.5. Those are complete at birth. Other rows start at0. The current fallback's kind/profile, packet and any later native/foreign producer must be read before concluding either that exit reuses a completed packet or that it gets a fresh timed blend.

## Consumer, easing and actual evaluation schedule

`0x1279190` first iterates every queued request from head to tail. Its type dispatch invokes kind 6 evaluator `0x1286330` at `0x1279643`. It then handles each record's delay and progress (`0x12797bc..0x1279818`): countdown delay; otherwise add dt toward duration, or subtract dt for reverse/outgoing packets.

Curve0 sets blend weight 1 immediately. Curve5 computes:

```
t = clamp(progress / duration, 0, 1)
weight = 3*t*t - 2*t*t*t
```

The curve 5 instructions at `0x1279cc8..0x1279d1b` feed the shared cubic terms at `0x1279bce..0x1279be5`. Reverse packets use1-weight. Other selectors include immediate/step/linear and additional polynomial variants; the JSON retains their switch map, but no unnecessary authored names are invented.

The blend target block begins at request+`0xb8`. Helper `0x1275fa0` blends accumulator position at+0..8, vertical FOV at+`0xc`, and quaternion at+`0x10..0x1c`; its first position/FOV arithmetic is explicit at `0x127601b..0x127608d`. The queue consumer eventually calls native CameraAPI FOV, local position and local rotation setters at `0x127a11a`,`0x127a135`,`0x127a14e`. This is native camera blending, without synthetic look input or skeletal pose replay.

There is a **separate positional lag stage** after request blending. Its coefficient is zero for active kind 6 and certain completed transitions involving kind 6 (`0x12799f1..0x1279a21`); the zero-coefficient branch selects final position alpha 1 at `0x127a04c..0x127a0b1`. This is not the request duration field. Editing one stage would not establish control over the other, and applying generic lag to native aim would change native camera response.

After the camera update,`0x127a420` separately loops all queued kind 6 records and calls rig update `0x1288920` at `0x127a51f`. Thus a retained transition can involve more than one request evaluation and FPS rig evaluation. The native FPS evaluator also invokes FPS exit when its avatar cannot be resolved (`0x12863dd..0x12863e1`). Top request identity alone is not necessarily the identity of the request currently being evaluated.

### Conditional snap mechanism and ownership hazard

The newest zero-packet FPS request has progress 0 >= duration 0. At `0x127987e..0x12798c3`, if more than one request exists, the consumer can advance head directly to that newest completed request, discarding older exit-transition records. Since active FPS also bypasses the separate positional lag, an early mod reentry can end the native handoff abruptly. This is a precise conditional mechanism, not a claim about current screen output or the exact frame when the mod reenters.

Conversely, assigning a nonzero packet to a new FPS request could retain old and new requests concurrently. The current C FOV/pose policy's top-snapshot/lease proofs do not by themselves prove which native request produced a callback. Before any experiment with native packet duration, establish actual evaluator argument identity, call order, same-stack/evaluation association, request and rig lifetime, interaction with native exit and foreign slot/data writers. This investigation grants no write/call authority.

The generic camera blend also does not prove a cohesive weapon/hand transition: native FPS exit restores rig/body visibility and native state before the generic world-camera update. Keeping world motion smooth does not automatically retain the complete FPS model or native grip/reload animation across that cleanup.

## Best next read-only evidence

Capture both vanilla native ADS exit with F11 off and the same sequence with TFP enabled, with copied bounded records and no new controls:

1. All live queue slots, head/tail/top, request kind/avatar/weapon/profile, packet `+0xa4..0xb4` and pose block `+0xb8..0xe7`. Capture the saved fallback separately before and after exit. Record which producer last published each value rather than assuming a6->4 lookup occurred.
2. Proved aim-state block `+0x4/+0xc/+0xd/+0x10`, flag 48, flag 82, flag 108, controller mask/bytes4/5 and saved aiming-view preference. Correlate the native clear/set helpers and controller transitions without writing them.
3. Actual evaluator request argument and post-rig argument, thread, native evaluation/frame/update identifier and native dt. A top-only20 Hz game-thread sample is useful context but cannot prove these relationships or catch every transition frame.
4. Exact frame where new kind 6 is appended and where native queue head changes. Compare it with native exit-blend progress and the first visible jump. Separate custom150 ms positional residual, native request blend and native lag-stage effects.
5. Root0, branch5, camera50, actual camera Unit local/world pose and the complete current gun/hand/attachment chain. Retain the existing end-to-end world-view caveat; pointer forwarding alone is insufficient.
6. Native action/vehicle identity: seat entity/role, mask family, flag 108, desired byte and native camera kind/order. The validated masks/caller addresses are leads, not semantic labels. Confirm restoration/reentry after the native action ends and without manipulating its masks.

Rejected shortcuts: treat saved ADS preference as permanent hip FP; pin desired `+0xd`; clear native masks; repeatedly call controller entry through an action; skip native exit cleanup; assume saved fallback equals a completed queue slot; replace the FPS zero packet without evaluation ownership proof; smooth native input to cover a camera-state discontinuity. None has been qualified by this static investigation.
