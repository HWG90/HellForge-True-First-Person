/* Head-camera eye damping (src/camera_damp.h): the eye follows the head through a low-pass, with a limited lag. */
#include "camera_damp.h"
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static float apart(const float *a, const float *b) {
  return sqrtf((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}
static bool same(const float *a, const float *b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }
/* The head's eye `ms` after the start of a script: 0 still, 1 a step of `size` at 100 ms, 2 `size` there and back
 * within 100 ms (a half sine from 100 to 200 ms). */
static void script(int which, float size, double ms, float *eye) {
  float moved = 0;
  if (which == 1) moved = ms >= 100 ? size : 0;
  if (which == 2) moved = ms > 100 && ms < 200 ? size*sinf((float)(ms-100)/100*TFP_PI) : 0;
  eye[0] = .03f; eye[1] = .24f+moved*.6f; eye[2] = 1.54f-moved*.8f;
}
/* Plays a script at `fps` for `until` ms. Returns the farthest the presented eye was from its start; `last`, when
 * given, takes the presented eye of the last frame, `lag` the largest distance from the true eye. */
static float play(int which, float size, float damp_ms, double fps, double until, float *last, float *lag) {
  TfpEyeDamp d; tfp_eye_damp_reset(&d);
  float start[3], eye[3], out[3] = {0}, peak = 0; script(which, size, 0, start);
  if (lag) *lag = 0;
  for (int i = 0; i*1000.0/fps <= until; ++i) {
    const double ms = i*1000.0/fps;
    script(which, size, ms, eye);
    const float behind = tfp_eye_damp_step(&d, eye, 5000+ms, 250, damp_ms, out);
    CHECK(fabsf(behind-apart(out, eye)) < 1e-5f);
    if (lag && behind > *lag) *lag = behind;
    if (apart(out, start) > peak) peak = apart(out, start);
  }
  if (last) memcpy(last, out, 12);
  return peak;
}
int main(void) {
  /* The option's percentage as a time constant: 0 none, 100 % 300 ms, linear; anything else is kept inside that. */
  CHECK(tfp_camera_damping_ms(0) == 0 && tfp_camera_damping_ms(60) == 180 && tfp_camera_damping_ms(100) == 300);
  CHECK(tfp_camera_damping_ms(150) == 300 && tfp_camera_damping_ms(-5) == 0 && tfp_camera_damping_ms(NAN) == 0);
  /* Without damping the eye is the true one in every frame, exactly. */
  {
    TfpEyeDamp d; tfp_eye_damp_reset(&d);
    for (int i = 0; i < 40; ++i) {
      float eye[3], out[3] = {9, 9, 9}; script(2, .05f, i*8.0, eye);
      CHECK(tfp_eye_damp_step(&d, eye, 5000+i*8.0, 250, 0, out) == 0 && same(out, eye) && !d.known);
    }
  }
  /* The first frame is the true eye; a still head stays exactly where it is. */
  {
    TfpEyeDamp d; tfp_eye_damp_reset(&d);
    float eye[3], out[3]; script(0, 0, 0, eye);
    CHECK(tfp_eye_damp_step(&d, eye, 5000, 250, 180, out) == 0 && same(out, eye) && d.known);
    for (int i = 1; i < 30; ++i) CHECK(tfp_eye_damp_step(&d, eye, 5000+i*16.0, 250, 180, out) == 0 && same(out, eye));
  }
  /* A step of the head: the eye goes there on the time constant, the same at any frame rate, and arrives. */
  {
    static const double rates[] = {30, 60, 144};
    float at[3][3], end[3], truth[3], start[3]; script(1, .05f, 0, start); script(1, .05f, 2000, truth);
    for (int r = 0; r < 3; ++r) {
      float lag = 0;
      play(1, .05f, 180, rates[r], 100+180+500/rates[r], at[r], &lag); /* one time constant after the step, to within a frame */
      CHECK(lag <= .05f+1e-5f);
      play(1, .05f, 180, rates[r], 2000, end, NULL);
      CHECK(apart(end, truth) < .0005f);
    }
    CHECK(fabsf(apart(at[1], start)-.05f*(1-expf(-1))) < .006f);
    CHECK(apart(at[0], at[1]) < .006f && apart(at[2], at[1]) < .004f);
    /* Nothing moves more in one frame than the time constant allows. */
    TfpEyeDamp d; tfp_eye_damp_reset(&d);
    float before[3], eye[3], out[3], most = 0; script(1, .05f, 0, before);
    for (int i = 0; i < 60; ++i) { script(1, .05f, i*1000.0/60, eye); tfp_eye_damp_step(&d, eye, 5000+i*1000.0/60, 250, 180, out);
      if (apart(out, before) > most) most = apart(out, before);
      memcpy(before, out, 12); }
    CHECK(most > .001f && most < .05f*(1000.0f/60/180)*1.01f);
  }
  /* A head that jerks 0.05 m there and back within 100 ms moves the eye clearly less than it moved itself. */
  {
    static const double rates[] = {30, 60, 144};
    for (int r = 0; r < 3; ++r) {
      const float raw = play(2, .05f, 0, rates[r], 600, NULL, NULL), damped = play(2, .05f, 180, rates[r], 600, NULL, NULL);
      CHECK(raw > .04f && damped > .002f && damped < raw*.5f);
    }
  }
  /* The lag is limited: however far the head goes in one frame, the eye is never more than 0.10 m behind it. */
  {
    TfpEyeDamp d; tfp_eye_damp_reset(&d);
    float eye[3] = {0, .2f, 1.5f}, out[3];
    tfp_eye_damp_step(&d, eye, 5000, 250, 300, out);
    eye[0] = .3f; eye[1] = -.2f; eye[2] = 1.5f; /* 0.5 m in one frame */
    const float behind = tfp_eye_damp_step(&d, eye, 5016, 250, 300, out);
    CHECK(fabsf(behind-TFP_EYE_DAMP_LAG_M) < 1e-5f && fabsf(apart(out, eye)-TFP_EYE_DAMP_LAG_M) < 1e-5f);
    for (int i = 2; i < 200; ++i) { eye[0] += .02f; CHECK(tfp_eye_damp_step(&d, eye, 5000+i*16.0, 250, 300, out) <= TFP_EYE_DAMP_LAG_M+1e-5f); }
  }
  /* It starts again from the true eye: after a pause longer than the gap, a tick that goes back, a reset, an input
   * that is not a number, and a frame without damping. */
  {
    TfpEyeDamp d; tfp_eye_damp_reset(&d);
    float a[3] = {0, .2f, 1.5f}, b[3] = {0, .25f, 1.5f}, out[3];
    tfp_eye_damp_step(&d, a, 5000, 250, 180, out);
    CHECK(tfp_eye_damp_step(&d, b, 5016, 250, 180, out) > .04f);             /* behind */
    CHECK(tfp_eye_damp_step(&d, a, 5016+251, 250, 180, out) == 0 && same(out, a)); /* a pause */
    CHECK(tfp_eye_damp_step(&d, b, 5300, 250, 180, out) > .04f);
    CHECK(tfp_eye_damp_step(&d, a, 5200, 250, 180, out) == 0 && same(out, a)); /* the clock went back */
    CHECK(tfp_eye_damp_step(&d, b, 5216, 250, 180, out) > .04f);
    tfp_eye_damp_reset(&d);
    CHECK(tfp_eye_damp_step(&d, a, 5232, 250, 180, out) == 0 && same(out, a));
    CHECK(tfp_eye_damp_step(&d, b, 5248, 250, 180, out) > .04f);
    float bad[3] = {0, NAN, 1.5f};
    CHECK(tfp_eye_damp_step(&d, bad, 5264, 250, 180, out) == 0 && out[0] == 0 && out[1] != out[1] && !d.known);
    CHECK(tfp_eye_damp_step(&d, a, 5280, 250, 180, out) == 0 && same(out, a));
    CHECK(tfp_eye_damp_step(&d, b, 5296, 250, 180, out) > .04f);
    CHECK(tfp_eye_damp_step(&d, b, (double)NAN, 250, 180, out) == 0 && same(out, b) && !d.known);
    CHECK(tfp_eye_damp_step(&d, a, 5312, 250, 180, out) == 0 && same(out, a));
    CHECK(tfp_eye_damp_step(&d, b, 5328, 250, 180, out) > .04f);
    CHECK(tfp_eye_damp_step(&d, b, 5344, 250, 0, out) == 0 && same(out, b) && !d.known); /* the option at 0 */
    CHECK(tfp_eye_damp_step(&d, a, 5360, 250, 180, out) == 0 && same(out, a));
    /* The same instant again: the same answer. */
    float first[3], again[3];
    tfp_eye_damp_step(&d, b, 5376, 250, 180, first); tfp_eye_damp_step(&d, b, 5376, 250, 180, again);
    CHECK(same(first, again) && !same(first, b));
    CHECK(tfp_eye_damp_step(&d, b, 5392, 250, (float)NAN, out) == 0 && same(out, b) && !d.known); /* a time constant that is not a number: none */
  }
  /* Letting go (the body leaves 'on foot', full aim begins): the lag that is carried is eased out on 40 ms, whatever
   * the head does meanwhile, and the state is forgotten once it is gone. */
  {
    static const double rates[] = {30, 60, 144};
    for (int r = 0; r < 3; ++r) {
      TfpEyeDamp d; tfp_eye_damp_reset(&d);
      float a[3] = {0, .2f, 1.5f}, b[3] = {.3f, -.2f, 1.5f}, out[3], behind[3] = {9, 9, 9};
      CHECK(tfp_eye_damp_release(&d, 4990, 250, behind) == 0 && behind[0] == 0 && behind[1] == 0 && behind[2] == 0); /* nothing carried */
      tfp_eye_damp_step(&d, a, 5000, 250, 300, out);
      CHECK(tfp_eye_damp_release(&d, 5001, 250, behind) == 0 && !d.known); /* a still head: nothing carried either */
      tfp_eye_damp_step(&d, a, 5000, 250, 300, out);
      CHECK(fabsf(tfp_eye_damp_step(&d, b, 5010, 250, 300, out)-TFP_EYE_DAMP_LAG_M) < 1e-5f); /* 0.10 m behind */
      const float first[3] = {out[0]-b[0], out[1]-b[1], out[2]-b[2]};
      float last = TFP_EYE_DAMP_LAG_M, at150 = 9; bool gone = false;
      CHECK(tfp_eye_damp_release(&d, 5010, 250, behind) == last && same(behind, first)); /* the same instant: not dropped */
      for (int i = 1; i*1000.0/rates[r] <= 400; ++i) {
        const double ms = i*1000.0/rates[r];
        const float lag = tfp_eye_damp_release(&d, 5010+ms, 250, behind);
        CHECK(lag <= last && fabsf(lag-sqrtf(behind[0]*behind[0]+behind[1]*behind[1]+behind[2]*behind[2])) < 1e-6f);
        if (!gone && lag > 0) CHECK(fabsf(lag-TFP_EYE_DAMP_LAG_M*expf(-(float)ms/TFP_EYE_DAMP_RELEASE_MS)) < 1e-4f
            && fabsf(behind[0]*first[1]-behind[1]*first[0]) < 1e-6f); /* on the time constant, along the same line */
        if (lag == 0) { gone = true; CHECK(!d.known); }
        if (ms <= 150) at150 = lag;
        last = lag;
      }
      CHECK(gone && at150 < .01f);
    }
    /* Damping that begins again while the lag is let go starts from where the eye was shown. */
    TfpEyeDamp d; tfp_eye_damp_reset(&d);
    float a[3] = {0, .2f, 1.5f}, b[3] = {0, .28f, 1.5f}, c[3] = {.5f, .28f, 1.2f}, out[3], behind[3];
    tfp_eye_damp_step(&d, a, 5000, 250, 300, out); tfp_eye_damp_step(&d, b, 5010, 250, 300, out);
    const float lag = tfp_eye_damp_release(&d, 5020, 250, behind);
    CHECK(lag > .05f && lag < .08f);
    CHECK(fabsf(tfp_eye_damp_step(&d, c, 5020, 250, 300, out)-lag) < 1e-6f && fabsf(out[0]-(c[0]+behind[0])) < 1e-6f
        && fabsf(out[1]-(c[1]+behind[1])) < 1e-6f && fabsf(out[2]-(c[2]+behind[2])) < 1e-6f);
    /* A pause, or a clock that went back, while letting go: forgotten at once. */
    tfp_eye_damp_step(&d, a, 6000, 250, 300, out); tfp_eye_damp_step(&d, b, 6010, 250, 300, out);
    CHECK(tfp_eye_damp_release(&d, 6010+251, 250, behind) == 0 && behind[1] == 0 && !d.known);
    tfp_eye_damp_step(&d, a, 7000, 250, 300, out); tfp_eye_damp_step(&d, b, 7010, 250, 300, out);
    CHECK(tfp_eye_damp_release(&d, 7000, 250, behind) == 0 && behind[1] == 0 && !d.known);
    tfp_eye_damp_step(&d, a, 8000, 250, 300, out); tfp_eye_damp_step(&d, b, 8010, 250, 300, out);
    CHECK(tfp_eye_damp_release(&d, (double)NAN, 250, behind) == 0 && behind[1] == 0 && !d.known);
  }
  printf("camera damp: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
