# The single jumps left in 0.6.0-feel.48

Written 2026-11-24 (branch `round10`), after one game session on feel.48: the Stable soak with seed 14, the
35-phase matrix, and the Bodycam soak with seed 15. The log is `~/.cache/tfp-soak-24.modlog` (`--logged 0319809`);
the four diagnoses are under `~/.cache/tfp-round10/` on the machine that ran it.

**This round changes nothing the player sees.** It changes how a jump is measured, and it makes the log say four
things it could not say. No rule of the filter and no number of the eye damping was touched. Each class below says
why, and what was tried.

Nothing here was run in the game.

## What was flagged

24 actions and phases: 13 of the Stable soak, 2 of the matrix, 9 of the Bodycam soak, some with two frames over the
limit. By cause, from the diagnoses (the number of actions in brackets):

| Class | What it is | Whose | Done |
|---|---|---|---|
| Jump-vault (4) | The body climbs: the root rises 1.2 m in eight frames, and the rig eye is the root a frame late | the body's own | the judge |
| W: backwards dive (1) | The root turns 156 degrees under a still view; 0.6 m carried goes round with it | the filter's | left; the log now has the view |
| L: prone out of an emote (1, two frames) | The Stable eye falls at the head's whole speed, the native view comes in, the lent speed is dropped | the damping's limit, then the filter's | left; the log now has the damping |
| Crouch out of an emote (1) | The same limit coming into force | the damping's limit | left, a mend tried on paper |
| Dive out of an emote, head camera (2) | The pose is cut from the emote to the dive in two frames; nothing is looked for | the game's, unfiltered | left; the log now says off foot |
| Stratagem menu, Bodycam (1) | The rig is thrown 170 ms before jumps are looked for | the game's, unfiltered | left |
| The entry's dead stop (3) | Every native entry throws the rig eye 0.33 m and brings it back, stopping dead | the eye's own | left; not explained |
| R2, T, C (8) | The documented cases of the filter, at new places | the filter's | left |
| A landing shown as it is (2), a rig eye that freezes (1), a root hop (in one of the R2 actions) | Nothing carried, no rule acted | the eye's own | the judge names them |

## 1. The judge (changed)

`tools/harness/matrix.py` `measure`, `judge`; `tools/harness/trace.py` `frames`; `tools/harness/soak.py`.

The judge took the difference of two steps' **lengths**, between frames of one Trace window, and printed the
largest. Three things were wrong with that, each shown by a diagnosis.

**It counted the body.** The session has four holds under mask 0x20 (a climb), and they are the four flagged
jump-vaults; feel.47's log has one, also flagged. Five of five. The root's step in one of them: 0.008, 0.031, 0.039,
0.133, 0.288, 0.376, 0.305, 0.260, 0.146, 0.022 m. The rig eye makes the same steps one frame later, to the
millimetre in x and y. With the filter not looking at all, every one of them would still be flagged (0.115 to
0.154 m). What the filter added, as a vector beyond the root's own change, is 0.006 to 0.050 m.

So: where the root's own step changes by `POP_M` (0.06 m) or more, in this frame or the one before, **and** the
wanted eye makes that change too within a frame either side (to 0.03 m, or half of it), that vector is taken off
the shown one. What is left is judged against the same 0.06 m. The root's own change is printed beside the figure
("body 0.158"), and so is the old figure ("by lengths 0.135").

Two conditions keep it narrow:

- A root's change under 0.06 m takes nothing off. Without this the rule shaved a dozen marginal steps (0.061 to
  0.090 m, among them C at 08:16:06.4 and R2 at 08:16:08.5) to under the limit with two or three centimetres of
  root. That would have been making numbers pass.
- A root's change the wanted eye does not make takes nothing off. A head-camera frame of a body checked in a dive
  (the root stops, the head goes on, the shown eye slowed with the root) stays the filter's.

**It did not see a change of direction.** 0.04 m forwards and 0.04 m back is 0.08 m of change and counted as 0.
The arc of the backwards dive (class W below) is eight frames of 0.07 to 0.10 m as vectors and read as one 0.100.
The judge now takes the vector, as `replay.py` has since 2026-11-22. This makes it stricter everywhere else.

**It did not look at all the frames.** A State line ended a window, so three frames with a State line between them
were never measured against each other (0.120 m at the landing of 08:18:55.525). A soak cut the log per action and
dropped the frames before a stretch's first State line (08:31:09.150 at 0.072 m and 08:32:05.640 at 0.061 m were
over the limit and not flagged). Now every traced frame of a stretch is judged against its neighbours in time
(`tick=`, the helper's clock), and a soak judges the whole log once more at its end for what lies across two
actions.

Every frame at or over the limit is printed with its time, kind, whether jumps were looked for, what was taken and
carried, and whose it is: `the filter's`, `the eye's own (nothing carried)` (the wanted eye makes 0.8 of the change
and under 0.03 m is carried: still a failure), or `the body's own, not counted`. An action without a traced frame
prints `no frames traced`, not `pop 0.000`.

The same log, judged both ways (frames at 0.06 m or more; the old judge would also have missed those across a State
line):

| Stretch | By lengths, every frame | The old judge as it ran | By vector: the filter's | the eye's own | the body's (not counted) | By lengths that are now the body's |
|---|---|---|---|---|---|---|
| feel.48 Stable soak and matrix (4439 frames) | 31 | 28 | 28 | 12 | 17 | 12 |
| feel.48 Bodycam soak to 08:46:24 (3284) | 21 | 19 | 20 | 12 | 12 | 6 |
| feel.47 Stable soak, `tfp-soak-22` (4755) | 24 | 20 | 24 | 19 | 9 | 5 |

"By lengths, every frame" is the old measure over every three frames next to each other in time: more than the old
judge ever saw. As it ran it flagged 28, 19 and 20 (corrected in round 11; the first writing of this table gave the
31, 21 and 24 as the old judge's): it did not measure across a State line, and in the Bodycam soak two frames lay
across an action's cut.

So a soak now fails on more frames than before (40 for 28, 32 for 19), not fewer. **Figures from this
judge are not comparable with those written down before round 10.** The "by lengths" figure is printed for that.
The three columns of the vector judge are round 10's; round 11 changed the rule for the body's own, and its
figures are in "Round 11" below.

**What can now slip through:**

- A step the filter makes that happens to equal, within 0.06 m, a lunge of 0.06 m or more that the root made in
  that frame or the one before.
- R2's second frame. The root's change is set aside in its own frame and the eye is given it whole in the next.
  That reads as the body's, a frame late. In this log: 08:34:07.181 (a landing inside a swap, 0.099 m shown, the
  root's 0.099 m of the frame before, 0.551 m carried; 0.092 m is what the old judge read by the lengths) and
  08:37:14.177. The eye does no more than the body did, but a diagnosis called the first one the filter's, and the
  judge no longer flags it. **Closed in round 11 for the first: see below.**
- The body itself. A climb's 0.15 m is felt, and is no longer a failure. It is printed.

Also listed as the body's in this log, outside the climbs: 08:10:44.575, 08:15:21.651, 08:28:15.384, 08:35:53.884
and the root's hop of 08:37:13.8 (0.142 m up in one frame at the end of a sprint, the head camera riding it).

### Round 11: the body's lunge once, in the frame it reaches the eye

Written 2026-11-24 (branch `round11`), after the review of round 10. Harness only; nothing was run in the game.

The review found four holes in the rule above, and they are closed:

1. **One lunge was excused twice.** The root's change was taken off the eye's change in its own frame and again in
   the next. A made-up scene: the body sets off at 0.15 m a frame, the eye takes that, lurches a further 0.15 m a
   frame in the next frame and eases back, ending 0.30 m ahead of the body. The lengths read 0.150 m; round 10 read
   0.050 m and passed it, at 8, 16.7, 33.3 and 50 ms a frame. Now one change of the root is taken off in one frame.
2. **An eye given the root's change late read as the body's.** 08:34:07.181: the head camera, a landing inside a
   swap. The root brakes by 0.099 m a frame at .148 and the head by 0.321 m; the shown eye stands (0.317 m taken
   up) and makes the 0.099 m in the next frame, in which the wanted eye does something else (0.180 m, 0.081 m off
   the root's change). Round 10 found a wanted eye for it in the frame after that.
3. **The wanted eye was looked for a frame either side of the frame judged**, whichever frame the root's change was
   from: up to two frames after the root.
4. **A frame was named by the stamp of its line.** The frames from before an edge are written with the edge, six or
   so under one stamp; a soak dropped a frame over the limit that lay across two actions when another frame under
   the same stamp was listed. A frame is now named by its `tick=`, and its line gives its own time, with the stamp
   beside it where that is another (`08:10:44.540 (written 08:10:44.575)`).

**The rule now.** The root's change of step (0.06 m or more) is taken off the shown eye's change in the frame in
which the body's lunge reaches the eye, which is one of two:

- the root's own frame, where the wanted eye makes that change (to 0.03 m or half of it) in that frame or the next:
  the shown eye went with the root;
- the frame after the root's, where the wanted eye makes that change in that same frame: the shown eye went with
  the rig eye.

Once: only frames over the limit ask for a change of the root, a frame whose own wanted eye makes it comes before
one whose wanted eye makes it a frame later, then the better fit, and of two alike the root's own frame.

**"The same frame as the root" cannot be the whole rule, and the notes above say why.** In a native frame the rig
eye makes the root's steps one frame later, to the millimetre: that is the game. 17 of the 21 climb frames are the
shown eye making the root's change with the rig eye, a frame after the root. A rule that fails every eye a frame
after the root fails the climbs again, with the filter not looking at all. So "late" is measured against the wanted
eye: an eye that makes the body's lunge in a frame in which the wanted eye does not make it has nothing taken off.
What this cannot tell apart from a climb, and still reads as the body's: 08:37:14.177 (a native frame, the root's
0.081 m of the frame before, the rig eye 0.074 m and the shown eye 0.063 m in the same frame, 0.224 m carried),
08:10:44.575 and 08:15:21.651 (nothing carried; the shown eye is the wanted one). The old judge did not flag any of
the three (0.002, 0.046 and 0.077 m by the lengths, the last across a State line).

The same three stretches (the Bodycam soak read to 08:46:24.999: 3284 frames):

| Stretch | The old judge as it ran | Round 10: the filter's / the eye's own / the body's | Now: the filter's / the eye's own / the body's |
|---|---|---|---|
| feel.48 Stable soak and matrix (4439 frames) | 28 | 28 / 12 / 17 | 28 / 13 / 16 |
| feel.48 Bodycam soak (3284) | 19 | 20 / 12 / 12 | 23 / 12 / 9 |
| feel.47 Stable soak (4755) | 20 | 24 / 19 / 9 | 24 / 19 / 9 |

Four frames changed hands, all towards a failure:

- 08:34:07.181, the body's to the filter's at 0.099 m (hole 2).
- 08:06:31.271 (the frame of that time itself; five more are written under its stamp), a climb, the body's to the eye's own at 0.106 m. Nothing is
  carried and the shown eye is the rig eye. The rig eye's change is 0.045 m off the root's 0.081 m of the frame
  before, over the 0.040 m that counts as making it; round 10 had it by a wanted eye of a neighbouring frame.
- 08:28:14.483, a climb, the body's to the filter's at 0.098 m. The shown eye makes the root's 0.087 m of the frame
  before to 0.013 m; the rig eye makes only its level part (0.050 m, 0.054 m off). Round 10 had it by the wanted
  eye of the frame after.
- 08:28:14.550, the same climb, the body's to the filter's at 0.068 m. The root's 0.060 m of that frame was taken
  off here and again at .584, where the rig eye makes it (hole 1, in the game).

So of the 21 climb frames 18 still read as the body's, and two of the four climbs of feel.48 are flagged again
(08:06:31 and 08:28:14); 08:07:04 and 08:17:16 pass, and feel.47's.

Every frame the old judge flagged that is the body's now (19), with what makes it so. "With the rig eye": the
root's change is of the frame before, and the wanted eye and the shown eye make it in this one.

| Frame | The lengths | The root's change | The wanted eye's | Shown | Left | Carried | How |
|---|---|---|---|---|---|---|---|
| 08:06:31.236 (written under 08:06:31.271), climb | 0.091 | 0.110 | 0.111 | 0.111 | 0.006 | 0.000 | with the rig eye; the shown eye is the rig eye |
| 08:06:31.403, climb | 0.081 | 0.112 | 0.123 | 0.100 | 0.017 | 0.251 | with the rig eye |
| 08:07:04.590, climb | 0.107 | 0.122 | 0.089 | 0.128 | 0.006 | 0.092 | with the rig eye |
| 08:07:04.622, climb | 0.135 | 0.158 | 0.158 | 0.137 | 0.021 | 0.103 | with the rig eye |
| 08:07:04.689, climb | 0.075 | 0.072 | 0.072 | 0.078 | 0.008 | 0.049 | with the rig eye |
| 08:07:04.723, climb | 0.109 | 0.113 | 0.046, and 0.113 in the next frame | 0.110 | 0.012 | 0.049 | with the root, in the root's own frame; in the next the shown eye does 0.022 m |
| 08:07:04.790, climb | 0.112 | 0.124 | 0.124 | 0.173 | 0.050 | 0.020 | with the rig eye; 0.050 m of it is not the body's, under the limit |
| 08:17:16.133, climb | 0.109 | 0.118 | 0.079 | 0.119 | 0.006 | 0.035 | with the rig eye |
| 08:17:16.166, climb | 0.140 | 0.153 | 0.150 | 0.142 | 0.017 | 0.070 | with the rig eye |
| 08:17:16.299, climb | 0.126 | 0.115 | 0.106 | 0.126 | 0.012 | 0.081 | with the rig eye |
| 08:17:16.333, climb | 0.101 | 0.125 | 0.126 | 0.158 | 0.032 | 0.097 | with the rig eye |
| 08:28:14.517, climb | 0.089 | 0.123 | 0.123 | 0.106 | 0.028 | 0.104 | with the rig eye |
| 08:28:14.651, climb (the landing) | 0.169 | 0.205 | 0.205 | 0.225 | 0.021 | 0.118 | with the rig eye |
| 08:28:15.384, after that climb | 0.114 | 0.117 | 0.115 | 0.122 | 0.006 | 0.029 | with the rig eye: the root rises 0.108 m a frame more at .352 |
| 08:37:13.810 (the frame of that time itself), head camera | 0.077 | 0.102 | 0.096 | 0.083 | 0.021 | 0.000 | root, head and shown eye in one frame: the root's hop at the end of a sprint coming down |
| 05:22:12.982, feel.47's climb | 0.148 | 0.185 | 0.176 | 0.167 | 0.020 | 0.103 | with the rig eye |
| 05:22:13.148, the same | 0.073 | 0.080 | 0.080 | 0.084 | 0.021 | 0.070 | with the rig eye |
| 05:22:20.059, feel.47, head camera | 0.098 | 0.099 | 0.118 | 0.101 | 0.004 | 0.376 | root, head and shown eye in one frame: the body is thrown up 0.099 m a frame; what is carried goes from 0.381 to 0.376 m in it |
| 05:29:01.778, feel.47 | 0.158 | 0.195 | 0.190 | 0.187 | 0.016 | 0.000 | with the rig eye; nothing carried |

The review listed 05:22:20.059 with the frames that should fail. It is not an eye given the root's change late:
the root, the head and the shown eye make it in the same frame, to 0.004 m. It stays the body's.

What can still slip through: a step the filter makes in the very frame the body's lunge reaches the eye, of that
lunge's size and way (within 0.06 m), once. And in a native frame, a lunge of the root that the filter set aside
and the rig eye then makes: the shown eye making it with the rig eye is the body's whatever was carried
(08:37:14.177).

Tests: `Measure` has seven more cases (the scene of hole 1 at four frame times, two lunges each taken off, an eye
a frame after the wanted eye, a frame before it, the wanted eye two frames after the root, an eye with the root a
frame before the rig eye, a frame's own time and tick); `SeenInTheGame` has the landing
(`tests/reference/trace-landing-late.log`); `SoakWholeLog` has two frames under one stamp. Three were written as guards and did not fail
first: the two lunges, the eye a frame before the wanted eye, and the eye with the root (the climb cut from the
log failed for that one before the rule had its first case).

Tests of round 10: `tests/test_harness_trace.py` `Measure` (twelve made-up cases) and `SeenInTheGame` (a climb and a braking cut
from the log: `tests/reference/trace-climb.log` reads 0.135 m by lengths and 0.050 m now, "body 0.158";
`trace-braking.log`, R2 at 08:13:20.3, is still the filter's at 0.119 m). `tests/test_harness_soak.py`
`SoakWholeLog`.

## 2. What the log now says (changed; nothing shown changes)

`src/camera_helper.c`, `src/camera_helper.h`, `src/mod_view.c`, `src/trace_edges.h`. Each `Trace:` line ends with
four new fields after `tick=`:

- `off_foot=1`: a head-camera frame of a body that was not on foot. Its eye is not damped and jumps in it are not
  looked for. The question is asked only in a head-camera frame; a native frame always says 0.
- `damped=1`: the eye damping ran in this frame.
- `damp=(x y z)`: what the damping added to the true eye, in the world. `wanted - damp` is the head camera's own
  eye. With `damped=0` it is a lag being let go.
- `view=(x y z)`: which way the view pointed. The head camera's forward; in a native frame the forward axis of the
  rig eye the camera hangs on.

Nothing reads these four yet: not the judge, not `trace.py`, not `replay.py`. They are in the log for a diagnosis
to use.

And a window of Trace lines is opened when the body's action changes (idle, sprint, crouch, prone, dive), not only
at a lease or a hold. An action begun and ended inside the native view left no traced frame: 23 of 32 dives in the
Stable soak and 28 of 36 in the Bodycam soak were never looked at. The continuity is **not** told of the action:
looking for jumps in a dive would change what is shown. Such a window has no State line before it and `t=` starts
again in it; the harness readers already allowed for that. The motion runtime samples the action only while there
is a lease, so the head camera gets no window from this.

The next soak will therefore judge many more frames. Expect more flags from that alone.

Tests: `tests/camera_smooth_test.cpp` scenarios 21 and 22; `tests/trace_edges_test.c` (`tests/run-trace-edges.sh`,
the 51st script); `tests/test_harness_replay.py` `NewFields` (the readers take the longer line; this one was written
as a guard and did not fail first).

## 3. The classes left alone

### W: what is carried goes round with a root that turns under a still view

Stable 211, a backwards dive, 08:12:09.5. From .543 the root's axes yaw by 0.303 rad a frame for nine frames. The
rig eye barely goes round. What is carried (0.41 to 0.64 m) is kept against the root and rides the axes: the shown
eye goes 0.259, 0.237, 0.226, 0.211, 0.195, 0.168, 0.177, 0.155, 0.139 m a frame on an arc where the wanted eye
goes 0.269, 0.244, 0.083, 0.072, 0.065, 0.060, 0.056, 0.055, 0.052. About 0.8 m of path that is not the eye's, over
300 ms. Of everything left in Stable this is what a player would see most.

It is W of the soak-19 analysis, never this large. Four variants were tried by the diagnosis on scratch copies:

| Variant | Result on this log | Tests |
|---|---|---|
| What is carried keeps its direction in the world wherever the root's turn would sweep it by half the measure | 48 to 41 of the filter's steps | 40 of 3,094 checks fail (the re-seat in a fast even turn, soak 6's turn, three pins) |
| The same, only on the filter's own evidence that the eye does not ride | no effect: the evidence is never reached in the dive | 0 fail |
| The same with weaker evidence | 48 to 43 | 2 fail, both pinned "not mended" cases |

The filter cannot tell a root that turns under a still view (this dive) from a body that turns with its view (the
mouse), and the second is where carrying against the root is right. The helper knows the view. That is why `view=`
is now in the Trace rows: with it, "the root turned and the view did not" can be tried on a log before it is tried
in the filter. Not done this round because no log has the view yet.

### L and its first frame: prone out of an emote (Stable)

Stable 308, 08:15:46.9 to 47.1. Two frames over the limit; the soak printed only the larger.

- **F1**, head camera, not looked at, nothing carried: the eye sets off downwards at 0.021, 0.123, 0.248 m a frame
  (0.133 m of change).
- **F2**, the third native frame: the eye has gone on at the lent speed for two frames (0.222, 0.174 m) and the rest
  of it, 5.2 m/s, drops in one frame: 0.179 m. The view dips to 0.335 m above the ground, 0.26 m under where it
  rests, and rises again over half a second. This is L as documented; feel.47 has the same (0.144).

New this round, by arithmetic: **F1 is the damping's limit coming into force**, and the lent speed of F2 is the
head's whole speed for the same reason. Stable low-passes the head at 350 ms and never lets the eye be more than
0.25 m behind. A head that falls 0.13 m and then 0.26 m a frame gives, through `tfp_eye_damp_follow` at 33.3 ms:
0.012, 0.128, 0.260 m. Seen: 0.021, 0.123, 0.248. The second frame is the limit reached part of the way through.
The diagnosis could not say whether the damping ran in those frames; the shape says it did, and `damped=` and
`damp=` will say so outright.

Nothing within a 0.25 m limit mends it. A head at 7.7 m/s uses the whole 0.25 m in one frame at 30 frames a second;
after that the eye has the head's speed whatever the curve. To keep the eye's step from changing by more than
0.06 m a frame it needs 4.3 frames to reach that speed and falls about 0.5 m behind meanwhile. That is a different
camera: the eye half a metre above a falling head for 150 ms, looking down on the body's own back. It may well be
the better one (a base-game camera lowers over 0.3 to 0.5 s and does not ride a bone), but whether the picture is
right is not a thing a number says. It needs the game and an eye.

For F2 alone, earlier rounds rejected eight variants; the diagnosis adds one sign the filter has in the edge's
own frame (the rig eye is already behind along the lent way: -0.111 m here, -0.144 m in feel.47, -0.059, -0.167 and
-0.100 m in soak 19's three) and shows it is not enough alone: other edges have it while the body travels.

### A crouch out of an emote (Stable): the same limit

Stable 166, 08:10:15.7, 0.067 m (feel.47: 0.065). Head camera, steps 0.015, 0.020, 0.087, 0.109, 0.113. A 350 ms
low-pass alone cannot change its step by 0.067 m in a frame. A head going 0.12 m a frame gives 0.011, 0.021, 0.078,
0.120 through the damping: the limit reached in the third frame.

A mend was tried on paper and **not made**: above a knee the lag is let go on a shorter time, solved exactly per
frame so it does not depend on the frame time; below the knee nothing differs by a bit, and the 0.25 m stays the
most. Largest change of the eye's step, old to new, for made-up heads (knee at 0.6 of the limit, a sixth of the
time above it):

| Head | 8 ms | 16.7 ms | 33.3 ms | 50 ms |
|---|---|---|---|---|
| Crouch, 0.5 m at 3.3 m/s, 100 ms to speed | 0.021 to 0.010 | 0.043 to 0.023 | 0.087 to 0.040 | 0.131 to 0.080 |
| Crouch, 0.5 m at 3.3 m/s at once | 0.019 to 0.009 | 0.043 to 0.023 | 0.059 to 0.033 | 0.131 to 0.080 |
| Prone, 0.9 m at 7.7 m/s | 0.040 to 0.036 | 0.070 to 0.077 | 0.194 to 0.173 | 0.289 to 0.251 |
| A landing, 0.3 m at 4 m/s | 0.008 to 0.004 | 0.009 to 0.013 | 0.011 to 0.044 | 0.010 to 0.044 |
| A step down, 0.15 m at 2 m/s | the same | the same | the same | the same |
| Head bob, 0.04 m at 2 Hz | the same | the same | the same | the same |

It halves the crouch and does nothing for prone. It also makes the eye's step change four times as much in a move
of 0.15 to 0.25 m (the landing row), where today's low-pass is at its smoothest. That is trading one frame of
0.067 m, once in a soak, for a little more of the head in every landing: Stable's whole point is that the head's
jerks do not reach the view. Not worth it unseen. The script is `~/.cache/tfp-round10/work/knee.py`. With `damp=`
in the log the head's true path can be had from the next soak, and this or a longer leash tried on it.

### A dive out of an emote, in the head camera (Bodycam 279 and 289)

08:37:59.950: 0.489 m as a vector, the largest single figure of the session. The view had been in the head camera
for an emote; the frames are the six before the native view came back. The head goes 0.538, 0.245, 0.033 m a
frame, its height above the root 1.15, 0.66, 0.37 m, and the root's up axis is tilted in the first frame and
upright after. Inferred: the game cuts the pose from the emote to the dive in two frames, and the head camera sits
on the head. The filter did nothing, by design: nothing is looked for in a head-camera frame off foot (it left the
eye behind the head of a thrown body for half a second, once). 08:38:26.692 is the same before another entry, after
frames the lock missed (`helper error 15`, ragdoll flag 1, in an emote), which is a fault of its own.

Left. To take up a jump of the head off foot is the thing that was taken out. The rows now say `off_foot=`.

### The stratagem menu (Bodycam 226): thrown before looking

08:36:05.4. For five frames before `State: holding` the rig eye goes 0.170, 0.023, 0.088, 0.033, 0.039 m and is
shown whole (0.149 m). Looking begins when the control mask changes, 170 ms after the rig first moved. Nothing in
the log leads the rig's move: no Motion line, no State line. The Gate line of that second counts the avatar bit
changing five times, but per second, not per frame. Left; a signal that leads has to be found first.

### The entry's dead stop (matrix quick-swaps and stratagem-menu, Bodycam 162)

Every native entry throws the rig eye about 0.33 m in its second frame (taken up) and brings it back over 300 ms:
slowly at first, then 0.06 to 0.077 m a frame for three frames, then a dead stop about 358 ms after `Pose: begin`.
31 such stops in this log with the body standing. In 23 some of the fast frames were taken up and the stop shows
0.000 to 0.037 m. In 8 all three were followed, the first being a hair under the measure, and the stop shows 0.035
to 0.072 m. A let-down at a followed stop would hide it, and is the coasting past a stop that rounds 1 and 7 decided against.

What throws the eye is not known. Looked at this round:

- The mod's own draw animation (`src/pose_helper.c`, `entry_animation`: the rig starts 0.35 m down and 0.06 m back,
  0.355 m together, and rises over 220 ms on an ease-out) is the right size. The eye's excursion from where it
  comes to rest is 0.31 to 0.33 m in the three flagged entries.
- It is the wrong shape (an ease-out is fast first and ends softly; this is slow first and ends dead) and the wrong
  length (220 ms; this takes 300).
- By the source it cannot reach the eye: the offset is on node 5 and "the camera is a sibling of node 5".

So the size may be a coincidence, or the game may blend the rig from the pose written at arming in its own time.
One run settles it and nothing offline does: the draw animation off (`tfp_pose_set_entry_animation(0, ...)`; no
option reaches it today), and see whether the 0.33 m goes. If it does, the mend is in the pose helper and every
entry gets quieter. Left.

### R2, T and C at new places

All documented and not mended; rounds 1 to 7 tried each. What this session adds is frames:

- R2 at a braking: 08:13:20.3, a sprint ending 330 ms after the entry, 0.119 m for a wanted 0.053 m (feel.47, the
  same action: 0.158). Now `tests/reference/trace-braking.log`.
- R2 at a dive's launch (08:16:08.5) and a landing (08:34:07.1), and with a root turning 3.2 degrees a frame
  (08:37:14.2).
- T at a turn's end: 08:37:21.6, 0.068 m sideways two frames after the turn stopped. That it is the turn's kept
  speed is inferred.
- C as a brake: 08:18:55.9 and 08:34:07.4. The native view comes in while a catch-up runs at 3 m/s; the new curve
  keeps that speed and gets its length from the distance alone (105 and 137 ms), and one frame later it is at
  0.6 m/s. Larger than the 0.028 m written down for C. A curve that allows for its start speed was tried: 6 checks
  fail, 8 probes are worse, and carries cut off go from 14 to 18.
- C on retail's glide: 08:16:06.4, the fourth frame at the curve's steepest.

### The eye's own, with nothing carried

A landing in the native view shown as the game's own first-person eye has it (08:13:13.044, 0.078 m), and a rig eye
that slides and freezes when a hold begins (08:14:41.8, 0.061 m). No rule acted. They stay failures and are now
printed as `the eye's own`.

## The earlier logs through the filter

The filter was not changed, so every log replays as it did. Run before the first commit and after the last, with
`replay.py LOG --logged REV`, "the present filter, replayed, there":

| Soak | Build | The filter's steps (beyond the root's own) | Metre-frames |
|---|---|---|---|
| 8 | 9a3470b | 9 (8) | 179.5 |
| 9 | 9a3470b | 16 (15) | 312.9 |
| 10 | 66ee111 | 4 (4) | 117.8 |
| 11 | 66ee111 | 7 (6) | 209.5 |
| 13 | f557194 | 7 (3) | 115.6 |
| 14 | f557194 | 7 (3) | 95.6 |
| 15 | b486193 | 10 (3) | 110.1 |
| 17 | ebe3c1f | 10 (4) | 126.6 |
| 18 | ebe3c1f | 26 (12) | 268.4 |
| 19 | ebe3c1f | 17 (15) | 256.8 |
| 20 | b8a591b | 17 (15) | 334.0 |
| 21 | e1b57fd | 14 (7) | 222.9 |
| 22 | 06f72dd | 21 (12) | 354.9 |
| 23 | 0319809 | 25 (14) | 264.4 |
| 24 | 0319809 | 48 (26) | 510.8 |

The same before and after, to the digit.

What a replay cannot show of this round: nothing was changed that a replay runs. The new Trace fields and the new
windows only appear in a log a new build writes.

## What only the game can show

1. That the build still runs and the Trace lines come out with the four new fields, and how many more frames a
   soak judges with a window at every change of action.
2. Whether `damped=1` and a `damp` of 0.25 m long stand in the frames of a prone or a crouch out of an emote, as the
   arithmetic says.
3. Whether a player reads a climb as a pop. The numbers say the eye moves with the body and no more.
4. Whether the 0.33 m at every entry goes with the mod's draw animation off.
5. Whether, in a backwards dive, `view=` stays while the root's axes go round. That is the evidence W needs.
6. Whether a longer leash for the Stable eye in a fall looks right. The picture, not the figure.

## Round 13: the game always finds its own weapon socket

Written 2026-11-24 (branch `round13-arms`), after round 12's read-only plan and its challenge
(`~/.cache/tfp-round12/PLAN.md`, `CHALLENGE.md`) and one game session on feel.49 with a test switch. **This round
changes what the player sees**: where the camera sits against the drawn arms and gun whenever the body's hands are
not where the rig's own are. The filter is not touched.

Nothing of this branch was run in the game. The proof run below was feel.49 with `arms_live_restore.txt`, which does
what this branch now does by default.

### The finding

The game's kind-6 evaluator seats the first-person eye by the rig's weapon socket: node `0x4fdd3337`, rig node 8,
joint 41 of the 42 the arm drive writes (`~/.cache/tfp-round9/eval.txt`, game+0x1287423 to 0x1287785). It puts the rig
so that the socket is at depth D before the eye. The arm drive wrote that socket and left it in the rig until the
next frame, so the evaluator read the mod's socket: every move of the drawn arms along the view moved the camera the
other way. A melee 0.7 m in a frame, a backward dive, the aim fade, the alignment's 150 ms ease, the first frame of
every hold (where the drive did put the game's values back, and the eye went the socket's whole offset at once).

### The proof run

Stable, soak seed 15, twice: the build as it is, and the same build with `arms_live_restore.txt` (the game's joint
values put back after every primary draw). The first is the second half of `~/.cache/tfp-soak-25.modlog`, from the
`Camera style: Stable` line at 10:19:44.878; its output is `~/.cache/tfp-feel49/soak-bodycam-15.out`, **named the
wrong way round** (the panel was switched the other way than the file names say). The second is
`~/.cache/tfp-soak-26.modlog`, output `~/.cache/tfp-feel49r/soak-seed15.out`. No crash in either.

| | As it is (soak 25, Stable) | Put-back (soak 26) |
|---|---|---|
| Actions, flagged | 434, 74 | 337, 30 |
| Frames over the limit | 270: 260 the eye's own (251 along the view), 10 the filter's | 42: 32 the eye's own (21 along the view), 10 the filter's |
| Largest | 0.753 m (a melee) | 0.164 m (the filter's, a sprint into a native entry) |
| Holds whose first 150 ms had a jump taken up | 39 of 60, up to 0.54 m | 0 of 56 |
| Entries with a jump taken up 0.1 to 0.7 s after | 43 of 60 | 11 of 57 |
| Kind-6 seconds with `eye_jump` of 0.15 / 0.30 / 0.60 m or more (every second, traced or not) | 49 / 14 / 11 of 887 | 2 / 0 / 0 of 664, after the second the view was switched on |
| Frames not held in first person | 0 | 2 (quick-swaps, head camera, helper error 15) |

The same seed gives the same action at the same place in the list, so the first 337 actions can be set side by side:

- flagged 57 against 30; 16 in both, 41 only as it is, 14 only with put-back;
- these left the list: pistol-aim (7 of 10 flagged before, none now; largest 0.144 against 0.009 m), aim+swap (5),
  aim-fire (3), prone+stim (3), aim-walk (2), crouch (2), and ammo-check, stim, pistol-reload and melee (1 each).
  Plain aim was not flagged in these 337 in either run (largest 0.059 against 0.026 m);
- the melee: 12 of the 16 have no traced frame in either run. The four that have: largest 0.753 m against 0.025 m.
  The untraced ones are covered by the `View:` line of each second: none over 0.30 m with put-back.

The challenger's pass marks, all met: no jump taken up at a hold's edge; counted frames from 270 to 42 (it expected
about 60 for both styles together); no melee second over 0.3 m.

### What changed

1. **Put-back is the rule** (`src/arm_drive.c`, `src/arm_drive.h`, `src/mod_view.c`). While the arm drive is on, the
   game's joint values are put back after every primary draw. The rig holds the mod's values from the draw's write
   to the end of that draw and no longer. No logic of the drive changed: the path was there for holds
   (`restore_after`), and the binder now asks for it always (`tfp_arm_drive_put_back`).
   - `arms_live_keep.txt` beside the DLL: the written values stay until the next frame, as before. For the
     comparison in the game only. It is not in `docs/guides/TEST-BUILD.md` yet (that file was not to be changed in
     this round).
   - `arms_live_restore.txt` is still read and changes nothing; with both files, keep wins and the line says so.
   - The start line says which runs: `Arms: the rig's own joint values are put back after every primary draw ...`
     or `Test: arms_live_keep.txt: ...`.
2. **The Trace row** ends with three more fields:
   - `sock=%.5f`: the weapon socket as the arm drive drew it in this frame less the game's own, along the view, in
     metres (positive: drawn further out). Under put-back it is the gun's depth error on screen. With `arms=2` it is
     how far the eye was thrown, and `eye + sock * view` is the eye the game would have had: put-back can be tried on
     any such log without the game.
   - `arms=%u`: 0 not driven (off, another camera, refused) or not known for the frame, 1 driven and put back, 2
     driven and left written, 3 driven in a hold or with the gun away.
   - `eye_edge=%d`: 1 where the presented eye's step changed by 0.15 m or more in this frame. The camera helper
     then opens a Trace window itself, with the six frames before, or keeps the open one open (`src/trace_edges.h`).
     A melee has no State line and no change of action; this is what gets it traced.
   - How `sock` gets there: the drive works it out at the draw from the two socket records it has (`base` and what
     it writes), through the rig root's axes onto the rig eye's forward axis. The draw comes after the frame's
     camera call, so the binder hands the figure to the helper at the next tick for the newest frame presented
     (`tfp_camera_trace_draw`), before it reads the rows. Where the game sets the camera twice in a frame, the
     earlier row says `arms=0`; the readers keep the later one.
   - 0.15 m is at 30 frames a second. A faster game spreads a move over more frames and each changes the step by
     less.
3. **The harness** (`tools/harness/matrix.py`, `soak.py`, `trace.py`).
   - The camera style is read from the log's `Camera style:` lines: printed at the top, where it changes, and in
     the summary (`camera style: Stable throughout`, or `... NOT ONE STYLE, judge the stretches apart`). Each matrix
     phase's result carries it. No argument and no file name says it.
   - `soak.py --log LOG` judges a log that is there, one line for each style it ran in. That is where the figures
     of the table above come from.
   - Each frame at or over the limit is printed with what is judged split along `view=` and across it, and "the
     eye's own" reads "the eye's own, along the view" where 0.06 m or more lies along it and less across. The
     summary counts them. **Nothing is excused**: `POP_M`, the counts and whose of every frame in soaks 22 to 26
     are what they were (checked frame for frame against `main`).
   - `trace.py` reads `view=`, `sock=`, `arms=`, `eye_edge=`; `replay.py` takes the longer line as it is.

### Who read the written joints, and what changes for each

| Reader | What it read | Now |
|---|---|---|
| The game's evaluator (the eye) | the written socket | the game's own socket. This is the change |
| The drive's own next frame (`tfp_graft_underlying`) | the written record, and recovered the game's value from what it had saved | the game's own record itself. The same values; one comparison instead of a recovery |
| The rest alignment and the `Rest:` line | the game's own socket (`base`), recovered | the same value, read directly. What is measured and kept does not change. What the line calls "socket X m off the rig's own" is now seen on screen as the gun's depth, where before the camera hid it by moving |
| The alignment's 150 ms ease, the aim fade (120 ms), a provisional alignment taken again | moved the written socket, and so the eye | move the drawn arms and gun only |
| The hold's put-back | the game's values went back in a hold and with the gun away; the hold's first frame re-seated the eye by the whole offset | unchanged path, and no edge: the frame before a hold was put back too. `sock=` says 3 in a hold |
| The gun's units (the weapon tree) | refreshed against the written socket and left there for the game's update | refreshed for the draw, and again against the game's socket after it. The game's update (flash, tracers, casings) has the gun where the rig's own arms hold it, `sock` away from where it is drawn |
| Sleeve and glove units on the rig | refreshed after the write | refreshed after the write and after the put-back |
| The head camera's held eye (`src/camera_helper.c`, the native eye remembered for a weapon when the view leaves) | the rig eye as the written socket seated it | the rig eye as the game's socket seats it: steadier, and elsewhere by `sock` where the view left mid-action. The watch that sets a moved eye aside for 200 ms (5 cm in a frame) fires less |
| The `Hold:` line's rig eye, the entry trace | the same rig eye | likewise |
| Hand grips and sleeves (`src/grip_hand.c`, `src/arm_pieces.c`) | the rig's world poses in the tick | **refused on this build** (held back since feel.18). Forced on with `arms_on.txt` they would now read the rig's own arms in the tick, not the drawn ones. Not tested |
| The pose helper (node 5, the depth pair) | does not read the socket | unchanged, bit for bit (`tests/run-diff-pose.sh`) |

One more write of up to 42 joints and one more refresh a frame. What that costs was not measured: the `Cost:` line
times the write before the draw only (56 us mean in both runs), not the put-back after it (`src/render_slot.c`).

### Tests

- `tests/arm_drive_test.c`: `test_game_sees_its_own_socket` (a melee 0.8 m along the view in 100 ms, at 8, 16.7, 33.3
  and 50 ms a frame: after every frame the rig's 63 records are the game's own bits and a model of the evaluator,
  D less the socket the rig holds, does not move; with the values left the same scene throws it by `sock` and the
  drawn joints are the same bits), `test_fade_and_ease_under_put_back`, and the challenger's
  `test_hold_begins_under_put_back` (the socket 0.2 m off along the view, a hold begins: no change across the edge;
  with the values left, the whole 0.2 m in the hold's first frame). 75 031 checks.
- `tests/camera_smooth_test.cpp` scenarios 23 (the eye opens a window) and 24 (`sock`, `arms` in the rows);
  `tests/trace_edges_test.c`.
- `tests/test_harness_trace.py` `AlongTheView`, `CameraStyle`; `tests/test_harness_replay.py` (the longer row).
  69 harness tests, 58 before.
- The tests of the leave-written path stay and ask for it by name.
- Watched failing first: all of the above. One caveat: the drive's put-back path existed, so the three drive tests
  failed for want of the new figures (`stats.draw`), and were then checked by taking the put-back out of the test's
  frame (they fail at the first frame).

### What is left under put-back, and whose it is

42 frames over the limit in 30 actions of soak 26; and 10 the body's own, not counted.

| Class | Actions | Frames, size | Whose | In soak 25 at the same place |
|---|---|---|---|---|
| A. A dive's launch (and one sprint's stop): the root's step grows over two frames by under 0.06 m each, the rig eye makes it in one, a frame late. 65 to 68 ms after the action became dive | dive 4, 8, 64, 148, 265, 331; dive-back 38, 142, 286; w+dive+swap 179, 199, 293; sprint+swap 202 | 13; 0.069 to 0.107, twelve of them 0.069 to 0.085 | the body's (the game's own lag; the judge takes nothing off a root change under 0.06 m) | flagged there too where traced, at 0.12 to 0.51 m: the written socket on top of it |
| B. Inside a dive, across the view: 300 ms in, and at a landing | dive 4, 148 | 2; 0.074, 0.088 | the eye's own | hidden under the larger frames |
| C. A reload's step: the eye goes 0.06 to 0.15 m back along the view when the action becomes reload (two frames, the first 0.084 to 0.096), stands there for the whole reload, and returns when it ends (in the one traced to its end, to within a millimetre of where it stood). Body still, nothing looked for, nothing carried | reload 11, 12, 36; dive-back 38 (landing in a reload) | 8; 0.072 to 0.116, all along the view | the eye's own; most likely the game's | not traced there (all 12 reloads). The `View:` lines of the same two seconds read 0.080 and 0.079 m, with no change of the mod's placement: the step is not put-back's |
| D. Back to idle: standing up from prone, a vault's end. The eye starts a glide of 0.04 m a frame along the view, 66 ms after the action became idle. Body root still | jump-vault 68, 149 (two), 240, 316; prone 229, 297 | 7; 0.061 to 0.070 | the eye's own | vault 68 was 0.105 m; three vaults and one prone were not traced; prone 229 was 0.052 m |
| E. A native entry with 0.06 to 0.66 m carried, 67 to 270 ms in | sprint 178 (three frames, 0.164 the largest); back 141 (two); grenade+swap 248; w+dive+swap 179 | 7; 0.064 to 0.164 | the filter's | back 141: 0.149 m. The others had no flagged frame there or were not traced |
| F. The head camera within 270 ms of a swap, something carried | w+dive+swap 128; back 141; sprint+swap 221 | 3; 0.061 to 0.072 | the filter's | 128 and 221 had the filter's frames too (0.079, 0.076) |
| G. The head camera's own in the frame of a swap's State line, inside a dive | w+dive+swap 147, 293 | 2; 0.067, 0.075, mostly across the view | the eye's own | 147: 0.065 m, the filter's |
| H. One frame not held in first person (head camera, helper error 15: the case of `tests/camera_smooth_test.cpp` scenario 19); the jump is under the limit | quick-swaps 99, 125 | none | the lock's | none missed in soak 25; soak 22 had two of the same |

By whose: the body's 13 frames, the eye's own 19 (B, C, D, G), the filter's 10 (E, F). The judge prints class A as
"the eye's own" like the other 19 (32 in its count): that it is the body's is this reading of the rows, not a rule. By action: dive 6, w+dive+swap
5, jump-vault 4, reload 3, dive-back 3, prone 2, quick-swaps 2, sprint+swap 2, sprint 1, back 1, grenade+swap 1.

**Did put-back make anything worse?** Not that the two logs show.

- Where both runs have a figure for the same action (147 of the 337): 51 were over the limit and shrank by more than
  0.01 m; 35 of them came under 0.06 m and 16 are still over it. 3 went over the limit from under it: w+dive+swap 179 (0.038 to 0.074: classes A and E), prone 229 (0.052 to
  0.070: D), grenade+swap 248 (0.051 to 0.069: E). All three are classes both runs have, at 0.06 to 0.07 m.
- Of the 14 actions flagged only with put-back, 9 had no traced frame in soak 25 (reload 3, jump-vault 3, sprint,
  dive, prone), so nothing was judged there; for the reloads the `View:` lines show the same step. 2 are the missed
  frame (H). 3 are the ones above.
- The two missed frames are the one thing new in kind. They are head-camera frames, where the drive does nothing,
  and soak 22 had the same; that put-back has no part in them is likely and not shown.

What the classes say for the next round:

- A is honest motion at 0.07 m, flagged because the root's own change is spread over two frames at under 0.06 each.
  It was hidden under M1's larger frames. No rule is added for it here.
- C is the largest thing left that a player would see standing still. Not settled whether it is the game's own
  (its rig's reload pose) or the mod's per-action placement on rig node 5, which sits above the socket and which the
  evaluator therefore sees like any move of the socket. For the game's own: soak 25 has the step with no `Motion:`
  line in those seconds, and the same placement change gives 0.060 m prone and 0.149 m standing. Against: one frame
  of 0.09 m is not how an animation blends. `sock=` will not tell the two apart (both move the drawn and the game's
  socket alike). A reload with the reload placement set equal to the idle one would.
- E and F are the filter's old cases at entries and swaps. Its input is another eye now; they are to be judged again
  on a soak of this build before anything is mended.

### What the picture shows

Twelve poses each, one frame a pose, taken by hand: `~/.cache/tfp-feel49r/sheet.jpg` (put-back) against
`~/.cache/tfp-feel49b/sheet.jpg` (the build as it is; another mission and light, and two more frames).

- With put-back the gun and hands are in view in 10 of the 12: hip, looking down, pistol, prone, prone looking down,
  melee, reload, aim, sprint, crouch. The other two have none in either build: the frame in the middle of a swap
  (the head camera, with the back of the helmet filling the middle of the picture) and the stim (no gun in the
  hands).
- The build as it is shows no gun in the prone frame and none in the melee frame; put-back shows the pistol in both
  prone frames and the rifle in the melee frame.
- No frame shows the gun behind the lens or cut off by the near plane, as far as a thumbnail 600 pixels wide shows.
- Resting poses look the same in both, as they should: at rest the alignment makes `sock` zero.

The challenge expected worse from the hold edges of soak 25: the eye going towards the drawn arms by 0.11 m or more
in 16 of 149 holds, by 0.20 m or more in 8. Twelve stills do not rule that out; they are single frames.

### What only the game can still show

1. That this branch runs: the start line, `sock=`, `arms=1` and `eye_edge=` in the Trace rows, a window at every
   melee, the style at the top of a soak's output.
2. **The gun just after a swap, before its first rest.** A provisional alignment was hidden by the camera until now
   (four holds of soak 25 at 0.47 to 0.50 m, a weapon with "no rest alignment kept for it yet"). It would show as a
   gun out of place, or out of view, until the first rest. `sock=` in those rows says how far.
3. **Prone with the view pitched**, and **standing, looking 25 degrees down**: whether the gun stays in view and
   clear of the near plane (prone with one weapon read -0.16 to -0.20 m in nine holds: a steady offset).
4. Hip fire: muzzle flash, tracers and casings come from where the game has the gun, `sock` away from where it is
   drawn. Zero at rest; in a moving burst it is whatever the row says.
5. A pass drawn after the primary one (the arms' shadow, a scope picture) with the rig's own arms.
6. Whether the reload's step (C) is the game's: a reload with the reload placement equal to the idle one.
7. Whether the two missed frames come again, and without the arm drive.
8. A soak in each style on this build, the styles read from the log, and the same with `arms_live_keep.txt` for the
   comparison.

### Not proven

- That every one of soak 25's 251 along-the-view frames was the socket. 21 of soak 26's 32 own frames still lie
  along the view (classes A, C, D): dives go along the view, and so does whatever C is.
- The cause of C and of D.
- Anything about Bodycam under put-back: the proof run was Stable only.
- `sock=`, the eye's own windows and the style reading have run in tests and on old logs, not in the game. The
  binder's hand-over of `sock` to the right frame rests on the order tick, camera call, draw in every frame; where
  that does not hold, a row carries the neighbouring frame's figure.
- The filter's replays are the same as on `main` for soaks 20 to 26 (run and compared); soaks 8 to 19 were not run
  again. None of the filter's files changed.

## Round 19: a move straight out of an emote (THE CUT)

Written 2026-11-24 (branch `round19-emote-move`), from the logs of feel.50 and feel.51 (`~/.cache/tfp-soak-27` to
`-30.modlog`). Nothing here was run in the game.

### What the rows say

Sprint, dive or dive-back straight out of an emote, the head camera (kind 4) holding the view and the gun out of
the hands (`Hold: ... why=the gun has left the hands`, phase=waiting). Soak 30 at 16:33:39.703 (emote 174, then
sprint 178), as the Trace rows have it, with the head = wanted - damp taken against the root:

| | before (5 frames) | the frame | after |
|---|---|---|---|
| root's step | 0.003 to 0.011 m, then 0.050 | **0.163 m**, yaw +3.6 degrees | 0.054, 0.024 |
| head against the root | 0.002 to 0.010 m | **0.340 m** | 0.043, 0.036 |
| damp (Stable, 0.25 m most) | 0.18 to 0.25 m, falling | 0.250 (the limit) | 0.19, 0.14 |
| eye shown | 0.020 to 0.052 m | **0.355 m** | 0.064 |
| looked, taken, correction | 0, -, 0 | 0, -, 0 | 0, -, 0 |

The other cases are the same frame: the head 0.34 to 0.61 m against the root in one frame, from under 0.4 m/s, the
root lunging 0.08 m or snapping 0.16 m, a dive-back's yaw flipping 157 to 180 degrees. Every one in soaks 27 to 30:
27 at 12:52:57.770 (the cut takes two frames there, 0.39 and 0.32 m: 0.401 then 0.619 m of step change shown),
13:04:44.367, 13:05:17.921; 28 at 16:03:34.909, 16:04:02.153; 29 at 16:16:30.771, 16:17:04.358; 30 at 16:33:39.703,
16:37:34.709, 16:38:01.952. Soak 25 (feel.49) has none that nothing looked at: its head jumps of 0.27 to 0.36 m all
fell in a looked-at window and were taken up.

### Why the continuity took none of it

Not looked for. `smooth_detect` looks for jumps for `SMOOTH_EVENT_MS` (600 ms) after a change of the control mask,
the request's kind or weapon, the binder's weapon key, or a word from the binder. The gun left the hands 0.9 to 1 s
before (the `Hold:` line), and moving out of an emote changes none of those: every row says looked=0. Within the
head camera only a change of camera is taken up otherwise.

Not the damping, and not the head camera's own path: the damping did what it is for. Stable took 0.25 m of the
head's 0.34 to 0.54 m (its limit, reached in that frame), Bodycam 0.10 m; the rest went through. `off_foot` is 0 in
all these rows (a dive is "on foot" to the layer it reads, round 14), so the head-camera exclusion for a thrown body
did not apply either. `orphan_head` is not involved: these are kind 4 rows with source=head.

### What changed

The smallest place is the continuity: a frame that is no event and is not looked at, where the game re-seats the
head as it re-seats a rig.

- `src/camera_continuity.h`, THE CUT: `tfp_continuity_step_cut` takes the head the eye is built on, before the eye
  damping, in the root's frame (NULL where it is not given; `tfp_continuity_step_in` is that call with NULL). A frame
  in which nothing is looked for and the head moves `TFP_CONTINUITY_CUT_M` (0.25 m) or more against the root, or
  `TFP_CONTINUITY_CUT_ACCEL` (150 m/s^2) times the frame's time squared where that is more, from under
  `TFP_CONTINUITY_CUT_REST` (1 m/s) the frame before, is taken up as a change of camera: whole, on the curve, the
  speed lent from before (none). The frame after one cut can be a cut too (soak 27), a third cannot. The root's
  course is kept through a cut frame and nothing is tried from it: believed, a dive-back's yaw flip was the course of
  the frame after, the root read as re-seated off it, and what was carried swung round by as much again (0.376 m of
  step change at 16:16:30.838 of soak 29, found by the replay).
- `src/camera_helper.c` `smooth_present` gives the head in a head-camera frame (kind 4, source head) of a body that
  is on foot and not down, not in full aim or its gap. The trace row says `taken=cut`
  (`TFP_CAMERA_TRACE_CUT`), and the `Smooth:` line counts the cuts apart from the changes of camera.
- `tools/harness/replay.py` reads `off_foot=` and `damp=`, gives the filter the head where the helper would
  (`head_given`: kind 4, source head, not off foot, not full aim; a line before feel.48 does not say off_foot and is
  taken as on foot), and writes HeadFrame rows (`--emit-head`) for tests.

What is not a cut stays as it was: the dive's own arc (the head moving 0.1 to 0.2 m a frame against the root after
the first frame: never from rest), a body off foot (thrown, ragdoll: the head is not given; soak 28's throw is no cut
even with it given, the head going 2.7 m/s the frame before), the held eye let go in an emote (0.15 to 0.18 m, round
14's M), prone out of an emote (the head from rest at 120 m/s^2: 0.13 m in a frame at 33 ms, 0.30 m at 50 ms, under
the bound at every frame time), every native frame, and every frame something is looked for.

Two damping scenarios of `tests/camera_smooth_test.cpp` (17 and 20) dropped the head 0.5 m in one frame from rest to
fill the damping's lag. That is a cut now; they drop it in two frames (0.24 and 0.26 m) and test the damping as before.

### Tests

- `tests/camera_continuity_test.c`: the real rows of soaks 27, 29 and 30 (`tests/reference/soak-30-cut.h`: sprint,
  dive-back twice, dive, the two-frame cut): the cut's step change against the root's place under the measure
  (0.001 to 0.013 m, the log 0.20 to 0.55 m), the frame after and the arc after it no more than the eye's own and the
  curve (under 0.03 m), no frame of the arc taken up. Soak 28's throw and soak 30's held eye: no cut, nothing carried.
  Made up at 8, 16.7, 33.3 and 50 ms: a 0.4 m cut, then the dive's arc at 30 m/s^2 against a root lunging at 5 m/s
  (nothing shown in the cut's frame, 0.002 to 0.041 m beyond the arc's own after it, under 0.01 m carried after 288 to 301 ms); the head from
  rest at 60 and 120 m/s^2 is no cut.
- `tests/camera_smooth_test.cpp` scenario 25: the same through the helper, Bodycam and Stable, on foot and thrown.
- `tests/test_harness_replay.py`: reading the fields, where the head is given, the rows for a test.

### The logs through the filter

`replay.py LOG --logged REV`, the present filter where the replay is exact, before (main, 383ddb1) and after:

| Soak | Build | The filter's steps, before | after | Metre-frames, before | after |
|---|---|---|---|---|---|
| 8 to 26 | | the same to the digit | | the same | |
| 27 | b70333e (feel.50) | 18 | 25 | 244.6 | 251.3 |
| 28 | cc0a95f (feel.51) | 13 | 17 | 122.0 | 126.3 |
| 29 | cc0a95f | 21 | 26 | 160.5 | 164.8 |
| 30 | cc0a95f | 12 | 17 | 89.4 | 93.2 |

The cut frames themselves, the log's step change against the present filter's (shown in the world, so the root's
own lunge of 0.08 m stays in it):

| Soak, frame | log | present | the root's own change |
|---|---|---|---|
| 27 12:52:57.770 / .804 | 0.402 / 0.619 | 0.084 / 0.071 | 0.083 / 0.082 |
| 27 13:04:44.367, 13:05:17.921 | 0.291, 0.481 | 0.083, 0.077 | 0.083, 0.082 |
| 28 16:03:34.909, 16:04:02.153 | 0.292, 0.486 | 0.083, 0.079 | 0.082, 0.082 |
| 29 16:16:30.771, 16:17:04.358 | 0.306, 0.456 | 0.082, 0.080 | 0.081, 0.082 |
| 30 16:33:39.703 (sprint) | 0.348 | 0.162 | 0.155 |
| 30 16:37:34.709, 16:38:01.952 | 0.272, 0.324 | 0.083, 0.080 | 0.082, 0.082 |

Every step added to the filter's count lies in these dives, 70 to 300 ms after the cut, on the dive's own arc: the
shown change is the wanted eye's own within 0.003 to 0.014 m (for example 16:37:34.909 of soak 30: 0.087 for a
wanted 0.085). The judge calls them the filter's only because the cut is still being let out there (0.03 m or more
carried), where it called the same frames the game's own before. The game's own count falls by as many or more (soak
30: 82 to 71). Nothing outside these actions changed.

### What only the game can show

1. That a dive or a sprint out of an emote now reads as one motion: the view stays and is eased onto the head over
   about 300 ms, and is not left behind it long enough to see the body from outside or the inside of the helmet
   (the eye is 0.3 to 0.4 m off the head at the cut). `taken=cut` in the row and the `Smooth:` line say it ran.
2. The sprint out of an emote keeps the root's own snap (0.16 m in one frame in soak 30): the root is shown, as
   everywhere. Whether a player sees it.
3. Whether a cut ever fires where it should not: a head-camera frame of a body on foot that moves 0.25 m against
   the root from rest for a reason of its own (an animation not seen in these soaks).
4. That a throw out of an emote still sets `off_foot` in its first moving frame, as soak 28's did.

### After the review of round 19

The review held the cause and the fix for the emote cases and found:

1. **A flick of the mouse was a cut.** The head handed to the filter was the head camera's eye less the damping,
   0.18 m ahead of the head node along the level view. In kind 4 the root does not turn with the view, so a turn of
   about 88 degrees or more in one frame after a calm frame moved that eye a chord of up to 0.36 m: flicks of 95 and
   180 degrees at 8, 16.7 and 33.3 ms were cuts, the eye 0.265 / 0.360 m off (0.18 m behind the head node) for about
   300 ms. Now the helper gives the head node itself (`smoothed`, the point `lock_damp_eye` damps), which does not
   move on a view turn: no cut, the eye on the head camera's eye in every frame, as main. The node moves 0.255 to
   0.553 m against the root in the eleven real cut frames (soak 27 12:52:57.770 is the least, against the 0.25 m bound).
2. **The root moving alone was a cut.** The root alone 0.5 m, the head and the eye still in the world: cut, the eye
   0.500 m off. A cut now also needs the head to jump as far in the world (the real cuts: 0.330 to 0.578 m); the
   root alone is read as before (Bodycam 0.060 m, Stable 0.310 m off at most, as main).
3. **The ease's end.** Soak 30 16:37:35.009, 300 ms after a cut, the step change went 0.009 -> 0.029 m where the
   cubic ended (it brakes hardest in its last frame). A cut is now eased on the quartic (1-s)^3 (d (1+3s) + v T s),
   at rest at its end with no acceleration, its length reckoned for its start (12 d / T^2 at ease_accel, up to
   max_ms times the square root of 2). Over the ten cuts, the 15 frames after each against main: the most a frame's
   step change is beyond main's 0.020 -> 0.008 m, summed 0.260 -> 0.141 m. (The quintic, at rest at both ends,
   brakes in its middle as hard as the cubic at its end: 0.021 and 0.429 m.)
4. **Exclusions not pinned.** Tests now pin each: a third cut in a row, a frame looked at, full aim, a change of
   camera, the frame after a start and after a restart, a head without the world, kind 5, a body that is down, and
   the first frame out of full aim. The ADS gap stays unpinned on purpose: it starts with a change of camera and
   lasts under 250 ms, inside the 600 ms in which jumps are looked for after one, so it cannot be told apart.
5. Scenario 17 drops the head 0.5 m in one frame again: with the smooth eye on that is a cut (the trace says so,
   the damping still runs at its 0.10 m, the eye is not moved in that frame), and the native view after it starts
   from where the eye is. Scenario 20 drops in one frame with the smooth eye off, in two with it on.

The node is smoothed by the head camera's own 0.06 m lag, so a made-up 0.4 m cut moves it 0.34 m: under the bound at
50 ms a frame (0.375 m, prone's 0.30 m in such a frame from rest stays shown), and shown as before there; at 50 ms a
0.55 m cut is a cut. The replay rebuilds the node from `damp=` and `view=` (feel.48 on); older lines are replayed
without the head. Soaks 8 to 30 replayed against main: soaks 8 to 26 the same to the digit; 27 to 30 eleven cut frames
(the ten cuts), 21 frames better by more than 0.02 m of step change, none worse (before: one, 16:37:35.009).
