/* The rig's depth held steady (src/depth_steady.h): the game's own depth worked out as the game does, the share of it
 * that reaches the rig, the hold and its ease, the "Steady:" line, and made-up scenes at 8, 16.7, 33.3 and 50 ms a
 * frame. The scenes run a small model of the game's side: a lens that goes towards its target at 2 rad/s, a target
 * that is the weapon's own (60 degrees) while the unzoom gate is clear and the wide one (90) while it is set, and the
 * evaluator's depth blend over them (game+0x12873b3..0x1287423). */
#include "depth_steady.h"
#include <fenv.h>
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static bool near(double a, double b, double e) { return fabs(a-b) <= e; }
static const float RAD = .01745329238474369f;

/* The game's side, made up. */
typedef struct Game { float lens, weapon, wide, prior, current, previous; } Game;
static Game scythe(void) { const Game g = {60*RAD, 60*RAD, 90*RAD, 80*RAD, .24f, 0}; return g; } /* the request built at the mod's locked 80 degrees; settled */
static TfpSteadyInput game_frame(Game *g, bool bit, float dt_ms) {
  const float target = bit ? g->wide : g->weapon, most = 2.0f*dt_ms*.001f;
  float way = target-g->lens; if (way > most) way = most; if (way < -most) way = -most;
  g->lens += way;
  TfpSteadyInput in; memset(&in, 0, sizeof in);
  in.target = target; in.lens = g->lens; in.prior = g->prior; in.current = g->current; in.previous = g->previous; in.settled = g->current;
  in.bit = bit; in.mix_before = in.mix_after = 1; in.share = 1; in.on = true;
  return in;
}
/* A new optic distance, as game+0x1278cc0 leaves the request: the old one kept, the lens as it stands. */
static void game_optic(Game *g, float distance, float weapon_degrees) { g->previous = g->current; g->current = distance; g->prior = g->lens; g->weapon = weapon_degrees*RAD; }

typedef struct Scene { float raw_step, eye_step, delta_most; int frames, shifted; float last; } Scene;
static const float DT[4] = {8, 16.7f, 33.3f, 50};

/* One frame of a scene: the game's depth, the hold, and the depth the rig ends at (the game's plus the shift). `base`
 * is everything else the game puts in the same argument (the rig's own animation): it passes through. */
static void scene_frame(Scene *s, TfpSteady *state, const TfpSteadyInput *in, uint64_t lease, double tick, float base, float *raw_last, float *eye_last, float *base_last) {
  float delta = 0, depth = 0, held = 0;
  const uint32_t reason = tfp_steady_frame(state, in, lease, tick, &delta, &depth, &held);
  CHECK(reason == TFP_STEADY_WORKED);
  const float raw = base+depth, eye = base+depth+delta;
  if (s->frames) {
    const float raw_step = fabsf((raw-*raw_last)-(base-*base_last)), eye_step = fabsf((eye-*eye_last)-(base-*base_last));
    if (raw_step > s->raw_step) s->raw_step = raw_step;
    if (eye_step > s->eye_step) s->eye_step = eye_step;
  }
  if (fabsf(delta) > s->delta_most) s->delta_most = fabsf(delta);
  s->shifted += delta != 0; ++s->frames; s->last = eye-base;
  *raw_last = raw; *eye_last = eye; *base_last = base;
}

/* One lease of `frames` frames of node 5: the root follows in `follows` of every `period` frames in which it was moved,
 * and after each frame moved come `idle` in which nothing is. Returns the frames in which node 5 was moved alone. */
static unsigned pair_alone(unsigned frames, unsigned period, unsigned follows, unsigned idle, bool *stopped, unsigned *said) {
  TfpSteadyPair pair; memset(&pair, 0, sizeof pair); unsigned alone = 0, moved = 0; bool now = false;
  *stopped = false; *said = 0;
  for (unsigned i = 0; i < frames; ++i) {
    *stopped = tfp_steady_pair_frame(&pair, 5, &now);
    if (now) ++*said;
    if (*stopped || i % (idle+1)) continue;
    tfp_steady_pair_moved(&pair);
    if (moved++ % period < follows) tfp_steady_pair_root(&pair); else ++alone;
  }
  return alone;
}

int main(void) {
  /* ---- the game's depth, as the evaluator works it out ---- */
  {
    TfpSteadyInput in; memset(&in, 0, sizeof in); float d = 0;
    in.target = 60*RAD; in.lens = 60*RAD; in.prior = 90*RAD; in.current = .24f; in.previous = .1f;
    CHECK(tfp_steady_depth(&in, &d) && d == .24f);                 /* the lens on its target: the selected optic's distance, to the bit */
    in.lens = 90*RAD; CHECK(tfp_steady_depth(&in, &d) && near(d, .1, 1e-7)); /* the lens where the request was built: the distance before */
    in.lens = 67.5f*RAD; CHECK(tfp_steady_depth(&in, &d) && near(d, .75*.24+.25*.1, 1e-6));
    in.lens = 120*RAD; CHECK(tfp_steady_depth(&in, &d) && near(d, .1, 1e-7)); /* never past the distance before */
    in.target = 90*RAD; in.lens = 67.6f*RAD; CHECK(tfp_steady_depth(&in, &d) && d == .24f); /* the target on the request's own lens: no span, the selected distance */
    in.prior = 80*RAD; CHECK(tfp_steady_depth(&in, &d) && near(d, .1, 1e-7));               /* a gated frame of a request built at 80 degrees */
    /* values the term cannot use */
    in.lens = NAN; CHECK(!tfp_steady_depth(&in, &d)); in.lens = 67.6f*RAD;
    in.target = 0; CHECK(!tfp_steady_depth(&in, &d)); in.target = 3; CHECK(!tfp_steady_depth(&in, &d)); in.target = 90*RAD;
    in.prior = -1; CHECK(!tfp_steady_depth(&in, &d)); in.prior = 80*RAD;
    in.current = 4.5f; CHECK(!tfp_steady_depth(&in, &d)); in.current = INFINITY; CHECK(!tfp_steady_depth(&in, &d)); in.current = .24f;
    in.previous = NAN; CHECK(!tfp_steady_depth(&in, &d));
  }
  /* ---- how much of the depth reaches the rig (game+0x1287a44..0x1287bb4) ---- */
  {
    TfpSteadyInput in; memset(&in, 0, sizeof in); float g = -1;
    in.mix_before = in.mix_after = 1;
    CHECK(tfp_steady_gain(&in, &g) && g == 1);                               /* request+0x8d clear: whole */
    in.ability = 1; CHECK(tfp_steady_gain(&in, &g) && g == 1);
    in.blend = 1; CHECK(tfp_steady_gain(&in, &g) && g == 0);                 /* +0x8d and +0x8f set, the weight at 1: the rig's vector is the fixed one, no depth in it */
    in.ability = 0; CHECK(tfp_steady_gain(&in, &g) && g == 1);
    in.mix_before = .25f; in.mix_after = .5f; CHECK(tfp_steady_gain(&in, &g) && g == .5f); /* the 75 ms after a reload state changed */
    in.ability = 1; CHECK(tfp_steady_gain(&in, &g) && g == .5f);
    in.mix_after = .75f; CHECK(tfp_steady_gain(&in, &g) && g == .25f);
    in.bit = 1; CHECK(!tfp_steady_gain(&in, &g));                            /* and the gate set in them: the game scales the whole vector by main row +0x11ac; not ours to undo */
    in.mix_before = 1; in.mix_after = 1; CHECK(tfp_steady_gain(&in, &g) && g == 0);
    in.mix_after = NAN; CHECK(!tfp_steady_gain(&in, &g)); in.mix_after = 1.5f; CHECK(!tfp_steady_gain(&in, &g));
  }
  /* ---- calm frames: nothing is added, to the bit ---- */
  for (int k = 0; k < 4; ++k) {
    Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state);
    for (int i = 0; i < 200; ++i) {
      const TfpSteadyInput in = game_frame(&g, false, DT[k]); float delta = 1, depth = 0, held = 0;
      CHECK(tfp_steady_frame(&state, &in, 1, 1000.0+(double)i*(double)DT[k], &delta, &depth, &held) == TFP_STEADY_WORKED);
      CHECK(delta == 0 && depth == .24f && held == .24f);
    }
  }
  /* ---- the scenes ---- */
  printf("  made up, at 8 / 16.7 / 33.3 / 50 ms a frame: the largest step of the rig's depth in one frame, the game's own -> held\n");
  for (int k = 0; k < 4; ++k) {
    const float dt = DT[k]; const int second = (int)(1000.0f/dt);
    /* 1. Standing, the gate flipping every frame for two seconds, a calm half second either side. */
    {
      Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state); Scene s; memset(&s, 0, sizeof s); float a = 0, b = 0, c = 0;
      for (int i = 0; i < 3*second; ++i) {
        const bool episode = i >= second/2 && i < second/2+2*second;
        const TfpSteadyInput in = game_frame(&g, episode && (i & 1), dt);
        scene_frame(&s, &state, &in, 1, 1000.0+(double)i*(double)dt, 0, &a, &b, &c);
      }
      printf("    %4.1f ms  standing: %.3f -> %.6f m (shifted in %d of %d frames, by %.3f m at most)\n", (double)dt, (double)s.raw_step, (double)s.eye_step, s.shifted, s.frames, (double)s.delta_most);
      CHECK(s.raw_step > .05f && s.eye_step < 1e-6f && near(s.last, .24, 1e-6) && s.shifted >= second-1);
    }
    /* 2. The slow cycle: set for eight frames, clear for two, for three seconds. */
    {
      Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state); Scene s; memset(&s, 0, sizeof s); float a = 0, b = 0, c = 0;
      for (int i = 0; i < 3*second; ++i) {
        const TfpSteadyInput in = game_frame(&g, i >= 5 && i%10 < 8, dt);
        scene_frame(&s, &state, &in, 1, 1000.0+(double)i*(double)dt, 0, &a, &b, &c);
      }
      printf("    %4.1f ms  slow cycle: %.3f -> %.6f m\n", (double)dt, (double)s.raw_step, (double)s.eye_step);
      CHECK(s.raw_step > .02f && s.eye_step < 1e-6f);
    }
    /* 3. Right after a swap: a new native entry (a new lease) whose first frames already flip. The hold begins at the
     * game's own depth of the first frame (no step into it) and goes to the settled distance in one go, never back. */
    {
      Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state); Scene s; memset(&s, 0, sizeof s); float a = 0, b = 0, c = 0;
      for (int i = 0; i < second; ++i) { const TfpSteadyInput in = game_frame(&g, false, dt); scene_frame(&s, &state, &in, 1, 1000.0+(double)i*(double)dt, 0, &a, &b, &c); }
      g = scythe(); g.lens = 80*RAD; g.prior = 80*RAD; /* the new request, built at the lens the camera had */
      memset(&s, 0, sizeof s); float before = 0; bool back = false; float first_delta = 1;
      for (int i = 0; i < 2*second; ++i) {
        const TfpSteadyInput in = game_frame(&g, (i & 1) == 0, dt);
        const double tick = 5000.0+(double)i*(double)dt;
        if (!i) { TfpSteady copy = state; float depth = 0, held = 0; tfp_steady_frame(&copy, &in, 2, tick, &first_delta, &depth, &held); }
        scene_frame(&s, &state, &in, 2, tick, 0, &a, &b, &c);
        if (i && s.last < before-2e-7f) back = true;
        before = s.last;
      }
      printf("    %4.1f ms  after a swap: %.3f -> %.4f m, at rest on %.4f m\n", (double)dt, (double)s.raw_step, (double)s.eye_step, (double)s.last);
      CHECK(first_delta == 0 && !back && near(s.last, .24, 1e-6) && s.raw_step > .01f);
      CHECK(s.eye_step <= .24f*(float)TFP_STEADY_RATE*.3679f*dt*.001f+1e-5f); /* a critically damped ease never goes faster than distance * rate / e */
    }
    /* 4. A sprint that stops, the flipping beginning as it does: the rig's own motion (2 m/s falling to nothing in the
     * same argument) goes through whole; the depth beside it does not move. */
    {
      Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state); Scene s; memset(&s, 0, sizeof s); float a = 0, b = 0, c = 0, base = 0;
      for (int i = 0; i < 3*second; ++i) {
        const float speed = i < second ? 2.0f : i < second+second/4 ? 2.0f*(1-(float)(i-second)/(float)(second/4)) : 0;
        base += speed*dt*.001f;
        const TfpSteadyInput in = game_frame(&g, i >= second && (i & 1), dt);
        scene_frame(&s, &state, &in, 1, 1000.0+(double)i*(double)dt, base, &a, &b, &c);
      }
      printf("    %4.1f ms  sprint stop: %.3f -> %.6f m beside the rig's own motion\n", (double)dt, (double)s.raw_step, (double)s.eye_step);
      CHECK(s.raw_step > .05f && s.eye_step < 3e-6f);
    }
    /* 5. One honest change of the gate, set and kept. The game's own depth jumps towards the distance before and
     * comes back as the lens travels; both ends are the same settled distance, so the held depth has nowhere to go. */
    {
      Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state); Scene s; memset(&s, 0, sizeof s); float a = 0, b = 0, c = 0;
      for (int i = 0; i < 3*second; ++i) { const TfpSteadyInput in = game_frame(&g, i >= second && i < 2*second, dt); scene_frame(&s, &state, &in, 1, 1000.0+(double)i*(double)dt, 0, &a, &b, &c); }
      printf("    %4.1f ms  one change, there and back a second later: %.3f -> %.6f m\n", (double)dt, (double)s.raw_step, (double)s.eye_step);
      CHECK(s.raw_step > .05f && s.eye_step < 1e-6f && near(s.last, .24, 1e-6));
    }
    /* 6. Aim in and out quickly: the hip share goes 1 -> 0 over 150 ms and back, the optic distance changes with it.
     * In full aim the game's own depth is shown whole; on the way the two are mixed. No frame steps more than the
     * game's own largest step and the ease's together. */
    {
      Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state); Scene s; memset(&s, 0, sizeof s); float a = 0, b = 0, c = 0;
      const int in_at = second, out_at = second+second/4, in_again = second+second/2, end = 4*second;
      float share_most = 0, share = 1, target_share = 1;
      for (int i = 0; i < end; ++i) {
        if (i == in_at || i == in_again) { game_optic(&g, .30f, 45); target_share = 0; }
        if (i == out_at) { game_optic(&g, .24f, 60); target_share = 1; }
        const float before = share, most = dt/150.0f;
        share += target_share > share ? fminf(most, target_share-share) : -fminf(most, share-target_share);
        if (fabsf(share-before) > share_most) share_most = fabsf(share-before);
        TfpSteadyInput in = game_frame(&g, false, dt); in.share = share;
        scene_frame(&s, &state, &in, 1, 1000.0+(double)i*(double)dt, 0, &a, &b, &c);
      }
      printf("    %4.1f ms  aim in, out, in: %.4f -> %.4f m, in full aim the shift is %.6f m\n", (double)dt, (double)s.raw_step, (double)s.eye_step, (double)(s.last-.30f));
      CHECK(s.last == .30f);                                                      /* settled in full aim: the game's own */
      CHECK(s.eye_step <= s.raw_step+.06f*(float)TFP_STEADY_RATE*.3679f*dt*.001f+.06f*share_most+1e-5f);
    }
    /* 7. An episode that ends in a real move: the player walks off (the rig's own motion) and the gate stays clear. */
    {
      Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state); Scene s; memset(&s, 0, sizeof s); float a = 0, b = 0, c = 0, base = 0;
      for (int i = 0; i < 3*second; ++i) {
        if (i >= 2*second) base += 1.5f*dt*.001f;
        const TfpSteadyInput in = game_frame(&g, i < 2*second && (i & 1), dt);
        scene_frame(&s, &state, &in, 1, 1000.0+(double)i*(double)dt, base, &a, &b, &c);
      }
      printf("    %4.1f ms  ends in a move: %.3f -> %.6f m beside the move\n", (double)dt, (double)s.raw_step, (double)s.eye_step);
      CHECK(s.eye_step < 3e-6f && near(s.last, .24, 2e-6));
    }
    /* 8. A real change of the optic at the hip: eased over the hold's own time, no step, no overshoot. */
    {
      Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state); Scene s; memset(&s, 0, sizeof s); float a = 0, b = 0, c = 0, most = 0;
      for (int i = 0; i < 3*second; ++i) {
        if (i == second) game_optic(&g, .40f, 30);
        const TfpSteadyInput in = game_frame(&g, false, dt);
        scene_frame(&s, &state, &in, 1, 1000.0+(double)i*(double)dt, 0, &a, &b, &c);
        if (s.last > most) most = s.last;
      }
      printf("    %4.1f ms  another optic: %.4f -> %.4f m\n", (double)dt, (double)s.raw_step, (double)s.eye_step);
      CHECK(near(s.last, .40, 1e-6) && most < .40f+1e-6f && s.eye_step <= .16f*(float)TFP_STEADY_RATE*.3679f*dt*.001f+1e-5f);
    }
  }
  /* ---- how long the ease takes: nine tenths of the way within 250 ms, at rest within 600 ms, at any frame time ---- */
  for (int k = 0; k < 4; ++k) {
    TfpSteady state; memset(&state, 0, sizeof state); double ninety = -1, rest = -1;
    for (int i = 0; i*(double)DT[k] <= 1000; ++i) {
      const double t = i*(double)DT[k]; const float held = tfp_steady_held(&state, 1, 1000.0+t, 0, i ? .2f : 0);
      if (ninety < 0 && held >= .18f) ninety = t;
      if (rest < 0 && held == .2f) rest = t;
    }
    printf("  ease at %4.1f ms a frame: nine tenths after %.0f ms, at rest after %.0f ms\n", (double)DT[k], ninety, rest);
    CHECK(ninety > 0 && ninety <= 250 && rest > 0 && rest <= 600);
  }
  /* ---- the state: a new lease, a pause, time going back begin again at the game's own depth; the same instant twice changes nothing ---- */
  {
    TfpSteady state; memset(&state, 0, sizeof state);
    CHECK(tfp_steady_held(&state, 1, 1000, .1f, .24f) == .1f);
    const float one = tfp_steady_held(&state, 1, 1016, .1f, .24f);
    CHECK(one > .1f && one < .24f);
    CHECK(tfp_steady_held(&state, 1, 1016, .1f, .24f) == one);
    CHECK(tfp_steady_held(&state, 2, 1032, .05f, .24f) == .05f);
    CHECK(tfp_steady_held(&state, 2, 1032+TFP_STEADY_GAP_MS+1, .07f, .24f) == .07f);
    CHECK(tfp_steady_held(&state, 2, 1000, .09f, .24f) == .09f);
  }
  /* ---- passes: nothing added, the state as it was, no floating-point fault left behind by a value that is no number ---- */
  {
    Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state); float delta = 1, depth = 1, held = 1;
    TfpSteadyInput in = game_frame(&g, true, 16);
    CHECK(tfp_steady_frame(&state, &in, 1, 1000, &delta, &depth, &held) == TFP_STEADY_WORKED && delta == 0); /* the first frame: begun at the game's depth */
    in = game_frame(&g, false, 16);
    CHECK(tfp_steady_frame(&state, &in, 1, 1016, &delta, &depth, &held) == TFP_STEADY_WORKED && delta != 0);
    const TfpSteady kept = state;
    in.lens = NAN; in.target = NAN; in.current = NAN; in.mix_after = NAN; in.share = NAN; in.settled = NAN;
    feclearexcept(FE_ALL_EXCEPT);
    CHECK(tfp_steady_frame(&state, &in, 1, 1032, &delta, &depth, &held) == TFP_STEADY_RANGE && delta == 0);
    CHECK(!fetestexcept(FE_INVALID) && !memcmp(&kept, &state, sizeof state));
    in = game_frame(&g, true, 16); in.blend = 1; in.mix_before = .5f; in.mix_after = .7f;
    CHECK(tfp_steady_frame(&state, &in, 1, 1048, &delta, &depth, &held) == TFP_STEADY_SCALED && delta == 0 && !memcmp(&kept, &state, sizeof state));
    in = game_frame(&g, true, 16); in.on = false;
    CHECK(tfp_steady_frame(&state, &in, 1, 1064, &delta, &depth, &held) == TFP_STEADY_OFF && delta == 0 && depth < .24f && !state.known); /* off: the game's depth is still worked out for the line */
    in = game_frame(&g, true, 16); in.share = .5f; in.settled = .30f; memset(&state, 0, sizeof state);
    tfp_steady_frame(&state, &in, 1, 1080, &delta, &depth, &held);
    in = game_frame(&g, false, 16); in.share = .5f; in.settled = .30f; state.held = .30f; state.speed = 0;
    CHECK(tfp_steady_frame(&state, &in, 1, 1096, &delta, &depth, &held) == TFP_STEADY_WORKED && near(delta, .5*(.30-(double)depth), 1e-7)); /* half the hip share: half the shift */
  }
  /* ---- no shift over a metre (the zoom depth term's own limit): inside its own ranges the rule could ask for 8 m ---- */
  {
    TfpSteady state; memset(&state, 0, sizeof state); float delta = 1, depth = 1, held = 1;
    TfpSteadyInput in; memset(&in, 0, sizeof in);
    in.target = 60*RAD; in.lens = 60*RAD; in.prior = 80*RAD; in.current = 3.5f; in.previous = -3.5f; in.settled = 3.5f;
    in.mix_before = in.mix_after = 1; in.share = 1; in.on = true;
    CHECK(tfp_steady_frame(&state, &in, 1, 1000, &delta, &depth, &held) == TFP_STEADY_WORKED && delta == 0 && held == 3.5f);
    const TfpSteady kept = state;
    in.target = 90*RAD; in.lens = 62*RAD; /* the gate set: the game's depth is the distance before, 7 m away */
    CHECK(tfp_steady_frame(&state, &in, 1, 1016, &delta, &depth, &held) == TFP_STEADY_RANGE && delta == 0 && held == 0 && depth == -3.5f);
    CHECK(!memcmp(&kept, &state, sizeof state));                       /* a pass: the state as it was */
    in.previous = 2.4f; CHECK(tfp_steady_frame(&state, &in, 1, 1016, &delta, &depth, &held) == TFP_STEADY_RANGE && delta == 0 && !memcmp(&kept, &state, sizeof state)); /* 1.1 m */
    in.previous = 4; in.current = 2.9f; in.settled = 2.9f; state.held = 2.9f;
    CHECK(tfp_steady_frame(&state, &in, 1, 1016, &delta, &depth, &held) == TFP_STEADY_RANGE && delta == 0); /* and the other way */
    state = kept; in.current = in.settled = 3.5f; in.previous = 2.6f;
    CHECK(tfp_steady_frame(&state, &in, 1, 1016, &delta, &depth, &held) == TFP_STEADY_WORKED && near(delta, .9, 1e-6) && held == 3.5f); /* 0.9 m goes out */
    state = kept; in.previous = 2.4f; in.share = .5f;
    CHECK(tfp_steady_frame(&state, &in, 1, 1016, &delta, &depth, &held) == TFP_STEADY_WORKED && near(delta, .55, 1e-6)); /* what counts is what would be added */
    CHECK(TFP_STEADY_MOST == 1.0f);
  }
  /* ---- node 5 never keeps moving without its root: two frames running (or three in the lease, below), and the held depth stops for the lease ---- */
  {
    TfpSteadyPair pair; memset(&pair, 0, sizeof pair); bool now = true;
    CHECK(TFP_STEADY_ALONE == 2);
    CHECK(!tfp_steady_pair_frame(&pair, 1, &now) && !now);
    tfp_steady_pair_moved(&pair); tfp_steady_pair_root(&pair);            /* a frame as it should be */
    CHECK(!tfp_steady_pair_frame(&pair, 1, &now) && !now && pair.alone == 0);
    tfp_steady_pair_moved(&pair);                                         /* the root did not come */
    CHECK(!tfp_steady_pair_frame(&pair, 1, &now) && !now && pair.alone == 1); /* once: not yet */
    tfp_steady_pair_moved(&pair); tfp_steady_pair_root(&pair);
    CHECK(!tfp_steady_pair_frame(&pair, 1, &now) && pair.alone == 0);     /* it followed again: the count begins again */
    tfp_steady_pair_moved(&pair);
    CHECK(!tfp_steady_pair_frame(&pair, 1, &now) && pair.alone == 1);
    CHECK(!tfp_steady_pair_frame(&pair, 1, &now) && pair.alone == 1);     /* a frame in which nothing was moved owes nothing */
    tfp_steady_pair_moved(&pair);
    CHECK(tfp_steady_pair_frame(&pair, 1, &now) && now);                  /* twice running: stopped, and this is the frame that says so */
    CHECK(tfp_steady_pair_frame(&pair, 1, &now) && !now);                 /* and stays stopped; said once */
    tfp_steady_pair_root(&pair);
    CHECK(tfp_steady_pair_frame(&pair, 1, &now) && !now);                 /* a root that comes later does not start it again */
    CHECK(!tfp_steady_pair_frame(&pair, 2, &now) && !now && pair.alone == 0); /* a new lease does */
    tfp_steady_pair_moved(&pair);
    CHECK(!tfp_steady_pair_frame(&pair, 3, &now) && !now && pair.alone == 0 && pair.lost == 0); /* what the old lease owed is not the new one's */
  }
  /* ---- and never more than TFP_STEADY_ALONE_LEASE frames alone in one lease, whatever the pattern: a root that comes
   * every other frame begins the count of frames running again each time, and node 5 would go on alone in half of
   * all frames ---- */
  {
    bool stopped = false, now = false; unsigned said = 0;
    CHECK(TFP_STEADY_ALONE_LEASE == 3);
    CHECK(pair_alone(120, 2, 1, 0, &stopped, &said) == 3 && stopped && said == 1);        /* every other frame: 60 of 120 without the rule */
    CHECK(pair_alone(120, 3, 2, 0, &stopped, &said) == 3 && stopped && said == 1);        /* two in three: 40 */
    CHECK(pair_alone(120, 10, 9, 0, &stopped, &said) == 3 && stopped && said == 1);       /* nine in ten: 12 */
    CHECK(pair_alone(120, 2, 1, 3, &stopped, &said) == 3 && stopped && said == 1);        /* the same with frames between in which nothing is moved */
    CHECK(pair_alone(120, 1, 0, 0, &stopped, &said) == 2 && stopped && said == 1);        /* never: the two running, as before */
    CHECK(pair_alone(120, 3, 1, 0, &stopped, &said) == 2 && stopped && said == 1);        /* one in three: two running come first */
    CHECK(pair_alone(4000000, 1, 1, 0, &stopped, &said) == 0 && !stopped && said == 0);   /* a healthy lease, 18 hours of it at 60 a second: never */
    CHECK(pair_alone(4000000, 1, 1, 2, &stopped, &said) == 0 && !stopped && said == 0);
    uint32_t seed = 12345; unsigned most = 0;
    for (unsigned run = 0; run < 2000; ++run) { /* any pattern at all */
      TfpSteadyPair pair; memset(&pair, 0, sizeof pair); unsigned alone = 0;
      seed = seed*1664525u+1013904223u; const uint32_t odds = (seed >> 8) % 100u;
      for (unsigned i = 0; i < 600; ++i) {
        if (tfp_steady_pair_frame(&pair, 9, &now)) continue;
        seed = seed*1664525u+1013904223u; if ((seed >> 8) % 4u == 0) continue;            /* nothing moved */
        tfp_steady_pair_moved(&pair);
        seed = seed*1664525u+1013904223u; if ((seed >> 8) % 100u < odds) tfp_steady_pair_root(&pair); else ++alone;
      }
      if (alone > most) most = alone;
    }
    printf("  frames of one lease in which node 5 was moved alone, over 2000 made-up patterns: at most %u\n", most);
    CHECK(most == TFP_STEADY_ALONE_LEASE);
    /* one lost frame is borne, and a second one apart from it; the third stops, and says how many and how */
    TfpSteadyPair pair; memset(&pair, 0, sizeof pair);
    for (unsigned i = 0; i < 1000; ++i) {
      CHECK(!tfp_steady_pair_frame(&pair, 4, &now) && !now);
      tfp_steady_pair_moved(&pair);
      if (i != 100 && i != 700) tfp_steady_pair_root(&pair);
    }
    CHECK(!tfp_steady_pair_frame(&pair, 4, &now) && !now && pair.lost == 2 && pair.alone == 0);
    tfp_steady_pair_moved(&pair);
    CHECK(tfp_steady_pair_frame(&pair, 4, &now) && now && pair.lost == 3 && pair.alone == 1);
    CHECK(tfp_steady_pair_frame(&pair, 4, &now) && !now && pair.lost == 3);
  }
  /* ---- the "Steady:" line ---- */
  {
    TfpSteadyLog log; memset(&log, 0, sizeof log); TfpSteadySecond s; char text[640];
    Game g = scythe(); TfpSteady state; memset(&state, 0, sizeof state);
    for (int i = 0; i < 30; ++i) { /* a calm second */
      const TfpSteadyInput in = game_frame(&g, false, 33.3f); float delta = 0, depth = 0, held = 0;
      const uint32_t reason = tfp_steady_frame(&state, &in, 1, 1000.0+i*33.3, &delta, &depth, &held);
      tfp_steady_note(&log, reason, in.bit, depth, held, delta, .31f);
    }
    tfp_steady_take(&log, &s);
    CHECK(s.frames == 30 && s.worked == 30 && s.shifted == 0 && !tfp_steady_worth(&s));
    for (int i = 0; i < 30; ++i) { /* a flipping one, the root following in every frame the rig was moved in */
      const TfpSteadyInput in = game_frame(&g, (i & 1) == 0, 33.3f); float delta = 0, depth = 0, held = 0;
      const uint32_t reason = tfp_steady_frame(&state, &in, 1, 2000.0+i*33.3, &delta, &depth, &held);
      tfp_steady_note(&log, reason, in.bit, depth, held, delta, .07f+depth);
      if (delta != 0) { tfp_steady_moved(&log, delta); tfp_steady_root(&log); }
    }
    tfp_steady_take(&log, &s);
    CHECK(s.frames == 30 && s.worked == 30 && s.gated.count == 15 && s.clear.count == 15 && s.shifted == 15 && s.roots == 15 && tfp_steady_worth(&s));
    tfp_steady_line(&s, text, sizeof text);
    printf("  %s\n", text);
    CHECK(strlen(text) < 600 && strstr(text, "Steady: 30 frames of the rig, the game's depth worked out in 30: ") && strstr(text, " m with the gate set (15 frames), ")
          && strstr(text, " m clear (15); held at 0.2400 to 0.2400 m; the rig moved to it in 15 frames, its root with it in 15, by 0.2400 to 0.2400 m; the rig's depth argument 0.0700 to 0.0700 m gated, 0.3100 to 0.3100 m clear; passed in 0") && !strstr(text, "passed in 0 ("));
    CHECK(strstr(text, "; the rig's depth argument ") && !strstr(text, "depth_steady_off.txt"));
    /* "moved" is a frame in which node 5's float changed, as the helper says it; a shift worked out is not one yet (it
     * can be too small for the float, and the root's half is only asked for where the float changed) */
    tfp_steady_note(&log, TFP_STEADY_WORKED, 0, .24f, .24f, 1e-9f, .3f);
    tfp_steady_take(&log, &s);
    CHECK(s.frames == 1 && s.worked == 1 && s.held.count == 1 && s.shifted == 0 && s.delta.count == 0 && s.roots == 0 && !tfp_steady_worth(&s));
    tfp_steady_note(&log, TFP_STEADY_WORKED, 0, .2f, .24f, .04f, .3f); tfp_steady_moved(&log, .04f); tfp_steady_root(&log);
    tfp_steady_take(&log, &s);
    CHECK(s.shifted == 1 && s.roots == 1 && s.delta.count == 1 && s.delta.most == .04f && tfp_steady_worth(&s));
    /* the two passes of the helper's own: the code not pinned (nothing of the frame is believed), the root not following
     * (the game's depth is still worked out and counted, as with the switch) */
    for (int i = 0; i < 3; ++i) tfp_steady_note(&log, TFP_STEADY_NO_CODE, 0, 0, 0, 0, .3f);
    tfp_steady_take(&log, &s);
    CHECK(s.frames == 3 && s.worked == 0 && s.passed[TFP_STEADY_NO_CODE] == 3 && tfp_steady_passed(&s) == 3 && tfp_steady_worth(&s) && !tfp_steady_stop_line(&s, text, sizeof text));
    tfp_steady_line(&s, text, sizeof text);
    CHECK(strstr(text, "; passed in 3 (the evaluator's code is not the pinned one 3)"));
    for (int i = 0; i < 4; ++i) tfp_steady_note(&log, TFP_STEADY_NO_ROOT, i & 1, (i & 1) ? .1f : .24f, 0, 0, .3f);
    tfp_steady_stopped(&log, 7, 2, 2);
    tfp_steady_take(&log, &s);
    CHECK(s.frames == 4 && s.worked == 4 && s.gated.count == 2 && s.clear.count == 2 && s.held.count == 0 && s.passed[TFP_STEADY_NO_ROOT] == 4 && s.stops == 1 && s.stop_lease == 7 && tfp_steady_worth(&s));
    tfp_steady_line(&s, text, sizeof text);
    CHECK(strstr(text, "nothing held; the rig moved to it in 0 frames, its root with it in 0; ") && strstr(text, "; passed in 4 (the root did not follow 4)"));
    CHECK(tfp_steady_stop_line(&s, text, sizeof text));
    printf("  %s\n", text);
    CHECK(!strcmp(text, "Steady: stopped for lease 7: the rig was moved in 2 frames of this lease and its root did not follow (the last 2 running), so the gun moved "
                        "against the eye in them; nothing more is held in this lease, and it begins again at the next"));
    {
      TfpSteadyLog other; memset(&other, 0, sizeof other); TfpSteadySecond o;
      tfp_steady_note(&other, TFP_STEADY_NO_ROOT, 0, .24f, 0, 0, .3f);
      tfp_steady_stopped(&other, 8, 3, 1);                               /* a root that came now and then */
      tfp_steady_take(&other, &o);
      CHECK(o.stops == 1 && o.stop_lease == 8 && o.stop_lost == 3 && o.stop_running == 1 && tfp_steady_stop_line(&o, text, sizeof text));
      printf("  %s\n", text);
      CHECK(!strcmp(text, "Steady: stopped for lease 8: the rig was moved in 3 frames of this lease and its root did not follow (no two of them running), so the gun moved "
                          "against the eye in them; nothing more is held in this lease, and it begins again at the next"));
    }
    for (int i = 0; i < 4; ++i) tfp_steady_note(&log, TFP_STEADY_NO_ROOT, i & 1, (i & 1) ? .1f : .24f, 0, 0, .3f);
    tfp_steady_take(&log, &s);
    CHECK(s.stops == 0 && tfp_steady_worth(&s) && !tfp_steady_stop_line(&s, text, sizeof text)); /* later seconds of that lease: a line where the game's depth moved, as with the switch; the stop is said once */
    for (int i = 0; i < 40; ++i) tfp_steady_note(&log, TFP_STEADY_NO_ROOT, 0, .24f, 0, 0, .3f);
    tfp_steady_take(&log, &s);
    CHECK(!tfp_steady_worth(&s));
    /* passes are named; the switch is said; a second of passes only is worth a line when a value was out of range or the game scaled the vector */
    tfp_steady_note(&log, TFP_STEADY_NO_LENS, 0, 0, 0, 0, .3f); tfp_steady_note(&log, TFP_STEADY_NO_LENS, 0, 0, 0, 0, .3f);
    tfp_steady_take(&log, &s);
    CHECK(!tfp_steady_worth(&s));
    for (int i = 0; i < 3; ++i) tfp_steady_note(&log, TFP_STEADY_NO_LENS, 0, 0, 0, 0, .3f);
    tfp_steady_take(&log, &s);
    CHECK(tfp_steady_worth(&s)); /* three frames without the lens: nothing is being held, and that is worth saying */
    tfp_steady_note(&log, TFP_STEADY_NO_LENS, 0, 0, 0, 0, .3f); tfp_steady_note(&log, TFP_STEADY_SCALED, 1, .2f, 0, 0, .3f); tfp_steady_note(&log, TFP_STEADY_RANGE, 0, 0, 0, 0, NAN);
    tfp_steady_note(&log, 99, 0, 0, 0, 0, .3f);
    tfp_steady_take(&log, &s);
    CHECK(tfp_steady_worth(&s));
    tfp_steady_line(&s, text, sizeof text);
    CHECK(strstr(text, "Steady: 4 frames of the rig, the game's depth worked out in 1: 0.2000 to 0.2000 m with the gate set (1 frames), not seen clear; nothing held; "
                       "the rig moved to it in 0 frames, its root with it in 0; ")
          && strstr(text, "; passed in 4 (no lens read for the frame 2, values out of range 1, the game scales the rig by main row +0x11ac 1)"));
    for (int i = 0; i < 4; ++i) tfp_steady_note(&log, TFP_STEADY_OFF, i & 1, (i & 1) ? .1f : .24f, 0, 0, .3f);
    tfp_steady_take(&log, &s); s.off = 1;
    CHECK(tfp_steady_worth(&s)); /* switched off, the game's depth moving: the line that the comparison needs */
    tfp_steady_line(&s, text, sizeof text);
    CHECK(strstr(text, "passed in 4 (depth_steady_off.txt 4); depth_steady_off.txt: nothing is held"));
    for (int i = 0; i < 4; ++i) tfp_steady_note(&log, TFP_STEADY_OFF, 0, .24f, 0, 0, .3f);
    tfp_steady_take(&log, &s); s.off = 1;
    CHECK(!tfp_steady_worth(&s));
    tfp_steady_line(&s, text, 32); CHECK(strlen(text) < 32);
    tfp_steady_take(&log, &s); CHECK(!tfp_steady_worth(&s));
  }
  printf("Depth steady: %d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
