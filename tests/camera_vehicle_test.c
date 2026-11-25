/* The vehicle seat rule and the seat glide, the arithmetic alone (src/camera_vehicle.h). */
#include <stdio.h>
#include "camera_vehicle.h"

static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

static float apart(const float *a, const float *b) {
  return sqrtf((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}
static TfpQ yaw(float radians) { return tfp_q(0, 0, sinf(radians/2), cosf(radians/2)); }

int main(void) {
  /* ---- 1. The rule: a seat is a vehicle only when the body is seated, off foot and not in physics. ---- */
  for (int seated = 0; seated < 2; ++seated) for (int off = 0; off < 2; ++off) for (int ragdoll = 0; ragdoll < 2; ++ragdoll)
    CHECK(tfp_vehicle_seat(seated, off, ragdoll) == (seated && off && !ragdoll));

  /* ---- 2. The duration: sqrt(2 d / 14 m/s²), 300 to 600 ms; beyond 8 m (or not a number) a cut. ---- */
  CHECK(tfp_seat_glide_ms(0) == TFP_SEAT_GLIDE_MIN_MS);
  CHECK(tfp_seat_glide_ms(.2f) == TFP_SEAT_GLIDE_MIN_MS);                        /* 169 ms: raised to 300 */
  CHECK(fabsf(tfp_seat_glide_ms(1.0f) - sqrtf(2/14.0f)*1000) < .01f);            /* 378 ms */
  CHECK(tfp_seat_glide_ms(3.6f) == TFP_SEAT_GLIDE_MAX_MS);                       /* the exosuit's 3.6 m: 717 ms, held to 600 */
  CHECK(tfp_seat_glide_ms(8.0f) == TFP_SEAT_GLIDE_MAX_MS);
  CHECK(tfp_seat_glide_ms(8.01f) == 0 && tfp_seat_glide_ms(NAN) == 0 && tfp_seat_glide_ms(-1) == 0 && tfp_seat_glide_ms(INFINITY) == 0);
  for (float d = 0; d <= 8; d += .05f) { const float ms = tfp_seat_glide_ms(d); CHECK(ms >= 300 && ms <= 600); }

  /* ---- 3. A glide to a target that moves on: the first frame is the start, the last frame before the end is on the
   * target to within a hair, the frame at the end writes nothing (the game's own frame is forwarded), and so does
   * every frame after it. Rotation the same way, by the shortest arc. ---- */
  {
    TfpSeatGlide g; tfp_seat_glide_reset(&g);
    const float from[3] = {-22.3f, -.65f, 3.99f}; const TfpQ from_q = yaw(.4f);
    float target[3] = {-24.5f, -2.7f, 6.1f};
    CHECK(tfp_seat_glide_begin(&g, from, from_q, target, 1000.0));
    CHECK(g.active && g.ms == TFP_SEAT_GLIDE_MAX_MS);
    float out[3] = {0}; TfpQ q = tfp_q_identity();
    CHECK(tfp_seat_glide_apply(&g, target, yaw(2.0f), 1000.0, out, &q));
    CHECK(out[0] == from[0] && out[1] == from[1] && out[2] == from[2] && tfp_degrees_between(q, from_q) < .01f);
    float last[3] = {from[0], from[1], from[2]}; int frames = 0; double tick = 1000;
    for (;;) {
      tick += 8.3; target[0] += .02f; /* the suit walks on: 2.4 m/s */
      float before[3] = {-1, -1, -1}; TfpQ before_q = tfp_q(9, 9, 9, 9);
      memcpy(out, before, sizeof out); q = before_q;
      if (!tfp_seat_glide_apply(&g, target, yaw(2.0f), tick, out, &q)) {
        CHECK(memcmp(out, before, sizeof out) == 0 && q.x == 9); /* nothing written at or after the end */
        break;
      }
      ++frames; memcpy(last, out, sizeof last);
      if (tick + 8.3 >= 1000.0 + g.ms) CHECK(apart(out, target) < .002f && tfp_degrees_between(q, yaw(2.0f)) < .2f);
    }
    CHECK(frames >= 70 && frames <= 73 && !g.active);
    float again[3] = {7, 7, 7}; TfpQ again_q = tfp_q(5, 5, 5, 5);
    CHECK(!tfp_seat_glide_apply(&g, target, yaw(2.0f), tick + 100, again, &again_q) && again[0] == 7 && again_q.x == 5);
  }

  /* ---- 4. Its pace: to a still target from rest to rest, no step larger than the smoothstep's peak (1.5 d / T a
   * second), steps that grow then shrink without a jump between neighbours, and the first and last steps small. ---- */
  {
    TfpSeatGlide g; tfp_seat_glide_reset(&g);
    const float from[3] = {0, 0, 0}, target[3] = {0, 2.0f, 0};
    CHECK(tfp_seat_glide_begin(&g, from, tfp_q_identity(), target, 0));
    const float ms = g.ms, dt = 10; float steps[100] = {0}; int n = 0; float last = 0;
    for (double tick = dt; n < 100; tick += dt) {
      float out[3]; TfpQ q;
      if (!tfp_seat_glide_apply(&g, target, tfp_q_identity(), tick, out, &q)) break;
      steps[n++] = out[1] - last; last = out[1];
    }
    float most = 0, change = 0;
    for (int i = 0; i < n; ++i) { if (steps[i] > most) most = steps[i]; CHECK(steps[i] >= 0); }
    for (int i = 1; i < n; ++i) { const float c = fabsf(steps[i] - steps[i-1]); if (c > change) change = c; }
    CHECK(n == (int)(ms/dt) - 1 + (fmodf(ms, dt) > 0));
    CHECK(most <= 1.5f*2.0f*dt/ms*1.001f);                 /* the peak speed of a smoothstep */
    CHECK(change <= 6*2.0f*dt*dt/(ms*ms)*1.01f);            /* its largest change of step: the peak acceleration */
    CHECK(steps[0] < most*.1f && steps[n-1] < most*.15f);
    printf("a 2 m glide over %.0f ms: largest step %.4f m in %.0f ms, largest change of step %.5f m\n", (double)ms, (double)most, (double)dt, (double)change);
  }

  /* ---- 5. No glide: beyond 8 m, from nowhere, from a rotation that is not one, or after a reset. ---- */
  {
    TfpSeatGlide g; tfp_seat_glide_reset(&g);
    const float from[3] = {0, 0, 0}, far[3] = {0, 8.5f, 0}, near[3] = {0, 1, 0}, bad[3] = {NAN, 0, 0};
    CHECK(!tfp_seat_glide_begin(&g, from, tfp_q_identity(), far, 0) && !g.active);
    CHECK(!tfp_seat_glide_begin(&g, bad, tfp_q_identity(), near, 0) && !g.active);
    CHECK(!tfp_seat_glide_begin(&g, from, tfp_q(0, 0, 0, 0), near, 0) && !g.active);
    CHECK(!tfp_seat_glide_begin(&g, from, tfp_q_identity(), bad, 0) && !g.active);
    float out[3]; TfpQ q;
    CHECK(!tfp_seat_glide_apply(&g, near, tfp_q_identity(), 10, out, &q));
    CHECK(tfp_seat_glide_begin(&g, from, tfp_q_identity(), near, 0) && g.active);
    tfp_seat_glide_reset(&g); CHECK(!g.active && !tfp_seat_glide_apply(&g, near, tfp_q_identity(), 10, out, &q));
    /* A clock that went back is the glide's start, not a step backwards. */
    CHECK(tfp_seat_glide_begin(&g, from, tfp_q_identity(), near, 100));
    CHECK(tfp_seat_glide_apply(&g, near, tfp_q_identity(), 50, out, &q) && out[1] == 0);
    /* A target rotation that is not one keeps the start's. */
    CHECK(tfp_seat_glide_apply(&g, near, tfp_q(0, 0, 0, 0), 200, out, &q) && tfp_degrees_between(q, tfp_q_identity()) < .01f);
  }

  printf("camera vehicle: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
