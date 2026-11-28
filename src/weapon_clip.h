/* Round 53: the near plane and the gun. The arithmetic and the "Clip:" line only; no engine reads here.
 *
 * A tester on 0.6.0-feel.59: "some weapon meshes dissapear even if you tweak with the clearance ... stocks dissapear,
 * frames go invisible and shots show the moving bolt inside the guns". Nothing of the mod hides a weapon mesh (the
 * head visibility's weapon-tree rows change the shadow pass only, src/head_visibility.c shadow_only). What cuts a gun
 * is the camera's near plane, and what puts the cut on screen is the field of view:
 *
 *  - FULL AIM. The game seats the gun with its sight node on the view axis and sets the near clip to the sight's own
 *    distance (src/ads_seat.h: 0.066 to 0.35 m; 0.098 m for the BR-14 Adjudicator in the user's log). Every gun has
 *    0.15 to 0.27 m of stock and receiver behind its sight node (game data, tests/weapon_clip_test.c), so all of that
 *    is cut away, by the game, in every full aim. At the game's own lens (30 degrees and less) the near plane shows
 *    0.026 m under the sight line: the rear sight and nothing else. With "Aim field of view" on "Your locked view"
 *    or "A chosen value" the same near plane is drawn at 50 to 120 degrees and shows 0.046 to 0.17 m under the sight
 *    line: the receiver sliced open at the sight, the stock gone, the bolt moving inside. No clearance setting
 *    reaches it ("Full ADS always uses the game's distance").
 *    So in full aim the near clip is scaled by tan(game lens/2)/tan(shown/2) (tfp_clip_ads_scale, tfp_clip_ads_near):
 *    what the near plane shows on screen stays what the game's own lens shows, never nearer than the game's own
 *    0.05 m and never further than the game's value. At the game's lens the scale is 1 and the value is the game's
 *    to the bit.
 *  - THE HIP AND NORMAL AIM. The mod moves the gun off the axis by the placement (x right, z up: 0.15 right and 0.12
 *    down as shipped) and sets the near clip to "Near clip distance" (15 cm as shipped) or the weapon's own clearance.
 *    The gun reaches back past the eye, so the near plane always cuts it; whether the cut is seen depends on where the
 *    gun comes onto the screen: at the depth its top (the sight line and TFP_CLIP_RISE_M over it, for an optic and the
 *    walking bob) leaves the frustum's lower side, top/tan(fov/2). At the game's own 45 degrees that is 0.19 m, past
 *    every shipped setting; at the lock's 80 to 120 degrees it is 0.095 to 0.046 m, inside the shipped 15 cm, and
 *    larger clearances cut more.
 *    So at the hip and in normal aim the near clip in force is the setting held at that depth (tfp_clip_hip_near),
 *    never under the game's own 0.05 m. A setting nearer than the gun stands as it is.
 *
 * The "Clip:" line says per weapon and view what was in force, so a tester's log proves it. */
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

/* The depth from the eye (m, along the view) at which a gun under a sight line `x` right and `z` up of the view axis
 * (the placement; down is negative) is first on screen, at the vertical field of view `fov` (radians). 0: from the
 * eye on, or nothing to go by. */
static inline float tfp_clip_first_depth(float x, float z, float fov) {
  if (!tfp_clip_fov(fov) || !isfinite(x) || !isfinite(z)) return 0;
  const float t = tanf(fov/2), top = z+TFP_CLIP_RISE_M, side = fabsf(x)-TFP_CLIP_HALF_M;
  const float down = top >= 0 ? 0 : -top/t, across = side <= 0 ? 0 : side/(TFP_CLIP_ASPECT*t);
  return down > across ? down : across;
}
/* The near clip at the hip or in normal aim (m): `wanted` held at the depth the gun is first on screen at, never under
 * the game's own. A setting that is off (the game's 0.05 or less), not a number, or nothing to go by: as it came. */
static inline float tfp_clip_hip_near(float wanted, float x, float z, float fov) {
  if (!(wanted > TFP_CLIP_GAME_M) || !tfp_clip_fov(fov) || !isfinite(x) || !isfinite(z)) return wanted;
  const float first = tfp_clip_first_depth(x, z, fov);
  if (first >= wanted) return wanted;
  return first < TFP_CLIP_GAME_M ? TFP_CLIP_GAME_M : first;
}
/* Full aim: what the game's near clip is scaled by for the near plane to show on screen what it shows at the game's
 * own lens `game_fov`, at the shown field of view. 1: the game's lens or more zoom, or nothing to go by. */
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
 *   Clip: weapon=e8a3969f81abec5f name="BR-14 Adjudicator" view=hip fov=90.0 (game 45.0) near in force 0.080 m (game
 *         0.050 m, setting 0.150 m held at 0.080 m); by placement the gun's top is 0.080 m under and 0.100 m beside the
 *         view axis, first on screen 0.080 m from the eye; cut on screen: none
 *   Clip: weapon=... view=ads fov=90.0 (game 30.0) near in force 0.050 m (game 0.098 m: its sight); the gun is on the
 *         view axis; cut on screen at the near plane: 0.050 m under the sight line (the game's own lens: 0.026 m)
 * near in force: what the near helper last handed the game's own setter (its statistics); game: what the game asked
 * for in that call. fov: the field of view shown and the game's own, degrees. "by placement": from the placement in
 * force, not from the mesh. A cut that is on screen reads "the gun from A to B m". */
typedef enum TfpClipView { TFP_CLIP_HIP, TFP_CLIP_AIM, TFP_CLIP_ADS } TfpClipView;
typedef struct TfpClipSample {
  TfpClipView view;
  const char *key, *name;         /* NULL: "?" */
  float fov, game_fov;            /* radians */
  bool has_near;                  /* the near helper's statistics were read */
  float game_near, near;          /* m: what the game asked for, what it was handed */
  float wanted;                   /* m: the setting for this view; 0: off (the game's own) */
  float x, z;                     /* the placement in force (hip and normal aim) */
} TfpClipSample;

static inline int tfp_clip_line(const TfpClipSample *s, char *out, size_t size) {
  static const char *const views[] = {"hip", "aim", "ads"};
  const float deg = 180/TFP_CLIP_PI;
  char head[200], force[64];
  snprintf(head, sizeof head, "Clip: weapon=%s name=\"%s\" view=%s fov=%.1f (game %.1f) near in force", s->key ? s->key : "?",
           s->name ? s->name : "?", views[s->view <= TFP_CLIP_ADS ? s->view : 0], s->fov*deg, s->game_fov*deg);
  if (s->has_near) snprintf(force, sizeof force, "%.3f m", s->near); else snprintf(force, sizeof force, "not read");
  if (s->view == TFP_CLIP_ADS) {
    char game[48] = "";
    if (s->has_near) snprintf(game, sizeof game, " (game %.3f m: its sight)", s->game_near);
    return snprintf(out, size, "%s %s%s; the gun is on the view axis; cut on screen at the near plane: %.3f m under the sight line (the game's "
                    "own lens: %.3f m)", head, force, game, tfp_clip_on_screen(s->has_near ? s->near : 0, s->fov),
                    tfp_clip_on_screen(s->has_near ? s->game_near : 0, s->game_fov));
  }
  const float first = tfp_clip_first_depth(s->x, s->z, s->fov), held = tfp_clip_hip_near(s->wanted, s->x, s->z, s->fov);
  char setting[64], game[32] = "", top[96], cut[64] = "none";
  if (!(s->wanted > TFP_CLIP_GAME_M)) snprintf(setting, sizeof setting, "setting off");
  else if (held < s->wanted) snprintf(setting, sizeof setting, "setting %.3f m held at %.3f m", s->wanted, held);
  else snprintf(setting, sizeof setting, "setting %.3f m", s->wanted);
  if (s->has_near) snprintf(game, sizeof game, "game %.3f m, ", s->game_near);
  const float under = -(s->z+TFP_CLIP_RISE_M), beside = fabsf(s->x)-TFP_CLIP_HALF_M;
  if (first > 0) snprintf(top, sizeof top, "%.3f m under and %.3f m beside the view axis", under > 0 ? under : 0, beside > 0 ? beside : 0);
  else snprintf(top, sizeof top, "on the view axis");
  if (!s->has_near) snprintf(cut, sizeof cut, "not known");
  else if (s->near > first+.0005f) {
    if (first > 0) snprintf(cut, sizeof cut, "the gun from %.3f to %.3f m", first, s->near);
    else snprintf(cut, sizeof cut, "the gun to %.3f m", s->near);
  }
  return snprintf(out, size, "%s %s (%s%s); by placement the gun's top is %s, first on screen %.3f m from the eye; cut on screen: %s", head,
                  force, game, setting, top, first, cut);
}
/* Whether `b` is worth a line after `a`: another weapon or view, a millimetre of a distance, half a degree. */
static inline bool tfp_clip_changed(const TfpClipSample *a, const TfpClipSample *b) {
  const float mm = .001f, half = .5f*TFP_CLIP_PI/180;
  const char *ka = a->key ? a->key : "", *kb = b->key ? b->key : "";
  return a->view != b->view || strcmp(ka, kb) != 0 || a->has_near != b->has_near || fabsf(a->near-b->near) >= mm ||
         fabsf(a->game_near-b->game_near) >= mm || fabsf(a->wanted-b->wanted) >= mm || fabsf(a->x-b->x) >= mm || fabsf(a->z-b->z) >= mm ||
         fabsf(a->fov-b->fov) >= half || fabsf(a->game_fov-b->game_fov) >= half;
}
#endif
