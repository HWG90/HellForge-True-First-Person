/* The sight brought closer in full aim (round 20; docs/design/specs/2026-11-22-locked-view-design.md "Round 20").
 *
 * In full aim the game seats the rig itself (the kind-6 evaluator: the weapon's socket at the optic's depth D before
 * the eye, docs/design/specs/2026-11-23-gate-flip.md), and it sets the camera's near clip to the sight's own distance
 * (src/near_helper.c: 0.173 m for the LAS-5 Scythe, 0.22 m and 0.35 m for the two zoom levels of one scope, 0.066 m
 * for the LAS-44 Nova, the game's 0.05 for a pistol's iron sights). Nothing of the mod moves the rig in full aim
 * (src/pose_helper.c: the hip placement's weight is 0, the zoom depth and the held depth pass full aim, the ADS
 * placement is all zeros unless saved), so the sight sits exactly where the game puts it.
 *
 * The player can ask for it closer: `wanted` (locked_view.json ads_sight_cm) is how far from the eye the sight is to
 * be. The pull is how far the gun is moved toward the eye along the rig's forward axis (node 5, where the game's own
 * D goes) for the sight to be there, read off the game's near clip. The near clip moves in with the gun by as much,
 * so the same parts of the weapon are cut as the game cuts them; it never goes under the game's own 0.05. The pull
 * is eased in by the ADS share (1 - the hip placement's weight: 0 at the hip, 1 in full aim, over the placement's own
 * blend), and between zoom levels it glides (80 ms). No engine reads here. */
#ifndef TFP_ADS_SEAT_H
#define TFP_ADS_SEAT_H
#include <math.h>
#include <stdbool.h>

#define TFP_ADS_PULL_MOST .25f     /* m: never more than this toward the eye */
#define TFP_ADS_SIGHT_LEAST .05f   /* m: the game's own near clip; a near this close is no sight's */
#define TFP_ADS_SIGHT_MOST 1.0f    /* m: the near helper's own limit */
#define TFP_ADS_GLIDE_MS 80.0f
#define TFP_ADS_STALE_MS 250.0

/* The pull a sight at `sight` m (the game's near clip) asks for, to be `wanted` m from the eye. 0: nothing to pull
 * (no sight, already closer, or wanted 0: the game's own seat). */
static inline float tfp_ads_pull_for(float sight, float wanted) {
  if (!(wanted > 0) || !isfinite(wanted) || !isfinite(sight) || !(sight > TFP_ADS_SIGHT_LEAST) || sight > TFP_ADS_SIGHT_MOST) return 0;
  const float pull = sight - wanted;
  return pull <= 0 ? 0 : pull > TFP_ADS_PULL_MOST ? TFP_ADS_PULL_MOST : pull;
}
typedef struct TfpAdsPull { float shown; double at; bool has; } TfpAdsPull;
static inline void tfp_ads_pull_reset(TfpAdsPull *p) { p->shown = 0; p->at = 0; p->has = false; }
/* One frame of the pull toward `target`, `tick` in milliseconds. The first frame, or one after a pause, takes the
 * target at once: the ADS share eases it in from nothing on the way into aim. */
static inline float tfp_ads_pull_step(TfpAdsPull *p, float target, double tick) {
  if (!isfinite(target) || target < 0) target = 0;
  if (!isfinite(tick)) return p->has ? p->shown : 0;
  if (!p->has || tick < p->at || tick - p->at > TFP_ADS_STALE_MS) p->shown = target;
  else if (tick > p->at) p->shown += (target - p->shown)*(1 - expf(-(float)(tick - p->at)/TFP_ADS_GLIDE_MS));
  p->at = tick; p->has = true;
  return p->shown;
}
/* How much closer to the eye the gun is drawn: the pull by the ADS share, less the ADS placement's forward (the
 * placement editor's ADS action: forward is away from the eye). Never below 0. */
static inline float tfp_ads_closer(float pull, float share, float ads_forward) {
  const float closer = pull*share - (isfinite(ads_forward) ? ads_forward : 0);
  return closer > 0 && isfinite(closer) ? closer : 0;
}
/* The near clip in full aim: the game's `native`, moved in with the gun, never under the game's 0.05. */
static inline float tfp_ads_near(float native, float closer) {
  if (!isfinite(native) || !(closer > 0) || !isfinite(closer)) return native;
  const float near = native - closer;
  return near < TFP_ADS_SIGHT_LEAST ? (native < TFP_ADS_SIGHT_LEAST ? native : TFP_ADS_SIGHT_LEAST) : near;
}
#endif
