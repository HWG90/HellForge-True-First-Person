/* Round 49: the direction shown across a change of camera (src/camera_turn.h). */
#include "camera_turn.h"
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static TfpQ about(float x, float y, float z, float degrees) {
  const float n = sqrtf(x*x+y*y+z*z), h = degrees*TFP_PI/360;
  return tfp_q(x/n*sinf(h), y/n*sinf(h), z/n*sinf(h), cosf(h));
}
static float between(TfpQ a, TfpQ b) { return tfp_turn_degrees(tfp_q_normalised(tfp_q_mul(tfp_q_inv(a), b))); }
static bool same(TfpQ a, TfpQ b) { return memcmp(&a, &b, sizeof a) == 0; }
/* One change of camera of `degrees` after ten frames, `dt` ms a frame, the game's direction turning `mouse` degrees a
 * second from the change on. Checks the ease; `carried`, when given, takes what is carried in each frame from the
 * change on (60 at most) and the count comes back. */
static int play(uint32_t from, uint32_t to, float degrees, double dt, float mouse, TfpQ *carried) {
  TfpTurn t; tfp_turn_reset(&t);
  const TfpQ before = about(0, 0, 1, 20), after = tfp_q_normalised(tfp_q_mul(before, about(.3f, .1f, 1, degrees)));
  double tick = 5000; float began = -1;
  for (int i = 0; i < 10; ++i, tick += dt) {
    const TfpQ carry = tfp_turn_step(&t, before, true, from, tick, 250, &began);
    CHECK(tfp_turn_none(carry) && began == 0 && same(tfp_turn_apply(before, carry), before));
  }
  const float duration = tfp_turn_duration_ms(degrees);
  float left = degrees; int frames = 0; TfpQ last = before;
  for (int i = 0; i*dt < duration+400; ++i, tick += dt) {
    const TfpQ game = tfp_q_normalised(tfp_q_mul(after, about(0, 0, 1, mouse*(float)(i*dt)/1000)));
    const TfpQ carry = tfp_turn_step(&t, game, true, to, tick, 250, &began);
    const TfpQ shown = tfp_turn_apply(game, carry);
    const float off = between(game, shown);
    CHECK(tfp_q_valid(shown));
    if (i == 0) {
      CHECK(fabsf(began-degrees) < .02f);
      CHECK(between(before, shown) < .02f); /* the frame of the change is shown the way the frame before it was */
    } else CHECK(began == 0);
    CHECK(off <= left+1e-3f);                                   /* monotone */
    CHECK(left-off <= TFP_TURN_RATE_DEG_S*(float)dt/1000+.02f); /* never faster than the rate */
    if (!(mouse != 0)) CHECK(between(last, shown) <= TFP_TURN_RATE_DEG_S*(float)dt/1000+.02f);
    if (i*dt >= duration) { CHECK(tfp_turn_none(carry)); CHECK(same(shown, game)); } /* over: the game's own, bit for bit */
    if (carried && frames < 60) carried[frames] = carry;
    ++frames; left = off; last = shown;
  }
  CHECK(left < .01f && !t.active);
  return frames < 60 ? frames : 60;
}
/* Round 49, the check's blocker: a pan through a change out of the ordinary camera into the native view. At that change
 * the frame before is a whole frame earlier, so the game's new direction differs from the one last shown by the
 * player's own turn of one frame as well. `rate`: the pan about the world's up, degrees a second, under a view pitched
 * 20 degrees, `before` frames of the ordinary camera `dt` ms apart (every `twice`th one called a second time in its
 * frame, 0: none), then the change `gap` ms later and the native view at `dt`; from the change on the pan is `after`
 * degrees a second and the game's direction `degrees` off in the view's own frame. Returns the degrees the change began
 * a carry with; `carried` and `shown` (40 each), when given, take the frames from the change on. */
static float pan_kinds(uint32_t k0, uint32_t k1, float rate, float after, float degrees, double dt, double gap, int twice, TfpQ *carried, TfpQ *shown, TfpQ *last_before) {
  TfpTurn t; tfp_turn_reset(&t);
  const TfpQ pitch = about(1, 0, 0, 20), off = about(.3f, .1f, 1, degrees);
  double tick = 9000; float yaw = 12, began = -1, first = 0;
  for (int i = 0; i < 12; ++i) {
    const TfpQ game = tfp_q_normalised(tfp_q_mul(about(0, 0, 1, yaw), pitch));
    for (int call = 0; call < (twice && i%twice == 0 ? 2 : 1); ++call) {
      const TfpQ carry = tfp_turn_step(&t, game, true, k0, tick+call*.3, 250, &began);
      CHECK(tfp_turn_none(carry) && began == 0);
    }
    if (last_before) *last_before = game;
    if (i < 11) { tick += dt; yaw += rate*(float)dt/1000; }
  }
  tick += gap; yaw += after*(float)gap/1000;
  for (int i = 0; i < 40; ++i) {
    TfpQ game = tfp_q_normalised(tfp_q_mul(about(0, 0, 1, yaw), pitch));
    if (degrees != 0) game = tfp_q_normalised(tfp_q_mul(game, off));
    const TfpQ carry = tfp_turn_step(&t, game, true, k1, tick, 250, &began);
    if (i == 0) first = began; else CHECK(began == 0);
    if (carried) carried[i] = carry;
    if (shown) shown[i] = tfp_turn_apply(game, carry);
    if (degrees == 0 && after == rate) CHECK(tfp_turn_none(carry) && same(tfp_turn_apply(game, carry), game)); /* main's frame, bit for bit */
    tick += dt; yaw += after*(float)dt/1000;
  }
  return first;
}
static float pan(float rate, float after, float degrees, double dt, double gap, int twice, TfpQ *carried, TfpQ *shown, TfpQ *last_before) {
  return pan_kinds(4, 6, rate, after, degrees, dt, gap, twice, carried, shown, last_before);
}
int main(void) {
  /* How long: the steepest point at 200 degrees a second, 120 ms at least. */
  CHECK(fabsf(tfp_turn_duration_ms(29)-217.5f) < .01f && fabsf(tfp_turn_duration_ms(39.5f)-296.25f) < .01f && fabsf(tfp_turn_duration_ms(73)-547.5f) < .01f);
  CHECK(tfp_turn_duration_ms(2) == TFP_TURN_LEAST_MS && tfp_turn_duration_ms(.5f) == TFP_TURN_LEAST_MS);
  /* The ease is rest to rest, not a straight line: a quarter of the way through 27/32 is still carried (a straight line:
   * 3/4), half way a half, three quarters 5/32; nothing before it began is more than the whole, nothing after it is over. */
  {
    const TfpQ from = about(.3f, .1f, 1, 40);
    CHECK(fabsf(tfp_turn_degrees(tfp_turn_carry_at(from, 75, 300))-40*27/32.f) < .01f);
    CHECK(fabsf(tfp_turn_degrees(tfp_turn_carry_at(from, 150, 300))-20) < .01f);
    CHECK(fabsf(tfp_turn_degrees(tfp_turn_carry_at(from, 225, 300))-40*5/32.f) < .01f);
    CHECK(fabsf(tfp_turn_degrees(tfp_turn_carry_at(from, -5, 300))-40) < .01f && tfp_turn_none(tfp_turn_carry_at(from, 300, 300)) && tfp_turn_none(tfp_turn_carry_at(from, 10, 0)));
  }
  /* The three logged turns, at 8, 16.7 and 33 ms a frame, out of the native view and into it. */
  static const double dts[3] = {8, 16.7, 33}; static const float sizes[3] = {29, 39.5f, 73};
  for (int d = 0; d < 3; ++d) for (int s = 0; s < 3; ++s) { play(6, 4, sizes[s], dts[d], 0, NULL); play(4, 6, sizes[s], dts[d], 0, NULL); }
  /* A mouse turn during the ease goes through whole: what is carried is the same frame for frame, bit for bit. */
  for (int d = 0; d < 3; ++d) for (int s = 0; s < 3; ++s) {
    TfpQ still[60], turning[60];
    const int a = play(6, 4, sizes[s], dts[d], 0, still), b = play(6, 4, sizes[s], dts[d], 90, turning);
    CHECK(a == b && memcmp(still, turning, sizeof(TfpQ)*(size_t)a) == 0);
  }
  /* The check's blocker. A pan through a change out of the ordinary camera into the native view, the two cameras
   * agreeing: nothing is carried, every frame is the game's own bit for bit (main's), at 100 and 300 degrees a second
   * (and 30 and 600), at 8, 16.7 and 33 ms a frame, the change a frame later or two, and where the ordinary camera was
   * called twice in some frames (tests/reference/trace-two-calls-one-frame.log). */
  {
    static const float rates[4] = {30, 100, 300, 600}; static const double frames[3] = {8, 16.7, 33};
    for (int r = 0; r < 4; ++r) for (int d = 0; d < 3; ++d) for (int twice = 0; twice < 4; ++twice) {
      CHECK(pan(rates[r], rates[r], 0, frames[d], frames[d], twice, NULL, NULL, NULL) == 0);
      CHECK(pan(rates[r], rates[r], 0, frames[d], 2*frames[d], twice, NULL, NULL, NULL) == 0);
      CHECK(pan(-rates[r], -rates[r], 0, frames[d], frames[d], twice, NULL, NULL, NULL) == 0);
    }
    /* a pan that speeds up a tenth in the frame of the change: what is left is under the skip */
    CHECK(pan(100, 110, 0, 16.7, 16.7, 0, NULL, NULL, NULL) == 0);
    /* a pan that stops dead in the frame of the change: one frame's turn is left over and carried (it cannot be told
     * from the cameras pointing apart), no more than that */
    { const float began = pan(300, 0, 0, 16.7, 16.7, 0, NULL, NULL, NULL); CHECK(fabsf(began-300*.0167f) < .05f); }
    /* Round 49f check: the other way, out of the native view into the ordinary camera, is usually 0-1 ms apart but the logs
     * hold 6 -> 4 changes a whole frame (33.6 ms at 30 fps) and most of one apart: a pan through them is the game's own too. */
    for (int r = 0; r < 4; ++r) for (int d = 0; d < 3; ++d) for (int twice = 0; twice <= 2; twice += 2) {
      CHECK(pan_kinds(6, 4, rates[r], rates[r], 0, frames[d], frames[d], twice, NULL, NULL, NULL) == 0);
      CHECK(pan_kinds(6, 4, rates[r], rates[r], 0, frames[d], frames[d]/2, twice, NULL, NULL, NULL) == 0);
      CHECK(pan_kinds(6, 4, rates[r], rates[r], 0, frames[d], 1, twice, NULL, NULL, NULL) == 0);
      CHECK(pan_kinds(6, 4, -rates[r], -rates[r], 0, frames[d], frames[d], twice, NULL, NULL, NULL) == 0);
    }
  }
  /* The same pan with the cameras really 29 degrees apart at the change: the 29 degrees are carried (not 29 and the
   * pan's frame), the frame of the change is shown where the pan would have taken the last one, and from there what
   * is carried is what the still view carries frame for frame: the pan goes through whole while the 29 degrees ease. */
  {
    static const float rates[2] = {100, 300}; static const double frames[2] = {16.7, 33};
    for (int r = 0; r < 2; ++r) for (int d = 0; d < 2; ++d) {
      TfpQ still[40], panned[40], shown[40], before;
      const float a = pan(0, 0, 29, frames[d], frames[d], 0, still, NULL, NULL);
      const float b = pan(rates[r], rates[r], 29, frames[d], frames[d], 0, panned, shown, &before);
      CHECK(fabsf(a-29) < .02f && fabsf(b-29) < .05f);
      const TfpQ expected = tfp_q_normalised(tfp_q_mul(about(0, 0, 1, rates[r]*(float)frames[d]/1000), before));
      CHECK(between(expected, shown[0]) < .05f);
      for (int i = 0; i < 40; ++i) CHECK(between(still[i], panned[i]) < .05f);
      CHECK(tfp_turn_none(panned[39]) && !tfp_turn_none(panned[3]));
    }
  }
  /* No difference, and one under half a degree: nothing carried, the argument bit for bit. */
  for (int which = 0; which < 2; ++which) {
    TfpTurn t; tfp_turn_reset(&t); float began = -1;
    const TfpQ a = about(.2f, .1f, 1, 33), b = which ? tfp_q_normalised(tfp_q_mul(a, about(0, 0, 1, .4f))) : a;
    for (int i = 0; i < 5; ++i) CHECK(tfp_turn_none(tfp_turn_step(&t, a, true, 6, 100+i*16.7, 250, &began)));
    for (int i = 5; i < 12; ++i) { const TfpQ carry = tfp_turn_step(&t, b, true, 4, 100+i*16.7, 250, &began);
      CHECK(tfp_turn_none(carry) && began == 0 && same(tfp_turn_apply(b, carry), b) && !t.active); }
    for (int i = 12; i < 18; ++i) CHECK(tfp_turn_none(tfp_turn_step(&t, a, true, 6, 100+i*16.7, 250, &began)));
  }
  /* Over 120 degrees is another view; so is the first frame after a pause; neither is carried. */
  {
    TfpTurn t; tfp_turn_reset(&t); float began = -1;
    const TfpQ a = about(0, 0, 1, 10), far = tfp_q_normalised(tfp_q_mul(a, about(0, 0, 1, 150))), near = tfp_q_normalised(tfp_q_mul(a, about(0, 0, 1, 40)));
    tfp_turn_step(&t, a, true, 6, 1000, 250, &began);
    CHECK(tfp_turn_none(tfp_turn_step(&t, far, true, 4, 1016, 250, &began)) && began == 0);
    tfp_turn_reset(&t); tfp_turn_step(&t, a, true, 6, 1000, 250, &began);
    CHECK(tfp_turn_none(tfp_turn_step(&t, near, true, 4, 1300, 250, &began)) && began == 0);
    /* an ease under way does not outlive a pause either */
    tfp_turn_reset(&t); tfp_turn_step(&t, a, true, 6, 1000, 250, &began);
    CHECK(!tfp_turn_none(tfp_turn_step(&t, near, true, 4, 1016, 250, &began)) && began > 39);
    CHECK(tfp_turn_none(tfp_turn_step(&t, near, true, 4, 1016+300, 250, &began)));
    /* a kind that is neither camera's (a death camera) is no change to carry across */
    tfp_turn_reset(&t); tfp_turn_step(&t, a, true, 5, 1000, 250, &began);
    CHECK(tfp_turn_none(tfp_turn_step(&t, near, true, 4, 1016, 250, &began)));
  }
  /* A second change while a carry is under way starts from what is shown then: no turn in that frame. */
  {
    TfpTurn t; tfp_turn_reset(&t); float began = 0;
    const TfpQ a = about(0, 0, 1, 0), b = about(0, 0, 1, -29), c = tfp_q_normalised(tfp_q_mul(about(0, 0, 1, -17), about(1, 0, 0, 38)));
    TfpQ shown = a; double tick = 1000;
    tfp_turn_step(&t, a, true, 6, tick, 250, &began);
    for (int i = 0; i < 4; ++i) { tick += 33; shown = tfp_turn_apply(b, tfp_turn_step(&t, b, true, 4, tick, 250, &began)); }
    CHECK(between(b, shown) > 5); /* still under way */
    tick += 33;
    const TfpQ next = tfp_turn_apply(c, tfp_turn_step(&t, c, true, 6, tick, 250, &began));
    CHECK(between(shown, next) < .02f && began > 20);
    float left = between(c, next);
    for (int i = 0; i < 20; ++i) { tick += 33; const float off = between(c, tfp_turn_apply(c, tfp_turn_step(&t, c, true, 6, tick, 250, &began)));
      CHECK(off <= left+1e-3f && left-off <= 6.62f); left = off; }
    CHECK(left < .01f && !t.active);
  }
  /* A frame that does not know the game's direction carries on with the ease and is no start for the next change. */
  {
    TfpTurn t; tfp_turn_reset(&t); float began = 0;
    const TfpQ a = about(0, 0, 1, 0), b = about(0, 0, 1, 40);
    tfp_turn_step(&t, a, true, 6, 1000, 250, &began);
    const TfpQ first = tfp_turn_step(&t, b, true, 4, 1016, 250, &began), second = tfp_turn_step(&t, b, false, 4, 1033, 250, &began);
    CHECK(!tfp_turn_none(first) && !tfp_turn_none(second) && tfp_turn_degrees(second) < tfp_turn_degrees(first) && !t.known);
    const TfpQ third = tfp_turn_step(&t, a, true, 6, 1050, 250, &began);
    CHECK(began == 0 && tfp_turn_degrees(third) < tfp_turn_degrees(second)); /* the ease goes on; nothing new is begun */
  }
  /* A frame that carries nothing by rule (full aim: the sights stay on the eye): a carry under way ends, none begins,
   * and the frame is still the start for the next change. */
  {
    TfpTurn t; tfp_turn_reset(&t); float began = 0;
    const TfpQ a = about(0, 0, 1, 0), b = about(0, 0, 1, 40);
    tfp_turn_step(&t, a, true, 4, 1000, 250, &began);
    tfp_turn_drop(&t, b, true, 6, 1016);                      /* into full aim, 40 degrees off: as the game gives it */
    CHECK(!t.active && t.known && same(t.shown, b) && t.kind == 6);
    const TfpQ out = tfp_turn_step(&t, a, true, 4, 1033, 250, &began); /* and out again: carried from what full aim showed */
    CHECK(!tfp_turn_none(out) && fabsf(began-40) < .02f);
    tfp_turn_drop(&t, a, true, 6, 1050);
    CHECK(!t.active && tfp_turn_none(tfp_turn_step(&t, a, true, 6, 1066, 250, &began)));
    tfp_turn_drop(&t, a, false, 6, 1083); CHECK(!t.known);
  }
  /* THE DIRECTION: the rig eye's axes times the local argument. */
  {
    const float yaw = 1.1f, c = cosf(yaw), s = sinf(yaw);
    const float basis[9] = {c, s, 0, -s, c, 0, 0, 0, 1}; /* right, forward, up: turned 63 degrees about up */
    float forward[3]; TfpQ world;
    tfp_turn_forward(basis, tfp_q_identity(), forward);
    CHECK(fabsf(forward[0]+s) < 1e-6f && fabsf(forward[1]-c) < 1e-6f && forward[2] == 0);
    const TfpQ local = about(1, 0, 0, 30); /* pitched up 30 degrees in the eye's own frame */
    tfp_turn_forward(basis, local, forward);
    CHECK(fabsf(forward[2]-.5f) < 1e-5f && fabsf(forward[0]+s*cosf(TFP_PI/6)) < 1e-5f && fabsf(forward[1]-c*cosf(TFP_PI/6)) < 1e-5f);
    CHECK(tfp_turn_world(basis, local, &world));
    const float ahead[3] = {0, 1, 0}; float through[3];
    tfp_q_rotate(world, ahead, through);
    for (int i = 0; i < 3; ++i) CHECK(fabsf(through[i]-forward[i]) < 1e-5f);
    const float skew[9] = {1, 0, 0, .5f, .5f, 0, 0, 0, 1};
    CHECK(!tfp_turn_world(skew, local, &world));
  }
  /* Round 55, THE DIRECTION SHOWN: tfp_turn_peek tells the carry of a frame before the frame is played (the head
   * camera's eye is built along it), the same to the bit as tfp_turn_step will give, and leaves the state as it was. A
   * leave of 63 degrees, every frame of its ease; and a frame of no change (the identity, bit for bit). */
  {
    TfpTurn t; tfp_turn_reset(&t);
    const TfpQ before = about(0, 0, 1, 20), after = tfp_q_normalised(tfp_q_mul(before, about(0, 0, 1, -63)));
    double tick = 7000; float began = 0;
    for (int i = 0; i < 6; ++i, tick += 33.3) tfp_turn_step(&t, before, true, 6, tick, 250, &began);
    int carried = 0, agree = 0, frames = 0;
    for (int i = 0; i < 24; ++i, tick += 33.3, ++frames) {
      const TfpTurn kept = t;
      const TfpQ peek = tfp_turn_peek(&t, after, true, 4, tick, 250);
      CHECK(memcmp(&kept, &t, sizeof t) == 0); /* nothing of the state changed */
      const TfpQ carry = tfp_turn_step(&t, after, true, 4, tick, 250, &began);
      agree += same(peek, carry); carried += !tfp_turn_none(carry);
      if (i == 0) CHECK(fabsf(began-63) < .02f && between(before, tfp_turn_apply(after, peek)) < .02f);
    }
    CHECK(agree == frames && carried > 10);
    const TfpQ none = tfp_turn_peek(&t, after, true, 4, tick, 250);
    CHECK(tfp_turn_none(none) && same(none, tfp_q_identity()));
  }
  printf("camera turn: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
