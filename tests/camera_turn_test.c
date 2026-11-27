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
int main(void) {
  /* How long: the steepest point at 200 degrees a second, 120 ms at least. */
  CHECK(fabsf(tfp_turn_duration_ms(29)-217.5f) < .01f && fabsf(tfp_turn_duration_ms(39.5f)-296.25f) < .01f && fabsf(tfp_turn_duration_ms(73)-547.5f) < .01f);
  CHECK(tfp_turn_duration_ms(2) == TFP_TURN_LEAST_MS && tfp_turn_duration_ms(.5f) == TFP_TURN_LEAST_MS);
  /* The three logged turns, at 8, 16.7 and 33 ms a frame, out of the native view and into it. */
  static const double dts[3] = {8, 16.7, 33}; static const float sizes[3] = {29, 39.5f, 73};
  for (int d = 0; d < 3; ++d) for (int s = 0; s < 3; ++s) { play(6, 4, sizes[s], dts[d], 0, NULL); play(4, 6, sizes[s], dts[d], 0, NULL); }
  /* A mouse turn during the ease goes through whole: what is carried is the same frame for frame, bit for bit. */
  for (int d = 0; d < 3; ++d) for (int s = 0; s < 3; ++s) {
    TfpQ still[60], turning[60];
    const int a = play(6, 4, sizes[s], dts[d], 0, still), b = play(6, 4, sizes[s], dts[d], 90, turning);
    CHECK(a == b && memcmp(still, turning, sizeof(TfpQ)*(size_t)a) == 0);
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
  printf("camera turn: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
