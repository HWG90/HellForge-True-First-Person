# Round 49g: the stance class, and the landing judged raw

Branch `round49g-stance-class`, from main c6beee8. Judge only: `tools/harness/matrix.py`, `soak.py`, `stance.py` and
their tests. Nothing in `src/`. round49d's vault rule (`tfp_traversal_looks`) is not here: the owner dropped it.

## Why the vault rule was dropped

The independent check of round49d (2026-11-27) found that the rule, by not looking for jumps for 600 ms after a
traversal, showed the game's own landing dip raw: the eye 0.24 to 0.42 m off its step in one frame, where main takes
the dip up and eases it. The step judge did not see it: a change of the eye that fits the root's change is named "the
body's own" and not counted.

## The landing measure (`matrix.landings`)

A landing: a native frame in which the body root goes 0.15 m or more down in one frame, the control mask no longer a
traversal's, a traversal mask no more than 250 ms before. Its frames: the two after it (the rig eye is the root a
frame late, so the drop reaches the eye in the first). In each, the raw vertical step of the eye shown is set against
the root's own step of the same frame; "beyond" is what the eye goes further than the root that frame, or against it.
jump-vault, clamber-low and clamber-high fail over 0.05 m, and so do the soak's actions of those names. The steps
are printed whether it fails or not.

Every landing in the 21 soak logs of feel.50 to feel.59 (soaks 27 to 38, 43-user, 50-A to 52-C, 60-A, 62-C to 65-A) (shown / the body's own, metres, the two frames):

| Log, the drop | Root | As logged | Beyond | round49d's rule (replay) | Beyond |
|---|---|---|---|---|---|
| 27 12:56:57.287 | -0.272 | -0.009/+0.087, +0.217/+0.244 | 0.009 | -0.253/+0.087, +0.146/+0.244 | 0.253 |
| 29 16:16:04.998 | -0.178 | -0.008/+0.046, +0.022/+0.045 | 0.008 | -0.158/+0.046, +0.099/+0.045 | 0.158 |
| 34 02:10:31.610 | -0.244 | -0.087/-0.140, -0.096/-0.087 | 0.009 | -0.190/-0.140, -0.087/-0.087 | 0.050 |
| 36 07:13:57.467 | -0.290 | +0.025/+0.041, +0.042/+0.030 | 0.013 | -0.237/+0.041, +0.094/+0.030 | 0.237 |
| 51-B 23:23:20.749 | -0.272 | -0.007/+0.108, +0.088/+0.236 | 0.007 | -0.259/+0.108, +0.162/+0.236 | 0.259 |
| 51-B 23:23:43.183 | -0.188 | +0.024/+0.056, +0.065/+0.053 | 0.012 | -0.128/+0.056, +0.132/+0.053 | 0.128 |

Main's present rules replayed give the logged column again (to 0.001 m). Five of the six fail under round49d's rule;
soak 34 is a step off a ledge, not a dip (the root goes on down), and reads 0.050.

What the measure is not:
- It is not the change of step, and takes nothing off for the body. The game's own rig eye, shown as the game gives
  it, fails it: that is the owner's decision (the dip is to be eased), not a finding that the mod made the motion.
- It sees a landing only where the root drops 0.15 m in a frame just after a traversal mask. Of the seven frames the
  check listed, two (31-user 20:28:01.933 and 37 09:55:56.747) are inside the hold itself and are not landings by
  this rule. The matrix runs' own logs (60-A, 62-C to 65-A) have no landing at all: 60-A's one vault ended without
  such a drop, and the others have no traversal mask. A phase with no landing says nothing of one.
- It does not see what main pays for the easing: the eye 0.2 to 0.5 m off the rig for 300 to 400 ms after the
  landing (the float judge has that, from 0.26 m held 300 ms).
- The whole-log pass at a soak's end is not judged for landings; each vault action is.

## The stance class

Brought over from round49d as it was (`tools/harness/stance.py`, the hook in `measure`, the reference rows of feel.59
19:34:38), with what the check found open closed:
- the root's change of the frame before is now held under POP_M (it was not; 4 of the 96 frames named on feel.50+);
- the soak judges an action that presses the reload key without the class (it judged all with it);
- the four mutants that passed (speed band, kind 6, veil, frame gap) each fail a test now.

Still open: the class names 92 of 715 "eye's own" frames on feel.50+ (soaks 27 to 62-C). The check counted 28 of
the 96 as dive launches; how many of the 92 are was not counted again. No limit is
raised: every frame it names leaves under 0.06 m and shows under 0.10 m.

## After the check (2026-11-28)

**The drop frame is judged too.** The check's major finding: the measure never looked at the drop frame, and an eye
that moved with the root in the same frame was 0 beyond the body's own in every frame it did look at (the 51-B rows
with the eye made to follow the root with no lag: -0.272 m in one frame at 23:23:20.749, and -0.188 m at
23:23:43.183, both passed; the step judge names them "body"). Now `landings` also gives the drop frame's raw vertical
step of the eye shown (`dropped`), judged alone, not against the root: over `LAND_STEP_M` (0.10 m) the phase fails
("a snap of the view in the drop frame"), and the soak flags the action. The bound is from the logged drop frames
(main's rules replayed give the same, to 0.001 m):

| Log, the drop | Root | Eye shown in the drop frame |
|---|---|---|
| 23 08:17:16.800 (feel.48) | -0.263 | +0.007 |
| 24 08:28:15.085 (feel.48) | -0.253 | -0.029 |
| 25 10:13:52.412 (feel.49) | -0.288 | -0.013 |
| 25 10:13:52.579 (feel.49) | -0.161 | -0.089 |
| 27 12:56:57.287 | -0.272 | -0.018 |
| 29 16:16:04.998 | -0.178 | -0.005 |
| 34 02:10:31.610 | -0.244 | -0.029 |
| 36 07:13:57.467 | -0.290 | +0.042 |
| 51-B 23:23:20.749 | -0.272 | -0.005 |
| 51-B 23:23:43.183 | -0.188 | +0.044 |

At most 0.044 m from feel.50 on, 0.089 m in all (soak 25's second drop, a step off a ledge with the root going on
down, which fails on the frames after it anyway: 0.071 beyond). The eye that follows the root reads 0.272 and 0.188;
half of it 0.136 at 23:23:20.749, which fails, and 0.094 at 23:23:43.183, which would not. Soak 18 (feel.44) shows
-0.183 in its drop frame and fails on the frame after it (0.168 beyond).

**The reload exclusion agrees everywhere.** `matrix.phase_judge` is what the matrix's main loop judges a phase with,
and it leaves the stance class out wherever `matrix.reloading` says (ammo-check and aim-scope-zoom too, not only
RELOADS); the slide judge stays on RELOADS. `phases_levers.judge_lever` does the same for the foregrip phases and
support-weapon.

**Merged with main's round 49a.** Every phase and soak action keeps the turn and veil judges (the mouse let off where
the script has a look), and the soak's `flagged(j, script)` flags veil faults and turns without a look beside the
landing.
