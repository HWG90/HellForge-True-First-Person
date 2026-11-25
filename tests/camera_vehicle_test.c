/* The vehicle seat rule, the seat anchor and eye, and the seat glide: the arithmetic alone (src/camera_vehicle.h). */
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

  /* ---- 6. Round 38: the cases by name. A valve is seated on foot: not a seat. A thrown or dead body (off foot, not
   * seated) is not a seat. A body in physics in its seat is down, not a seat. ---- */
  CHECK(!tfp_vehicle_seat(true, false, false));   /* a valve */
  CHECK(!tfp_vehicle_seat(false, true, false));   /* thrown, diving */
  CHECK(!tfp_vehicle_seat(false, true, true));    /* dead */
  CHECK(!tfp_vehicle_seat(true, true, true));     /* killed in the seat: the lock's body that is down */
  CHECK(tfp_vehicle_seat(true, true, false));     /* the exosuit */

  /* ---- 7. The anchor: the head's offset is used as it is until it has stayed within 2 cm for 300 ms (and 300 ms
   * after entry); then it is frozen, and a bob of 3 cm does not move it; a move of 0.3 m starts the settling again. */
  {
    TfpSeatAnchor a; tfp_seat_anchor_reset(&a);
    float out[3]; const float settle[3] = {.02f, .05f, .84f};
    double tick = 5000;
    /* the climb: the head swings 0.1 m a frame for 200 ms: used as it is */
    for (int i = 0; i < 6; ++i, tick += 33.3) {
      const float head[3] = {.02f, .05f + .1f*(float)(6-i), .84f};
      CHECK(tfp_seat_anchor_step(&a, head, tick, out) && apart(out, head) < 1e-6f && !a.settled);
    }
    /* still: frozen once 300 ms have passed with it within 2 cm */
    int frames = 0;
    while (!a.settled && frames < 40) { const float head[3] = {settle[0], settle[1] + (frames & 1 ? .01f : 0), settle[2]};
      CHECK(tfp_seat_anchor_step(&a, head, tick, out)); tick += 33.3; ++frames; }
    CHECK(a.settled && frames >= 9 && frames <= 11 && apart(out, settle) < .011f);
    const float frozen[3] = {out[0], out[1], out[2]};
    /* a ±3 cm bob (the suit's gait) does not move it */
    float most = 0;
    for (int i = 0; i < 60; ++i, tick += 8.3) {
      const float bob[3] = {settle[0] + .03f*sinf((float)i*.7f), settle[1] + .03f*cosf((float)i*.5f), settle[2] + .03f*sinf((float)i*1.3f)};
      CHECK(tfp_seat_anchor_step(&a, bob, tick, out));
      const float d = apart(out, frozen); if (d > most) most = d;
    }
    CHECK(most == 0 && a.settled);
    /* re-seated 0.35 m away: let go, used as it is, frozen again 300 ms later */
    const float moved[3] = {settle[0], settle[1] - .35f, settle[2]};
    CHECK(tfp_seat_anchor_step(&a, moved, tick, out) && !a.settled && apart(out, moved) < 1e-6f);
    for (int i = 0; i < 12; ++i) { tick += 33.3; CHECK(tfp_seat_anchor_step(&a, moved, tick, out)); }
    CHECK(a.settled && apart(out, moved) < 1e-6f);
    /* not a number: nothing written, nothing changed */
    const float bad[3] = {NAN, 0, 0}; float keep[3] = {7, 7, 7};
    CHECK(!tfp_seat_anchor_step(&a, bad, tick, keep) && keep[0] == 7 && a.settled);
    /* reset: a fresh entry is not frozen before 300 ms, even when the head is still from the first frame */
    tfp_seat_anchor_reset(&a); tick = 100;
    for (int i = 0; i < 9; ++i, tick += 33.3) { CHECK(tfp_seat_anchor_step(&a, settle, tick, out)); CHECK(!a.settled); }
    tick += 33.3; CHECK(tfp_seat_anchor_step(&a, settle, tick, out) && a.settled);
  }

  /* ---- 8. The eye: root + R (anchor) + forward along the root's level forward + up. The root's yaw turns it; its
   * pitch does not tip the forward step up or down; a root with no level forward is refused. ---- */
  {
    const float root[3] = {-24.9f, -2.5f, 4.9f}, offset[3] = {.03f, .06f, .84f};
    for (int k = 0; k < 8; ++k) {
      const float yaw_r = (float)k*.8f, c = cosf(yaw_r), s = sinf(yaw_r);
      const float basis[9] = {c, s, 0, -s, c, 0, 0, 0, 1};
      float eye[3]; CHECK(tfp_seat_eye(root, basis, offset, .18f, .04f, eye));
      float local[3]; tfp_seat_local(basis, root, eye, local);
      CHECK(fabsf(local[0]-.03f) < 1e-5f && fabsf(local[1]-(.06f+.18f)) < 1e-5f && fabsf(local[2]-(.84f+.04f)) < 1e-5f);
    }
    /* pitched 20 degrees forward: the step stays level, the offset turns with the root */
    const float p = .349f, basis[9] = {1, 0, 0, 0, cosf(p), -sinf(p), 0, sinf(p), cosf(p)};
    float eye[3]; CHECK(tfp_seat_eye(root, basis, offset, .18f, .04f, eye));
    float want[3]; for (int c = 0; c < 3; ++c) want[c] = root[c] + offset[0]*basis[c] + offset[1]*basis[3+c] + offset[2]*basis[6+c];
    want[1] += .18f; want[2] += .04f;
    CHECK(apart(eye, want) < 1e-5f);
    const float up_basis[9] = {1, 0, 0, 0, 0, 1, 0, -1, 0}; float keep[3] = {7, 7, 7};
    CHECK(!tfp_seat_eye(root, up_basis, offset, .18f, .04f, keep) && keep[0] == 7);
    const float bad[3] = {0, NAN, 0};
    CHECK(!tfp_seat_eye(root, basis, bad, .18f, .04f, keep) && keep[0] == 7);
  }

  printf("camera vehicle: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
