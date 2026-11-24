/* Continuity of the presented eye (beyond retail; the camera helper uses it only while the camera lock holds a
 * frame). The arithmetic only: no engine reads here.
 *
 * The helper presents an eye from one of several sources: the head camera, the game's first-person camera on the
 * arm rig, the last native argument repeated while the camera is still linked. Each is continuous by itself most
 * of the time; what jumps is the change from one to another, and the rig when the game re-seats it in one frame
 * (a weapon leaving the hands, a new camera request). This keeps the eye where it was going and carries the
 * difference as a correction that is eased out: presented = wanted + correction.
 *
 *  - Positions are in the body's frame (the body root's position and axes), so the body's own travel is not a
 *    jump and "the eye never moves faster than the body plus a bounded catch-up".
 *  - The root itself can be re-seated in one frame with the eye going on as it went (seen in the game 2026-11-22: a
 *    backwards dive's first frame, the root 0.6 m further on and the rig eye following a frame later). In the root's
 *    frame alone that reads as the eye jumping, and "keeping the eye where it was against the root" threw it 0.7 m
 *    in the world. So the caller also gives the eye's place in the world (tfp_continuity_step_in), which with the
 *    eye in the root's frame gives the root itself. THE COMPARISON: the root's part of a step is how far the root is
 *    off its own even course in this frame, seen at the eye: the eye in the root's frame as it is, less where the
 *    same eye in the world would be in the frame of a root that had gone on as it went (its turning, last frame's,
 *    and its speed averaged over 50 ms; the speed kept in the world, as a thrown body keeps it, or along the root's
 *    own axes, as a body running in a turn keeps it, whichever fits the frame better). When a step is a jump in the root's frame, the
 *    root's part of it is more than the measure, and the step without that part is smaller, only the step without
 *    it is taken up; the rest is the root's. What is carried goes on as it was in the world (turned by as much as
 *    the root is off its course). A root that runs and turns evenly has no part in anything, however fast: a jump
 *    is then the eye re-seated, as without the world; a body moved with its camera (a respawn) is nothing, as before.
 *    (Not the eye's speed in the world: that turns with the body, its average lags behind in a fast turn, and the
 *    lag passed for the root's part, so that part of a genuine re-seat was shown at once: 0.26 m of step change for
 *    0.17 m, the eye 0.8 m ahead of a root turning at 12 rad/s.)
 *  - THE TURNING. The root's frame is right for an eye that goes round with the root, and only for that. Seen in
 *    the soak of 2026-11-22 on 0.6.0-feel.39 (tests/reference/soak-6-steps.h):
 *    a body that began to sprint turned its axes by 0.26 rad in one frame; what had been taken up the frame before
 *    was kept against the root, and the presented eye, 0.87 m from the root, went round with the axes: 0.176 m in
 *    a frame the root moved 0.041 m. And a root that turned on the spot at 10 rad/s after a landing, the rig eye
 *    standing in the world 0.6 m from it: in the root's frame that eye goes round, each frame's step is off the last
 *    by 0.6 m times the turn squared, more than the measure; jump after jump was taken up, 0.61 m was carried, and
 *    the presented eye went 0.06 m a frame while the wanted one stood. So every step is read a second way as well,
 *    with the root's turning left out: the eye against the root's place (its place on its course, if the root was
 *    re-seated) along the world's axes, against that eye's own speed, kept and averaged as the other is. A frame is
 *    read that way, and what is carried and the eye's speed then keep their direction in the world, when
 *      - the root's axes are off their even course by more than the measure, seen at the eye, and the step is a jump
 *        both ways or a change of camera: what is taken up is the step without the turning, and the presented eye
 *        goes on against the root's place, not round with its axes; or
 *      - the eye has not gone round with the root of late: the wanted eye's step changed more from frame to frame
 *        in the root's frame than without the turning, by half the measure on average over 100 ms. An
 *        eye that goes round with a root turning evenly, however fast, is the other way about (its step changes
 *        more without the turning) and is read in the root's frame as before, a re-seat in a turn too.
 *    In a frame the axes are off their course what is carried keeps its direction in the world whatever the step.
 *    THE ONSET. A root that begins to turn is off its course for one frame, which reads as the root re-seated; in the
 *    next it keeps the new turning, and that is its course from there (THE COMPARISON). An eye that does not go
 *    round with it is then off the speed kept for it in the root's frame by the turn times its distance from the
 *    root, frame after frame: jumps in the root's frame alone, and to take them up is to hold the presented eye
 *    against axes that turn on. Seen in the soak of 2026-11-22 on 0.6.0-feel.41 (tests/reference/soak-11-steps.h):
 *    a dive out of a sprint, the root's axes rolling by 0.06 rad a frame under a rig eye 1.25 m above it that went
 *    straight on; three frames were taken up, the presented eye went 0.21 m to the side and its step changed by
 *    0.090 m where the wanted eye's changed by 0.026 m. (Made up, the same roll at 8 rad/s and 8 ms a frame: 0.83 m
 *    to the side.) So a frame in which the root keeps a course it took up alone the frame before is followed, not
 *    taken up, when
 *      - its step is a jump in the root's frame and none with the turning left out, the two half the measure apart;
 *      - the root's axes are on that course within half the measure, seen at the eye, and their turn of this frame
 *        moves the eye by half the measure or more in the root's frame (a kept course that does not turn makes no
 *        such jump: the two readings then differ by the speeds kept for them, as they did at 00:30:53.388 of the
 *        second feel.40 soak, where following the frame made 0.135 m of 0.055 m); and
 *      - the eye has not been seen to go round with the root of late (its step changing more without the turning
 *        than with it, by half the measure over 100 ms: a rig re-seated in a fast even turn can be under the
 *        measure without the turning by chance).
 *    What is carried keeps its direction in the world in that frame, and the eye's speed in the root's frame is
 *    believed at once, the new turning in it, so the frames after are measured against it as any others.
 *  - A jump is a frame whose step differs from the wanted eye's own recent speed by more than
 *    max(pop_m, pop_accel * dt * dt). A change of source takes up any difference. Jumps within a source are looked
 *    for only where the caller says so (`detect`: the short time after something that can re-seat the rig); anywhere
 *    else a change of speed is the eye's own motion and is followed. A step shorter than the speed predicted, in the
 *    same direction, is the motion slowing down and is followed, not taken up. THE STOP: so is a step of next to
 *    nothing (under TFP_CONTINUITY_STOP of the measure and a quarter of the step predicted) whichever way it points:
 *    an eye that stops dead has no direction, and its last millimetres point where they will. Seen in the Bodycam
 *    soak of 2026-11-23 on 0.6.0-feel.44 (tests/reference/soak-18-steps.h): a rig eye going 1.9 m/s against a
 *    standing root stopped in one frame, its 4 mm against its way: a jump of 0.064 m, taken up, and the next frame's
 *    3 mm too; the presented eye went 0.117 m past an eye that stood, and when the third frame's 3 mm pointed
 *    forwards the frame was followed and the kept speed dropped whole on the catch-up begun, 0.076 m of step change
 *    for the eye's own 0.001 m. (Made up, 4 m/s at 33 ms a frame, all four frames against the way: the run's time
 *    out and the speed handed to the curve, 0.56 m past an eye that stood.) Only as the first frame, not in a run of
 *    jumps: there the frames before were taken up already, and to follow one then is to drop the kept speed after
 *    it was carried on (on the logs, without that: 0.065 and 0.080 m where 0.031 and under 0.06 m had been). And
 *    not against a speed kept from the camera before (THE LENT SPEED), which is let down, not dropped.
 *    THE HITCH. A stop followed drops the kept speed whole. An eye that only missed a frame (its step next to nothing
 *    for one frame, the game's own hitch) and then goes on as it went was a jump against a speed of nothing in its
 *    next frame, and in those after it for the time of a run: made up, 2 and 4 m/s at 16.7 to 50 ms a frame, 0.21
 *    to 0.61 m carried, 450 to 550 ms until it was let out, and the presented eye fell back 0.03 to 0.15 m a frame
 *    behind an eye it was to catch up with. So the speed dropped at a stop is kept beside the new one for one frame,
 *    and the frame after a stop whose step is within the measure of that speed's is the eye going on: followed, and
 *    its speed believed at once, as any new speed is. Nothing is carried; what shows is the hitch. Seen once in nine
 *    soak logs (tests/reference/soak-14-steps.h, soak14_hitch: a rig eye dying down, 0.052, 0.002 and 0.056 m, then
 *    at rest; the 0.056 m was taken up and let out over four frames, and is now shown as it is). One frame only: an
 *    eye that stands for two frames and goes on has stopped and set off, as before.
 *    THE TRIAL. The frame that goes on after a stop is one frame, and one frame's step is no speed. With its speed
 *    believed for good, an eye that only flickered after its stop (a step on, the same step back, at rest: made up,
 *    2 and 4 m/s at 16.7 to 50 ms a frame) was a jump of two steps in the frame it went back, taken up: the presented
 *    eye ran one step more forwards, 0.067 to 0.200 m past anywhere the wanted eye had been, and came back over 200
 *    to 400 ms with two steps carried, where the filter before THE HITCH had carried one for 117 to 150 ms. So that
 *    speed is on trial for one frame, and what was kept before it is kept beside it. The next frame confirms it by
 *    going on within the measure of it, and is an ordinary frame; a frame that slows down or stops is followed as
 *    any such frame is (a second hitch is a stop after it, and the frame after that can go on again). Any other
 *    frame says the speed never was, and is asked against what was kept before the trial: a step back, no further
 *    than a measure past where the eye stood, is the game's own flicker and is followed, nothing carried and no speed
 *    believed from it (a flicker is no speed); anything else is a jump against the speed kept before, so the
 *    presented eye stays where the wanted eye has been. Only a frame whose step along the trial's way is at least the
 *    trial's own (on at twice the speed) is still a jump against the speed on trial: the presented eye going on at
 *    it is then behind the wanted one, not past it.
 *    And the speed dropped at a stop is kept for the frame after only if the eye was going at it: not when it was
 *    believed the frame before, when a run's time was over or at once straight after a frame taken up, and no frame
 *    has since gone on at it. Seen in the Stable soak of 2026-11-23 on 0.6.0-feel.44 (tests/reference/
 *    soak-19-steps.h): an eye thrown about in a dive, 0.25 m carried, a run of jumps ended with 1.9 m/s believed, the
 *    next frame 0.006 m (a stop) and the one after 0.051 m, within the measure of that speed's step by chance. It was
 *    followed as the eye going on, while the curve was at its steepest: 0.064 m of step change for the wanted eye's
 *    0.042 m, a step the filter before THE HITCH did not make. Now it is a jump, as it was.
 *    Jumps in a row are taken up for
 *    TFP_CONTINUITY_RUN_MS at most (a time, not a number of frames); then the new speed is believed, and the
 *    difference between the two speeds goes into the correction's curve, so the presented eye keeps its speed in
 *    that frame and gathers the new one along the curve. The new speed is what the run's last frames agree on
 *    (TFP_CONTINUITY_SPEED_FRAMES at most, each axis by itself: tfp_continuity_believed), never one frame's step
 *    alone: the frame the time runs out in can itself be a jump (seen in the game 2026-11-22: a rig eye going back
 *    and forth by 0.19 m from frame to frame; the last flip, believed as 6 m/s, sent the presented eye off at that
 *    speed while the rig stood). A flicker is no speed at any frame time; a motion that begins and goes on is.
 *    Once in a run, within its time, a frame counts as a jump that is under the measure by a tenth of it at most
 *    and goes the way the jump before it went (within 45 degrees). A rig re-seated over three frames is three
 *    steps of one re-seat, and the third can be a hair under: seen in the soak of 2026-11-22 on 0.6.0-feel.41
 *    (tests/reference/soak-11-steps.h: 0.053, 0.052 and 0.045 m, the last 0.0396 m off the kept speed against a
 *    measure of 0.0406 m) and three times in the soaks before it (0.039 m against 0.040 m). Followed, that frame
 *    was shown whole while the curve begun at the jump before it was at its steepest: 0.066 m of step change for an
 *    eye whose own was 0.006 m. Taken up, nothing is believed for it; the kept speed stands as in every frame of a
 *    run. Only once: such a frame can as well be the first of a motion that goes on, each one taken up is up to a
 *    measure more carried, and at 8 ms a frame eight of them fit a run (made up, 0.15 m and then 0.97 of the
 *    measure a frame for eight frames: 0.28 m more carried and 230 ms longer without the limit; with it 0.027 to
 *    0.063 m more, 32 to 100 ms longer, the step 0.001 to 0.004 m more). (Any frame above half the measure kept in
 *    the run, whichever way it went, broke the flicker test; three quarters of the measure, the way of the jump
 *    before, carried 0.027 m more for a motion dying down at 8 ms a frame.)
 *    A run that ends before its time, at a frame that is no jump, ends without a speed believed: that frame is
 *    followed as any other. (Tried on the soaks of 2026-11-22 and taken out again: that frame taken up as well and
 *    what the run's frames and it agree on handed to the curve as the eye's new speed. The run's frames are the
 *    re-seat, and a re-seat is no speed: a rig re-seated in two frames that then crept on was believed to be going at
 *    the re-seat's speed and the presented eye was thrown backwards, 0.07 m in a frame and 0.24 m in all at 60 frames
 *    a second; a re-seat against the eye's way left one frame that agreed with itself, no speed was believed and the
 *    eye that went on was taken up frame after frame. tests/camera_continuity_test.c, THE RUN'S END IS NO SPEED.)
 *    THE LENT SPEED. At a change of camera the speed kept is the camera before's; the new camera's own is not known
 *    until a frame of it has been followed or a run's time is over. Frames of the new camera that are jumps against
 *    that speed are taken up, and in each the presented eye went on at it whole: the old camera's motion shown under
 *    the new camera's eye. Seen in the soak of 2026-11-22 on 0.6.0-feel.42 (tests/reference/soak-14-steps.h): a dive
 *    begun, the head camera's eye thrown forwards at 5 m/s against the root (0.166 m a frame), and the native view
 *    entered in that frame. The rig eye was re-seated by 0.29 m in its first frame and stood in the second: jumps
 *    both against the lent speed, and the presented eye went 0.33 m on the head camera's way under a rig eye that
 *    had never gone it. In the third frame the rig eye moved 0.086 m another way, which against 0.166 m reads as the
 *    motion slowing down and is followed: the lent speed dropped whole in one frame, 0.180 m of step change for the
 *    rig eye's own 0.076 m. (The same at 00:30:53.355 of the second feel.40 soak: 2.5 m/s lent, 0.117 m for 0.055 m.
 *    A rig eye that stands in its second frame is "slowing down" against any speed: made up, 0.177 m at 33 ms a
 *    frame and 0.283 m at 50 ms.)
 *    So from the change of camera on, in every frame that is taken up until the new camera's speed is known, the
 *    presented eye is let down from the lent speed by pop_accel (what is no jump at any frame time), as far as the
 *    speed whose step in one frame is the measure: to drop the rest at once is then no jump either. What is asked of
 *    a frame (jump, slowing, a run's end) is asked against the lent speed whole, as before: only what the presented
 *    eye does in the frames taken up is changed, and what is handed to the curve when a run's time is over is what
 *    the presented eye still had. A lent speed under that of the measure (a walk, a sprint's bob) is not touched.
 *    (Tried: the kept speed itself let down, so that the frames are asked against less. The 0.180 m went as well,
 *    and the rig eye's own motion in its third frame was then a run of jumps and a speed believed, two curves begun
 *    later: more than 0.05 m was carried for 469 ms after that change of camera where it had been 435 ms. Without
 *    the floor, either way, a creep of 0.04 m/s was let down too and a step of the logs went from 0.05993 to
 *    0.06005 m.)
 *    THE LET-DOWN AT A SLOWING. A frame followed as the motion slowing down drops the kept speed whole. Straight after
 *    frames taken up the presented eye was still going at that speed (all of one believed when a run's time was over,
 *    what was left of a lent one), and it is the presented eye's step that drops. Seen in the Stable soak of 2026-11-23
 *    on 0.6.0-feel.45 (tests/reference/soak-slowing-steps.h): the native view entered in a swap, the rig eye thrown
 *    forwards for four frames, 4.7 m/s believed (0.156 m a frame) with the presented eye 0.87 m behind and gathering
 *    that speed along the curve; the next frame went 0.048 m the same way, and the presented eye, which had 0.87 m to
 *    catch up, was thrown back: 0.114 m of step change, 0.046 m further from the eye it was closing on, and 0.061 m in
 *    the frame after. In a dive, 6.4 m/s believed and 0.044 m gone: 0.144, 0.080 and 0.066 m, 0.131 m further behind.
 *    So in such a frame (followed as slowing down, over the measure, the frame before taken up; not a stop, which is
 *    THE STOP's) the part of the speed dropped that points at the wanted eye is kept by the presented eye and let down
 *    by pop_accel a frame, as a lent speed is, and sooner where the way to the wanted eye is shorter than that takes:
 *    it never goes past the wanted eye along that way, nor does the curve take it past (the way counts less what the
 *    curve's own speed needs: without that, 0.007 m past at 00:25:52.043 of the feel.40 soaks). What it goes is
 *    taken off what is carried; the curve keeps its end. Only where that part's step is over the measure. The frames are asked as before: none is taken up or
 *    followed that was not, and the kept speed is the wanted eye's new one at once.
 *    A FRAME TAKEN UP WHILE IT RUNS (a jump, a change of camera) puts the wanted eye somewhere else, and the way to it
 *    is measured anew there: against what is carried after the jump and the curve begun in that frame, its speed and
 *    its whole length. Where the wanted eye is then behind the presented one, or the new curve needs the way itself,
 *    the let-down ends in that frame; where it is still ahead (a change of camera to the side) it goes on. Made up
 *    (no log has a frame where the two differ): 8 m/s believed, a quarter of it for one frame, the rig re-seated 1.2 m
 *    backwards in the next and standing, 33.3 ms a frame. With the room as it was before the jump the presented eye
 *    went 0.120 m on in the frame of the re-seat, away from the wanted eye, and lost that the frame after: 0.154 m of
 *    step change, 0.348 m past, settled in 333 ms. Now 0.069 m, 0.228 m and 266 ms. (The filter before the let-down:
 *    0.139 m, 0.072 m, 166 ms. It throws the eye back 0.091 m in the frame that slows down, which is what the let-down
 *    mends; the 0.160 m the let-down goes in that frame, towards the wanted eye as it then stood, is how much further
 *    past it the re-seat leaves the presented eye, and that takes the curve 100 ms longer. The frame that slows down
 *    does not know of a re-seat to come.)
 *    The part that points away from the wanted eye, or across, is dropped as it was. That is the larger steps of the
 *    feel.44 soak: going prone out of an emote, 7.7 m/s lent, the presented eye 0.32 m past a rig eye that all but
 *    stands and 5.3 m/s dropped at once, 0.182 m of step change for the rig eye's own 0.070 m (three times, and 0.074 m
 *    for a speed believed). To let that down is to go on away from the wanted eye, the eye coasting past a stop: tried
 *    in several ways and left (docs/design/specs/2026-11-22-locked-view-design.md, round 7).
 *  - The catch-up is a cubic with zero end speed whose length follows from the distance, so that its largest
 *    acceleration is ease_accel: sqrt(6 * distance / ease_accel), between min_ms and max_ms. A new jump during a
 *    catch-up starts the curve again from the correction's present value and speed.
 *  - More than max_m is not carried (the rest is shown at once); a step of more than teleport_m, a pause longer
 *    than the helper's frame gap, or a value that is not finite starts again with no correction.
 *  - quiet (full aim): nothing new is taken up and what is carried ends within 80 ms; the sights stay on the eye.
 *  - THE CUT (round 19, tfp_continuity_step_cut). Out of an emote the game cuts the body's pose in one frame when the
 *    diver moves (sprint, dive, dive-back), and the head camera rides the head: the root jumps or turns (a dive-back's
 *    yaw flips 160 degrees) and the head is 0.34 to 0.61 m further on against it in that frame, from rest. Nothing is
 *    looked for there (no event in SMOOTH_EVENT_MS: the gun left the hands a second before), so the whole cut was shown,
 *    0.27 to 0.62 m of step change (soaks 25 to 30 on 0.6.0-feel.49 to .51; tests/reference/soak-30-cut.h). So where
 *    the caller gives the head (the head node the head camera's eye is built on, before the eye damping, in the root's
 *    frame: a head-camera frame of a body on foot; not the eye, which swings round the node with the view), a frame in
 *    which nothing is looked for and the head moves TFP_CONTINUITY_CUT_M or more against the root and as far in the
 *    world, from under TFP_CONTINUITY_CUT_REST the frame before, is a cut, and is taken up as a change of camera: whole,
 *    the eye's speed lent from before (none: it was at rest), on a curve that ends at rest (a quartic, its length
 *    reckoned for its start; tfp_continuity_curve). The frame after one cut can be a cut too
 *    (seen on feel.50: 0.39 and then 0.32 m), not a third. The bound grows with the frame's time as an acceleration
 *    (TFP_CONTINUITY_CUT_ACCEL times its square), so honest fast motion from rest is no cut at any frame rate: prone out
 *    of an emote (the head at 240 m/s^2 from rest, 0.30 m in a frame of 50 ms) is shown as before. What is no cut is as
 *    it was: the head moving the frame before (a dive's own arc, 0.2 m a frame), a body off foot (thrown, ragdoll: the
 *    caller does not give the head), the held eye let go (0.15 to 0.18 m), a flick of the mouse (the node does not
 *    move), the root moving by itself with the head still in the world (the re-seat's to read), and every native
 *    frame. (Round 19 review: with the eye given, flicks of 95 and 180 degrees and the root alone 0.5 m were cuts.) */
/*  - THE CLIMB (round 24, `traversal`, told by the caller: a frame under a clamber's or a vault's mask,
 *    src/camera_traversal.h): a run of jumps whose time is over in such a frame hands no speed to the curve; the
 *    presented eye takes the wanted eye's new speed in that frame. In the user's log of 2026-11-24 the run was the
 *    head's crouch onto a ledge read against a root rising 1.8 m in 0.3 s, and the speed handed on (the head going up
 *    with the root) carried the eye 0.6 m above the head.
 *  - THE SETTLE (round 24, `settle_ok`, told by the caller: the native view on mask 0, src/camera_helper.c). Back from
 *    a backpack pick-up (the user's log of 2026-11-24, 20:27:07, 20:32:10, 20:34:19) the rig's eye is 0.22 to 0.25 m
 *    under the head camera's and re-seats itself against the root for about 300 ms (forward 0.32 -> 0 m, up 1.53 ->
 *    1.76 m) while the body turns 0.1 rad a frame: the change of camera was taken up, two or three re-seats more, and
 *    then what was carried grew again with nothing taken up (0.068 -> 0.235 m), a drift of 0.2 to 0.5 m. So a change
 *    of camera of TFP_CONTINUITY_SETTLE_M or more where the caller allows it holds the eye it presents fixed against
 *    the root until the new camera's eye has been steady against the root for TFP_CONTINUITY_SETTLE_FRAMES frames in a
 *    row (its step there no jump against the step before: it stands or goes on evenly; the re-seat's own frames are
 *    jumps), or for TFP_CONTINUITY_SETTLE_MS at most, and then eases to it once, from rest, on the curve whose length
 *    follows from the distance, and follows its own even motion from there. Another camera, full aim or a frame the
 *    caller no longer allows it ends the hold with that frame's own answer. The caller allows it only for the first
 *    entry after the native view was left because its request or firearm changed (the binder's
 *    tfp_camera_settle_arm): allowed at every entry on mask 0, soaks 8 to 31 replayed had 554 frames worse by more
 *    than 0.02 m of step change and floats up to 1.05 m (a rig that moves evenly and fast against the root, a sprint's
 *    entry, held as if it were settling). (Tried first: steady as under 0.5 m/s
 *    against the root. The rig's slow climb at 20:32:10 never was, the hold ran its 350 ms, and the one ease was
 *    0.28 m, more than the drift it replaced.) */
#ifndef TFP_CAMERA_CONTINUITY_H
#define TFP_CAMERA_CONTINUITY_H
#include "camera_math.h"

typedef struct TfpContinuityTuning { float pop_m, pop_accel, ease_accel, min_ms, max_ms, max_m, teleport_m; } TfpContinuityTuning;
#define TFP_CONTINUITY_DEFAULT {.04f, 36.0f, 24.0f, 100.0f, 400.0f, 1.6f, 2.5f} /* max_m 1.0 and teleport_m 1.5 until 0.6.0-feel.39: a change of camera in a sprint is 1.2 m */
#define TFP_CONTINUITY_RUN_MS 70.0   /* jumps in a row are taken up for this long without a change of source */
#define TFP_CONTINUITY_NEAR .9f       /* of the measure: once in a run a frame from here on counts as a jump, if it goes the way of the jump before it */
#define TFP_CONTINUITY_STOP .2f       /* of the measure: a step under this (and a quarter of the step predicted) is the eye stopping, whichever way it points */
#define TFP_CONTINUITY_QUIET_MS 80.0f
#define TFP_CONTINUITY_SPEED_FRAMES 4u /* the frames looked at for the speed believed when a run's time is over */
#define TFP_CONTINUITY_CUT_M .25f      /* THE CUT: the head's move against the root in one frame, at least */
#define TFP_CONTINUITY_CUT_ACCEL 150.0f /* ... or this (m/s^2) times the frame's time squared, whichever is more */
#define TFP_CONTINUITY_CUT_REST 1.0f   /* ... from a head that went slower than this (m/s) against the root the frame before */
#define TFP_CONTINUITY_SETTLE_M .10f    /* THE SETTLE: a change of camera of at least this, where the caller allows it */
#define TFP_CONTINUITY_SETTLE_FRAMES 3u /* ... the new camera's eye steady against the root this many frames in a row, */
#define TFP_CONTINUITY_SETTLE_MS 350.0  /* ... or this long at most */

typedef struct TfpContinuity {
  bool known, active, moving; /* moving: the wanted eye's speed is known (a step has been seen since the start) */
  uint32_t source, run;
  bool neared;                           /* the present run has had its frame under the measure */
  bool lent; float left;                 /* the kept speed is the camera before's (THE LENT SPEED); how much of it the presented eye still has (1: all) */
  float coast[3];                        /* THE LET-DOWN AT A SLOWING: what the presented eye still has of a speed dropped, towards the wanted eye (m/s) */
  bool paused; float before[3];          /* last frame was followed as the eye stopping (THE STOP); the speed kept until then (THE HITCH) */
  bool calm, fresh;                      /* the last frame was followed; the kept speed was believed when a run's time was over or at once straight after a
                                          * frame taken up, and no frame has been followed since that went on within the measure of it or after another followed */
  bool trial; float held[3], held_loose[3]; /* last frame was followed as the eye going on after a stop; the speeds kept until then (THE TRIAL) */
  double tick, started, run_started; /* run_started: the first jump of the present run */
  float wanted[3], speed[3], shown[3];   /* last frame: the wanted eye, its smoothed speed (m/s), the presented eye */
  bool ago_known; float ago[3];          /* the wanted eye of the frame before the last, same camera (THE WAY BACK) */
  float from[3], from_speed[3], duration; /* the correction's curve: value and speed at `started`, length in ms */
  float recent[TFP_CONTINUITY_SPEED_FRAMES][3]; uint32_t recent_at; /* the last frames' steps as speeds (m/s), a ring */
  bool placed, coursed;                  /* last frame's root is known (tfp_continuity_step_in); so is its course */
  float root[3], basis[9];               /* last frame: the root in the world and its axis rows */
  float root_speed[3], root_along[3], spin[3]; /* the root's course: its speed in the world and along its own axes (m/s), its turning (rad/s, about its axes) */
  bool loose;                            /* loose_speed is known */
  float loose_speed[3];                  /* the wanted eye's speed against the root's place, along the world's axes (m/s): THE TURNING */
  bool stepped;                          /* last frame's steps are known: */
  float held_step[3], loose_step[3];     /* as speeds (m/s), in the root's frame and with the turning left out */
  float held_off, loose_off;             /* how much the step changed from frame to frame of late (m, over 100 ms), read each way */
  bool tried;                            /* last frame the root was off its course: what it did then, should it prove a new course */
  float tried_speed[3], tried_along[3], tried_spin[3];
  uint64_t jumps, edges, restarts;       /* taken up within a source; at a change of source; started again */
  uint64_t reseats;                      /* frames in which the root alone was re-seated (nothing taken up) */
  bool head_known, head_cut;             /* THE CUT: last frame's head is known; last frame was a cut */
  float head[3], head_speed;             /* last frame: the head in the root's frame, and its speed the frame before a cut (m/s; unknown: huge) */
  bool head_placed; float head_world[3]; /* last frame: the same head in the world, where the world was given */
  bool rest_end;                         /* the curve is a cut's: it ends with no acceleration either (tfp_continuity_curve) */
  uint64_t cuts;                         /* frames taken up as a cut */
  float largest;                         /* the largest correction carried since the reader last cleared it */
  bool traversal;                        /* round 24, set by the caller before each frame: a clamber's or a vault's (THE CLIMB) */
  bool settle_ok;                        /* round 24, set by the caller before each frame: THE SETTLE may begin or go on in it */
  bool settling; uint32_t settle_source, steady; double settle_since; float settle[3], settle_step[3], settle_from; /* THE SETTLE: the eye held, in the root's frame; the new camera's last step there; how far apart the two were when it began */
  uint64_t settles;                      /* settles begun */
} TfpContinuity;

static inline void tfp_continuity_reset(TfpContinuity *c) {
  const uint64_t jumps = c->jumps, edges = c->edges, restarts = c->restarts, reseats = c->reseats, cuts = c->cuts, settles = c->settles; const float largest = c->largest;
  memset(c, 0, sizeof *c); c->jumps = jumps; c->edges = edges; c->restarts = restarts; c->reseats = reseats; c->cuts = cuts; c->largest = largest; c->settles = settles;
}
static inline float tfp_continuity_length(const float *v) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
/* `v` (along the root's old axes) along its new ones: `turn` is new axes times old axes transposed. */
static inline void tfp_continuity_turn(const float *turn, float *v) {
  const float old[3] = {v[0], v[1], v[2]};
  for (unsigned a = 0; a < 3; ++a) v[a] = turn[a*3]*old[0]+turn[a*3+1]*old[1]+turn[a*3+2]*old[2];
}
/* The root from the eye in the world and in the root's frame (wanted = basis times (world - root)). */
static inline void tfp_continuity_root(const float *wanted, const float *world, const float *basis, float *root) {
  for (unsigned k = 0; k < 3; ++k) { root[k] = world[k]; for (unsigned a = 0; a < 3; ++a) root[k] -= wanted[a]*basis[a*3+k]; }
}
/* The root and its axis rows, kept for the next frame. NULL: not known. */
static inline void tfp_continuity_place(TfpContinuity *c, const float *wanted, const float *world, const float *basis) {
  c->placed = world != NULL;
  if (!world) { c->coursed = false; return; }
  tfp_continuity_root(wanted, world, basis, c->root);
  for (unsigned i = 0; i < 9; ++i) c->basis[i] = basis[i];
}
/* A turn (new axes times old axes transposed) as the angle about each axis, and back. Half a circle has no axis
 * here and counts as no turn; nothing turns that far in a frame on an even course. */
static inline void tfp_continuity_angle(const float *turn, float *angle) {
  const float s[3] = {(turn[7]-turn[5])/2, (turn[2]-turn[6])/2, (turn[3]-turn[1])/2};
  const float sine = tfp_continuity_length(s), cosine = (turn[0]+turn[4]+turn[8]-1)/2;
  const float scale = sine > 1e-6f ? atan2f(sine, cosine)/sine : 1;
  for (unsigned i = 0; i < 3; ++i) angle[i] = isfinite(scale) ? s[i]*scale : 0;
}
static inline void tfp_continuity_turned(const float *angle, float *turn) {
  const float size = tfp_continuity_length(angle);
  for (unsigned i = 0; i < 9; ++i) turn[i] = i%4 ? 0.0f : 1.0f;
  if (!(size > 1e-6f) || !isfinite(size)) return;
  const float x = angle[0]/size, y = angle[1]/size, z = angle[2]/size, sine = sinf(size), rest = 1-cosf(size);
  const float k[9] = {0, -z, y, z, 0, -x, -y, x, 0};
  for (unsigned a = 0; a < 3; ++a) for (unsigned b = 0; b < 3; ++b) {
    float squared = 0; for (unsigned m = 0; m < 3; ++m) squared += k[a*3+m]*k[m*3+b];
    turn[a*3+b] += sine*k[a*3+b]+rest*squared;
  }
}
/* The root's part of this frame's eye (in the root's frame) for one course: `spin`, and `speed` in the world or
 * `along` the root's axes. `even`, `root`: the axis rows and the place of the root on that course. Returns the part's
 * length. */
static inline float tfp_continuity_part(const TfpContinuity *c, const float *spin, const float *speed, bool along, const float *wanted,
                                        const float *world, float dt, float *even, float *root, float *part) {
  const float angle[3] = {spin[0]*dt, spin[1]*dt, spin[2]*dt};
  float turn[9];
  tfp_continuity_turned(angle, turn);
  for (unsigned i = 0; i < 9; ++i) even[i] = 0;
  for (unsigned a = 0; a < 3; ++a) for (unsigned b = 0; b < 3; ++b) for (unsigned k = 0; k < 3; ++k) even[a*3+k] += turn[a*3+b]*c->basis[b*3+k];
  for (unsigned k = 0; k < 3; ++k) { root[k] = c->root[k];
    if (along) for (unsigned a = 0; a < 3; ++a) root[k] += speed[a]*dt*even[a*3+k]; else root[k] += speed[k]*dt; }
  for (unsigned a = 0; a < 3; ++a) { part[a] = wanted[a]; for (unsigned k = 0; k < 3; ++k) part[a] -= (world[k]-root[k])*even[a*3+k]; }
  return tfp_continuity_length(part);
}
/* The correction and its speed at `tick`. */
static inline void tfp_continuity_curve(const TfpContinuity *c, double tick, float *value, float *speed) {
  for (unsigned i = 0; i < 3; ++i) value[i] = speed[i] = 0;
  if (!c->active || !(c->duration > 0)) return;
  const double s = (tick-c->started)/c->duration;
  if (!(s >= 0) || s >= 1) return;
  const double seconds = c->duration/1000.0;
  /* A cut's curve (rest_end) is the quartic that ends with no acceleration either, (1-s)^3 (d (1+3s) + v T s): the
   * cubic brakes hardest in its last frame and then stops (round 19 review: soak 30 16:37:35.009, 300 ms after a cut,
   * the step change went 0.009 -> 0.029 m where the cubic ended). The quartic starts hardest instead (12 d / T^2), in
   * the frames right after the cut, where the move out of the emote starts too, brakes at 4 d / T^2 at most two thirds
   * in, and comes to rest; its length is reckoned for its start (tfp_continuity_step_cut). Over the ten cuts of soaks
   * 27 to 30, replayed against main: the most a frame's step change is beyond main's in the 15 frames after a cut, 0.020
   * m with the cubic, 0.008 m with this; summed, 0.260 and 0.141 m. (Tried: the quintic, at rest at both ends, on the
   * cubic's length, 0.021 and 0.429 m: it brakes in the middle as hard as the cubic at its end; this quartic on the
   * cubic's length, 0.017 and 0.275 m.) */
  const double u = 1-s;
  const double h00 = c->rest_end ? u*u*u*(1+3*s) : 2*s*s*s-3*s*s+1, h10 = c->rest_end ? s*u*u*u : s*s*s-2*s*s+s;
  const double d00 = (c->rest_end ? -12*s*u*u : 6*s*s-6*s)/seconds, d10 = c->rest_end ? u*u*(1-4*s) : 3*s*s-4*s+1;
  for (unsigned i = 0; i < 3; ++i) {
    value[i] = (float)(c->from[i]*h00+c->from_speed[i]*seconds*h10);
    speed[i] = (float)(c->from[i]*d00+c->from_speed[i]*d10);
  }
}
static inline void tfp_continuity_begin(TfpContinuity *c, const float *value, const float *speed, double tick, float duration_ms) {
  for (unsigned i = 0; i < 3; ++i) { c->from[i] = value[i]; c->from_speed[i] = speed[i]; }
  c->started = tick; c->duration = duration_ms; c->active = true;
}
/* The speed believed along one axis when a run's time is over, from the last `count` frames' speeds (the run's and
 * the present one).
 *  - The frames that count are the newest and those before it for as long as none goes the other way from the one
 *    after it. An eye that flickers between two places goes back in every frame: the newest stands alone, and one
 *    frame's step is no speed (0). With two or three flips in the run the middle of all its frames was itself a flip
 *    (35 to 70 ms a frame: 0.16 m in a frame while the wanted eye stood).
 *  - Of two frames that agree, the slower; of three or four, the middle. A rig re-seated in the frame a motion
 *    begins is one frame far off and the rest alike: the rest is the speed. (The run's way divided by its time
 *    would take the re-seat for speed, and gives 2.7 m/s for an odd number of flips.)
 *  - Frames that are each slower than the one before are a motion dying down, and its speed is the newest frame's.
 *    The middle is then too much, and too much is the side that shows: the next frame is shorter than believed and
 *    the same way, so it is followed as the motion slowing down, and the difference is in the presented eye at once
 *    (seen in the soak of 2026-11-22 on 0.6.0-feel.40: a head thrown in a dive, 0.26, 0.21, 0.136 and 0.083 m a
 *    frame, 0.175 m believed, 0.053 m in the next frame, and 0.077 m of step change for the wanted eye's 0.032 m).
 *  Believing too little is the safe side: the next frame is then a jump again and the next run settles it. */
static inline float tfp_continuity_believed(const TfpContinuity *c, unsigned axis, unsigned count) {
  float sorted[TFP_CONTINUITY_SPEED_FRAMES] = {0}; unsigned agree = 0; float after = 0, newest = 0; bool dying = true;
  if (count > TFP_CONTINUITY_SPEED_FRAMES) count = TFP_CONTINUITY_SPEED_FRAMES;
  for (unsigned i = 0; i < count; ++i) {
    const float v = c->recent[(c->recent_at+TFP_CONTINUITY_SPEED_FRAMES-1-i)%TFP_CONTINUITY_SPEED_FRAMES][axis];
    if (i && v*after < 0) break;
    if (!i) newest = v; else if (!(fabsf(v) > fabsf(after))) dying = false;
    unsigned at = i; while (at > 0 && sorted[at-1] > v) { sorted[at] = sorted[at-1]; --at; }
    sorted[at] = v; after = v; agree = i+1;
  }
  if (agree < 2) return 0;
  if (dying) return newest;
  if (agree == 2) return fabsf(sorted[0]) < fabsf(sorted[1]) ? sorted[0] : sorted[1];
  return agree & 1 ? sorted[agree/2] : (sorted[agree/2-1]+sorted[agree/2])/2;
}
/* THE LET-DOWN AT A SLOWING: how far the presented eye goes on in this frame on what it still has (c->coast):
 * pop_accel less than in the frame before, and no more than leaves it room to come to rest at the wanted eye at that
 * rate (n frames more). `value`, `speed`: what is carried against this frame's wanted eye and the curve's speed;
 * `curved`, `left_ms`: a curve is running, and for how long yet. The room is the way to the wanted eye less what the
 * curve needs for the speed it has: a cubic from `d` closing at `v` with `T` to go passes its end when d is under
 * v T / 3. */
static inline void tfp_continuity_glide(TfpContinuity *c, const float *value, const float *speed, bool curved, float left_ms, bool quiet, float dt,
                                        const TfpContinuityTuning *t, float *glide) {
  const float fast = tfp_continuity_length(c->coast), down = t->pop_accel*dt*dt; float room = 0, closing = 0, most = 0;
  if (fast > 0 && isfinite(fast) && !quiet && down > 0) {
    for (unsigned i = 0; i < 3; ++i) { room -= value[i]*c->coast[i]/fast; closing += speed[i]*c->coast[i]/fast; }
    if (curved && closing > 0) room -= closing*left_ms/3000;
    if (room > 0) { const float n = floorf((sqrtf(1+8*room/down)-1)/2); most = (room+down*n*(n+1)/2)/(n+1); }
  }
  const float go = fminf(fast*dt-down, most);
  for (unsigned i = 0; i < 3; ++i) { glide[i] = go > 0 ? c->coast[i]/fast*go : 0; c->coast[i] = go > 0 ? glide[i]/dt : 0; }
}
/* THE CUT: the head in the world, from the head in the root's frame and the eye in both (wanted = basis times
 * (world - root)); false where the world is not given. */
static inline bool tfp_continuity_head_world(const float *head, const float *wanted, const float *world, const float *basis, float *out) {
  if (!head || !world || !basis) return false;
  for (unsigned k = 0; k < 3; ++k) { out[k] = world[k]; for (unsigned a = 0; a < 3; ++a) out[k] += (head[a]-wanted[a])*basis[a*3+k]; }
  return tfp_finite3(out);
}
/* THE CUT: the head kept for the next frame. `speed`: its speed against the root that a cut is asked against (m/s). */
static inline void tfp_continuity_head(TfpContinuity *c, const float *head, const float *wanted, const float *world, const float *basis, float speed, bool cut) {
  c->head_known = head && tfp_finite3(head); c->head_cut = cut;
  c->head_speed = c->head_known ? speed : 1e9f;
  for (unsigned i = 0; i < 3; ++i) c->head[i] = c->head_known ? head[i] : 0;
  c->head_placed = c->head_known && tfp_continuity_head_world(head, wanted, world, basis, c->head_world);
}
/* One presented frame. `wanted`: the eye the helper would present, in the body's frame. `world`, `basis`: the same
 * eye in the world and the body root's axis rows (wanted = basis times (world - root)), or both NULL where the
 * world is not known: the root is then taken to move evenly and to carry the eye round with it. `source`: which camera
 * gave it. `detect`: jumps within a source are looked for (the native view); false: only a change of source is
 * taken up (the head camera, whose eye turns about the head with the view). Returns the correction's length;
 * `out` is the eye to present. */
/* tfp_continuity_step_in with THE CUT: `head`, the head node the head camera's eye is built on (as the head camera's
 * own smoothing leaves it, before the eye damping), in the root's frame (as `wanted`), given only in a head-camera
 * frame of a body on foot; NULL anywhere else (nothing is a cut there). Not the eye: it sits 0.18 m ahead of the head
 * along the level view and swings round it with every turn of the view (a flick of 95 degrees in one frame moved it
 * 0.27 m against a root that does not turn with the view, and was taken for a cut: round 19 review). */
static inline float tfp_continuity_step_cut(TfpContinuity *c, const float *wanted, const float *world, const float *basis, const float *head,
                                            uint32_t source, bool detect, bool quiet, double tick, uint32_t max_gap_ms, const TfpContinuityTuning *t,
                                            float *out) {
  for (unsigned i = 0; i < 3; ++i) out[i] = wanted[i];
  if (!world || !basis || !tfp_finite3(world)) world = basis = NULL;
  if (!tfp_finite3(wanted) || !isfinite(tick)) { if (c->known) ++c->restarts; tfp_continuity_reset(c); return 0; }
  if (!c->known || tick < c->tick || tick-c->tick > max_gap_ms) {
    if (c->known) ++c->restarts;
    tfp_continuity_reset(c);
    c->known = true; c->tick = tick; c->source = source;
    for (unsigned i = 0; i < 3; ++i) c->wanted[i] = c->shown[i] = wanted[i];
    tfp_continuity_place(c, wanted, world, basis);
    tfp_continuity_head(c, head, wanted, world, basis, 1e9f, false); /* its speed not known: no cut in the next frame */
    return 0;
  }
  float value[3], speed[3];
  tfp_continuity_curve(c, tick, value, speed);
  const float dt = (float)(tick-c->tick)/1000;
  if (!(dt > 0)) { /* the same instant again: the same answer */
    for (unsigned i = 0; i < 3; ++i) out[i] = wanted[i]+value[i];
    return tfp_continuity_length(value);
  }
  float step[3], expected[3], difference[3];
  /* THE CUT: the head given in this frame and the last, of the same camera. */
  bool cut = false; float head_speed = 1e9f;
  if (head && tfp_finite3(head) && c->head_known && source == c->source) {
    float moved[3]; for (unsigned i = 0; i < 3; ++i) moved[i] = head[i]-c->head[i];
    const float far = tfp_continuity_length(moved), bound = fmaxf(TFP_CONTINUITY_CUT_M, TFP_CONTINUITY_CUT_ACCEL*dt*dt);
    head_speed = far/dt;
    /* The head jumped in the world too, as far: a root moving by itself (the game re-seating it, the head and the eye
     * still) moves the head against the root alone, and that is the re-seat's to read, not a cut (round 19 review: the
     * root alone 0.5 m was a cut, the eye 0.500 m off). The ten cuts seen move the head 0.33 to 0.58 m in the world. */
    float there[3], went[3]; bool jumped = false;
    if (c->head_placed && tfp_continuity_head_world(head, wanted, world, basis, there)) {
      for (unsigned i = 0; i < 3; ++i) went[i] = there[i]-c->head_world[i];
      jumped = tfp_continuity_length(went) >= bound;
    }
    cut = !detect && !quiet && c->head_speed < TFP_CONTINUITY_CUT_REST && far >= bound && jumped;
  }
  /* a cut keeps the speed from before it for the frame after (a cut in two frames), only once */
  const float head_kept = cut && !c->head_cut ? c->head_speed : head_speed;
  const bool edge = source != c->source || cut;
  /* THE WAY BACK: a move over the measure that ends within a quarter of its own length of where the eye was two
   * frames ago, in a frame in which jumps are not looked for. */
  float back_by[3], went_by[3]; for (unsigned i = 0; i < 3; ++i) { back_by[i] = wanted[i]-c->ago[i]; went_by[i] = wanted[i]-c->wanted[i]; }
  const float went_far = tfp_continuity_length(went_by);
  const bool back = !detect && !edge && c->ago_known && went_far > fmaxf(t->pop_m, t->pop_accel*dt*dt) && tfp_continuity_length(back_by) <= went_far/4;
  for (unsigned i = 0; i < 3; ++i) { step[i] = wanted[i]-c->wanted[i];
    /* The first step after a start gives the speed; it is not measured against a speed of nothing. */
    expected[i] = c->moving || edge ? c->speed[i]*dt : step[i]; difference[i] = step[i]-expected[i]; }
  float size = tfp_continuity_length(difference);
  const float measure = fmaxf(t->pop_m, t->pop_accel*dt*dt);
  /* The root's part of the step (see THE COMPARISON above), in the root's present axes. */
  float root[3] = {0}, moved[3] = {0}, turn[9] = {0}, part_size = 0, swing_size = 0, even_root[3] = {0};
  const bool placed = world && c->placed; bool reseated = false;
  if (placed) {
    tfp_continuity_root(wanted, world, basis, root); memcpy(even_root, root, sizeof root);
    for (unsigned a = 0; a < 3; ++a) for (unsigned b = 0; b < 3; ++b) for (unsigned k = 0; k < 3; ++k) turn[a*3+b] += basis[a*3+k]*c->basis[b*3+k];
    for (unsigned a = 0; a < 3; ++a) for (unsigned k = 0; k < 3; ++k) moved[a] += (root[k]-c->root[k])*basis[a*3+k];
  }
  float rest[3] = {0}, off[9] = {0}, rest_size = size; bool reseat = false, kept = false; /* kept: the course is the one tried last frame */
  if (placed && c->coursed) {
    /* The course that fits this frame best. One that the root took up only last frame, in a frame it was off its
     * course, counts once it is kept: that was a new speed (a throw), not a re-seat, and it is the course from here. */
    float even[9] = {0}, part[3] = {0}, at[3] = {0}, swing[3]; unsigned best = 0;
    for (unsigned course = 0; course < (c->tried ? 4u : 2u); ++course) {
      float axes[9], its[3], place[3];
      const float length = tfp_continuity_part(c, course < 2 ? c->spin : c->tried_spin, course == 0 ? c->root_speed : course == 1 ? c->root_along
                                               : course == 2 ? c->tried_speed : c->tried_along, course & 1, wanted, world, dt, axes, place, its);
      if (course && !(length < part_size)) continue;
      part_size = length; best = course; memcpy(even, axes, sizeof even); memcpy(part, its, sizeof part); memcpy(at, place, sizeof at);
    }
    /* how far the root's axes alone are off their course, seen at the eye (the root where it is) */
    for (unsigned a = 0; a < 3; ++a) { swing[a] = wanted[a]; for (unsigned k = 0; k < 3; ++k) swing[a] -= (world[k]-root[k])*even[a*3+k]; }
    swing_size = tfp_continuity_length(swing);
    kept = best >= 2;
    if (best >= 2) for (unsigned i = 0; i < 3; ++i) { c->root_speed[i] = c->tried_speed[i]; c->root_along[i] = c->tried_along[i]; c->spin[i] = c->tried_spin[i]; }
    for (unsigned i = 0; i < 3; ++i) rest[i] = difference[i]-part[i];
    rest_size = tfp_continuity_length(rest);
    /* The root re-seated: the step is measured against the root on its course. */
    reseat = !edge && c->moving && isfinite(rest_size) && size > measure && part_size > measure && rest_size < size;
    if (reseat) { memcpy(even_root, at, sizeof at);
      for (unsigned a = 0; a < 3; ++a) for (unsigned b = 0; b < 3; ++b) for (unsigned k = 0; k < 3; ++k) off[a*3+b] += basis[a*3+k]*even[b*3+k]; }
    else rest_size = size;
  }
  /* The same step with the root's turning left out (THE TURNING above): against the root's place (on its course, if
   * it was re-seated), along the world's axes, here turned into the root's present ones. */
  float loose[3] = {0}, free_step[3] = {0}, held_step[3], loose_size = rest_size; bool freely = false, onset = false;
  for (unsigned i = 0; i < 3; ++i) held_step[i] = reseat ? c->speed[i]*dt+rest[i] : step[i]; /* the step in the root's frame, a re-seated root on its course */
  if (placed) {
    for (unsigned k = 0; k < 3; ++k) { free_step[k] = world[k]-even_root[k]; for (unsigned a = 0; a < 3; ++a) free_step[k] -= c->wanted[a]*c->basis[a*3+k]; }
    if (c->loose && (c->moving || edge)) {
      for (unsigned a = 0; a < 3; ++a) for (unsigned k = 0; k < 3; ++k) loose[a] += (free_step[k]-c->loose_speed[k]*dt)*basis[a*3+k];
      loose_size = tfp_continuity_length(loose);
      /* The frame is read without the turning when the axes left their course in it and the step is a jump either
       * way (or a change of camera), or when the eye has not gone round with the root of late. How the step changed
       * from the last frame's, each way, says which: the wanted eye's steps as they are, not measured against a speed
       * the filter kept (a run of jumps keeps one, and in a body thrown while it turns the two kept speeds part by
       * themselves). */
      const bool swung = !reseat && swing_size > measure, both = rest_size > measure && loose_size > measure;
      const bool rides = c->loose_off-c->held_off > measure/2; /* as it stood before this frame */
      if (!edge && c->stepped) {
        float held[3], free[3]; const float fade = 1-expf(-dt/.1f);
        for (unsigned i = 0; i < 3; ++i) { held[i] = held_step[i]-c->held_step[i]*dt; free[i] = free_step[i]-c->loose_step[i]*dt; }
        const float held_size = tfp_continuity_length(held), free_size = tfp_continuity_length(free);
        if (isfinite(held_size) && isfinite(free_size)) { c->held_off += (held_size-c->held_off)*fade; c->loose_off += (free_size-c->loose_off)*fade; }
      }
      const bool apart = !edge && rest_size > measure && c->held_off-c->loose_off > measure/2;
      /* THE ONSET: the root keeps the course it took up alone last frame, and the jump is that course's turning seen
       * at an eye that does not go round with it. The frame is followed in the root's frame. */
      float spun[3] = {c->wanted[0], c->wanted[1], c->wanted[2]}; /* how far the axes' turn of this frame moves the eye in the root's frame */
      tfp_continuity_turn(turn, spun);
      for (unsigned i = 0; i < 3; ++i) spun[i] -= c->wanted[i];
      onset = kept && !edge && rest_size > measure && !(loose_size > measure) && rest_size-loose_size > measure/2 && !rides
              && tfp_continuity_length(spun) > measure/2 && swing_size < measure/2;
      freely = !onset && isfinite(loose_size) && (apart || (swung && (edge || both)));
      if (freely || swung || onset) { /* what is carried does not go round with the axes */
        tfp_continuity_turn(turn, c->from); tfp_continuity_turn(turn, c->from_speed); tfp_continuity_turn(turn, c->coast);
        tfp_continuity_curve(c, tick, value, speed);
      }
      if (freely) { /* nor does the eye's speed: it keeps its direction in the world */
        tfp_continuity_turn(turn, c->speed); tfp_continuity_turn(turn, c->before); tfp_continuity_turn(turn, c->held);
        for (unsigned i = 0; i < TFP_CONTINUITY_SPEED_FRAMES; ++i) tfp_continuity_turn(turn, c->recent[i]);
        for (unsigned i = 0; i < 3; ++i) { expected[i] = c->speed[i]*dt; difference[i] = loose[i]; step[i] = expected[i]+loose[i]; }
        size = loose_size;
      }
    }
  }
  if (placed) { c->stepped = !edge;
    for (unsigned i = 0; i < 3; ++i) { c->held_step[i] = held_step[i]/dt; c->loose_step[i] = free_step[i]/dt; } }
  if (reseat) { ++c->reseats; reseated = true; }
  if (reseat && !freely) {
    /* What is carried, and the eye's speed, keep their direction in the world: they are turned by as much as the
     * root's axes are off their even course. */
    tfp_continuity_turn(off, c->from); tfp_continuity_turn(off, c->from_speed); tfp_continuity_turn(off, c->coast); tfp_continuity_turn(off, c->speed); tfp_continuity_turn(off, c->before);
    tfp_continuity_turn(off, c->held);
    for (unsigned i = 0; i < TFP_CONTINUITY_SPEED_FRAMES; ++i) tfp_continuity_turn(off, c->recent[i]);
    tfp_continuity_curve(c, tick, value, speed);
    for (unsigned i = 0; i < 3; ++i) { expected[i] = c->speed[i]*dt; difference[i] = rest[i]; step[i] = expected[i]+rest[i]; }
    size = rest_size;
  }
  if (!isfinite(size) || size > t->teleport_m) { /* a real teleport: nothing is carried across it */
    ++c->restarts; tfp_continuity_reset(c);
    c->known = true; c->tick = tick; c->source = source;
    for (unsigned i = 0; i < 3; ++i) c->wanted[i] = c->shown[i] = wanted[i];
    tfp_continuity_place(c, wanted, world, basis);
    tfp_continuity_head(c, head, wanted, world, basis, 1e9f, false);
    return 0;
  }
  /* THE TRIAL: the frame before went on after a stop and its speed was believed. A frame that does not go on within
   * the measure of it, and is no slowing down either, says that speed never was: the kept speed is what it was before
   * that frame, and a frame that goes back, no further than a measure past where the eye stood, is the game's own. */
  bool returned = false, denied = false;
  if (c->trial && !edge) {
    const float went = tfp_continuity_length(step), meant = tfp_continuity_length(expected);
    const float way = step[0]*expected[0]+step[1]*expected[1]+step[2]*expected[2];
    const bool stop = !c->run && !c->lent && went < TFP_CONTINUITY_STOP*measure && went < meant/4;
    if (size > measure && !((way >= 0 || stop) && went < meant) && way < meant*meant) {
      denied = true; returned = way < 0 && went < meant+measure;
      for (unsigned i = 0; i < 3; ++i) { difference[i] += (c->speed[i]-c->held[i])*dt; expected[i] = c->held[i]*dt; c->speed[i] = c->held[i]; }
      if (placed && c->loose) for (unsigned i = 0; i < 3; ++i) c->loose_speed[i] = c->held_loose[i];
      size = tfp_continuity_length(difference);
    }
  }
  float last[3], alike = 0; /* the frame before, off the kept speed (m/s), and how far this frame goes its way */
  for (unsigned i = 0; i < 3; ++i) { last[i] = c->recent[(c->recent_at+TFP_CONTINUITY_SPEED_FRAMES-1)%TFP_CONTINUITY_SPEED_FRAMES][i]-c->speed[i]; alike += last[i]*difference[i]; }
  for (unsigned i = 0; i < 3; ++i) c->recent[c->recent_at%TFP_CONTINUITY_SPEED_FRAMES][i] = step[i]/dt;
  c->recent_at = (c->recent_at+1)%TFP_CONTINUITY_SPEED_FRAMES;
  const float limit = measure;
  const float along = step[0]*expected[0]+step[1]*expected[1]+step[2]*expected[2];
  const float gone = tfp_continuity_length(step), meant = tfp_continuity_length(expected);
  /* THE STOP: a step of next to nothing, whichever way it points, against the camera's own speed and not in a run. */
  const bool stopped = !c->run && !c->lent && gone < TFP_CONTINUITY_STOP*measure && gone < meant/4;
  const bool slowing = (along >= 0 || stopped) && gone < meant;
  const bool over = size > limit;
  /* THE HITCH: the frame after a stop goes on as the eye went before it. */
  float apart[3]; for (unsigned i = 0; i < 3; ++i) apart[i] = step[i]-c->before[i]*dt;
  const bool resumed = c->paused && !edge && over && tfp_continuity_length(apart) < limit;
  const bool within = !c->run || (tick >= c->run_started && tick-c->run_started <= TFP_CONTINUITY_RUN_MS);
  /* A run's last frame a hair under the measure: once in a run, within its time, a frame that goes the way the jump
   * before it went counts as a jump from TFP_CONTINUITY_NEAR of the measure. */
  const bool near = c->run && within && !c->neared && size > TFP_CONTINUITY_NEAR*limit && alike > .7f*size*tfp_continuity_length(last);
  const bool jump = !quiet && !edge && detect && (over || near) && !slowing && !onset && !resumed && !returned;
  const bool believe = jump && !within; /* jumps for longer than the run: a new speed, not a re-seated rig */
  const bool take = !quiet && size > 1e-4f && (edge || jump);
  /* THE LET-DOWN AT A SLOWING: the frame before was taken up and this one is followed as slowing down. What the
   * presented eye still had of the kept speed, less this frame's own, as far as it points at the wanted eye. */
  if (!take && !quiet && !c->calm && over && slowing && !stopped) {
    const float share = c->lent ? c->left : 1, far = tfp_continuity_length(value); float on = 0;
    for (unsigned i = 0; i < 3; ++i) on -= far > 1e-6f ? (share*c->speed[i]-step[i]/dt)*value[i]/far : 0;
    if (isfinite(on) && on*dt > measure) for (unsigned i = 0; i < 3; ++i) c->coast[i] = -on*value[i]/far;
  }
  float glide[3] = {0, 0, 0};
  if (take) {
    float next[3], from_speed[3], rate = 0;
    /* THE LENT SPEED: the presented eye is let down from it, as far as the speed whose step in a frame is the measure. */
    if (edge) { c->lent = true; c->left = 1; }
    if (c->lent) {
      const float fast = tfp_continuity_length(c->speed)*c->left, slower = fmaxf(fast-t->pop_accel*dt, fminf(fast, measure/dt));
      if (fast > 0 && isfinite(fast)) c->left *= slower/fast;
    } else c->left = 1;
    for (unsigned i = 0; i < 3; ++i) { next[i] = value[i]-difference[i]-(1-c->left)*expected[i]; from_speed[i] = speed[i]; }
    float length = tfp_continuity_length(next);
    if (length > t->max_m) { for (unsigned i = 0; i < 3; ++i) next[i] *= t->max_m/length; length = t->max_m; }
    if (believe) {
      /* This frame is still taken up, so the presented eye goes on at the old speed; the wanted eye's speed is
       * now the new one, and the difference of the two goes into the correction's curve. The new one is what the
       * run's frames and this one agree on (tfp_continuity_believed): a frame that is itself a jump does not pass
       * for a speed. */
      float fresh[3]; for (unsigned i = 0; i < 3; ++i) fresh[i] = tfp_continuity_believed(c, i, c->run+1);
      const float fast = tfp_continuity_length(fresh);
      if (fast > 8) for (unsigned i = 0; i < 3; ++i) fresh[i] *= 8/fast;
      if (placed) for (unsigned k = 0; k < 3; ++k) for (unsigned a = 0; a < 3; ++a) c->loose_speed[k] += (fresh[a]-c->speed[a])*basis[a*3+k];
      /* THE CLIMB: not in a clamber's or a vault's frame. There the run is the body's own lunge read against the root
       * (a head that crouches onto a ledge while the root rises), and its speed handed on kept the presented eye
       * rising after the head had stopped: 0.646 m carried, the eye 0.6 m above the head. The presented eye takes the
       * wanted eye's speed in this frame instead. */
      for (unsigned i = 0; i < 3; ++i) { if (!c->traversal) from_speed[i] += c->left*c->speed[i]-fresh[i]; c->speed[i] = fresh[i]; }
      c->lent = false; /* the new camera's speed is known */
      rate = tfp_continuity_length(from_speed);
    }
    /* The curve's length: its largest acceleration is ease_accel where max_ms allows (6 d / T^2 + 4 v / T). */
    const float a = fmaxf(t->ease_accel, 1e-3f);
    float duration = 1000*(4*rate+sqrtf(16*rate*rate+24*a*length))/(2*a);
    duration = fminf(fmaxf(duration, t->min_ms), t->max_ms);
    /* A cut's curve, the quartic (tfp_continuity_curve), starts at 12 d / T^2 + 6 v / T: as long again by the square
     * root of 2 for the same largest acceleration, and up to max_ms as long again by as much. */
    if (cut) duration = fminf(fmaxf(1000*(6*rate+sqrtf(36*rate*rate+48*a*length))/(2*a), t->min_ms), t->max_ms*1.41421356f);
    /* THE LET-DOWN AT A SLOWING, in a frame taken up: measured anew, against the wanted eye where the jump put it. */
    tfp_continuity_glide(c, next, from_speed, true, duration, quiet, dt, t, glide);
    for (unsigned i = 0; i < 3; ++i) next[i] += glide[i];
    tfp_continuity_begin(c, next, from_speed, tick, duration);
    c->rest_end = cut; /* kept when the curve is begun again from where it is (the glide, full aim) */
    for (unsigned i = 0; i < 3; ++i) value[i] = next[i];
    if (cut) { ++c->cuts; c->run = 0; } else if (edge) { ++c->edges; c->run = 0; } else if (believe) c->run = 0; else { ++c->jumps; if (!c->run++) c->run_started = tick; }
    c->neared = c->run && (c->neared || !over); c->paused = c->trial = c->calm = false;
    if (believe) c->fresh = true; /* believed in this frame: no frame has gone on at it yet */
    /* otherwise the wanted eye's speed is kept: after a jump it goes on as it went */
  } else {
    tfp_continuity_glide(c, value, speed, c->active, (float)(c->started+c->duration-tick), quiet, dt, t, glide);
    if (tfp_continuity_length(glide) > 0 && c->active) { /* the curve as it was, from where the eye now is: its end is not put off */
      for (unsigned i = 0; i < 3; ++i) value[i] += glide[i];
      tfp_continuity_begin(c, value, speed, tick, (float)(c->started+c->duration-tick));
    }
    c->run = 0; c->neared = false; c->lent = false;
    /* the kept speed is dropped whole in this frame: what it was is kept for one frame more, unless it was believed
     * only the frame before, out of a run of jumps or straight after a frame taken up (the eye was not going steadily) */
    c->paused = stopped && over && !c->fresh;
    if (c->paused) for (unsigned i = 0; i < 3; ++i) c->before[i] = c->speed[i];
    c->fresh = over && !c->calm; c->calm = true;
    c->trial = resumed; /* its speed is believed at once and is on trial for one frame: what was kept is kept beside it */
    if (resumed) for (unsigned i = 0; i < 3; ++i) { c->held[i] = c->speed[i]; c->held_loose[i] = c->loose_speed[i]; }
    /* a new speed is believed at once, an ordinary frame is averaged in, a step back after a trial is no speed */
    const float k = returned ? 0 : over || !c->moving ? 1 : 1-expf(-dt/.05f);
    for (unsigned i = 0; i < 3; ++i) c->speed[i] += (step[i]/dt-c->speed[i])*k;
    const float fast = tfp_continuity_length(c->speed);
    if (fast > 8) for (unsigned i = 0; i < 3; ++i) c->speed[i] *= 8/fast;
    /* THE WAY BACK: the eye went and came back, which is no speed. Kept whole (as it was until 0.6.0-feel.46), a
     * flip of 0.2 m a frame was 6 m/s, and the first frame looked at afterwards sent the presented eye on at it. */
    if (back) for (unsigned i = 0; i < 3; ++i) c->speed[i] = 0;
    if (quiet && c->active && c->started+c->duration-tick > TFP_CONTINUITY_QUIET_MS) tfp_continuity_begin(c, value, speed, tick, TFP_CONTINUITY_QUIET_MS);
    if (placed) { /* the speed with the turning left out, kept and averaged as the other is */
      const float kept = returned ? 0 : loose_size > limit || denied || !c->loose ? 1 : 1-expf(-dt/.05f);
      for (unsigned i = 0; i < 3; ++i) c->loose_speed[i] += (free_step[i]/dt-c->loose_speed[i])*kept;
      const float quick = tfp_continuity_length(c->loose_speed);
      if (quick > 8) for (unsigned i = 0; i < 3; ++i) c->loose_speed[i] *= 8/quick;
      if (back) for (unsigned i = 0; i < 3; ++i) c->loose_speed[i] = 0;
      c->loose = isfinite(quick);
    }
  }
  if (!placed) { c->loose = c->stepped = false; c->held_off = c->loose_off = 0; }
  if (placed) {
    /* The root's course. A root that is off it by more than the measure without the eye jumping against it has a
     * new speed (the body thrown, a dive), believed at once. A root re-seated keeps the course it had; what it did
     * in this frame is kept beside it, for the next frame to say whether it was a new course after all. A root in a
     * frame of THE CUT is neither: it is part of the pose cut (a dive-back's yaw flips 157 degrees in it), and the
     * course is kept as it was, nothing tried. Believed or tried, that turn was the course of the frame after (an eye
     * above the root hardly moves with a turn about the root's up axis, so the tried course fitted the frame best), the
     * root read as re-seated off it and what was carried was swung round by as much again: soak 29 on feel.51,
     * 16:16:30.838, 0.376 m of step change for the wanted eye's 0.034 m. */
    if (cut) c->tried = false;
    else {
      float along[3], spin[3];
      tfp_continuity_angle(turn, spin);
      for (unsigned i = 0; i < 3; ++i) { along[i] = moved[i]/dt; spin[i] /= dt; }
      float *const speed_to = reseated ? c->tried_speed : c->root_speed, *const along_to = reseated ? c->tried_along : c->root_along;
      const float k = reseated || part_size > limit || !c->coursed ? 1 : 1-expf(-dt/.05f);
      for (unsigned i = 0; i < 3; ++i) { speed_to[i] += ((root[i]-c->root[i])/dt-speed_to[i])*k; along_to[i] += (along[i]-along_to[i])*k;
        (reseated ? c->tried_spin : c->spin)[i] = spin[i]; }
      c->tried = reseated; c->coursed = true;
    }
  }
  /* THE SETTLE (round 24): see the header. Over what the frame did above: every decision there is made as before and
   * the state goes on (the eye's speed, the root's course); only the curve is put where the held eye is. */
  if (c->settling || (edge && take && !cut && c->settle_ok && !quiet && tfp_continuity_length(value) >= TFP_CONTINUITY_SETTLE_M)) {
    const float zero[3] = {0, 0, 0}; float held[3];
    if (!c->settling) { /* begun: the eye as this change of camera presents it, from here on fixed against the root */
      c->settling = true; c->settle_source = source; c->steady = 0; c->settle_since = tick; ++c->settles;
      for (unsigned i = 0; i < 3; ++i) { c->settle[i] = wanted[i]+value[i]; c->settle_step[i] = 0; }
      c->settle_from = tfp_continuity_length(value);
    } else { /* steady: its step against the root is no jump against the step before it (it goes on evenly, or stands) */
      float moved[3], change[3];
      for (unsigned i = 0; i < 3; ++i) { moved[i] = wanted[i]-c->wanted[i]; change[i] = moved[i]-c->settle_step[i]; c->settle_step[i] = moved[i]; }
      c->steady = tfp_continuity_length(change) < measure ? c->steady+1 : 0;
    }
    for (unsigned i = 0; i < 3; ++i) held[i] = c->settle[i]-wanted[i];
    /* the frame's own answer stands: another camera, full aim, not allowed, or the new camera's eye gone away from the
     * held one by more than TFP_CONTINUITY_SETTLE_M (no re-seat: the body's own motion; soak 14, 04:02:12.999, the rig
     * 0.9 m away within 250 ms of the entry; ended with an ease from rest there, its first step was 0.196 m) */
    if (!c->settle_ok || source != c->settle_source || quiet || !tfp_finite3(held) || tfp_continuity_length(held) > c->settle_from+TFP_CONTINUITY_SETTLE_M)
      c->settling = false;
    else {
      const bool over = c->steady >= TFP_CONTINUITY_SETTLE_FRAMES || tick-c->settle_since >= TFP_CONTINUITY_SETTLE_MS;
      float duration = t->max_ms, from_speed[3] = {0, 0, 0};
      if (over) { /* one ease from the held eye to the settled one: the held eye at rest against the root, so the curve
                   * starts against the new camera's own even motion and gathers it (at 50 ms a frame, from rest the
                   * curve's start was a step change of that motion, 0.074 m for 0.046 m) */
        for (unsigned i = 0; i < 3; ++i) from_speed[i] = dt > 0 ? -c->settle_step[i]/dt : 0;
        const float a = fmaxf(t->ease_accel, 1e-3f), rate = tfp_continuity_length(from_speed);
        if (!tfp_finite3(from_speed) || rate > 8) for (unsigned i = 0; i < 3; ++i) from_speed[i] = 0;
        const float r = tfp_continuity_length(from_speed);
        duration = fminf(fmaxf(1000*(4*r+sqrtf(16*r*r+24*a*tfp_continuity_length(held)))/(2*a), t->min_ms), t->max_ms);
        c->settling = false;
      }
      tfp_continuity_begin(c, held, over ? from_speed : zero, tick, duration); c->rest_end = false;
      for (unsigned i = 0; i < 3; ++i) { value[i] = held[i]; c->coast[i] = 0; }
      c->lent = false;
    }
  }
  tfp_continuity_place(c, wanted, world, basis);
  c->moving = true;
  if (c->active && tick-c->started >= c->duration) c->active = false;
  const float carried = tfp_continuity_length(value);
  if (carried > c->largest) c->largest = carried;
  for (unsigned i = 0; i < 3; ++i) { out[i] = wanted[i]+value[i]; c->ago[i] = c->wanted[i]; c->wanted[i] = wanted[i]; c->shown[i] = out[i]; }
  c->ago_known = !edge; /* across a change of camera the eye before is another camera's */
  c->tick = tick; c->source = source;
  tfp_continuity_head(c, head, wanted, world, basis, head_kept, cut);
  return carried;
}
/* Without THE CUT: every caller but the helper's head-camera frames. */
static inline float tfp_continuity_step_in(TfpContinuity *c, const float *wanted, const float *world, const float *basis, uint32_t source,
                                           bool detect, bool quiet, double tick, uint32_t max_gap_ms, const TfpContinuityTuning *t, float *out) {
  return tfp_continuity_step_cut(c, wanted, world, basis, NULL, source, detect, quiet, tick, max_gap_ms, t, out);
}
static inline float tfp_continuity_step(TfpContinuity *c, const float *wanted, uint32_t source, bool detect, bool quiet, double tick,
                                        uint32_t max_gap_ms, const TfpContinuityTuning *t, float *out) {
  return tfp_continuity_step_in(c, wanted, NULL, NULL, source, detect, quiet, tick, max_gap_ms, t, out);
}
#endif
