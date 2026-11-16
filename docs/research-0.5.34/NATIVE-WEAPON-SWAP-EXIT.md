# Owned native FPS request retirement after weapon swap

Static original-build25480438 follow-up only. No live process access, game
call, target write or product edit. Source line references bind the inspected
snapshot recorded in `native-weapon-swap-exit-evidence.json`.

## Bounded decision

The exit ABI takes **only the context**. Native code selects and cleans the
old top request's weapon; it does not require that weapon to remain the current
wielded weapon. Requiring `firearm(lease.origin,current)` before cleanup creates
an avoidable liveness loop after the current wield changes.

A dedicated retirement route can separate **old owned request cleanup** from
**fresh current weapon discovery**. Preserve exact same-player/context/top-ring
slot/request-header/body ownership and safe-action/real-ADS refusal. Separately
prove the old request weapon descriptor and its Unit generation are still valid
before taking the inspected weapon-present exit path. A fresh current weapon
is relevant to subsequent entry; it must not have to equal the old one to retire
the old request. Native exit also discovers the current avatar rig anew, so
old-viewmodel==new-viewmodel is not a native ABI requirement.

**Do not blanket-admit an already removed old weapon.** There is an explicit
null lookup branch, but a later flag-dependent path can still dereference the
null old descriptor. Missing-oldgun cleanup remains unqualified without a full
path proof. These static results are not live safety/visual acceptance.

## Source liveness loop

- `backend.c:107..204` discovers current wield, but rejects top kind6 when its
  request weapon/body differs from that current firearm.
- `lifecycle.c:79..85` first identifies the exact old top request using a basic
  snapshot. It then calls strict `fp_snapshot(true)` and `firearm(origin,w)` at
  lines88..90, refusing the native exit after a wield change.
- `firearm` at lines12..16 additionally requires old/new weapon descriptor,
  viewmodel and weapon descriptor bytes to match.
- `runtime.c:263..276` closes old pose policy on firearm mismatch, then waits
  for this exit before proceeding, leaving a stale native request possible.
- `native_call.c:14..24` passes only `s->context` to FP_NATIVE_EXIT.

## Exact native exit behavior

`game+0x127e9d0` uses context tail at `0x127e9f3`, chooses
`context+0x200+((tail-1)&31)*0xe8`, and checks kind6 at `0x127ea16`.
It does not first prove head!=tail. A caller must prove nonempty ownership,
including that the selected **top** row is the exact leased old request; the
function cannot be given a specific arbitrary queued request.

Before the old weapon lookup it sets native near clip to **0.05** through
CameraAPI+0x68 (`0x127ea3f`), sets context+0x168 to1, calls0x128dcc0, performs a
native world operation and resolves request+4's avatar. Avatar/descriptor map
failure skips to the final transition path. It calls avatar presentation helper
0x826450 with false at `0x127eb7d`; that helper performs multiple current-rig
and attachment operations. This is not merely a queue pop.

The old weapon is explicitly **request+8**, loaded at `0x127ebe9` and passed
to descriptor lookup0xfd9d40. The saved descriptor is placed at stack+0x48;
`0x127ebfc..0x127ebff` tests it and jumps to0x127efa2 if null, bypassing the
immediately following old-weapon component operations. This supports separating
current-wield identity, but is insufficient to qualify null throughout exit.

At0x127efa2..0x127f003, the subsequent predicate simplifies to avatar
flags+0x53e880 **bit30 (0x40000000)**. Other qword operands are ANDed with zero.
If this bit is clear, it jumps to0x127f20f. If set, `0x127f010` reads `[RSI+0xc]`,
where RSI is still the old descriptor and can be null on that branch.
`0x127f1ce..0x127f1e7` also loads saved old descriptor+8 on a deeper branch.
This pass has no proof that prior helper calls necessarily clear bit30; the
direct0x826450 body does not supply a direct53e880 bit30 clear witness.

There are further attachment/definition branches later in exit. Some are gated
by native ownership lookups; one passes the saved old descriptor to0x5092b0,
whose null entry returns0. That local null handling does not prove all later
uses of its returned pointer safe. Keep the missing-descriptor conclusion
bounded instead of treating EBFF's branch as a universal guarantee.

The final transition calls0x12771d0 at `0x12805f5`. That helper changes the old
request kind to0 at `0x12771e8` and captures current output pose/lens. Exit then
copies the saved fallback request from context+0x1f00 into the tail ring slot,
advances tail (and head if full) at `0x12806c0..0x12806e6`, and performs native
dispatch/update at `0x12806f7` and `0x1280702`. A native call can therefore invoke
SDK callbacks during cleanup; obsolete pose policies/leases must not apply old
weapon corrections through this handoff.

## Required retirement safeguards

Use fresh basic ownership to prove exact leased top kind6/avatar/old weapon/body
and unchanged ring position immediately before invocation, with current local
player/body/rig/Unit generation and safe action/ADS checks. Independently
resolve the old firearm descriptor/Unit for the weapon-present path; do not
substitute the new firearm into the old request or hand an arbitrary old
queued row to a context-only exit. Recheck ownership after the call and retain
cleanup state on interrupted/ambiguous acknowledgement. Only begin the new
weapon after native retirement is positively observed.

No old-request field overwrite, manual ring mutation, blanket missing-gun exit
or live experiment was performed in this investigation.
