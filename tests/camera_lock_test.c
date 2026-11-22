/* Camera lock maths (src/camera_lock.h): the kick the view takes from the head's own movement. */
#include "camera_lock.h"
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static TfpQ about(float x, float y, float z, float degrees) {
  const float h = degrees*TFP_PI/360, s = sinf(h); return tfp_q(x*s, y*s, z*s, cosf(h));
}
/* Damping: the head's pitch against a steady view `ms` after the start of a script: 1 a step of 6 degrees at 100 ms,
 * 2 six degrees there and back within 100 ms (a half sine from 100 to 200 ms). */
static float pitch_at(int which, double ms) {
  if (which == 1) return ms >= 100 ? 6.0f : 0;
  return ms > 100 && ms < 200 ? 6.0f*sinf((float)(ms-100)/100*TFP_PI) : 0;
}
typedef struct Played { float peak, change, last, at; bool sound; } Played;
/* Plays a script at `fps` for `until` ms: the largest kick, the largest change of the kick from one frame to the
 * next, the kick of the last frame and of the frame at `sample` ms (all in degrees), and that every rotation
 * returned was a unit one. */
static Played play(int which, float damp_ms, double fps, double until, double sample) {
  const TfpKickTuning t = {.gain = 1.0f, .max_degrees = 8.0f, .settle_ms = 300.0f, .damp_ms = damp_ms};
  const TfpQ view = about(0, 0, 1, 37);
  TfpKick k; tfp_kick_reset(&k);
  Played p = {0, 0, 0, 0, true}; TfpQ before = tfp_q_identity();
  for (int i = 0; i*1000.0/fps <= until; ++i) {
    const double ms = i*1000.0/fps;
    const TfpQ out = tfp_kick_step(&k, view, tfp_q_mul(view, about(1, 0, 0, pitch_at(which, ms))), (float)(1/fps), &t);
    if (!tfp_q_valid(out) || fabsf(sqrtf(tfp_q_dot(out, out))-1) > 1e-4f) p.sound = false;
    const float size = tfp_degrees_between(out, tfp_q_identity()), change = tfp_degrees_between(out, before);
    if (size > p.peak) p.peak = size;
    if (change > p.change) p.change = change;
    if (ms <= sample) p.at = size;
    p.last = size; before = out;
  }
  return p;
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
    const TfpKickTuning tune = {.gain = 1.0f, .max_degrees = 8.0f, .settle_ms = 300.0f};
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
  /* Damping (damp_ms): the kick is low-passed in time before it is returned. */
  {
    /* 0: the undamped kick, bit for bit, through a step, a turn of the view, a frame that cannot be read and a reset. */
    const TfpKickTuning plain = {.gain = .7f, .max_degrees = 8.0f, .settle_ms = 300.0f}, none = {.gain = .7f, .max_degrees = 8.0f, .settle_ms = 300.0f, .damp_ms = 0};
    TfpKick a, b; tfp_kick_reset(&a); tfp_kick_reset(&b);
    for (int i = 0; i < 240; ++i) {
      const TfpQ view = about(0, 0, 1, 37 + (i > 120 && i < 130 ? .2f*(i-120) : 0));
      TfpQ head = tfp_q_mul(view, about(1, 0, 0, pitch_at(i < 100 ? 1 : 2, i*7.0) + (i > 150 ? 3.0f*sinf(i*.3f) : 0)));
      if (i == 60) head.x = NAN;
      if (i == 200) { tfp_kick_reset(&a); tfp_kick_reset(&b); }
      const TfpQ raw = tfp_kick_raw(&a, view, head, .007f, &plain), out = tfp_kick_step(&b, view, head, .007f, &none);
      CHECK(!memcmp(&raw, &out, sizeof raw));
    }
    static const double rates[] = {30, 60, 144};
    Played step[3], brief[3];
    for (int r = 0; r < 3; ++r) {
      /* 60 % (180 ms). A 6 degree step of the head: undamped it is in the view in one frame; damped, no frame turns
       * the view by more than the time constant allows, and it ends where the undamped one ends (settled). */
      const Played raw = play(1, 0, rates[r], 4000, 350);
      step[r] = play(1, 180, rates[r], 4000, 350);
      CHECK(raw.sound && step[r].sound);
      CHECK(raw.change > 5.0f && raw.peak > 5.0f); /* less what the head's rest takes in that frame */
      CHECK(step[r].peak > 1.5f && step[r].peak < raw.peak*.6f);
      CHECK(step[r].change < 6.0f*(float)(1000/rates[r]/180)*1.02f + .01f);
      CHECK(raw.last < .02f && step[r].last < .02f);
      /* A brief movement of the head (100 ms there and back) reaches the view at well under half its undamped size. */
      const Played jerk = play(2, 0, rates[r], 1500, 250);
      brief[r] = play(2, 180, rates[r], 1500, 250);
      CHECK(jerk.sound && brief[r].sound && jerk.peak > 4.5f && brief[r].peak > .3f && brief[r].peak < jerk.peak*.45f);
    }
    for (int r = 0; r < 3; ++r) printf("damping 60 %%, %g frames a second: step peak %.2f, at 250 ms %.2f, largest change in a frame %.3f; brief peak %.2f\n", rates[r], step[r].peak, step[r].at, step[r].change, brief[r].peak);
    /* The same at every frame rate: the kick 250 ms after the step, and the peaks. */
    for (int r = 0; r < 3; ++r) {
      CHECK(step[r].at > 1.5f && fabsf(step[r].at - step[1].at) < .2f && fabsf(step[r].peak - step[1].peak) < .2f);
      CHECK(fabsf(brief[r].peak - brief[1].peak) < .2f);
    }
    /* More damping is calmer: 100 % (300 ms) passes less of the brief movement than 60 %. */
    CHECK(play(2, 300, 60, 1500, 250).peak < brief[1].peak*.8f);
    /* A reset forgets the damped kick; a frame that cannot be read gives none and the next ones are sound. */
    const TfpKickTuning damped = {.gain = 1.0f, .max_degrees = 8.0f, .settle_ms = 300.0f, .damp_ms = 180};
    const TfpQ view = about(0, 0, 1, 37), dipped = tfp_q_mul(view, about(1, 0, 0, 6));
    TfpKick k; tfp_kick_reset(&k);
    tfp_kick_step(&k, view, view, .016f, &damped);
    TfpQ out = tfp_q_identity();
    for (int i = 0; i < 6; ++i) out = tfp_kick_step(&k, view, dipped, .016f, &damped);
    CHECK(tfp_degrees_between(out, tfp_q_identity()) > 1.0f);
    tfp_kick_reset(&k);
    CHECK(tfp_degrees_between(tfp_kick_step(&k, view, dipped, .016f, &damped), tfp_q_identity()) == 0);
    CHECK(tfp_degrees_between(tfp_kick_step(&k, view, dipped, .016f, &damped), tfp_q_identity()) < .001f); /* the head rests there now */
    for (int i = 0; i < 6; ++i) out = tfp_kick_step(&k, view, view, .016f, &damped);
    CHECK(tfp_degrees_between(out, tfp_q_identity()) > 1.0f);
    TfpQ bad = view; bad.w = NAN;
    out = tfp_kick_step(&k, view, bad, .016f, &damped);
    CHECK(out.x == 0 && out.y == 0 && out.z == 0 && out.w == 1);
    out = tfp_kick_step(&k, view, view, .016f, &damped);
    CHECK(out.x == 0 && out.y == 0 && out.z == 0 && out.w == 1);
    const TfpKickTuning odd = {.gain = 1.0f, .max_degrees = 8.0f, .settle_ms = 300.0f, .damp_ms = NAN};
    for (int i = 0; i < 6; ++i) { out = tfp_kick_step(&k, view, i & 1 ? dipped : view, .016f, &odd); CHECK(tfp_q_valid(out) && fabsf(sqrtf(tfp_q_dot(out, out))-1) < 1e-4f); }
  }
  printf("camera lock: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
