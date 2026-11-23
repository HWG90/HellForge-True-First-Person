# The swap's eye glide under the camera lock (2026-11-23, round 6)

From the analysis of the Stable soak of 0.6.0-feel.44 (soak 19, seed 9, 508 actions; `~/.cache/tfp-soak19-analysis/REPORT.md`
on the machine that ran it, section 4 item 6 and "For the harness"). Nothing here was run in the game.

## What the code did

`src/camera_helper.c`, the head-camera branch. Through a weapon swap the helper holds the native view's eye as a
difference from the head camera's own eye (`eye_delta`, in the body root's frame). When the weapon identity changes
(`weapon_key` is no longer `native_weapon_key`) and the incoming weapon's eye is remembered, `eye_delta` is moved to the
remembered one, every head-camera frame, by `1-expf(-dt/.07f)` of what is left. That is retail's glide (the differential
test compares it with retail's helper), and it is used again when the eye has let go and a weapon is drawn (`eye_return`).

- It is `1-exp(-t/70 ms)` of the way after t whatever the frames: 38 % in the first frame at 33 ms, 95 % after 210 ms,
  98.6 % after 300 ms. It never ends.
- Its first frame is its largest. A way of 0.25 m begins with 0.095 m at 33 ms a frame, 0.47 m with 0.178 m.
- In a frame the lock holds, the continuity filter looks for jumps then (a change of `weapon_key` opens
  `smooth_detect`'s 600 ms), and a step that is more than 0.04 m off the eye's kept speed is one: the glide's first
  frames are taken up as the rig re-seated, and the presented eye catches up on the filter's curve.
- When there is no eye to glide to any more (the swap goes back to the weapon it came from) the eye stops in the frame.

How long a swap leaves, from the soak's 69 "swap (weapon identity changed)" lines: the head camera had the view for
nine more frames (300 to 302 ms) in 45 of them, for five (167 ms) in 15, longer in the rest. The 15 short ones had
glides of 0.07 m or less.

How long the glides were (the eye's own change of step in the first glide frame, over retail's share of the way in
that frame; other motion is in it): 9 under 0.105 m (below that retail's first frame is no jump at 30 frames a
second), 10 from there to 0.2625 m, 8 to 0.378 m, 42 longer; 34 of the 69 are between 0.37 and 0.47 m.

## What it does now

`src/camera_glide.h`, new, as `src/camera_damp.h` and `src/camera_lock.h` are: the arithmetic alone.

- `tfp_eye_glide_retail`: retail's glide as it was, cut out of the helper. Without the lock nothing else is used, and
  the helper's results are the same to the bit (`tests/run-native.sh`).
- `tfp_eye_glide_step`: the gentle glide. From rest to rest, at the target itself. The eye gathers way at an even rate
  for the first 0.6 of the glide's time and loses it at an even rate (1.5 times as hard) for the rest:
  `x(u) = u^2/0.6` up to `u = 0.6` and `1-(1-u)^2/0.4` after, `u` the share of the time gone. The time is
  `sqrt(2 d / (0.6 * 14 m/s^2))`, 150 ms at least and 250 ms at most. The place is a function of the time since the
  glide began, so it is the same at the same time whatever the frames, a long frame included.
  - A target that changes under way: a glide from where the eye is, with the speed it has, to the new target; its
    time is that of the distance and no less than the eye needs to turn round.
  - An eye that is not where the last frame left it, or the first frame after `tfp_eye_glide_reset`: a glide from rest.
- `tfp_eye_glide_rest`: no target any more while the eye is under way: it comes to rest at the slowing-down rate.
- `tfp_eye_glide_lock_step`: what the helper calls in a frame the lock holds. THE GATE: a way of no more than
  0.2625 m when the glide begins (what 14 m/s^2 covers in 250 ms) is the gentle glide; a longer one is retail's, to
  the bit, and the filter hides it as before. Which of the two is settled when a glide begins.

In the helper: the lock's frames call `tfp_eye_glide_lock_step`, all others `tfp_eye_glide_retail`; the glide is
forgotten where the helper sets `eye_delta` anew (the first head-camera frame after the native view, the start of the
glide back) and when the hold lapses (the 80 ms fade, which is as it was); with the lock and no incoming eye an eye
under way is brought to rest. The view leaving the head camera mid-glide needs nothing more: no head-camera frame runs,
and the first one after the native view sets the eye anew.

The times, and why. 14 m/s^2 is under what the filter follows at 30 frames a second without taking anything up (a
steady 17 m/s^2 is 0.04 m off the speed it averages over 50 ms; slowing down is always followed), and the slowing-down
rate, 21 m/s^2, is 0.023 m of step change a frame there. 250 ms and not the 300 a swap leaves: a glide that ends in
the head camera's last frame hands the filter a kept speed that is lent to the native view (see below). 150 ms at
least so that the short glides of the 167 ms swaps are slow and still over in time. Retail's was 88 % there after
150 ms and 97 % after 250.

No log line of the mod speaks of the glide's time or shape; `eye_glides` counts the frames a glide moved the eye, as
before. `docs/design/ACTION-CAMERA.md` says of the glide back before the weapon returns "(70 ms time constant,
retail's swap glide)": that is still so without the lock, and with it for a way of more than 0.2625 m.

## Through the filter

`tools/harness/glide_filter.c` (how to build it is in its head): a made-up swap through the glide, the head camera's
damping and the continuity filter, in the helper's order. The root goes straight at 1.5 or 5 m/s (the head bobbing by
0.015 m at 1.8 Hz and 0.035 m at 2.8 Hz, made up), the native view on an ideal rig, the head camera for two frames
with the eye held, the glide for the 300 ms a swap leaves, the native view again at the incoming weapon's eye.

Columns, for retail's glide and for the lock's: the most the wanted eye's step changes in a head-camera frame; frames
taken up as jumps; the most carried; ms in all (and until, from the glide's start) more than 0.05 m is carried; the
most the presented eye's step changes; what the glide had left when the native view came back. Metres.

```
style   size speed frame | retail: wanted jumps carried over (until) shown  left | lock: wanted jumps carried over (until) shown  left
Stable  0.25  1.5   8.0 | 0.027  0 0.014   0 (  0) 0.027 0.003 | 0.001  0 0.016   0 (  0) 0.003 0.000
Stable  0.25  1.5  16.7 | 0.053  2 0.090  84 ( 83) 0.037 0.003 | 0.006  0 0.018   0 (  0) 0.006 0.000
Stable  0.25  1.5  33.3 | 0.096  2 0.142 166 (166) 0.051 0.003 | 0.023  0 0.023   0 (  0) 0.023 0.000
Stable  0.25  1.5  50.0 | 0.130  1 0.129 150 (150) 0.088 0.003 | 0.053  0 0.026   0 (  0) 0.053 0.000
Stable  0.25  5.0   8.0 | 0.027  0 0.026   0 (  0) 0.027 0.003 | 0.001  0 0.024   0 (  0) 0.002 0.000
Stable  0.25  5.0  16.7 | 0.052  2 0.087  84 ( 83) 0.036 0.003 | 0.006  0 0.021   0 (  0) 0.006 0.000
Stable  0.25  5.0  33.3 | 0.091  2 0.134 133 (133) 0.048 0.003 | 0.023  0 0.009   0 (  0) 0.029 0.000
Stable  0.25  5.0  50.0 | 0.125  1 0.122 100 (100) 0.085 0.003 | 0.051  0 0.019   0 (  0) 0.051 0.000
Stable  0.47  1.5   8.0 | 0.051  3 0.132 112 (120) 0.037 0.006 | 0.051  3 0.132 112 (120) 0.037 0.006
Stable  0.47  1.5  16.7 | 0.100  5 0.286 250 (250) 0.032 0.006 | 0.100  5 0.286 250 (250) 0.032 0.006
Stable  0.47  1.5  33.3 | 0.179  3 0.317 333 (333) 0.022 0.007 | 0.179  3 0.317 333 (333) 0.022 0.007
Stable  0.47  1.5  50.0 | 0.242  2 0.333 300 (300) 0.090 0.007 | 0.242  2 0.333 300 (300) 0.090 0.007
Stable  0.47  5.0   8.0 | 0.051  3 0.132 112 (120) 0.037 0.006 | 0.051  3 0.132 112 (120) 0.037 0.006
Stable  0.47  5.0  16.7 | 0.099  5 0.278 250 (250) 0.031 0.006 | 0.099  5 0.278 250 (250) 0.031 0.006
Stable  0.47  5.0  33.3 | 0.175  3 0.307 333 (333) 0.038 0.006 | 0.175  3 0.307 333 (333) 0.038 0.006
Stable  0.47  5.0  50.0 | 0.237  2 0.319 250 (250) 0.086 0.006 | 0.237  2 0.319 250 (250) 0.086 0.006
Bodycam 0.25  1.5   8.0 | 0.027  0 0.013   0 (  0) 0.027 0.003 | 0.001  0 0.015   0 (  0) 0.002 0.000
Bodycam 0.25  1.5  16.7 | 0.053  2 0.091  84 ( 83) 0.037 0.003 | 0.006  0 0.017   0 (  0) 0.006 0.000
Bodycam 0.25  1.5  33.3 | 0.096  2 0.142 166 (166) 0.051 0.003 | 0.024  0 0.019   0 (  0) 0.024 0.000
Bodycam 0.25  1.5  50.0 | 0.130  1 0.129 150 (150) 0.088 0.003 | 0.053  0 0.023   0 (  0) 0.053 0.000
Bodycam 0.25  5.0   8.0 | 0.027  0 0.030   0 (  0) 0.027 0.003 | 0.001  0 0.028   0 (  0) 0.002 0.000
Bodycam 0.25  5.0  16.7 | 0.052  2 0.087  84 ( 83) 0.035 0.003 | 0.006  0 0.024   0 (  0) 0.007 0.000
Bodycam 0.25  5.0  33.3 | 0.091  2 0.133 133 (133) 0.048 0.003 | 0.023  0 0.012   0 (  0) 0.032 0.000
Bodycam 0.25  5.0  50.0 | 0.124  1 0.120 100 (100) 0.084 0.003 | 0.050  0 0.020   0 (  0) 0.053 0.000
Bodycam 0.47  1.5   8.0 | 0.051  3 0.132 112 (120) 0.037 0.006 | 0.051  3 0.132 112 (120) 0.037 0.006
Bodycam 0.47  1.5  16.7 | 0.100  5 0.287 250 (250) 0.032 0.006 | 0.100  5 0.287 250 (250) 0.032 0.006
Bodycam 0.47  1.5  33.3 | 0.179  3 0.317 333 (333) 0.021 0.007 | 0.179  3 0.317 333 (333) 0.021 0.007
Bodycam 0.47  1.5  50.0 | 0.242  2 0.334 300 (300) 0.090 0.007 | 0.242  2 0.334 300 (300) 0.090 0.007
Bodycam 0.47  5.0   8.0 | 0.051  3 0.131 112 (120) 0.036 0.006 | 0.051  3 0.131 112 (120) 0.036 0.006
Bodycam 0.47  5.0  16.7 | 0.099  4 0.256 234 (234) 0.039 0.006 | 0.099  4 0.256 234 (234) 0.039 0.006
Bodycam 0.47  5.0  33.3 | 0.174  3 0.305 333 (333) 0.040 0.006 | 0.174  3 0.305 333 (333) 0.040 0.006
Bodycam 0.47  5.0  50.0 | 0.236  2 0.317 250 (250) 0.086 0.006 | 0.236  2 0.317 250 (250) 0.086 0.006
```

Over all 96 (0.07, 0.15, 0.25, 0.26, 0.37 and 0.47 m):
`96 swaps: 132 frames taken up as jumps with retail's glide, 88 with the lock's; less is carried in 34, the same in 50, more in 12 (by 0.002 m more at most); the presented step changes by more in 0 (by 0.000 m more at most)`

- 0.25 m: the filter takes nothing up at any frame time (retail's: 1 to 2 frames at 16.7 ms a frame and above). At
  16.7 ms a frame and above 0.009 to 0.026 m is carried where 0.087 to 0.142 m was, never more than 0.05 m (retail's:
  for 83 to 166 ms). The presented eye's step changes by 0.002 to 0.053 m where it changed by 0.027 to 0.088 m, less in
  every one of the 16. At 8 ms a frame retail's first frame is no jump either (0.027 m): there what is carried when
  the native view comes back is within 0.002 m of what it was, more in a walk (0.016 for 0.014 m) and less in a
  sprint; that is the change of camera, not a jump. The "more in 12" of the line above are all of that kind.
- 0.47 m: retail's glide under the lock too, so every figure is the same. This is the larger part of the finding
  (0.25 to 0.47 m carried, 34 of the soak's 69 swaps between 0.37 and 0.47 m) and it is **not mended**.

The logged swaps (`work/glide_filter logged`; the logged rows through the filter from nothing at the first row, once
as logged and once with the glide exchanged; `tests/reference/soak-19-glide.h`). What the filter carries in the first
rows is therefore not what the log carried, and the damping is not run again:

| Swap | Way | In the head camera, as logged | With the lock's glide |
|---|---|---|---|
| 07:12:32.118, standing | 0.209 m | 2 jumps, 0.116 m carried, presented step change 0.043 m at most | no jump, 0.000 m carried, 0.023 m |
| 07:01:50.764, sprint | 0.305 m | 3 jumps, 0.183 m carried | retail's (GATE): the same |
| 07:04:10.384, sprint (the 835 ms) | 0.629 m | 5 jumps, 0.468 m carried | retail's (GATE): the same |

In soak 19, five swaps had a glide of 0.105 to 0.2625 m that the filter took up (two jumps each, 0.058 to 0.118 m
carried): 07:00:43.691, 07:12:32.118, 07:12:33.252, 07:18:35.474, 07:18:48.382. Those are what this changes.

## What was tried for the longer ways and not kept

The rule for this round: a glide that does not reduce what is carried and for how long, or that makes the presented
eye's step change by more anywhere, is not kept. Both of these carry less and both fail the second half.

1. **The gentle glide for every way, 300 ms at most** (`-DTFP_EYE_GLIDE_MAX_MS=300.0f -DGLIDE_UNGATED`). 0.47 m is
   gone at 17.4 m/s^2, 0.63 m needs 23.3.
   - Made-up swaps: 4 frames taken up in all 96 where retail's glide has 132; 0.47 m at 33 ms a frame carries 0.015 to
     0.027 m for retail's 0.305 to 0.317 m and never more than 0.05 m.
   - But the presented eye's step changes by more in 6 of the 96, by up to 0.017 m: 0.47 m in a walk at 33 ms a frame,
     0.029 m for 0.021 to 0.022 m (the glide's own slowing down, which the filter no longer hides; under its measure),
     and in a sprint at 50 ms a frame 0.101 to 0.104 m for 0.086 m (0.47 m) and 0.086 to 0.089 m for 0.071 m (0.37 m).
     There the glide ends in the head camera's last frame, the speed the filter keeps for it (about 1.3 m/s) is lent to
     the native view, 0.064 m is carried at the change of camera, and the native eye's next frame reads as "slowing
     down": the lent speed is dropped at once (item 1 of the analysis).
   - The logged swap of 0.629 m: 3 jumps for 5 and 0.189 m carried in the head camera for 0.468 m, but one frame
     (07:04:10.619) shows 0.081 m of step change where the same rows with retail's glide show 0.054 m, and the
     glide's first frame 0.054 m for 0.036 m (it is followed while the catch-up from the change of camera before it is
     still running).
   - The logged swap of 0.305 m: no jump for 3, nothing carried in the head camera for 0.183 m, more than 0.05 m for
     368 ms in all its rows for 535 ms; the largest step change of its rows 0.054 m for 0.056 m.
2. **The gate at the 0.378 m that 14 m/s^2 covers in 300 ms** (`-DTFP_EYE_GLIDE_MAX_MS=300.0f`): the presented eye's
   step changes by more in 2 of the 96, 0.37 m in a sprint at 50 ms a frame, 0.086 to 0.089 m for 0.071 m, the same
   lent speed. At 33 ms a frame in a sprint 0.37 m is taken up in part (2 frames, 0.084 to 0.088 m carried for 0.229 m,
   over 0.05 m for 100 ms in all for 233 ms, but until 433 ms after the glide began for 233 ms).

Why no gentle glide can pass for 0.47 m as the rule is written: 0.47 m in 300 ms needs 21 m/s^2 somewhere at the
least (even rates, half and half), which is 0.023 m of step change a frame at 33 ms, and retail's glide shows 0.021 to
0.022 m there because the filter carries all of it for a third of a second. A glide the filter follows shows its own
acceleration; one the filter hides shows the filter's curve. For the long ways the two halves of the rule pull apart,
and what would let them through is on the filter's side: the lent speed dropped in one frame at the change of camera
(what makes the 50 ms figures), or a swap that left more than 300 ms.

Both are one constant away, and the numbers above are there to choose by: 30 frames a second is what the soaks run at,
and at 33 ms a frame and below the ungated glide is worse only by the 0.007 m in a walk.

## Not done

- The longer ways (above).
- The fade when the hold lapses (`expf(-dt/.08f)`) begins at full speed as well. Not touched: it is not the swap's
  glide, and the analysis did not name it.
- A swap that goes back to the weapon it came from leaves `eye_delta` where the glide had brought it (now: where the
  eye came to rest), which is neither weapon's eye; the native view's return is then a change of camera of that much.
  That is retail's logic and was left.
- `tests/camera_glide_helper_test.cpp` uses the scene of `tests/camera_smooth_test.cpp` by including that file with
  its `main` renamed; a change to that file's helpers will show there.
- Nothing was looked at in the game.

## The harness: settled, or cut off by the trace

`tools/harness/replay.py` printed "more than 0.05 m for N ms at most after a change of camera". The figure ran to the
first frame at or under 0.05 m, or to the last frame there was, without saying which. `carries()` now tells them
apart, and the line says which the figure is, gives the longest of the other kind and counts both; the line begins as
before. Soak 19, where the replay is exact (log, logged filter and present filter alike):

`carried: 257.1 metre-frames; more than 0.05 m for 835 ms at most after a change of camera (07:04:10.351), cut off by
the end of the trace with 0.180 m still carried; the longest that settled: 801 ms (07:10:10.927); 61 settled, 11 cut off`

Over all frames: 69 settled, 11 cut off (the log itself: 68 and 11).

## To look at in the game

Camera lock on, either camera style, 30 and 60 frames a second.

- Swap between two weapons whose remembered eyes are less than 0.26 m apart (in soak 19 the swaps at 07:00:43.691,
  07:12:32.118, 07:12:33.252, 07:18:35.474 and 07:18:48.382 were such; the same seed gives the same actions), standing
  and walking. In the log, after `State: swap (weapon identity changed)`: the `Trace:` rows with `kind=4` should show
  `taken=-` and `correction=` at 0.000 through the swap (they showed `taken=jump` twice and 0.058 to 0.118 m), and the
  wanted eye's steps against the root should rise and fall (0.008, 0.023, 0.039, 0.054, 0.048, 0.025, 0.004 m for
  0.2 m at 33 ms a frame) instead of beginning with the largest. The `Smooth:` line of that second should count no
  rig moves taken up for the swap.
- The same with a pair further apart (the primary and the support weapon were 0.37 to 0.47 m in the soak): as before,
  `taken=jump` in the first two or three head-camera frames. Nothing should have changed there.
- Tap the swap key twice within 0.1 s between a near pair: the eye should come to rest, not stop in a frame, and the
  native view's return is taken up as a change of camera as before.
- `python3 tools/harness/replay.py LOG --logged REV`: the "carried:" lines now say settled or cut off; the count of the
  filter's steps should not rise against soak 19's 17.
