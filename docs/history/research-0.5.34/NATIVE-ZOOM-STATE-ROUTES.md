# Native zoom state and view-only routes — original build 25480438

2026-11-09. Offline hypothesis audit only. No process attachment, game calls,
live memory access, hooks, game-state writes, installation, product edits or
test edits were performed.

## Decision

**Reject changing the weapon's actual selected optic while in hip True First
Person and restoring it for ADS.** The native selection is a shared, published
weapon state, not a private camera choice. Calling its native cycle function
changes that real selection. Saving it in a mod and restoring it later would
not preserve the selection while the temporary value was installed.

There **are native unzoom bypass gates** in the first-person evaluator, and a
native CameraAPI FOV setter already used by this mod. None has been established
as a supported, independently writable parameter that makes both hip input and
weapon presentation unzoomed while preserving every gameplay state and the
user's native ADS choice. This audit does not establish that such a parameter
cannot exist; the located candidates and remaining gaps are below.

The strongest new facts are the real native menu cycle and its publication
path. **Direct zeroing/ballistics coupling is not proved by this investigation.**
It must not be invented as a reason to reject the route: changing the actual
user-selected state already violates the requirement on its own.

## Original artifact and method

Source: `~/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll`,
74,727,424 bytes, SHA256
`642b0aac0b4f577054993a965054827455ef143ced9895b2dfd09d1ecd433a37`.
This is the previously captured **mapped** original game image, indexed by RVA;
its ordinary PE raw-section offsets must not be used to locate these bytes.

Capstone decodes were checked against the capture's runtime-function fragments,
grouping chained Windows unwind records. Direct E8/E9 references were searched
in `[0x1000, 0x2110a93)` and their instruction boundaries validated. This covers
direct calls/jumps in that code interval; it does **not** recover indirect
callers, all pointer tables, a complete game call graph, other binaries or live
dispatch ownership.

REA/Ghidra 12.1.2 independently decoded exact source-derived raw slices:

- Selection getter: `ev_baab03b6403d44f244b926aeaece735a053c3b26a3a5d4dc5ef057c2678ff62a`.
- Packed parameter getter: `ev_8f2bd53bc3f2e65c20d2f950a045fa840c2eca07981b6436e44369ee4f80d93a`.
- Native parameter cycle: `ev_d11327027d9be9d4fea5993aa06d46bcf3b93ddd7399d501ab17b4403f87d94f`.

The slices were bound at their actual RVA and preserved byte-for-byte. Ghidra's
raw generic GCC compiler profile assigns incorrect Win64 parameter ordinals in
its pseudocode; the register-level ABI below comes from the actual assembly.
The short selection-getter slice has an outside-slice tail target, which Ghidra
labels bad/truncated pseudocode. The full-image Capstone witness verifies that
tail jump and its callee. These limitations are retained in the evidence bundles;
no pseudocode warning is treated as runtime behavior. All REA sessions were closed.

Machine-readable witnesses, call-site inventories, hashes and retained artifact
paths are in
[native-zoom-state-evidence.json](~/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge/docs/research/native-zoom-state-evidence.json).

## The selection is a weapon parameter

`game+0x1787500` has the observed register contract:

```c
/* Inferred from original Win64 assembly; not a declared SDK function. */
uint32_t get_weapon_parameter(uint32_t weapon_entity, uint32_t parameter_kind);
```

ECX is the weapon entity and EDX is the parameter kind. It checks that the
weapon exists, resolves its row from the **WeaponData live manager** at global
`game+0x3326ce0`, and searches its four configured option kinds:

```text
manager+0x58 -> rows, stride 0x3f0
row+0x350, +0x354, +0x358, +0x35c -> option kind identifiers
kind 1 -> optic selection
```

When it finds the requested kind, it tail-calls the generic getter at
`game+0x755ba0`, with RCX=manager, EDX=weapon entity and R8D=the option slot.
For kind 1, the generic getter reads:

```text
manager+0x60 -> packed state rows, stride 12
selected = (packed_row.word_at_4 >> 4) & 3
```

Exact original sites: `0x178759d` calculates the 0x3f0 row; `0x17875a4` adds
0x350; `0x17875b0..bf` scans four kinds; `0x17875d5..e3` tail-dispatches;
`0x755c69..78` reads/shifts the packed word; `0x755f3a` applies `&3`.

The C port's `src/depth.c:135-155` independently reads the same manager row,
packed stride and bits. Its `pool_bytes` starts at manager+0x30, so its relative
`pool_bytes+0x30` is the same **manager+0x60** pointer, not another copy of state.

Other kinds share this packed row, including programmable-ammunition options.
Changing only bits 4..5 would preserve those neighboring bits, but would still
change the real selected optic. Replacing the whole word would additionally
risk changing unrelated weapon options.

## The multiplier does not offer an unzoomed argument

`game+0x759b10` resolves the weapon definition through `0x509a40`, then calls
`0x1787500(weapon, 1)` at `0x759ba9`. It returns:

| Selected result | Definition member returned |
| --- | --- |
| 0 | float at +0x74 |
| 2 | float at +0x7c |
| 1, or another result reaching this switch | float at +0x78 |
| Weapon lookup fails before definition resolution | native constant at game+0x23c6d70 |

Thus **UINT32_MAX/“no optic parameter” does not generally mean unzoomed**:
the successful weapon-definition route falls through to the middle member.
Nor is index 0 guaranteed to be widest. The minimum must be determined from the
actual three definition values; the selected minimum is fully unzoomed only if
its magnification is 1. This audit did not acquire current weapon definitions
from live memory.

There is no separate `scoped_only`, `view`, `hip`, `ADS` or override argument in
this multiplier's observed call path. Its first incoming RCX value is not used
to select a private view state; EDX supplies the weapon. Its selection getter
reads the same real packed state.

All four direct multiplier calls in the bounded code range were validated:

| Call site | Containing native root | Observed use |
| --- | --- | --- |
| 0x1278f20 | 0x1278cc0 | Native presentation-distance calculation |
| 0x127f4a7 | 0x127e9d0 | Normal/exit camera path |
| 0x1286a46 | 0x1286330 | Native first-person evaluator |
| 0x12896a0 | 0x1289180 | Another native camera evaluator |

## Real native menu route and side effects

`game+0x755200` is a slot-to-kind adapter. It takes the weapon from EDX and
option slot from R8D, resolves the live manager, reads the configured kind at
row+0x350+slot*4, then tail-calls `0x7552d0` at `0x7552bb`.

`game+0x7552d0` is the actual cycle operation, with RCX=manager, EDX=weapon and
R8D=kind. Its kind-1 branch at `0x75537e..3d5` implements:

```text
old_word = packed_row.word_at_4
new_selected = (((old_word >> 4) & 3) + 1) % 3
packed_row.word_at_4 = (old_word & ~0x30) | (new_selected << 4)
publish(descriptor.unit_ref_at_0x10, 0x31f86165, &packed_row.word_at_4)
```

The literal original store is at **0x7553b9**. The publication call at
**0x7553d0** goes to **0xfd97e0**. That function builds a record containing the
unit identity, property identifier and data pointer, queues it for consumers,
and can drain the queue through `0xfddec0`. Its exact persistence/network
semantics are not fully established here; **publication beyond the camera is
observed**, and the actual packed write is unambiguous.

The weapon-options UI/input root at **0x1825f40** reads a configured option kind
at **0x1827084** and directly calls this native cycle at **0x182708e**. The nearby
UI path updates per-option feedback. The other nonrecursive direct cycle caller,
`0x6cab51`, passes kind **12**, proving why this generic operation must not be
treated as an optic-only function.

A native creation/serialization path at `0x75d020` also initializes the packed
word (literal **0x50** at `0x75d11a`) and emits its pointer with property
**0x31f86165** (`0x75d134..145`). This is another witness that the word is a real
weapon state participating in native publication. It is not evidence that the
selection is written to a disk settings file.

“Call the native cycle twice, evaluate hip FPS, then restore” still temporarily
changes this shared state and emits notifications. A direct masked store would
avoid the native notification while leaving the shared-state change visible to
other readers. CAS or a mod-side save does not serialize all game consumers or
protect a simultaneous user option change. Neither route meets the stated
preserve-selection requirement.

## Native unzoom gates that do not change the optic bits

The first-person evaluator contains an important narrower lead:

1. At `0x12864d0`, it calculates `avatar_index*0x78`. At `0x1286a37..42`, it
   reads bit 0 of `avatar_owner+0x547830+index*0x78` into a temporary flag.
2. It obtains the selected multiplier at `0x1286a46`, retaining copies in XMM8
   and XMM10, and computes the selected view lens from settings TPV FOV /
   multiplier at `0x1286a76..90`.
3. At `0x1286aa6`, it queries the weapon-related helper `0x776010`. It publishes
   the result into **request+0x8f**, resets request+0x88 on changes, and may call
   native distance recalculation when the result becomes false.
4. If either that request flag or avatar bit is set, `0x1286ad9..6afa` instead
   uses **the unzoomed TPV FOV** for the local lens and sets **XMM8 to 1**.

This proves native **lens bypass without changing the packed selection is
possible internally**. It does not establish a safe mod control for it:

- The avatar record is shared state. Its flag is read by avatar update and HUD
  paths (`0x82f424`, `0x17b7687`), and the record participates in whole-record
  copies/serialization (`0x83abe7` and neighboring stores). A full displacement
  scan found 48 matching instructions in this code interval; that is not a
  complete alias analysis. Flipping it is not established as view-only.
- `request+0x8f` is a **derived flag**, overwritten from `0x776010` during each
  native evaluation. That helper has 34 validated direct calls across several
  systems, resolves weapon/owner/component state, and compares a component
  definition value with `0x7cbe60`. It is not a dedicated camera setting API.
  This study does not assign the flag an unproved gameplay name.
- The bypass **does not replace XMM10's selected multiplier**. Native camera
  positional/preset quantities are still divided by it at `0x1286ba1`,
  `0x1286baf`, `0x1286bc1` and `0x1286bc6`. A lens bypass alone therefore does
  not prove coherent unzoomed weapon placement.
- The native input ratio consumes request+0x84 **earlier**, at `0x1286851`,
  before the selected lens computation; the final native lens update is later
  (`0x1286d09`). Changing a final lens or derived bit late in evaluation cannot
  be assumed to correct that evaluation's already-consumed input.

The next offline question for this lead is its exact producer/ownership and
whether a **private request-local view parameter** can select that native branch
before input without falsifying avatar/weapon state. That is **unresolved**.
No candidate writes or live tests were performed.

## Nonvisual effects: evidence boundaries

| Aspect | Established | Not established |
| --- | --- | --- |
| User option selection | Cycle changes the shared stored index and publishes it; menu uses that operation | Disk settings persistence was not traced |
| Native input sensitivity | Native6 reads request lens/TPV FOV ratio, device scope gain and native steering; actual selected multiplier contributes to the later native lens | Stable widest state does not by itself prove every configured sensitivity policy is correct |
| Steering | Previous original-build investigation traced the native steering producer and camera projection boundary; this evaluator preserves its separate steering term | All steering effects of an alternate native state/gate are not proved; no blanket “pure visual” claim is justified |
| Weapon presentation | Selected zoom controls native lens, selected depth coefficients and camera/preset positional quantities | A lens-only bypass does not normalize all placement quantities |
| Fire rate / programmable ammo | They are separate parameter kinds sharing the same option/packed-state machinery; kind-8 callers select alternate projectile data | A bits-4..5-only change has not been shown to directly alter fire rate or projectile kind |
| Crosshair / HUD | Generic conditional crosshair selectors inspect parameter kind/value pairs at definition+0x194; selected options also feed the native UI | Whether a specific current weapon conditions its crosshair on kind 1 requires its actual definition |
| Zeroing | SDK declares ProjectileWeapon zeroing members +0x10/+0x1c/+0x20, explicitly **unverified** and authored/respawn-scoped | No fully traced original runtime path connects packed optic bits 4..5 to bullet zeroing. The SDK labels do not establish that relationship |
| Firing / physics | Real selection is globally published; view/input changes can change the resulting aim direction | No proof of unchanged projectile speed, gravity, spread, muzzle convergence or zeroing under a temporary state. No direct physics/zeroing consumer was established by the bounded direct-getter search |

All **13** direct calls to `0x1787500` were classified in the evidence JSON.
Only three explicitly pass kind 1: `0x74c84b`, `0x759ba9`, `0x1278f0f`.
Others request kinds 7, 8, 12, or a data-driven parameter kind. Some data-driven
callers can therefore consume optic selection for particular definitions.
Those observations do not amount to an exhaustive nonvisual side-effect proof.

## SDK and module choices

The installed SDK16 typed function header has no `SetOpticZoom`,
`GetOpticSelection`, scoped-only view gate or equivalent declared function.
`HfApi.function` resolves only a **mapped name**, and `hook_function` uses the
loader's existing function-hook mechanism (`hellforge_mod.h:1396-1405`). Naming
a custom C wrapper around `0x7552d0` would not make that shared-state mutation a
view-only SDK capability. Using an SDK function hook must not be presented as
meeting the no-inline-hook constraint merely because the call has an SDK name.

The existing **CameraAPI+0x80 FOV setter** is an actual view argument and does
preserve the selected weapon state. The C port already intercepts its exact
final-camera caller in `src/fov_policy.c:18-39`. It changes projection late in
the pipeline; it does not independently change the earlier native input ratio
or all native weapon-placement calculations.

A selective **return-value filter** at exactly the camera's selection-query
call sites could conceptually leave the packed word and generic gameplay/UI
queries untouched. The retired Steam module `native/zoom/native_ads_zoom.cpp`
implements that concept with an inline selector hook. Its source itself marks
that distinction, and the Lua runtime quarantines the policy after crash
reports. It is **not an acceptable existing solution under the present hook
constraint**. No equivalent original-build writable SDK selector pointer was
found in this bounded investigation; absence of an indirect route remains a
bounded negative result.

The acceptable architectural direction remains **native camera ownership plus
view-local policy**. The actual-optic-swap hypothesis is rejected. The native
unzoom gates are research leads requiring further state/producer evidence, not
a finished implementation or a reason to change the user's selection.
