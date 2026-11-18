# Native projection, viewport and shader-pass investigation

Target: **Helldivers 2 build25480438**, original captured mapped EXE. This is read-only static analysis: no live attachment, native invocation, hook installation, SDK trial or game-file change. `native-projection-evidence.json` retains the exact bytes and decoded instructions for 49 selected wrappers/native bodies (2,454 instructions), typed conversion observations, data fields and unresolved edges.

## Answer supported by the code

The SDK has real **per-camera projection commands**, real **multiple viewport/render-target machinery**, and real **per-mesh/world shader flags**. It does **not yet establish a native weapon-only projection or clipping domain**. Every inspected camera setter addresses one Camera pointer and one camera render ID. If the main scene and first-person gun use that same ID/pass, changing its FOV/frustum/post-projection/near distance affects the shared camera. Finding a setter is not proof that it can safely isolate the gun.

The most useful next step is to identify the actual native camera/viewport/render-pass pairings and renderer flags while the game itself enters native ADS. Native animation, camera/recoil and lifecycle should remain authoritative during that observation.

## 1. The actual post-projection route

`Camera.set_post_projection_transform` is registered at EXE+**0x41e630**. It converts Lua argument1 with `lua_touserdata` to a Camera pointer, converts argument2 similarly, adds **4 bytes** to the matrix userdata, and calls native EXE+**0x30da00**. The native ABI inferred from the complete body is:

```c
void set_post_projection_transform(Camera *camera, const float matrix[16]);
```

This is a static ABI description, not permission to pass numeric UnitRefs or arbitrary userdata. The SDK matrix representation has its own prefix; `matrix_userdata+4` is the 64-byte float payload. A C engine bridge can preserve the game's real userdata/handle domains rather than fabricate them.

A concrete **native C SDK pointer slot** is available. Two initializers independently load EXE+0x30da00 and store it to EXE+**0x27c98c0**:

- LEA/store at **0x1cd118 / 0x1cd11f**.
- LEA/store at **0x30ed3e / 0x30ed45**.

The static CameraAPI table base is **0x27c9820**, aligned with the already used position setter+0x20, near getter+0x58/setter+0x68, and FOV getter+0x78/setter+0x80. Thus the post-projection setter is **CameraAPI+0xa0**. Current live SDK-root identity and the slot's exact function still need a read-only check before treating that static layout as current runtime authority.

### It is a queued render command, not a host matrix assignment

The native setter follows Camera+8 to the owning world/manager, obtains a command allocator through owner+0x98, allocates **0x58 bytes**, sets envelope type**2**, links the command into a queue with an atomic exchange, and writes this payload:

| Payload offset | Value |
|---|---|
| +0x00 | object kind **4** |
| +0x04 | camera render ID from **Camera+4** |
| +0x08..+0x47 | all **16 matrix floats** copied from the supplied buffer |

The command therefore carries an exact camera identity. There is no host-side current post-projection matrix assignment in this setter and no public post-projection getter in the inspected Camera inventory. The render-side consumer/baseline remains unresolved. **Restoring an assumed identity matrix is not a proved restoration strategy**: native jitter, projection offsets or other owners may have supplied a different baseline.

The pointer route makes an approved passive SDK observation possible without an inline patch, but a future observer would still need generation, camera/ID, thread, argument lifetime and dispatch ownership guards. No such observer was installed here.

## 2. FOV, clip, explicit frustum and projection type

These camera fields are directly established by getters, setters and queued payloads:

| Camera offset | Type | Established role |
|---|---|---|
| +0x04 | uint32 | camera render-command ID |
| +0x08 | owner pointer | route to render command manager |
| +0x18 / +0x20 | graph pointer / int32 | world-pose graph and node index used by render_world |
| +0x28 / +0x2c | float / float | near / far distance |
| +0x30 | int32 | projection type |
| +0x34 | float | vertical FOV |
| +0x38,+0x3c,+0x40,+0x44 | four floats | explicit frustum values |
| +0x50 | uint32 | frustum form:1 ordinary,2 half-angle setter |

The near getter at **0x30d940** and FOV getter at **0x30d980** simply return their respective fields. Near/far/projection changes queue through **0x4cfa10**: envelope type0, payload kind4, cameraID, then the near/far pair and projection type. FOV/frustum changes queue through **0x4cfab0**: envelope type1, payload kind4, cameraID, then camera+0x34..0x53.

`Camera.set_frustum` (**0x41e9a0**) accepts camera userdata and numeric Lua arguments2–5. Their float values are written in order to +0x38,+0x3c,+0x40,+0x44; +0x50 becomes1. `set_frustum_half_angles` (**0x41eb80**) uses the same fields, negates arguments2 and4, and sets +0x50 to2. Both then queue the descriptor. Both convert an optional sixth argument to an integer but do not use that result in their inspected paths. Exact geometric argument names, sign conventions and angle units should be corroborated with native caller data rather than guessed.

`Camera.set_projection_type` (**0x41e3a0**) converts argument2 to an integer, writes +0x30, then queues near/far/type. Its wrapper does not establish valid enum bounds or gameplay ownership. These are shared camera values; no mesh or weapon selector is present in these setters.

### Two misleading SDK entrypoints

`Camera.set_mode` (**0x41f3b0**) is literally `xor eax,eax; ret`. It cannot select a real gameplay first-person state. `Camera.mode` pushes constant integer0. `Application.update_render_world` (**0x3f8370**) is also an immediate-return binding on this image. The engine exposes some compatibility stubs; names alone cannot justify a feature.

## 3. Viewports, render targets and render_world

`Application.create_viewport` (**0x3f9aa0**) takes a world userdata and a string/hash-like configuration selector. It passes the native World pointer and a32-bit identifier to **0x76b40** through the application global. The constructor looks up a render configuration, allocates a **0x90-byte viewport**, initializes it from the selected configuration, and registers it in the World's viewport list at **World+0x2308f0 / +0x2308f8**. The matching destroy route is **0x1b3d20**, which removes the viewport from that list and releases its associated allocations. This is substantive resource/lifecycle work, not a safe getter.

Relevant viewport fields and commands:

| API | Native target | Established behavior |
|---|---|---|
| `Viewport.set_rect` | **0x332fd0** | writes four floats at viewport+0x18..0x24; queues type0/kind0xb + viewportID + rectangle |
| `Viewport.set_output_render_target` | **0x3332c0** | updates output pointer+0x78 and associated value+0x38; queues type1/kind0xb + viewportID + pointer |
| `Viewport.register_render_resource` | **0x333480** | queues type3/kind0xb + viewportID +32-bit resource-name ID + opaque resource pointer |

`Application.render_world` is a thunk at **0x3f80e0** to body **0x3f7e90**. The first arguments are a World userdata, Camera userdata, Viewport userdata and an additional opaque userdata. Later optional arguments select opaque render resources/configuration names through string/hash/boxed-userdata branches. The fourth argument is commonly called a shading environment in SDK usage, but this pass does not independently prove that semantic type.

The wrapper calls world preparation **0x318090**, then world render **0x318150**, then renderer backend **0x107400**. World render uses **the supplied Camera's graph+0x18 and node+0x20** to read a64-byte world matrix and stores it at **World+0x230920..0x23095f**. The actual supplied camera and viewport are passed onward together. The backend constructs render resources/jobs and copies data into deferred commands. Optional resource overrides lead to indirect dispatch that remains explicit in the evidence.

This establishes the mechanics for additional views and outputs. It does **not** establish a built-in first-person-only scene, an isolated weapon mask, the appropriate render configuration, independent weapon near clipping, depth clearing/compositing, temporal-history handling or compatibility with the game's native render scheduling. Rendering the same world a second time without those contracts could simply render the scene twice.

## 4. Shader-pass flags are not visibility/shadow byte masks

`Mesh.set_shader_pass_flag` (**0x488e70**) has the observable signature:

```text
(Mesh userdata, flag-name string, enabled truthiness)
```

It hashes the name with the engine hash function, uses the upper32 bits, and searches the registry at **EXE global0x1a102d0** (count at header+0, hash-array pointer at+8). It sets or clears the resulting bit in the **64-bit mesh+0x88 mask**, then calls native **0x50f700**. That function updates the stored mask and queues envelope type1, kind3, mesh render ID and the64-bit mask.

`World.set_shader_pass_flag` accepts a world, a string/hash-like name and enabled truthiness. Native **0x334f70** uses the same registry and updates **World+0x230578**. When its render owner is active, it queues a corresponding world-level command.

**A missing registry name produces index−1, and register BTS/BTR maps that to bit63.** The wrapper does not reject the name. Therefore a guessed string such as a presumed first-person or shadow flag cannot be used to infer semantics safely. Read the live registry and correlate the actual resource/configuration definition first.

The existing native mesh visibility route (**0x50f620**) acts on the **byte mask at mesh+0xdc**. It is a different field and command from shader flags. The established body/default/shadow visibility policy must not be silently replaced with shader flags just because their name contains “pass.” A shader flag might help select a real viewmodel pass, but its existence, bit meaning and render-config use remain unqualified.

## 5. Concrete read-only live work

1. Verify the current game's CameraAPI+0xa0 pointer and code witnesses. Record the actual Camera pointer, cameraID and owner across ordinary third person, native ADS, mod FP, weapon swaps and action/vehicle transitions.
2. Copy camera near/far/FOV/projection/frustum fields, graph identity and node index without changing them. Determine whether native ADS/FP creates a different camera render ID or only changes the existing camera's parent/parameters.
3. Enumerate the bounded World viewport list and record each viewport's ID, owner, configuration, rectangle and output target. Correlate actual camera/viewport render pairings through a separately approved passive observation only if pure reads cannot answer it.
4. Copy the shared shader-flag registry, current FPS rig/gun mesh+0x88 flag masks, their separate byte visibility masks and World+0x230578. Match names/hashes against actual render resources. Do not call setters with trial strings.
5. Locate a render-side post-projection baseline or observe native setter inputs with exact ownership before proposing an owned matrix lease. Establish asynchronous command acknowledgement and retirement/restoration behavior.
6. Only after these contracts are known, design a small C module and offline fixture for the relevant native path, then coordinate a reversible live trial. Visual acceptance must cover weapon/world composition, native recoil/animation, ADS reversal, weapon/action/vehicle changes and restoration.

Useful module boundaries for that work would be `native_camera_api` (typed SDK slot and owner proof), `native_projection_observer` (copied native camera/projection samples), `render_domains` (camera/viewport/mesh membership), and `shader_flag_registry` (validated registry names and masks). These are proposed roles, not implemented or live-qualified features.

## Evidence scope

The exact source image and SHA-256, named Lua-export corroboration, per-function direct control-flow graphs, call targets, field witnesses and unknown edges are recorded in `native-projection-evidence.json`. PE unwind scopes split some functions into several records, so the extractor follows direct branches instead of treating one unwind entry as the entire body. It does not pretend indirect dispatch or GPU shader semantics are reconstructed.

The earlier inventory's Lua table-write helper label has been corrected from `lua_settable` to **`lua_rawset`** at EXE+0x13f79e0. Its exact registration bytes, function names and counts are unchanged. The installed Lua DLL export RVAs corroborate104 captured Lua import slots; this comparison is not a current live-pointer observation.


## Read-only native observer module

`src/native_observer.c/h` now provides the stateless C API:

```c
bool fp_native_observe(const FpMemory *memory, FpNativeObservation *out);
```

It is intended for an explicitly enabled diagnostic in the existing **owned game-thread callback**. The main module owns sampling cadence, timestamps, log/file output and user coordination. The observer itself has no engine calls, hooks, setters, allocation, recording loop or file output. It never turns numeric UnitRefs into Lua/SDK handles.

A true result means the exact build matched; the core player, selected camera request, ring head/tail, all20 request bytes, controller bytes and avatar flags passed closing consistency checks; and every flagged available component passed its closing proofs. Availability is independent for `FP_OBSERVE_CAMERA`, `BODY`, `RIG`, `LENS` and `WEAPON`. Unknown/uninitialized components stay zero and are explicitly marked unavailable. A false result discards all component data and records a typed failure.

The camera component contains its generation-proved graph, camera render ID, graph node/link, current world matrix and external-parent graph identity when applicable. The body component uses the shared `FpBodySample` head/hash/link proof and copies root/head positions and root basis. The strict known63-node FPS graph supplies world matrices for **nodes0,5,50**. The lens component reads only the proved near/far, projection type, vertical FOV, four frustum values and frustum mode; its near/FOV getter byte witnesses are checked. No unproven orthographic or post-projection field is guessed.

The optional weapon component adds the strict current wield descriptor snapshot, generation-proved weapon graph/node0 world matrix, external parent graph/link, parent node (`weapon_link>>16`), and that attachment node's world matrix. It rechecks the current weapon descriptor, Unit identity, graph and parent at closing. An unavailable weapon does not invalidate an otherwise stable core sample; a weapon that retires after being sampled rejects the observation. This helps compare native FPS nodes0/5 against the actual gun/attachment world state, while still not granting same-evaluation authority.

Closing validation re-resolves the Unit registry/generations and graph pointers/topology, checks the camera node/render-ID/parent, verifies body head metadata and strict rig metadata, rereads lens values and closes the owner/request state. **This does not prove the matrices and lens belong to one native evaluation or render frame.** A game-thread snapshot can contain stable cached poses from different engine phases; diagnosing co-motion still needs timestamps and, if required, a separately qualified observation at a native evaluation boundary. Successful copies are diagnostics, never replay/restore authority.

`tests/native_observer_test.c` deterministically covers the complete snapshot, explicit unavailable rig/lens, a valid FPS camera parent, rig/body graph replacement, rig/camera/weapon Unit generation retirement, weapon descriptor replacement, valid FPS node8 weapon attachment, missing weapon, low- and high-bit request changes, controller changes, lens changes and wrong-build/bad-argument refusal. Linux C11 with AddressSanitizer+UndefinedBehaviorSanitizer passed:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I src src/backend.c src/scene.c src/presentation.c src/native_observer.c tests/native_observer_test.c -lm -o /tmp/tfp-native-observer-test
ASAN_OPTIONS=detect_leaks=0 /tmp/tfp-native-observer-test
```

The optimized asserted Windows x64 fixture also passed under the isolated Wine test prefix, using the same four C modules and `-O2 -UNDEBUG` with the installed Zig compiler. No existing build script or runtime source was modified by this subtask; main must review and integrate the module and diagnostic output separately. The observer has not been loaded into or exercised against the running game.


Main integration extension: observer rig observations now also include native hands13/34 and socket8 under the existing strict rig graph/closing proof. The Windows and sanitizer fixture seeds those matrices explicitly. The research recorder remains session-only20Hz/20seconds, no automatic rearm on a VM change, and its schema is separately tested. The original observer hashes in this research evidence identify the initial delivered module, not these subsequent integration additions.
