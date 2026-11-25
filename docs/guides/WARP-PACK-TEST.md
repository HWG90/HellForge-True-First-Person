# Warp Pack glow: the first game run (round 22)

What to do in the game, what each log line should say, and when the stop can be switched on by default. The addresses and
the evidence are in `docs/design/specs/2026-11-24-warp-pack-glow.md`. Nothing in this round has run in the game yet.

## Before the run

1. Build: `TMPDIR=$HOME/.cache bash build.sh --feel`. The DLL is `dist/true-first-person-feel/goose_true_first_person.dll`.
   Install it as usual, but only when the user says so. This round does not write to the HellForge folder.
2. Put an empty file `warp_hide.txt` beside the installed DLL. Without it the module only watches: the `Warp:` lines still
   come, and nothing is stopped or written.
3. Check that full-body mode is off. In full body the pack's meshes stay drawn on purpose (head_visibility.c:905-913), and
   `hidden_backpack` stays 0. The stop works either way.

## Getting the Warp Pack on the player

There is no proven way on this build to spawn a backpack into the player's hands:
- `HARNESS.md` ("The HD2 runtime in a test session") found no call in the HD2 runtime that spawns a pick-up.
- LLtM's `hd2_*` spawner kit is not in the HellForge prefix, and its addresses are retail's.

So use the game's own way, as the user did when the bug was seen:
1. Put the Warp Pack stratagem in the loadout before the mission.
2. In the mission (a difficulty 1 solo mission is enough), call it in and pick it up.

The Bingus harness can start the mission (`HD2H_TARGET=hellforge python harness.py start --preset light`, `ship`,
`mission --difficulty 1`). Picking the loadout and calling the stratagem in are done by hand: stratagem codes do not
register from SendInput keys (HARNESS.md).

## The run

Do all of this twice, with the camera style on Stable and then on Bodycam. View on (U) for every step unless the step
says otherwise.

| # | Do | Look for |
|---|---|---|
| 1 | Start, before the pack | `Test: warp_hide.txt: …` once at the start. `Warp: no Warp Pack (backpack none)`. `Check: backpack: none (snapshot player; inventory row +0x0..+0x14: … [+0xc backpack 0xffffffff or 0] …)` |
| 2 | Pick the Warp Pack up | `Check: backpack: found entity=N renderer=R descriptor=680e9b5d795697b4…` (the descriptor's first 8 bytes are the type B49756795D9B0E68 in memory order). Then `Warp: pack N: displacement row i of n, settings the type's; mode table: mode 0 (tag 0) play […] stop […]; mode 1 (tag 1) …; mode 2 (tag 2) …; K distinct ids`. Then `Head: … hidden_backpack H …` with H > 0 (not in full body) |
| 3 | Stand still | `Warp: pack N (row i) bit48=0 ads_cache=0 mode_word=0x0 charge=… effects=held playing […] of K ids; code ok; view owned, switch on; did: stopped S […]` once, then `did: quiet (nothing of the stop lists plays)` once a second. **The glow is gone.** |
| 4 | Charge the pack | `mode_word` goes to 0x1, then 0x2. Each change brings one `did: stopped 1 […]` line, then `quiet`. No glow, or a flash of a single frame |
| 5 | Warp | The same as 4. The view stays first person, camera kind 6 (`Head: kind=6` and the `View:` lines) |
| 6 | Full aim down sights, then release | While aiming: `bit48=1 ads_cache=1`, and the game hides the glow itself. The line says `did: the game's full ADS` if the view stays TFP's while aiming, or `did: restored` then `not owned` if TFP hands the view to the game's ADS. Either is right. On release: `bit48=0 ads_cache=0`, then a `did: stopped …` line (the game replays at the falling edge, and the next tick stops it again) |
| 7 | Third person (U off), then back on | Off: `view not owned … did: restored: the cached mode word set to 0xffffffff`, then `mode_word=0x0` (or the current mode) again. **The glow is back in third person.** On again: `did: stopped …` |
| 8 | Photo mode, if it is installed | The same as 7: the glow comes back while the view is not TFP's |
| 9 | Drop the pack | `Warp: pack N: restored: …` then `Warp: no Warp Pack (backpack none)`. **The pack on the ground glows.** Pick it up again: step 2 and 3 again |
| 10 | Die with the pack on | Either the view stays (camera lock) and the pack stays stopped, or the pack drops and step 9 applies. No crash |
| 11 | A second player, if there is one | They still see your glow. The stop passes replicate = 0, so nothing goes over the network |

## Reading a bad result

- **`code <name> at game+0x… differs` or `is unreadable`**: the module stops nothing. Send the line; the game's code is
  not what the listing says.
- **`did: refused: …`**: the reason is in the line. "does not hold the pack" or "is not the pack's" means the backpack read
  and the displacement manager disagree.
- **The glow stays while the line says `playing []` or `did: quiet`**: the glow is not one of the table's events. Two
  possible causes:
  - a one-shot warp effect or the second path (`0x88be00`). Note which step it was;
  - the pack's mesh (an emissive part). Look at `hidden_backpack`.

  The next round's fallback is the rig's `World.link_particles` hook, filtered on the pack's unit.
- **A `did: stopped` line every second with the same ids**: the game plays them again every frame, so the glow would
  flicker. Send the log.
- **The glow stays gone in third person after step 7**: the restore did not take. Check that `mode_word=0xffffffff`
  appeared in the line, then send the log.
- **`Check: backpack: none` with the pack on**: send the line. It carries the row's six words; if the pack's entity shows
  in a word other than +0xc, the slot is not the one the game's switch says.

## When to switch it on by default

Switch it on by default only when one run with `warp_hide.txt` has shown all of these, in both camera styles:

- code ok;
- the glow gone in steps 3 to 6;
- the glow back in steps 7 to 9;
- no `refused`;
- no repeating `did: stopped`;
- no crash.

Then the next build reads `warp_off.txt` instead, and this guide is updated. Until then it stays off.
