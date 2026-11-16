# Native FPV camera rewrite review

Reviewed UTC: 2026-11-09T19:33:17.238741+00:00

This is a read-only independent source review of the in-progress C replacement for the rejected camera pin. No game process was attached, no game function was executed, and no implementation or installed file was changed by this review. The main agent is editing concurrently, so findings refer to the snapshot hashes below and must be closed against the final source rather than treated as a claim about a later build.

## Final research integration review — 2026-11-09T20:09:41.570706+00:00

Source-only independent review of the new `sdk_inventory.c/h`, `native_observer.c/h`, `mod.c` recording integration and `engine.c` raw traversal. No tests were rerun, no live process was read, and no implementation/test/installed files were changed by this review.

**Earlier camera re-review details 1–2 are now closed in source.** `camera_policy.c:155-175,251-262,339-352` resolves and closes the exact held weapon Unit object and rejects carried history from a different rig object/descriptor. `runtime.c:315-319` matches explicit native6 mask0/2 and on-foot normal4 mask0/2/0x20 visibility; the old extra `player.safe` mismatch is removed. The end-to-end root0/branch5/camera50/actual-camera world-view caveat remains unresolved by source review.

### Remaining actionable issue

**P2 — Completed capture automatically rearms across VM close/recreation.** `sdk_inventory.c:223-226` closes/reset the entire `observation` struct whenever `eng_state_generation()` changes, clearing `attempted`. If F11 intent remains enabled, `228-238` immediately starts a new20-second capture without a new manual activation. `engine.c:188-199` increments generation when the native VM closes, even while `eng_ready()` is false, so a capture can start for the close generation and then again for its replacement. This violates the documented first-manual-F11 one-shot behavior and can reintroduce the recording workload after it was reported finished. Preserve a process/session one-shot/explicit-arm latch across per-VM metadata resets; close an open capture on generation loss/change and do not rearm from enabled level alone. Add a recorder lifecycle check for completed capture -> VM close -> replacement while enabled, and for a generation change mid-capture.

### Checked boundaries and remaining limits

- `native_observer.c` makes protected reads and pure validation calls only. No discovered SDK function, controller, setter, event, new hook or automatic camera toggle is invoked. It clears failed output, flags missing components explicitly, closes current request/controller/flags, graph/Unit/descriptor/attachment identity and lens fields. It is stateless and holds no SDK userdata or component pointer authority across samples.
- The raw SDK traversal bounds rows32768, visited tables1024, depth8, key127bytes and path511bytes; raw table access avoids field/metatable discovery calls. Aliases, omissions and depth/row limits are recorded. Visitor/pcall failure restores caller stack and clears census context. Current VM identity/generation is checked on return. Its existing closed-VM fixture was inspected but not rerun.
- The recorder bounds the active window to20seconds,512rows,20Hz with no catch-up loop; after normal completion it does no repeated memory observation unless the rearm issue above occurs. File writes/read failures terminate the capture, with partial data retained; per-row flush and close errors are checked. Interrupted process exit intentionally leaves a `.part` log, rather than pretending a complete finalization.
- Both `native_observer.h:57-62` and TSV header explicitly reject same-native-evaluation/render-frame claims. Samples run after the mod tick on HellForge's pre-game-update callback. World matrices/lens can describe different evaluation times despite stable identity;20Hz data cannot resolve every animation-frame jitter. Root0/5/50/camera world matrices help locate offsets but do not replace a synchronized end-to-end observation.
- No full hand13/34/all-gun-child chain is captured yet. The current observer records rig0/5/50 plus weapon node0 and its attachment parent's world matrix; this is a bounded first observation, not complete hierarchy/animation causality proof.

Reviewed snapshot hashes:

- `src/sdk_inventory.c`: `0f6bf3e96132b702b3d325fdd1f3670eae3b4c37b04efa6707646a4d6fd0640b`
- `src/sdk_inventory.h`: `fe148c7f9514ce3465ad289bbd271213814aac47f79f4af102f29bf1707181f1`
- `src/native_observer.c`: `630bf9a72d3410d6acaf0cbd0d8d4a32ce7a4dcefcbf076152450494e30ba156`
- `src/native_observer.h`: `a1b5f77a0541ef6df3e85a09d170b0dd6949fd840063d6da535c1d2489e6a96f`
- `src/mod.c`: `7547a5b18aec23447f393aa1f4c5a7aec43e26aa2200f44ffa3e101e63644f38`
- `src/engine.c`: `bca44e64d23280a0e510ebe76604029e0d7169ddf6b43a93ad5cc7556d7c3aaf`
- `src/camera_policy.c`: `580bd504cfb5d341408b8bfdf1a0f9b5024e2f4799bf4ad0ac6af7797f172996`
- `src/runtime.c`: `ae8d86298296f14ff0952efc6c86b2345d57cd45aa8ea5f4faa0bacef57a642c`

---

## Independent re-review — 2026-11-09T19:36:32.229353+00:00

The current edited camera, correction, scene, pose and runtime files were independently re-read. This re-review is source-only; it does not establish gameplay acceptance. The original findings below remain as historical evidence against the earlier snapshot.

Original findings 3–7 are addressed: native high-pitch quaternions are published before optional angle sampling; shared frames invalidate on owner loss/close and pose falls back to a freshly read native parent with neutral optional motion; native6 admission history expires; sampled head metadata is closed through `FpBodySample`; and the earliest global callback epoch plus locked policy publication prevent the previously identified reopen window. Original findings 1–2 are addressed in direction, with two remaining concrete details:

1. **P2 — Resolve the weapon Unit object in the carried-residual close.** `camera_policy.c:201-211` compares numeric weapon UnitRef and descriptor fields. `fp_snapshot(true)` (`backend.c:174-197`) proves descriptors, but does not resolve the Unit registry object. Capture a nonzero `fp_unit_object(memory, firearm.weapon_unit)` at the initial sample and require the same object when closing. The pose helper already uses this object-level proof. Test registry/object retirement/replacement at constant descriptor/UnitRef.
2. **P2 — Match visibility to the camera fallback's allowed masks.** `camera_policy.c:85-87` admits normal4 masks 0, 2 and 0x20 after on-foot and space proofs. `runtime.c:315-318` additionally requires `player.safe`, while `backend.c:167-168` defines that as mask zero. Therefore mask2/0x20 can receive an eye-camera correction while the body/effects/markers are restored as if third person. Use the same current explicit mask/owner/on-foot admission for both camera and visible predicate, or narrow camera admission to match. Test mask2 and the positively verified mask0x20 on-foot action, alongside native vehicle/action restore.

The end-to-end **root0 → camera50 → Camera world pose** caveat remains unresolved and prominent below. Exact CameraAPI pointer forwarding is boundary evidence only; final world-view behavior needs read-only observation.

Re-reviewed file identities:

- `src/camera_policy.c`: `f6e20439e4beb961e835559c0eb92d101064b05e359acf2b768a59f475274812`
- `src/camera_policy.h`: `f78815e83576d2cb9486ae750c5b41a1347060e10f5f3a573e8cdd3759499316`
- `src/correction.c`: `980367a8fe549d592085c58680fb9de78cfdf6e15615cf3d9ea808ca9fd650cc`
- `src/correction.h`: `2690842e666fe32f69b95a95ef31adf7f06d151f1952f656db1935961b777ec9`
- `src/scene.c`: `a7063295ebb0128e684f4f60b4cf9be88bb9481b1df071e7e6e91fe73fc3af7a`
- `src/scene.h`: `f2ff90f6f6492bd7e8ef58b84861769a0231005b70ed5fb38371e6daaa06ee74`
- `src/pose_policy.c`: `2c64365b5a5dbe9cbf76fbe418f652e92f74339e6c4f2e28d6c41f3ba0484c76`
- `src/runtime.c`: `6a8bf7aa945f4204928e86acdb3c1df1a0bb029ca8107e6d31858061facd751f`

---

## Initial snapshot result

The replacement has the correct central direction: `camera_call` always forwards the original position pointer for native kind6, and the rewritten file has no quaternion scratch writer or raw-input integrator. Vehicle/unsupported action kinds forward unchanged. The new `correction.c` is a straightforward C translation of the accepted position-only handoff algorithm: finite body-relative vectors, bounded residuals, weapon identity, fractional millisecond timestamps, immutable active duration, repeated-timestamp stability, and explicit retirement on clock/weapon discontinuity.

Several integration and terminal-proof gaps should be fixed before the next live candidate. These were sent to the main agent as they were found.

## Actionable findings

### 1. Revalidate the firearm immediately before a corrected normal4 call

Priority P2. Snapshot `src/camera_policy.c:199-208` obtains a strict firearm snapshot once and uses its numeric weapon to apply a native6 handoff residual. The final `owner`/`same_request` proof is player-only. Normal4 request identity can stay the same while the equipped firearm or descriptor changes, so a change after the first firearm sample can admit the previous firearm's correction.

Close with another strict firearm snapshot immediately before committing and compare weapon ID, descriptor, descriptor bytes and weapon Unit object/generation. If weapon sampling is unavailable, discard the residual; never grant a previous weapon's history permission to affect the new one. Test a wield swap or descriptor/Unit replacement between the initial firearm sample and terminal commit while the normal4 request is unchanged.

### 2. Restore body/effects/markers during native action and vehicle cameras

Priority P2. Snapshot `src/runtime.c:287-339` considers the armed camera lease active even for unsupported camera kinds, then calls model hiding and effect suppression with `wanted=true` unconditionally. Previously the pin intended to keep those views at the eyes. After the camera now correctly yields to a native third-person/action/vehicle view, that unconditional presentation can leave the player body/clothing hidden and FP effects or aim-marker suppression active in that native view.

Gate presentation on actual qualified first-person visibility: a supported local native6 request, or the currently qualified on-foot unparented normal4 fallback. Keep the user's F11 intent while yielding presentation. Test vehicle/action entry and exit without toggling F11 and verify body/default masks and native markers restore during the yielded view.

### 3. Preserve a valid native quaternion when optional angle extraction fails

Priority P2. Snapshot `src/camera_policy.c:102-108` updates `p->view` only after `fp_q_angles`. `src/presentation.c:25-28` returns false near the vertical poles (absolute pitch at least about 84.6 degrees), despite a valid native quaternion. The camera itself is forwarded correctly, but `publish` sends an older view to `pose_policy.motion_parent` (`src/pose_policy.c:45-76`), which uses it to convert authored arm/weapon rotation offsets. The arms can therefore rotate in an older camera frame near vertical look.

Publish the valid native quaternion before optional yaw/pitch extraction. Unsupported bob/sway angle sampling should produce neutral optional motion, not a stale orientation. Add a near-vertical valid quaternion test checking both exact native camera forwarding and the shared published quaternion.

### 4. Expire the separate native6-seen blend shortcut

Priority P2. The C state has a `native_seen` Boolean but no timestamp for it. In snapshot `src/camera_policy.c:173-184`, an old `native_seen` forces normal4 blend to one before `fp_correction_apply` retires stale correction history. The accepted helper explicitly clears `native6_seen` for rollback or gaps beyond `max_gap_ms` (`native_ads_camera.cpp:529`). After a long gap, the C fallback can thus skip its normal 120 ms admission blend despite having rejected its old handoff history.

Track the last qualified native6 sample time and expire this Boolean on rollback or a gap beyond 250 ms before consuming it. Test native6 followed by normal4 after 251 ms and after a clock reversal; do not derive permission from a stale Boolean.

### 5. Close the sampled head topology, not just graph array addresses

Priority P2. `fp_body_points` samples the unique head hash/link/parent hash, but the terminal proof in snapshot `camera_policy.c:204-208` compares only `FpGraph` addresses/count and parent-space metadata. In-place hash/link changes can pass `graph_same` while invalidating the sampled head identity used for the eye solve. The accepted helper explicitly closes the sampled head hash/link/parent hash (`native_ads_camera.cpp:493-497`). The same concern applies when admitting a read-only native6 point into handoff history.

Return a compact body/head proof from sampling and revalidate its node, hash, link and parent hash immediately before adjusted use/history admission. Animation matrix values need not be frozen; identity/topology must remain the sampled one. Test a metadata mutation at constant graph-array addresses.

### 6. Close the callback's generation before reading reopened policy state

Priority P2 under concurrent callback/lifecycle execution; actual scheduling concurrency was not established by this static review. Snapshot `camera_call:211-217` reads mutable `p->memory`/`origin` before capturing `callback_generation`, while `open:283-309` writes that policy outside `p->lock`. A callback that passes an old armed filter and is delayed before its generation load can observe a reopened generation and sample old native arguments into its new history. The unsynchronized mutable field reads also need a clear concurrency contract. The accepted helper captures its entered generation at hook entry and publishes the replacement policy under `policy_lock` (`native_ads_camera.cpp:513-525,700-708`).

Capture the epoch at the earliest entry snapshot, recheck the active pointer/epoch under lock, and publish mutable new-session policy under the same serialization (or publish immutable per-generation policy). The current test hook is after generation capture, so add a test covering the earlier delay/reopen boundary. If engine serialization is the intended guarantee, document and establish it rather than relying on it silently.

### 7. Shared motion/view frames need retirement validity

Priority P2. Snapshot `fp_camera_policy_read_frame:53-65` checks seqlock consistency and quaternion validity only. Owner failure in `camera_call:226` resets correction but can still publish earlier `last_motion`/`view`; close does not invalidate the shared publication. `pose_policy.motion_parent:50-53` retains its previous motion/view when a read fails. A concurrent owner/read failure or camera transition can therefore keep optional stale motion alive until the next successful sample/pose cleanup.

Tag shared frame validity with the camera generation and observation sequence, invalidate/neutralize on owner loss and close, and make the pose helper use a qualified fresh native parent orientation plus neutral optional motion when publication is invalid. Avoid carrying a previous frame's motion as a fallback. Test close, failed read and retired generation while pose remains in a qualified native evaluation.

## Native-authority boundary to verify live

The exact CameraAPI pointer-forward test establishes that one boundary, not every contributor to the final world view. `src/pose_policy.c:139-169` still applies the paired depth compensation to FPS rig root node0. The rig's camera node50 is below that root (`src/scene.c` rig topology), and native gameplay camera space can be parented to node50 (`fp_camera_native_parent`). Depending on evaluator order and native compensations, changing root0 may affect the final world-view position even though CameraAPI receives its native pointer unchanged. No final effect is claimed from static source alone.

A useful read-only comparison records native request kind/identity, FPS root0, branch5, camera node50, actual camera parent, Camera world pose, selected zoom and final lens during native ADS and TFP. Compare only same-owner, same-weapon samples. This can establish whether viewmodel depth normalization moves only the visible assembly or also the world view.

## Native-first opportunities established by inspected source

| Area | Inspected capability | What remains to qualify |
|---|---|---|
| Native mode lifecycle | `src/native_call.c:17-26`, `src/lifecycle.c:30-110`, `src/profile.h:19-21` already use the game's allowed/entry/exit functions and let it construct/destroy native requests | Identify a supported gameplay SDK/controller entry rather than inventing a replacement camera; prove any replacement call's exact old-build ABI and lifecycle |
| Native rig and animations | `src/scene.c` proves the native FPS hierarchy; branch5 placement preserves the common hand/gun subtree | Preserve native animation/recoil authority; do not substitute per-hand transforms or pose snapshots |
| Camera observation | Photo Mode's declared SDK needs include `Unit.camera`, `Unit.num_cameras`, `Camera.local_pose/world_pose`, `Camera.world_position`, `Camera.vertical_fov`, scene graph nodes (`photo-mode/src/engine_api.c:16-45`) | Query live binding presence and accepted argument/handle contracts, then observe the actual gameplay camera across handoffs; declarations alone do not prove live support |
| Parent-space handling | Existing Unit scene-graph APIs and native graph proofs expose current parent relationships | Make parent/request identity explicit in state transitions and yield unsupported actions; do not preserve orientation by replacing the native quaternion |
| Animation state | Current action reader reproduces the native getter using copied memory (`src/action.c`); neighboring SDK use has `Unit.has_animation_state_machine`, `Unit.animation_bone_mode`, `Unit.set_animation_bone_mode` (`photo-mode/src/engine_api.c:196-207`) | Investigate an actual read-only state getter in the SDK inventory; bone-mode setters are not evidence for a native aiming-state selector and must not be used merely to force a pose |
| Visibility and shadows | Real SDK Unit handles + `Unit.set_mesh_visibility` and Mesh metadata already exist (`src/models.c:262,311-319`) | Keep default visibility and shadow passes independent, restore body visibility on native actions, and qualify the complete linked weapon subtree |
| Renderer effects | `Application.render_setting/set_render_setting`, `RenderDataComponent.instances/get_property/set_property`, world/entity enumeration are existing calls (`src/effects.c`) | Prefer exact component/profile ownership and acknowledge asynchronous settings; global near clipping is not evidence of a separate viewmodel projection |
| Viewmodel projection | Native FOV and depth math are separately inspected in `fov_policy`/`depth` | No dedicated weapon-only camera/projection layer is established by these sources. Inventory and runtime evidence must identify one before promising separate weapon/world FOV or near planes |
| Look sensitivity | Pin/raw-look removal leaves native input authoritative | Native zoom sensitivity remains unresolved. The later SDK input experiment is a separate unaccepted trial; do not claim the camera rewrite fixes it or transplant Steam offsets into HellForge |

The SDK table here is a focused source-backed opportunity map, not an exhaustive engine/HellForge API inventory. Other delegated inventories cover that broader request. New native functions require exact old-build qualification; generic SDK availability does not establish that arbitrary calls are safe inside a camera callback. Engine Lua SDK calls remain game-thread-only through the C bridge, with real Unit userdata handles and VM generation ownership (`src/engine.h:3-18,28-43`).

## Snapshot identity

- `src/camera_policy.c`: `f6e20439e4beb961e835559c0eb92d101064b05e359acf2b768a59f475274812`
- `src/camera_policy.h`: `f78815e83576d2cb9486ae750c5b41a1347060e10f5f3a573e8cdd3759499316`
- `src/correction.c`: `980367a8fe549d592085c58680fb9de78cfdf6e15615cf3d9ea808ca9fd650cc`
- `src/correction.h`: `2690842e666fe32f69b95a95ef31adf7f06d151f1952f656db1935961b777ec9`
- `src/runtime.c`: `6a8bf7aa945f4204928e86acdb3c1df1a0bb029ca8107e6d31858061facd751f`
- `src/pose_policy.c`: `2c64365b5a5dbe9cbf76fbe418f652e92f74339e6c4f2e28d6c41f3ba0484c76`
- `src/scene.c`: `a7063295ebb0128e684f4f60b4cf9be88bb9481b1df071e7e6e91fe73fc3af7a`
- `src/presentation.c`: `8c4fa594240fe7c77fc2553ec736531d5602cb4bf5ef4d34c0efb309972896ea`

Accepted comparison source: `/home/goose/Projects/Super-LiberTEA-LE/sources/gadget-kit/native/first_person/native_ads_camera.cpp` and `camera_correction.h`; those are comparison sources for behavior, not old-build offsets to reuse.


## Main integration correction after final review

The SDK observer now preserves a session/process one-shot latch across VM generation changes. A VM change closes any active capture and cannot automatically rearm it. The dedicated research_recording C fixture covers native-off/manual start, cadence/window, TSV schema matching, VM-close without rearm, and SDK census once-per-VM behavior. The stateless observer additionally captures both native hands13/34 and the FPS weapon socket8, under the same rig generation/topology proof. Full gun-child graphs and same-native-evaluation timing remain outside this first capture. Final test results are in VALIDATION.json and native-policy-tests.json.
