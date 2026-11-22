/* Field-of-view lock (beyond retail): the arithmetic of one field of view for every frame of the view. The value
 * glides in and out (80 ms time constant), so switching the lock, or the view, never pops. No engine reads here. */
#ifndef TFP_FOV_LOCK_H
#define TFP_FOV_LOCK_H
#include <math.h>
#include <stdbool.h>

#define TFP_FOV_LOCK_GLIDE_MS 80.0f
#define TFP_FOV_LOCK_STALE_MS 250.0   /* longer than this since the last frame: what is on screen is the game's */
#define TFP_FOV_LOCK_DONE .003f       /* radians (0.17 degrees): a glide out ends here */

typedef struct TfpFovLock { float shown; double at; } TfpFovLock; /* shown: 0 while nothing is changed */

static inline void tfp_fov_lock_reset(TfpFovLock *l) { l->shown = 0; l->at = 0; }
static inline bool tfp_fov_lock_active(const TfpFovLock *l) { return l->shown > 0; }
/* One frame. `wanted`: the locked field of view in radians, 0 with the lock off. `applied`: what the game (or the
 * mod's own zoom policy) would show this frame. `tick`: milliseconds. Returns the field of view to show. */
static inline float tfp_fov_lock_step(TfpFovLock *l, float wanted, bool zoom_when_aiming, bool aiming, float applied, double tick) {
  if (!(applied > 0) || !isfinite(applied) || !isfinite(tick)) { tfp_fov_lock_reset(l); return applied; }
  const bool on = wanted > 0;
  const bool fresh = l->shown > 0 && tick >= l->at && tick-l->at <= TFP_FOV_LOCK_STALE_MS;
  if (!on && !fresh) { tfp_fov_lock_reset(l); return applied; }
  const float target = on && !(aiming && zoom_when_aiming) ? wanted : applied;
  float shown = fresh ? l->shown : applied; /* start from what was on screen */
  if (fresh && tick > l->at) shown += (target-shown)*(1-expf(-(float)(tick-l->at)/TFP_FOV_LOCK_GLIDE_MS));
  if (!on && fabsf(shown-applied) < TFP_FOV_LOCK_DONE) { tfp_fov_lock_reset(l); return applied; }
  l->shown = shown; l->at = tick;
  return shown;
}
#endif
