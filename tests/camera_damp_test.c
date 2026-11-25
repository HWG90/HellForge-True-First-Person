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

/* ---- Round 30, the whip (tfp_eye_whip_step). ---- */
typedef struct WhipRow { float ms, root[3], axes[9], head[3], view[3]; } WhipRow;
/* soak 36 (feel.53, ~/.cache/tfp-soak-36.modlog), 07:03:00.839 to 07:03:01.473: ms, body root, its axes (rows), the head (the
 * wanted eye less c.forward 0.18 m along the level view and c.up 0.04 m: the damped head), the view */
static const WhipRow WHIP_36[] = {
  {0.0f, {-1.1845f, 21.1426f, 5.6823f}, {0.973495f, 0.222895f, -0.051235f, -0.223239f, 0.974763f, -0.001018f, 0.049715f, 0.012429f, 0.998686f},
   {-1.3241f, 21.7699f, 6.1275f}, {-0.22178f, 0.95751f, -0.18435f}},
  {33.3f, {-1.1845f, 21.1422f, 5.6818f}, {0.973099f, 0.222804f, -0.058619f, -0.223254f, 0.974760f, -0.001164f, 0.056880f, 0.014220f, 0.998280f},
   {-1.3210f, 21.7763f, 6.1194f}, {-0.22150f, 0.95785f, -0.18293f}},
  {66.6f, {-1.1846f, 21.1420f, 5.6816f}, {0.972697f, 0.222712f, -0.065262f, -0.223270f, 0.974756f, -0.001296f, 0.063326f, 0.015831f, 0.997867f},
   {-1.3183f, 21.7823f, 6.1119f}, {-0.22125f, 0.95806f, -0.18213f}},
  {100.2f, {-1.1846f, 21.1419f, 5.6814f}, {0.972299f, 0.222621f, -0.071238f, -0.223286f, 0.974752f, -0.001414f, 0.069125f, 0.017281f, 0.997458f},
   {-1.3159f, 21.7877f, 6.1050f}, {-0.22104f, 0.95817f, -0.18180f}},
  {133.6f, {-1.2000f, 21.1602f, 5.6531f}, {0.948422f, 0.291469f, -0.124669f, -0.292963f, 0.956101f, 0.006585f, 0.121115f, 0.030278f, 0.992177f},
   {-1.3554f, 21.8027f, 6.0746f}, {-0.22084f, 0.95820f, -0.18190f}},
  {168.0f, {-1.2377f, 21.1800f, 5.6651f}, {0.930736f, 0.343986f, -0.124113f, -0.345053f, 0.938486f, 0.013481f, 0.121115f, 0.030278f, 0.992177f},
   {-1.4283f, 21.8140f, 6.0860f}, {-0.22148f, 0.95801f, -0.18211f}},
  {200.5f, {-1.2613f, 21.1469f, 5.6619f}, {0.580139f, 0.808899f, -0.095503f, -0.805463f, 0.587167f, 0.080404f, 0.121115f, 0.030278f, 0.992177f},
   {-1.7572f, 21.5437f, 6.1232f}, {-0.22202f, 0.95773f, -0.18294f}},
  {234.1f, {-1.2692f, 21.1018f, 5.6611f}, {-0.131907f, 0.991161f, -0.014145f, -0.983835f, -0.129162f, 0.124039f, 0.121115f, 0.030278f, 0.992177f},
   {-1.8794f, 21.0175f, 6.1487f}, {-0.22804f, 0.95693f, -0.17967f}},
  {266.5f, {-1.2896f, 21.0910f, 5.6398f}, {0.102586f, 0.993801f, -0.042851f, -0.987323f, 0.106973f, 0.117258f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9056f, 21.1648f, 6.1231f}, {-0.23419f, 0.95748f, -0.16851f}},
  {300.0f, {-1.3232f, 21.1168f, 5.6139f}, {0.294177f, 0.953537f, -0.065010f, -0.948046f, 0.299750f, 0.106581f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9163f, 21.3185f, 6.0906f}, {-0.23488f, 0.95894f, -0.15895f}},
  {333.4f, {-1.3671f, 21.1406f, 5.6166f}, {0.441380f, 0.893643f, -0.081151f, -0.889109f, 0.447756f, 0.094870f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9241f, 21.4397f, 6.0866f}, {-0.23268f, 0.96057f, -0.15217f}},
  {367.0f, {-1.3934f, 21.1267f, 5.6195f}, {0.443815f, 0.892413f, -0.081411f, -0.887896f, 0.450203f, 0.094647f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9508f, 21.4244f, 6.0912f}, {-0.23047f, 0.96177f, -0.14791f}},
  {400.8f, {-1.4009f, 21.1098f, 5.6134f}, {0.446254f, 0.891172f, -0.081670f, -0.886672f, 0.452655f, 0.094423f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9582f, 21.4062f, 6.0873f}, {-0.22954f, 0.96250f, -0.14458f}},
  {434.7f, {-1.4059f, 21.1028f, 5.6076f}, {0.448675f, 0.889932f, -0.081928f, -0.885450f, 0.455087f, 0.094199f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9628f, 21.3984f, 6.0838f}, {-0.22886f, 0.96309f, -0.14167f}},
  {467.2f, {-1.4090f, 21.0985f, 5.6040f}, {0.451107f, 0.888678f, -0.082187f, -0.884214f, 0.457532f, 0.093974f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9652f, 21.3936f, 6.0824f}, {-0.22825f, 0.96342f, -0.14044f}},
  {500.7f, {-1.4109f, 21.0957f, 5.6018f}, {0.450705f, 0.888885f, -0.082144f, -0.884418f, 0.457128f, 0.094011f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9673f, 21.3888f, 6.0824f}, {-0.22767f, 0.96348f, -0.14098f}},
  {534.1f, {-1.4122f, 21.0940f, 5.6003f}, {0.450705f, 0.888885f, -0.082144f, -0.884418f, 0.457128f, 0.094011f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9683f, 21.3858f, 6.0830f}, {-0.22712f, 0.96331f, -0.14299f}},
  {568.4f, {-1.4130f, 21.0929f, 5.5994f}, {0.450705f, 0.888885f, -0.082144f, -0.884418f, 0.457128f, 0.094011f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9687f, 21.3839f, 6.0839f}, {-0.22661f, 0.96296f, -0.14610f}},
  {601.1f, {-1.4135f, 21.0922f, 5.5989f}, {0.450705f, 0.888885f, -0.082144f, -0.884418f, 0.457128f, 0.094011f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9687f, 21.3828f, 6.0849f}, {-0.22613f, 0.96245f, -0.15017f}},
  {634.6f, {-1.4138f, 21.0918f, 5.5985f}, {0.450705f, 0.888885f, -0.082144f, -0.884418f, 0.457128f, 0.094011f, 0.121115f, 0.030278f, 0.992177f},
   {-1.9685f, 21.3821f, 6.0858f}, {-0.22573f, 0.96193f, -0.15406f}},
};

#define WHIP_ROWS (int)(sizeof WHIP_36/sizeof WHIP_36[0])
/* The scene at `ms`: the logged rows played at any frame time, each value linear between rows (the axes made
 * orthonormal again, the head kept where it is against the root). `turn`: false, the root's heading held at the first
 * row's (the body's own path without the whip: what the eye would do if the root had not turned). */
typedef struct WhipScene { float root[3], axes[9], head[3], view[3]; } WhipScene;
static void whip_scene(double ms, bool turn, WhipScene *out) {
  int i = 0; while (i+2 < WHIP_ROWS && WHIP_36[i+1].ms <= ms) ++i;
  const WhipRow *a = &WHIP_36[i], *b = &WHIP_36[i+1];
  float t = (float)((ms-a->ms)/(b->ms-a->ms)); if (t < 0) t = 0; if (t > 1) t = 1;
  float against[2][3] = {{0}};
  for (int k = 0; k < 2; ++k) { const WhipRow *r = k ? b : a;
    for (int axis = 0; axis < 3; ++axis) for (int c = 0; c < 3; ++c) against[k][axis] += (r->head[c]-r->root[c])*r->axes[axis*3+c]; }
  for (int c = 0; c < 3; ++c) { out->root[c] = a->root[c]+(b->root[c]-a->root[c])*t; out->view[c] = a->view[c]+(b->view[c]-a->view[c])*t; }
  for (int c = 0; c < 9; ++c) out->axes[c] = turn ? a->axes[c]+(b->axes[c]-a->axes[c])*t : WHIP_36[0].axes[c];
  /* Gram-Schmidt: right, forward from it, up as their cross */
  float *x = out->axes, *y = out->axes+3, *z = out->axes+6, n = sqrtf(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]);
  for (int c = 0; c < 3; ++c) x[c] /= n;
  const float d = x[0]*y[0]+x[1]*y[1]+x[2]*y[2]; for (int c = 0; c < 3; ++c) y[c] -= d*x[c];
  n = sqrtf(y[0]*y[0]+y[1]*y[1]+y[2]*y[2]); for (int c = 0; c < 3; ++c) y[c] /= n;
  z[0] = x[1]*y[2]-x[2]*y[1]; z[1] = x[2]*y[0]-x[0]*y[2]; z[2] = x[0]*y[1]-x[1]*y[0];
  for (int c = 0; c < 3; ++c) { out->head[c] = out->root[c];
    for (int axis = 0; axis < 3; ++axis) out->head[c] += (against[0][axis]+(against[1][axis]-against[0][axis])*t)*out->axes[axis*3+c]; }
}
/* The head camera's eye on a head: c.forward 0.18 m along the level view, c.up 0.04 m. */
static void whip_eye(const WhipScene *s, const float *add, float *eye) {
  const float n = sqrtf(s->view[0]*s->view[0]+s->view[1]*s->view[1]);
  eye[0] = s->head[0]+s->view[0]/n*.18f+add[0]; eye[1] = s->head[1]+s->view[1]/n*.18f+add[1]; eye[2] = s->head[2]+.04f+add[2];
}
/* Plays the scene at frame time `dt` from 0 to 1300 ms (the rows, then the last one held). `whip`: through
 * tfp_eye_whip_step; `turn`: the root turns as logged. Returns the judge's measure (tools/harness/matrix.py measure:
 * |(c-b) - (b-a)| of the shown eye) at its largest; `carried_most`: the most carried; `steady_ms`: when the carry was
 * last over 1 mm. */
static float whip_play(double dt, bool whip, bool turn, float *carried_most, double *steady_ms) {
  TfpEyeWhip w; w.whips = 0; tfp_eye_whip_reset(&w);
  float eye[3][3] = {{0}}, most = 0; int n = 0; if (carried_most) *carried_most = 0; if (steady_ms) *steady_ms = 0;
  for (double ms = 0; ms <= 1300; ms += dt, ++n) {
    WhipScene s; whip_scene(ms, turn, &s);
    float add[3] = {0}, offset[3];
    for (int c = 0; c < 3; ++c) offset[c] = s.head[c]-s.root[c];
    const float carried = whip ? tfp_eye_whip_step(&w, offset, s.axes, tfp_eye_whip_view_yaw(s.view), true, 5000+ms, 250, add) : 0;
    if (carried_most && carried > *carried_most) *carried_most = carried;
    if (steady_ms && carried > .001f) *steady_ms = ms;
    memcpy(eye[0], eye[1], 12); memcpy(eye[1], eye[2], 12); whip_eye(&s, add, eye[2]);
    if (n >= 2) { float d = 0; for (int c = 0; c < 3; ++c) { const float v = eye[2][c]-2*eye[1][c]+eye[0][c]; d += v*v; } if (sqrtf(d) > most) most = sqrtf(d); }
  }
  return most;
}
/* A made-up body: the root at `root`, turned to `yaw` (rows of its axes), the head `ahead` metres in front of it and
 * 0.4 m up (the point that rides it), the view's heading `view`. One frame through `w`; returns what it carried. */
static float whip_frame(TfpEyeWhip *w, float yaw, float ahead, float view, bool begin, bool head_rides, double ms, float *add) {
  const float c = cosf(yaw), s = sinf(yaw), axes[9] = {c, s, 0, -s, c, 0, 0, 0, 1};
  const float offset[3] = {head_rides ? -s*ahead : 0, head_rides ? c*ahead : ahead, .4f};
  return tfp_eye_whip_step(w, offset, axes, view, begin, 5000+ms, 250, add);
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
  /* ---- Round 30, the whip. ---- */
  {
    /* 1. Soak 36's dive (07:03:01.039): at every frame time the arc is carried and let out, and the judge's measure of the
     * shown eye falls under its 0.06 m, or within 0.01 m of the floor where the body's own path is over it (at 50 ms): the
     * eye's own path with the root's heading held. Without the whip it is the arc's (0.68 m at 33.3 ms in the log). */
    for (double dt = 8; dt <= 50.1; dt = dt < 16 ? 16.7 : dt < 33 ? 33.3 : dt < 49 ? 50 : 99) {
      float carried = 0; double steady = 0;
      const float with = whip_play(dt, true, true, &carried, &steady), bare = whip_play(dt, false, true, NULL, NULL), held = whip_play(dt, false, false, NULL, NULL);
      printf("  whip, soak 36's dive at %.1f ms: the judge's measure %.3f m (without the whip %.3f, the root's heading held %.3f), carried at most %.3f m, let out by %.0f ms\n",
             dt, with, bare, held, carried, steady);
      CHECK(with < fmaxf(.06f, held+.01f) && with < bare && carried > .4f && carried <= TFP_EYE_WHIP_MOST_M && steady < 1300-2*dt);
      if (dt > 30) CHECK(bare > .2f);
    }
    /* 2. The player turning: the view and the root together, 6 rad/s (a fast mouse turn) or 12, with the head 0.7 m in
     * front of the root (prone): nothing is carried. The view turning alone (the root held): nothing. */
    for (double dt = 8; dt <= 50.1; dt = dt < 16 ? 16.7 : dt < 33 ? 33.3 : dt < 49 ? 50 : 99) for (float rate = 6; rate <= 12; rate += 6) {
      TfpEyeWhip w; w.whips = 0; tfp_eye_whip_reset(&w); float add[3], most = 0, alone = 0;
      for (double ms = 0; ms <= 600; ms += dt) { const float yaw = rate*(float)ms/1000; most = fmaxf(most, whip_frame(&w, yaw, .7f, yaw+1.5708f, true, true, ms, add)); }
      tfp_eye_whip_reset(&w);
      for (double ms = 0; ms <= 600; ms += dt) alone = fmaxf(alone, whip_frame(&w, 0, .7f, rate*(float)ms/1000, true, true, ms, add));
      CHECK(most == 0 && alone == 0 && w.whips == 0);
    }
    /* 3. A root that turns round a head that stays where it was (soak 27, 13:04:44.367: the heading flipped 180 degrees,
     * the head did not swing): no whip. A root turning under a held view at 6 rad/s, under the start: none. A whip in a
     * body that is not on foot (begin false: a ragdoll, a throw, full aim): none. */
    {
      TfpEyeWhip w; w.whips = 0; tfp_eye_whip_reset(&w); float add[3], most = 0;
      most = fmaxf(most, whip_frame(&w, 0, .7f, 0, true, false, 0, add));
      most = fmaxf(most, whip_frame(&w, 3.1f, .7f, 0, true, false, 33.3, add));
      CHECK(most == 0 && w.whips == 0);
      tfp_eye_whip_reset(&w);
      for (double ms = 0; ms <= 300; ms += 33.3) most = fmaxf(most, whip_frame(&w, 6*(float)ms/1000, .7f, 0, true, true, ms, add));
      CHECK(most == 0 && w.whips == 0);
      tfp_eye_whip_reset(&w);
      most = fmaxf(most, whip_frame(&w, 0, .7f, 0, false, true, 0, add));
      most = fmaxf(most, whip_frame(&w, .6f, .7f, 0, false, true, 33.3, add));
      CHECK(most == 0 && w.whips == 0);
    }
    /* 4. A dive's arc and a crouch: the root goes forward and down and pitches, its heading held, the head with it:
     * nothing is carried. */
    {
      TfpEyeWhip w; w.whips = 0; tfp_eye_whip_reset(&w); float add[3], most = 0;
      for (double ms = 0; ms <= 600; ms += 16.7) {
        const float t = (float)ms/1000, pitch = -1.2f*t, c = cosf(pitch), s = sinf(pitch);
        const float axes[9] = {1, 0, 0, 0, c, s, 0, -s, c}, offset[3] = {0, c*.06f-s*1.5f*(1-t), s*.06f+c*1.5f*(1-t)};
        (void)axes; const float root_axes[9] = {1, 0, 0, 0, c, s, 0, -s, c};
        most = fmaxf(most, tfp_eye_whip_step(&w, offset, root_axes, 1.5708f, true, 5000+ms, 250, add));
      }
      CHECK(most == 0 && w.whips == 0);
    }
    /* 5. One whip of 0.6 rad in a frame and nothing after, at every frame time: carried in that frame, then let out from
     * rest to rest: the step's change from the let-out at most TFP_EYE_WHIP_STEP_CHANGE_M (plus rounding), over no less
     * than TFP_EYE_WHIP_LET_OUT_MS; a pause longer than the gap forgets it. */
    for (double dt = 8; dt <= 50.1; dt = dt < 16 ? 16.7 : dt < 33 ? 33.3 : dt < 49 ? 50 : 99) {
      TfpEyeWhip w; w.whips = 0; tfp_eye_whip_reset(&w); float add[3], prev[3] = {0}, prev_step[3] = {0}, change = 0, first = 0; double gone = 0, ms = 0;
      whip_frame(&w, 0, .7f, 0, true, true, ms, add); ms += dt;
      first = whip_frame(&w, .6f, .7f, 0, true, true, ms, add); memcpy(prev, add, 12);
      for (int i = 0; i < 400 && w.known && (w.turning || w.letting); ++i) {
        ms += dt; whip_frame(&w, .6f, .7f, 0, true, true, ms, add);
        float step[3], d = 0; for (int c = 0; c < 3; ++c) { step[c] = add[c]-prev[c]; d += (step[c]-prev_step[c])*(step[c]-prev_step[c]); }
        change = fmaxf(change, sqrtf(d)); memcpy(prev_step, step, 12); memcpy(prev, add, 12);
        if (!w.letting && !w.turning) gone = ms;
      }
      CHECK(fabsf(first-2*.7f*sinf(.3f)) < .002f && w.whips == 1 && !w.letting);
      CHECK(change <= TFP_EYE_WHIP_STEP_CHANGE_M+.002f && gone-dt >= TFP_EYE_WHIP_LET_OUT_MS-dt);
      whip_frame(&w, 0, .7f, 0, true, true, ms, add); whip_frame(&w, .6f, .7f, 0, true, true, ms+dt, add);
      CHECK(whip_frame(&w, .6f, .7f, 0, true, true, ms+dt+300, add) == 0 && !w.turning);
    }
  }
  printf("camera damp: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
