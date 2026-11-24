/* Field-of-view lock (beyond retail): the arithmetic of one field of view for every frame of the view. The value
 * glides in and out (80 ms time constant), so switching the lock, or the view, never pops. No engine reads here. */
#ifndef TFP_FOV_LOCK_H
#define TFP_FOV_LOCK_H
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define TFP_FOV_LOCK_GLIDE_MS 80.0f
#define TFP_FOV_LOCK_STALE_MS 250.0   /* longer than this since the last frame: what is on screen is the game's */
#define TFP_FOV_LOCK_DONE .003f       /* radians (0.17 degrees): a glide out ends here */

/* "Aim field of view" (round 20, docs/design/specs/2026-11-22-locked-view-design.md "Round 20"): what full aim shows.
 * TFP_FOV_AIM_ZOOM: the game's own lens (its zoom level), what "Zoom when aiming" on did. TFP_FOV_AIM_LOCKED: the
 * locked value, what it did off. A value above 0: that field of view in radians, whatever the zoom level, and with
 * the lock off too. */
#define TFP_FOV_AIM_ZOOM (-1.0f)
#define TFP_FOV_AIM_LOCKED 0.0f

typedef struct TfpFovLock { float shown; double at; } TfpFovLock; /* shown: 0 while nothing is changed */

static inline void tfp_fov_lock_reset(TfpFovLock *l) { l->shown = 0; l->at = 0; }
static inline bool tfp_fov_lock_active(const TfpFovLock *l) { return l->shown > 0; }
/* One frame. `wanted`: the locked field of view in radians, 0 with the lock off. `aim`: TFP_FOV_AIM_* or a chosen
 * value. `applied`: what the game (or the mod's own zoom policy) would show this frame. `tick`: milliseconds.
 * Returns the field of view to show. */
static inline float tfp_fov_lock_step_aim(TfpFovLock *l, float wanted, float aim, bool aiming, float applied, double tick) {
  if (!(applied > 0) || !isfinite(applied) || !isfinite(tick)) { tfp_fov_lock_reset(l); return applied; }
  const bool chosen = aiming && aim > 0 && isfinite(aim);
  const bool on = wanted > 0 || chosen;
  const bool fresh = l->shown > 0 && tick >= l->at && tick-l->at <= TFP_FOV_LOCK_STALE_MS;
  if (!on && !fresh) { tfp_fov_lock_reset(l); return applied; }
  const float target = chosen ? aim : wanted > 0 && !(aiming && !(aim == TFP_FOV_AIM_LOCKED)) ? wanted : applied;
  float shown = fresh ? l->shown : applied; /* start from what was on screen */
  if (fresh && tick > l->at) shown += (target-shown)*(1-expf(-(float)(tick-l->at)/TFP_FOV_LOCK_GLIDE_MS));
  if (!on && fabsf(shown-applied) < TFP_FOV_LOCK_DONE) { tfp_fov_lock_reset(l); return applied; }
  l->shown = shown; l->at = tick;
  return shown;
}
/* The call of feel.51 and before: "Zoom when aiming" on is the game's zoom, off the locked view. */
static inline float tfp_fov_lock_step(TfpFovLock *l, float wanted, bool zoom_when_aiming, bool aiming, float applied, double tick) {
  return tfp_fov_lock_step_aim(l, wanted, zoom_when_aiming ? TFP_FOV_AIM_ZOOM : TFP_FOV_AIM_LOCKED, aiming, applied, tick);
}
/* What the log calls an answer. */
static inline const char *tfp_fov_aim_said(float aim, char *out, size_t size) {
  if (aim > 0 && isfinite(aim)) snprintf(out, size, "a chosen %.1f degrees", aim*57.29577951308232);
  else snprintf(out, size, "%s", aim == TFP_FOV_AIM_LOCKED ? "your locked view" : "the game's zoom");
  return out;
}
#endif
