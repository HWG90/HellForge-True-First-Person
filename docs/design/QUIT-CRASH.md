# The crash while the game closes (helldivers2.exe+0x66a371 / +0x66a403)

Seen on HellForge build 25480438 on 2026-11-20 in 5 of 11 harness sessions, always after the session was
over ("ended by quit with exit code 1", access violation on the main thread). Codex's notes blame the rig
trial ZIP for the same address. Neither mod is the cause as far as the evidence goes.

## What happens

The game's Lua `shutdown` reaches `game.dll+0xab7d90`, which destroys a large object at
`[game.dll+0x3326340] + 0xE4A30`. Its destructor (`game.dll+0x55e1f0`, through `+0x565353` and `+0x873a70`)
releases the array `{pointer, count}` at `[game.dll+0x3326340] + 0xF846C8`.

That array holds 420 pointers and sits alone at the start of a 64 KiB-aligned 4 KiB mapping: it was not
handed out by the engine's block allocator. The release goes through the allocator wrapper at
`helldivers2.exe+0x5c2ed0` into the block allocator's free (`+0x66a2f0`, rpmalloc-shaped: the span header is
taken to be at `pointer & ~0xFFFF`, here the array itself). The free reads array entries as header fields:

- `[span+0x28] & 4` is set (low byte of entry 5), so it "re-aligns" the pointer by
  `(pointer - span - 0x80) mod [span+0x24]`, and `[span+0x24]` is the high half of entry 4;
- when the 420 objects sit below 0x2_0000_0000 that high half is 1, the remainder is 0, and the free only
  scribbles inside the array's own page: a clean exit;
- when they sit at 0x3_xxxx_xxxx the remainder is 2, the write goes to `pointer - 2`, which is unmapped:
  the crash (`edx = 0xfffffffe` in the dumps).

## Evidence

| Session | View used | Entries point at | Exit |
|---|---|---|---|
| control (mod loaded, F11 never pressed) | no | 0x1_e73b… | clean |
| run 6 | yes | 0x3_2cf4… | crash |
| run 7 | yes | 0x1_e303… (read before F11; unchanged by F11, swap, grenade, stim, reload, F11 off, leaving) | clean, as predicted |

The array pointer and its entries did not change at any point of a session. Where the 420 objects land is
decided at start-up.

## More sessions (ship only, the view never used)

feel.16, no working guard: 6 sessions, entries at 0x3_… in 2, both crashed; at 0x1_… in 4, all clean.
The fault needs neither the view nor a mission.

## The guard (feel.18)

Mapping the page below the table does not work: that memory is the unusable tail of the allocation before
it (feel.16: `VirtualAlloc` refused in 5 of 6 sessions).

The release routine `game.dll+0x873a70` frees three `{pointer, count}` pairs of its argument (+0x00, +0x10,
+0x20). For this object the first and third are 64 KiB-aligned blocks, 0x10000 apart; the second is an
ordinary block. feel.17 emptied only the first pair and the fault moved to the third (`+0x66a403`, called from
`game.dll+0x873ae3`).

feel.18 hooks the routine (after checking its first 20 bytes). When it is called with
`[game.dll+0x3326340] + 0xF846C8`, every pair whose pointer sits exactly at a 64 KiB boundary is emptied
before the engine's own code runs, so that pair is skipped; the block allocator can never have handed out such
a pointer. All other calls pass through.

| Build | Sessions with entries at 0x3_… | Crash dump at +0x66a371/+0x66a403 |
|---|---|---|
| feel.14, feel.16 (no guard) | 7 | 7 |
| feel.17 (first pair only) | 5 | 5 |
| feel.18 | 4 (one of them a full mission with the view used) | 0 |

One feel.18 session of the four still ended with exit code 0xC0000005 and no crash dump: a later fault, after
the crash reporter had gone, not yet looked at.

## Open

- The late fault without a dump (1 of 9 feel.18 sessions).
- The guard lives in True First Person; the fault does not need this mod. It belongs in a small mod of its own.
