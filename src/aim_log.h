/* The "Aim:" line (round 20; docs/design/specs/2026-11-22-locked-view-design.md "Round 20"): per full aim, what the
 * player saw through the sights. One line when an aim has settled (300 ms in), one when something of it changed and
 * settled again (a zoom level, the sight, the placement editor, the ADS placement, the option), one at the end with
 * how long it lasted and the zoom levels seen. A calm aim writes two lines. Game thread, once a tick; no engine reads.
 *
 *   Aim: in: "LAS-5 Scythe"; field of view 61.5 degrees (Aim field of view: the game's zoom; the game asked for 61.5);
 *     the game's sight at 0.173 m (its near clip), sight distance in aim 0.100 m: the gun 0.073 m closer (pull 0.073 m,
 *     ADS placement forward +0.000 m), near clip 0.100 m; the game's depth 0.210 m; placement editor closed
 *   Aim: out after 16.2 s; zoom levels seen (the game's field of view): 61.5 degrees
 *
 * (one line in the log; broken here.) "the game's depth" is request+0x90 blended as the evaluator blends it (the
 * socket's distance before the eye), worked out in the frames of full aim. */
#ifndef TFP_AIM_LOG_H
#define TFP_AIM_LOG_H
#include "fov_lock.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TFP_AIM_SETTLE_MS 300.0
#define TFP_AIM_ZOOMS 6

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
typedef struct TfpAimLog {
  bool in, told, pending; double since, pending_at;
  float told_fov, told_sight, told_forward, told_aim; bool told_editor; char told_action[16];
  float zooms[TFP_AIM_ZOOMS]; unsigned zoom_count;
} TfpAimLog;

static inline void tfp_aim_log_reset(TfpAimLog *l) { memset(l, 0, sizeof *l); }
static inline const char *tfp_aim_metres(float v, const char *none, char *out, size_t size) {
  if (isfinite(v)) snprintf(out, size, "%.3f m", v); else snprintf(out, size, "%s", none);
  return out;
}
static inline const char *tfp_aim_degrees(float radians, char *out, size_t size) {
  if (isfinite(radians) && radians > 0) snprintf(out, size, "%.1f", radians*57.29577951308232); else snprintf(out, size, "not read");
  return out;
}
static inline void tfp_aim_log_zoom(TfpAimLog *l, float fov) {
  if (!isfinite(fov) || !(fov > 0)) return;
  for (unsigned i = 0; i < l->zoom_count; i++) if (fabsf(l->zooms[i] - fov) < .0087f) return; /* half a degree */
  if (l->zoom_count < TFP_AIM_ZOOMS) l->zooms[l->zoom_count++] = fov;
}
static inline void tfp_aim_log_line(const TfpAimSample *s, const char *what, char *out, size_t size) {
  char shown[24], game[24], sight[64], depth[24], clip[24], weapon[80], field[48], editor[48];
  const float on_screen = isfinite(s->shown_fov) && s->shown_fov > 0 ? s->shown_fov : s->game_fov;
  if (isfinite(on_screen) && on_screen > 0) snprintf(shown, sizeof shown, "%.1f degrees", on_screen*57.29577951308232); else snprintf(shown, sizeof shown, "not read");
  if (s->weapon && s->weapon[0]) snprintf(weapon, sizeof weapon, "\"%.60s\"", s->weapon); else snprintf(weapon, sizeof weapon, "no weapon known");
  if (isfinite(s->sight) && s->sight > .05f) snprintf(sight, sizeof sight, "at %.3f m (its near clip)", s->sight);
  else snprintf(sight, sizeof sight, "not known (no near clip of a sight seen)");
  if (s->editor) snprintf(editor, sizeof editor, "open on \"%.20s\"", s->editor_action ? s->editor_action : "?"); else snprintf(editor, sizeof editor, "closed");
  snprintf(out, size, "Aim: %s: %s; field of view %s (Aim field of view: %s; the game asked for %s); the game's sight %s, sight distance in aim %.3f m: "
           "the gun %.3f m closer (pull %.3f m, ADS placement forward %+.3f m), near clip %s; the game's depth %s; placement editor %s",
           what, weapon, shown, tfp_fov_aim_said(s->aim_field, field, sizeof field), tfp_aim_degrees(s->game_fov, game, sizeof game), sight,
           isfinite(s->wanted) ? s->wanted : 0, isfinite(s->closer) ? s->closer : 0, isfinite(s->pull) ? s->pull : 0,
           isfinite(s->ads_forward) ? s->ads_forward : 0, tfp_aim_metres(s->near_clip, "not read", clip, sizeof clip),
           tfp_aim_metres(s->depth, "not read", depth, sizeof depth), editor);
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
  tfp_aim_log_zoom(l, s->game_fov);
}
/* One tick. true: `out` holds a line to write. */
static inline bool tfp_aim_log_step(TfpAimLog *l, const TfpAimSample *s, char *out, size_t size) {
  if (!s->full_ads) {
    if (!l->in) return false;
    char zooms[160] = "";
    for (unsigned i = 0; i < l->zoom_count; i++) {
      const size_t used = strlen(zooms);
      snprintf(zooms + used, sizeof zooms - used, "%s%.1f", i ? ", " : "", l->zooms[i]*57.29577951308232);
    }
    const double held = s->now_ms > l->since ? (s->now_ms - l->since)/1000 : 0;
    if (!l->told) snprintf(out, size, "Aim: out after %.1f s (a tap: not settled)", held);
    else snprintf(out, size, "Aim: out after %.1f s; zoom levels seen (the game's field of view): %s degrees", held, zooms[0] ? zooms : "none read");
    tfp_aim_log_reset(l);
    return true;
  }
  if (!l->in) { tfp_aim_log_reset(l); l->in = true; l->since = s->now_ms; return false; }
  if (!l->told) {
    if (s->now_ms - l->since < TFP_AIM_SETTLE_MS) return false;
    tfp_aim_log_line(s, "in", out, size); tfp_aim_log_tell(l, s);
    return true;
  }
  if (!tfp_aim_log_differs(l, s)) { l->pending = false; return false; }
  if (!l->pending) { l->pending = true; l->pending_at = s->now_ms; return false; }
  if (s->now_ms - l->pending_at < TFP_AIM_SETTLE_MS) return false;
  tfp_aim_log_line(s, "changed", out, size); tfp_aim_log_tell(l, s);
  return true;
}
#endif
