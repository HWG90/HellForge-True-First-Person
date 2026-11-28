/* Round 53: the near plane and the gun. The arithmetic and the "Clip:" line only; no engine reads here.
 *
 * A tester on 0.6.0-feel.59: "some weapon meshes dissapear even if you tweak with the clearance ... stocks dissapear,
 * frames go invisible and shots show the moving bolt inside the guns". Two videos (frame-stepped): the Anti-Materiel
 * Rifle is drawn whole at the hip; in full aim, shown wide (about 90 degrees, no zoom, no scope picture), only its
 * front half is drawn: the scope, the receiver and the stock are gone, standing still and firing, the parts nearest
 * the eye first. A cut plane a short way in front of the eye. Nothing of the mod hides a weapon mesh (the head
 * visibility's weapon-tree rows change the shadow pass only, src/head_visibility.c shadow_only).
 *
 * FULL AIM, what this round changes. The game seats the gun with its sight on the view axis and, for a sight, sets the
 * near clip to the sight's own distance (src/near_helper.c: seen live at 0.098 m for the BR-14 Adjudicator at its 30
 * degree level, 0.115 m, and 0.22 / 0.35 m for the two levels of one scope; other weapons and levels keep the game's
 * 0.05 m). That value is made for the game's own lens, where the near plane is on screen only a few centimetres round
 * the sight line. With "Aim field of view" on "Your locked view" or "A chosen value" the same plane is drawn at 50 to
 * 120 degrees and everything nearer than it that is now on screen is cut: the scope body, the receiver, the stock.
 * So in full aim shown wider than the game's lens the near clip is the game's value times
 * tan(game lens / 2) / tan(shown / 2) (tfp_clip_ads_scale, tfp_clip_ads_near): the near plane is on screen as far from
 * the view axis as at the game's lens. Never under the game's own 0.05 m, never past the game's value; at the game's
 * lens the game's value to the bit. Where the game's value is already 0.05 m nothing changes. The two fields of view
 * are the frame's own, read by the near helper in its own call (src/near_helper.h tfp_near_set_fov_source,
 * src/fov_helper.h tfp_fov_frame_pair).
 * The floor: 0.098 m at 30 degrees shown at 90 would scale to 0.026 m and is 0.05 m, so the plane is on screen to
 * 0.050 m from the axis where the game's lens showed 0.026 m. Less is cut than before; what is nearer than 5 cm to the
 * eye still is, as everywhere in the game.
 *
 * THE HIP AND NORMAL AIM are unchanged (main's): "Near clip distance" and the per-weapon clearances are handed to the
 * game as they are set. tfp_clip_first_depth is a figure for the log only: from the placement's x (right) and z (up)
 * alone, the depth at which a gun's top would leave the lower side of the view. It knows no rotation, sway, sprint
 * pose, reload or recoil, and nothing of the mesh; nothing in the mod acts on it.
 *
 * The "Clip:" line says per weapon and view what the game's setter was handed, so a tester's log shows the cut. */
#ifndef TFP_WEAPON_CLIP_H
#define TFP_WEAPON_CLIP_H
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TFP_CLIP_GAME_M .05f        /* the game's own near clip */
#define TFP_CLIP_RISE_M .04f        /* over the sight line: an optic's housing, the walking bob */
#define TFP_CLIP_HALF_M .05f        /* half the widest gun */
#define TFP_CLIP_ASPECT (32.0f/9)   /* the widest screen: the side comes onto the screen nearest there */
#define TFP_CLIP_PI 3.14159265358979f
/* Shown this near the game's lens (radians; 0.17 degrees, where the field-of-view lock's glide ends: TFP_FOV_LOCK_DONE)
 * is the game's lens: the lock glides onto the game's zoom without ever landing on it to the bit. */
#define TFP_CLIP_SAME_FOV .003f

static inline bool tfp_clip_fov(float fov) { return isfinite(fov) && fov > 0 && fov < TFP_CLIP_PI; }

/* A figure for the log, computed and not measured: the depth from the eye (m, along the view) at which a gun under a
 * sight line `x` right and `z` up of the view axis (the placement alone; down is negative) would first be on screen,
 * at the vertical field of view `fov` (radians). 0: from the eye on, or nothing to go by. */
static inline float tfp_clip_first_depth(float x, float z, float fov) {
  if (!tfp_clip_fov(fov) || !isfinite(x) || !isfinite(z)) return 0;
  const float t = tanf(fov/2), top = z+TFP_CLIP_RISE_M, side = fabsf(x)-TFP_CLIP_HALF_M;
  const float down = top >= 0 ? 0 : -top/t, across = side <= 0 ? 0 : side/(TFP_CLIP_ASPECT*t);
  return down > across ? down : across;
}
/* Full aim: what the game's near clip is scaled by for the near plane to be on screen as far from the view axis as at
 * the game's own lens `game_fov`, at the shown field of view. 1: the game's lens or more zoom, or nothing to go by. */
static inline float tfp_clip_ads_scale(float game_fov, float shown_fov) {
  if (!tfp_clip_fov(game_fov) || !tfp_clip_fov(shown_fov) || !(shown_fov > game_fov + TFP_CLIP_SAME_FOV)) return 1;
  return tanf(game_fov/2)/tanf(shown_fov/2);
}
/* The near clip in full aim: the game's `native` by `scale`, never under the game's own 0.05 (a native value at or
 * under it is left). A scale that is not in (0, 1): the game's value to the bit. */
static inline float tfp_clip_ads_near(float native, float scale) {
  if (!isfinite(native) || !(scale > 0) || !(scale < 1) || native <= TFP_CLIP_GAME_M) return native;
  const float near_m = native*scale;
  return near_m < TFP_CLIP_GAME_M ? TFP_CLIP_GAME_M : near_m;
}
/* How far from the view axis (m, up or down) the near plane at `near_m` is on screen. */
static inline float tfp_clip_on_screen(float near_m, float fov) {
  return tfp_clip_fov(fov) && isfinite(near_m) && near_m > 0 ? near_m*tanf(fov/2) : 0;
}

/* ---- the "Clip:" line ----
 *   Clip: weapon=e8a3969f81abec5f name="BR-14 Adjudicator" view=ads fov=90.0 (game 30.0) near in force 0.050 m (game
 *         0.098 m, scaled by 0.268); computed: the near plane is on screen to 0.050 m from the view axis (the game's
 *         value at the game's lens: 0.026 m)
 *   Clip: weapon=... view=hip fov=90.0 (game 70.0) near in force 0.150 m (game 0.050 m, setting 0.150 m); computed
 *         from the placement alone, not measured: the gun's top 0.080 m under and 0.100 m beside the view axis, on
 *         screen from 0.080 m, so the near plane would cut 0.080 to 0.150 m of it on screen
 * Read from the helpers: "near in force" is what the near helper last handed the game's own setter and "game" what the
 * game asked for in that call (its statistics); fov is what the field-of-view helper last handed the game's setter and
 * what the game asked for (degrees; "fov not read" without them); "scaled by" what the near helper's last full-aim
 * call scaled by; "setting" the distance of this view from the settings (off: the game's own).
 * Computed, and said so in the line: everything after "computed". The hip's figures come from the placement's x and z
 * and know no rotation, movement or mesh. */
typedef enum TfpClipView { TFP_CLIP_HIP, TFP_CLIP_AIM, TFP_CLIP_ADS } TfpClipView;
typedef struct TfpClipSample {
  TfpClipView view;
  const char *key, *name;         /* NULL: "?" */
  bool has_fov;                   /* the field-of-view helper's pair was read */
  float fov, game_fov;            /* radians: what the setter was handed, what the game asked for */
  bool has_near;                  /* the near helper's statistics were read */
  float game_near, near;          /* m: what the game asked for, what its setter was handed */
  float scale;                    /* full aim: what the near helper's last call scaled by (1: nothing) */
  float wanted;                   /* m: the setting for this view; 0: off (the game's own) */
  float x, z;                     /* the placement in force (hip and normal aim) */
} TfpClipSample;

static inline int tfp_clip_line(const TfpClipSample *s, char *out, size_t size) {
  static const char *const views[] = {"hip", "aim", "ads"};
  const float deg = 180/TFP_CLIP_PI;
  char head[240], fov[48], force[64];
  if (s->has_fov) snprintf(fov, sizeof fov, "fov=%.1f (game %.1f)", s->fov*deg, s->game_fov*deg); else snprintf(fov, sizeof fov, "fov not read");
  snprintf(head, sizeof head, "Clip: weapon=%s name=\"%s\" view=%s %s near in force", s->key ? s->key : "?", s->name ? s->name : "?",
           views[s->view <= TFP_CLIP_ADS ? s->view : 0], fov);
  if (s->has_near) snprintf(force, sizeof force, "%.3f m", s->near); else snprintf(force, sizeof force, "not read");
  if (s->view == TFP_CLIP_ADS) {
    char game[72] = "", computed[160] = "";
    if (s->has_near && s->scale > 0 && s->scale < 1) snprintf(game, sizeof game, " (game %.3f m, scaled by %.3f)", s->game_near, s->scale);
    else if (s->has_near) snprintf(game, sizeof game, " (game %.3f m)", s->game_near);
    if (s->has_near && s->has_fov)
      snprintf(computed, sizeof computed, "; computed: the near plane is on screen to %.3f m from the view axis (the game's value at the "
               "game's lens: %.3f m)", tfp_clip_on_screen(s->near, s->fov), tfp_clip_on_screen(s->game_near, s->game_fov));
    return snprintf(out, size, "%s %s%s%s", head, force, game, computed);
  }
  char setting[48], game[32] = "", computed[260] = "";
  if (!(s->wanted > TFP_CLIP_GAME_M)) snprintf(setting, sizeof setting, "setting off");
  else snprintf(setting, sizeof setting, "setting %.3f m", s->wanted);
  if (s->has_near) snprintf(game, sizeof game, "game %.3f m, ", s->game_near);
  if (s->has_fov) {
    const float first = tfp_clip_first_depth(s->x, s->z, s->fov), under = -(s->z+TFP_CLIP_RISE_M), beside = fabsf(s->x)-TFP_CLIP_HALF_M;
    char top[96], cut[96];
    if (first > 0) snprintf(top, sizeof top, "the gun's top %.3f m under and %.3f m beside the view axis, on screen from %.3f m", under > 0 ? under : 0,
                            beside > 0 ? beside : 0, first);
    else snprintf(top, sizeof top, "the gun's top on the view axis, on screen from the eye on");
    if (!s->has_near) snprintf(cut, sizeof cut, "the near plane was not read");
    else if (s->near > first+.0005f) snprintf(cut, sizeof cut, "so the near plane would cut %.3f to %.3f m of it on screen", first, s->near);
    else snprintf(cut, sizeof cut, "so the near plane would cut nothing of it on screen");
    snprintf(computed, sizeof computed, "; computed from the placement alone, not measured: %s, %s", top, cut);
  }
  return snprintf(out, size, "%s %s (%s%s)%s", head, force, game, setting, computed);
}
/* Whether `b` is worth a line after `a`: another weapon or view, a millimetre of a distance, half a degree. */
static inline bool tfp_clip_changed(const TfpClipSample *a, const TfpClipSample *b) {
  const float mm = .001f, half = .5f*TFP_CLIP_PI/180;
  const char *ka = a->key ? a->key : "", *kb = b->key ? b->key : "";
  return a->view != b->view || strcmp(ka, kb) != 0 || a->has_near != b->has_near || a->has_fov != b->has_fov || fabsf(a->near-b->near) >= mm ||
         fabsf(a->scale-b->scale) >= .01f ||
         fabsf(a->game_near-b->game_near) >= mm || fabsf(a->wanted-b->wanted) >= mm || fabsf(a->x-b->x) >= mm || fabsf(a->z-b->z) >= mm ||
         fabsf(a->fov-b->fov) >= half || fabsf(a->game_fov-b->game_fov) >= half;
}
#endif
