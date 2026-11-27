/* Field-of-view lock (beyond retail): the arithmetic of one field of view for every frame of the view. The value
 * glides in and out (80 ms time constant), so switching the lock, or the view, never pops. No engine reads here. */
#ifndef TFP_FOV_LOCK_H
#define TFP_FOV_LOCK_H
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TFP_FOV_LOCK_GLIDE_MS 80.0f
#define TFP_FOV_LOCK_STALE_MS 250.0   /* longer than this since the last frame: what is on screen is the game's */
#define TFP_FOV_LOCK_DONE .003f       /* radians (0.17 degrees): a glide out ends here */

/* "Aim field of view" (round 20, docs/design/specs/2026-11-22-locked-view-design.md "Round 20"): what full aim shows.
 * TFP_FOV_AIM_ZOOM: the game's own lens (its zoom level), what "Zoom when aiming" on did. TFP_FOV_AIM_LOCKED: the
 * locked value, what it did off. A value above 0: that field of view in radians, whatever the zoom level, and with
 * the lock off too. */
#define TFP_FOV_AIM_ZOOM (-1.0f)
#define TFP_FOV_AIM_LOCKED 0.0f

typedef struct TfpFovLock { float shown; double at; bool aimed; float glide; } TfpFovLock; /* shown: 0 while nothing is changed; aimed: the last step's `aiming` (round 42); glide: the time constant since aim began, ms (0: TFP_FOV_LOCK_GLIDE_MS; round 42) */

static inline void tfp_fov_lock_reset(TfpFovLock *l) { l->shown = 0; l->at = 0; l->aimed = false; l->glide = 0; }
static inline bool tfp_fov_lock_active(const TfpFovLock *l) { return l->shown > 0; }
/* One frame. `wanted`: the locked field of view in radians, 0 with the lock off. `aim`: TFP_FOV_AIM_* or a chosen
 * value. `applied`: what the game (or the mod's own zoom policy) would show this frame. `tick`: milliseconds.
 * Returns the field of view to show. */
static inline float tfp_fov_lock_glide(TfpFovLock *l, float wanted, float aim, bool aiming, float applied, double tick, bool entered) {
  if (!(applied > 0) || !isfinite(applied) || !isfinite(tick)) { tfp_fov_lock_reset(l); return applied; }
  const bool chosen = aiming && aim > 0 && isfinite(aim);
  const bool on = wanted > 0 || chosen;
  const bool fresh = l->shown > 0 && tick >= l->at && tick-l->at <= TFP_FOV_LOCK_STALE_MS;
  if (!on && !fresh) { tfp_fov_lock_reset(l); return applied; }
  const float target = chosen ? aim : wanted > 0 && !(aiming && !(aim == TFP_FOV_AIM_LOCKED)) ? wanted : applied;
  float shown = fresh ? l->shown : applied; /* start from what was on screen */
  if (entered) shown = target; /* round 42: "Aim-in time" 0, the frame aim begins: the aim's value at once */
  else if (fresh && tick > l->at) shown += (target-shown)*(1-expf(-(float)(tick-l->at)/(l->glide > 0 ? l->glide : TFP_FOV_LOCK_GLIDE_MS)));
  if (!on && fabsf(shown-applied) < TFP_FOV_LOCK_DONE) { tfp_fov_lock_reset(l); return applied; }
  l->shown = shown; l->at = tick;
  return shown;
}
/* Round 42 (not in retail): `enter_ms`, the "Aim-in time". 0 takes the aim's value in the frame `aiming` begins, with no
 * glide. Between 0 and 180 the glide's time constant from that frame until aim ends is TFP_FOV_LOCK_GLIDE_MS scaled by
 * enter_ms/180 (a zoom level in aim glides that fast too); aim-out and the lock switched glide as before. 180 or more:
 * the call of round 41 and before, to the bit. */
static inline float tfp_fov_lock_step_enter(TfpFovLock *l, float wanted, float aim, bool aiming, float applied, double tick, float enter_ms) {
  const bool edge = aiming && !l->aimed;
  if (edge) l->glide = enter_ms > 0 && enter_ms < 180 ? TFP_FOV_LOCK_GLIDE_MS*(enter_ms/180.0f) : 0;
  else if (!aiming) l->glide = 0;
  const float shown = tfp_fov_lock_glide(l, wanted, aim, aiming, applied, tick, edge && !(enter_ms > 0));
  l->aimed = aiming;
  return shown;
}
/* `instant`: "Aim-in time" 0; false: 180, the call of round 41 and before, to the bit. */
static inline float tfp_fov_lock_step_in(TfpFovLock *l, float wanted, float aim, bool aiming, float applied, double tick, bool instant) {
  return tfp_fov_lock_step_enter(l, wanted, aim, aiming, applied, tick, instant ? 0.0f : 180.0f);
}
static inline float tfp_fov_lock_step_aim(TfpFovLock *l, float wanted, float aim, bool aiming, float applied, double tick) {
  return tfp_fov_lock_step_in(l, wanted, aim, aiming, applied, tick, false);
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

/* ---- Full aim with 'zoom when aiming': the optic's own zoom, not the unzoom gate's (round 20,
 * docs/design/specs/2026-11-23-gate-flip.md "Round 20").
 * Round 20c: what src/fov_helper.c shows from this is behind the test switch zoom_steady.txt (off: the game's value,
 * as feel.51). The notes and the factor are kept with the switch off too: the "Aim:" line reports them. Review 27
 * found the goal can still pop at a zoom-level change while the gate flips and can sharpen a real unzoom at a wall;
 * no log has yet shown the gate flipping in full aim (docs/design/specs/2026-11-22-locked-view-design.md "Round 20c").
 * The kind-6 evaluator turns its lens toward a target (game+0x1286a46..0x1286d09): the third-person field of view
 * divided by the optic's magnification for the selected zoom level (game+0x759b10: one of three floats of the
 * optic's data, +0x74 / +0x78 / +0x7c), or, while the unzoom gate is set (the avatar's packed bit 0, or request+0x8f),
 * the third-person field of view itself. The lens moves toward it at clamp(10 * |target - lens|, 0.1, 2) rad/s, times
 * the magnification while the gate is clear, by 1 while it is set (game+0x1286ca8, game+0x173cb30). The hooked setter
 * call of the frame hands the camera a value composed from that lens.
 * So a gate that flips pushes the lens wide at up to 2 rad/s on every set frame, and a clear frame pulls it back only
 * by a share of what is left; the lens rests 0.2 / magnification rad off its target and moves 2 rad/s * frame time
 * every frame, the same angle at every zoom level. The narrower the zoom, the larger that angle is on screen.
 * What the lock glides toward in full aim, while the gate flips, is therefore the value the game would hand over
 * with the gate clear and the lens at rest on the optic's own target: the composition of the setter's value from the
 * lens (a factor in the tangent: 60 degrees of lens gave 61.5 in the user's log) applied to that target.
 *
 * Only while the gate really flips (review 24, BLOCKING 1): a set stretch shorter than TFP_AIM_FLIP_SET_MS with
 * clear frames on both sides, the clear frames between no longer than TFP_AIM_FLIP_CLEAR_MS. A gate held set for
 * longer is the game's own unzoom (a wall right in front, a ledge), and a lone clear frame inside it changes nothing:
 * the game's value is shown as it is. A set stretch that begins with the lens at rest on the optic's own target is
 * held from its first frame, for as long as it could still be a flip (TFP_AIM_FLIP_SET_MS): what is on screen is
 * then the optic's own zoom already, so holding it is no change, and the first frames of a burst of flips do not
 * pulse. The cost: the game's own unzoom from a steady aim shows that much later, and then eased. The correction comes in and goes out over TFP_AIM_EASE_IN_MS / _OUT_MS (eased, in
 * the magnification), never in one frame; with it out the goal is the game's value, to the bit.
 *
 * A value a frame behind its lens (review 24, IMPORTANT 2): the hooked call's value comes from a camera getter or a
 * blend that was not traced, so it may be built from the lens of the frame before. The goal while held is therefore
 * not this frame's value scaled by this frame's lens (a lag would double the pulse) but the optic's target composed
 * with a factor read where a lag cannot matter: on a quiet frame, the lens moved under TFP_AIM_QUIET of itself from
 * the note before it to the note after it (so a note a frame late cannot fool it either). Before any quiet frame, a
 * running mean of the factor in the logarithm (TFP_AIM_K_MS): a lag makes the factor of one frame too large and of
 * the next too small by the same ratio, and the mean is between. The "Aim:" line gives the factor's range in the
 * second: a constant factor while the lens moves says the value is this frame's; a spread that follows the gate says
 * it lags. */
#define TFP_AIM_LENS_FRESH_MS 100.0   /* the evaluator's numbers are this frame's: no older than this */
#define TFP_AIM_FLIP_SET_MS 175.0     /* a set stretch shorter than this is a flip (8 frames at 60 fps: 133 ms); longer, the game's own unzoom */
#define TFP_AIM_FLIP_CLEAR_MS 100.0   /* the clear frames on each side of it no further than this */
#define TFP_AIM_EASE_IN_MS 120.0      /* the correction comes in over this ... */
#define TFP_AIM_EASE_OUT_MS 250.0     /* ... and goes out over this (the game's own unzoom is under way by then: a slower hand-over) */
#define TFP_AIM_K_MS 500.0            /* the factor's running mean (of two frames running), while no quiet frame has been seen */
#define TFP_AIM_REST 1e-3f            /* the lens at rest on its target: this near it, as a share of it */
#define TFP_AIM_QUIET 1e-4f           /* a quiet frame: the lens moved under this share of itself over three notes */
typedef struct TfpAimLens {
  double note_at, clear_at;           /* ms: the last note; the last clear note */
  float lens, clear, before;          /* the lens noted last; the optic's own target, as the last clear note had it; the lens of the note before */
  bool gated, began, running;         /* the last note's gate; a run of notes is going; the note before was in it (no more than TFP_AIM_LENS_FRESH_MS before) */
  double set_begin, set_end, set_length; /* the stretch set now (its first set note), and the last one that ended (its first clear note, its length) */
  bool set_bounded, last_bounded;     /* the stretch began after a clear note of the same run */
  bool rest, set_from_rest;           /* the last note was clear with the lens at rest on its target; the stretch set now began right after such a note */
  float candidate, candidate_lens;    /* the frame before's factor, where its lens was the note before's too (0: not), and that lens */
  float quiet, mean, factor, last;    /* the composition factor: from the last quiet frame (0: none), the running mean of its logarithm, this frame's (0: none), the frame before's logarithm */
  bool mean_known, last_known; double mean_at, ease_at;
  float ease;                         /* 0 the game's value, 1 the optic's own, linear in time */
} TfpAimLens;
static inline void tfp_aim_lens_reset(TfpAimLens *a) { memset(a, 0, sizeof *a); }
static inline bool tfp_aim_lens_angle(float r) { return isfinite(r) && r > .0002f && r < 2.96f; }
/* One evaluation of the kind-6 lens: `gated` the gate as the evaluator took it, `target` the lens target it chose,
 * `lens` the lens after its step (what the request keeps at +0x84). `tick` milliseconds. */
static inline void tfp_aim_lens_note(TfpAimLens *a, bool gated, float target, float lens, double tick) {
  if (!tfp_aim_lens_angle(target) || !tfp_aim_lens_angle(lens) || !isfinite(tick)) { tfp_aim_lens_reset(a); return; }
  a->running = a->began && tick >= a->note_at && tick-a->note_at <= TFP_AIM_LENS_FRESH_MS;
  if (!a->running) { a->set_end = 0; a->last_bounded = a->set_bounded = a->set_from_rest = false; if (gated) a->set_begin = tick; }
  else if (gated != a->gated) {
    if (gated) { a->set_begin = tick; a->set_bounded = true; a->set_from_rest = a->rest; }
    else { a->set_length = tick-a->set_begin; a->set_end = tick; a->last_bounded = a->set_bounded; }
  }
  a->before = a->running ? a->lens : 0;
  a->began = true; a->note_at = tick; a->lens = lens; a->gated = gated;
  if (!gated) { a->clear = target; a->clear_at = tick; }
  a->rest = !gated && fabsf(lens-target) <= TFP_AIM_REST*target;
}
/* The gate flips: the last set stretch that ended was short and had clear frames on both sides, and the frames
 * since are either clear and close to it, or a set stretch, still short, that began close to it. Or a set stretch,
 * still short, began with the lens at rest on the optic's own target. */
static inline bool tfp_aim_lens_flipping(const TfpAimLens *a) {
  if (!a->running && !a->gated) return false;
  if (a->gated && a->set_from_rest && a->note_at-a->set_begin < TFP_AIM_FLIP_SET_MS) return true; /* held from its first frame */
  if (!(a->set_end > 0) || !a->last_bounded || !(a->set_length < TFP_AIM_FLIP_SET_MS)) return false;
  if (a->gated) return a->set_bounded && a->note_at-a->set_begin < TFP_AIM_FLIP_SET_MS && a->set_begin-a->set_end < TFP_AIM_FLIP_CLEAR_MS;
  return a->note_at-a->set_end < TFP_AIM_FLIP_CLEAR_MS;
}
/* What full aim's zoom glides toward this frame. `applied`: the frame's value as the game (and the helper) would
 * hand it over; `previous_call`: the time of the hooked call before this one (a note before it is another frame's).
 * Called once a hooked frame in full aim (it keeps the ease and the factor). `*from_clear`: the optic's own zoom is
 * in the goal. */
static inline float tfp_aim_lens_goal(TfpAimLens *a, float applied, double tick, double previous_call, bool *from_clear) {
  *from_clear = false; a->factor = 0;
  if (!(applied > 0) || !(applied < 3.1f) || !isfinite(tick)) { a->ease = 0; a->ease_at = 0; return applied; }
  const bool noted = a->began && tick >= a->note_at && tick-a->note_at <= TFP_AIM_LENS_FRESH_MS;
  if (!(noted && a->note_at > previous_call)) { a->last_known = false; a->candidate = 0; }
  else { /* this frame's note: the factor */
    const float factor = tanf(applied*.5f)/tanf(a->lens*.5f);
    if (isfinite(factor) && factor > .5f && factor < 2) {
      a->factor = factor;
      /* the frame before was quiet if its note, the one before it and this one have the same lens: then its value
       * was built from that lens whether it is this frame's, the frame before's, or the note a frame late */
      if (a->candidate > 0 && fabsf(a->lens-a->candidate_lens) <= TFP_AIM_QUIET*a->lens) a->quiet = a->candidate;
      a->candidate = a->before > 0 && fabsf(a->lens-a->before) <= TFP_AIM_QUIET*a->lens ? factor : 0; a->candidate_lens = a->lens;
      const float log_one = logf(factor), log_factor = a->last_known ? (log_one+a->last)*.5f : log_one;
      a->last = log_one; a->last_known = true;
      if (!a->mean_known || !(tick >= a->mean_at) || tick-a->mean_at > TFP_FOV_LOCK_STALE_MS) a->mean = log_factor;
      else a->mean += (log_factor-a->mean)*(1-expf(-(float)(tick-a->mean_at)/(float)TFP_AIM_K_MS));
      a->mean_known = true; a->mean_at = tick;
    }
  }
  const float factor = a->quiet > 0 ? a->quiet : a->mean_known ? expf(a->mean) : 0;
  const bool want = noted && a->clear_at > 0 && factor > 0 && tfp_aim_lens_flipping(a);
  if (!(a->ease_at > 0) || !(tick >= a->ease_at) || tick-a->ease_at > TFP_FOV_LOCK_STALE_MS) a->ease = 0;
  else {
    const double by = tick-a->ease_at;
    a->ease = want ? fminf(1, a->ease+(float)(by/TFP_AIM_EASE_IN_MS)) : fmaxf(0, a->ease-(float)(by/TFP_AIM_EASE_OUT_MS));
  }
  if (want && a->gated && a->set_from_rest && a->note_at == a->set_begin) a->ease = 1; /* from rest on the optic: the hold is what is on screen */
  a->ease_at = tick;
  if (!(a->ease > 0) || !(factor > 0)) return applied;
  const float own = tanf(a->clear*.5f)*factor, game = tanf(applied*.5f);
  if (!(own > 0) || !isfinite(own) || !(game > 0)) return applied;
  const float e = a->ease*a->ease*(3-2*a->ease);
  const float goal = 2*atanf(expf((1-e)*logf(game)+e*logf(own)));
  if (!(goal > 0) || !(goal < 3.1f)) return applied;
  *from_clear = true;
  return goal;
}
#endif
