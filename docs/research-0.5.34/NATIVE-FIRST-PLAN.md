# Native first-person investigation and acceptance ledger

User goal: Create a first person mod that feels almost like Arrowhead intended it to be in the game.

## Accepted foundation

The True FPV chat (01a1b49b-f52d-76b0-b26f-cec4b302188d) isolated stretched shoulders to the persistent camera pin. The user explicitly removed that policy, accepted natural motion in 0.5.35 and confirmed restored performance in 0.5.38. The HellForge dev.2 port incorrectly used the older .34 camera. The staged dev.3 source removes the independent look integrator, quaternion scratch writes, camera-only motion/kick/free aim and action/vehicle pinning. It forwards the native FP/ADS CameraAPI frame exactly, with a bounded positional-only on-foot normal4 fallback. Saved settings are retained. This does not by itself prove the final world camera is unchanged: paired depth still alters a rig ancestor of camera50, which requires observation.

## Inventory coverage and semantic evidence

- All seven shipped HellForge public C SDK headers inventoried: 91 HfApi function pointers, 131 record types, 1,923 fields and the named functions/globals. See HF-SDK-AUDIT.md and hf-sdk-inventory.json. No dedicated gameplay FPS-mode API is exposed by HfApi.
- Original-build engine registration census: 1,223 verified paths / 101 namespaces / 1,846 registration+callback byte pairs. Broader historical census: 1,874 names / 117 namespaces. Historical-only names are not current support claims. See ENGINE-SDK-INVENTORY.md/json.
- Camera/render, native input and rig/lifecycle functions have selected implementation traces, with further ownership/reentry/render-consumer work in progress. Reports distinguish registered names, native behavior and actual live availability. Entire SDK semantics are NOT marked investigated or proven solely by an inventory.
- A new optional C metadata census traverses the actual captured Stingray SDK table via raw Lua C API operations. It invokes no discovered SDK function, respects aliases/cycles/output/depth limits, and preserves the caller stack. Its one-shot output will let the next live session close dynamic namespace gaps. The guarded native memory observer captures camera/body/FPS0/5/50/weapon/lens at20Hz for20seconds after manual F11, then stops. This module is enabled only in the development research build.

## Questions and acceptance gates

| Area | Evidence needed | Current state |
|---|---|---|
| Native camera authority | FPV hip/ADS full frames plus rig0/5/50 and final Camera world pose in the same evaluation | Boundary passthrough tested; final world-view coupling unresolved |
| Input fidelity | Native mouse/controller filtering, zoom gain and target-steering order; no camera integrator or saved setting changes | Old-build static routes mapped; no new input hook live-tested |
| Rig integrity | Full native hands/gun hierarchy, recoil/reload/stance behavior; no per-bone pinning | Common native rig offset retained; rendering acceptance pending |
| Projection/clip/shadows | Per-camera versus per-object/render-pass behavior, actual pass registry, owned baseline restoration | Native command/field tracing complete for selected routes; no separate weapon layer established |
| Lifecycle | Native constructor/controller and desired gate behavior; event ordering through weapon swap/respawn/ADS/action | Constructor path verified; native hip mode is not a controller-enter drop-in |
| Discovery/performance | HF weapon/spawn/state events versus repeated scans; bounded identity proofs and measured FPS | Event declarations audited; live delivery/order and FPS pending |
| Effects/HUD | Scope DOF and vignette off; full crosshair+circle aim policy; native action/vehicle restore | C leases tested; current native-camera presentation gating corrected, live checks pending |
| UI/settings | F11, F8 arms/gun only, per-model/actions save/cancel; no misleading inactive controls | Native controls retained; camera-only controls parked with settings preserved |

## Next coordinated live session

The user confirmed launcher closure, and a fresh process check found both the game and launcher closed. Tested dev.3 was installed at2026-11-09T20:48:32Z with all15 runtime hashes verified. The complete previous dev.2 directory and three loader/settings files were backed up; settings and loader ownership remain unchanged. See `docs/installation-dev3.json`. Fresh session `9472b065334c40c9a443d56cb03a3279` was started through the CLI using saved launch settings; its loader hash matches the tested DLL, and VM generation1 was captured. F11 remains off and gameplay acceptance is pending. The read-only SDK census completed; the next step is the user's gameplay-ready signal and manual F11 baseline check. The bounded game-thread comparison includes actual Unit generations and copied rig/camera values, but does not establish same-evaluation authority. Any new write-enabled input/projection trial remains separately coordinated, with no inline patch.

## Findings that change the design

- Camera.set_mode is an immediate no-op. Camera.mode returns0. A native-mode checkbox cannot be built on those names.
- Post-projection is a real per-camera render command, not an established per-weapon layer. Multi-viewport calls alone do not prove isolation of weapons from the world.
- Mesh shader-pass flags use a different 64-bit field from the existing native mesh visibility byte. Actual names/hash registry and render ownership must be observed before writes; unknown names affect bit63.
- Unit.animation_get_state can observe native layer indices, but the target helper returns full layer count after filling only32 slots. Independently qualify the known ASM/layer count; no generic snapshot/replay.
- Animation merge options alter timing only. Bone/root mode setters and World.link_unit mutate animation/hierarchy; link resets child pose. They are not default tools for keeping native FPS hands and gun coherent.
- Native controller_enter is held-ADS state. It can adopt an existing kind6; controller_exit publishes a fallback request. There is no established persistent non-ADS FPS preference to replace the mod's intent state.
- A pre-filter UnitAPI+0x90 input route exists, but simple zoom gain inversion also changes native target steering. The experimental clone and caller-context/lifetime work stays out of the pin-removal release.

Strict-C caller-frame feasibility is now proven in isolated optimized fixtures:432207 checks/36018 callback invocations across frame-pointer and frame-pointer-omitted builds. This does not qualify the actual game SDK hook, input-source coherence or clone-reader lifetime. See C-CALLER-CONTEXT-FEASIBILITY.md.

## Further function investigation

- `NATIVE-ZOOM-STATE-ROUTES.md` traces13 direct parameter-selection calls, allfour selected-zoom multiplier calls and33 original-build byte ranges. Cycling the actual optic publishes the user's shared selection; a hidden hip-view selection swap is rejected. A native unzoom branch exists, but its ownership and placement/input effects need further qualification. Zeroing/ballistics coupling remains unproved.
- `NATIVE-CAPTURE-ANALYSIS.md` documents a new offline inverse-affine pose analyzer. It separates camera-relative weapon/hand positions from gun-relative hand positions, with explicit owner/request/state boundaries, unavailable components and partial capture handling. Its1,001 known affine cases and capture-boundary checks pass. No actual dev.3 capture has been analyzed yet.
- The existing dev.3 runtime/source archives remain the immutable tested candidate. Later research reports and offline analysis tooling are workspace additions, not newly installed runtime behavior.
- `NATIVE-LIFECYCLE-REENTRY.md` closes the desired-input/reentry and blend-consumer trace:25 procedures and51 authored table rows. Native curve5 is smoothstep; FPS constructs an immediate zero packet. Recreating FPS can discard an unfinished exit blend, conditionally explaining a snap. This is not yet the observed cause. Nonzero blending needs actual evaluator ownership because multiple requests can run in one update.
- `NATIVE-VIEWMODEL-RENDER-ROUTES.md` proves the render-side post-projection consumer and traces native rig camera/gun attachment requests. A branch5-only depth comparison may keep camera/root unchanged while retaining the complete native arms/gun hierarchy. Actual parent8/parent50 completion, attachments and evaluation timing must be confirmed first.
- `NATIVE-UNZOOM-OWNERSHIP.md` rejects a shared avatar flag and a derived reload flag as private unzoom controls. Reusing the existing FOV hook to align request84 with the displayed lens could correct next-evaluation input without changing native steering order. The same scalar feeds native interpolation/depth history, so repeated writes have a concrete feedback risk. No input writes were added.

The further pass changed no playable C source. Installed dev.3 remains exactly the11-suite/19-fixture candidate. These three next candidates are evidence-backed hypotheses, not enabled features or gameplay acceptance.

`LIVE-SDK-CENSUS.md` records the first actual dev.3 census:1947 function paths,
all1223 static paths present with zero callback-RVA mismatches,724 additional
paths,143 visited tables,123 aliases and38 explicit omissions. All11 selected
native API pointers were readable. Presence does not qualify behavior; no
discovered SDK function was called. Manual FPS/ADS testing remains pending.

## Qualified next tools and closed SDK leads

- `LIVE-INPUT-SDK-FOLLOWUP.md`: the confirmed shake links feed device rumble;
  dead-zone setters change axis remapping. They do not supply the FPS temporal
  filter or a zoom-sensitivity switch. The unlink callback calls the append
  helper on this captured build, so its name is not sufficient inverse proof.
- `LIVE-RENDER-SDK-FOLLOWUP.md`: existing RenderData blocks provide a bounded
  typed-property discovery route. Mesh's generic getter returns empty;
  ShadingBlend exposes priority/opacity. Renderer.resource synchronizes renderer
  work and can insert missing keys. No speculative calls were made.
- `NATIVE-PHASE-OBSERVER.md`: a separate C11 prototype recovers caller PC/RSP/
  RBX/R15 through exactly two own frames and distinguishes an actual queued
  native6 request from the composed FOV phase. It passed421101 private capture
  checks over36111 callbacks and74 fake-memory binding checks, with no failures.
  The previous432207-check feasibility result is separate and was not rerun or
  added to these totals. The new prototype remains outside the runtime build;
  actual game pairing, latency and lease lifetime are unqualified.

The input-only lens-borrow follow-up found no currently leased SDK dispatch
between native input's84 read and its later history read at the inspected
direct-call depth. The final lens-helper return is already that history read,
and a conditional earlier helper copies84 into depth history. This closes that
specific restoration-boundary hypothesis without adding a hook or field write.

Current decisive live gate: user ready in firearm gameplay, followed by manual
F11 activation. The current session has remained off, and no pose recording has
started. Native feel, startup, arms/gun continuity, ADS release and FPS acceptance
cannot be inferred from the loader, SDK census or private C fixtures.
