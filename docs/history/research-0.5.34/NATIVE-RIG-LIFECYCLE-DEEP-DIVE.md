# Native FPS rig and lifecycle investigation — original build 25480438

> Historical dev.3 investigation. References below to the current root-carrier policy describe dev.3. Dev.4 forwards root0 unchanged and applies qualified placement/depth only to branch5; see ../FEATURE-COVERAGE.md and ../LIVE-TEST.md for current behavior and pending acceptance.

Date:2026-11-09. This is an offline source and captured-binary investigation. It made no live attachment, game calls, controller calls, animation writes, settings changes, hooks, installation changes or implementation/test edits. `native-rig-evidence.json` holds 74 selected original-build callback disassemblies,27 manually interpreted API records, ten direct native-helper disassemblies, and selected camera lifecycle windows. It does not claim every engine callee or every SDK function signature is understood.

## Result and implementation direction

The best supported route is to preserve the complete game-owned FPS rig, its weapon attachments, animation graph and native camera. Observe native lifecycle and graph identity; reacquire only after the game has returned a qualified normal on-foot camera. Use the native mesh pass API for local model/shadow visibility. Individual wrist/gun pose replay, root/bone animation suppression, skeleton reparenting and repeated native constructors are not established improvements.

The SDK exposes useful animation **observations**, authored graph variables/events, constraint targets, root/bone mode switches and real hierarchy mutations. It does **not** expose a proved persistent non-ADS first-person preference. `Camera.set_mode` is a literal no-op on this build. `Unit.set_animation_merge_options` is animation timing policy, not bone retargeting; `Unit.is_child` does not return its answer to Lua. Names alone are insufficient evidence.

**End-to-end world-view proof is still required.** Exact native `CameraAPI` pointer/orientation forwarding proves that boundary only. The current C pose policy also moves FPS rig root 0 for depth correction. Root 0 is an ancestor of the native FPS camera node 50. Branch 5 contains the arms/weapon hierarchy; node 50 and branch 5 are siblings under node 4. Observe root 0, branch 5, node 50 and the actual camera Unit world pose together before claiming that the complete world view is unchanged. `src/scene.c:28-48,165-172` establishes the current expected topology; `src/pose_policy.c:120-172` contains the root carrier correction and `174-239` the separate branch offset.

## Evidence and scope

| Source | Identity / use |
|---|---|
| Original EXE mapped capture | `~/Applications/HellForge/mods-retired/localappdata-1019/LocalAppData/LinksLinkToMemory/r6_reload_exe_20261017.bin`, SHA 256 `2b2c496218a5ce7e4f3453951acb8bd756bc4c2f281ce95eadef17b894e39c16`; Unit/World/Camera Lua callbacks and engine native helpers |
| Original game.dll mapped capture | `~/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll`, SHA 256 `642b0aac0b4f577054993a965054827455ef143ced9895b2dfd09d1ecd433a37`; gameplay controller, native camera constructor/exit/request handling |
| Original-build registration inventory | `engine-sdk-inventory.json`: exact callback RVAs from original registration sites; not historical Steam signature transplantation |
| Installed public SDK16 | `~/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/hellforge_mod.h` and `docs/modding/runtime.md`; event/thread contracts |
| Accepted later Steam behavior | `~/Projects/Super-LiberTEA-LE/sources/gadget-kit/manifest.json` reports 0.5.39 and retains the accepted natural/performance baseline; current `native_ads_camera.cpp`, `camera_correction.h`, `src/first_person/backend.lua` are behavior comparisons only |
| Local release receipts | `outputs/True-First-Person-0.5.35-live-checks.md`, `0.5.38-performance-report.md`, `0.5.39-recovery-report.md` in `~/Documents/Codex/2026-11-07/tr`; pin removal, natural rig acceptance, performance acceptance and remaining limits |
| Historical read-only rig census | `docs/live-rig-check.json`: original body 107 nodes/head 90; FPS 63 nodes, resource `ed8ba03c494dcfd0`, branch 5, hands 13/34, attachment 8, camera 50. This is an earlier session observation, not current live ownership proof |

Both binary captures are mapped images: offsets used here are RVAs. A lifecycle RVA in **game.dll** is not the same bytes at that RVA in **EXE**. The corresponding installed Lua DLL is an ordinary disk PE, so its exports were resolved with section mapping. Captured EXE import pointer values agree with 104 installed Lua export RVAs at one historical base; that consistency names import slots, but does not prove the current process DLL base. Original EXE Lua index bias at `0x190829c` is 1.

`lua_rawset` is EXE IAT slot `0x13f79e0`, not `lua_settable`; this distinction does not invalidate the namespace registration census. The machine-readable artifact retains the export match, source hashes and import-slot map. No running binary was opened in REA. A bounded offline EXE merge-options window was checked with Ghidra assembly and closed; evidence `ev_acdad4a1a7e22c43d6df2a9c7594d800a3e74776ddb6862f7f8791bfb1308e47` is embedded in the JSON.

## Native camera lifecycle: what the game actually owns

All RVAs in this section are **game.dll** RVAs.

| Function / site | Verified behavior | Consequence |
|---|---|---|
| `FP_ENTRY` wrapper `0x127ddb0` | Loads global FPS camera context `0x346d560` into RCX, calls constructor `0x127de20` using avatar EDX/weapon R 8 D, evaluates latest request atdt 0 through `0x1286330`, then calls native near-plane setter. No controller+4 write | Current `src/native_call.c:17-26` deliberately calls this raw native request wrapper, not the native ADS controller |
| Constructor `0x127de20` | Selects top queue request or fallback context+`0x1f00`. `0x127deb4` compares top kind against 4; `0x127deb8` returns unless equal. Accepted path constructs kind 6 and appends a 0 xe 8 record at tail in a 32-slot ring | Already-kind 6 does not produce a nested FPS request. Calls during native vehicle/action requests also do not establish a persistent FP route |
| `controller_enter` `0x12b29b0` | Requires controller+4 false, mask dword 0, controller+5 false and `FP_ALLOWED`. Sets controller+4 true; triggers native hash `0x4e02de4f`; calls the same constructor and evaluator | If kind 6 already exists, the constructor declines to append but the controller evaluates that existing record. Native ADS can take over the mod's current kind 6 record |
| `controller_update` `0x12b26a0` | Resolves current avatar/held item; calls controller exit when local body/weapon unavailable or avatar flag bit 48 is clear (`0x12b28a3..0x12b28c7`) | This controller state belongs to native held ADS. Calling controller_enter as a persistent hip FP switch would be undone when native aim ends |
| `controller_exit` `0x12b2ae0` | Returns when controller+4 false; otherwise clears it then calls `FP_EXIT` | Raw TFP entry with controller+4 false is not removed solely by this held-aim exit path until native ADS has adopted it |
| `FP_EXIT` `0x127e9d0` | Tests current top kind 6 (`0x127ea16`); otherwise returns. Accepted path restores native presentation, calls retirement helper `0x12771d0`, copies context+`0x1f00` fallback into queue[tail] and advances tail modulo 32 (`0x12805ef..0x12806e6`) | “Pop” is conceptual. It does not tail--. Request identity, queue indices and current native owner must be observed; do not infer ownership from kind alone |
| Desired-input gate `0xa41c0b` | Compares desired AL with controller+4, calls enter/exit only if different. One preceding path reads input object+`0xd`; disallowed/no weapon clears it at `0xa41c04` | A native desired-state route exists, but full upstream intent/owner semantics have not been recovered. Writing this byte is not qualified |
| Suppress/unmask `0x12b2b40/0x12b2ba0/0x12b2c00` | Sets controller+5 or ORs mask bits and clears active ADS via FP_EXIT; unmask ANDs out bits and can call native reentry after another predicate | Native action ownership is explicit. Do not defeat these mask paths with repeated enter calls |
| `FP_ALLOWED` `0x12b2d20` | Resolves held weapon definition and requires definition+`0x180` nonzero | Eligibility is weapon data; calling a constructor does not make unsupported gear a complete FPS rig |

The accepted Steam backend already states the same takeover behavior at `src/first_person/backend.lua:1130-1135`: ordinary ADS can take over the same kind 6, and kind 4 is observed while retiring/reacquiring a lease. The current C lifecycle follows exact request identity in `src/lifecycle.c:30-110`; current runtime retires changed requests at `258-270`, keeps user intent separate, and only attempts entry from qualified kind 4 (`100-105,285-286`).

There is no proved independent native persistent non-ADS camera preference yet. Replacing raw entry with controller_enter is not an equivalent fix. A useful next investigation is the containing desired-input function and the native action reentry predicate `0x12b2e20`, traced read-only with queue and flag changes. An intermittent “Restoring the previous native camera” message only proves the runtime branch at `src/runtime.c:271-275` was reached; it does not distinguish entry preparation failure, pose retirement, native adoption, dispatch loss, changed rig or actual gameplay input. Capture those predicates before assigning a cause.

## Unit animation APIs: exact useful behavior and write hazards

All RVAs in this and the following SDK sections are **EXE** RVAs. Unit Lua arguments are actual engine Unit userdata/lightuserdata. Bindings obtain the userdata then shift its encoded value right 2 to recover native Unit Ref. Numeric entity IDs/Unit Refs are not interchangeable with userdata. Temporary vectors/matrices must be copied/boxed in their valid frame; never retain temporary engine pointers across updates.

| API / callback | Verified arguments and result | Safe role / material limit |
|---|---|---|
| `Unit.has_animation_state_machine` `0x40e510` | `(Unit)` -> boolean; Unit vtable+`0x1b0` non-null | Necessary ASM qualification after current Unit proof |
| `Unit.animation_get_state` `0x410b20` | `(Unit)` ->one result per layer; integer state index, ornil for -1. No Lua+1 bias on state IDs | Useful current graph observer, with additional bounds proof below |
| `Unit.animation_set_state` `0x410da0` | `(Unit,state1,state2,...)`, nil->-1, integers unchanged; zero Lua results | Restores/restarts layer states through native helper. It can reset recoil/reload/transition phases; not a held-aim preference |
| `Unit.animation_find_variable` `0x40ed60` / `has_variable` `0x40ef80` | `(Unit,name/string-or-IdString)` ->native index+1 /boolean | Discover only authored names on current graph; returned 0 can encode native -1 |
| `Unit.animation_get_variable` `0x40f0f0` | `(Unit,Lua index)` -> float asnumber; subtracts 1, calls `0x201d50` | Read a qualified graph input rather than guessing state bit significance |
| `Unit.animation_set_variable` `0x40f1d0` | `(Unit,Lua index,number)`, index-1 and float conversion to `0x201df0` | Real graph input write. Could fight native locomotion/reload/aim; no variable has been qualified as persistent hip FP intent |
| `Unit.has_animation_event` `0x40e760` | `(Unit,event name)` -> boolean; false with no ASM | Event existence is not safe sequencing evidence |
| `Unit.animation_event` `0x40e9c0` | `(Unit,event name)` ->zero results. Native trigger `0x201860` only if total argc==2 | Additional arguments silently skip trigger. Avoid repeated held-aim events replacing native transitions |
| `Unit.animation_layer_info` `0x4117f0` | `(Unit,Lua layer index)` ->two numbers: record+8 double, record+4 float; record=`ASM+0x1d0+(index-1)*64` | Field names/meanings are unproved. Do not label them time/weight from intuition; visible range check absent |
| `Unit.animation_wanted_root_pose` `0x410270` | `(Unit)` ->temporary Matrix4x4; helper `0x2026e0` | Compare native intended root with applied graph root. Requires current blender/graph and copied result |

**Important bounds issue:** native get-state helper `0x5542d0` writes `min(ASM layer_count,32)` entries, but returns the full layer_count (`0x55437e`). Its Lua wrapper copies 32 entries then loops over that returned count. Prove layer_count<=32 before invoking this wrapper. The earlier body observation had 31 layers; the FPS rig ASM/layer count still needs proof. `animation_set_state` copies at most 32 arguments but passes **unclamped** argc-1 to helper `0x554440`, which does not clamp. Neither is a safe generic arbitrary-rig call merely because it is an SDK binding.

### “Merge”, root/bone modes and constraints

`Unit.set_animation_merge_options` callback `0x4114d0..0x411669` reads `(Unit,"option",number,...)`. Only `max_start_time`, `max_drift` and `clock_fidelity` are recognized. It begins with three current floats via Unit vtable+`0x190`; native `0x202400` validates ASM then calls Unit vtable+`0x198` with the three-float struct. There is no second Unit, bone map or retargeting operation. It is animation timing/merge configuration. Unknown names are ignored; numeric bounds are not established.

`Unit.animation_root_mode` / `set_animation_root_mode` read/write animation blender+`0x280`. Modes areignore 0, delta_position 1, delta_rotation 2, delta_scale 4, their bitwise combinations 3/5/6, and delta_transform 7. `Unit.animation_bone_mode` /setter use+`0x284` with ignore/position/rotation/scale/transform and the same combinations. Setter callbacks are thunks (`0x410130` ->`0x40ff80`, `0x410700` ->`0x410550`). An unknown setter string defaults toignore. These can stop authored root/bone application; they do not select first-person animation or repair coherence. Photo Mode's intentional animation freeze is not a good default for an active FPS mode.

Constraints are real authored graph targets:

- `animation_find_constraint_target` `0x40f6f0`: `(Unit,name)` ->native index+1 through `0x202ea0`; `has_constraint_target` `0x40f4d0` -> boolean through `0x202f50`.
- `animation_get_constraint_target` `0x40f870`: `(Unit,index)` ->temporary Matrix4x4; helper `0x202fd0` reads target at `ASM+0x98+(index-1)*64`. Missing ASM returnsnull inside the helper; qualify it and target bounds before the Lua marshaler.
- `animation_set_constraint_target` `0x40fa10`: `(Unit,index,typed math userdata)`. Vector3 tag `0x02b4dab5` selects translation helper `0x203120`; Quaternion tag `0x58235806` selects rotation helper `0x203240`, preserving target translation; otherwise the payload is treated as a full matrix and helper `0x203060` copies 64 bytes.

These are useful read-only observations to establish native grip/IK relationships. No wrist/aim constraint name or semantic role is qualified here. Rewriting targets every frame can fight native IK; invalid type/bounds can reach unsafe native memory reads. Keep hand, gun and all authored attachment animation authoritative until observation identifies a narrow missing native state.

## Hierarchy and mesh APIs

| API | Verified behavior | Consequence |
|---|---|---|
| `Unit.bones` `0x40e3a0` | Returns Lua table of names indexed 1..n from resourcebone data via `0x5f2160/0x5f2290`, key `0x18dead01056b72e9` | Bone-resource ordinal is not automatically scenegraph node index |
| `Unit.node` / `has_node` `0x4057e0/0x405530` | Searches current scenegraph hashes; node returnsindex+1,0 when absent | Cross-check names/hashes/current graph rather than hardcoding weapon-specific indices |
| `Unit.num_scene_graph_items` `0x408660` | Returns scenegraph count+`0x10` | Bounded topology census |
| `Unit.scene_graph_parent` `0x408770` | `(Unit,node)` subtracts 1; internal linklow 16==1 returns upper 16+1; external kind 2 returnsno Lua values | External attachment proof needs parent Unit object, external worlds pointer and encoded link, as current `scene.c` does |
| `Unit.is_child` `0x411e30` | Parses two Units, calls `0x204fd0`, then returns 0 results without pushing a boolean | Do not use as a Lua boolean hierarchy predicate. Native callee semantics not inferred here |
| `Unit.scene_graph_link` `0x408900` | `(Unit,node,parentnode/nil)`: index-1; valid parent calls `0x4ea1f0`, nil/below 1 calls `0x4ea3b0` | Real internal reparent/unlink write |
| `World.link_unit` `0x3eb860` | `(World,childUnit,[childNode],parentUnit,[parentNode])`; numeric third argument shifts parent argument; omitted nodes default 0 native; helper `0x3361c0` | Real external Unit reparenting; resets selected child local pose to identity |
| `World.unlink_unit` `0x3eb9a0` | `(World,Unit)` resolves the object then calls helper `0x1af780` | Real attachment mutation |
| `Unit.local_pose` / `world_pose` | Temporary Matrix4x4 reads of qualified graph nodes | Capture to copied output within valid math allocator lifetime |
| `Unit.set_local_pose` `0x406960` | `(Unit,node,Matrix4x4)` ->helper `0x1fcad0`; zero results | Animator may overwrite it; separately writing gun/hands can separate their hierarchies |

`World.link_unit` also calls helper `0x1af240` after resolving both Unit objects. That helper replaces the selected child local pose with identity (`0x1af368..0x1af3a0`), writes an external graph link kind 2 and parent node (`0x1af3ec..0x1af404`), and rewires Unit parent/child/sibling pointers+`0x1d0/+0x1d8/+0x1e0` (`0x1af454..0x1af469`). It can discard authored local offsets; this is not a benign render-only attachment correction.

The 74 raw callback records also include the original `AnimationStateMachineComponent` and `AnimationBlenderComponent` APIs, enable/disable/set ASM, simple animation/crossfade, meshes and related Unit functions. Their registration and instructions are inventoried; only manually interpreted rows above have the stated signature/semantics. Component/entity handles are not automatically Unit userdata. Do not reuse Unit calls against those components without further ABI and lifetime proof.

### Native visibility and shadows

`Unit.set_mesh_visibility` binding `0x4128e0` jumps to helper `0x4124d0..0x4128de`. Arguments are `(Unit,meshIndex-or-name,visible,[pass])`; numeric mesh indices subtract 1. It calls Unit vtable+`0xc8(UnitObject,meshIndex,visible,passMask,1)`.

| Pass | Id32 | Mask |
|---|---|---:|
| `default` | `0xf2760503` |1 |
| `shadow_caster` | `0x3100c3d2` |2 |
| `cubemap` | `0xa63da7cc` |8 |
| `occluder` | `0xe4ff316b` |16 |
| `trample` | `0x2f8235d5` |16 |

These names are corroborated by original string bytes and the binding's recognized hash branches. Omitting pass selects `0xff` (all passes); unknown nonzero pass selects 0. `shadow` is **not** the accepted name. Current `src/models.c:256-268` correctly calls `default` and `shadow_caster` separately, checks current mesh membership and verifies mask readback.

This is enough to disable local FPS arms/gun shadow casting while keeping their visible geometry, and to hide qualified body meshes while native FPS presentation is active. Restore owned bits when native vehicle/action cameras take over. It is **not** an established per-camera/per-view visibility layer: a shadow/default render pass is not a dedicated weapon projection. Likewise registered `Camera.set_post_projection_transform` and `set_projection_type` do real camera operations but do not prove a separate viewmodel layer. `Camera.set_mode` callback `0x41f3b0` is exactly `33c0c3` and cannot activate native FP.

A forearms-only body mesh mode is possible only if the actual renderer meshes separate those pieces; bones alone do not imply separable meshes. Discover exact renderer mesh ownership and submeshes first. Hiding shoulders cannot by itself repair hands that leave a separately transformed gun.

## HellForge lifecycle events and a bounded reacquire module

Installed SDK header lines 530-556,628-644,673-680 and 714-724 establish these contracts. `docs/modding/runtime.md:39-42,143-153,175-177` confirms game-thread callbacks and that `on_game_thread` runs before the game's update. The native loader implementation was not located in the inspected public SDK tree; hook origin and queue timing beyond the documented contract remain unproved here.

| Event | Relevant exact payload | Recommended role |
|---|---|---|
| `HF_ON_STATE` 1 | `HfStateEvent {info,from,to}` | Invalidate world/player/rig owners; preserve user-enabled intent separately |
| `HF_ON_PLAYER_SPAWN` 4 /DOWN 5 | `HfPlayerEvent {info,peer,entity,local}`; local 1 means this machine's player, entity may be 0 | Local respawn/down schedules fresh ownership discovery or suspension |
| `HF_ON_SPAWN` 9 | `HfSpawnEvent {info,entity,network,resource,authority}`; includesremote | Filter relevant local held/attached chains; no broad per-entity frame scans |
| `HF_ON_WEAPON` 12 | `HfWeaponEvent {info,avatar,previous_avatar,entity,previous,resource,previous_resource,selection,previous_selection}` | Mark model, placement and rig identity dirty; checked at 10 Hz, so not a precise ADS/frame arbiter |

`HfEventInfo` supplies size/kind/update. Event data is borrowed for the listener only; copy IDs/scalars. Events observe game state; editing their payload does not command the game. Every machine raises its own events. The first weapon poll establishes the baseline; held entity 0 is normal before first item/after avatar loss. Inventory selection 1 primary/2 secondary/3 support/4-5 held item/0 none is not a Unit handle. Each new event-driven candidate still needs current world, player, Unit registry generation, exact object, descriptor, rig and request proof on the game thread.

Proposed modules, still research rather than implementation:

1. **Native lifecycle intent:** explicit user enablement independent of request ownership. Event notifications mark dirty; per-frame native kind/owner/controller gates decide suspension and reacquisition. A native action is allowed to own its camera. Do not rewrite desired input or call constructors repeatedly through action masks.
2. **Rig observer:** bounded copied graph topology, current ASM states/variables/constraints, native root and complete attached gun chain. It reports differences; it never replays a previous animated frame or pins individual hands.
3. **Visibility owner:** native `default` / `shadow_caster` per-mesh leases with exact object/mesh/world/attachment identity and foreign-bit preservation. Release on native action/vehicle camera takeover.
4. **Observation ledger:** finite preallocated records keyed by update, evaluation/callback phase, thread, player/weapon/rig/request generation. Include lifecycle exit/reentry reason codes rather than repeated string status. Steady frames should reuse identity-qualified discoveries, following 0.5.38's accepted performance approach, without suppressing fresh mutation proof.

No camera/ADS/vehicle-specific HellForge event was established. Those native transitions still need current-frame controller/request observation. Event-based invalidation reduces rediscovery work; it does not replace a render/evaluation-phase proof or guarantee render synchronization.

## Read-only observation plan

This plan is not executed. Use existing supported read-only buffers/observers, and obtain any additional live test authorization through the root agent. No new setter/constructor/event/constraint calls are part of this plan.

1. Revalidate build stamp, exact binary witnesses, active VM, actual userdata, player/world/avatar, held Unit object, weapon descriptor 20 bytes, FPS resource/count/hashes/links and camera request head/tail/top. Copy metadata again at the end; reject a sample if identity changed. Qualify ASM/layer count<=32 before any SDK get-state call. Prefer already-proved bounded copied raw buffers when SDK qualification is absent.
2. Capture native F11-off ADS enter/release as reference, then TFP idle/walk/fire/reload, ADS enter/release/reversal while firing, zoom cycles and weapon swaps. Capture action/vehicle entry/exit, crouch/prone/dive/interact and respawn separately. Keep saved motion/placement preferences fixed for each comparison.
3. Per evaluation/frame, copy root 0 and node 4 local/world matrices; branch 5; hands 13/34; attachment 8; camera 50; every relevant external gun/attachment child/sibling endpoint; actual camera Unit local/world matrix and external parent/link/node. Copy controller mask, bytes 4/5, actual native request identity/kind/indices, aim/seated/action flags and observed input preference only after proving its object.
4. Compare exact native CameraAPI input position/quaternion with the resulting camera world pose. Correlate root 0 depth compensation and branch 5 offsets in the same evaluation. This is the required root 0/branch 5/camera 50/world-camera test; pointer equality alone cannot establish no world-view displacement.
5. Observe qualified ASM layer state, available authored variables/constraints, wanted root pose, current native pose and mesh pass masks. Do not force the aiming pose, suppress root/bone output, reparent or hide additional meshes during this baseline.
6. Use monotonic frame/update IDs, callback phase and thread IDs to expose scheduling differences. A pre-update HellForge sample and post-animation camera callback are not simultaneous. Missing/changed-owner samples are gaps, not zero deltas. Record finite copied buffers only; retain no live engine temporaries, Unit pointers as authority or unbounded per-frame logs.

Acceptance remains on screen: hands/gun stay coherent through locomotion, recoil, reload, ADS reversal and weapon changes; native nuances remain; no view spin or stuck pitch; native action cameras are respected; return is smooth; same-scene frame time stays acceptable. Offline inventory and memory captures can identify causality but cannot certify that appearance.
