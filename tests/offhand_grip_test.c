/* The support hand's reach (src/offhand_grip.h) on a model left arm with known bone lengths, in the frame of the rig's
 * presentation root (X right, Y forward, Z up): reach, stretch, the elbow's side, the step limit over random target
 * paths at four frame times, a target going round behind the shoulder, release, and the refusals. */
#include "offhand_grip.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const double UPPER = .3027, LOWER = .2872;   /* the rig's own left arm, measured on the recorded frames */
static const double SHOULDER[3] = {-.187, -.117, -.124};
static uint64_t seed = 0x9e3779b97f4a7c15u;
static double rnd(void) { seed ^= seed << 13, seed ^= seed >> 7, seed ^= seed << 17; return (double)(seed >> 11) / 9007199254740992.0; }

static TfpGraftMatrix matrix(const double r[3][3], const double p[3]) {
  TfpGraftMatrix m = {{0}};
  for (unsigned i = 0; i < 3; i++) {
    for (unsigned j = 0; j < 3; j++) m.values[4 * i + j] = (float)r[i][j];
    m.values[12 + i] = (float)p[i];
  }
  m.values[15] = 1;
  return m;
}
static void rotation(const double axis_in[3], double angle, double r[3][3]) {
  double axis[3] = {axis_in[0], axis_in[1], axis_in[2]};
  const double n = tfp_og_len(axis);
  for (unsigned i = 0; i < 3; i++) axis[i] /= n;
  double id[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  tfp_og_turn_rows(id, axis, angle);
  memcpy(r, id, sizeof id);
}
static void random_rotation(double r[3][3]) {
  const double axis[3] = {rnd() - .5, rnd() - .5, rnd() - .5 + 1e-3};
  rotation(axis, rnd() * 6.28, r);
}
/* The model arm: the upper arm hangs from the shoulder turned by `turn`; each bone lies along its local X. */
typedef struct { TfpGraftMatrix upper, fore, hand; } Arm;
static Arm model(void) {
  Arm arm;
  double r[3][3];
  const double axis[3] = {.2, .3, 1};
  rotation(axis, 1.2, r);
  arm.upper = matrix(r, SHOULDER);
  const double bend[3] = {0, 0, 1}, fp[3] = {UPPER, 0, 0}, hp[3] = {LOWER, 0, 0};
  rotation(bend, -.9, r);
  arm.fore = matrix(r, fp);
  const double twist[3] = {1, .2, 0};
  rotation(twist, .4, r);
  arm.hand = matrix(r, hp);
  return arm;
}
typedef struct { TfpOgFrame u, f, h; } World;
static World world(const Arm *a) {
  World w;
  const TfpOgFrame u = tfp_og_frame(&a->upper), f = tfp_og_frame(&a->fore), h = tfp_og_frame(&a->hand);
  w.u = u, w.f = tfp_og_then(&f, &u), w.h = tfp_og_then(&h, &w.f);
  return w;
}
static double gap(const double a[3], const double b[3]) { double d[3]; tfp_og_sub(a, b, d); return tfp_og_len(d); }
static double degrees(const TfpGraftMatrix *a, const TfpGraftMatrix *b) {
  const TfpOgFrame x = tfp_og_frame(a), y = tfp_og_frame(b);
  return tfp_og_angle(x.r, y.r) * 180 / TFP_OG_PI;
}
static TfpGraftMatrix target_at(const double p[3], const double r[3][3]) { return matrix(r, p); }
/* A point `distance` from the shoulder in direction (yaw, pitch) of the root's frame. */
static void around(double distance, double yaw, double pitch, double out[3]) {
  out[0] = SHOULDER[0] + distance * cos(pitch) * sin(yaw);
  out[1] = SHOULDER[1] + distance * cos(pitch) * cos(yaw);
  out[2] = SHOULDER[2] + distance * sin(pitch);
}
static TfpOffhandResult run(TfpOffhand *s, Arm *out, const TfpGraftMatrix *target, bool wanted, double seconds, int frames) {
  const Arm base = model();
  TfpOffhandResult r = {0};
  for (int i = 0; i < frames; i++) {
    *out = base;
    r = tfp_offhand_step(s, &out->upper, &out->fore, &out->hand, target, wanted, seconds);
  }
  return r;
}

/* Targets in reach: the hand ends exactly on them, turned as they are; shoulder and bone lengths stay. */
static void test_reach(void) {
  const Arm base = model();
  const World b = world(&base);
  CHECK(fabs(gap(b.f.p, b.u.p) - UPPER) < 1e-6 && fabs(gap(b.h.p, b.f.p) - LOWER) < 1e-6);
  for (int n = 0; n < 400; n++) {
    TfpOffhand s;
    tfp_offhand_reset(&s);
    double p[3], r[3][3];
    around(.12 + rnd() * (.95 * (UPPER + LOWER) - .12), rnd() * 2.6 - 1.3, rnd() * 1.6 - .8, p);
    if (gap(p, b.h.p) > TFP_OG_SHIFT_M - .02) { n--; continue; }
    random_rotation(r);
    const TfpGraftMatrix t = target_at(p, r);
    Arm a;
    const TfpOffhandResult res = run(&s, &a, &t, true, 1 / 60.0, 60);
    const World w = world(&a);
    CHECK(res.reason == TFP_OG_HELD && res.weight == 1);
    CHECK(gap(w.h.p, p) < 2e-5 && res.to_target_m < 2e-5);
    double tr[3][3];
    for (unsigned i = 0; i < 3; i++) for (unsigned j = 0; j < 3; j++) tr[i][j] = t.values[4 * i + j];
    CHECK(tfp_og_angle(w.h.r, tr) * 180 / TFP_OG_PI < .01);
    CHECK(gap(w.u.p, b.u.p) < 1e-6 && fabs(gap(w.f.p, w.u.p) - UPPER) < 1e-5 && fabs(gap(w.h.p, w.f.p) - LOWER) < 1e-5);
    /* the positions written are the drive's, untouched */
    for (unsigned i = 12; i < 15; i++) CHECK(a.upper.values[i] == base.upper.values[i] && a.fore.values[i] == base.fore.values[i] && a.hand.values[i] == base.hand.values[i]);
    CHECK(tfp_graft_rigid(&a.upper) && tfp_graft_rigid(&a.fore) && tfp_graft_rigid(&a.hand));
  }
}
/* A target past the arm's reach (but within REACH): the hand stops on the line to it at STRETCH of the arm, the elbow
 * bent on the pole's side (down and a little out). */
static void test_stretch(void) {
  for (int n = 0; n < 100; n++) {
    TfpOffhand s;
    tfp_offhand_reset(&s);
    double p[3], r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const double distance = (UPPER + LOWER) * (1.0 + rnd() * (TFP_OG_REACH - 1.01));
    around(distance, rnd() * 1.2 - .6, rnd() * .6 - .3, p);
    const World b = world(&(Arm){model().upper, model().fore, model().hand});
    if (gap(p, b.h.p) > TFP_OG_SHIFT_M - .01) { n--; continue; }
    const TfpGraftMatrix t = target_at(p, r);
    Arm a;
    const TfpOffhandResult res = run(&s, &a, &t, true, 1 / 60.0, 60);
    const World w = world(&a);
    double dir[3], reached[3];
    tfp_og_sub(p, SHOULDER, dir);
    for (unsigned i = 0; i < 3; i++) dir[i] /= distance, reached[i] = SHOULDER[i] + dir[i] * (UPPER + LOWER) * TFP_OG_STRETCH;
    CHECK(res.reason == TFP_OG_HELD && gap(w.h.p, reached) < 3e-5);
    CHECK(fabs(res.to_target_m - (distance - (UPPER + LOWER) * TFP_OG_STRETCH)) < 3e-5);
    /* the elbow is off the line, below it */
    double es[3], along = 0, off[3];
    tfp_og_sub(w.f.p, SHOULDER, es);
    along = tfp_og_dot(es, dir);
    for (unsigned i = 0; i < 3; i++) off[i] = es[i] - dir[i] * along;
    CHECK(tfp_og_len(off) > .03 && off[2] < 0);
  }
}
/* Too far from the shoulder, or from the drawn hand: not taken; the arm goes back to the drive's pose, bit for bit. */
static void test_refused(void) {
  const Arm base = model();
  const World b = world(&base);
  double r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, p[3];
  TfpOffhand s;
  /* first held on a good target, then the target goes beyond reach */
  tfp_offhand_reset(&s);
  around(.4, .3, 0, p);
  TfpGraftMatrix t = target_at(p, r);
  Arm a;
  CHECK(run(&s, &a, &t, true, 1 / 60.0, 30).reason == TFP_OG_HELD && s.active);
  around((UPPER + LOWER) * (TFP_OG_REACH + .02), .3, 0, p);
  t = target_at(p, r);
  CHECK(gap(p, b.h.p) < TFP_OG_SHIFT_M);
  TfpOffhandResult res = run(&s, &a, &t, true, 1 / 60.0, 1);
  CHECK(res.reason == TFP_OG_OUT_OF_REACH && s.active);
  res = run(&s, &a, &t, true, 1 / 60.0, 60);
  CHECK(res.reason == TFP_OG_OUT_OF_REACH && !s.active && !memcmp(&a, &base, sizeof a));
  /* too far from the drawn hand */
  tfp_offhand_reset(&s);
  for (unsigned i = 0; i < 3; i++) p[i] = b.h.p[i] + (i == 0 ? -(TFP_OG_SHIFT_M + .01) : 0);
  t = target_at(p, r);
  res = run(&s, &a, &t, true, 1 / 60.0, 5);
  CHECK(res.reason == TFP_OG_TOO_FAR && !s.active && !memcmp(&a, &base, sizeof a));
  /* no target while wanted */
  res = run(&s, &a, NULL, true, 1 / 60.0, 5);
  CHECK(res.reason == TFP_OG_NO_TARGET && !memcmp(&a, &base, sizeof a));
  /* not wanted, nothing held: idle, untouched */
  res = run(&s, &a, &t, false, 1 / 60.0, 5);
  CHECK(res.reason == TFP_OG_IDLE && !memcmp(&a, &base, sizeof a));
  /* a pose that is not rigid: this frame is left as it is, the corrections are kept */
  tfp_offhand_reset(&s);
  around(.4, .3, 0, p);
  t = target_at(p, r);
  run(&s, &a, &t, true, 1 / 60.0, 30);
  double kept[3][3][3];
  memcpy(kept, s.c, sizeof kept);
  Arm bad = base;
  bad.fore.values[0] = 3;
  const Arm before = bad;
  res = tfp_offhand_step(&s, &bad.upper, &bad.fore, &bad.hand, &t, true, 1 / 60.0);
  CHECK(res.reason == TFP_OG_NOT_RIGID && !memcmp(&bad, &before, sizeof bad) && !memcmp(kept, s.c, sizeof kept));
  /* no time: nothing moves (a second draw in the same tick shows the same arm) */
  a = base;
  const TfpOffhandResult same = tfp_offhand_step(&s, &a.upper, &a.fore, &a.hand, &t, true, 0);
  CHECK(same.step_degrees == 0 && !memcmp(kept, s.c, sizeof kept));
  /* a zero-length bone */
  bad = base;
  bad.fore.values[12] = 0;
  tfp_offhand_reset(&s);
  CHECK(tfp_offhand_step(&s, &bad.upper, &bad.fore, &bad.hand, &t, true, 1 / 60.0).reason == TFP_OG_NOT_RIGID);
}
/* Released: back to the drive's pose at the step limit, ending bit for bit on it. */
static void test_release(void) {
  const Arm base = model();
  TfpOffhand s;
  tfp_offhand_reset(&s);
  double p[3], r[3][3];
  around(.45, .4, .2, p);
  random_rotation(r);
  const TfpGraftMatrix t = target_at(p, r);
  Arm a, prev;
  run(&s, &a, &t, true, 1 / 60.0, 40);
  prev = a;
  int frames = 0;
  TfpOffhandResult res;
  do {
    a = base;
    res = tfp_offhand_step(&s, &a.upper, &a.fore, &a.hand, &t, false, 1 / 60.0);
    CHECK(degrees(&a.upper, &prev.upper) <= res.limit_degrees + 1e-3 && degrees(&a.fore, &prev.fore) <= res.limit_degrees + 1e-3 &&
          degrees(&a.hand, &prev.hand) <= res.limit_degrees + 1e-3);
    prev = a;
  } while (s.active && ++frames < 200);
  CHECK(!s.active && frames > 1 && frames < 60 && !memcmp(&a, &base, sizeof a) && res.reason == TFP_OG_IDLE);
}
/* Random target paths, held, with jumps and with the target dropping out: no written joint ever turns more than the
 * frame's limit, at 8, 16.7, 33.3 and 50 ms a frame. */
static void test_step_limit(void) {
  static const double frame_ms[4] = {8, 16.7, 33.3, 50};
  for (unsigned k = 0; k < 4; k++) {
    const double dt = frame_ms[k] / 1000;
    double most = 0;
    for (int path = 0; path < 40; path++) {
      TfpOffhand s;
      tfp_offhand_reset(&s);
      const Arm base = model();
      Arm a = base, prev = base;
      double yaw = rnd() * 2 - 1, pitch = rnd() - .5, distance = .3 + rnd() * .3, r[3][3];
      random_rotation(r);
      for (int i = 0; i < 400; i++) {
        yaw += (rnd() - .5) * .3, pitch += (rnd() - .5) * .2, distance += (rnd() - .5) * .06;
        if (rnd() < .03) yaw = rnd() * 6.28 - 3.14, pitch = rnd() * 2 - 1, distance = .1 + rnd() * .6, random_rotation(r); /* a jump */
        if (distance < .05) distance = .05;
        if (distance > .7) distance = .7;
        double p[3];
        around(distance, yaw, pitch, p);
        const TfpGraftMatrix t = target_at(p, r);
        const bool gone = rnd() < .05, wanted = rnd() > .03;
        a = base;
        const TfpOffhandResult res = tfp_offhand_step(&s, &a.upper, &a.fore, &a.hand, gone ? NULL : &t, wanted, dt);
        const double limit = fmin(TFP_OG_TURN_DPS * dt, TFP_OG_TURN_CAP);
        CHECK(fabs(res.limit_degrees - limit) < 1e-3);
        const double step = fmax(degrees(&a.upper, &prev.upper), fmax(degrees(&a.fore, &prev.fore), degrees(&a.hand, &prev.hand)));
        CHECK(step <= limit + 2e-3);
        CHECK(res.step_degrees <= limit + 1e-6);
        CHECK(tfp_graft_rigid(&a.upper) && tfp_graft_rigid(&a.fore) && tfp_graft_rigid(&a.hand));
        if (step > most) most = step;
        prev = a;
      }
    }
    printf("offhand grip: %4.1f ms frames: largest joint step %.2f degrees (limit %.2f)\n", frame_ms[k], most, fmin(TFP_OG_TURN_DPS * dt, TFP_OG_TURN_CAP));
  }
}
/* The target goes round the shoulder, through the side and behind it, slowly enough to be held all the way: the elbow
 * stays on one side (no flip), and every frame is within the step limit. */
static void test_behind(void) {
  const double dt = 1 / 60.0;
  for (int lap = 0; lap < 3; lap++) {
    TfpOffhand s;
    tfp_offhand_reset(&s);
    const Arm base = model();
    Arm a, prev = base;
    double r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, last_side[3] = {0, 0, 0};
    bool has_last = false;
    double worst_turn = 0;
    for (int i = 0; i <= 720; i++) {
      double p[3];
      const double yaw = (double)i / 720 * 2 * TFP_OG_PI, pitch = lap == 0 ? 0 : lap == 1 ? .5 : -.4;
      around(.42, yaw, pitch, p);
      const TfpGraftMatrix t = target_at(p, r);
      a = base;
      const TfpOffhandResult res = tfp_offhand_step(&s, &a.upper, &a.fore, &a.hand, &t, true, dt);
      (void)res;
      CHECK(fmax(degrees(&a.upper, &prev.upper), fmax(degrees(&a.fore, &prev.fore), degrees(&a.hand, &prev.hand))) <= fmin(TFP_OG_TURN_DPS * dt, TFP_OG_TURN_CAP) + 2e-3);
      if (has_last) {
        const double c = tfp_og_dot(s.side, last_side);
        const double turn = acos(c > 1 ? 1 : c < -1 ? -1 : c) * 180 / TFP_OG_PI;
        if (turn > worst_turn) worst_turn = turn;
        CHECK(turn < 20); /* the elbow's side moves on, it never turns over */
      }
      memcpy(last_side, s.side, sizeof last_side), has_last = s.has_side;
      prev = a;
    }
    printf("offhand grip: target round the shoulder at pitch %s: the elbow's side turned at most %.1f degrees in a frame\n",
           lap == 0 ? "0" : lap == 1 ? "+0.5" : "-0.4", worst_turn);
  }
}
/* The shorter way round: the share of a turn near 180 degrees, and of its negated quaternion, is the same rotation. */
static void test_short_way(void) {
  for (int n = 0; n < 2000; n++) {
    double r[3][3], half[3][3], q[4], back[3][3];
    const double axis[3] = {rnd() - .5, rnd() - .5, rnd() - .5 + 1e-3};
    const double angle = rnd() * 3.14;
    rotation(axis, angle, r);
    tfp_og_q_of(r, q);
    tfp_og_of_q(q, back);
    CHECK(tfp_og_angle(r, back) < 1e-6);
    for (unsigned i = 0; i < 4; i++) q[i] = -q[i];
    tfp_og_of_q(q, back);
    CHECK(tfp_og_angle(r, back) < 1e-6);
    tfp_og_part(r, .5, half);
    const double id[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    CHECK(fabs(tfp_og_angle(half, id) - angle / 2) < 1e-5);
    double twice[3][3];
    tfp_og_rmul(half, half, twice);
    CHECK(tfp_og_angle(twice, r) < 1e-5);
  }
}

int main(void) {
  test_reach();
  test_stretch();
  test_refused();
  test_release();
  test_step_limit();
  test_behind();
  test_short_way();
  printf("offhand grip: %u checks passed\n", checks);
  return 0;
}
