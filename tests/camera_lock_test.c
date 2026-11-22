/* Camera lock maths (src/camera_lock.h): the kick the view takes from the head's own movement. */
#include "camera_lock.h"
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static TfpQ about(float x, float y, float z, float degrees) {
  const float h = degrees*TFP_PI/360, s = sinf(h); return tfp_q(x*s, y*s, z*s, cosf(h));
}
int main(void) {
  const TfpKickTuning t = {.gain = .5f, .max_degrees = 10, .settle_ms = 300};
  TfpKick k; tfp_kick_reset(&k);
  const TfpQ camera = about(0, 0, 1, 40);            /* looking 40 degrees to the side */
  const TfpQ head0 = tfp_q_mul(camera, about(1, 0, 0, 5));
  /* The first frame and a still head: no kick. */
  CHECK(tfp_degrees_between(tfp_kick_step(&k, camera, head0, .016f, &t), tfp_q_identity()) < .001f);
  for (int i = 0; i < 20; ++i) CHECK(tfp_degrees_between(tfp_kick_step(&k, camera, head0, .016f, &t), tfp_q_identity()) < .001f);
  /* Turning the view with the head following exactly: no kick (mouse look is not a body movement). */
  for (int i = 1; i <= 30; ++i) {
    const TfpQ turned = about(0, 0, 1, 40 + 3.0f*i);
    CHECK(tfp_degrees_between(tfp_kick_step(&k, turned, tfp_q_mul(turned, about(1, 0, 0, 5)), .016f, &t), tfp_q_identity()) < .01f);
  }
  /* The head pitches 8 degrees in one frame: half of it reaches the view, about the same axis, in the camera's frame. */
  tfp_kick_reset(&k); tfp_kick_step(&k, camera, head0, .016f, &t);
  const TfpQ kicked = tfp_kick_step(&k, camera, tfp_q_mul(camera, tfp_q_mul(about(1, 0, 0, 8), about(1, 0, 0, 5))), .016f, &t);
  CHECK(tfp_degrees_between(kicked, about(1, 0, 0, 4 * expf(-.016f/.3f))) < .05f);
  /* It settles: after two seconds of the head staying there the kick is gone. */
  TfpQ last = kicked;
  for (int i = 0; i < 125; ++i) last = tfp_kick_step(&k, camera, tfp_q_mul(camera, tfp_q_mul(about(1, 0, 0, 8), about(1, 0, 0, 5))), .016f, &t);
  CHECK(tfp_degrees_between(last, tfp_q_identity()) < .05f);
  /* A violent movement (the head thrown 120 degrees) is limited to the maximum. */
  tfp_kick_reset(&k); tfp_kick_step(&k, camera, head0, .016f, &t);
  const TfpQ thrown = tfp_kick_step(&k, camera, tfp_q_mul(camera, about(0, 1, 0, 120)), .016f, &t);
  CHECK(fabsf(tfp_degrees_between(thrown, tfp_q_identity()) - 10) < .05f);
  /* Gain 0 switches it off; a long gap or a bad value starts again without a kick. */
  const TfpKickTuning off = {.gain = 0, .max_degrees = 10, .settle_ms = 300};
  tfp_kick_reset(&k); tfp_kick_step(&k, camera, head0, .016f, &off);
  CHECK(tfp_degrees_between(tfp_kick_step(&k, camera, tfp_q_mul(camera, about(0, 1, 0, 60)), .016f, &off), tfp_q_identity()) < .001f);
  tfp_kick_reset(&k); tfp_kick_step(&k, camera, head0, .016f, &t);
  CHECK(tfp_degrees_between(tfp_kick_step(&k, camera, tfp_q_mul(camera, about(0, 1, 0, 60)), 0, &t), tfp_q_identity()) < .001f);
  CHECK(tfp_degrees_between(tfp_kick_step(&k, camera, tfp_q(NAN, 0, 0, 1), .016f, &t), tfp_q_identity()) < .001f);
  /* Seen in the game (2026-11-22): the head does not follow the view rigidly, so a turn of the mouse or the recoil
   * of a shot moved the view against the head and came back as a kick. While the view itself turns fast
   * (60 degrees a second and more) nothing is added, and nothing is left over when it stops. */
  tfp_kick_reset(&k); tfp_kick_step(&k, camera, head0, .016f, &t);
  float worst = 0;
  for (int i = 1; i <= 40; ++i) { /* 2 degrees a frame = 125 degrees a second; the head stays where it was */
    const float size = tfp_degrees_between(tfp_kick_step(&k, tfp_q_mul(camera, about(1, 0, 0, 2.0f*i)), head0, .016f, &t), tfp_q_identity());
    if (size > worst) worst = size;
  }
  CHECK(worst < .01f);
  for (int i = 0; i < 20; ++i) { /* the view stops: no kick appears afterwards */
    const float size = tfp_degrees_between(tfp_kick_step(&k, tfp_q_mul(camera, about(1, 0, 0, 80)), head0, .016f, &t), tfp_q_identity());
    if (size > worst) worst = size;
  }
  CHECK(worst < .01f);
  /* Applying: the view is the game's rotation turned by the kick in its own frame. */
  const TfpQ applied = tfp_kick_apply(camera, about(1, 0, 0, 4));
  CHECK(tfp_degrees_between(applied, tfp_q_mul(camera, about(1, 0, 0, 4))) < .001f);
  /* The same kick at any frame rate: a view that is still but carries rounding in its last bit (rebuilt every frame
   * from a matrix) is still. Measured through acos, one step below 1 reads as 0.04 degrees, which at 2 ms a frame is
   * 20 degrees a second of "turning": the kick came out weaker and gone in three frames at 500 frames a second. */
  {
    const TfpKickTuning tune = {1.0f, 8.0f, 300.0f};
    float sizes[2];
    for (int noisy = 0; noisy < 2; ++noisy) {
      TfpKick kick; tfp_kick_reset(&kick);
      const TfpQ steady = about(0, 0, 1, 37);
      TfpQ rough = steady; /* the nearest rotation the acos measure tells apart from `steady` */
      for (int n = 0; n < 4096 && !(tfp_degrees_between(steady, rough) > 0); ++n) { rough.w = nextafterf(rough.w, 0); rough.x = nextafterf(rough.x, 1); }
      if (!noisy) CHECK(tfp_degrees_between(steady, rough) > .03f);
      const TfpQ at_rest = tfp_q_mul(steady, about(1, 0, 0, 0)), dipped = tfp_q_mul(steady, about(1, 0, 0, 6));
      for (int i = 0; i < 50; ++i) tfp_kick_step(&kick, noisy && (i & 1) ? rough : steady, at_rest, .002f, &tune);
      TfpQ out = tfp_q_identity();
      for (int i = 0; i < 25; ++i) out = tfp_kick_step(&kick, noisy && (i & 1) ? rough : steady, dipped, .002f, &tune); /* 50 ms into a 6 degree dip */
      sizes[noisy] = tfp_degrees_between(out, tfp_q_identity());
    }
    CHECK(sizes[0] > 4.5f && sizes[0] < 5.5f);            /* 6 degrees less 50 ms of a 300 ms settle */
    CHECK(fabsf(sizes[1] - sizes[0]) < .15f);
  }
  printf("camera lock: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
