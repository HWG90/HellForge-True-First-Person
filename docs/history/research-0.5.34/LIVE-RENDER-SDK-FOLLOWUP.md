# Live-confirmed render SDK follow-up — build 25480438

Date: 2026-11-09. This follow-up reads the existing live SDK census and original captured EXE only. It makes no live calls, attachment, component creation, renderer requests, memory writes, runtime changes or installed-file edits. Exact evidence is in `live-render-sdk-followup-evidence.json`.

## Result

**RenderDataComponent is the useful discovery lead.** Its existing component instances contain finite typed property blocks that can be copied with protected reads and decoded without guessing property names. The Lua getter itself is a lookup by supplied path, not a schema enumerator. No actual block or FPS component membership was sampled here.

**MeshComponent.get_property is unhelpful on this build:** its native provider always returns an empty variant. **ShadingEnvironmentBlendComponent.get_property exposes only priority and opacity**, not camera/pass/depth settings. **Renderer.resource(name) returns an opaque renderer pointer, requires renderer synchronization and can insert a missing map key.** It is not suitable for guessed resource-name calls in a read-only probe.

None of these findings establishes a separate FPS projection, a per-view mesh filter or the draw CameraID used by the gun.

## Evidence input and limits

The current census input is:

`~/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/logs/sessions/9472b065334c40c9a443d56cb03a3279/game/true-first-person-sdk-g1.tsv`

It contains 2,645 rows and 1,947 function paths. Its footer reports 143 tables, 123 aliases, 38 omitted values and `complete=0`. Thus it confirms the selected callback addresses; it is not represented here as a complete inventory of everything callable. The root agent separately established that all 1,223 earlier static paths match this launch.

| Confirmed path | EXE callback |
|---|---:|
| Renderer.resource | 0x417cd0 |
| RenderDataComponent.get_property | 0x467410 |
| MeshComponent.get_property | 0x467410 |
| ShadingEnvironmentBlendComponent.get_property | 0x466230 |

The original mapped EXE is `~/Applications/HellForge/mods-retired/localappdata-1019/LocalAppData/LinksLinkToMemory/r6_reload_exe_20261017.bin`, SHA-256 `2b2c496218a5ce7e4f3453951acb8bd756bc4c2f281ce95eadef17b894e39c16`. File offsets are RVAs. The JSON contains 33 direct-control-flow function records, four selected construction/allocation windows, exact provider vtable bytes and 2,772 verified instruction records. Calls are not recursively assumed understood; indirect dispatch cases are included separately when interpreted.

## Renderer.resource: named lookup with synchronization and insertion

Callback **0x417cd0** accepts argument 1 using `lua_tolstring`, hashes its bytes through **0x5d5840**, takes the upper 32 bits, and schedules callback **0x4178a0** through renderer task enqueue **0x1107c0**. It then calls **0x105e40** and **0x106030**, which enqueue/wait on renderer synchronization state, before returning the resulting pointer through `lua_pushlightuserdata`.

The queued task obtains the named entry from **renderer+0x1600**, using helper **0xa6e30**, and copies its pointer value to the waiting result cell. The returned object is opaque: no resource type, camera, mesh membership, pass, depth format or schema accompanies it.

Helper **0xa6e30** is a lookup-or-insert operation. Its existing-key fast path returns the value cell, but a missing-key path grows/rehashes through **0xa7b60**, increments map count at **+0x18**, and writes keys and links into map entries. Consequently, `Renderer.resource("guessed_name")` is not a strictly passive read, even though the public function looks like a getter. There is no reason to use it as a speculative schema scanner.

A protected copy of **existing** map key hashes/pointers is a possible later observation if the renderer owner/thread/lifetime boundary is qualified. The present game-thread observer does not supply that renderer synchronization proof. Such a copy would enumerate hashes and pointers, not typed resource descriptors or original names.

## Actual component providers and handles

The `EntityManager` factories resolve the current World userdata and return/cache Lua userdata containing the relevant native manager pointer. They do not create a game component. Their creation of Lua wrapper/cache objects should not be confused with `MeshComponent.create` or `RenderDataComponent.create`, which are separate mutators.

| Factory callback | Native World field | Provider vtable | Native get-property slot +0x30 |
|---|---:|---:|---:|
| EntityManager.render_data_component, 0x4638d0 | +0x230c78 | 0x1677dc8 | **0x2dd7f0** |
| EntityManager.mesh_component, 0x462b60 | +0x230c50 | 0x16786d8 | **0x305e30** |
| EntityManager.shading_environment_blend_component, 0x462d50 | +0x230ca0 | 0x16788f8 | **0x30ace0** |

The World constructor **0x1a5640** installs these exact vtables and stores the corresponding provider pointers in those fields. This links public callbacks to their actual native implementations, rather than assuming all providers behave alike because two Lua callbacks are aliases.

RenderData/Mesh callback **0x467410** jumps to generic implementation **0x466ff0**. Its arguments are:

```text
get_property(manager_userdata, entity_userdata, instance_id, property_path)
```

The manager userdata contains a pointer. The entity userdata is decoded by shifting its encoded value right two bits. The instance ID is read with `lua_tointeger`, then provider vtable **+0x38** resolves entity plus instance ID to an internal nonzero row. Only a found row reaches provider vtable **+0x30**, which returns a 24-byte tagged variant.

Property paths can be a string or an array of strings. String paths are split on dots by helper **0x62a0c0**. Each segment's native 32-bit hash is combined using the observed `0x5bd1e995` mix. The string-array route computes the same combined hash. Passing an unknown numeric/path type does not enumerate properties; the inspected path uses hash 0. Non-string array entries are not a qualified input.

ShadingBlend callback **0x466230** jumps to **0x465de0**, the single-instance version. It obtains the entity's row through **0x62a450**. A numeric third argument makes the path argument 4; otherwise the path is argument 3. The single-instance provider lookup ignores the requested instance ID. This overload is still a supplied-key lookup.

Entity component handles and native Unit references are separate ownership domains. Both wrappers using an encoded-userdata shift does not prove those handles are interchangeable. Do not pass a rig Unit Ref as component entity userdata. `UnitComponent.unit` callback **0x472a50** is a possible existing-entity bridge: it resolves the entity's UnitComponent and returns an encoded Unit Ref through provider method +0x98. Qualify component presence first; the wrapper itself does not visibly reject internal row 0 before calling that method. A current, alive entity-to-current-rig mapping has not been established in this task.

## Mesh getter: always empty

Native **0x305e30**, the +0x30 slot of the proved Mesh provider, ignores the requested property and instance. Its entire body builds a tag-0 variant and returns it. The generic Lua marshaler returns **zero Lua results** for tag 0.

Therefore, `MeshComponent.get_property` cannot discover mesh resource, camera, pass or depth properties through this route on the current build. A successfully loaded callback is not evidence that its public-looking operation is useful. The provider has existing mesh-pointer arrays used by other methods, but their relationship to the local FPS Unit's meshes is not established by this getter.

## ShadingBlend getter: two scalar fields

Native **0x30ace0** recognizes exactly two stored property hashes:

| Property path | Captured hash | Hash global | Backing value |
|---|---:|---:|---|
| priority | 0x2ee3e067 | 0x235eac8 | manager+0xd0 array, uint32 read and converted to float by generic getter |
| opacity | 0x4e049f61 | 0x235eac4 | manager+0xd8 array, float32 |

The names are corroborated by canonical native path-hash calculation and the dedicated `priority`/`opacity` callback bodies, **0x46e5a0** and **0x46e740**, which read those same arrays. The dedicated priority callback returns a signed Lua integer; the generic property route converts the stored uint32 to a float. Unknown property hashes return tag 0.

This is a blend-controller interface. It does not reveal the shading environment's complete render properties, and it supplies no camera/pass/depth schema for a weapon model.

## RenderData: finite typed property blocks

Native getter **0x2dd7f0** reads:

```text
block = manager->property_blocks[internal_instance_row]  // manager+0xe8
```

A null block returns tag 0. Otherwise it calls **0x1f7280**, which linearly searches the existing property-hash array and decodes a matching record. This lookup has no key-insertion path. The allocation/growth path was separately inspected at **0x2dd5e0..0x2dd713**, and clone helper **0x1f6f10** confirms the block's allocated-size field.

| Block field | Proved layout |
|---|---|
| +0x0 | uint32 allocated byte capacity |
| +0x4 | uint32 property count `n` |
| +0x10 | `n` uint32 combined property-path hashes |
| +0x10 + 4*n | `n` records, 16 bytes per record |
| record+0x0 | uint32 variant tag |
| record+0x8 | scalar bits or packed relative-data information |

The native-to-Lua variant marshaler is **0x465040**, with an explicit tag switch at **0x465150**:

| Tag | Property block decode / Lua result |
|---:|---|
| 0 | Empty; no Lua results |
| 1 | Boolean byte at record+8; Lua boolean |
| 2 | float32 at record+8; Lua number |
| 3 | uint16 block-relative offset at record+8 to a NUL-terminated string; Lua string |
| 4 | uint16 block-relative offset at record+8, uint16 byte length at record+0xa; one Lua number per float32 element |
| 5 | Integer bits at record+8; marshaler uses signed low 32 bits for Lua integer |
| Other | Remain raw/unknown; marshaler returns no results for values above 5 |

This offers a bounded **present-property census**: hashes, tags and actual values can be copied without fabricating a path. It does not recover original names from hashes. String values such as an authored shading-environment mapping can identify a real asset/schema to inspect; the existing mod already uses exact `shading_environment_mapping` for vignette ownership. A property table on a scene shading entity is not automatically attached to, or selective for, the FPS rig.

No `properties` or schema-enumeration member appears in the captured RenderData, Mesh or ShadingBlend namespaces. Passing an empty path is still a lookup for its computed hash, not a request to enumerate the root.

## Bounded read-only observation worth considering

The following is a plan only. It adds no calls or recorder to the current pending manual-F11 check.

1. Qualify the original build, exact callback bytes, current World and corresponding provider pointer/vtable. Reuse valid existing userdata/entities; do not call create/destroy/set-property/resource-generation APIs.
2. Start from a finite existing entity/instance list. Prove entity alive, instance ID and internal row through the provider's existing lookup structure. Bound every pointer, chain and array operation. Do not equate Unit and entity IDs.
3. For an existing RenderData block, validate capacity and count; require `16 + 20*n <= capacity`. Apply explicit capture limits, for example 256 properties and 64 KiB per block, as observer budgets rather than asserted engine limits.
4. Copy the bounded existing block, then close World, manager/vtable, entity/instance row, block pointer and header identities. Reject pointer/count/size changes. A protected copy with matching closing metadata still is not proof of one native evaluation or immutable data during the copy.
5. Decode only proved tags. Require every relative payload to remain inside the captured buffer, tuple lengths to be divisible by four, and string NUL terminators to occur within a bounded captured range. Keep unknown hashes/tags raw. Never dereference arbitrary values as resources from their shape alone.
6. Resolve names from actual authored assets or exact known strings whose complete path hash matches. Collect mapping strings and scalar values first; avoid guessed shader/resource names. If tying a result to FPS, independently prove the entity's UnitComponent maps to the current generation-qualified FPS Unit.

This is useful for discovering real shading/property data and deciding what is unrelated. It does not replace the already planned root0/branch5/camera50/weapon-parent8 observation. Actual draw-camera identity, per-view mesh membership and any shader-level projection/depth override remain open.
