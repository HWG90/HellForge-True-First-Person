# Native viewmodel rendering routes — build 25480438

> Historical dev.3 investigation. References below to the current root-carrier policy describe dev.3. Dev.4 forwards root0 unchanged and applies qualified placement/depth only to branch5; see ../FEATURE-COVERAGE.md and ../LIVE-TEST.md for current behavior and pending acceptance.

Date: 2026-11-09. This is static analysis of the original captured EXE and game.dll. It made no live attachment, engine calls, writes to game memory, hooks, runtime changes or installation changes. The accompanying `native-viewmodel-render-routes.json` preserves exact instruction bytes, object-table entries, identity constants and source hashes. This investigation narrows existing routes; it does not establish visual acceptance.

## Supported conclusion

`CameraAPI.set_post_projection_transform` has a real render-side consumer. It stores a matrix on **one render Camera**, then the camera update multiplies that matrix into that Camera's projection. The setter carries **CameraID**, with no Unit, mesh, weapon, pass or layer selector. It is therefore not an established way to reposition only the first-person rig while retaining the main world's projection.

The native FPS constructor reuses the camera context and requests its camera Unit root to attach to the FPS rig's camera node. The equipped gun is separately attached to the same rig's weapon socket. The inspected path supports a coherent shared scene hierarchy. It does not introduce a separate viewmodel Camera. However, the actual active draw submissions and any shader-specific projection overrides have not been observed, so this is **not a whole-engine proof that every visible FPS mesh always uses exactly one world projection**.

The narrowest supported future comparison is to keep native root 0 and camera node 50 unchanged, and move only the native visual branch 5 with its attached gun. This uses the existing hierarchy; it does not require a new projection pass. Whether removing only the current root-carrier depth compensation gives the desired placement remains a hypothesis that needs current attachment and timing evidence.

## Sources and method

| Source | Identity and use |
|---|---|
| Original EXE mapped image | `/home/goose/Applications/HellForge/mods-retired/localappdata-1019/LocalAppData/LinksLinkToMemory/r6_reload_exe_20261017.bin`; SHA-256 `2b2c496218a5ce7e4f3453951acb8bd756bc4c2f281ce95eadef17b894e39c16`; command producers, renderer dispatch, Camera constructors, projection update |
| Original game.dll mapped image | `/home/goose/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll`; SHA-256 `642b0aac0b4f577054993a965054827455ef143ced9895b2dfd09d1ecd433a37`; FPS constructor and camera/gun attachment requests |
| Earlier source-backed research | `NATIVE-PROJECTION-DEEP-DIVE.md`, `NATIVE-RIG-LIFECYCLE-DEEP-DIVE.md` and their JSON evidence; SDK wrapper arguments and native visibility APIs |
| Current source | `src/scene.c`, `src/pose_policy.c`, `src/native_observer.c`; graph qualification, current paired depth policy and observer scope |

Both captures are mapped images. Offsets are RVAs in the named binary. A game.dll RVA is not an EXE RVA. Direct control-flow traversal follows conditional branches and direct jumps, preserves exact bytes and reports unresolved indirect branches. A PE unwind entry is not assumed to cover a whole function. Calls are followed only when explicitly included as separate records. The new artifact contains 19 function records and eight selected instruction windows; it is a focused trace, not a complete renderer decompilation.

## Camera command ownership and consumer

The producer already established in the projection report is EXE **0x30da00**, reached by Lua callback **0x41e630** and the static CameraAPI **+0xa0** slot. It:

1. Resolves the command manager through host `Camera+8`, its pointed owner, then owner **+0x98**.
2. Allocates a **0x58-byte** envelope; writes envelope type **2** at header **+0xc**.
3. Writes payload object kind **4**, CameraID from host **Camera+4**, and 64 matrix bytes at payload **+8**.

The renderer's object-command dispatcher begins at EXE **0x1e84a0**. The queue payload comes from header plus the offset stored at header+0xe. It switches on payload object kind minus one. The camera's index 3 in jump table **0x1e9484** resolves to **0x1e941a**. That is a generic object route, rather than a discard:

```text
payload+4 CameraID
    -> mask from renderer+0x150, field+0x40
    -> object-index array renderer+0x1c0
    -> object-pointer array renderer+0x170
    -> object vtable+0x18(object, envelope_type, payload)
```

The render Camera vtable is **0x166aa70**. Its **+0x18** entry is **0x4d0ea0**. The render Camera constructor **0x4cfb50** installs that exact vtable and object kind 4, and copies host near/far/projection/frustum fields into the matching render object. These independent witnesses identify the handler as the actual camera command consumer.

| Envelope type | Exact behavior in consumer 0x4d0ea0 |
|---:|---|
| 0 | Copy payload+8 near/far pair to render Camera+0x20; copy projection type from payload+0x10 to +0x28 |
| 1 | Copy payload+8..+0x27, 32 bytes, to render Camera+0x2c..+0x4b |
| **2** | **Copy payload+8..+0x47, 64 bytes, to render Camera+0x284..+0x2c3** |
| 3 | Copy 64 bytes from payload+0xc to render Camera+0x1f8; role not recovered here |
| 4 | Copy 64 bytes from payload+8 to render Camera+0x238; this transform is consumed with the graph world pose during camera update |
| Other | Return without modification in this handler |

Envelope 3 and 4 are not discovered weapon-only routes. Their producers and complete ownership contract have not been recovered. The common object handler itself is selected by CameraID.

## Projection update and restoration limits

Render Camera update is EXE **0x4cfe40**. Its actual body extends through **0x4d0e92**. It stores viewport/aspect inputs, obtains or composes camera world pose, calls native projection builder **0x4ce830** using render Camera **+0x20** near/far/type and **+0x2c** FOV/frustum descriptor, and stores the resulting 64 bytes at **+0x138**.

The same update then reads the matrix at **+0x284** and multiplies it into that **+0x138** projection. This is scalar arithmetic in the body, not an inferred meaning from an API name. For flat arrays P at +0x138 and M at +0x284, the first stored result at **0x4d0876** is:

```text
Pnew[0] = P[0]*M[0] + P[1]*M[4] + P[2]*M[8] + P[3]*M[12]
```

The surrounding body calculates and stores the remaining elements. This formula states the actual memory convention without imposing a different row/column-vector convention on the game. There is no per-Unit selector in this composition.

Constructor **0x4cfb50**, explicit-pose constructor **0x4cfbf0**, and the renderer's inlined camera creation at **0x1da9a0** initialize +0x284 from constants **0x190d730..0x190d76f**. Those constants are exactly the 4x4 identity matrix. This proves a **construction default**. It does not prove that a currently active Camera still has identity there, or that identity is safe to restore after native/foreign code has changed the current state. No public post-projection getter was identified in the original SDK inventory. A future ownership lease would need current render Camera identity, its current matrix and a reliable current-owner/restoration policy.

Relevant render Camera fields proved by this trace:

| Offset | Meaning or exact operation |
|---|---|
| +0x8 | Object kind 4 |
| +0x10 | Graph node index; -1 selects explicit pose route |
| +0x14 | Graph render ID copied from host Camera's graph |
| +0x18 | Render graph pointer |
| +0x20/+0x24/+0x28 | Near, far, projection type |
| +0x2c..+0x4b | FOV/frustum descriptor |
| +0xb8..+0xf7 | Camera world-pose matrix |
| +0xf8..+0x137 | Matrix returned from helper 0x62d670 using the world pose; likely inverse/view matrix, not required for the projection conclusion |
| +0x138..+0x177 | Projection matrix, including post transform after update |
| +0x178..+0x1b7; +0x1b8..+0x1f7 | Cached copies of the preceding two matrices on the inspected first-view path |
| +0x238..+0x277 | Graph-composed transform; envelope 4 writes it |
| +0x284..+0x2c3 | Post-projection transform; envelope 2 writes it |

These offsets describe a **render-side** Camera object. They must not be read from the host Camera pointer as if both objects shared a layout.

## View submission and camera choice

`Application.render_world` reaches native **0x318150**, which takes supplied World, Camera and Viewport objects. It reads the host Camera's graph **+0x18** and node **+0x20**, copies that graph world pose into World+0x230920, and forwards the Camera/Viewport to backend **0x107400**. The backend explicitly reads **Camera+4** at **0x10747c** and **Viewport+4** at **0x107483** into its submission data.

The renderer's view processing resolves its selected camera render ID at **0x1eab56..0x1eab7c** through the same renderer object arrays, then calls **0x4cfe40** on that Camera at **0x1ead16**, using viewport-dependent dimensions/aspect. This is a real camera/view association. Full end-to-end mapping from a particular gameplay submission to the on-screen world and gun still requires observations of actual submissions; this report does not silently bridge unresolved scheduling/queue boundaries.

Native viewport creation, rectangle/output-target changes, resource registration and multiple render-world calls are implemented. Those are useful low-level components for a separate view. They do not establish an existing FPS-only scene or mesh filter, the depth that should be tested against the world, temporal history, motion vectors, correct transparent passes, or composition ordering. Creating a second view without those contracts would not be a qualified native viewmodel solution.

## Native FPS camera and weapon pairing

The FPS constructor in **game.dll 0x127de20** keeps its input camera context in RSI from **0x127de54**. At **0x127e114**, it calls current `CameraAPI+0x78` on **context+0**, the existing Camera pointer, to read FOV. The inspected entry path does not construct a new separate viewmodel Camera.

Two attachment sequences are particularly useful:

| Sequence | Exact constructor arguments |
|---|---|
| Camera at **0x127e52f..0x127e574** | Look up node hash **0x60ed8c39** on avatar FPS rig; request context+8 camera Unit **child node 0**, parent FPS rig, parent node returned by lookup |
| Gun at **0x127e5a4..0x127e647** | Test/resolve socket hash **0x4fdd3337** on FPS rig; request equipped weapon descriptor Unit **child node 0**, parent FPS rig, resolved socket node (fallback 0 if absent) |

Original UnitAPI table **0x27c8f10** provides **+0x6d0 = 0x1fe590** (has-node) and **+0x6d8 = 0x1fe5e0** (node lookup). Their bodies search the Unit scenegraph hash array. This independently supports the interpretation of the constructor's hash lookups, rather than labelling the hashes from names alone.

World API accessor **0x33a220** returns table **0x27cab40**. Its +0xc8 method **0x3362c0** enqueues a link operation with child Unit/node and parent Unit/node; +0xf8 **0x336500** enqueues an unlink. The constructor uses those slots through its SDK root+8 table. Runtime table identity and completion of the queued operations must still be qualified; these calls are requests, not immediate graph-completion proof.

The existing qualified 63-node topology in `src/scene.c` maps those hashes to **camera node 50** and **weapon socket node 8**. Both visual branch **5** and camera **50** are children of node **4**. Socket 8 is under node 7, beneath branch 5. Root **0** is an ancestor of both. Therefore, with the actual current graph/attachment verified:

- Moving root 0 can affect both the visible rig and camera 50.
- Moving branch 5 can move the arms/socket/gun hierarchy while leaving sibling camera 50's transform unchanged.
- Moving the whole rig by a projection matrix on its main Camera is not selective: that matrix belongs to the Camera's projection.

This topology does not prove every accessory is linked to socket 8, that a queued link already completed, or that all cameras use node 50 during every action. The observer's generation/graph/parent closing proofs remain necessary.

## Narrow branch-only depth comparison

The current C policy applies its depth delta twice: `position_call_stack` adds the correction to branch 5's local forward component; `carrier_call` subtracts a corresponding forward-vector correction from root 0. The latter root is also above camera 50. Exact CameraAPI pointer forwarding cannot by itself eliminate this separate camera displacement route.

A future comparison can remove **only the root-carrier compensation**, retaining the native branch 5 transform, native root/Camera inputs, native animations and the existing qualified attached gun hierarchy. Under a common, rigid ancestor frame and matching update phase, shifting the visual branch forward with an unshifted eye can produce the same relative gun-to-eye displacement as shifting the eye backward while cancelling the gun's world displacement. This is the root agent's hypothesis, not an implemented or accepted result.

Required conditions before testing that comparison:

1. Current weapon root has an external link to **this FPS rig's node 8**, with matching registry generation, graph world pointer and current native request. A fallback socket 0 or attachment to another rig invalidates this particular hypothesis.
2. Current Camera Unit root is attached to **this rig's node 50**; root 0, node 4, branch 5 and camera 50 have the expected topology and finite transforms.
3. Native branch/root callbacks represent compatible phases of the same evaluator. Pre-update game-thread copies do not establish this. Record caller/request/thread/evaluation context before attributing a delta.
4. The comparison preserves natural recoil/reload/hand/socket transforms and every attached gun part as one hierarchy. It must not pin individual wrists, freeze animation output or independently replay gun transforms.
5. On-screen check covers movement, firing, reload, ADS release/reversal, zoom, weapon swaps and actions. A relative-position metric alone is not a jitter or feel verdict.

No runtime code was changed here. In particular, this report does not recommend immediately enabling the comparison based only on static topology.

## Options and remaining gaps

| Option | Evidence-bounded status |
|---|---|
| Preserve native camera/root and move qualified branch 5 plus attached gun | Existing hierarchy route; narrow comparison described above is plausible and observable; outcome untested |
| Camera post-projection, FOV, frustum or near distance | Real per-Camera operations; useful for an intended whole-view change, not an established weapon-only adjustment |
| Mesh `default` / `shadow_caster` visibility | Native pass bits are established in the rig report; usable for local render/shadow policy; no separate projection implied |
| Separate Viewport/RenderTarget | Real low-level infrastructure; missing FPS-only membership, depth, temporal and composition contracts |
| Guessed shader flags or Camera.set_mode | Unsupported. Unknown shader flag names have a real invalid-index hazard; Camera.set_mode is a literal no-op on this build |

The decisive missing observation is the actual camera/view/draw ownership: active CameraIDs and ViewportIDs, render Camera pointers/fields, actual FPS mesh membership and any material-level projection override. Static investigation of the camera consumer does not exclude such an override elsewhere. Conversely, SDK names alone do not prove one exists. The finite current observer can first resolve root0/5/50, gun socket8 and host Camera world relationships without new native calls. Any later render-phase observer needs a separately qualified lifetime/thread boundary; this report has not added one.
