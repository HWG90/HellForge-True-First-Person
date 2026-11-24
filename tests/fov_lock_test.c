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

  /* Round 20: full aim's goal, the optic's own zoom rather than the unzoom gate's, and only while the gate flips
   * (review 24: a lone clear frame inside a long set stretch must not snap to full zoom; a value that lags the lens
   * by a frame must not make it worse). A model of the evaluator (game+0x1286a46..0x1286d09) at 60 frames a second,
   * a x12 optic under a 90 degree wide target, the setter's value composed from the lens by a factor in the tangent. */
  {
    const float wide = DEG(90), optic = DEG(7.5f), k = tanf(DEG(61.5f)*.5f)/tanf(DEG(60)*.5f);
    const float rest = 2*atanf(k*tanf(optic*.5f));
    /* pattern: 0 every frame, 1 set 36 frames (600 ms) / clear 1 frame, 2 set 16 (260 ms) / clear 1, 3 8 set / 2 clear,
     * 4 clear, 5 set 16 / clear 2 (the lens back at rest on the optic in each clear stretch) */
    for (int pattern = 0; pattern < 6; ++pattern) for (int lag = 0; lag < 3; ++lag) {
      TfpAimLens a; tfp_aim_lens_reset(&a); double at = 5000, call = 0; float lens = optic, before = optic, ratio_most = 0, last = 0, game_most = 0, game_last = 0;
      unsigned corrected = 0, frames = 0; bool exact = true;
      for (int i = 0; i < 300; ++i) {
        const double dt = 1000.0/60*(i % 3 == 0 ? .9 : i % 3 == 1 ? 1.1 : 1.0); at += dt;
        const double ms = at-5000;
        const bool gated = pattern == 0 ? (i & 1) : pattern == 1 ? i % 37 != 0 : pattern == 2 ? i % 17 != 0 : pattern == 3 ? i % 10 < 8 : pattern == 5 ? i % 18 > 1 : false;
        const float target = gated ? wide : optic, way = target-lens;
        float rate = 10*fabsf(way); rate = rate < .1f ? .1f : rate > 2 ? 2 : rate; if (!gated) rate *= 12;
        const float step = (way < 0 ? -1.0f : 1.0f)*rate*(float)(dt*.001);
        const float previous = before; before = lens;
        lens = fabsf(step) >= fabsf(way) ? target : lens+step;
        /* lag 0: the value and the note both this frame's; 1: the value built from the lens of the frame before;
         * 2: the note a frame late (what the pose helper read was the frame before's) */
        static bool g1; static float t1;
        if (lag == 2) { if (i) tfp_aim_lens_note(&a, g1, t1, before, at); g1 = gated; t1 = target; }
        else tfp_aim_lens_note(&a, gated, target, lens, at);
        (void)previous;
        const float applied = 2*atanf(k*tanf((lag == 1 ? before : lens)*.5f));
        bool clear = false;
        const float goal = tfp_aim_lens_goal(&a, applied, at+.5, call, &clear);
        call = at+.5;
        if (!clear && goal != applied) exact = false;
        if (ms > 1000) {
          ++frames; corrected += clear;
          const float r = fabsf(tanf(last*.5f)/tanf(goal*.5f)-1), g = fabsf(tanf(game_last*.5f)/tanf(applied*.5f)-1);
          if (r > ratio_most) ratio_most = r;
          if (g > game_most) game_most = g;
        }
        last = goal; game_last = applied;
      }
      printf("    pattern %d lag %d: largest change of magnification of the goal %.3f %% (the game's own %.3f %%), corrected in %u of %u\n",
             pattern, lag, (double)ratio_most*100, (double)game_most*100, corrected, frames);
      CHECK(exact); /* not correcting: the game's value, to the bit */
      if (pattern == 0) CHECK(corrected == frames && ratio_most < .002f);                       /* flips every frame: held on the optic, a lag or not */
      if (pattern == 1 || pattern == 2) CHECK(corrected == 0 && ratio_most == game_most);       /* a long set stretch with a lone clear frame: the game's own */
      if (pattern == 3) CHECK(corrected == frames && ratio_most < .01f);                         /* 8 set / 2 clear at 60 fps: a flip, held */
      if (pattern == 4) CHECK(corrected == 0 && ratio_most == 0);
      if (pattern == 5) CHECK(corrected > 0 && ratio_most < game_most); /* held from rest for a while, then eased toward the game's own unzoom: under its own swing */
      (void)rest;
    }
    /* At rest while held: the optic's own value as the game composes it. */
    {
      TfpAimLens a; tfp_aim_lens_reset(&a); double at = 5000, call = 0; bool clear = false; float goal = 0;
      for (int i = 0; i < 60; ++i) { at += 16; tfp_aim_lens_note(&a, false, optic, optic, at); goal = tfp_aim_lens_goal(&a, rest, at+.5, call, &clear); call = at+.5; }
      CHECK(goal == rest && !clear); /* the gate clear: nothing done */
      float lens = optic;
      for (int i = 0; i < 60; ++i) {
        at += 16; const bool gated = i & 1;
        lens = gated ? lens+.032f : optic+(lens-optic)*.2f;
        tfp_aim_lens_note(&a, gated, gated ? wide : optic, lens, at);
        goal = tfp_aim_lens_goal(&a, 2*atanf(k*tanf(lens*.5f)), at+.5, call, &clear); call = at+.5;
      }
      CHECK(clear && fabsf(goal-rest) < DEG(.001f));
      /* The ease: the gate kept set from here (a wall right in front). Held while the stretch is short, then let go
       * over TFP_AIM_EASE_MS: no frame changes the magnification by more than a quarter, and it ends on the game's. */
      float worst = 0, before = goal, applied = 0, own = 0, game_before = goal;
      for (int i = 0; i < 40; ++i) {
        at += 16; lens += .032f; if (lens > wide) lens = wide;
        tfp_aim_lens_note(&a, true, wide, lens, at);
        applied = 2*atanf(k*tanf(lens*.5f));
        goal = tfp_aim_lens_goal(&a, applied, at+.5, call, &clear); call = at+.5;
        const float r = fabsf(tanf(before*.5f)/tanf(goal*.5f)-1), g = fabsf(tanf(game_before*.5f)/tanf(applied*.5f)-1);
        if (r > worst) worst = r;
        if (g > own) own = g;
        before = goal; game_before = applied;
      }
      printf("    the gate kept set after flipping: largest change of magnification of the goal %.2f %% a frame (the game's own %.2f %%)\n", (double)worst*100, (double)own*100);
      CHECK(!clear && goal == applied && worst < own*1.1f);
      /* No note for longer than TFP_AIM_LENS_FRESH_MS while held: let go, eased, ending on the game's value. */
      for (int i = 0; i < 60; ++i) {
        at += 16; const bool gated = i & 1;
        lens = gated ? lens+.032f : optic+(lens-optic)*.2f;
        tfp_aim_lens_note(&a, gated, gated ? wide : optic, lens, at);
        goal = tfp_aim_lens_goal(&a, 2*atanf(k*tanf(lens*.5f)), at+.5, call, &clear); call = at+.5;
      }
      CHECK(clear);
      for (int i = 0; i < 30; ++i) { at += 16; goal = tfp_aim_lens_goal(&a, DEG(20), at+.5, call, &clear); call = at+.5; }
      CHECK(!clear && goal == DEG(20));
      /* Values that are not a lens: forgotten, the game's value. */
      tfp_aim_lens_note(&a, false, NAN, DEG(20), at += 8);
      CHECK(tfp_aim_lens_goal(&a, DEG(20.5f), at+.5, at-8, &clear) == DEG(20.5f) && !clear);
      CHECK(isnan(tfp_aim_lens_goal(&a, NAN, at+.5, at-8, &clear)) && !clear);
    }
    /* Correction comes in eased: the first frames of a flipping gate do not jump to the optic's value. */
    {
      TfpAimLens a; tfp_aim_lens_reset(&a); double at = 5000, call = 0; bool clear = false; float lens = optic, goal = 0, first = 0;
      for (int i = 0; i < 30; ++i) { at += 16; tfp_aim_lens_note(&a, false, optic, optic, at); tfp_aim_lens_goal(&a, rest, at+.5, call, &clear); call = at+.5; }
      int on = -1; float worst = 0, before = rest;
      for (int i = 0; i < 30; ++i) {
        at += 16; const bool gated = !(i & 1);
        lens = gated ? lens+.032f : optic+(lens-optic)*.2f;
        tfp_aim_lens_note(&a, gated, gated ? wide : optic, lens, at);
        const float applied = 2*atanf(k*tanf(lens*.5f));
        goal = tfp_aim_lens_goal(&a, applied, at+.5, call, &clear); call = at+.5;
        if (clear && on < 0) { on = i; first = fabsf(tanf(before*.5f)/tanf(goal*.5f)-1); }
        const float r = fabsf(tanf(before*.5f)/tanf(goal*.5f)-1); if (r > worst) worst = r;
        before = goal;
      }
      /* from rest on the optic the first set frame is held at once: no change at all on screen */
      CHECK(on == 0 && first < 1e-5f && worst < 1e-5f && clear && fabsf(goal-rest) < DEG(.001f));
      /* from a lens not at rest (a clear frame pulled it back only partly): after the first short set stretch, eased in */
      tfp_aim_lens_reset(&a); on = -1; worst = 0; lens = optic+.05f; before = 2*atanf(k*tanf(lens*.5f)); float game_worst = 0, game_before = before;
      for (int i = 0; i < 40; ++i) {
        at += 16; const bool gated = !(i & 1);
        lens = gated ? lens+.032f : optic+(lens-optic)*.5f;
        tfp_aim_lens_note(&a, gated, gated ? wide : optic, lens, at);
        const float applied = 2*atanf(k*tanf(lens*.5f));
        goal = tfp_aim_lens_goal(&a, applied, at+.5, call, &clear); call = at+.5;
        if (clear && on < 0) on = i;
        const float r = fabsf(tanf(before*.5f)/tanf(goal*.5f)-1), g = fabsf(tanf(game_before*.5f)/tanf(applied*.5f)-1);
        if (r > worst) worst = r;
        if (g > game_worst) game_worst = g;
        before = goal; game_before = applied;
      }
      CHECK(on >= 1 && on <= 3 && worst <= game_worst && clear && fabsf(goal-rest) < DEG(.001f));
    }
  }
  printf("fov lock: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
