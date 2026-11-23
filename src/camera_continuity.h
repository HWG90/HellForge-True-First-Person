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
 *    same direction, is the motion slowing down and is followed, not taken up. Jumps in a row are taken up for
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
 *  - The catch-up is a cubic with zero end speed whose length follows from the distance, so that its largest
 *    acceleration is ease_accel: sqrt(6 * distance / ease_accel), between min_ms and max_ms. A new jump during a
 *    catch-up starts the curve again from the correction's present value and speed.
 *  - More than max_m is not carried (the rest is shown at once); a step of more than teleport_m, a pause longer
 *    than the helper's frame gap, or a value that is not finite starts again with no correction.
 *  - quiet (full aim): nothing new is taken up and what is carried ends within 80 ms; the sights stay on the eye. */
#ifndef TFP_CAMERA_CONTINUITY_H
#define TFP_CAMERA_CONTINUITY_H
#include "camera_math.h"

typedef struct TfpContinuityTuning { float pop_m, pop_accel, ease_accel, min_ms, max_ms, max_m, teleport_m; } TfpContinuityTuning;
#define TFP_CONTINUITY_DEFAULT {.04f, 36.0f, 24.0f, 100.0f, 400.0f, 1.6f, 2.5f} /* max_m 1.0 and teleport_m 1.5 until 0.6.0-feel.39: a change of camera in a sprint is 1.2 m */
#define TFP_CONTINUITY_RUN_MS 70.0   /* jumps in a row are taken up for this long without a change of source */
#define TFP_CONTINUITY_NEAR .9f       /* of the measure: once in a run a frame from here on counts as a jump, if it goes the way of the jump before it */
#define TFP_CONTINUITY_QUIET_MS 80.0f
#define TFP_CONTINUITY_SPEED_FRAMES 4u /* the frames looked at for the speed believed when a run's time is over */

typedef struct TfpContinuity {
  bool known, active, moving; /* moving: the wanted eye's speed is known (a step has been seen since the start) */
  uint32_t source, run;
  bool neared;                           /* the present run has had its frame under the measure */
  bool lent; float left;                 /* the kept speed is the camera before's (THE LENT SPEED); how much of it the presented eye still has (1: all) */
  double tick, started, run_started; /* run_started: the first jump of the present run */
  float wanted[3], speed[3], shown[3];   /* last frame: the wanted eye, its smoothed speed (m/s), the presented eye */
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
  float largest;                         /* the largest correction carried since the reader last cleared it */
} TfpContinuity;

static inline void tfp_continuity_reset(TfpContinuity *c) {
  const uint64_t jumps = c->jumps, edges = c->edges, restarts = c->restarts, reseats = c->reseats; const float largest = c->largest;
  memset(c, 0, sizeof *c); c->jumps = jumps; c->edges = edges; c->restarts = restarts; c->reseats = reseats; c->largest = largest;
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
  const double h00 = 2*s*s*s-3*s*s+1, h10 = s*s*s-2*s*s+s, d00 = (6*s*s-6*s)/seconds, d10 = 3*s*s-4*s+1;
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
/* One presented frame. `wanted`: the eye the helper would present, in the body's frame. `world`, `basis`: the same
 * eye in the world and the body root's axis rows (wanted = basis times (world - root)), or both NULL where the
 * world is not known: the root is then taken to move evenly and to carry the eye round with it. `source`: which camera
 * gave it. `detect`: jumps within a source are looked for (the native view); false: only a change of source is
 * taken up (the head camera, whose eye turns about the head with the view). Returns the correction's length;
 * `out` is the eye to present. */
static inline float tfp_continuity_step_in(TfpContinuity *c, const float *wanted, const float *world, const float *basis, uint32_t source,
                                           bool detect, bool quiet, double tick, uint32_t max_gap_ms, const TfpContinuityTuning *t, float *out) {
  for (unsigned i = 0; i < 3; ++i) out[i] = wanted[i];
  if (!world || !basis || !tfp_finite3(world)) world = basis = NULL;
  if (!tfp_finite3(wanted) || !isfinite(tick)) { if (c->known) ++c->restarts; tfp_continuity_reset(c); return 0; }
  if (!c->known || tick < c->tick || tick-c->tick > max_gap_ms) {
    if (c->known) ++c->restarts;
    tfp_continuity_reset(c);
    c->known = true; c->tick = tick; c->source = source;
    for (unsigned i = 0; i < 3; ++i) c->wanted[i] = c->shown[i] = wanted[i];
    tfp_continuity_place(c, wanted, world, basis);
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
  const bool edge = source != c->source;
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
        tfp_continuity_turn(turn, c->from); tfp_continuity_turn(turn, c->from_speed);
        tfp_continuity_curve(c, tick, value, speed);
      }
      if (freely) { /* nor does the eye's speed: it keeps its direction in the world */
        tfp_continuity_turn(turn, c->speed);
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
    tfp_continuity_turn(off, c->from); tfp_continuity_turn(off, c->from_speed); tfp_continuity_turn(off, c->speed);
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
    return 0;
  }
  float last[3], alike = 0; /* the frame before, off the kept speed (m/s), and how far this frame goes its way */
  for (unsigned i = 0; i < 3; ++i) { last[i] = c->recent[(c->recent_at+TFP_CONTINUITY_SPEED_FRAMES-1)%TFP_CONTINUITY_SPEED_FRAMES][i]-c->speed[i]; alike += last[i]*difference[i]; }
  for (unsigned i = 0; i < 3; ++i) c->recent[c->recent_at%TFP_CONTINUITY_SPEED_FRAMES][i] = step[i]/dt;
  c->recent_at = (c->recent_at+1)%TFP_CONTINUITY_SPEED_FRAMES;
  const float limit = measure;
  const float along = step[0]*expected[0]+step[1]*expected[1]+step[2]*expected[2];
  const bool slowing = along >= 0 && tfp_continuity_length(step) < tfp_continuity_length(expected);
  const bool over = size > limit;
  const bool within = !c->run || (tick >= c->run_started && tick-c->run_started <= TFP_CONTINUITY_RUN_MS);
  /* A run's last frame a hair under the measure: once in a run, within its time, a frame that goes the way the jump
   * before it went counts as a jump from TFP_CONTINUITY_NEAR of the measure. */
  const bool near = c->run && within && !c->neared && size > TFP_CONTINUITY_NEAR*limit && alike > .7f*size*tfp_continuity_length(last);
  const bool jump = !quiet && !edge && detect && (over || near) && !slowing && !onset;
  const bool believe = jump && !within; /* jumps for longer than the run: a new speed, not a re-seated rig */
  const bool take = !quiet && size > 1e-4f && (edge || jump);
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
      for (unsigned i = 0; i < 3; ++i) { from_speed[i] += c->left*c->speed[i]-fresh[i]; c->speed[i] = fresh[i]; }
      c->lent = false; /* the new camera's speed is known */
      rate = tfp_continuity_length(from_speed);
    }
    /* The curve's length: its largest acceleration is ease_accel where max_ms allows (6 d / T^2 + 4 v / T). */
    const float a = fmaxf(t->ease_accel, 1e-3f);
    float duration = 1000*(4*rate+sqrtf(16*rate*rate+24*a*length))/(2*a);
    duration = fminf(fmaxf(duration, t->min_ms), t->max_ms);
    tfp_continuity_begin(c, next, from_speed, tick, duration);
    for (unsigned i = 0; i < 3; ++i) value[i] = next[i];
    if (edge) { ++c->edges; c->run = 0; } else if (believe) c->run = 0; else { ++c->jumps; if (!c->run++) c->run_started = tick; }
    c->neared = c->run && (c->neared || !over);
    /* otherwise the wanted eye's speed is kept: after a jump it goes on as it went */
  } else {
    c->run = 0; c->neared = false; c->lent = false;
    const float k = over || !c->moving ? 1 : 1-expf(-dt/.05f); /* a new speed is believed at once, an ordinary frame is averaged in */
    for (unsigned i = 0; i < 3; ++i) c->speed[i] += (step[i]/dt-c->speed[i])*k;
    const float fast = tfp_continuity_length(c->speed);
    if (fast > 8) for (unsigned i = 0; i < 3; ++i) c->speed[i] *= 8/fast;
    if (quiet && c->active && c->started+c->duration-tick > TFP_CONTINUITY_QUIET_MS) tfp_continuity_begin(c, value, speed, tick, TFP_CONTINUITY_QUIET_MS);
    if (placed) { /* the speed with the turning left out, kept and averaged as the other is */
      const float kept = loose_size > limit || !c->loose ? 1 : 1-expf(-dt/.05f);
      for (unsigned i = 0; i < 3; ++i) c->loose_speed[i] += (free_step[i]/dt-c->loose_speed[i])*kept;
      const float quick = tfp_continuity_length(c->loose_speed);
      if (quick > 8) for (unsigned i = 0; i < 3; ++i) c->loose_speed[i] *= 8/quick;
      c->loose = isfinite(quick);
    }
  }
  if (!placed) { c->loose = c->stepped = false; c->held_off = c->loose_off = 0; }
  if (placed) {
    /* The root's course. A root that is off it by more than the measure without the eye jumping against it has a
     * new speed (the body thrown, a dive), believed at once. A root re-seated keeps the course it had; what it did
     * in this frame is kept beside it, for the next frame to say whether it was a new course after all. */
    float along[3], spin[3];
    tfp_continuity_angle(turn, spin);
    for (unsigned i = 0; i < 3; ++i) { along[i] = moved[i]/dt; spin[i] /= dt; }
    float *const speed_to = reseated ? c->tried_speed : c->root_speed, *const along_to = reseated ? c->tried_along : c->root_along;
    const float k = reseated || part_size > limit || !c->coursed ? 1 : 1-expf(-dt/.05f);
    for (unsigned i = 0; i < 3; ++i) { speed_to[i] += ((root[i]-c->root[i])/dt-speed_to[i])*k; along_to[i] += (along[i]-along_to[i])*k;
      (reseated ? c->tried_spin : c->spin)[i] = spin[i]; }
    c->tried = reseated; c->coursed = true;
  }
  tfp_continuity_place(c, wanted, world, basis);
  c->moving = true;
  if (c->active && tick-c->started >= c->duration) c->active = false;
  const float carried = tfp_continuity_length(value);
  if (carried > c->largest) c->largest = carried;
  for (unsigned i = 0; i < 3; ++i) { out[i] = wanted[i]+value[i]; c->wanted[i] = wanted[i]; c->shown[i] = out[i]; }
  c->tick = tick; c->source = source;
  return carried;
}
static inline float tfp_continuity_step(TfpContinuity *c, const float *wanted, uint32_t source, bool detect, bool quiet, double tick,
                                        uint32_t max_gap_ms, const TfpContinuityTuning *t, float *out) {
  return tfp_continuity_step_in(c, wanted, NULL, NULL, source, detect, quiet, tick, max_gap_ms, t, out);
}
#endif
