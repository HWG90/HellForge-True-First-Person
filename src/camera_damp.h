/* Camera damping (beyond retail): how much the view is steadied against the body's own movement. One option, a
 * percentage, gives one time constant; the kick of the view's rotation is low-passed with it (src/camera_lock.h),
 * and so is the eye of the head camera, here. No engine reads here. */
#ifndef TFP_CAMERA_DAMP_H
#define TFP_CAMERA_DAMP_H
#include "camera_math.h"

#define TFP_CAMERA_DAMPING_MS 300.0f /* the time constant at 100 % */
#define TFP_EYE_DAMP_LAG_M .10f      /* the most the presented eye is ever behind the true one */

/* The option's percentage as a time constant in milliseconds: 0 none, 100 the longest, linear between. */
static inline float tfp_camera_damping_ms(double percent) {
  if (!(percent > 0)) return 0;
  return (float)(percent < 100 ? percent : 100)*TFP_CAMERA_DAMPING_MS/100;
}

/* eye: the eye last presented, relative to the body root in the root's frame; tick: when, in milliseconds */
typedef struct TfpEyeDamp { float eye[3]; double tick; bool known; } TfpEyeDamp;
static inline void tfp_eye_damp_reset(TfpEyeDamp *d) { d->eye[0] = d->eye[1] = d->eye[2] = 0; d->tick = 0; d->known = false; }
/* One head-camera frame. `wanted` is the true eye relative to the body root, `damp_ms` the time constant (0: no
 * damping), `out` the eye to present. Returns how far `out` is behind `wanted`; 0: it is `wanted` itself.
 * It starts again from the true eye after a pause longer than `max_gap_ms`, after an input that is not a number and
 * after a frame without damping. The caller resets it in every frame that is not a head-camera frame, so the change
 * to the head camera itself is not damped here (src/camera_continuity.h takes that). */
static inline float tfp_eye_damp_step(TfpEyeDamp *d, const float *wanted, double tick, uint32_t max_gap_ms, float damp_ms, float *out) {
  for (unsigned i = 0; i < 3; ++i) out[i] = wanted[i];
  if (!isfinite(wanted[0]) || !isfinite(wanted[1]) || !isfinite(wanted[2]) || !isfinite(tick) || !(damp_ms > 0) || !isfinite(damp_ms)) {
    tfp_eye_damp_reset(d); return 0;
  }
  if (!d->known || tick < d->tick || tick-d->tick > max_gap_ms) {
    for (unsigned i = 0; i < 3; ++i) d->eye[i] = wanted[i];
    d->tick = tick; d->known = true; return 0;
  }
  const float k = 1-expf(-(float)(tick-d->tick)/damp_ms); /* the same instant again: 0, and the same answer */
  float behind[3], square = 0;
  for (unsigned i = 0; i < 3; ++i) { d->eye[i] += (wanted[i]-d->eye[i])*k; behind[i] = wanted[i]-d->eye[i]; square += behind[i]*behind[i]; }
  float lag = sqrtf(square);
  if (!isfinite(lag)) { tfp_eye_damp_reset(d); return 0; }
  if (lag > TFP_EYE_DAMP_LAG_M) { /* pulled along at the limit */
    for (unsigned i = 0; i < 3; ++i) d->eye[i] = wanted[i]-behind[i]*(TFP_EYE_DAMP_LAG_M/lag);
    lag = TFP_EYE_DAMP_LAG_M;
  }
  d->tick = tick;
  if (!(lag > 0)) return 0;
  for (unsigned i = 0; i < 3; ++i) out[i] = d->eye[i];
  return lag;
}
#endif
