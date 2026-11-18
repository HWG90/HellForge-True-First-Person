# Original-build native weapon visibility ABI

Static original captured build25480438 only. This work issued no live call, attached no observer, wrote no target memory, and changed no project source. Byte evidence accompanies this report. EXE addresses below are RVAs, added to the current independently verified EXE base, not game.dll base.

## Correct table and signatures

Obtain UnitAPI from the real SDK root cell at game+0x3326308, then root+0x18. Original static UnitAPI table is EXE0x27c8f10. Relevant Win64 C routes are:

| UnitAPI slot | EXE target | Proved Win64 arguments | Effect |
| --- | --- | --- | --- |
| +0x258 | 0x203540 | uint32 UnitRef | Queued World operation0x18, animation constraint work. Not visibility. |
| +0x658 | 0x205680 | uint32 UnitRef, int visible, bool extraMeshSystems | Queued World operation0x15, all-pass mask0xff. |
| +0x660 | 0x205750 | uint32 UnitRef, uint32 passId32, int visible, bool extraMeshSystems | Immediate whole-Unit visibility in selected pass. |
| +0x668 | 0x205700 | uint32 UnitRef | Queued World operation0x17: restore cached local mesh flags. |
| +0x670 | 0x2057b0 | uint32 UnitRef, uint32 passId32 | Immediate cached restore; target vtable+d8 ignores the decoded pass mask, so restores all cached flags. |
| +0x678 | 0x205900 | uint32 UnitRef, uint32 zeroBasedMeshIndex, uint32 passId32, int visible | Immediate single-mesh setter; updates Unit cache. |

All routes are void. The whole-Unit Lua wrapper EXE0x412980 (`Unit.set_unit_visibility`) accepts (Unit,boolean,[boolean=true]). It shifts encoded native Unit userdata by2, converts arg2 via lua_toboolean and arg3 via lua_toboolean only when arg3 is a Lua boolean; otherwise arg3 defaults true. This establishes that second native int is boolean visibility, not a mesh index/pass selector. The third boolean gates the Unit+e0/+e8 extra mesh-subsystem loop. It does not traverse linked Unit children.

Pass decoder EXE0x1fc180 maps id32=0 to0xff; default0xf2760503->1; shadow_caster0x3100c3d2->2; cubemap0xa63da7cc->8; occluder0xe4ff316b->16; trample0x2f8235d5->16; other nonzero IDs->0. These are actual code constants, not guessed strings.

## Consumer and terminal operation

Queued routes resolve UnitRef through EXE0x9d8c0, dereference Unit+0x18 World, reserve one 0x58-byte World record with lock xadd(World+0x170038), and fill fields at World+0x10038. Producer opword is record+0x50. These producers do not null-check the resolved Unit before dereferencing it.

Consumer EXE0x12ea20 reads these records. Operation0x15 at0x12f64c resolves the current generation-qualified UnitRef and skips a null result; it invokes Unit object vtable+d0, target0x2c41d0, with (UnitObject,visible,extraMeshSystems,passMask). Operation0x17 at0x12f627 calls Unit vtable+d8, target0x2c4760, with (UnitObject,passMask). The implementation ignores that second register.

Whole visibility0x2c41d0 iterates local Unit mesh count+0xc8 and invokes vtable+c8 for every local mesh using the supplied passMask. It additionally updates other local render systems; it is not limited to the current gun receiver or a single variant. The per-mesh implementation0x2c3990 finds Mesh at Unit+0xd0+16*index, calls0x50f620, and optionally calls cache updater0x2c2ed0.

Mesh setter0x50f620 changes Mesh+0xdc by OR mask for visible or AND complement for hidden. If a renderer attachment exists at Mesh+8, it queues render command kind3 carrying Mesh+4 object ID, Mesh+0xd8 and the resulting Mesh+0xdc byte. Updating this byte directly would miss the native render command.

## Cached restore preserves patterns, with limits

Restore0x2c4760 reads one cached visibility byte per local mesh from Unit+0x100 (length Unit+0xf8), then makes the actual Mesh+0xdc equal that cached byte and queues native kind3 render commands. It ends by running cache updater0x2c2ed0. It does not traverse the linked Unit child tree and does not restore the additional mesh-subsystem loop gated by whole setter's third argument.

The cache updater collects current flags A[i], computes G=OR(A[i]), then updates cache C[i]=(C[i]&~G)|A[i]. Thus a pass absent across ALL local meshes retains its earlier cached per-mesh pattern; a pass present on ANY mesh replaces that pass's pattern across the cache. A whole-hide executes its per-mesh clears first and only then runs cache update; globally cleared default/shadow passes therefore retain their previous cached pattern. A per-mesh zero change cannot clear an old cached bit if that pass has become globally absent.

This is a meaningful native restore operation, not blindly show-all. Original GAME0x9aa0a9 loads weapon descriptor+0xc UnitRef. Its true branch calls UnitAPI+668 at0x9aa0b5; false branch calls +658(UnitRef,0,0) at0x9aa0c6. The original game itself pairs cached restore with whole-weapon hide.

Cached restore also restores cached shadow bits. A queued +668 followed immediately by immediate +660 shadow-clear does NOT prove reset-then-shadow-clear order: the reset is still pending. Immediate +670 reset then immediate +660 shadow-clear has program order on a qualified game thread, but adds an invocation route that requires its own runtime qualification. Reset cannot recover default visibility if the cached default OR is also0. It cannot promise a stable or authored pattern if the cache was already contaminated by show-all or other changes.

## Intervention choices and guards

For native SDK pointer interception, the supported narrow protection is suppress qualified current-weapon whole-hide requests. Converting hide to show through +658 forces all pass0xff bits on every mesh, enables authored-hidden variants, and reintroduces shadows. Hooking +658 does not catch immediate +660 calls, direct Unit vtable writes, WorldAPI routes, or Lua direct mesh setter routes. No complete all-writer coverage was proved by this bounded pass.

Qualify exact original EXE/game identity and expected SDK slot target/bytes before ownership; preserve foreign slot owners using compare-and-swap. Require callback entry epoch captured before active/original reads and locked lease publication, as with existing policies, so a delayed old callback cannot adopt a reopened generation. Forward unqualified native calls with exactly the original arguments.

For an owned write/call, require current strict firearm snapshot, same local avatar/body/descriptor/weapon Unit generation/object, completed current FPS gun8/camera50 attachment when presentation policy requires it, same World, native on-foot TFP request ownership and controls. Qualify linked gun attachments individually through current parent/child chain and registry generation; do not include all FPS rig descendants or arbitrary weapons. Refuse retired/unknown/unreadable state and close metadata before invoking the native API on game thread. Native enqueue wrappers dereference resolved Unit without checking null.

Read cache length/pointer/count and actual mesh pointer array under matching Unit identity before choosing restore; bound counts. Read cache/Unit identity again afterwards. A usable cached pattern is evidence of native state, not proof of authored appearance or live render acceptance. Match same native setter slot ownership during cleanup, and do not restore stale visibility over a later authoritative game write.

## Existing World inventory failure and mesh Lua arity

Parent independently proved World.units callback pushlightuserdata encodes UnitRef*4+1 at EXE0x3eafe3. Existing models.c's old encoded%4==0 filter rejected every such Unit despite targets resolving natively; that explains count0 after target_count15. Native Unit wrappers decode by shifting2. The candidate now has the tag1 correction; this pass did not edit it.

Existing `Unit.set_mesh_visibility` call with four Lua arguments (Unit,1-based mesh index,bool,pass string) is correct. Callback0x4128e0 jumps helper0x4124d0; final dispatch0x4128b7..0x4128cc invokes native vtable+c8(UnitObject,zeroBasedIndex,visible,passMask,updateCache=true). Omitted pass is0xff; explicit default/shadow_caster confines the bit. No fifth Lua argument is needed. The old gun mask2 only clears shadows and never recovers defaultbit1; fixing inventory alone cannot make an entirely hidden gun visible.
