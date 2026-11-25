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
#define TFP_ADS_SNAP .0002f       /* m: a glide this close to its target is there */

/* The pull a sight at `sight` m (the game's near clip) asks for, to be `wanted` m from the eye. 0: nothing to pull
 * (no sight, already closer, or wanted 0: the game's own seat). */
static inline float tfp_ads_pull_for(float sight, float wanted) {
  if (!(wanted > 0) || !isfinite(wanted) || !isfinite(sight) || !(sight > TFP_ADS_SIGHT_LEAST) || sight > TFP_ADS_SIGHT_MOST) return 0;
  const float pull = sight - (wanted < TFP_ADS_SIGHT_LEAST ? TFP_ADS_SIGHT_LEAST : wanted); /* not inside the game's own 0.05 */
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
  if (fabsf(p->shown - target) < TFP_ADS_SNAP) p->shown = target; /* ends: a glide to nothing is nothing */
  p->at = tick; p->has = true;
  return p->shown;
}
/* How much closer to the eye the gun is drawn: the pull by the ADS share, less the ADS placement's forward (the
 * placement editor's ADS action: forward is away from the eye). Never below 0. */
static inline float tfp_ads_closer(float pull, float share, float ads_forward) {
  const float closer = pull*share - (isfinite(ads_forward) ? ads_forward : 0);
  return closer > 0 && isfinite(closer) ? closer : 0;
}
/* Where the sight can be this frame (round 20 review, B2). The game's near clip (`sight`) jumps to a new zoom level's
 * distance in one frame, and reaches the pose helper a tick later; the game moves its own seat there at a pace of its
 * own: the depth D (`depth`, request+0x90 blended over the lens, src/depth_steady.h tfp_steady_depth), on its way to
 * the selected optic's own distance (`rest`, request+0x90). The sight goes with D. Where it is now:
 *   - once the sight and D have stood still at rest together for TFP_ADS_SETTLED_MS (`a`, a pair), that sight moved by
 *     as much as D moved since, or the near clip if nearer: right in the frame the zoom level changes, while the near
 *     clip the pose helper has is a tick old;
 *   - before that (an aim coming in), the near clip less what D still lacks of its rest (a D short of its rest is the
 *     gun still nearer the eye). Not once paired: the near clip a tick old against the new zoom level's rest would
 *     count the change twice, and the gun would jump out for a frame.
 * With no depth worked out this frame (NAN), the near clip alone. Nothing known stays nothing: `sight` back.
 * Round 20c (review 27): the pair is made once an aim (the pose helper resets it at the hip) and kept. Where the sight
 * goes with D, a new pair says what the kept one says; where the game were to change its near clip and not its depth,
 * a new pair 50 ms later would move the limit by the whole difference in one frame (and toward the farther level pull
 * the gun 13 cm in, in front of a sight that had not moved). A near clip nearer than the pair still counts (the min);
 * how fast the pull may then go down is tfp_ads_pull_frame's. */
#define TFP_ADS_MARGIN 0.f         /* m: the sight never nearer than the wanted distance less this. 0: with 1 cm (the
                                    * review's margin) the first step of a glide toward a new zoom level could bring the
                                    * sight 1 cm inside where the near clip stood (set before the gun in that frame) */
#define TFP_ADS_SETTLED_MS 50.0    /* ms the sight and D stand still at rest before they are a pair */
#define TFP_ADS_AT_REST .001f      /* m: D this near its rest is at rest */
typedef struct TfpAdsSeat { float sight, depth, seen_sight, seen_rest; double since; bool has, seen; } TfpAdsSeat;
static inline void tfp_ads_seat_reset(TfpAdsSeat *a) { a->sight = a->depth = a->seen_sight = a->seen_rest = 0; a->since = 0; a->has = a->seen = false; }
static inline float tfp_ads_sight_least(TfpAdsSeat *a, float sight, float depth, float rest, double tick) {
  if (!(sight > TFP_ADS_SIGHT_LEAST) || !(sight <= TFP_ADS_SIGHT_MOST)) { tfp_ads_seat_reset(a); return sight; }
  if (!isfinite(depth) || !isfinite(rest) || !isfinite(tick)) return sight;
  const bool at_rest = fabsf(depth - rest) <= TFP_ADS_AT_REST;
  if (!at_rest || !a->seen || sight != a->seen_sight || rest != a->seen_rest || tick < a->since) {
    a->seen = true; a->seen_sight = sight; a->seen_rest = rest; a->since = tick;
  } else if (!a->has && tick - a->since >= TFP_ADS_SETTLED_MS) { a->sight = sight; a->depth = depth; a->has = true; }
  if (!a->has) return sight + (depth < rest ? depth - rest : 0);
  const float moved = a->sight + (depth - a->depth);
  return moved < sight ? moved : sight;
}
/* The most the gun may be pulled in a frame whose sight is at least `least` m away: the sight no nearer than the
 * wanted distance less TFP_ADS_MARGIN, nor than the game's own 0.05 (a near clip never goes under it). */
static inline float tfp_ads_pull_most(float least, float wanted) {
  if (!(wanted > 0) || !isfinite(least)) return TFP_ADS_PULL_MOST;
  const float floor = wanted - TFP_ADS_MARGIN > TFP_ADS_SIGHT_LEAST ? wanted - TFP_ADS_MARGIN : TFP_ADS_SIGHT_LEAST, most = least - floor;
  return most > 0 ? (most < TFP_ADS_PULL_MOST ? most : TFP_ADS_PULL_MOST) : 0;
}
/* The glide held to `most`: it goes on from there, so the pull follows the game's seat while it is limited. */
static inline float tfp_ads_pull_limit(TfpAdsPull *p, float most) {
  if (isfinite(most) && p->shown > most) p->shown = most > 0 ? most : 0;
  return p->shown;
}
/* The pull lowered no faster than the game's own depth moves (round 20c; review 27, B1). The limit above takes the
 * game's near clip the frame it changes, and the near clip of a nearer zoom level can come before the game has moved
 * its seat there: the limit alone cut the pull at once and the gun jumped 8 to 13 cm out in one frame, then crept back
 * in as the game's depth caught up (or stayed out, were the depth never to move). So while the pull is shown (`aiming`:
 * the ADS share above 0) it may go down in a frame by no more than the game's depth `depth` went down since the last
 * frame (the gun the game brings toward the eye, the pull gives back as much: the sight stays where it was). With no
 * depth worked out this frame or the last, it glides down (TFP_ADS_GLIDE_MS) to where the target and the limit have
 * it, never at once. The player asking for a farther sight (`wanted` grown: the menu) is no move of the game's: that
 * glides too, until the pull is where it was going. So does a pull with nothing left to ask for it (review 28, I1): the
 * wanted distance set to 0 (off) mid-aim, or no sight handed (`sight` not one: tfp_ads_pull_for's 0); held, the pull
 * would stay until the aim ended, as the depth does not move. A sight nearer than the wanted distance (a nearer zoom
 * level) is the game's move and is held. Going up, and at the hip, as the glide and the limit have it.
 * `depth` is the depth the gun is drawn at (the game's, plus what the held depth added: review 27's I1). The first
 * frame, or one after a pause, is the glide's (it takes the target; the share eases it in). */
typedef struct TfpAdsFall { float depth, wanted; double at; bool has, asked; } TfpAdsFall;
static inline void tfp_ads_fall_reset(TfpAdsFall *f) { f->depth = f->wanted = 0; f->at = 0; f->has = f->asked = false; }
/* No sight to pull toward: none handed, the game's own 0.05 (iron sights), or beyond the near helper's limit. */
static inline bool tfp_ads_sight_none(float sight) { return !isfinite(sight) || !(sight > TFP_ADS_SIGHT_LEAST) || sight > TFP_ADS_SIGHT_MOST; }
static inline float tfp_ads_pull_frame(TfpAdsPull *p, TfpAdsFall *f, float sight, float target, float most, float depth, float wanted, bool aiming,
                                       double tick) {
  const bool fresh = p->has && isfinite(tick) && tick >= p->at && tick - p->at <= TFP_ADS_STALE_MS;
  const float before = p->shown;
  const double since = fresh ? tick - p->at : 0;
  const bool depths = isfinite(depth) && f->has && isfinite(tick) && tick >= f->at && tick - f->at <= TFP_ADS_STALE_MS;
  const float fell = depths && f->depth > depth ? f->depth - depth : 0;
  const bool off = !(isfinite(wanted) && wanted > 0);
  if (f->at > 0 && ((!off && wanted > f->wanted) || (off && f->wanted > 0) || tfp_ads_sight_none(sight)))
    f->asked = true; /* the player's own change, or no sight left: glided, not held */
  f->wanted = isfinite(wanted) ? wanted : 0;
  tfp_ads_pull_step(p, target, tick);
  tfp_ads_pull_limit(p, most);
  if (aiming && fresh && p->shown < before) {
    const float to = isfinite(most) && most < target ? (most > 0 ? most : 0) : target;
    float glided = before + (to - before)*(1 - expf(-(float)since/TFP_ADS_GLIDE_MS));
    if (fabsf(glided - to) < TFP_ADS_SNAP) glided = to; /* ends as the glide ends */
    const float least = depths && !f->asked ? before - fell : glided;
    if (p->shown < least) p->shown = least;
  }
  if (f->asked && (!aiming || fabsf(p->shown - (isfinite(most) && most < target ? most : target)) < TFP_ADS_SNAP)) f->asked = false;
  if (isfinite(depth) && isfinite(tick)) { f->depth = depth; f->at = tick; f->has = true; } else { f->has = false; if (isfinite(tick)) f->at = tick; }
  return p->shown;
}
/* What the near clip is moved in by (round 20 review, B1). The game may call its near setter before the gun's node in
 * a frame, and then the near clip would stand where last frame's gun was: behind the sight's rear while the gun comes
 * in, cutting into the sight. So while the gun comes closer the near clip leads it by three times the last frame's
 * step: the next step of the aim's smoothstep is at most three times the one before (after its first frame, the next is
 * a little under three times; later ones less), and the pull's glide has shrinking steps. Twice, with no snap term,
 * left 0.12 mm cut at 60 fps (tests/wire_pose_test.c ads_frames). Going out it follows the gun itself (a near clip short of the sight cuts
 * nothing; only one beyond it does). The pull's glide ends in a snap of up to TFP_ADS_SNAP: that too is led by.
 * `last` is the closer published the frame before, within TFP_ADS_STALE_MS. */
typedef struct TfpAdsLead { float last; double at; bool has; } TfpAdsLead;
static inline float tfp_ads_lead(TfpAdsLead *l, float closer, double tick) {
  if (!isfinite(closer) || closer < 0) closer = 0;
  const bool fresh = l->has && isfinite(tick) && tick >= l->at && tick - l->at <= TFP_ADS_STALE_MS;
  const float grow = fresh ? closer - l->last : 0;
  l->last = closer; l->at = isfinite(tick) ? tick : 0; l->has = true;
  return grow > 0 ? closer + 3*grow + TFP_ADS_SNAP : closer; /* and the glide's last step, its snap */
}
/* The near clip in full aim: the game's `native`, moved in with the gun, never under the game's 0.05. */
static inline float tfp_ads_near(float native, float closer) {
  if (!isfinite(native) || !(closer > 0) || !isfinite(closer)) return native;
  const float near = native - closer;
  return near < TFP_ADS_SIGHT_LEAST ? (native < TFP_ADS_SIGHT_LEAST ? native : TFP_ADS_SIGHT_LEAST) : near;
}
#endif
