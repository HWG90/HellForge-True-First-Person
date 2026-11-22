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
 *  - A jump is a frame whose step differs from the wanted eye's own recent speed by more than
 *    max(pop_m, pop_accel * dt * dt). A change of source takes up any difference. Jumps within a source are looked
 *    for only where the caller says so (`detect`: the short time after something that can re-seat the rig); anywhere
 *    else a change of speed is the eye's own motion and is followed. A step shorter than the speed predicted, in the
 *    same direction, is the motion slowing down and is followed, not taken up. Jumps in a row are taken up for
 *    TFP_CONTINUITY_RUN_MS at most (a time, not a number of frames); then the new speed is believed, and the
 *    difference between the two speeds goes into the correction's curve, so the presented eye keeps its speed in
 *    that frame and gathers the new one along the curve.
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
#define TFP_CONTINUITY_DEFAULT {.04f, 36.0f, 24.0f, 100.0f, 400.0f, 1.0f, 1.5f}
#define TFP_CONTINUITY_RUN_MS 70.0   /* jumps in a row are taken up for this long without a change of source */
#define TFP_CONTINUITY_QUIET_MS 80.0f

typedef struct TfpContinuity {
  bool known, active, moving; /* moving: the wanted eye's speed is known (a step has been seen since the start) */
  uint32_t source, run;
  double tick, started, run_started; /* run_started: the first jump of the present run */
  float wanted[3], speed[3], shown[3];   /* last frame: the wanted eye, its smoothed speed (m/s), the presented eye */
  float from[3], from_speed[3], duration; /* the correction's curve: value and speed at `started`, length in ms */
  uint64_t jumps, edges, restarts;       /* taken up within a source; at a change of source; started again */
  float largest;                         /* the largest correction carried since the reader last cleared it */
} TfpContinuity;

static inline void tfp_continuity_reset(TfpContinuity *c) {
  const uint64_t jumps = c->jumps, edges = c->edges, restarts = c->restarts; const float largest = c->largest;
  memset(c, 0, sizeof *c); c->jumps = jumps; c->edges = edges; c->restarts = restarts; c->largest = largest;
}
static inline float tfp_continuity_length(const float *v) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
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
/* One presented frame. `wanted`: the eye the helper would present, in the body's frame. `source`: which camera
 * gave it. `detect`: jumps within a source are looked for (the native view); false: only a change of source is
 * taken up (the head camera, whose eye turns about the head with the view). Returns the correction's length;
 * `out` is the eye to present. */
static inline float tfp_continuity_step(TfpContinuity *c, const float *wanted, uint32_t source, bool detect, bool quiet, double tick,
                                        uint32_t max_gap_ms, const TfpContinuityTuning *t, float *out) {
  for (unsigned i = 0; i < 3; ++i) out[i] = wanted[i];
  if (!tfp_finite3(wanted) || !isfinite(tick)) { if (c->known) ++c->restarts; tfp_continuity_reset(c); return 0; }
  if (!c->known || tick < c->tick || tick-c->tick > max_gap_ms) {
    if (c->known) ++c->restarts;
    tfp_continuity_reset(c);
    c->known = true; c->tick = tick; c->source = source;
    for (unsigned i = 0; i < 3; ++i) c->wanted[i] = c->shown[i] = wanted[i];
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
  const float size = tfp_continuity_length(difference);
  if (!isfinite(size) || size > t->teleport_m) { /* a real teleport: nothing is carried across it */
    ++c->restarts; tfp_continuity_reset(c);
    c->known = true; c->tick = tick; c->source = source;
    for (unsigned i = 0; i < 3; ++i) c->wanted[i] = c->shown[i] = wanted[i];
    return 0;
  }
  const float limit = fmaxf(t->pop_m, t->pop_accel*dt*dt);
  const float along = step[0]*expected[0]+step[1]*expected[1]+step[2]*expected[2];
  const bool slowing = along >= 0 && tfp_continuity_length(step) < tfp_continuity_length(expected);
  const bool over = size > limit;
  const bool jump = !quiet && !edge && detect && over && !slowing;
  const bool within = !c->run || (tick >= c->run_started && tick-c->run_started <= TFP_CONTINUITY_RUN_MS);
  const bool believe = jump && !within; /* jumps for longer than the run: a new speed, not a re-seated rig */
  const bool take = !quiet && size > 1e-4f && (edge || jump);
  if (take) {
    float next[3], from_speed[3], rate = 0;
    for (unsigned i = 0; i < 3; ++i) { next[i] = value[i]-difference[i]; from_speed[i] = speed[i]; }
    float length = tfp_continuity_length(next);
    if (length > t->max_m) { for (unsigned i = 0; i < 3; ++i) next[i] *= t->max_m/length; length = t->max_m; }
    if (believe) {
      /* This frame is still taken up, so the presented eye goes on at the old speed; the wanted eye's speed is
       * now the new one, and the difference of the two goes into the correction's curve. */
      float fresh[3]; for (unsigned i = 0; i < 3; ++i) fresh[i] = step[i]/dt;
      const float fast = tfp_continuity_length(fresh);
      if (fast > 8) for (unsigned i = 0; i < 3; ++i) fresh[i] *= 8/fast;
      for (unsigned i = 0; i < 3; ++i) { from_speed[i] += c->speed[i]-fresh[i]; c->speed[i] = fresh[i]; }
      rate = tfp_continuity_length(from_speed);
    }
    /* The curve's length: its largest acceleration is ease_accel where max_ms allows (6 d / T^2 + 4 v / T). */
    const float a = fmaxf(t->ease_accel, 1e-3f);
    float duration = 1000*(4*rate+sqrtf(16*rate*rate+24*a*length))/(2*a);
    duration = fminf(fmaxf(duration, t->min_ms), t->max_ms);
    tfp_continuity_begin(c, next, from_speed, tick, duration);
    for (unsigned i = 0; i < 3; ++i) value[i] = next[i];
    if (edge) { ++c->edges; c->run = 0; } else if (believe) c->run = 0; else { ++c->jumps; if (!c->run++) c->run_started = tick; }
    /* otherwise the wanted eye's speed is kept: after a jump it goes on as it went */
  } else {
    c->run = 0;
    const float k = over || !c->moving ? 1 : 1-expf(-dt/.05f); /* a new speed is believed at once, an ordinary frame is averaged in */
    for (unsigned i = 0; i < 3; ++i) c->speed[i] += (step[i]/dt-c->speed[i])*k;
    const float fast = tfp_continuity_length(c->speed);
    if (fast > 8) for (unsigned i = 0; i < 3; ++i) c->speed[i] *= 8/fast;
    if (quiet && c->active && c->started+c->duration-tick > TFP_CONTINUITY_QUIET_MS) tfp_continuity_begin(c, value, speed, tick, TFP_CONTINUITY_QUIET_MS);
  }
  c->moving = true;
  if (c->active && tick-c->started >= c->duration) c->active = false;
  const float carried = tfp_continuity_length(value);
  if (carried > c->largest) c->largest = carried;
  for (unsigned i = 0; i < 3; ++i) { out[i] = wanted[i]+value[i]; c->wanted[i] = wanted[i]; c->shown[i] = out[i]; }
  c->tick = tick; c->source = source;
  return carried;
}
#endif
