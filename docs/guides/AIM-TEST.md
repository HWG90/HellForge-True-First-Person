# Aim test (round 20c)

One game run that answers what both round 20 aiming fixes were guessing at: whether the unzoom gate flips in full
aim, what each scope zoom level does to the sight distance and the depth, and whether the field-of-view value the mod
is handed lags the lens. Everything is read from the `Aim:` lines in the mod's log. The design and what each answer
decides: `docs/design/specs/2026-11-22-locked-view-design.md` "Round 20c".

What this build changes by default: nothing you can see in full aim. Aim field of view is the game's zoom and Sight
distance in aim is 0, so full aim looks as it did on feel.51, a saved ADS placement included: the near clip follows
the gun only while Sight distance in aim is above 0 or the placement editor is open on its ADS action. The placement
editor now works while aiming.

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
entry with no optic, seen after standing at the wall, is the game's own unzoom, not a range. The `out` line files each
range under the game's field of view as sampled once a tick, so while the gate flips a range can land under the gate's
wide value (x6 filed as 24.0 instead of 15.0); its sight and depth are still that range's. Which ranges there are,
read from the `second` lines' `optic target`.

In pass C, per range: `wanted 0.150`, `closer` = sight - 0.15 (at most 0.25), `near clip` about 0.15 (never under
0.05). During a range change the `second` lines' `closer` should change smoothly, with `depth`, not in one jump.

In pass B: `corrected` above 0 only in seconds where `changed K times` is large; `step` lower than pass A's at the same
range if the gate flips. If the gate never flips, pass B looks like pass A and that is the answer.

## The harness (round 26)

The harness now runs the aim test itself, without a player. Until round 26 it never reached full aim: its right
button gave the game's shoulder aim only. In the 26 harness soak logs 8 to 34 there are 1,332 `Motion: action=aim`
lines and 538 `shoulder=1`, but 1 `State: aim in`, 1 `View:` second with `ads=1` and 0 `Aim:` lines. The user's own
log 31 has 13 `State: aim in`. Full aim (the sights on the eye) is the game's other aim mode. On PC you switch to it
with the middle mouse button while holding aim: the game's "switch aim mode" binding, which the mod's aim watch
calls the aim-mode button (`src/wire_action.h`). No harness script pressed it. The right button did reach the game;
the shoulder aim shows that. Holding it longer would not have helped either: the phases held it 0.7 to 1.5 s.

How a full aim is played now (`tools/harness/aim.py`, `full_aim()`):

1. `down right; keep right; wait 0.35`: raise the aim. `keep` leaves the button down when the input helper ends.
   Before, each helper let go of everything it held, so an aim could not last across two runs.
2. `click middle`: switch the aim mode.
3. Read the mod's log for up to 1.5 s until it says `State: aim in`. If it does not, press the middle button once
   more: if the game remembered first person for this weapon, the first press took the aim out of it. Two presses
   without it means the phase fails with "the game did not go into full aim".
4. The phase's action in full aim (a shot for aim-fire, a walk for aim-walk, the zoom for aim-scope-zoom), then the
   hold: 2 s for aim-full.
5. `up right`. The button is let go even if the action or the input fails, and `Input.close()` lets go of anything
   still kept.

Phases (`tools/harness/matrix.py OUT_DIR PHASE ...`):

| Phase | What it does | Passes when |
| --- | --- | --- |
| `aim-full` | full aim, then 2 s | the camera judge passes, `State: aim in` came, no `State: aim out` before the harness let go, an `Aim: in` line, an `Aim: out after` at least 2.0 s |
| `aim-scope-zoom` | round 30: full aim, then the scope's range the game's way on PC: hold R (the weapon functions menu), the mouse wheel up one notch, let go of R (`aim.ZOOM_STEP`); each range, the last too, held 2 s in full aim after R is let go (the menu shut), up to 4 steps or until the field of view comes round to the first. Where no step changed it, the same steps before aiming, each followed by a full aim of 2 s | as aim-full (2 s), at least two zoom levels in the `Aim:` lines, and every step where the zoom changed (in aim, or before aiming) held in full aim; a loss of full aim in the in-aim steps does not fail the fallback |
| `dive-swap` | round 30: the primary up and the game's view, then a dive forward with a swap to the secondary D s into it, D = 1.0, 0.6, 1.3, 0.3 in turn until the log says `State: left the native view` with `Motion: action=dive` the last action before it; the primary back after each try (`tools/harness/dive.py`) | the camera judge passes, a try left the game's view in the dive, and that leave's veiled `Trace:` rows carry `veil_head` |
| `aim`, `aim-fire`, `aim-walk`, `pistol-aim` | as before, but in full aim | as before, and as aim-full with their own hold |

The phase line prints `full aim after T s (N aim-mode presses)` or `NO full aim`, the `Aim:` lines counted by kind
and the zoom levels seen; aim-scope-zoom adds `range steps N: the zoom changed in aim` (or `before aim`, or `never
changed`) and the field of view after each step, `*` where it changed. `matrix.json` has all of it under `aim`, with
each step's sight and depth and its capture (`OUT/aim-scope-zoom-N.png`). The soak (`soak.py`) plays its aim actions
the same way, prints the same line for each, and its summary counts how many reached full aim. It leaves out
aim-scope-zoom. dive-swap prints `dive + swap: left the game's view with the swap D s into the dive (try N); K veiled
frames, the head lowest X m from the eye` and is under `dive` in `matrix.json`; the soak plays it too and its summary
gives the lowest head of all of them.

`python3 tools/harness/aim.py full` and `aim.py zoom` play one aim outside the matrix; `aim.py --log LOG` counts what
a log says of aiming (the numbers above). `dive.py` plays one dive + swap; `dive.py --log LOG` gives a log's veiled
rows and the lowest head (soak 36: 112 rows, lowest -0.223 m, a crouch; no dive among them).

Before aim-scope-zoom: have a scoped rifle with ranges as the primary (the R-4 Hyena, or another with 25 / 50 / 100
m) and the view on. With iron sights it fails with "one zoom level seen".

### What the next game run must show

Run `matrix.py OUT aim-full aim-scope-zoom aim aim-fire pistol-aim dive-swap` with a scoped rifle as the primary
(dive-swap ends with the primary in hand).

- aim-full: `full aim after` about 0.4 to 1.0 s with **1** aim-mode press. In the log, after `State: aim in`: View
  lines with `ads=1`, `Motion: action=ads`, one `Aim: in`, one or two `Aim: second`, `Aim: out after 2.x s`, and no
  `Aim: release not kept`. If it always takes **2** presses, the game remembers the aim mode per weapon (the profile's
  `remember_aim_mode`): say so here and start with the second press's state. **NO full aim** after both presses means
  the middle button is not the aim-mode binding in this profile, or the game does not hear it. Then check that
  `drive.py run "click middle"` pings or marks anything in the game.
- aim-scope-zoom (round 30; round 26's arrow keys never changed the zoom): `the zoom changed in aim` or `before
  aim`. Write down which here. Expect one field of view per range (two or three) and the `Aim: out` line listing each
  with its sight and depth, and look at the step captures: the reticle or the range shown should change with each
  step. Then the gate's `changed K times` at each range confirms or kills the testers' "zoom shake at 50/100 m".
  - `full aim was lost while the scope's range was stepped` means holding R ends full aim; the before-aim steps
    then show whether the range is set outside the sights. Where they change the zoom the phase passes and that
    note is under `in_aim_notes`.
  - `the zoom never changed` with `seat_moved` steps (the sight or depth moved) means the range changed and this
    weapon's field of view does not follow it: a zeroing, not a zoom. Try a scope that really magnifies.
  - `the zoom never changed` and nothing moved, with captures that look the same: the wheel did not reach the menu.
    To see the menu: `drive.py run "down r; keep r; wait 1"`, `drive.py shot menu.png`, then
    `drive.py run "wheel 1; wait 0.5"`, `drive.py shot menu2.png`, `drive.py run "up r"`. If the game's aim is on
    tap, not hold, set it to hold and run again.
- dive-swap: `left the game's view` and the head's lowest point. Round 26 estimated 0.55 m below the eye in a dive;
  the lowest so far is -0.223 m (a crouch). Write the number here. `the game's view never left` after four tries
  means the swap is held through at every delay; try it after a sprint, or swap to the support weapon (`key 3`).
  `the view was left after the dive, not in it` means the swap came once the dive had landed: try the shorter
  delays first.
- The aim phases pass with the view in first person the whole time (`+0 missed`). This is the first harness run of
  the Aim field of view and of the near clip in full aim, so judge their captures too.

Not done by the harness: the editor in aim (step 5 of pass A; it needs the placement editor's mouse drags), passes
B and C (a test file and a menu change, each needing a restart of the game).
