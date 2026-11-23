/* The glide of the head camera's eye to the incoming weapon's eye. The helper holds the eye of the native view
 * through a weapon swap as a difference from the head camera's own eye (eye_delta, in the body root's frame); when
 * the weapon identity changes and the incoming weapon's eye is remembered, that difference is moved to the incoming
 * one's while the swap has the view in the head camera, so the native view returns to a camera that is already
 * there. The arithmetic only: no engine reads here. */
#ifndef TFP_CAMERA_GLIDE_H
#define TFP_CAMERA_GLIDE_H
#include "camera_math.h"

/* Retail's glide, one frame: `value` goes to `target` on a time constant of 70 ms; `dt` in seconds. True: it moved
 * (by more than 0.000001 m along an axis). 1-exp(-t/70 ms) of the way after t whatever the frames, so it never ends
 * (1.4 % is left after 300 ms), and it starts at full speed: the first frame is its largest, 38 % of the way at 33 ms
 * a frame. */
static inline bool tfp_eye_glide_retail(float *value, const float *target, float dt) {
  const float step = dt > 0 ? 1-expf(-dt/.07f) : 0; bool moved = false;
  for (unsigned axis = 0; axis < 3; ++axis) {
    const float change = (target[axis]-value[axis])*step;
    if (fabsf(change) > 1e-6f) moved = true;
    value[axis] += change;
  }
  return moved;
}

/* The glide under the camera lock (beyond retail). Retail's begins with its largest step, and in a frame held by the
 * lock the continuity filter (src/camera_continuity.h) takes that up as the eye re-seated and carries it: seen in the
 * soak of 2026-11-23 on 0.6.0-feel.44 (tests/reference/soak-19-glide.h), where in 45 of 69 swaps the head camera's eye
 * changed its step by 0.06 m or more in the glide's first frame (0.344 m at most) and the presented eye then stood
 * 0.25 to 0.47 m from the wanted one, the catch-up started again and again, for over a second.
 * The gentle glide begins from rest and ends at rest, at the target itself: the eye gathers way at an even rate for
 * the first TFP_EYE_GLIDE_SHARE of its time and loses it at an even rate for what is left, so its velocity has no jump
 * anywhere and its step changes from frame to frame by that rate times the frame's time squared and no more.
 *  - The time follows from the distance, so that the eye gathers way at TFP_EYE_GLIDE_ACCEL:
 *    sqrt(2 d / (share * accel)), between TFP_EYE_GLIDE_MIN_MS and TFP_EYE_GLIDE_MAX_MS. 14 m/s^2 is what the filter
 *    follows at 30 frames a second without taking anything up (a step is a jump there from 0.04 m off the eye's speed
 *    averaged over 50 ms: 17 m/s^2 kept up; any slowing down is followed), and slowing down is then 21 m/s^2, 0.023 m
 *    of step change a frame. The 150 ms at least are for the short swaps: in that soak the head camera had the view
 *    for nine frames after the weapon identity changed in 45 swaps (300 to 302 ms) and for five (167 ms) in 15 more,
 *    whose glides were 0.07 m or less. (Retail's was 88 % there after 150 ms, 97 % after 250 and 98.6 % after 300.)
 *  - GATE. 250 ms at most, and a way that cannot be gone in that at 14 m/s^2 (more than TFP_EYE_GLIDE_FAR_M, 0.2625 m)
 *    is glided on retail's 70 ms under the lock as well (tfp_eye_glide_lock_step), the filter hiding it as before.
 *    Tried through the filter (tools/harness/glide_filter.c) and not kept for those:
 *      - 300 ms, the whole of what a swap leaves, for every way: 0.47 m is then gone at 17.4 m/s^2 with nothing taken
 *        up (0.027 m carried for retail's 0.317 m at 33 ms a frame), but the glide ends in the head camera's last
 *        frame and the speed the filter still keeps for it is lent to the native view (THE LENT SPEED there): at
 *        50 ms a frame in a sprint the presented eye's step changed by 0.101 to 0.104 m in the frame after, where it
 *        had been 0.086 m. And a way of 0.63 m (the logged swap) needs 23 m/s^2, is taken up in part, and a frame
 *        of that swap showed 0.081 m of step change where the log had 0.054 m.
 *      - 300 ms for ways up to the 0.378 m that 14 m/s^2 allows in it: the same at 50 ms a frame in a sprint for a
 *        way of 0.37 m, 0.086 to 0.089 m for 0.071 m.
 *    With 250 ms a frame at 20 frames a second is left between the glide's end and the swap's, and over 96 made-up
 *    swaps (0.07 to 0.47 m, a walk and a sprint, 8 to 50 ms a frame, both camera styles) the presented eye's step
 *    changes by more in none. What is left as it was: in that soak 50 of the 69 glides were longer than 0.2625 m (27
 *    of them 0.37 to 0.47 m), 10 were between that and the 0.105 m from which retail's first frame is a jump at 30
 *    frames a second.
 *  - The glide is a place for every time, not a step for every frame: the same place at the same time whatever the
 *    frames, a long frame included.
 *  - A target that changes while the eye is under way (a second swap inside the first) begins a glide from where the
 *    eye is with the speed it has, to the new target. Its time is that of its distance, and no less than the eye
 *    needs to turn round at the slowing-down rate (speed * (1 - share^2) / (share^2 * accel)), within the same 250 ms.
 *    The eye may pass a target that has come too near to stop at, and comes back to it.
 *  - The caller's value is the state: an eye that is not where the last frame left it (the helper set it anew from
 *    the native view, or let it go) begins a glide from rest, and so does the first frame after tfp_eye_glide_reset.
 *  - With no target any more while the eye is under way, tfp_eye_glide_rest brings it to rest at the slowing-down
 *    rate, where retail's stops in the frame (it is slow by then; this one is fastest in the middle). */
#define TFP_EYE_GLIDE_ACCEL 14.0f    /* m/s^2 the eye gathers way at, where the longest time allows */
#define TFP_EYE_GLIDE_SHARE .6f      /* of a glide's time is gathering way; the rest is slowing down, 1.5 times as hard */
#define TFP_EYE_GLIDE_MIN_MS 150.0f
#ifndef TFP_EYE_GLIDE_MAX_MS         /* (tools/harness/glide_filter.c can be built with another, to see what was tried) */
#define TFP_EYE_GLIDE_MAX_MS 250.0f
#endif
/* the longest way that is gone at TFP_EYE_GLIDE_ACCEL within TFP_EYE_GLIDE_MAX_MS: 0.2625 m */
#define TFP_EYE_GLIDE_FAR_M (TFP_EYE_GLIDE_SHARE*TFP_EYE_GLIDE_ACCEL*(TFP_EYE_GLIDE_MAX_MS/1000)*(TFP_EYE_GLIDE_MAX_MS/1000)/2)

/* from, speed: where the glide began and with what speed (m/s); top: its speed when the gathering of way ends;
 * target: where it ends; at: the eye as the last frame left it; elapsed, rise, duration: ms since it began, the
 * time it gathers way for, its whole time. */
typedef struct TfpEyeGlide { bool active, retail; float from[3], speed[3], top[3], target[3], at[3], elapsed, rise, duration; } TfpEyeGlide;
static inline void tfp_eye_glide_reset(TfpEyeGlide *g) { memset(g, 0, sizeof *g); }
/* The glide's place and speed `ms` after it began. */
static inline void tfp_eye_glide_at(const TfpEyeGlide *g, float ms, float *value, float *speed) {
  if (!(ms < g->duration)) { for (unsigned i = 0; i < 3; ++i) { value[i] = g->target[i]; speed[i] = 0; } return; }
  const float rise = g->rise/1000, fall = (g->duration-g->rise)/1000, t = ms/1000;
  for (unsigned i = 0; i < 3; ++i) {
    if (ms < g->rise) {
      const float rate = (g->top[i]-g->speed[i])/rise;
      value[i] = g->from[i]+g->speed[i]*t+rate*t*t/2; speed[i] = g->speed[i]+rate*t;
    } else {
      const float later = t-rise, rate = g->top[i]/fall;
      value[i] = g->from[i]+(g->speed[i]+g->top[i])*rise/2+g->top[i]*later-rate*later*later/2; speed[i] = g->top[i]-rate*later;
    }
  }
}
static inline void tfp_eye_glide_begin(TfpEyeGlide *g, const float *from, const float *speed, const float *target) {
  float square = 0, fast = 0;
  for (unsigned i = 0; i < 3; ++i) { g->from[i] = g->at[i] = from[i]; g->speed[i] = speed[i]; g->target[i] = target[i];
    square += (target[i]-from[i])*(target[i]-from[i]); fast += speed[i]*speed[i]; }
  const float share = TFP_EYE_GLIDE_SHARE;
  float ms = 1000*fmaxf(sqrtf(2*sqrtf(square)/(share*TFP_EYE_GLIDE_ACCEL)), sqrtf(fast)*(1-share*share)/(share*share*TFP_EYE_GLIDE_ACCEL));
  ms = fminf(fmaxf(ms, TFP_EYE_GLIDE_MIN_MS), TFP_EYE_GLIDE_MAX_MS);
  g->duration = ms; g->rise = ms*share; g->elapsed = 0; g->active = true;
  /* the way is half the first speed over the rise and half the top speed over the whole time */
  for (unsigned i = 0; i < 3; ++i) g->top[i] = (2*(target[i]-from[i])-speed[i]*g->rise/1000)/(ms/1000);
}
/* One frame: `value` (the eye as the last frame left it) goes towards `target`; `dt` in seconds. True: it moved (by
 * more than 0.000001 m along an axis, as tfp_eye_glide_retail says it). A `dt` that is not above 0 moves nothing; a
 * value or a target that is not a number moves nothing and forgets the glide. */
static inline bool tfp_eye_glide_step(TfpEyeGlide *g, float *value, const float *target, float dt) {
  if (!tfp_finite3(value) || !tfp_finite3(target)) { tfp_eye_glide_reset(g); return false; }
  const float rest[3] = {0, 0, 0};
  if (!g->active || value[0] != g->at[0] || value[1] != g->at[1] || value[2] != g->at[2]) tfp_eye_glide_begin(g, value, rest, target);
  else if (target[0] != g->target[0] || target[1] != g->target[1] || target[2] != g->target[2]) {
    float place[3], speed[3];
    tfp_eye_glide_at(g, g->elapsed, place, speed);
    tfp_eye_glide_begin(g, value, speed, target);
  }
  if (!(dt > 0) || !isfinite(dt)) return false;
  g->elapsed += dt*1000;
  float speed[3]; bool moved = false;
  tfp_eye_glide_at(g, g->elapsed, g->at, speed);
  for (unsigned i = 0; i < 3; ++i) { if (fabsf(g->at[i]-value[i]) > 1e-6f) moved = true; value[i] = g->at[i]; }
  return moved;
}
/* One frame without a target: an eye that is under way comes to rest at the slowing-down rate, from where it is and
 * the way it was going. True: it moved. Nothing under way (no glide, a glide that is over, an eye that is not where
 * the last frame left it): nothing moves, and the glide is forgotten. */
static inline bool tfp_eye_glide_rest(TfpEyeGlide *g, float *value, float dt) {
  if (!g->active || !tfp_finite3(value) || value[0] != g->at[0] || value[1] != g->at[1] || value[2] != g->at[2]) { tfp_eye_glide_reset(g); return false; }
  if (g->rise > 0) { /* not yet coming to rest: from the speed it has, with no gathering of way */
    float place[3], speed[3], fast = 0;
    tfp_eye_glide_at(g, g->elapsed, place, speed);
    for (unsigned i = 0; i < 3; ++i) fast += speed[i]*speed[i];
    fast = sqrtf(fast);
    if (!(fast > 0) || !isfinite(fast)) { tfp_eye_glide_reset(g); return false; }
    const float seconds = fast*(1-TFP_EYE_GLIDE_SHARE)/(TFP_EYE_GLIDE_SHARE*TFP_EYE_GLIDE_ACCEL);
    for (unsigned i = 0; i < 3; ++i) { g->from[i] = value[i]; g->speed[i] = g->top[i] = speed[i]; g->target[i] = value[i]+speed[i]*seconds/2; }
    g->duration = seconds*1000; g->rise = 0; g->elapsed = 0;
  }
  if (!(dt > 0) || !isfinite(dt)) return false;
  g->elapsed += dt*1000;
  float speed[3]; bool moved = false;
  tfp_eye_glide_at(g, g->elapsed, g->at, speed);
  for (unsigned i = 0; i < 3; ++i) { if (fabsf(g->at[i]-value[i]) > 1e-6f) moved = true; value[i] = g->at[i]; }
  if (!(g->elapsed < g->duration)) tfp_eye_glide_reset(g);
  return moved;
}
/* One frame of the glide in a frame the camera lock holds: the gentle glide where the way is no more than
 * TFP_EYE_GLIDE_FAR_M when it begins, retail's where it is longer (GATE). Which of the two is settled when a glide
 * begins (from rest, or an eye that is not where the last frame left it) and holds until it is over: a gentle glide
 * whose target moves away stays gentle within its 300 ms, a retail one goes on as retail's until it moves no more. */
static inline bool tfp_eye_glide_lock_step(TfpEyeGlide *g, float *value, const float *target, float dt) {
  if (!tfp_finite3(value) || !tfp_finite3(target)) { tfp_eye_glide_reset(g); return false; }
  if (!(g->active || g->retail) || value[0] != g->at[0] || value[1] != g->at[1] || value[2] != g->at[2]) {
    float square = 0;
    for (unsigned i = 0; i < 3; ++i) square += (target[i]-value[i])*(target[i]-value[i]);
    tfp_eye_glide_reset(g);
    g->retail = sqrtf(square) > TFP_EYE_GLIDE_FAR_M;
  }
  if (!g->retail) return tfp_eye_glide_step(g, value, target, dt);
  const bool moved = tfp_eye_glide_retail(value, target, dt);
  for (unsigned i = 0; i < 3; ++i) g->at[i] = value[i];
  if (!moved && dt > 0) g->retail = false;
  return moved;
}
#endif
