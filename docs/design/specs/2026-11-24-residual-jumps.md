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

| Stretch | By lengths (old) | By vector: the filter's | the eye's own | the body's (not counted) | Old flags that are now the body's |
|---|---|---|---|---|---|
| feel.48 Stable soak and matrix (4439 frames) | 31 | 28 | 12 | 17 | 12 |
| feel.48 Bodycam soak to 08:46:24 (3280) | 21 | 20 | 12 | 12 | 6 |
| feel.47 Stable soak, `tfp-soak-22` (4755) | 24 | 24 | 19 | 9 | 5 |

So a soak now fails on more frames than before (40 for 31, 32 for 21), not fewer. **Figures from this
judge are not comparable with those written down before round 10.** The "by lengths" figure is printed for that.

**What can now slip through:**

- A step the filter makes that happens to equal, within 0.06 m, a lunge of 0.06 m or more that the root made in
  that frame or the one before.
- R2's second frame. The root's change is set aside in its own frame and the eye is given it whole in the next.
  That reads as the body's, a frame late. In this log: 08:34:07.181 (a landing inside a swap, 0.099 m shown, the
  root's 0.092 m of the frame before, 0.551 m carried) and 08:37:14.177. The eye does no more than the body did,
  but a diagnosis called the first one the filter's, and the judge no longer flags it.
- The body itself. A climb's 0.15 m is felt, and is no longer a failure. It is printed.

Also listed as the body's in this log, outside the climbs: 08:10:44.575, 08:15:21.651, 08:28:15.384, 08:35:53.884
and the root's hop of 08:37:13.8 (0.142 m up in one frame at the end of a sprint, the head camera riding it).

Tests: `tests/test_harness_trace.py` `Measure` (twelve made-up cases) and `SeenInTheGame` (a climb and a braking cut
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
