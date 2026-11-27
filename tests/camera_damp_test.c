/* Head-camera eye damping (src/camera_damp.h): the eye follows the head through a low-pass, with a limited lag. */
#include "camera_damp.h"
#include "camera_continuity.h"
#include <stdio.h>
#include <string.h>
#include "reference/soak-60-standup.h"
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
/* Round 49, the damping's frame. A root's axes (rows: right, forward, up) at heading `degrees`, level. */
static void level_axes(float degrees, float *axes) {
  const float c = cosf(degrees*TFP_PI/180), s = sinf(degrees*TFP_PI/180);
  const float made[9] = {s, -c, 0, c, s, 0, 0, 0, 1}; memcpy(axes, made, sizeof made);
}
static void level_view(float degrees, float pitch, float *view) {
  const float c = cosf(pitch*TFP_PI/180);
  view[0] = cosf(degrees*TFP_PI/180)*c; view[1] = sinf(degrees*TFP_PI/180)*c; view[2] = sinf(pitch*TFP_PI/180);
}
/* The heading of a frame's forward row, in degrees. */
static float heading(const float *frame) { return atan2f(frame[4], frame[3])*180/TFP_PI; }
static float turned(float from, float to) { float d = to-from; while (d > 180) d -= 360; while (d < -180) d += 360; return d; }
static bool level_frame(const float *f) { /* level, of unit axes, right-handed as the root's are */
  return f[2] == 0 && f[5] == 0 && f[6] == 0 && f[7] == 0 && f[8] == 1 && fabsf(f[3]*f[3]+f[4]*f[4]-1) < 1e-5f && fabsf(f[0]-f[4]) < 1e-6f && fabsf(f[1]+f[3]) < 1e-6f;
}
/* The logged stand-up (tests/reference/soak-60-standup.h) through the eye damping and the continuity as the helper
 * runs them (lock_damp_eye, then smooth_present): `level` false the damping in the root's own axes (as built before
 * round 49), true in the frame of tfp_damp_frame_step. `change` takes, for every row from the third, how much the
 * step of the eye shown changed, as a vector (the judge's measure, tools/harness/matrix.py; the root stands still). */
static void standup(bool level, float *change) {
  static const TfpContinuityTuning tuning = TFP_CONTINUITY_DEFAULT;
  const TfpCameraStyle stable = tfp_camera_style(false, 0);
  const int count = (int)(sizeof standup_rows/sizeof standup_rows[0]);
  TfpContinuity c; memset(&c, 0, sizeof c); TfpEyeDamp d; tfp_eye_damp_reset(&d); TfpDampFrame frame; tfp_damp_frame_reset(&frame);
  float shown[3][3] = {{0}};
  for (int i = 0; i < count; ++i) {
    const StandupFrame *f = &standup_rows[i]; const double tick = 1000.0+(double)f->ms;
    const float flat = sqrtf(f->view[0]*f->view[0]+f->view[1]*f->view[1]);
    const float node[3] = {f->wanted[0]-f->damp[0]-f->view[0]/flat*.18f, f->wanted[1]-f->damp[1]-f->view[1]/flat*.18f, f->wanted[2]-f->damp[2]-.04f};
    float axes[9], relative[3] = {0}, damped[3], eye[3], wanted[3] = {0}, head[3] = {0}, out[3];
    memcpy(axes, f->axes, sizeof axes);
    if (level) tfp_damp_frame_step(&frame, f->axes, f->view, tick, 250, axes);
    for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (node[k]-f->root[k])*axes[a*3+k];
    const float lag = tfp_eye_damp_follow(&d, relative, tick, 250, stable.eye_damp_ms, stable.eye_lag_m, damped);
    for (int k = 0; k < 3; ++k) { eye[k] = f->wanted[k]-f->damp[k]; if (lag > 0) for (int a = 0; a < 3; ++a) eye[k] += (damped[a]-relative[a])*axes[a*3+k]; }
    for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) { wanted[a] += (eye[k]-f->root[k])*f->axes[a*3+k]; head[a] += (node[k]-f->root[k])*f->axes[a*3+k]; }
    const float carried = tfp_continuity_step_cut(&c, wanted, eye, f->axes, head, (uint32_t)f->source, f->looked != 0, false, tick, 250, &tuning, out);
    memcpy(shown[0], shown[1], 12); memcpy(shown[1], shown[2], 12); memcpy(shown[2], eye, 12);
    if (carried > 1e-6f) for (int k = 0; k < 3; ++k) { shown[2][k] = f->root[k]; for (int a = 0; a < 3; ++a) shown[2][k] += out[a]*f->axes[a*3+k]; }
    change[i] = 0;
    if (i >= 2) { float square = 0; for (int k = 0; k < 3; ++k) { const float v = shown[2][k]-2*shown[1][k]+shown[0][k]; square += v*v; } change[i] = sqrtf(square); }
  }
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
  /* The camera style (the Bodycam option). Bodycam is the camera damping option as it is, with the 0.10 m limit;
   * Stable follows the head on 350 ms with up to 0.25 m of lag, whatever the option says. */
  {
    const TfpCameraStyle bodycam = tfp_camera_style(true, 180), stable = tfp_camera_style(false, 180), stable_raw = tfp_camera_style(false, 0);
    CHECK(bodycam.eye_damp_ms == 180 && bodycam.eye_lag_m == TFP_EYE_DAMP_LAG_M && bodycam.kick_damp_ms == 180 && bodycam.foot_kick == 1 && bodycam.thrown_kick == 1);
    CHECK(tfp_camera_style(true, 0).eye_damp_ms == 0 && tfp_camera_style(true, 0).kick_damp_ms == 0);
    CHECK(stable.eye_damp_ms == 350 && stable.eye_lag_m == .25f && stable.kick_damp_ms == 250 && stable.foot_kick == 0 && stable.thrown_kick == .5f);
    CHECK(stable_raw.eye_damp_ms == 350 && stable_raw.eye_lag_m == .25f && stable_raw.kick_damp_ms == 250);
    /* The limit given is the limit used: tfp_eye_damp_step is tfp_eye_damp_follow at 0.10 m, bit for bit. */
    TfpEyeDamp a, b; tfp_eye_damp_reset(&a); tfp_eye_damp_reset(&b);
    for (int i = 0; i < 200; ++i) {
      float eye[3], one[3], two[3]; script(i < 100 ? 1 : 2, i < 100 ? .5f : .05f, i*7.0, eye);
      const float first = tfp_eye_damp_step(&a, eye, 5000+i*7.0, 250, 180, one), second = tfp_eye_damp_follow(&b, eye, 5000+i*7.0, 250, 180, TFP_EYE_DAMP_LAG_M, two);
      CHECK(first == second && same(one, two));
    }
    static const double rates[] = {30, 60, 144};
    for (int r = 0; r < 3; ++r) {
      /* A head that jerks 0.05 m there and back within 100 ms: the eye moves a fifth of that at most (0.008 m; the
       * same jerk moves it 0.014 m at Bodycam's default 180 ms and 0.05 m undamped), and is back within a second. */
      TfpEyeDamp d; tfp_eye_damp_reset(&d);
      float start[3], eye[3], out[3] = {0}, peak = 0, lag = 0; script(2, .05f, 0, start);
      for (int i = 0; i*1000.0/rates[r] <= 1400; ++i) {
        const double ms = i*1000.0/rates[r];
        script(2, .05f, ms, eye);
        const float behind = tfp_eye_damp_follow(&d, eye, 5000+ms, 250, stable.eye_damp_ms, stable.eye_lag_m, out);
        CHECK(fabsf(behind-apart(out, eye)) < 1e-5f);
        if (behind > lag) lag = behind;
        if (apart(out, start) > peak) peak = apart(out, start);
      }
      const float body = play(2, .05f, 180, rates[r], 1400, NULL, NULL);
      printf("Stable eye, %g frames a second: a 0.05 m jerk of the head moves it %.4f m (Bodycam at 60 %%: %.4f m), %.4f m left after 1.2 s\n",
             rates[r], peak, body, apart(out, start));
      CHECK(peak > .001f && peak < .010f && peak < body*.6f && lag <= .05f+1e-5f && apart(out, start) < .0005f);
      /* A head that drops 0.5 m and stays (a crouch, going prone): followed to within 0.05 m inside 1.2 s, never
       * more than 0.25 m behind, and never past it. */
      tfp_eye_damp_reset(&d);
      float truth[3], worst = 0, within = -1, at1200 = 9, before_left = 9; script(1, .5f, 5000, truth);
      for (int i = 0; i*1000.0/rates[r] <= 2500; ++i) {
        const double ms = i*1000.0/rates[r];
        script(1, .5f, ms, eye);
        const float behind = tfp_eye_damp_follow(&d, eye, 5000+ms, 250, stable.eye_damp_ms, stable.eye_lag_m, out);
        if (behind > worst) worst = behind;
        if (ms >= 100) {
          const float left = apart(out, truth);
          CHECK(left <= before_left+1e-6f); /* only ever nearer */
          before_left = left;
          if (within < 0 && left < .05f) within = (float)ms-100;
          if (ms-100 <= 1200) at1200 = left;
        }
      }
      printf("Stable eye, %g frames a second: a 0.5 m drop of the head is at most %.3f m ahead of the eye, within 0.05 m after %.0f ms, %.4f m left at 1.2 s\n",
             rates[r], worst, within, at1200);
      CHECK(fabsf(worst-.25f) < 1e-5f && within > 0 && within < 1200 && at1200 < .05f && apart(out, truth) < .001f);
    }
  }
  /* A change of camera style while a lag is carried never moves the eye by itself. The limit in force goes down to
   * the new style's at TFP_EYE_DAMP_LIMIT_M_S, and the time constant is not shortened before it is there; a limit
   * that goes up, and a longer time constant, hold nothing back and are in force at once. */
  {
    const TfpCameraStyle bodycam = tfp_camera_style(true, 180), stable = tfp_camera_style(false, 180);
    static const double rates[] = {30, 60, 144};
    for (int r = 0; r < 3; ++r) {
      const double dt = 1000.0/rates[r];
      /* Stable, and the head drops 0.5 m in one frame: 0.25 m carried. Then Bodycam, [0] with the head staying
       * there, [1] with the head going on down at 1 m a second, so the eye is pulled along at the limit. `stay` is
       * the same camera left in Stable. Counted: the most the eye moves in one frame against the true eye (with a
       * still head, the most it moves at all), and the same for `stay`. */
      for (int going = 0; going < 2; ++going) {
        TfpEyeDamp d, stay; tfp_eye_damp_reset(&d);
        float eye[3] = {.03f, .24f, 1.54f}, out[3], kept[3];
        tfp_eye_damp_follow(&d, eye, 5000, 250, stable.eye_damp_ms, stable.eye_lag_m, out);
        eye[2] -= .5f;
        float lag = tfp_eye_damp_follow(&d, eye, 5000+dt, 250, stable.eye_damp_ms, stable.eye_lag_m, out);
        CHECK(fabsf(lag-.25f) < 1e-5f);
        stay = d;
        float most = 0, own = 0, first = 0, at300 = 9, behind[3], stay_behind[3];
        for (int i = 0; i < 3; ++i) behind[i] = stay_behind[i] = out[i]-eye[i];
        for (int i = 1; i*1000.0/rates[r] <= 1000; ++i) {
          const double ms = i*1000.0/rates[r];
          if (going) eye[2] -= (float)(dt/1000);
          lag = tfp_eye_damp_follow(&d, eye, 5000+dt+ms, 250, bodycam.eye_damp_ms, bodycam.eye_lag_m, out);
          tfp_eye_damp_follow(&stay, eye, 5000+dt+ms, 250, stable.eye_damp_ms, stable.eye_lag_m, kept);
          CHECK(fabsf(lag-apart(out, eye)) < 1e-5f);
          const float now[3] = {out[0]-eye[0], out[1]-eye[1], out[2]-eye[2]}, stay_now[3] = {kept[0]-eye[0], kept[1]-eye[1], kept[2]-eye[2]};
          const float moved = apart(now, behind), stay_moved = apart(stay_now, stay_behind);
          if (i == 1) first = moved;
          if (moved > most) most = moved;
          if (stay_moved > own) own = stay_moved;
          if (ms >= 300 && at300 == 9) at300 = lag;
          memcpy(behind, now, 12); memcpy(stay_behind, stay_now, 12);
        }
        printf("Stable to Bodycam with 0.25 m carried, %g frames a second, the head %s: %.4f m in the first frame, at most %.4f m in one frame "
               "(Stable itself: %.4f m), %.4f m behind after 300 ms\n", rates[r], going ? "going on down" : "still", first, most, own, at300);
        /* Stable's own follow from 0.25 m is 0.0227 m in a frame at 30 frames a second: the change adds nothing to it. */
        CHECK(most <= (own > .02f ? own : .02f)+1e-6f);
        CHECK(at300 <= bodycam.eye_lag_m+1e-5f && lag <= bodycam.eye_lag_m+1e-5f);
        if (going) CHECK(fabsf(lag-bodycam.eye_lag_m) < 1e-5f); else CHECK(lag < .01f);
      }
      /* The other way: Bodycam with 0.10 m carried, then Stable. The eye moves no more in any frame than Bodycam
       * itself would have moved it, and Stable's 0.25 m is the limit at once: a further drop is taken up to it. */
      {
        TfpEyeDamp d, stay; tfp_eye_damp_reset(&d);
        float eye[3] = {.03f, .24f, 1.54f}, out[3], kept[3], before[3], stay_before[3];
        tfp_eye_damp_follow(&d, eye, 5000, 250, bodycam.eye_damp_ms, bodycam.eye_lag_m, out);
        eye[2] -= .5f;
        float lag = tfp_eye_damp_follow(&d, eye, 5000+dt, 250, bodycam.eye_damp_ms, bodycam.eye_lag_m, out);
        CHECK(fabsf(lag-.10f) < 1e-5f);
        stay = d; memcpy(before, out, 12); memcpy(stay_before, out, 12);
        bool more = false;
        for (int i = 1; i <= 6; ++i) {
          lag = tfp_eye_damp_follow(&d, eye, 5000+dt+i*dt, 250, stable.eye_damp_ms, stable.eye_lag_m, out);
          tfp_eye_damp_follow(&stay, eye, 5000+dt+i*dt, 250, bodycam.eye_damp_ms, bodycam.eye_lag_m, kept);
          if (apart(out, before) > apart(kept, stay_before)+1e-6f) more = true;
          memcpy(before, out, 12); memcpy(stay_before, kept, 12);
        }
        CHECK(!more && lag > .03f);
        eye[2] -= .5f;
        CHECK(fabsf(tfp_eye_damp_follow(&d, eye, 5000+dt+7*dt, 250, stable.eye_damp_ms, stable.eye_lag_m, out)-.25f) < 1e-5f);
      }
      /* Bodycam with camera damping 0 while Stable carries 0.25 m: no damping is left, and the lag is let go, by
       * what the 40 ms of tfp_eye_damp_release takes or by 0.55 m a second, whichever is less in that frame: never
       * dropped in one frame, gone within 600 ms, and `release` itself is not changed by a bit. */
      {
        TfpEyeDamp d, plain; tfp_eye_damp_reset(&d);
        float eye[3] = {.03f, .24f, 1.54f}, out[3], behind[3], other[3];
        tfp_eye_damp_follow(&d, eye, 5000, 250, stable.eye_damp_ms, stable.eye_lag_m, out);
        eye[2] -= .5f;
        float lag = tfp_eye_damp_follow(&d, eye, 5000+dt, 250, stable.eye_damp_ms, stable.eye_lag_m, out), most = 0, first = 0; double gone = -1;
        plain = d;
        const float fast = tfp_eye_damp_release(&plain, 5000+2*dt, 250, other);
        CHECK(fabsf(fast-.25f*expf(-(float)dt/TFP_EYE_DAMP_RELEASE_MS)) < 1e-6f); /* as it was */
        for (int i = 1; i*1000.0/rates[r] <= 1000 && gone < 0; ++i) {
          const float now = tfp_eye_damp_release_slowly(&d, 5000+dt+i*1000.0/rates[r], 250, behind);
          CHECK(now <= lag && now >= fast*(i == 1)-1e-6f && fabsf(behind[0]) < 1e-6f && fabsf(behind[1]) < 1e-6f && behind[2] >= 0);
          if (i == 1) first = lag-now;
          if (lag-now > most && now > 0) most = lag-now;
          if (!(now > 0)) gone = i*1000.0/rates[r];
          lag = now;
        }
        printf("Stable to Bodycam at damping 0 with 0.25 m carried, %g frames a second: let go by %.4f m in the first frame (40 ms alone: %.4f m), "
               "at most %.4f m in one frame, gone after %.0f ms\n", rates[r], first, .25f-fast, most, gone);
        CHECK(most <= .02f && most <= TFP_EYE_DAMP_LIMIT_M_S*(float)dt/1000+1e-6f && gone > 300 && gone <= 600 && !d.known);
        /* A lag small enough for the 40 ms to be the slower of the two: the same bits as `release`. */
        TfpEyeDamp a, b; tfp_eye_damp_reset(&a);
        float small[3] = {.03f, .24f, 1.54f};
        tfp_eye_damp_follow(&a, small, 5000, 250, 180, .10f, out);
        small[2] -= .01f;
        tfp_eye_damp_follow(&a, small, 5000+dt, 250, 180, .10f, out);
        b = a;
        CHECK(tfp_eye_damp_release_slowly(&a, 5000+2*dt, 250, behind) == tfp_eye_damp_release(&b, 5000+2*dt, 250, other) && same(behind, other) && behind[2] > 0);
      }
    }
  }
  /* Round 49, the frame the eye is damped in (tfp_damp_frame_step): level, and it turns with the root's heading only as
   * far as the view's heading turns the same way in that frame. */
  {
    TfpDampFrame f; tfp_damp_frame_reset(&f);
    float axes[9], view[3], frame[9];
    /* The first frame is the root's own, to the bit; a level root that turns with the view stays the frame, to the bit
     * (what the damping worked in before round 49), whether the view turns as far or further. */
    for (int i = 0; i < 60; ++i) {
      level_axes(20+3.0f*i, axes); level_view(-70+(i < 30 ? 3.0f : 5.0f)*i-(i < 30 ? 0 : 60), -10, view);
      CHECK(tfp_damp_frame_step(&f, axes, view, 5000+i*16.0, 250, frame) && !memcmp(frame, axes, sizeof axes));
    }
    /* The other way round too, and at a turn of nothing. */
    tfp_damp_frame_reset(&f);
    for (int i = 0; i < 20; ++i) {
      level_axes(200-4.0f*(i < 10 ? i : 10), axes); level_view(100-6.0f*(i < 10 ? i : 10), 25, view);
      CHECK(tfp_damp_frame_step(&f, axes, view, 5960+i*16.0, 250, frame) && !memcmp(frame, axes, sizeof axes));
    }
  }
  { /* The root spins under a still view (the get-up of soak 60, round 30's whip): the frame stands still. */
    TfpDampFrame f; tfp_damp_frame_reset(&f);
    float axes[9], view[3], first[9], frame[9]; level_view(164, 14, view);
    level_axes(-110.7f, axes);
    CHECK(tfp_damp_frame_step(&f, axes, view, 5000, 250, first) && !memcmp(first, axes, sizeof axes));
    for (int i = 1; i <= 8; ++i) {
      level_axes(-110.7f+16.5f*i, axes);
      CHECK(!tfp_damp_frame_step(&f, axes, view, 5000+i*33.0, 250, frame) && level_frame(frame) && fabsf(turned(heading(first), heading(frame))) < 1e-3f);
    }
    /* From there the root turns with the view again: the frame turns as far as they do, from where it stood. */
    float before = heading(frame);
    for (int i = 1; i <= 6; ++i) {
      level_axes(-110.7f+16.5f*8+5.0f*i, axes); level_view(164+5.0f*i, 14, view);
      CHECK(!tfp_damp_frame_step(&f, axes, view, 5264+i*33.0, 250, frame) && level_frame(frame) && fabsf(turned(before, heading(frame))-5) < 1e-3f);
      before = heading(frame);
    }
  }
  { /* Only the view turns (a flick of the mouse over a still body): the frame is the root's, still. */
    TfpDampFrame f; tfp_damp_frame_reset(&f);
    float axes[9], view[3], frame[9]; level_axes(40, axes);
    for (int i = 0; i < 12; ++i) {
      level_view(40+25.0f*i, 0, view);
      CHECK(tfp_damp_frame_step(&f, axes, view, 5000+i*16.0, 250, frame) && !memcmp(frame, axes, sizeof axes));
    }
  }
  { /* Opposite senses: not at all. The same sense, the view less far: as far as the view. */
    TfpDampFrame f; tfp_damp_frame_reset(&f);
    float axes[9], view[3], first[9], frame[9];
    level_axes(10, axes); level_view(10, 0, view); tfp_damp_frame_step(&f, axes, view, 5000, 250, first);
    for (int i = 1; i <= 5; ++i) {
      level_axes(10+8.0f*i, axes); level_view(10-6.0f*i, 0, view);
      CHECK(!tfp_damp_frame_step(&f, axes, view, 5000+i*16.0, 250, frame) && level_frame(frame) && fabsf(turned(heading(first), heading(frame))) < 1e-3f);
    }
    for (int i = 1; i <= 5; ++i) {
      level_axes(50-9.0f*i, axes); level_view(-20-2.0f*i, 0, view);
      CHECK(!tfp_damp_frame_step(&f, axes, view, 5080+i*16.0, 250, frame) && level_frame(frame) && fabsf(turned(heading(first), heading(frame))+2.0f*i) < 1e-3f);
    }
  }
  { /* Across +-180 degrees a turn is the short way round, for the root and for the view. */
    TfpDampFrame f; tfp_damp_frame_reset(&f);
    float axes[9], view[3], frame[9];
    level_axes(175, axes); level_view(-178, 0, view); tfp_damp_frame_step(&f, axes, view, 5000, 250, frame);
    level_axes(-175, axes); level_view(172, 0, view); /* the root +10, the view -10: opposite */
    CHECK(!tfp_damp_frame_step(&f, axes, view, 5016, 250, frame) && fabsf(turned(175, heading(frame))) < 1e-3f);
    level_axes(-179, axes); level_view(-170, 0, view); /* the root -4 (the short way), the view +18 across the seam: opposite */
    CHECK(!tfp_damp_frame_step(&f, axes, view, 5032, 250, frame) && fabsf(turned(175, heading(frame))) < 1e-3f);
    level_axes(171, axes); level_view(175, 0, view);   /* the root -10 across the seam, the view -15 across it: the root's 10 */
    CHECK(!tfp_damp_frame_step(&f, axes, view, 5048, 250, frame) && fabsf(turned(175, heading(frame))+10) < 1e-3f);
  }
  { /* It starts again at the root's heading after a pause, a clock that went back, and what is not a number. */
    TfpDampFrame f; tfp_damp_frame_reset(&f);
    float axes[9], view[3], frame[9]; level_view(0, 0, view);
    for (int i = 0; i <= 4; ++i) { level_axes(30.0f*i, axes); tfp_damp_frame_step(&f, axes, view, 5000+i*16.0, 250, frame); }
    CHECK(fabsf(turned(0, heading(frame))) < 1e-3f);
    level_axes(150, axes);
    CHECK(tfp_damp_frame_step(&f, axes, view, 5064+251, 250, frame) && !memcmp(frame, axes, sizeof axes));
    level_axes(180, axes);
    CHECK(!tfp_damp_frame_step(&f, axes, view, 5064+251+16, 250, frame) && fabsf(turned(150, heading(frame))) < 1e-3f);
    CHECK(tfp_damp_frame_step(&f, axes, view, 5064, 250, frame) && !memcmp(frame, axes, sizeof axes));
    float broken[9]; memcpy(broken, axes, sizeof axes); broken[3] = NAN;
    CHECK(tfp_damp_frame_step(&f, broken, view, 5080, 250, frame) && !f.known);
    level_axes(-90, axes);
    CHECK(tfp_damp_frame_step(&f, axes, view, 5096, 250, frame) && !memcmp(frame, axes, sizeof axes));
    tfp_damp_frame_reset(&f);
    CHECK(!f.known);
  }
  { /* Without a view to compare with (none given, straight up or down, not a number) the frame turns with the root. */
    TfpDampFrame f; tfp_damp_frame_reset(&f);
    float axes[9], frame[9]; const float up[3] = {0, 0, 1}, nan[3] = {NAN, 0, 0};
    for (int i = 0; i < 12; ++i) {
      level_axes(12.0f*i, axes);
      CHECK(tfp_damp_frame_step(&f, axes, i < 4 ? NULL : i < 8 ? up : nan, 5000+i*16.0, 250, frame) && !memcmp(frame, axes, sizeof axes));
    }
  }
  { /* A root that is not level (lying, on a slope): the frame is level, at the heading of the root's forward axis. */
    TfpDampFrame f; tfp_damp_frame_reset(&f);
    const float tilted[9] = {-0.269517f, -0.924934f, 0.268060f, 0.901390f, -0.340268f, -0.267794f, 0.338904f, 0.169452f, 0.925435f};
    float view[3], frame[9]; level_view(164, 14, view);
    CHECK(!tfp_damp_frame_step(&f, tilted, view, 5000, 250, frame) && level_frame(frame) && fabsf(turned(heading(tilted), heading(frame))) < 1e-3f);
    /* The tilt going in one frame, the heading with it, the view still: the frame stays. */
    float axes[9]; level_axes(heading(tilted)+21, axes);
    CHECK(!tfp_damp_frame_step(&f, axes, view, 5033, 250, frame) && level_frame(frame) && fabsf(turned(heading(tilted), heading(frame))) < 1e-3f);
  }
  { /* A turn of the root of more than a quarter circle in one frame is no turn: the game has set the root's axes anew
     * (out of an emote into a dive backwards the heading flips 157 to 180 degrees in a frame, soaks 28 and 29), which
     * the continuity reads as a cut of the pose in the root's own axes (THE CUT). The frame goes with it, as before. */
    TfpDampFrame f; tfp_damp_frame_reset(&f);
    float axes[9], view[3], frame[9]; level_view(5, 0, view);
    level_axes(-17.6f, axes); tfp_damp_frame_step(&f, axes, view, 5000, 250, frame);
    level_axes(-174.5f, axes);
    CHECK(tfp_damp_frame_step(&f, axes, view, 5033, 250, frame) && !memcmp(frame, axes, sizeof axes));
    level_axes(-174.5f+89, axes); /* under it: a turn, and the view did not make it */
    CHECK(!tfp_damp_frame_step(&f, axes, view, 5066, 250, frame) && fabsf(turned(-174.5f, heading(frame))) < 1e-3f);
    level_axes(-174.5f+89-91, axes); /* over it, from a frame that already stands off the root: it keeps standing off as far */
    CHECK(!tfp_damp_frame_step(&f, axes, view, 5099, 250, frame) && fabsf(turned(-174.5f-91, heading(frame))) < 1e-3f);
  }
  { /* A forward axis straight up (a body thrown, while a lag is let go): the frame of the frame before stands. */
    TfpDampFrame f; tfp_damp_frame_reset(&f);
    const float upright[9] = {1, 0, 0, 0, 0, 1, 0, -1, 0};
    float axes[9], view[3], frame[9], kept[9]; level_view(30, 0, view);
    CHECK(tfp_damp_frame_step(&f, upright, view, 5000, 250, frame) && !memcmp(frame, upright, sizeof upright) && !f.known); /* nothing before it: the axes */
    level_axes(30, axes); tfp_damp_frame_step(&f, axes, view, 5016, 250, frame);
    level_axes(50, axes); CHECK(!tfp_damp_frame_step(&f, axes, view, 5032, 250, kept) && fabsf(turned(30, heading(kept))) < 1e-3f);
    CHECK(!tfp_damp_frame_step(&f, upright, view, 5048, 250, frame) && level_frame(frame) && fabsf(turned(30, heading(frame))) < 1e-3f);
    level_axes(50, axes); CHECK(!tfp_damp_frame_step(&f, axes, view, 5064, 250, frame) && fabsf(turned(30, heading(frame))) < 1e-3f);
  }
  /* The get-up of soak 60 (tests/reference/soak-60-standup.h). In the root's axes, as built before round 49, the eye's
   * step changes by 0.31 m in a frame; in the frame of tfp_damp_frame_step the first frame is 0.06 m at most and no
   * frame over 0.10 m. */
  {
    enum { ROWS = sizeof standup_rows/sizeof standup_rows[0] };
    float old[ROWS], now[ROWS], old_most = 0, now_most = 0; int first = -1;
    standup(false, old); standup(true, now);
    for (int i = 0; i < ROWS; ++i) {
      if (first < 0 && old[i] >= .06f) first = i;
      if (old[i] > old_most) old_most = old[i];
      if (now[i] > now_most) now_most = now[i];
    }
    printf("stand-up from lying on the back (soak 60, Stable), the change of the shown eye's step, row by row from row %d:\n  root's axes:", first);
    for (int i = first; i >= 0 && i < ROWS; ++i) printf(" %.3f", old[i]);
    printf("\n  round 49:   ");
    for (int i = first; i >= 0 && i < ROWS; ++i) printf(" %.3f", now[i]);
    printf("\n");
    CHECK(first == 6 && fabsf(old[6]-.291f) < .01f && fabsf(old_most-.312f) < .01f && fabsf(old[8]-old_most) < 1e-6f);
    CHECK(first >= 0 && now[first] <= .06f);
    CHECK(now_most <= .10f);
  }
  printf("camera damp: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
