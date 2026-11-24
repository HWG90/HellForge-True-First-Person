#include "../src/fov_lock.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
#define DEG(d) ((float)((d)*3.14159265358979/180))

int main(void) {
  TfpFovLock l; tfp_fov_lock_reset(&l);
  const float game = DEG(60), want = DEG(80);
  double t = 1000;
  /* Off, never on: the game's value, untouched. */
  CHECK(tfp_fov_lock_step(&l, 0, false, false, game, t) == game && !tfp_fov_lock_active(&l));
  /* Switched on: the first frame is what was on screen, then a glide to the locked value (80 ms time constant). */
  float shown = tfp_fov_lock_step(&l, want, false, false, game, t += 16);
  CHECK(shown == game && tfp_fov_lock_active(&l));
  float worst = 0, last = shown;
  for (int i = 0; i < 40; ++i) { shown = tfp_fov_lock_step(&l, want, false, false, game, t += 16); if (shown-last > worst) worst = shown-last; CHECK(shown >= last && shown <= want); last = shown; }
  CHECK(fabsf(shown-want) < DEG(.05f));
  CHECK(worst < DEG(4.0f));                     /* no frame of it is a pop: 20 degrees in steps under 4 */
  /* Whatever the game asks for (another weapon, another camera): the locked value. */
  CHECK(fabsf(tfp_fov_lock_step(&l, want, false, false, DEG(45), t += 16)-want) < DEG(.05f));
  /* Zoom when aiming: in full aim the game's (zoomed) value is the target, and back afterwards; both glide. */
  last = tfp_fov_lock_step(&l, want, true, true, DEG(30), t += 16);
  CHECK(last < want && last > DEG(70));
  for (int i = 0; i < 40; ++i) last = tfp_fov_lock_step(&l, want, true, true, DEG(30), t += 16);
  CHECK(fabsf(last-DEG(30)) < DEG(.05f));
  CHECK(fabsf(tfp_fov_lock_step(&l, want, false, true, DEG(30), t += 16)-DEG(30)) < DEG(10)); /* the option off: back toward the lock */
  for (int i = 0; i < 40; ++i) last = tfp_fov_lock_step(&l, want, false, true, DEG(30), t += 16);
  CHECK(fabsf(last-want) < DEG(.05f));
  /* Switched off: it glides out to the game's value as it glided in, then nothing is changed any more. */
  last = want; worst = 0; int frames = 0;
  for (; frames < 60; ++frames) {
    shown = tfp_fov_lock_step(&l, 0, false, false, game, t += 16);
    if (last-shown > worst) worst = last-shown;
    CHECK(shown <= last+1e-6f && shown >= game);
    last = shown;
    if (!tfp_fov_lock_active(&l)) break;
  }
  CHECK(frames > 5 && frames < 40 && shown == game && worst < DEG(4.0f));
  CHECK(tfp_fov_lock_step(&l, 0, false, false, DEG(50), t += 16) == DEG(50));
  /* A pause longer than 250 ms (the view was not drawn): no glide from an old value, in either direction. */
  for (int i = 0; i < 40; ++i) tfp_fov_lock_step(&l, want, false, false, game, t += 16);
  CHECK(tfp_fov_lock_step(&l, 0, false, false, game, t += 400) == game && !tfp_fov_lock_active(&l));
  for (int i = 0; i < 40; ++i) tfp_fov_lock_step(&l, want, false, false, game, t += 16);
  CHECK(tfp_fov_lock_step(&l, want, false, false, DEG(45), t += 400) == DEG(45));
  /* Time that does not move, or goes back: the same answer, nothing breaks. */
  shown = tfp_fov_lock_step(&l, want, false, false, game, t);
  CHECK(isfinite(shown) && tfp_fov_lock_step(&l, want, false, false, game, t) == shown);
  CHECK(isfinite(tfp_fov_lock_step(&l, want, false, false, game, t-50)));
  /* Values that are not a field of view are passed through. */
  tfp_fov_lock_reset(&l);
  CHECK(isnan(tfp_fov_lock_step(&l, want, false, false, NAN, t += 16)) && !tfp_fov_lock_active(&l));

  /* Round 20, "Aim field of view": what full aim shows. The game's zoom and the locked view are the two old answers of
   * the toggle, word for word; a chosen value is new, and holds with the lock off too (it is its own option). */
  const float scope = DEG(25.7f), chosen = DEG(45);
  tfp_fov_lock_reset(&l);
  for (int i = 0; i < 40; ++i) tfp_fov_lock_step_aim(&l, want, TFP_FOV_AIM_ZOOM, false, game, t += 16);
  for (int i = 0; i < 40; ++i) last = tfp_fov_lock_step_aim(&l, want, TFP_FOV_AIM_ZOOM, true, scope, t += 16);
  CHECK(fabsf(last-scope) < DEG(.05f));                                             /* the game's zoom: as zoom when aiming on */
  for (int i = 0; i < 40; ++i) last = tfp_fov_lock_step_aim(&l, want, TFP_FOV_AIM_LOCKED, true, scope, t += 16);
  CHECK(fabsf(last-want) < DEG(.05f));                                              /* your locked view: as the toggle off */
  for (int i = 0; i < 40; ++i) last = tfp_fov_lock_step_aim(&l, want, chosen, true, scope, t += 16);
  CHECK(fabsf(last-chosen) < DEG(.05f));                                            /* a chosen value, whatever the zoom level */
  CHECK(fabsf(tfp_fov_lock_step_aim(&l, want, chosen, true, DEG(36), t += 16)-chosen) < DEG(.05f));
  for (int i = 0; i < 40; ++i) last = tfp_fov_lock_step_aim(&l, want, chosen, false, game, t += 16);
  CHECK(fabsf(last-want) < DEG(.05f));                                              /* out of aim: the lock again */
  /* The lock off, a chosen value: the game's own view at the hip, untouched; the chosen value in full aim, glided in
   * and out; and once out, nothing changed any more. */
  tfp_fov_lock_reset(&l);
  CHECK(tfp_fov_lock_step_aim(&l, 0, chosen, false, game, t += 16) == game && !tfp_fov_lock_active(&l));
  last = tfp_fov_lock_step_aim(&l, 0, chosen, true, scope, t += 16);
  CHECK(last == scope && tfp_fov_lock_active(&l));                                  /* the first frame is what was on screen */
  worst = 0;
  for (int i = 0; i < 40; ++i) { shown = tfp_fov_lock_step_aim(&l, 0, chosen, true, scope, t += 16); if (fabsf(shown-last) > worst) worst = fabsf(shown-last); last = shown; }
  CHECK(fabsf(last-chosen) < DEG(.05f) && worst < DEG(4.0f));
  for (frames = 0; frames < 60 && tfp_fov_lock_active(&l); ++frames) last = tfp_fov_lock_step_aim(&l, 0, chosen, false, game, t += 16);
  CHECK(frames > 5 && frames < 40 && last == game);
  CHECK(tfp_fov_lock_step_aim(&l, 0, chosen, false, DEG(50), t += 16) == DEG(50));
  /* The lock off and the game's zoom or the locked view in aim: nothing is changed, aiming or not (as before). */
  tfp_fov_lock_reset(&l);
  CHECK(tfp_fov_lock_step_aim(&l, 0, TFP_FOV_AIM_ZOOM, true, scope, t += 16) == scope && !tfp_fov_lock_active(&l));
  CHECK(tfp_fov_lock_step_aim(&l, 0, TFP_FOV_AIM_LOCKED, true, scope, t += 16) == scope && !tfp_fov_lock_active(&l));
  /* The old call is the two first answers: zoom when aiming true is the game's zoom, false the locked view. */
  TfpFovLock a, b; tfp_fov_lock_reset(&a); tfp_fov_lock_reset(&b);
  bool agree = true;
  for (int i = 0; i < 120; ++i) {
    const bool aim = (i / 20) % 2 == 1, zoom = (i / 40) % 2 == 0;
    const float x = tfp_fov_lock_step(&a, want, zoom, aim, aim ? scope : game, t + i * 16);
    const float y = tfp_fov_lock_step_aim(&b, want, zoom ? TFP_FOV_AIM_ZOOM : TFP_FOV_AIM_LOCKED, aim, aim ? scope : game, t + i * 16);
    agree = agree && x == y;
  }
  CHECK(agree);
  /* What the line calls each answer. */
  char said[64];
  CHECK(!strcmp(tfp_fov_aim_said(TFP_FOV_AIM_ZOOM, said, sizeof said), "the game's zoom"));
  CHECK(!strcmp(tfp_fov_aim_said(TFP_FOV_AIM_LOCKED, said, sizeof said), "your locked view"));
  CHECK(!strcmp(tfp_fov_aim_said(DEG(45), said, sizeof said), "a chosen 45.0 degrees"));
  printf("fov lock: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
