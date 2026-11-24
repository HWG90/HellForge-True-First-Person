# Aim test (round 20c)

One game run that answers what both round 20 aiming fixes were guessing at: whether the unzoom gate flips in full
aim, what each scope zoom level does to the sight distance and the depth, and whether the field-of-view value the mod
is handed lags the lens. Everything is read from the `Aim:` lines in the mod's log. The design and what each answer
decides: `docs/design/specs/2026-11-22-locked-view-design.md` "Round 20c".

What this build changes by default: nothing you can see in full aim. Aim field of view is the game's zoom and Sight
distance in aim is 0, so full aim looks as it did on feel.51. The placement editor now works while aiming.

## Before you start

- Settings: Field of view lock on, Aim field of view "The game's zoom", Sight distance in aim 0. Keep the rest as you
  play.
- Weapons: a scoped rifle with a scope range setting (the R-4 Hyena if you have it, else another one with 25 / 50 /
  100 m ranges), the LAS-5 Scythe (iron sights), and a pistol.
- Write down roughly the time you start each part. The log is
  `logs/sessions/<newest folder>/mod-goose.true-first-person.log` in the HellForge folder.
- Quit the game between the passes below: test files are read only when the game starts.

## Pass A: without `zoom_steady.txt` (the default)

Make sure there is no `zoom_steady.txt` beside the DLL. Sight distance in aim stays 0.

1. **Scoped rifle, in the open** (nothing within 5 m in front of you). For each scope range, 25 m, then 50 m, then
   100 m:
   - aim and hold still for 5 seconds, then let go;
   - aim, and while aiming change the range once up and once down, holding 2 seconds after each change.
2. **Scoped rifle, at a ledge or a wall.** Stand within a metre of a rock edge or wall, facing it. At 50 m and at 100 m:
   aim for 5 seconds; then aim at open ground and slowly turn until the wall is right in front of the scope, and back.
3. **Aim taps.** With the scoped rifle at 100 m: ten quick aims, each well under half a second.
4. **Scythe and pistol.** Aim each for 5 seconds.
5. **The editor in aim.** With the scoped rifle, aim and keep holding aim, press Page Up. Drag the weapon left and
   right, scroll the wheel both ways. Note whether the aim stays while the editor is open. Save. Close the editor, let
   go of aim, aim again: is the change still there? Then open it in aim again, move it, and cancel: it should go back.
   (If you do not want to keep the saved change, move it back in the editor and save again.)

## Pass B: with `zoom_steady.txt`

Quit, put an empty file named `zoom_steady.txt` beside the DLL, start the game. The log says
`Test: zoom_steady.txt: ...` at the start. Repeat steps 1 to 3 of pass A. Say whether the view in the scope looks
steadier, the same, or worse than in pass A, and whether anything pops when you change the range or come up to the
wall. Then quit and delete `zoom_steady.txt`.

## Pass C: Sight distance in aim

Without `zoom_steady.txt`. In the menu set Sight distance in aim to 15. Repeat step 1 (all three ranges, with range
changes while aiming) and step 4. Say whether the scope comes closer, whether anything jumps when the range changes,
whether part of the sight or scope is cut away, and whether arms or body come into view. Set it back to 0 afterwards
(or keep the value you liked, and say which).

## Send

The mod log of each pass (the session folder of each start), `locked_view.json`, and what you noticed with the times.

## What the `Aim:` lines should read

Find them with `grep -n "Aim:" mod-goose.true-first-person.log`. Each aim gives an `in` line once it has settled, a
`changed` line when the range, the sight, the editor or a setting changed, a `second` line every second, and an `out`
line at the end. A quick tap gives only `Aim: out after 0.x s (a tap: not settled)`.

Before the `|`, what is on screen now; after it, the frames since the previous `Aim:` line. In pass A:

| Field | Expect | What it tells |
| --- | --- | --- |
| `fov X shown Y` | X changes with each range; Y equal to X (with Aim field of view the game's zoom) | the game's zoom per range |
| `sight S m (near clip)` | one value per range, e.g. 0.22 / 0.35 for two ranges; the Scythe about 0.173; the pistol "not known" | the game's sight distance per range |
| `wanted 0.000: pull 0.000, closer 0.000` | all 0 in passes A and B | the sight distance is off |
| `depth D m` | one value per range | the game's own gun depth |
| `editor closed` / `open on "ads"` | `open on "ads"` during step 5 | the editor works in aim |
| `gate set N of M noted, changed K times` | **the main question.** In the open: N and K 0 means the gate does not flip in full aim. At the wall: N close to M (a long set stretch, the game's own unzoom), K small | whether the shake is the gate |
| `optic target A-B deg (xP-xQ of T)` | one value per range (A = B), magnification P = Q, e.g. x3 / x6 / x12 | the optic's zoom levels |
| `game/lens xF-xG` | F = G to 3 or 4 places, even at aim-in and range changes | the value is this frame's (no lag); a spread that comes with gate changes means it lags a frame |
| `corrected C` | 0 in pass A and C; above 0 in pass B only while the gate flips | the zoom-shake hold acted |
| `step S %` | small while holding still; larger at range changes | how much the zoom jumped between two frames |
| `rig depth L-H m (n), step s m` | L = H and s near 0 while holding still | whether the eye moved along the view |

The `out` line lists each range seen: `zoom levels (game fov: sight m, depth m, optic): 30.8: sight 0.220, depth
0.270, optic 30.00 deg x3.00; ...`. That per-range sight and depth is what the Sight distance in aim needs. A wide
entry with no optic, seen after standing at the wall, is the game's own unzoom, not a range.

In pass C, per range: `wanted 0.150`, `closer` = sight - 0.15 (at most 0.25), `near clip` about 0.15 (never under
0.05). During a range change the `second` lines' `closer` should change smoothly, with `depth`, not in one jump.

In pass B: `corrected` above 0 only in seconds where `changed K times` is large; `step` lower than pass A's at the same
range if the gate flips. If the gate never flips, pass B looks like pass A and that is the answer.
