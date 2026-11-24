/* The "Aim:" line (round 20c; docs/design/specs/2026-11-22-locked-view-design.md "Round 20c", docs/guides/AIM-TEST.md).
 * One line for what the two round 20 branches each logged: the sight and the gun (round20-ads-distance) and the
 * unzoom gate, the optic's lens and the field of view frame by frame (round20-zoom-shake). It is written:
 *   - "in": once an aim has settled (TFP_AIM_SETTLE_MS in);
 *   - "changed": when the zoom level, the sight, the editor, the ADS placement or the option changed and settled again;
 *   - "second": once a second while aiming, with what the frames of that second did;
 *   - "out": at the end, with how long it lasted and each zoom level seen (its field of view, the game's sight and
 *     depth there, the optic's magnification).
 * Each of the first three gives the sight and the gun as they are now and the frames since the line before:
 *
 *   Aim: second: "LAS-5 Scythe"; fov 61.5 shown 61.5 (Aim field of view: the game's zoom); sight 0.173 m (near clip),
 *     wanted 0.000: pull 0.000, closer 0.000 (ADS fwd +0.000), near clip 0.173 m; depth 0.210 m; editor closed |
 *     116 frames: gate set 58, changed 115 times; optic target 7.50-7.50 deg (x12.00-x12.00 of 90.0); lens 7.50-8.60;
 *     game 7.73-8.86, shown 7.73-7.73 (corrected 0); step 0.001 %; game/lens x1.0272-x1.0272 (116); rig depth
 *     0.0000-0.2500 m (4), step 0.2400 m
 *
 * (one line in the log; broken here.) What it answers (docs/guides/AIM-TEST.md): whether the unzoom gate flips in full
 * aim ("gate set N, changed M times"); the optic's zoom levels (the optic's own lens target and its magnification of
 * the third-person field of view) and the game's sight distance (its near clip) and depth per level (the "out" line);
 * and whether the hooked field-of-view value lags the lens ("game/lens": one value while the lens moves, it is built
 * from this frame's lens; a spread that comes and goes with the gate, it lags a frame). "depth" is request+0x90
 * blended as the evaluator blends it (the socket's distance before the eye), plus what the held depth added. Kept
 * under 700 characters (src/mod_view.c say()). Game thread, once a tick; no engine reads. */
#ifndef TFP_AIM_LOG_H
#define TFP_AIM_LOG_H
#include "depth_steady.h"
#include "fov_helper.h"
#include "fov_lock.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TFP_AIM_SETTLE_MS 300.0
#define TFP_AIM_SECOND_MS 1000.0
#define TFP_AIM_ZOOMS 5
#define TFP_AIM_LINE 700

typedef struct TfpAimSample {
  bool full_ads;
  const char *weapon;            /* NULL: not known */
  float game_fov, shown_fov;     /* radians; NAN: not read */
  float aim_field;               /* src/fov_lock.h TFP_FOV_AIM_* or the chosen radians */
  float sight;                   /* m: the game's near clip of a sight; NAN or <= 0.05: none seen */
  float wanted;                  /* m: locked_view.json ads_sight_cm / 100 */
  float pull, closer, ads_forward, near_clip; /* m (not `near`: a macro of windows.h) */
  float depth;                   /* m: the game's depth in full aim; NAN: not read */
  bool editor; const char *editor_action;
  double now_ms;
} TfpAimSample;
/* A zoom level seen: told by the game's field of view (half a degree apart is another). */
typedef struct TfpAimLevel { float fov, sight, depth, optic_least, optic_most; } TfpAimLevel;
/* The rig's depth in the frames of full aim since the line before (from the "Steady:" second, src/depth_steady.h). */
typedef struct TfpAimRig { uint32_t frames; float least, most, step; } TfpAimRig;
typedef struct TfpAimLog {
  bool in, told, pending, mixed; double since, pending_at, line_at; /* mixed: something changed since the line before */
  float told_fov, told_sight, told_forward, told_aim; bool told_editor; char told_action[16];
  TfpAimLevel levels[TFP_AIM_ZOOMS]; unsigned level_count;
  TfpFovAimSecond fov; TfpAimRig rig; /* the frames since the line before */
  float third_person;                  /* degrees: the game's third-person field of view, as last read */
} TfpAimLog;

static inline void tfp_aim_log_reset(TfpAimLog *l) { memset(l, 0, sizeof *l); }
static inline void tfp_aim_range(float *least, float *most, uint32_t had, uint32_t more, float add_least, float add_most) {
  if (!more || !isfinite(add_least) || !isfinite(add_most)) return;
  if (!had || add_least < *least) *least = add_least;
  if (!had || add_most > *most) *most = add_most;
}
/* The field-of-view helper's frames (tfp_fov_aim_take), added to those since the line before. */
static inline void tfp_aim_log_add_fov(TfpAimLog *l, const TfpFovAimSecond *f) {
  TfpFovAimSecond *a = &l->fov;
  if (f->third_person > 0) a->third_person = l->third_person = f->third_person;
  if (!f->frames && !f->noted) return;
  tfp_aim_range(&a->clear_least, &a->clear_most, a->noted - a->gated, f->noted - f->gated, f->clear_least, f->clear_most);
  tfp_aim_range(&a->lens_least, &a->lens_most, a->noted, f->noted, f->lens_least, f->lens_most);
  tfp_aim_range(&a->game_least, &a->game_most, a->frames, f->frames, f->game_least, f->game_most);
  tfp_aim_range(&a->shown_least, &a->shown_most, a->frames, f->frames, f->shown_least, f->shown_most);
  tfp_aim_range(&a->factor_least, &a->factor_most, a->factored, f->factored, f->factor_least, f->factor_most);
  a->frames += f->frames; a->noted += f->noted; a->gated += f->gated; a->changes += f->changes; a->corrected += f->corrected; a->factored += f->factored;
  if (f->step_most > a->step_most) a->step_most = f->step_most;
}
/* The rig's depth in full aim of a "Steady:" second (tfp_pose_steady_take), added likewise. */
static inline void tfp_aim_log_add_rig(TfpAimLog *l, const TfpSteadySecond *s) {
  if (!s->aim_frames) return;
  tfp_aim_range(&l->rig.least, &l->rig.most, l->rig.frames, s->aim_frames, s->aim_depth.least, s->aim_depth.most);
  l->rig.frames += s->aim_frames;
  if (s->aim_step > l->rig.step) l->rig.step = s->aim_step;
}
/* The frames since the line before, as words (the second half of the line). */
static inline void tfp_aim_counts(const TfpFovAimSecond *f, const TfpAimRig *r, char *out, size_t size) {
  const double deg = 57.29577951308232;
  char lens[200], rig[80], factor[64];
  const unsigned clear = f->noted - f->gated;
  if (!f->noted) snprintf(lens, sizeof lens, "lens not noted (zoom level not known)");
  else if (!clear) snprintf(lens, sizeof lens, "gate set %u of %u noted, changed %u times; optic target not seen; lens %.2f-%.2f",
                            f->gated, f->noted, f->changes, f->lens_least*deg, f->lens_most*deg);
  else {
    const double t = f->third_person, low = f->clear_most > 0 ? t/(f->clear_most*deg) : 0, high = f->clear_least > 0 ? t/(f->clear_least*deg) : 0;
    snprintf(lens, sizeof lens, "gate set %u of %u noted, changed %u times; optic target %.2f-%.2f deg (x%.2f-x%.2f of %.1f); lens %.2f-%.2f",
             f->gated, f->noted, f->changes, f->clear_least*deg, f->clear_most*deg, low, high, t, f->lens_least*deg, f->lens_most*deg);
  }
  if (f->factored) snprintf(factor, sizeof factor, "game/lens x%.4f-x%.4f (%u)", f->factor_least, f->factor_most, f->factored);
  else snprintf(factor, sizeof factor, "game/lens not seen");
  if (r->frames) snprintf(rig, sizeof rig, "rig depth %.4f-%.4f m (%u), step %.4f m", (double)r->least, (double)r->most, r->frames, (double)r->step);
  else snprintf(rig, sizeof rig, "rig depth not worked out");
  if (!f->frames) snprintf(out, size, "0 frames counted; %s; %s", factor, rig);
  else snprintf(out, size, "%u frames: %s; game %.2f-%.2f, shown %.2f-%.2f (corrected %u); step %.3f %%; %s; %s",
                f->frames, lens, f->game_least*deg, f->game_most*deg, f->shown_least*deg, f->shown_most*deg, f->corrected,
                f->step_most*100.0, factor, rig);
}
static inline const char *tfp_aim_metres(float v, const char *none, char *out, size_t size) {
  if (isfinite(v)) snprintf(out, size, "%.3f m", v); else snprintf(out, size, "%s", none);
  return out;
}
static inline const char *tfp_aim_degrees(float radians, char *out, size_t size) {
  if (isfinite(radians) && radians > 0) snprintf(out, size, "%.1f", radians*57.29577951308232); else snprintf(out, size, "not read");
  return out;
}
/* The zoom level the sample is at: found by its field of view, or added; its sight, depth and (`optic`: the frames
 * since the line before were all at this level) the optic's own lens target noted. */
static inline void tfp_aim_log_level(TfpAimLog *l, const TfpAimSample *s, bool optic) {
  const float fov = s->game_fov;
  if (!isfinite(fov) || !(fov > 0)) return;
  TfpAimLevel *v = NULL;
  for (unsigned i = 0; i < l->level_count; i++) if (fabsf(l->levels[i].fov - fov) < .0087f) v = &l->levels[i]; /* half a degree */
  if (!v) {
    if (l->level_count >= TFP_AIM_ZOOMS) return;
    v = &l->levels[l->level_count++]; v->fov = fov; v->sight = v->depth = NAN; v->optic_least = v->optic_most = 0;
  }
  if (isfinite(s->sight) && s->sight > .05f) v->sight = s->sight;
  if (isfinite(s->depth)) v->depth = s->depth;
  const TfpFovAimSecond *f = &l->fov;
  if (optic && f->noted > f->gated && f->clear_least > 0) {
    if (!(v->optic_least > 0) || f->clear_least < v->optic_least) v->optic_least = f->clear_least;
    if (f->clear_most > v->optic_most) v->optic_most = f->clear_most;
  }
}
static inline void tfp_aim_log_line(TfpAimLog *l, const TfpAimSample *s, const char *what, char *out, size_t size) {
  char shown[24], game[24], sight[48], depth[24], clip[24], weapon[72], field[48], editor[40], counts[440];
  const float on_screen = isfinite(s->shown_fov) && s->shown_fov > 0 ? s->shown_fov : s->game_fov;
  tfp_aim_degrees(on_screen, shown, sizeof shown);
  if (s->weapon && s->weapon[0]) snprintf(weapon, sizeof weapon, "\"%.60s\"", s->weapon); else snprintf(weapon, sizeof weapon, "no weapon known");
  if (isfinite(s->sight) && s->sight > .05f) snprintf(sight, sizeof sight, "%.3f m (near clip)", s->sight);
  else snprintf(sight, sizeof sight, "not known");
  if (s->editor) snprintf(editor, sizeof editor, "open on \"%.20s\"", s->editor_action ? s->editor_action : "?"); else snprintf(editor, sizeof editor, "closed");
  tfp_aim_counts(&l->fov, &l->rig, counts, sizeof counts);
  snprintf(out, size, "Aim: %s: %s; fov %s shown %s (Aim field of view: %s); sight %s, wanted %.3f: pull %.3f, closer %.3f (ADS fwd %+.3f), "
           "near clip %s; depth %s; editor %s | %s",
           what, weapon, tfp_aim_degrees(s->game_fov, game, sizeof game), shown, tfp_fov_aim_said(s->aim_field, field, sizeof field), sight,
           isfinite(s->wanted) ? s->wanted : 0, isfinite(s->pull) ? s->pull : 0, isfinite(s->closer) ? s->closer : 0,
           isfinite(s->ads_forward) ? s->ads_forward : 0, tfp_aim_metres(s->near_clip, "not read", clip, sizeof clip),
           tfp_aim_metres(s->depth, "not read", depth, sizeof depth), editor, counts);
  memset(&l->fov, 0, sizeof l->fov); memset(&l->rig, 0, sizeof l->rig); l->fov.third_person = l->third_person; l->line_at = s->now_ms; l->mixed = false;
}
static inline bool tfp_aim_log_differs(const TfpAimLog *l, const TfpAimSample *s) {
  const bool fov = isfinite(s->game_fov) != isfinite(l->told_fov) || (isfinite(s->game_fov) && fabsf(s->game_fov - l->told_fov) > .0087f);
  const bool sight = isfinite(s->sight) != isfinite(l->told_sight) || (isfinite(s->sight) && fabsf(s->sight - l->told_sight) > .005f);
  const bool forward = fabsf((isfinite(s->ads_forward) ? s->ads_forward : 0) - l->told_forward) > .001f;
  const bool action = s->editor && strncmp(s->editor_action ? s->editor_action : "", l->told_action, sizeof l->told_action - 1);
  return fov || sight || forward || s->editor != l->told_editor || action || s->aim_field != l->told_aim;
}
static inline void tfp_aim_log_tell(TfpAimLog *l, const TfpAimSample *s) {
  l->told = true; l->pending = false;
  l->told_fov = s->game_fov; l->told_sight = s->sight; l->told_forward = isfinite(s->ads_forward) ? s->ads_forward : 0;
  l->told_aim = s->aim_field; l->told_editor = s->editor;
  snprintf(l->told_action, sizeof l->told_action, "%s", s->editor && s->editor_action ? s->editor_action : "");
}
/* One tick. true: `out` holds a line to write. Feed the frames first (tfp_aim_log_add_fov, tfp_aim_log_add_rig). */
static inline bool tfp_aim_log_step(TfpAimLog *l, const TfpAimSample *s, char *out, size_t size) {
  if (!s->full_ads) {
    if (!l->in) { memset(&l->fov, 0, sizeof l->fov); memset(&l->rig, 0, sizeof l->rig); return false; } /* the hip's frames are no aim's */
    char levels[300] = "", counts[440];
    for (unsigned i = 0; i < l->level_count; i++) {
      const TfpAimLevel *v = &l->levels[i];
      char sight[16], depth[16], optic[48] = "";
      if (isfinite(v->sight)) snprintf(sight, sizeof sight, "%.3f", v->sight); else snprintf(sight, sizeof sight, "-");
      if (isfinite(v->depth)) snprintf(depth, sizeof depth, "%.3f", v->depth); else snprintf(depth, sizeof depth, "-");
      if (v->optic_least > 0 && l->third_person > 0)
        snprintf(optic, sizeof optic, ", optic %.2f deg x%.2f", v->optic_least*57.29577951308232, l->third_person/(v->optic_least*57.29577951308232));
      const size_t used = strlen(levels);
      snprintf(levels + used, sizeof levels - used, "%s%.1f: sight %s, depth %s%s", i ? "; " : "", v->fov*57.29577951308232, sight, depth, optic);
    }
    tfp_aim_counts(&l->fov, &l->rig, counts, sizeof counts);
    const double held = s->now_ms > l->since ? (s->now_ms - l->since)/1000 : 0;
    if (!l->told) snprintf(out, size, "Aim: out after %.1f s (a tap: not settled) | %s", held, counts);
    else snprintf(out, size, "Aim: out after %.1f s; zoom levels (game fov: sight m, depth m, optic): %s | %s", held, levels[0] ? levels : "none read", counts);
    tfp_aim_log_reset(l);
    return true;
  }
  if (!l->in) {
    const TfpFovAimSecond f = l->fov; const TfpAimRig r = l->rig; /* the frames of this aim's first tick */
    tfp_aim_log_reset(l); l->in = true; l->since = s->now_ms; l->fov = f; l->rig = r;
    return false;
  }
  if (!l->told) {
    if (s->now_ms - l->since < TFP_AIM_SETTLE_MS) return false;
    tfp_aim_log_level(l, s, true); tfp_aim_log_tell(l, s); tfp_aim_log_line(l, s, "in", out, size);
    return true;
  }
  if (!tfp_aim_log_differs(l, s)) l->pending = false;
  else if (!l->pending) { l->pending = true; l->pending_at = s->now_ms; l->mixed = true; }
  else if (s->now_ms - l->pending_at >= TFP_AIM_SETTLE_MS) {
    tfp_aim_log_level(l, s, false); tfp_aim_log_tell(l, s); tfp_aim_log_line(l, s, "changed", out, size);
    return true;
  }
  if (s->now_ms - l->line_at >= TFP_AIM_SECOND_MS) {
    if (!l->pending) tfp_aim_log_level(l, s, !l->mixed); /* settled: the level's sight and depth as they are now */
    tfp_aim_log_line(l, s, "second", out, size);
    return true;
  }
  return false;
}
#endif
