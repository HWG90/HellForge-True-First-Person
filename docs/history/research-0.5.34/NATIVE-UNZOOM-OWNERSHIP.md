# Native unzoom ownership and lens-history feedback

Status: static investigation of the original HellForge build **25480438**.
No process was attached, no game function was executed, and no installed or
product file was changed. The candidate described below is **not implemented
or qualified in game**. This closes the bounded follow-up to
[NATIVE-ZOOM-STATE-ROUTES.md](NATIVE-ZOOM-STATE-ROUTES.md).

## Result

Neither the packed avatar bit nor request+0x8f is an independent native
unzoom switch. The first is regenerated from shared avatar state and published
to non-camera consumers; the second is a derived **reload-active** value that
also changes the camera/weapon presentation. Both are read after the early
input ratio, so changing their result in that evaluation would not undo the
zoom factor already applied to input.

There is a more targeted lead: **request+0x84 is the private previous/current
lens scalar read before input**. The existing CameraAPI+0x80 hook runs after
native code has updated that scalar and produced its output lens. Coupling it
to the exact lens actually displayed by that hook could make the **next**
evaluation's input ratio agree with the displayed field of view, while keeping
the real optic selection, device sensitivity, native steering addition and
orientation solver. It needs no additional dispatch hook in principle.

This is not yet a safe complete solution. The same scalar is also native lens
interpolation history and is copied into depth-transition history. Replacing
it with a wide displayed lens every frame can repeatedly restart the native
transition toward the selected narrow lens and change weapon-depth blending.
There are also exact queue/context ownership conditions that the current FOV
callback does not prove. **Promising input coherence route; unresolved depth
feedback and live ownership.**

The evidence file contains 26 exact byte-range witnesses, original image and
source hashes, phase ordering, and the remaining live predicates:
[native-unzoom-ownership-evidence.json](native-unzoom-ownership-evidence.json).
Its collection script is
[collect_evidence.py](../../work/native-unzoom-ownership/collect_evidence.py).

## Packed avatar bit0: producer, publication and other effects

In root `game+0x829e90`, the main avatar row is established at
`0x82a0af..0x82a0dd`:

```text
main = avatar_owner + 0x53d8b0 + avatar_index * 0x1238
packed = avatar_owner + 0x547830 + avatar_index * 0x78
```

The main row's **byte+5** supplies packed bit0. At `0x82aad9` R14 is restored
to the main-row pointer. `0x82aae1` checks main+0x3d, with additional local
flag/vector-result branches through `0x82aaf5..0x82aaff`. The branches write
main+5=1 at `0x82ab18` or main+5=0 at `0x82ac50`. They also select native vector
parameters and store a related computed scalar into main+0x11ac. The preceding
path calls `0x834170` and tests records of stride0x2c through `0x834450`.
This pass does **not** establish a semantic name for these main-row bytes or
the vector/volume condition. They are not the C mod's `FP_AVATAR_FLAGS`
at avatar+0x53e880 (`src/profile.h:22`).

At `0x82c068..0x82c080`, packed bit0 is set/cleared from main+5. The final packed
word store is `0x82c15e`. The same update publishes property **0xcd889dbc** via
`0x82c142`/`0x82c16c -> 0xfd97e0`, alongside other packed avatar fields.

There is a concrete non-camera consumer, not just a possible alias:
root `0x82e2a0` tests bit0 at `0x82f424`, chooses native unit/event paths, then
copies it at `0x82f74e..0x82f759` into the separate per-avatar presentation byte
`avatar_owner + index*0x1b8 + 0x546a8f`. A HUD path also reads the packed bit at
`0x17b7687`. Initialization and full-row restoration exist at `0x83a7be` and
`0x83abe7`. The bounded direct-displacement/reference material is retained in
`work/native-unzoom-ownership/mainrow-field-refs.json` and
`work/native-zoom-routes/native-unzoom-gate-refs.json`.

Consequently this bit is not privately owned by one camera request. Supplying
it globally would forge broader avatar presentation state and compete with
its ordinary producer. Supplying it only after orientation would still be too
late for the same evaluation's input ratio.

## Request+0x8f: reload-derived state with pose effects

The native query at `game+0x776010` receives a reload manager and weapon entity.
It resolves the weapon's reload definition through `0x4fd220`, obtains the
required ability at definition+4 (`0x776198`), resolves the weapon owner's
active ability with `0x7cbe60`, and compares the two at `0x7761aa`. The active
ability query returns zero when its row+0x10 active flag is false and otherwise
returns row+0. This is an ability equality query; it does not simply read a
visual camera flag.

The locally installed HUD++ profile provides a corroborating name. Its
`hudplusplus/sources/hudplusplus/lua/56e46a6dc1df0a13.lua:1752` labels the
corresponding helper `reloading`; line3266 assigns Steam RVA0x781720;
lines20325 and20361..20375 use it from `game.reload` with the reload manager.
The original and corresponding Steam helper have **137 instructions / 437
bytes**, with no differences after masking only relative control-flow
immediates and RIP-relative displacements. The complete comparison is
`work/native-unzoom-ownership/reloading-comparison.json`. This is a
source-correlated inferred identity, not a recovered native debug symbol.

The native first-person evaluation performs:

| Site | Effect |
| --- | --- |
| `0x1286a96` | Resolve current weapon from wielder |
| `0x1286a9b..0x1286aa6` | Load reload manager at global0x3326a70, call0x776010 |
| `0x1286aab..0x1286ab4` | Compare with and overwrite request+0x8f |
| `0x1286abb` | Reset request+0x88 transition weight on change |
| `0x1286acd` | Recalculate presentation distance on reload exit |
| `0x1286ad9..0x1286afa` | If avatar bit0 or request8f, choose wide target lens and multiplier1 |
| `0x1287adb..0x1287ba2` | Use request8f in camera/weapon position blending and vector selection |

Setting request8f would fake a reload-camera state. It would not start the
weapon's actual gameplay reload, but it would select that presentation branch
and be replaced by the native ability query on the next evaluation. Globally
changing helper0x776010 would affect its other native callers as well.

## Request+0x84 lifecycle and exact evaluation order

The constructor `0x127de20` builds a temporary request at RSP+0x60. At
`0x127e114`, CameraAPI+0x78 returns the actual camera lens; `0x127e11b` stores it
to temporary request+0x84. Request+0x80 receives the native preset,
request+0x88 starts at1, and request+0x8d starts at0. After helper0x1282a40,
`0x127e874..0x127e8f2` copies the full request to the context ring with stride0xe8.

Within native kind6 `0x1286330`, the established sequence is:

1. **Pre-input preparation:** helper0x1282a40 at `0x1286535`. Its inspected body
   uses request identity/vector fields and writes +0x60/+0x68; no direct
   +0x84/+0x8f/+0x8d store was found in that body. This is a bounded direct-field
   observation, not proof about every callee/alias in the game.
2. **Input:** `0x1286851` reads request84, converts to degrees and divides by
   settings+0xac408 (normal FOV). It multiplies the input axes by this ratio,
   applies the native mouse/pad scoped sensitivity, then adds native steering
   through0x128d820 at `0x12868c4`. The effective order remains `R*D*U + S`.
3. **Native orientation:** solver0x1277250 runs at `0x1286a21`.
4. **New target:** only now does `0x1286a37` read avatar bit0. The selected optic
   multiplier is obtained at `0x1286a46`; request8f is derived as above. Native
   preset-lens helper0x1277780 and its copied preset curves shape the target.
5. **History update:** `0x1286ca8` rereads request84 as previous lens. Depending
   on request8d, the scalar approach helper0x173cb30 moves toward the target by
   speed*dt, capped at the target. `0x1286d09` writes the result to request84;
   `0x1286d22` writes the same result to native stack+0x60.
6. **Depth/pose evaluation:** target=stack+0x5c, current=stack+0x60 and
   prior=request+0xa0 determine native depth interpolation at
   `0x12873b3..0x1287423`. The mod observes/corrects paired presentation later
   within this evaluation.
7. **Output:** `0x12887fb` publishes stack+0x60 to request+0xc4, the FOV in the
   request's output pose at +0xb8. `0x1288818` refreshes request8d from the
   queue-transition query0x127ba60.
8. **Composition/dispatch:** the outer dispatcher composes the queue's output
   poses, then calls CameraAPI+0x80 at `0x127a11a`, return **0x127a120**.

Additional direct request84 history use: presentation-distance helper0x1278cc0
reads it at `0x1278f9e`, stores it to request+0xa0 at `0x1278fa4`, shifts old
distance90 to9c, and stores the new distance90. Thus it influences the reference
for later depth transitions, not just the current view angle. This report does
not establish an exhaustive absence of other aliased readers/writers.

## Reusing the existing post-FOV hook

`src/fov_policy.c:18..49` already filters CameraAPI+0x80 on actual camera and
returnPC0x127a120. It uses the normal FOV in hip TFP and sends the resulting lens
to the original setter at line47. `src/lens.c:5..34` may apply a 5% shoulder
zoom over120ms and a180ms ADS handoff. Therefore the exact applied lens may be
between the nominal hip and ADS values.

The original outer dispatcher retains its context in RBX from `0x12791c2`.
At `0x127a0c0` it reads the actual camera from `[RBX]`; at `0x127a115` it reads
the composed lens from RBP-4. The exact setter returnPC matches the current
hook. Original EXE+0x30d990 directly stores its supplied lens at camera+0x34,
clears camera+0x50, and tails to0x4cfab0, which enqueues the lens parameters.
The getter at0x30d980 simply reads camera+0x34. No lens clamp appears in these
two setter/enqueue bodies. This does not exclude a foreign hook/owner or later
camera update.

If request identity and ownership were qualified, a post-native-write
`request84 = actual_displayed_lens` would make the next ratio:

```text
R[n+1] = displayed_lens[n] * radians_to_degrees / normal_FOV_degrees
```

Steady unzoomed hip gives R approximately1. Native filtering U, selected device
gain D, steering addition S and orientation authority remain in their native
order. No selected-optic bits, target steering state or orientation need be
written. Keeping the S expression untouched is not proof that every future
target-steering sequence is behaviorally unchanged, since the player's view
and input response intentionally differ.

The applied shoulder/handoff lens, rather than the final wide target, must be
used if coherence with the displayed view is the aim. The first activation,
an input-time ADS transition or a settings change can retain an old ratio for
one evaluation. This hook cannot retroactively repair that evaluation. On ADS
entry, retaining the last displayed hip lens as history is coherent;
restoring an old pre-hip narrow lens there would recreate a discontinuity.

### Exact ownership limits

The setter is a **generic composed-camera** boundary. The outer dispatcher
updates each queued row first, can retire rows, and then copies head output
B8..C7 at `0x1279988` into its local result. C4 becomes RBP-4. With additional
rows it blends output poses before dispatch. An empty queue can obtain the
current camera lens instead. Therefore returnPC0x127a120 alone does not
identify a kind6 request, and the snapshot's newest request need not be the
row supplying the composed result.

Current `fov_policy.c:28` checks kind6/nonempty/local body but **does not prove
one active row or recover the actual caller's RBX context**. A future bounded
trial would need a sole active local kind6 row in the exact context retained
by that caller, an unchanged generation/identity, callback frame/thread
coherence, and actual camera getter agreement after dispatch. The prior strict-C
unwind fixture is relevant to recovering the caller's nonvolatile registers,
but does not qualify this actual game callback or its threading/reentrancy.

Cleanup would also need a per-request ownership record and the fresh native84
seen before each overwrite. A frozen activation-time backup is insufficient:
the native value changes with zoom, ADS and request reuse. Conditional restore
must never overwrite a newer native value or a retired/reused row. A matching
value/CAS is only one predicate; it does not alone establish lifetime or
concurrency safety. No restoration mechanism was implemented here.

## Depth feedback: concrete consequence, not a generic warning

`src/depth.c:219..234` captures the native frame and request80..a3 at the late
UnitAPI getter callback; `src/pose_policy.c:105..116` restricts that observation
to returnPC0x1287a44. `depth.c:254..268` reads current/previous distances90/9c,
prior lensA0, target stack5c and current lens stack60. It requires request84's
bits to equal stack60, then derives:

```text
t = clamp(abs(target - current_lens) / abs(target - prior_lens), 0, 1)
native_depth = (1-t)*distance90 + t*distance9c
```

The mod's correction follows the corresponding widest-optic reference blend,
then applies the paired carrier correction; it does not pin individual bones.

A write at the outer FOV setter occurs **after** those observations and paired
corrections. Within-frame request84==stack60 can therefore remain valid. That
does not mean depth behavior remains unchanged. On the next native evaluation,
interpolation rereads the overwritten request84. With displayed wide lens A,
selected target N != A and steady dt, the new native lens is repeatedly a
one-step approach `approach(A,N,speed,dt)`, rather than a transition that
continues from its preceding native result toward N. For a stable target the
native result and t can stall at a repeatable intermediate value. Native
body-depth blending and the mod's reference blend can then remain at that
different t instead of settling to the ordinary current distance.

On zoom changes or reload exit, helper0x1278cc0 can copy the coupled84 intoA0
and old distance90 into9c, changing the next depth transition's baseline and
the mod's cache signature. These are real history feedback paths. Their
magnitude, visible result and first/last-frame behavior remain unmeasured.
The existing guards can pass while this behavior changes.

Request8d does not offer an independent workaround: helper0x127ba60 makes it
false only while the last transition is unfinished **and** queuecount>1;
a steady sole-kind6 request normally makes it true. Forcing it false, faking
reload8f or rewriting A0 would alter additional native contracts, not resolve
the evidence gap. None was attempted.

## Bounded decision and next qualification requirements

Reject the shared avatar-bit and fake-reload routes. Retain the post-FOVwrite
idea as an **offline hypothesis**, with the following unproved predicates:

- Exact dispatcher context and sole-kind6 request identity at the callback;
  ring generation, thread/frame ownership, reentrancy and competing owners.
- Actual displayed/getter lens equality, first activation and queue-overlap
  handling; native ADS enter/exit and selected-zoom/settings-change timing.
- Request84/A0/90/9c/8d evolution and the paired depth correction during those
  transitions; whether repeated history feedback changes visible placement.
- Safe relinquish/restore on ADS, disable, request retirement and player/body
  replacement, without writing obsolete native state.
- Indirect/aliased readers outside the bounded native paths inspected here.

There is no new live experiment or hook in this pass. A safe native-feel fix
cannot be claimed from source ordering alone, and no gameplay or visual
acceptance is inferred from these static witnesses.

## Addendum: no direct existing-dispatch restoration point in the input gap

A final bounded check examined only the saved original native6 instructions
from the input read **0x1286851** up to, but excluding, the interpolation-history
read **0x1286ca8**, plus the complete direct lens helper0x1277780. The native6
span has212 instructions and10 calls, **all direct**:

| Call site | Return PC | Direct target |
| --- | --- | --- |
| 0x12868c4 | 0x12868c9 | 0x128d820, steering addition |
| 0x1286a21 | 0x1286a26 | 0x1277250, orientation solver |
| 0x1286a46 | 0x1286a4b | 0x759b10, selected optic multiplier |
| 0x1286a96 | 0x1286a9b | 0x786920, wielder lookup |
| 0x1286aa6 | 0x1286aab | 0x776010, reload query |
| 0x1286acd | 0x1286ad2 | 0x1278cc0, conditional distance-history update |
| 0x1286b54 | 0x1286b59 | 0x1276e40 |
| 0x1286b88 | 0x1286b8d | 0x1277780, lens helper |
| 0x1286be0 | 0x1286be5 | 0x1277780, lens helper |
| 0x1286ca3 | **0x1286ca8** | 0x1277780, lens helper |

The complete100-instruction lens helper has only two direct runtime math calls:
0x127782a->0x210cfe8 (return0x127782f) and
0x127789f->0x210cfe8 (return0x12778a4). It has no UnitAPI or CameraAPI slot call.
The native6 span also has no indirect SDK call. In particular, it does not
directly call the currently leased UnitAPI+0/+0x38/+0x40, CameraAPI+0x20
position, +0x80 FOV or +0x68 near setter. Those leases are declared in
`src/pose_policy.c:378..381`, `src/camera_policy.c:477`,
`src/fov_policy.c:64` and `src/near_policy.c:88` in the inspected source snapshot.

Thus **no direct restoration callback is present at this requested depth**.
The last lens-helper return is already the instruction that rereads84. A
returnPC is not itself an interceptable SDK dispatch. Further, conditional
0x1286acd->0x1278cc0 can read84 at0x1278f9e and copy it into depth historyA0 at
0x1278fa4 *before* that final history read. An input-only borrowed84 would need
restoration before that path as well. The existing late pose/FOV callbacks
occur after native84 consumption and cannot retroactively prevent this history
effect.

This addendum does not traverse every other direct target's callees; it is not
a proof that no indirect callback exists anywhere transitively. None of the
listed direct calls establishes an already leased SDK boundary, and no new
hook, borrowed value or restoration transaction was implemented. Any proposed
inner-callee boundary would need its own exact ABI/frame and ownership proof;
a fixture for two C callback frames does not automatically identify an
intervening helper's native caller. Exact call bytes/order are appended in
`native-unzoom-ownership-evidence.json` under
`input_only_restore_boundary_followup`.
