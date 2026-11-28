/* The support hand's reach (src/offhand_grip.h) on a model left arm with known bone lengths, in the frame of the rig's
 * presentation root (X right, Y forward, Z up): reach, stretch, the elbow's side, the step limit over random target
 * paths at four frame times, a target going round behind the shoulder, release, and the refusals. */
#include "offhand_grip.h"
#include "grip_fingers.h"
#include "fp_hand_bind.h"
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
/* A target past the arm's reach (but within REACH): the arm straightens to STRETCH of its length and the shoulder
 * comes forward along the line to the target by the rest, so the hand is on it; the elbow bent on the pole's side
 * (down and a little out). */
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
    double dir[3], moved[3];
    tfp_og_sub(p, SHOULDER, dir);
    const double lack = distance - (UPPER + LOWER) * TFP_OG_STRETCH;
    for (unsigned i = 0; i < 3; i++) dir[i] /= distance, moved[i] = SHOULDER[i] + dir[i] * lack;
    CHECK(res.reason == TFP_OG_HELD && gap(w.h.p, p) < 3e-5 && res.to_target_m < 3e-5);
    CHECK(gap(w.u.p, moved) < 3e-5 && fabs(res.shoulder_m - lack) < 3e-5);
    CHECK(fabs(gap(w.h.p, w.u.p) - (UPPER + LOWER) * TFP_OG_STRETCH) < 3e-5);
    /* the elbow is off the line, below it */
    double es[3], along = 0, off[3];
    tfp_og_sub(w.f.p, w.u.p, es);
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
  around((UPPER + LOWER) * (TFP_OG_REACH_KEEP + .02), .3, 0, p);
  t = target_at(p, r);
  CHECK(gap(p, b.h.p) < TFP_OG_SHIFT_M);
  TfpOffhandResult res = run(&s, &a, &t, true, 1 / 60.0, 1);
  CHECK(res.reason == TFP_OG_OUT_OF_REACH && s.active);
  res = run(&s, &a, &t, true, 1 / 60.0, 60);
  CHECK(res.reason == TFP_OG_OUT_OF_REACH && !s.active && !memcmp(&a, &base, sizeof a));
  /* not held before: out of reach already beyond REACH */
  tfp_offhand_reset(&s);
  around((UPPER + LOWER) * (TFP_OG_REACH + .01), .3, 0, p);
  t = target_at(p, r);
  res = run(&s, &a, &t, true, 1 / 60.0, 5);
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
/* Round 36b (walking with the BR-14 Adjudicator: 321 "too far" frames in 13 s, held in 43 % of the frames): the drawn hand
 * swings away from a grip the body's hand stays pinned on. Once held, "too far" has a keep threshold (SHIFT_KEEP_M), as
 * the reach has; while the caller says the body's hand is pinned on the target it is not asked at all. A first take is
 * refused as before. */
static TfpGraftMatrix away_from_hand(double from_hand, double *from_shoulder) {
  const Arm base = model();
  const World b = world(&base);
  double d[3], p[3], r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  tfp_og_sub(b.u.p, b.h.p, d); /* from the hand through the shoulder, on: the furthest from the hand for a reach */
  const double h = tfp_og_len(d), reach = from_hand - h;
  for (unsigned i = 0; i < 3; i++) p[i] = b.u.p[i] + d[i] / h * reach;
  *from_shoulder = reach / (UPPER + LOWER);
  CHECK(fabs(gap(p, b.h.p) - from_hand) < 1e-9);
  return target_at(p, r);
}
static void test_too_far_keep(void) {
  TfpOffhand s;
  Arm a;
  const Arm base = model();
  double r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, p[3], share;
  around(.4, .3, 0, p);
  const TfpGraftMatrix near = target_at(p, r), swung = away_from_hand(.65, &share), gone = away_from_hand(.80, &share);
  CHECK(share < TFP_OG_REACH_KEEP && TFP_OG_SHIFT_M < .65 && .65 < TFP_OG_SHIFT_KEEP_M && TFP_OG_SHIFT_KEEP_M < .80);
  /* a first take at 0.65 m from the drawn hand: refused, whether or not the body's hand is pinned on it */
  for (int pinned = 0; pinned < 2; pinned++) {
    tfp_offhand_reset(&s);
    s.pinned = pinned;
    const TfpOffhandResult res = run(&s, &a, &swung, true, 1 / 60.0, 5);
    CHECK(res.reason == TFP_OG_TOO_FAR && !s.active && !memcmp(&a, &base, sizeof a));
  }
  /* held, then the drawn hand 0.65 m from the grip: kept, and the hand gets there */
  tfp_offhand_reset(&s);
  CHECK(run(&s, &a, &near, true, 1 / 60.0, 30).reason == TFP_OG_HELD);
  TfpOffhandResult res = {0};
  for (int i = 0; i < 60; i++) {
    res = run(&s, &a, &swung, true, 1 / 60.0, 1);
    CHECK(res.reason != TFP_OG_TOO_FAR);
  }
  CHECK(res.reason == TFP_OG_HELD && res.to_target_m < 1e-4);
  /* 0.80 m: beyond the keep threshold, let go, unless the body's hand is pinned on it */
  CHECK(run(&s, &a, &gone, true, 1 / 60.0, 1).reason == TFP_OG_TOO_FAR);
  tfp_offhand_reset(&s);
  s.pinned = true;
  CHECK(run(&s, &a, &near, true, 1 / 60.0, 30).reason == TFP_OG_HELD);
  for (int i = 0; i < 90; i++) {
    res = run(&s, &a, &gone, true, 1 / 60.0, 1);
    CHECK(res.reason != TFP_OG_TOO_FAR);
  }
  CHECK(res.reason == TFP_OG_HELD && res.to_target_m < 1e-4);
  /* the reach's keep threshold is unchanged: pinned or not, a grip beyond REACH_KEEP is let go */
  double far[3];
  around((UPPER + LOWER) * (TFP_OG_REACH_KEEP + .02), .3, 0, far);
  const TfpGraftMatrix out = target_at(far, r);
  CHECK(run(&s, &a, &out, true, 1 / 60.0, 1).reason == TFP_OG_OUT_OF_REACH);
  printf("offhand grip: held while the drawn hand swings 0.65 m and, pinned, 0.80 m from the grip; a first take at 0.65 m refused\n");
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

/* ---- round 26: scenes from the feel.52 Grip: lines (soaks 33 and 34) ----
 * A target `share` of the arm's length from the shoulder, in direction (yaw, pitch), turned `turn` radians about Z,
 * and its distance from the drawn hand. */
static TfpGraftMatrix logged_target(double share, double yaw, double pitch, double turn, double *from_hand) {
  double p[3], r[3][3];
  const double z[3] = {0, 0, 1};
  around((UPPER + LOWER) * share, yaw, pitch, p);
  rotation(z, turn, r);
  const World b = world(&(Arm){model().upper, model().fore, model().hand});
  if (from_hand) *from_hand = gap(p, b.h.p);
  return target_at(p, r);
}
/* Held at 1.05 and 1.149 of the arm's length (the logged 0.043 m median and 0.106 m most while held are
 * (share - 0.97) x 0.5899 m: the 97 % straightening limit, nothing else): the hand is on the grip, the shoulder
 * having come forward along the line to it by what the arm lacks, the bone lengths kept. */
static void test_logged_reach(void) {
  static const double shares[2] = {1.05, 1.149};
  for (unsigned k = 0; k < 2; k++) {
    double from_hand;
    const TfpGraftMatrix t = logged_target(shares[k], .25, -.15, .3, &from_hand);
    CHECK(from_hand < TFP_OG_SHIFT_M);
    TfpOffhand s;
    tfp_offhand_reset(&s);
    Arm a;
    const TfpOffhandResult res = run(&s, &a, &t, true, 1 / 30.0, 60);
    const World w = world(&a);
    const double p[3] = {t.values[12], t.values[13], t.values[14]};
    printf("offhand grip: a grip %.3f of the arm's length from the shoulder: the hand %.4f m from it, the shoulder %.3f m forward\n", shares[k],
           res.to_target_m, gap(w.u.p, SHOULDER));
    CHECK(res.reason == TFP_OG_HELD && res.to_target_m < 1e-4 && gap(w.h.p, p) < 1e-4);
    /* the shoulder moved along the line to the grip, by the arm's lack only */
    const double lack = (UPPER + LOWER) * (shares[k] - TFP_OG_STRETCH);
    CHECK(fabs(gap(w.u.p, SHOULDER) - lack) < 1e-4 && fabs(res.shoulder_m - lack) < 1e-4);
    CHECK(fabs(gap(p, w.u.p) + gap(w.u.p, SHOULDER) - gap(p, SHOULDER)) < 1e-5);
    CHECK(fabs(gap(w.f.p, w.u.p) - UPPER) < 1e-5 && fabs(gap(w.h.p, w.f.p) - LOWER) < 1e-5);
    CHECK(fabs(res.reach_share - shares[k]) < 1e-4);
  }
}
/* The grip swaying across the reach limit (1.14 to 1.16 of the arm's length, the logged out-of-reach flicker): once
 * held it stays held, on the grip. */
static void test_logged_reach_edge(void) {
  TfpOffhand s;
  tfp_offhand_reset(&s);
  const Arm base = model();
  Arm a;
  const TfpGraftMatrix start = logged_target(1.149, .25, -.15, .3, NULL);
  CHECK(run(&s, &a, &start, true, 1 / 30.0, 30).reason == TFP_OG_HELD);
  for (int i = 0; i < 120; i++) {
    const TfpGraftMatrix t = logged_target(1.15 + .01 * sin(i * .7), .25, -.15, .3, NULL);
    a = base;
    const TfpOffhandResult res = tfp_offhand_step(&s, &a.upper, &a.fore, &a.hand, &t, true, 1 / 30.0);
    CHECK(res.reason == TFP_OG_HELD && res.to_target_m < .01);
  }
}
/* Easing in from the body's pose 0.39 m and 17 degrees off the grip (the feel.51/52 measurement), out again when not
 * wanted, and out when the target goes (a reload) at 30 frames a second: no joint step reaches the allowance. */
static void test_logged_ease(void) {
  double from_hand = 0, yaw = 0;
  for (double y = -1.5; y < 1.5; y += .01) { /* the direction (at a level pitch) that puts the grip 0.39 m from the drawn hand */
    double m;
    logged_target(.80, y, -.15, .3, &m);
    if (fabs(m - .39) < fabs(from_hand - .39)) from_hand = m, yaw = y;
  }
  const TfpGraftMatrix t = logged_target(.80, yaw, -.15, .3, &from_hand);
  printf("offhand grip: the eased-in grip is %.3f m from the drawn hand\n", from_hand);
  CHECK(from_hand > .3 && from_hand < TFP_OG_SHIFT_M);
  for (int way = 0; way < 2; way++) {
    TfpOffhand s;
    tfp_offhand_reset(&s);
    const Arm base = model();
    Arm a, prev = base;
    double most = 0;
    int frames = 0;
    for (int i = 0; i < 90; i++) {
      const bool in = i < 45;
      a = base;
      const TfpOffhandResult res = tfp_offhand_step(&s, &a.upper, &a.fore, &a.hand, in || way == 0 ? &t : NULL, in, 1 / 30.0);
      const double step = fmax(degrees(&a.upper, &prev.upper), fmax(degrees(&a.fore, &prev.fore), degrees(&a.hand, &prev.hand)));
      CHECK(step <= .5 * res.limit_degrees + 1e-3 && res.step_degrees <= .5 * res.limit_degrees + 1e-3); /* half the allowance: 12 degrees */
      if (step > most) most = step;
      if (in && res.reason == TFP_OG_HELD && !frames) frames = i + 1;
      prev = a;
    }
    CHECK(frames > 4 && frames < 30 && !s.active && !memcmp(&a, &base, sizeof a));
    CHECK(most > 11.9 && most < 12.1); /* the measured 12.0: the ease is held back by the budget, not under it by chance */
    printf("offhand grip: eased %s at 33.3 ms frames: on the grip after %d frames, largest joint step %.1f degrees (allowed %.1f)\n",
           way ? "in, then out as the target goes" : "in and out", frames, most, TFP_OG_TURN_DPS / 30);
  }
}
/* The shoulder's top speed: held at 0.9 of the arm (no shoulder move), the grip jumps to 1.149 (a vertical grip, the
 * logged most): the shoulder comes forward by at most 0.6 m/s (0.02 m a 33.3 ms frame) and gets there, the 0.106 m
 * in six frames; then the hand is on the grip. The grip jumping back, the shoulder goes back as fast and no faster. */
static void test_shoulder_speed(void) {
  TfpOffhand s;
  tfp_offhand_reset(&s);
  Arm a;
  const TfpGraftMatrix near = logged_target(.90, .25, -.15, .3, NULL), far = logged_target(1.149, .25, -.15, .3, NULL);
  CHECK(run(&s, &a, &near, true, 1 / 30.0, 30).reason == TFP_OG_HELD && s.shift[0] == 0 && s.shift[1] == 0 && s.shift[2] == 0);
  const double lack = (UPPER + LOWER) * (1.149 - TFP_OG_STRETCH);
  for (int way = 0; way < 2; way++) {
    double last = way ? lack : 0, most = 0;
    int there = 0;
    for (int i = 0; i < 30; i++) {
      const TfpOffhandResult res = run(&s, &a, way ? &near : &far, true, 1 / 30.0, 1);
      const double moved = fabs(res.shoulder_m - last);
      CHECK(moved <= .02 + 1e-6);
      if (moved > most) most = moved;
      last = res.shoulder_m;
      if (!there && fabs(res.shoulder_m - (way ? 0 : lack)) < 1e-5) there = i + 1;
    }
    CHECK(there >= 6 && there <= 8 && most > .0199);
    CHECK(run(&s, &a, way ? &near : &far, true, 1 / 30.0, 1).reason == TFP_OG_HELD);
    printf("offhand grip: the grip jumping %s: the shoulder moves at most %.4f m a 33.3 ms frame, %s after %d frames\n",
           way ? "back to 0.9 of the arm" : "from 0.9 to 1.149 of the arm", most, way ? "back" : "0.106 m forward", there);
  }
}
/* Held while the body's arm jitters 20 degrees a frame, between 10 and 30 degrees from its pose, at 8.3 ms frames (a
 * body whose left arm follows the grip as the view turns): the drawn hand stays on the grip, the bones point where the
 * reach puts them, and the twist about
 * each follows the body's at 90 degrees a second at most, so a written joint turns by under a degree a frame (the
 * allowance is 6). When the body's arm is still again, the arm comes back to the reach of a hold that never saw the
 * jitter: the twist is the body's, nothing of the jitter is kept. */
static void test_held_body_moves(void) {
  TfpOffhand s, calm;
  tfp_offhand_reset(&s), tfp_offhand_reset(&calm);
  const TfpGraftMatrix t = logged_target(.80, .25, -.15, .3, NULL);
  const double p[3] = {t.values[12], t.values[13], t.values[14]};
  Arm a, prev, still;
  run(&s, &a, &t, true, 1 / 120.0, 120);
  run(&calm, &still, &t, true, 1 / 120.0, 240);
  prev = a;
  double most = 0, body = 0;
  for (int i = 0; i < 60; i++) {
    Arm moved = model();
    double r[3][3], u[3][3], turned[3][3];
    const double axis[3] = {.3, -.2, 1};
    rotation(axis, (i % 2 ? 30 : 10) * TFP_OG_PI / 180, r);
    const TfpOgFrame f = tfp_og_frame(&moved.upper);
    memcpy(u, f.r, sizeof u);
    tfp_og_rmul(u, r, turned);
    moved.upper = matrix(turned, f.p);
    a = moved;
    const TfpOffhandResult res = tfp_offhand_step(&s, &a.upper, &a.fore, &a.hand, &t, true, 1 / 120.0);
    const World w = world(&a);
    CHECK(res.reason == TFP_OG_HELD && gap(w.h.p, p) < 1e-4);
    const double step = fmax(degrees(&a.upper, &prev.upper), fmax(degrees(&a.fore, &prev.fore), degrees(&a.hand, &prev.hand)));
    CHECK(step < 1.0 && fabs(res.step_degrees - step) < 1e-3);
    if (i) CHECK(res.body_degrees > 19.9 && res.body_degrees < 20.1);
    if (step > most) most = step;
    if (res.body_degrees > body) body = res.body_degrees;
    prev = a;
  }
  /* still again: back on the calm hold's arm within a quarter of a second (each joint's twist under 20 degrees away) */
  int back = 0;
  for (int i = 0; i < 120; i++) {
    a = model();
    const TfpOffhandResult res = tfp_offhand_step(&s, &a.upper, &a.fore, &a.hand, &t, true, 1 / 120.0);
    CHECK(res.reason == TFP_OG_HELD && res.to_target_m < 1e-4);
    const double off = fmax(degrees(&a.upper, &still.upper), fmax(degrees(&a.fore, &still.fore), degrees(&a.hand, &still.hand)));
    if (off < 1e-3 && !back) back = i + 1;
  }
  CHECK(back > 0 && back <= 30);
  printf("offhand grip: held while the body's arm swings %.1f degrees a frame: largest written joint step %.2f degrees (allowed %.1f); "
         "still again, on the calm hold's arm after %d frames\n", body, most, TFP_OG_TURN_DPS / 120, back);
}
/* A long hold (review of round 26): the grip circles a small loop against the shoulder, once a second at 30 frames a
 * second (walking sway, breathing, the view turning), for thousands of loops while held. Each written joint's turn
 * against the body's own rotation for it stays where it was after the first loop: the twist about each bone is
 * anchored to the body's, not carried over from frame to frame (where every loop leaves a little twist behind, the
 * elbow and wrist wind up while the hand stays on the grip). Loops of 1 and 3 cm, at 0.8 and 1.05 of the arm. */
static void test_long_hold(void) {
  static const double loops[2] = {.01, .03}, shares[2] = {.80, 1.05};
  const int per = 30, cycles = 3000;
  for (unsigned k = 0; k < 4; k++) {
    const double amp = loops[k & 1], share = shares[k >> 1];
    double c[3], r[3][3];
    const double z[3] = {0, 0, 1};
    around((UPPER + LOWER) * share, .25, -.15, c);
    rotation(z, .3, r);
    TfpOffhand s;
    tfp_offhand_reset(&s);
    const Arm base = model();
    Arm a = base;
    double first[3] = {0, 0, 0}, drift = 0, off = 0;
    for (int cyc = 0; cyc <= cycles; cyc++) {
      for (int i = 0; i < per; i++) {
        const double ph = 2 * TFP_OG_PI * i / per, p[3] = {c[0] + amp * cos(ph), c[1], c[2] + amp * sin(ph)};
        const TfpGraftMatrix t = target_at(p, r);
        a = base;
        const TfpOffhandResult res = tfp_offhand_step(&s, &a.upper, &a.fore, &a.hand, &t, true, 1 / 30.0);
        if (cyc >= 1) {
          CHECK(res.reason == TFP_OG_HELD);
          if (res.to_target_m > off) off = res.to_target_m;
        }
      }
      const double now[3] = {degrees(&a.upper, &base.upper), degrees(&a.fore, &base.fore), degrees(&a.hand, &base.hand)};
      if (cyc == 1) memcpy(first, now, sizeof first);
      else if (cyc > 1)
        for (unsigned j = 0; j < 3; j++) drift = fmax(drift, fabs(now[j] - first[j]));
    }
    printf("offhand grip: held %d loops of %.0f cm at %.2f of the arm: the joints against the body's %.2f / %.2f / %.2f degrees after the first, "
           "drifted at most %.3f degrees; the hand at most %.5f m from the grip\n",
           cycles, amp * 100, share, first[0], first[1], first[2], drift, off);
    CHECK(drift < .5 && off < 1e-4);
  }
}

/* ---- the hand closed on an attachment's grip (src/grip_fingers.h; round 36b, B of the Adjudicator diagnosis) ----
 * A map like the drive's (joint i is rig node 10 + i), the rig's finger names and parents as fp_cha_avatar has them, and
 * finger locals that are nearly flat: each turned a little about its own +Y, with some splay about Z. */
static const uint32_t FINGER_NAMES[15] = {0x7a19e054, 0xd5371e86, 0x30545cb9, 0x7881f640, 0x2db8f964, 0xa2917eea, 0xe4767875, 0xda6cc994,
                                          0x83e16077, 0xc7d1ff28, 0xb15bab98, 0x6cd7940d, 0xb7d89ff0, 0xc2235d63, 0xdf1f5d92};
static void finger_map(TfpGraft *g, uint32_t fn[TFP_GRAFT_RIG_NODES]) {
  memset(g, 0, sizeof *g), memset(fn, 0, TFP_GRAFT_RIG_NODES * 4);
  for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) g->fp[i] = i < 40 ? 10 + i : i == 40 ? 58 : 8, g->parent[i] = 7;
  g->parent[2] = 10, g->parent[3] = 12; /* l_elbow, l_hand */
  for (unsigned f = 0; f < 5; f++)
    for (unsigned k = 0; k < 3; k++) {
      const unsigned node = 14 + 3 * f + k;
      fn[node] = FINGER_NAMES[3 * f + k], g->parent[node - 10] = k ? node - 1 : 13;
    }
}
static TfpGraftLocal finger_local(double curl_degrees, double splay_degrees, unsigned n) {
  double r[3][3];
  const double z[3] = {0, 0, 1};
  rotation(z, splay_degrees * TFP_OG_PI / 180, r);
  tfp_og_turn_rows(r, r[1], curl_degrees * TFP_OG_PI / 180);
  const double p[3] = {.03 + .001 * n, .002 * n, -.001 * n};
  TfpGraftLocal l = {.preserved = {1, 1, 1, (float)n}};
  const TfpGraftMatrix m = matrix(r, p);
  tfp_graft_set_local(&l, &m);
  return l;
}
static double local_curl(const TfpGraftLocal *l) { return tfp_gf_curl(l) * 180 / TFP_OG_PI; }
static void test_fingers(void) {
  TfpGraft g;
  uint32_t fn[TFP_GRAFT_RIG_NODES];
  TfpGripFingers fingers;
  finger_map(&g, fn);
  CHECK(tfp_grip_fingers_find(&g, fn, &fingers));
  for (unsigned f = 0; f < TFP_GF_FINGERS; f++)
    for (unsigned k = 0; k < 3; k++) CHECK(g.fp[fingers.joint[f][k]] == 14 + 3 * f + k);
  CHECK(g.fp[fingers.joint[TFP_GF_MIDDLE][0]] == 20 && g.fp[fingers.joint[TFP_GF_INDEX][1]] == 24 && g.fp[fingers.joint[TFP_GF_THUMB][0]] == 26);
  /* a name missing or a finger hung on another bone: no fingers (nothing is closed) */
  fn[21] ^= 1;
  CHECK(!tfp_grip_fingers_find(&g, fn, &fingers) && !fingers.ok);
  fn[21] ^= 1, g.parent[23 - 10] = 12;
  CHECK(!tfp_grip_fingers_find(&g, fn, &fingers));
  g.parent[23 - 10] = 13;
  CHECK(tfp_grip_fingers_find(&g, fn, &fingers));
  TfpGraftLocal open[TFP_GRAFT_JOINTS], l[TFP_GRAFT_JOINTS];
  for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) open[i] = finger_local(i == 18 - 10 ? 85 : 10, i % 3 ? 0 : 12, i);
  for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) CHECK(fabs(local_curl(&open[i]) - (i == 18 - 10 ? 85 : 10)) < 1e-3);
  /* weight 0: nothing; weight 1: each finger joint at its finger's grip curl, also one the body curls further (ring
   * joint 2, at 85: it would go into the handle); thumb joint 1 the body's, joints 2 and 3 the body's plus the turn; only
   * rotations about each bone's own +Y; positions and the rest of each record kept */
  memcpy(l, open, sizeof l);
  tfp_grip_fingers_close(&fingers, l, 0, TFP_GF_THUMB_WRAPPED);
  CHECK(!memcmp(l, open, sizeof l));
  for (unsigned half = 0; half < 2; half++) {
    const double w = half ? .5 : 1;
    memcpy(l, open, sizeof l);
    tfp_grip_fingers_close(&fingers, l, w, TFP_GF_THUMB_WRAPPED);
    for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) {
      const unsigned node = g.fp[i];
      const bool finger = node >= 14 && node <= 25, thumb = node >= 27 && node <= 28;
      CHECK(!memcmp(l[i].position, open[i].position, 12) && !memcmp(l[i].preserved, open[i].preserved, 16));
      if (!finger && !thumb) { CHECK(!memcmp(&l[i], &open[i], 64)); continue; } /* thumb joint 1 too */
      const double was = local_curl(&open[i]);
      const double want = finger ? was + (tfp_gf_curl_degrees[(node - 14) / 3] - was) * w : was + TFP_GF_THUMB_TURN_DEGREES * w;
      CHECK(fabs(local_curl(&l[i]) - want) < 1e-3);
      const TfpGraftMatrix a = tfp_graft_local_matrix(&l[i]), b = tfp_graft_local_matrix(&open[i]);
      for (unsigned j = 0; j < 3; j++) CHECK(fabsf(a.values[4 + j] - b.values[4 + j]) < 1e-6f); /* the bone's +Y: the axis it turned about */
      CHECK(tfp_graft_rigid(&a));
    }
  }
  CHECK(local_curl(&l[18 - 10]) < 85); /* opened towards the grip's */
  /* no fingers found: nothing */
  TfpGripFingers none = {0};
  memcpy(l, open, sizeof l);
  tfp_grip_fingers_close(&none, l, 1, TFP_GF_THUMB_WRAPPED);
  CHECK(!memcmp(l, open, sizeof l));
  /* the drawn curl: the angle between the X axes of two world matrices */
  double r[3][3];
  const double y[3] = {0, 1, 0}, at[3] = {0, 0, 0};
  rotation(y, 65 * TFP_OG_PI / 180, r);
  const TfpGraftMatrix id = matrix((double[3][3]){{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, at), turned = matrix(r, at);
  CHECK(fabs(tfp_gf_x_degrees(&id, &turned) - 65) < 1e-3);
  printf("fingers: pinky, ring, middle, index at %.0f, %.0f, %.0f, %.0f degrees a joint about each bone's own +Y (thumb joints 2 and 3 "
         "+%.0f, joint 1 the body's); half way at weight 0.5; positions kept\n",
         tfp_gf_curl_degrees[0], tfp_gf_curl_degrees[1], tfp_gf_curl_degrees[2], tfp_gf_curl_degrees[3], TFP_GF_THUMB_TURN_DEGREES);
}

/* Round 36b (the independent check of the grip): the fingers on fp_cha_avatar's own bind pose, the hand on the standard
 * vertical grip's node (the reach gives the wrist the node's place and turn), in the hand's frame. The handle is a cylinder
 * 4.6 cm across along the node's Y; a fingertip is the third joint plus 2.5 cm along its X (the checker's measure). */
static void bind_locals(TfpGraft *g, uint32_t fn[TFP_GRAFT_RIG_NODES], TfpGraftLocal locals[TFP_GRAFT_JOINTS]) {
  memset(g, 0, sizeof *g), memset(fn, 0, TFP_GRAFT_RIG_NODES * 4), memset(locals, 0, TFP_GRAFT_JOINTS * sizeof *locals);
  for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) g->fp[i] = i < 40 ? 10 + i : i == 40 ? 58 : 8, g->parent[i] = 7;
  g->parent[2] = 10, g->parent[3] = 12;
  for (unsigned n = FP_HAND_FIRST; n <= FP_HAND_LAST; n++) {
    fn[n] = fp_hand_name[n - FP_HAND_FIRST], g->parent[n - 10] = fp_hand_parent[n - FP_HAND_FIRST];
    if (n == FP_HAND_FIRST) continue;
    TfpGraftMatrix w, p;
    memcpy(w.values, fp_hand_world[n - FP_HAND_FIRST], 64);
    memcpy(p.values, fp_hand_world[fp_hand_parent[n - FP_HAND_FIRST] - FP_HAND_FIRST], 64);
    const TfpGraftMatrix l = tfp_graft_relative(&w, &p);
    tfp_graft_set_local(&locals[n - 10], &l);
  }
}
/* nodes 13 to 28 in the hand's frame (the hand the identity) */
static void bind_hand_frame(const TfpGraftLocal locals[TFP_GRAFT_JOINTS], TfpGraftMatrix w[FP_HAND_LAST + 1]) {
  w[FP_HAND_FIRST] = (TfpGraftMatrix){.values = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  for (unsigned n = FP_HAND_FIRST + 1; n <= FP_HAND_LAST; n++) {
    const TfpGraftMatrix l = tfp_graft_local_matrix(&locals[n - 10]);
    w[n] = tfp_graft_product(&l, &w[fp_hand_parent[n - FP_HAND_FIRST]]);
  }
}
static void bind_tip(const TfpGraftMatrix *third, double out[3]) {
  for (unsigned j = 0; j < 3; j++) out[j] = third->values[12 + j] + .025 * third->values[j];
}
/* the handle's axis in the hand's frame: a point (the box's centre) and its direction; half its length */
static void vgrip_axis(double at[3], double dir[3], double *half) {
  double ax[3][3], c[3];
  for (unsigned i = 0; i < 3; i++) {
    const double n = tfp_og_len(vgrip_node_axes[i]);
    for (unsigned j = 0; j < 3; j++) ax[i][j] = vgrip_node_axes[i][j] / n;
  }
  for (unsigned j = 0; j < 3; j++) c[j] = (vgrip_handle_min[j] + vgrip_handle_max[j]) / 2 - vgrip_node_origin[j];
  for (unsigned i = 0; i < 3; i++) at[i] = tfp_og_dot(c, ax[i]), dir[i] = ax[i][2]; /* the gun's Z in the node's axes */
  const double n = tfp_og_len(dir);
  for (unsigned i = 0; i < 3; i++) dir[i] /= n;
  *half = (vgrip_handle_max[2] - vgrip_handle_min[2]) / 2;
}
static double vgrip_dist(const double p[3], double *along) {
  double at[3], dir[3], half, q[3];
  vgrip_axis(at, dir, &half);
  for (unsigned j = 0; j < 3; j++) q[j] = p[j] - at[j];
  const double t = tfp_og_dot(q, dir);
  if (along) *along = t;
  for (unsigned j = 0; j < 3; j++) q[j] -= t * dir[j];
  return tfp_og_len(q);
}
static void test_fingers_on_bind(void) {
  TfpGraft g;
  uint32_t fn[TFP_GRAFT_RIG_NODES];
  TfpGraftLocal bind[TFP_GRAFT_JOINTS];
  TfpGripFingers fingers;
  bind_locals(&g, fn, bind);
  CHECK(tfp_grip_fingers_find(&g, fn, &fingers));
  double at[3], dir[3], half;
  vgrip_axis(at, dir, &half);
  CHECK(fabs(dir[1]) > .99 && fabs(vgrip_handle_max[0] - vgrip_handle_min[0] - .046) < 1e-9); /* along the hand's Y; 4.6 cm */
  const unsigned thumb[3] = {26, 27, 28};
  double tip_least = 1, tip_most = 0, bone_least = 1, thumb_y[3];
  static const double weights[3] = {0, .5, 1};
  for (unsigned wi = 0; wi < 3; wi++) {
    TfpGraftLocal l[TFP_GRAFT_JOINTS];
    TfpGraftMatrix w[FP_HAND_LAST + 1];
    memcpy(l, bind, sizeof l);
    tfp_grip_fingers_close(&fingers, l, weights[wi], TFP_GF_THUMB_WRAPPED);
    bind_hand_frame(l, w);
    /* the thumb: joint 1 the body's; its tip on the thumb's side of the palm (beyond the index knuckle, away from the
     * pinky), never inside the handle, and not swept across the hand (bind: tip at y -0.086, index knuckle -0.033) */
    CHECK(!memcmp(&l[thumb[0] - 10], &bind[thumb[0] - 10], sizeof l[0]));
    double t[3];
    bind_tip(&w[thumb[2]], t);
    thumb_y[wi] = t[1];
    CHECK(t[1] < w[23].values[13] - .01);
    CHECK(vgrip_dist(t, NULL) >= .020);
    for (unsigned k = 1; k < 3; k++) /* joints 2 and 3: the body's own curl and a share of the turn */
      CHECK(fabs((tfp_gf_curl(&l[thumb[k] - 10]) - tfp_gf_curl(&bind[thumb[k] - 10])) * 180 / TFP_OG_PI - TFP_GF_THUMB_TURN_DEGREES * weights[wi]) < 1e-3);
    /* the four fingers: closed, the tips round the handle's surface, no bone through it */
    for (unsigned f = TFP_GF_PINKY; f <= TFP_GF_INDEX; f++) {
      const unsigned n1 = 14 + 3 * f;
      double p[4][3], along;
      for (unsigned k = 0; k < 3; k++)
        for (unsigned j = 0; j < 3; j++) p[k][j] = w[n1 + k].values[12 + j];
      bind_tip(&w[n1 + 2], p[3]);
      for (unsigned s = 0; s < 3; s++)
        for (unsigned u = 0; u <= 10; u++) {
          double q[3];
          for (unsigned j = 0; j < 3; j++) q[j] = p[s][j] + (p[s + 1][j] - p[s][j]) * u / 10;
          const double d = vgrip_dist(q, NULL);
          CHECK(d >= .020);
          if (weights[wi] == 1) bone_least = fmin(bone_least, d);
        }
      const double tip = vgrip_dist(p[3], &along);
      if (weights[wi] == 1) {
        CHECK(tip >= .020 && tip <= .035 && fabs(along) < half);
        tip_least = fmin(tip_least, tip), tip_most = fmax(tip_most, tip);
      }
    }
  }
  CHECK(fabs(thumb_y[0] + .086) < .001); /* the checker's bind thumb tip */
  printf("fingers on fp_cha_avatar's bind pose, on the vertical grip's node: tips %.1f to %.1f cm from the handle's axis "
         "(radius 2.3), no bone nearer than %.1f cm; thumb tip y %.3f / %.3f / %.3f m at weight 0 / 0.5 / 1 (joint 1 the body's)\n",
         tip_least * 100, tip_most * 100, bone_least * 100, thumb_y[0], thumb_y[1], thumb_y[2]);
}

/* Round 42b: the straight thumb (the option "Foregrip thumb: Straight") on the same bind pose and handle. */
static double degrees_between(const double a[3], const double b[3]) {
  double c[3];
  tfp_og_cross(a, b, c);
  return atan2(tfp_og_len(c), tfp_og_dot(a, b)) * 180 / TFP_OG_PI;
}
typedef struct { double tip[3], tip_m, tip_along, chord, last, least_m; bool outside; } Thumb;
/* the thumb of locals `l` against the handle: its tip, the angle of joint 1 to the tip and of the last bone from the
 * handle's axis (up, towards the gun), and whether every point of its bones is outside the handle (4.6 cm across) */
static Thumb thumb_of(const TfpGraftLocal l[TFP_GRAFT_JOINTS]) {
  TfpGraftMatrix w[FP_HAND_LAST + 1];
  double at[3], up[3], half, p[4][3], chord[3];
  Thumb t = {.least_m = 1, .outside = true};
  bind_hand_frame(l, w);
  vgrip_axis(at, up, &half);
  for (unsigned k = 0; k < 3; k++)
    for (unsigned j = 0; j < 3; j++) p[k][j] = w[26 + k].values[12 + j];
  bind_tip(&w[28], p[3]);
  memcpy(t.tip, p[3], sizeof t.tip);
  t.tip_m = vgrip_dist(p[3], &t.tip_along);
  for (unsigned s = 0; s < 3; s++)
    for (unsigned u = 0; u <= 10; u++) {
      double q[3], along;
      for (unsigned j = 0; j < 3; j++) q[j] = p[s][j] + (p[s + 1][j] - p[s][j]) * u / 10;
      const double d = vgrip_dist(q, &along);
      if (fabs(along) <= half) t.least_m = fmin(t.least_m, d), t.outside = t.outside && d >= .023; /* beside the handle, not above its top */
    }
  tfp_og_sub(p[3], p[0], chord);
  const double x[3] = {w[28].values[0], w[28].values[1], w[28].values[2]};
  t.chord = degrees_between(chord, up), t.last = degrees_between(x, up);
  return t;
}
static void test_thumb_straight_on_bind(void) {
  TfpGraft g;
  uint32_t fn[TFP_GRAFT_RIG_NODES];
  TfpGraftLocal bind[TFP_GRAFT_JOINTS], l[TFP_GRAFT_JOINTS], wrapped[TFP_GRAFT_JOINTS];
  TfpGraftMatrix w[FP_HAND_LAST + 1];
  TfpGripFingers fingers;
  bind_locals(&g, fn, bind);
  CHECK(tfp_grip_fingers_find(&g, fn, &fingers));
  { /* the constants are the measured handle: its axis in the hand's frame, up towards the gun */
    double at[3], up[3], half, off[3];
    vgrip_axis(at, up, &half);
    tfp_og_sub(tfp_gf_handle_at, at, off);
    const double along = tfp_og_dot(off, up);
    for (unsigned j = 0; j < 3; j++) off[j] -= along * up[j];
    CHECK(tfp_og_len(off) < .002 && degrees_between(tfp_gf_handle_up, up) < .5 && fabs(tfp_og_len(tfp_gf_handle_up) - 1) < 1e-4);
  }
  bind_hand_frame(bind, w);
  const double knuckle_y = w[23].values[13], axis_x = tfp_gf_handle_at[0];
  const Thumb body = thumb_of(bind);
  CHECK(body.chord > 45 && body.chord < 52 && body.tip_m < .026); /* the body's own: across the handle's top */
  /* weight 0: nothing at all */
  memcpy(l, bind, sizeof l);
  tfp_grip_fingers_close(&fingers, l, 0, TFP_GF_THUMB_STRAIGHT);
  CHECK(!memcmp(l, bind, sizeof l));
  double last_chord = body.chord;
  Thumb full = body;
  for (unsigned step = 1; step <= 4; step++) {
    const double weight = step / 4.;
    memcpy(l, bind, sizeof l), memcpy(wrapped, bind, sizeof wrapped);
    tfp_grip_fingers_close(&fingers, l, weight, TFP_GF_THUMB_STRAIGHT);
    tfp_grip_fingers_close(&fingers, wrapped, weight, TFP_GF_THUMB_WRAPPED);
    /* joints 2 and 3 the body's own, bit for bit; joint 1 turned only (its place and the record's other words kept);
     * the four fingers as with the wrapped thumb */
    CHECK(!memcmp(&l[27 - 10], &bind[27 - 10], 64) && !memcmp(&l[28 - 10], &bind[28 - 10], 64));
    CHECK(memcmp(l[26 - 10].basis, bind[26 - 10].basis, 36) && !memcmp(l[26 - 10].position, bind[26 - 10].position, 12) &&
          !memcmp(l[26 - 10].preserved, bind[26 - 10].preserved, 16));
    const TfpGraftMatrix one = tfp_graft_local_matrix(&l[26 - 10]);
    CHECK(tfp_graft_rigid(&one));
    for (unsigned n = 14; n <= 25; n++) CHECK(!memcmp(&l[n - 10], &wrapped[n - 10], 64));
    for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) if (g.fp[i] < 14 || g.fp[i] > 28) CHECK(!memcmp(&l[i], &bind[i], 64));
    /* on its way: never through the handle, the tip on the thumb's side of the hand (beyond the index knuckle, away from
     * the pinky) and on the near side of the handle's axis; it rises steadily */
    const Thumb t = thumb_of(l);
    CHECK(t.outside && t.tip[1] < knuckle_y - .01 && t.tip[0] < axis_x && t.chord < last_chord);
    last_chord = t.chord, full = t;
  }
  /* there: joint 1 to the tip up along the handle's axis with its lean, the last bone within 25 degrees of it too, the
   * tip well outside the handle (4.6 cm across: 2.3 cm from the axis) and above the hand */
  CHECK(fabs(full.chord - TFP_GF_THUMB_LEAN_DEGREES) < .5 && full.last <= 25);
  CHECK(full.tip_m >= .035 && full.tip_m <= .055 && full.tip_along > body.tip_along && full.tip[1] < knuckle_y - .05);
  /* a body that holds the thumb otherwise (joint 2 curled 30 degrees further, joint 1 swung 25 degrees about the hand's
   * Z): the same end, joints 2 and 3 still its own */
  TfpGraftLocal other[TFP_GRAFT_JOINTS];
  memcpy(other, bind, sizeof other);
  {
    TfpGraftMatrix m = tfp_graft_local_matrix(&other[27 - 10]);
    TfpOgFrame fr = tfp_og_frame(&m);
    double axis[3];
    memcpy(axis, fr.r[1], sizeof axis);
    tfp_og_turn_rows(fr.r, axis, 30 * TFP_OG_PI / 180);
    for (unsigned i = 0; i < 3; i++) for (unsigned j = 0; j < 3; j++) other[27 - 10].basis[3 * i + j] = (float)fr.r[i][j];
    m = tfp_graft_local_matrix(&other[26 - 10]), fr = tfp_og_frame(&m);
    const double z[3] = {0, 0, 1};
    tfp_og_turn_rows(fr.r, z, 25 * TFP_OG_PI / 180);
    for (unsigned i = 0; i < 3; i++) for (unsigned j = 0; j < 3; j++) other[26 - 10].basis[3 * i + j] = (float)fr.r[i][j];
  }
  memcpy(l, other, sizeof l);
  tfp_grip_fingers_close(&fingers, l, 1, TFP_GF_THUMB_STRAIGHT);
  const Thumb o = thumb_of(l);
  CHECK(!memcmp(&l[27 - 10], &other[27 - 10], 64) && !memcmp(&l[28 - 10], &other[28 - 10], 64));
  CHECK(fabs(o.chord - TFP_GF_THUMB_LEAN_DEGREES) < .5 && o.outside && o.tip_m >= .035 && o.tip[1] < knuckle_y - .05 && o.tip[0] < axis_x);
  /* again on the result: already there, nothing more than rounding */
  memcpy(wrapped, l, sizeof l);
  tfp_grip_fingers_close(&fingers, l, 1, TFP_GF_THUMB_STRAIGHT);
  for (unsigned j = 0; j < 9; j++) CHECK(fabsf(l[26 - 10].basis[j] - wrapped[26 - 10].basis[j]) < 1e-5f);
  printf("straight thumb on fp_cha_avatar's bind pose, on the vertical grip's node: joint 1 to the tip %.1f degrees from the handle's axis "
         "(the body's %.1f), the last bone %.1f; the tip %.1f cm from the axis (radius 2.3; the body's %.1f), y %.3f m; joints 2 and 3 the body's\n",
         full.chord, body.chord, full.last, full.tip_m * 100, body.tip_m * 100, full.tip[1]);
}

int main(void) {
  test_fingers();
  test_fingers_on_bind();
  test_thumb_straight_on_bind();
  test_long_hold();
  test_logged_reach();
  test_logged_reach_edge();
  test_logged_ease();
  test_held_body_moves();
  test_shoulder_speed();
  test_reach();
  test_stretch();
  test_refused();
  test_too_far_keep();
  test_release();
  test_step_limit();
  test_behind();
  test_short_way();
  printf("offhand grip: %u checks passed\n", checks);
  return 0;
}
