# Native exit and a replaced current FPS rig

Bounded static follow-up for original build25480438. No live access, game call,
product edit or installed change. Exact bytes, API-table entries and bounded
disassemblies are in `native-current-rig-exit-followup-evidence.json`.

The old/new viewmodel equality at the inspected `lifecycle.c:105` is **not a
native exit ABI requirement**. Native exit resolves the current rig again from
the avatar manager. Replacing this equality with fresh current-rig lifetime
proof is a defensible retirement design; deleting it without such proof is not
the same thing. Current Talon visibility/attachment has succeeded in the live
session, so this guard is not established as the present failure's cause.

## Current rig is separate from outgoing weapon

- Exit `0x127eb1b..0x127eb7d` supplies request+4's avatar and false to helper
  `0x826450`. Entry supplies the avatar and true at `0x127e12e..0x127e146`.
- Helper `0x826450` remaps that avatar in the current avatar manager. At
  `0x826550`, R13 becomes current avatar row+`0x53e9b8`; `0x826574` reads its
  current FPS Unit. Current body Unit comes separately from current avatar
  descriptor+`0xc` at `0x826557..0x826570`. No leased old FPS Unit is passed.
- `0x82657c` calls UnitAPI+`0x658` on that current FPS Unit. The original API
  table maps this to EXE`0x205680`, the native function used by
  `Unit.set_unit_visibility`; it queues a World operation. Its exact integer
  and boolean arguments are evidence, not a proved global hide/show meaning
  for every pass or all child meshes.
- Current presentation accessories get queued unlink commands through
  WorldAPI+`0xf8`, then queued link commands through +`0xd0`. `0x82677a..0x8267ab` selects the current
  body Unit when false or current FPS Unit when true. This is active current
  hierarchy/presentation work, not merely retirement of an isolated old row.
- The equipment loop visits slots1,3,6 and calls helper `0x9a9fc0` with the
  inverse presentation boolean. It **skips** a slot when the current selected
  slot at equipment row+`0x1c` equals that slot (`0x826b4a..0x826b4f`). Slot
  resolution is explicit in the six-entry jump table at `0x8274e8`.
- `0x9a9fc0` looks up the slot's weapon descriptor, then routes its Unit through
  UnitAPI+`0x668` or +`0x658(0,0)`, plus optional component operations. The +668
  target EXE`0x205700` queues a distinct operation; no Lua name or universal
  enable/reset meaning is claimed here.

After this current-presentation helper, exit independently looks up the
**outgoing request+8 weapon** at `0x127ebe9..0x127ebff`. A resource-dependent
path calls `0x757480` on that outgoing weapon at `0x127ee6e`; this helper
clears its two-byte-per-weapon presentation flag at `0x757a5b` and performs
definition-dependent native operations. Entry's related `0x756d50` route also
uses its explicit weapon. These facts do not prove that exit writes the newly
selected current-wield ID or unconditionally reselects the outgoing gun.

## Consequences and qualification limits

Exit can change the **current** FPS rig and current presentation attachments
while retiring an old request. It therefore can interfere with a newer draw or
presentation transition if invoked at the wrong time. That is a supported risk
from the call structure, not proof of the current visual cause, nor proof that
every new held gun is unconditionally hidden or detached.

A changed-rig retirement route should retain exact old top-request/context/
ring/avatar/body ownership, safe action and real ADS refusal, and independent
old weapon descriptor/Unit generation proof. Keep the existing missing-old-gun
refusal: the earlier flag30/null-descriptor hazard remains unresolved.

Qualify the **current** FPS Unit's generation/object and graph metadata, then
close the current rig identity again before invocation. Compare current
`s.viewmodel` with the closing current snapshot, rather than with
`lease.origin.viewmodel`. Completed gun8/camera50 attachment cannot be required
as a universal precondition for retirement: native exit itself queues hierarchy
changes, and pending-link states are one reason retirement may be needed.

Only the exact old top request can be retired by this context-only exit. If
native code has already replaced or adopted a different request, relinquish
the old lease instead of exiting that successor. Disarm old placement callbacks
before native exit, whose final updates can invoke SDK callbacks. Observe new
weapon/rig/attachments separately after retirement; a kind4 acknowledgement
does not prove that every queued World link/visibility operation has completed.

This pass inspected the direct helper, its relevant jump-table leaves, the
slot-visibility helper and bounded weapon-presentation bodies. It did not prove
all transitive calls, asynchronous command-consumer ordering, or live draw-phase
ownership. No native cleanup call or weakening of a runtime guard was tested.
