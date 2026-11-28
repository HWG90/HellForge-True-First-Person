/* Round 53: the near plane and the gun (src/weapon_clip.h): the full-aim near clip scaled to the shown field of view,
 * the "Clip:" line, and the line's computed figure (the depth a placed gun would first be on screen at; the log only).
 * The figures are the game data's (filediver, HellForge 0.5 build 25480438): the sight node and the mesh bounds of the
 * AR-23 Liberator, the BR-14 Adjudicator, the R-63 Diligence and the JAR-5 Dominator. No game. */
#include "../src/weapon_clip.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static bool near(double a, double b, double e) { return fabs(a - b) <= e; }
#define DEG(d) ((float)((d)*3.14159265358979/180))

int main(void) {
  /* ---- the depth a gun under its sight line is first on screen at ---- */
  /* the shipped placement (0.15 right, 0.12 down) at the lock's 90 degrees: the gun's top (the sight line and
   * TFP_CLIP_RISE_M over it) is 0.08 m under the view axis, so on screen from 0.08 m */
  CHECK(near(tfp_clip_first_depth(.15f, -.12f, DEG(90)), .08, 1e-5));
  CHECK(near(tfp_clip_first_depth(.15f, -.12f, DEG(80)), .08/tan(DEG(40)), 1e-5));   /* the lock's default: 0.095 m */
  CHECK(near(tfp_clip_first_depth(.15f, -.12f, DEG(45)), .08/tan(DEG(22.5)), 1e-5)); /* the game's own hip lens: 0.193 m */
  CHECK(near(tfp_clip_first_depth(.15f, -.12f, DEG(120)), .08/tan(DEG(60)), 1e-5));  /* the widest lock: 0.046 m */
  /* the user's Adjudicator (0.0997, 0.1000, -0.0995): forward is no part of it */
  CHECK(near(tfp_clip_first_depth(.0997f, -.0995f, DEG(90)), .0595, 1e-5));
  /* a gun on the view axis (full aim, or a placement that high) is on screen from the eye on */
  CHECK(tfp_clip_first_depth(0, 0, DEG(90)) == 0 && tfp_clip_first_depth(.04f, -.03f, DEG(90)) == 0 && tfp_clip_first_depth(-.05f, .05f, DEG(90)) == 0);
  /* as high as the eye but to the side: the side decides, at the widest screen (32:9) */
  CHECK(near(tfp_clip_first_depth(.15f, -.03f, DEG(90)), .10/TFP_CLIP_ASPECT, 1e-5) && near(tfp_clip_first_depth(.15f, .05f, DEG(90)), .10/TFP_CLIP_ASPECT, 1e-5));
  CHECK(near(tfp_clip_first_depth(.30f, 0, DEG(90)), (.30 - TFP_CLIP_HALF_M)/TFP_CLIP_ASPECT, 1e-5));
  CHECK(near(tfp_clip_first_depth(-.30f, 0, DEG(90)), (.30 - TFP_CLIP_HALF_M)/TFP_CLIP_ASPECT, 1e-5));
  /* no field of view to go by: nothing is known (0: on screen from the eye on is the careful answer) */
  CHECK(tfp_clip_first_depth(.15f, -.12f, 0) == 0 && tfp_clip_first_depth(.15f, -.12f, NAN) == 0 && tfp_clip_first_depth(.15f, -.12f, DEG(180)) == 0);
  CHECK(tfp_clip_first_depth(NAN, -.12f, DEG(90)) == 0 && tfp_clip_first_depth(.15f, NAN, DEG(90)) == 0);

  /* ---- full aim: the game's near clip is its sight's distance, made for the game's lens ---- */
  /* the Adjudicator in the user's log: near 0.098 m at the game's 30 degrees. The same cut drawn at 90 degrees shows
   * 0.098 m of the sliced receiver under the sight line where the game's lens showed 0.026 m. */
  CHECK(near(tfp_clip_ads_scale(DEG(30), DEG(30)), 1, 0) && tfp_clip_ads_scale(DEG(30), DEG(20)) == 1); /* the game's lens, or more zoom: the game's */
  CHECK(tfp_clip_ads_scale(DEG(30), DEG(30.1)) == 1 && tfp_clip_ads_scale(DEG(30), DEG(30) + TFP_CLIP_SAME_FOV) == 1); /* the lock's glide ending on the game's zoom: the game's */
  CHECK(tfp_clip_ads_scale(DEG(30), DEG(30.3)) < 1 && tfp_clip_ads_scale(DEG(30), DEG(30.3)) > .98f);                 /* past it: a step of under 2 %, eased by the helper */
  CHECK(near(tfp_clip_ads_scale(DEG(30), DEG(90)), tan(DEG(15)), 1e-6));
  CHECK(near(tfp_clip_ads_scale(DEG(30), DEG(50)), tan(DEG(15))/tan(DEG(25)), 1e-6));
  CHECK(tfp_clip_ads_scale(0, DEG(90)) == 1 && tfp_clip_ads_scale(DEG(30), 0) == 1 && tfp_clip_ads_scale(NAN, DEG(90)) == 1 &&
        tfp_clip_ads_scale(DEG(30), NAN) == 1 && tfp_clip_ads_scale(DEG(30), DEG(180)) == 1 && tfp_clip_ads_scale(DEG(181), DEG(190)) == 1);
  CHECK(tfp_clip_ads_near(.098f, 1) == .098f);                                   /* the game's lens: its value to the bit */
  CHECK(near(tfp_clip_ads_near(.098f, tfp_clip_ads_scale(DEG(30), DEG(50))), .0563, 1e-4));  /* a chosen 50: the same 0.026 m on screen */
  CHECK(near(tfp_clip_on_screen(.0563f, DEG(50)), tfp_clip_on_screen(.098f, DEG(30)), 1e-4));
  CHECK(tfp_clip_ads_near(.098f, tfp_clip_ads_scale(DEG(30), DEG(90))) == TFP_CLIP_GAME_M);  /* the locked 90: the game's own 0.05, no nearer */
  CHECK(tfp_clip_ads_near(.35f, tfp_clip_ads_scale(DEG(10), DEG(90))) == TFP_CLIP_GAME_M);   /* a scope's far zoom level */
  CHECK(near(tfp_clip_ads_near(.35f, tfp_clip_ads_scale(DEG(30), DEG(40))), .35*tan(DEG(15))/tan(DEG(20)), 1e-5));
  CHECK(tfp_clip_ads_near(.05f, .3f) == .05f && tfp_clip_ads_near(.03f, .3f) == .03f);       /* the game's own or under it: left */
  CHECK(tfp_clip_ads_near(.2f, 0) == .2f && tfp_clip_ads_near(.2f, -1) == .2f && tfp_clip_ads_near(.2f, NAN) == .2f &&
        tfp_clip_ads_near(.2f, 1.5f) == .2f && isnan(tfp_clip_ads_near(NAN, .5f)));          /* not a scale: the game's */
  /* how much of the cut is on screen at the near plane, from the view axis down */
  CHECK(near(tfp_clip_on_screen(.098f, DEG(30)), .0263, 1e-4) && near(tfp_clip_on_screen(.098f, DEG(90)), .098, 1e-6));
  CHECK(tfp_clip_on_screen(.098f, 0) == 0 && tfp_clip_on_screen(NAN, DEG(90)) == 0);

  /* ---- the game data: how far each gun reaches behind its sight node, which full aim puts on the near plane ---- */
  static const struct { const char *name; float sight_y, sight_z, rear_y, top_z; } guns[] = {
    {"AR-23 Liberator", -.1000f, .2101f, -.3708f, .1928f}, {"BR-14 Adjudicator", -.0900f, .1947f, -.3576f, .2031f},
    {"R-63 Diligence", .0021f, .1728f, -.2498f, .1556f}, {"JAR-5 Dominator", -.1000f, .2008f, -.2532f, .2018f}};
  for (size_t i = 0; i < sizeof guns/sizeof *guns; i++) {
    const float behind = guns[i].sight_y - guns[i].rear_y, over = guns[i].top_z - guns[i].sight_z;
    CHECK(behind > .15f && behind < .28f);       /* 0.15 to 0.27 m of stock and receiver behind the sight: all of it cut in full aim */
    CHECK(over < TFP_CLIP_RISE_M && over > -.02f); /* the mesh's top is within 2 cm of the sight line: the rise covers it */
  }

  /* ---- the "Clip:" line: what was read is said as read, what is computed is said to be computed ---- */
  char line[560];
  TfpClipSample ads = {.view = TFP_CLIP_ADS, .key = "e8a3969f81abec5f", .name = "BR-14 Adjudicator", .has_fov = true, .fov = DEG(90), .game_fov = DEG(30),
                       .has_near = true, .game_near = .098f, .near = .05f, .scale = tanf(DEG(15))};
  tfp_clip_line(&ads, line, sizeof line);
  CHECK(!strcmp(line, "Clip: weapon=e8a3969f81abec5f name=\"BR-14 Adjudicator\" view=ads fov=90.0 (game 30.0) near in force 0.050 m (game 0.098 m, "
                      "scaled by 0.268); computed: the near plane is on screen to 0.050 m from the view axis (the game's value at the game's "
                      "lens: 0.026 m)"));
  ads.fov = DEG(30), ads.near = .098f, ads.scale = 1; /* the game's zoom: nothing scaled, and the line says so by saying nothing */
  tfp_clip_line(&ads, line, sizeof line);
  CHECK(strstr(line, "view=ads fov=30.0 (game 30.0) near in force 0.098 m (game 0.098 m); computed: the near plane is on screen to 0.026 m from the "
                     "view axis (the game's value at the game's lens: 0.026 m)") && !strstr(line, "scaled"));
  ads.has_fov = false; /* the field-of-view helper not read: no field of view, and nothing computed from one */
  tfp_clip_line(&ads, line, sizeof line);
  CHECK(strstr(line, "view=ads fov not read near in force 0.098 m (game 0.098 m)") && !strstr(line, "computed"));
  ads.has_fov = true, ads.has_near = false;
  tfp_clip_line(&ads, line, sizeof line);
  CHECK(strstr(line, "view=ads fov=30.0 (game 30.0) near in force not read") && !strstr(line, "computed") && !strstr(line, "(game 0."));
  ads.has_near = true, ads.fov = DEG(90), ads.near = .05f, ads.scale = tanf(DEG(15));
  TfpClipSample hip = {.view = TFP_CLIP_HIP, .key = "e8a3969f81abec5f", .name = "BR-14 Adjudicator", .has_fov = true, .fov = DEG(90), .game_fov = DEG(70),
                       .has_near = true, .game_near = .05f, .near = .15f, .scale = 1, .wanted = .15f, .x = .15f, .z = -.12f};
  tfp_clip_line(&hip, line, sizeof line);
  CHECK(!strcmp(line, "Clip: weapon=e8a3969f81abec5f name=\"BR-14 Adjudicator\" view=hip fov=90.0 (game 70.0) near in force 0.150 m (game 0.050 m, "
                      "setting 0.150 m); computed from the placement alone, not measured: the gun's top 0.080 m under and 0.100 m beside the view "
                      "axis, on screen from 0.080 m, so the near plane would cut 0.080 to 0.150 m of it on screen"));
  CHECK(!strstr(line, "held")); /* nothing holds the hip's near clip: the setting is what the game is handed */
  hip.near = .05f, hip.wanted = 0;
  tfp_clip_line(&hip, line, sizeof line);
  CHECK(strstr(line, "near in force 0.050 m (game 0.050 m, setting off); computed from the placement alone, not measured:") &&
        strstr(line, "so the near plane would cut nothing of it on screen"));
  hip.x = 0, hip.z = 0;
  tfp_clip_line(&hip, line, sizeof line);
  CHECK(strstr(line, "the gun's top on the view axis, on screen from the eye on, so the near plane would cut 0.000 to 0.050 m of it on screen"));
  hip.has_near = false;
  tfp_clip_line(&hip, line, sizeof line);
  CHECK(strstr(line, "near in force not read (setting off); computed from the placement alone, not measured:") && strstr(line, "the near plane was not read"));
  hip.has_fov = false;
  tfp_clip_line(&hip, line, sizeof line);
  CHECK(strstr(line, "view=hip fov not read near in force not read (setting off)") && !strstr(line, "computed"));
  hip.has_fov = hip.has_near = true;
  TfpClipSample none = {.view = TFP_CLIP_AIM, .has_fov = true, .fov = DEG(90), .game_fov = DEG(45), .has_near = true, .game_near = .05f, .near = .05f, .x = .1f, .z = -.1f};
  tfp_clip_line(&none, line, sizeof line);
  CHECK(strstr(line, "Clip: weapon=? name=\"?\" view=aim fov=90.0 (game 45.0) near in force 0.050 m (game 0.050 m, setting off)"));
  char small[24];
  tfp_clip_line(&hip, small, sizeof small); /* a short buffer: cut, terminated */
  CHECK(strlen(small) == sizeof small - 1);
  { /* the longest line fits the binder's buffer */
    TfpClipSample longest = hip;
    longest.key = "0123456789abcdef", longest.name = "A weapon with a name as long as a name gets here, forty-eight", longest.near = 123.456f, longest.wanted = 123.456f;
    longest.x = 123.456f, longest.z = -123.456f, longest.game_near = 123.456f;
    CHECK(tfp_clip_line(&longest, line, sizeof line) < (int)sizeof line);
  }
  /* a change worth a line: the view, a millimetre of the near plane, half a degree, the scale; not the noise under them */
  TfpClipSample other = ads;
  CHECK(!tfp_clip_changed(&ads, &other));
  other.near += .0004f; CHECK(!tfp_clip_changed(&ads, &other));
  other.near += .001f; CHECK(tfp_clip_changed(&ads, &other));
  other = ads; other.fov += DEG(.2); CHECK(!tfp_clip_changed(&ads, &other));
  other.fov += DEG(1); CHECK(tfp_clip_changed(&ads, &other));
  other = ads; other.view = TFP_CLIP_HIP; CHECK(tfp_clip_changed(&ads, &other));
  other = ads; other.key = "0294f1b2a3523d8d"; CHECK(tfp_clip_changed(&ads, &other));
  other = ads; other.has_near = false; CHECK(tfp_clip_changed(&ads, &other));
  other = ads; other.has_fov = false; CHECK(tfp_clip_changed(&ads, &other));
  other = ads; other.wanted = .1f; CHECK(tfp_clip_changed(&ads, &other));
  other = ads; other.scale = 1; CHECK(tfp_clip_changed(&ads, &other));

  printf("weapon clip: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
