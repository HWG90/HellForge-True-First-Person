/* Camera damping (beyond retail): how much the view is steadied against the body's own movement. One option, a
 * percentage, gives one time constant; the kick of the view's rotation is low-passed with it (src/camera_lock.h),
 * and so is the eye of the head camera, here: what is low-passed is the head's place against the body root, and
 * the lag of that is what the eye is moved by. No engine reads here. */
#ifndef TFP_CAMERA_DAMP_H
#define TFP_CAMERA_DAMP_H
#include "camera_math.h"

#define TFP_CAMERA_DAMPING_MS 300.0f /* the time constant at 100 % */
#define TFP_EYE_DAMP_LAG_M .10f      /* the most the presented eye is ever behind the true one, under the camera damping option */
#define TFP_EYE_DAMP_RELEASE_MS 40.0f /* the time constant a carried lag is let go on when damping stops */
#define TFP_EYE_DAMP_GONE_M .001f    /* a lag let go this far is gone */

/* The option's percentage as a time constant in milliseconds: 0 none, 100 the longest, linear between. */
static inline float tfp_camera_damping_ms(double percent) {
  if (!(percent > 0)) return 0;
  return (float)(percent < 100 ? percent : 100)*TFP_CAMERA_DAMPING_MS/100;
}

/* The camera style (the Bodycam option of the Locked view tab): which of two cameras the lock gives.
 *   Bodycam: the view moves with the head, like a body camera. The kick sliders and the camera damping option
 *            apply as they are; this is what the mod did before the option existed.
 *   Stable (the option off, the default): a steady, traditional first-person camera. The sliders for the kick on
 *            foot and the camera damping are not used; the numbers below are, and they are all of them. */
#define TFP_STABLE_FOOT_KICK 0.0f       /* share of the kick strength on foot: none. Walking, strafing, crouching, a reload, a grenade or a stim do not turn the view */
#define TFP_STABLE_THROWN_KICK .5f      /* share of the "when thrown" strength a thrown or ragdolled body still gives (its limit is not changed) */
#define TFP_STABLE_KICK_DAMP_MS 250.0f  /* the time constant that kick is low-passed with, so being thrown reads as a sway, not a jolt */
#define TFP_STABLE_EYE_DAMP_MS 350.0f   /* the time constant the head camera's eye follows the head with: the head's bobbing and jerks in an action mostly do not reach the view */
#define TFP_STABLE_EYE_LAG_M .25f       /* the most that eye is behind the head: a crouch or going prone (0.5 m, and staying there) is followed within about a second */
/* foot_kick, thrown_kick: what the two kick strengths are multiplied by; kick_damp_ms: the kick's low-pass (0: none);
 * eye_damp_ms, eye_lag_m: the head camera eye's low-pass (0: none) and the most it is behind. */
typedef struct TfpCameraStyle { float foot_kick, thrown_kick, kick_damp_ms, eye_damp_ms, eye_lag_m; } TfpCameraStyle;
/* `damping_ms`: the camera damping option as a time constant (tfp_camera_damping_ms), which only Bodycam uses. */
static inline TfpCameraStyle tfp_camera_style(bool bodycam, float damping_ms) {
  if (bodycam) return (TfpCameraStyle){1.0f, 1.0f, damping_ms, damping_ms, TFP_EYE_DAMP_LAG_M};
  return (TfpCameraStyle){TFP_STABLE_FOOT_KICK, TFP_STABLE_THROWN_KICK, TFP_STABLE_KICK_DAMP_MS, TFP_STABLE_EYE_DAMP_MS, TFP_STABLE_EYE_LAG_M};
}

/* eye: the point last presented, relative to the body root in the root's frame; behind: that point minus the true
 * one; tick: when, in milliseconds; releasing: the lag is being let go (tfp_eye_damp_release), `eye` is not kept */
typedef struct TfpEyeDamp { float eye[3], behind[3]; double tick; bool known, releasing; } TfpEyeDamp;
static inline void tfp_eye_damp_reset(TfpEyeDamp *d) {
  for (unsigned i = 0; i < 3; ++i) d->eye[i] = d->behind[i] = 0;
  d->tick = 0; d->known = false; d->releasing = false;
}
/* One head-camera frame. `wanted` is the true eye relative to the body root, `damp_ms` the time constant (0: no
 * damping), `lag_m` the most the presented eye is ever behind the true one (a camera style's eye_lag_m; not above
 * 0: no damping), `out` the eye to present. Returns how far `out` is behind `wanted`; 0: it is `wanted` itself.
 * It starts again from the true eye after a pause longer than `max_gap_ms`, after an input that is not a number and
 * after a frame without damping. The caller resets it in every frame that is not a head-camera frame, so the change
 * to the head camera itself is not damped here (src/camera_continuity.h takes that). */
static inline float tfp_eye_damp_follow(TfpEyeDamp *d, const float *wanted, double tick, uint32_t max_gap_ms, float damp_ms, float lag_m, float *out) {
  for (unsigned i = 0; i < 3; ++i) out[i] = wanted[i];
  if (!isfinite(wanted[0]) || !isfinite(wanted[1]) || !isfinite(wanted[2]) || !isfinite(tick) || !(damp_ms > 0) || !isfinite(damp_ms) || !(lag_m > 0)) {
    tfp_eye_damp_reset(d); return 0;
  }
  if (!d->known || tick < d->tick || tick-d->tick > max_gap_ms) {
    for (unsigned i = 0; i < 3; ++i) { d->eye[i] = wanted[i]; d->behind[i] = 0; }
    d->tick = tick; d->known = true; d->releasing = false; return 0;
  }
  if (d->releasing) { /* damping again before the lag was gone: from where the point was shown */
    for (unsigned i = 0; i < 3; ++i) d->eye[i] = wanted[i]+d->behind[i];
    d->releasing = false;
  }
  const float k = 1-expf(-(float)(tick-d->tick)/damp_ms); /* the same instant again: 0, and the same answer */
  float behind[3], square = 0;
  for (unsigned i = 0; i < 3; ++i) { d->eye[i] += (wanted[i]-d->eye[i])*k; behind[i] = wanted[i]-d->eye[i]; square += behind[i]*behind[i]; }
  float lag = sqrtf(square);
  if (!isfinite(lag)) { tfp_eye_damp_reset(d); return 0; }
  if (lag > lag_m) { /* pulled along at the limit */
    for (unsigned i = 0; i < 3; ++i) d->eye[i] = wanted[i]-behind[i]*(lag_m/lag);
    lag = lag_m;
  }
  d->tick = tick;
  for (unsigned i = 0; i < 3; ++i) d->behind[i] = d->eye[i]-wanted[i];
  if (!(lag > 0)) return 0;
  for (unsigned i = 0; i < 3; ++i) out[i] = d->eye[i];
  return lag;
}
/* The same with the camera damping option's own limit, TFP_EYE_DAMP_LAG_M. */
static inline float tfp_eye_damp_step(TfpEyeDamp *d, const float *wanted, double tick, uint32_t max_gap_ms, float damp_ms, float *out) {
  return tfp_eye_damp_follow(d, wanted, tick, max_gap_ms, damp_ms, TFP_EYE_DAMP_LAG_M, out);
}
/* One frame in which damping has stopped (the body left 'on foot', full aim began) while a lag is carried: the lag
 * is let go on TFP_EYE_DAMP_RELEASE_MS, not dropped in one frame, and nothing new is damped: `behind` takes what to
 * add to the true point this frame, whatever that point does. Returns its length; 0: nothing is carried (any more)
 * and the state is forgotten. */
static inline float tfp_eye_damp_release(TfpEyeDamp *d, double tick, uint32_t max_gap_ms, float *behind) {
  behind[0] = behind[1] = behind[2] = 0;
  if (!d->known || !isfinite(tick) || tick < d->tick || tick-d->tick > max_gap_ms) { tfp_eye_damp_reset(d); return 0; }
  const float keep = expf(-(float)(tick-d->tick)/TFP_EYE_DAMP_RELEASE_MS);
  float square = 0;
  for (unsigned i = 0; i < 3; ++i) { d->behind[i] *= keep; square += d->behind[i]*d->behind[i]; }
  const float lag = sqrtf(square);
  if (!isfinite(lag) || lag < TFP_EYE_DAMP_GONE_M) { tfp_eye_damp_reset(d); return 0; }
  for (unsigned i = 0; i < 3; ++i) behind[i] = d->behind[i];
  d->tick = tick; d->releasing = true;
  return lag;
}
#endif
