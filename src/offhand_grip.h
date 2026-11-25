/* The support hand on the weapon's grip (round 23, 2026-11-24): a two-bone reach of the left arm of the first-person
 * rig, applied by the arm drive (src/arm_drive.c) to the 42 poses it is about to write. Pure maths: nothing here reads
 * the game or keeps a pointer. docs/design/specs/2026-11-24-offhand-grip.md has the why.
 *
 * Why it is needed. The game pins the third-person body's left hand on the weapon's left-hand grip node (name
 * 67C94E4B; on a foregrip or a vertical grip, the attachment's own node) in the world, also while the first-person rig
 * is the view and the gun hangs on the rig (retail grip_hand.lua's header, measured 2026-11-13/14; the retired mod's
 * capture of 120 frames has the body's hand rigidly 0.226 m from the rig's socket while the body sways). The arm drive
 * copies the body's arms onto the rig through one alignment, the one that puts the body's RIGHT hand's weapon socket on
 * the rig's socket. The body's left hand is not held by its right hand but by a gun that is somewhere else, so through
 * that alignment it lands 0.37 to 0.42 m and 12 to 19 degrees from the grip it holds (the same capture, every 15th
 * frame): by the receiver instead of under the barrel.
 *
 * What it does. In the frame of the rig's presentation root (node 7: X right, Y forward, Z up, the eye's axes), with
 * the drive's locals of the upper arm (node 10, a child of node 7), the forearm (12, of 10) and the hand (13, of 12):
 *   - the hand is to be at the target (the grip node as the drive draws the gun) with the target's turn, blended in by
 *     the weight (eased over EASE_S, a smoothstep);
 *   - the shoulder stays; the elbow is put in the plane of shoulder, target and a pole that points down and a little
 *     out (the rig's own left elbow hangs that way at the hip), kept from the previous frame's side where the target
 *     comes near the pole's line; the arm stretches to STRETCH of its length at most (the hand stops short of a grip
 *     further away);
 *   - the result is a correction of each of the three joints' rotations (the positions are the bone lengths and are
 *     never changed). A correction moves from the previous frame's by at most TURN_DPS a second and TURN_CAP in one
 *     frame, so no joint can turn over by a flip of the solve; going away, it returns to none at the same rate;
 *   - a target further than SHIFT_M from where the body's pose put the hand, or than REACH times the arm's length from
 *     the shoulder, is not taken: the arm eases back to the body's pose (the reason says which).
 * The fingers are the body's (the drive copies them relative to the hand), and the body's hand is the one the game
 * shapes around this grip. */
#ifndef TFP_OFFHAND_GRIP_H
#define TFP_OFFHAND_GRIP_H
#include "arm_graft.h"

/* Joints of the drive's map (src/arm_graft.h: joint i is rig node 10 + i for the arms). */
enum { TFP_OG_UPPER = 0, TFP_OG_FORE = 2, TFP_OG_HAND = 3 };
#define TFP_OG_EASE_S .15       /* weight 0 to 1 and back: the retail module's EASE */
#define TFP_OG_TURN_DPS 720.0   /* a joint's correction changes by at most this many degrees a second... */
#define TFP_OG_TURN_CAP 30.0    /* ...and never by more than this in one frame */
#define TFP_OG_STRETCH .97      /* the arm is never straighter than this share of its length */
#define TFP_OG_REACH 1.15       /* a target further from the shoulder than this share of the arm's length is out of reach */
#define TFP_OG_SHIFT_M .60      /* a target further than this from the hand the body's pose drew is not taken */
#define TFP_OG_PI 3.14159265358979323846

/* What a frame did (TfpOffhandResult.reason). */
enum {
  TFP_OG_IDLE = 0,     /* nothing wanted and nothing left to undo: the poses are the drive's, untouched */
  TFP_OG_HELD,         /* the hand is on the target at full weight */
  TFP_OG_EASING,       /* on its way to the target or back to the body's pose */
  TFP_OG_NO_TARGET,    /* wanted, but no target this frame: easing back */
  TFP_OG_OUT_OF_REACH, /* the target is beyond REACH of the arm's length from the shoulder */
  TFP_OG_TOO_FAR,      /* the target is more than SHIFT_M from the hand the body's pose drew */
  TFP_OG_NOT_RIGID,    /* a pose was not rigid or a bone has no length: nothing was changed this frame */
  TFP_OG_REASONS
};
typedef struct TfpOffhand {
  bool active;            /* a correction is in the poses (not all three are none) */
  double weight;          /* 0..1, before the smoothstep */
  double c[3][3][3];      /* the corrections of the upper arm, forearm and hand: local rotation = c x the drive's */
  bool has_side;
  double side[3];         /* the elbow's side of the last solve, for continuity */
} TfpOffhand;
typedef struct TfpOffhandResult {
  unsigned reason;        /* TFP_OG_* */
  float weight;           /* as used (after the smoothstep) */
  float to_target_m;      /* the hand as written from the target; 0 without a target */
  float shift_m;          /* the hand as written from where the drive's pose had it */
  float step_degrees;     /* the largest change of any of the three corrections from the previous frame */
  float limit_degrees;    /* what that change was allowed to be this frame */
} TfpOffhandResult;

typedef struct { double r[3][3], p[3]; } TfpOgFrame; /* rows are the axes; a point is (local row) x r + p */

static inline TfpOgFrame tfp_og_frame(const TfpGraftMatrix *m) {
  TfpOgFrame f;
  for (unsigned i = 0; i < 3; ++i) {
    for (unsigned j = 0; j < 3; ++j) f.r[i][j] = m->values[4 * i + j];
    f.p[i] = m->values[12 + i];
  }
  return f;
}
static inline void tfp_og_rmul(const double a[3][3], const double b[3][3], double out[3][3]) {
  double t[3][3];
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j) t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
  memcpy(out, t, sizeof t);
}
static inline void tfp_og_rmul_t(const double a[3][3], const double b[3][3], double out[3][3]) { /* a x b transposed */
  double t[3][3];
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j) t[i][j] = a[i][0] * b[j][0] + a[i][1] * b[j][1] + a[i][2] * b[j][2];
  memcpy(out, t, sizeof t);
}
/* a then b: a's local frame placed in b's */
static inline TfpOgFrame tfp_og_then(const TfpOgFrame *a, const TfpOgFrame *b) {
  TfpOgFrame out;
  tfp_og_rmul(a->r, b->r, out.r);
  for (unsigned j = 0; j < 3; ++j) out.p[j] = a->p[0] * b->r[0][j] + a->p[1] * b->r[1][j] + a->p[2] * b->r[2][j] + b->p[j];
  return out;
}
static inline double tfp_og_dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline double tfp_og_len(const double a[3]) { return sqrt(tfp_og_dot(a, a)); }
static inline void tfp_og_cross(const double a[3], const double b[3], double out[3]) {
  const double t[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  memcpy(out, t, sizeof t);
}
static inline void tfp_og_sub(const double a[3], const double b[3], double out[3]) { for (unsigned i = 0; i < 3; ++i) out[i] = a[i] - b[i]; }
/* Radians between two rotations: atan2 of the sine (the skew part of a b^T) and the cosine (its trace), so that a few
 * parts in a million of float rounding read as that, not as the tenths of a degree an arccosine near 1 makes of them. */
static inline double tfp_og_angle(const double a[3][3], const double b[3][3]) {
  double m[3][3];
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j) m[i][j] = tfp_og_dot(a[i], b[j]);
  const double v[3] = {m[1][2] - m[2][1], m[2][0] - m[0][2], m[0][1] - m[1][0]};
  return atan2(tfp_og_len(v) / 2, (m[0][0] + m[1][1] + m[2][2] - 1) / 2);
}
/* A rotation as a unit quaternion and back. The pair is all this file needs of a convention: of_q(q_of(m)) is m. */
static inline void tfp_og_q_of(const double m[3][3], double q[4]) {
  const double trace = m[0][0] + m[1][1] + m[2][2];
  if (trace > 0) {
    const double s = 2 * sqrt(1 + trace);
    q[3] = s / 4, q[0] = (m[1][2] - m[2][1]) / s, q[1] = (m[2][0] - m[0][2]) / s, q[2] = (m[0][1] - m[1][0]) / s;
  } else {
    int i = m[0][0] >= m[1][1] ? 0 : 1;
    if (m[2][2] > m[i][i]) i = 2;
    const int j = (i + 1) % 3, k = (i + 2) % 3;
    const double s = 2 * sqrt(1 + m[i][i] - m[j][j] - m[k][k]);
    q[i] = s / 4, q[j] = (m[i][j] + m[j][i]) / s, q[k] = (m[i][k] + m[k][i]) / s, q[3] = (m[j][k] - m[k][j]) / s;
  }
  const double n = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  for (unsigned i = 0; i < 4; ++i) q[i] /= n;
}
static inline void tfp_og_of_q(const double q[4], double m[3][3]) {
  const double x = q[0], y = q[1], z = q[2], w = q[3];
  m[0][0] = 1 - 2 * (y * y + z * z), m[0][1] = 2 * (x * y + w * z), m[0][2] = 2 * (x * z - w * y);
  m[1][0] = 2 * (x * y - w * z), m[1][1] = 1 - 2 * (x * x + z * z), m[1][2] = 2 * (y * z + w * x);
  m[2][0] = 2 * (x * z + w * y), m[2][1] = 2 * (y * z - w * x), m[2][2] = 1 - 2 * (x * x + y * y);
}
/* The share `f` of rotation `m`, the shorter way round (a quaternion and its negation are one rotation: the sign is
 * chosen, never inherited, so no blend can go the long way). */
static inline void tfp_og_part(const double m[3][3], double f, double out[3][3]) {
  double q[4];
  tfp_og_q_of(m, q);
  if (q[3] < 0) for (unsigned i = 0; i < 4; ++i) q[i] = -q[i];
  const double half = acos(q[3] > 1 ? 1 : q[3]), s = sin(half);
  if (s < 1e-12) { memset(out, 0, 9 * sizeof(double)), out[0][0] = out[1][1] = out[2][2] = 1; return; }
  const double turned = sin(f * half) / s;
  const double r[4] = {q[0] * turned, q[1] * turned, q[2] * turned, cos(f * half)};
  tfp_og_of_q(r, out);
}
/* from moved towards to by at most `limit` radians: (to from^T)^share x from. Returns the angle moved; *held: it was
 * cut short. */
static inline double tfp_og_towards(const double from[3][3], const double to[3][3], double limit, double out[3][3], bool *held) {
  double rel[3][3];
  const double angle = tfp_og_angle(to, from);
  *held = angle > limit;
  if (!*held) { memcpy(out, to, 9 * sizeof(double)); return angle; }
  tfp_og_rmul_t(to, from, rel);
  double part[3][3];
  tfp_og_part(rel, limit / angle, part);
  tfp_og_rmul(part, from, out);
  return limit;
}
/* Each axis (row) turned about `axis` (unit) by `angle` radians, as a world rotation turns a vector. */
static inline void tfp_og_turn_rows(double r[3][3], const double axis[3], double angle) {
  const double c = cos(angle), s = sin(angle);
  for (unsigned i = 0; i < 3; ++i) {
    double v[3], k[3];
    memcpy(v, r[i], sizeof v);
    tfp_og_cross(axis, v, k);
    const double along = tfp_og_dot(axis, v) * (1 - c);
    for (unsigned j = 0; j < 3; ++j) r[i][j] = v[j] * c + k[j] * s + axis[j] * along;
  }
}
static inline void tfp_og_turn_vector(double v[3], const double axis[3], double angle) {
  double r[3][3] = {{v[0], v[1], v[2]}};
  tfp_og_turn_rows(r, axis, angle);
  memcpy(v, r[0], 3 * sizeof(double));
}
/* The turn that takes direction u onto direction v: axis and angle. */
static inline void tfp_og_arc(const double u[3], const double v[3], double axis[3], double *angle) {
  double c[3];
  tfp_og_cross(u, v, c);
  const double s = tfp_og_len(c), d = tfp_og_dot(u, v);
  *angle = atan2(s, d);
  if (s > 1e-12) { for (unsigned i = 0; i < 3; ++i) axis[i] = c[i] / s; return; }
  /* parallel or opposite: any axis across u */
  const double x[3] = {1, 0, 0}, y[3] = {0, 1, 0};
  tfp_og_cross(u, fabs(u[0]) < .9 ? x : y, axis);
  const double n = tfp_og_len(axis);
  for (unsigned i = 0; i < 3; ++i) axis[i] = n > 0 ? axis[i] / n : i == 2;
}
static inline bool tfp_og_frame_ok(const TfpGraftMatrix *m) { return tfp_graft_rigid(m); }

static inline void tfp_offhand_reset(TfpOffhand *s) {
  memset(s, 0, sizeof *s);
  for (unsigned j = 0; j < 3; ++j) s->c[j][0][0] = s->c[j][1][1] = s->c[j][2][2] = 1;
}
/* One frame. upper, fore, hand: the drive's locals of rig nodes 10, 12, 13 (of node 7, 10 and 12), changed in place
 * when a correction is in force (rotations only). target: the grip in node 7's frame, NULL for none. wanted: the hand
 * is to be on the target (the caller's conditions: switched on, the body's hand on that grip, not one-handed, not
 * aiming). seconds: since the previous frame (0: the same frame again, or unknown: nothing may change). */
static inline TfpOffhandResult tfp_offhand_step(TfpOffhand *s, TfpGraftMatrix *upper, TfpGraftMatrix *fore, TfpGraftMatrix *hand,
                                                const TfpGraftMatrix *target, bool wanted, double seconds) {
  TfpOffhandResult out = {.reason = TFP_OG_IDLE};
  if (!(seconds >= 0) || seconds > .25) seconds = 0; /* a NaN, a clock that ran back, a long pause: no step */
  const double limit = fmin(TFP_OG_TURN_DPS * seconds, TFP_OG_TURN_CAP) * (TFP_OG_PI / 180);
  out.limit_degrees = (float)(limit * 180 / TFP_OG_PI);
  if (!wanted && !s->active && !(s->weight > 0)) { s->weight = 0, s->has_side = false; return out; }
  if (!tfp_og_frame_ok(upper) || !tfp_og_frame_ok(fore) || !tfp_og_frame_ok(hand) || (target && !tfp_og_frame_ok(target))) {
    out.reason = TFP_OG_NOT_RIGID; /* corrections kept as they were for the next frame; this one is drawn as the body has it */
    return out;
  }
  const TfpOgFrame L[3] = {tfp_og_frame(upper), tfp_og_frame(fore), tfp_og_frame(hand)};
  const TfpOgFrame wu = L[0], wf = tfp_og_then(&L[1], &wu), wh = tfp_og_then(&L[2], &wf);
  double S[3], E[3], H[3], es[3], he[3];
  memcpy(S, wu.p, sizeof S), memcpy(E, wf.p, sizeof E), memcpy(H, wh.p, sizeof H);
  tfp_og_sub(E, S, es), tfp_og_sub(H, E, he);
  const double a = tfp_og_len(es), b = tfp_og_len(he);
  unsigned reason = TFP_OG_EASING;
  bool usable = target != NULL;
  TfpOgFrame T = {0};
  if (!(a > 1e-4) || !(b > 1e-4)) { out.reason = TFP_OG_NOT_RIGID; return out; }
  if (usable) {
    T = tfp_og_frame(target);
    double ts[3], th[3];
    tfp_og_sub(T.p, S, ts), tfp_og_sub(T.p, H, th);
    if (tfp_og_len(th) > TFP_OG_SHIFT_M) usable = false, reason = TFP_OG_TOO_FAR;
    else if (tfp_og_len(ts) > TFP_OG_REACH * (a + b)) usable = false, reason = TFP_OG_OUT_OF_REACH;
  } else if (wanted) reason = TFP_OG_NO_TARGET;
  const bool going = wanted && usable;
  if (seconds > 0) s->weight = going ? fmin(1, s->weight + seconds / TFP_OG_EASE_S) : fmax(0, s->weight - seconds / TFP_OG_EASE_S);
  const double w = s->weight * s->weight * (3 - 2 * s->weight);
  /* the corrections wanted: none, or the reach */
  double want[3][3][3];
  for (unsigned j = 0; j < 3; ++j) { memset(want[j], 0, sizeof want[j]); want[j][0][0] = want[j][1][1] = want[j][2][2] = 1; }
  if (usable && w > 0) {
    /* where the hand is to be: from the drawn hand towards the target by w */
    double D[3], Dr[3][3], rel[3][3], part[3][3];
    for (unsigned i = 0; i < 3; ++i) D[i] = H[i] + (T.p[i] - H[i]) * w;
    tfp_og_rmul_t(T.r, wh.r, rel);
    tfp_og_part(rel, w, part);
    tfp_og_rmul(part, wh.r, Dr);
    double to[3];
    tfp_og_sub(D, S, to);
    const double dist = tfp_og_len(to);
    if (dist > 1e-6) {
      double dir[3], hint[3] = {-.35, -.15, -.92}, side[3];
      for (unsigned i = 0; i < 3; ++i) dir[i] = to[i] / dist;
      const double reach = fmax(fabs(a - b) + 1e-4, fmin(dist, (a + b) * TFP_OG_STRETCH));
      /* the pole, across the reach; the last side keeps it where the pole comes near the reach's line */
      const double hn = tfp_og_len(hint);
      for (unsigned i = 0; i < 3; ++i) hint[i] /= hn;
      double h = tfp_og_dot(hint, dir);
      for (unsigned i = 0; i < 3; ++i) side[i] = hint[i] - dir[i] * h;
      if (s->has_side) {
        const double p = tfp_og_dot(s->side, dir);
        for (unsigned i = 0; i < 3; ++i) side[i] += .25 * (s->side[i] - dir[i] * p);
      }
      double n = tfp_og_len(side);
      if (n < 1e-6) { /* along the pole with nothing before: any side across the reach */
        double angle;
        tfp_og_arc(dir, dir, side, &angle);
        tfp_og_cross(dir, side, side);
        n = tfp_og_len(side);
      }
      for (unsigned i = 0; i < 3; ++i) side[i] /= n;
      memcpy(s->side, side, sizeof side), s->has_side = true;
      const double along = (a * a - b * b + reach * reach) / (2 * reach), up = sqrt(fmax(0, a * a - along * along));
      double E2[3], H2[3], axis[3], angle, ru[3][3], rf[3][3], v[3], e2s[3], h2e[3];
      for (unsigned i = 0; i < 3; ++i) E2[i] = S[i] + dir[i] * along + side[i] * up, H2[i] = S[i] + dir[i] * reach;
      /* the upper arm swung so its elbow is at E2; the forearm carried with it, then bent so the hand is at H2 */
      tfp_og_sub(E2, S, e2s);
      tfp_og_arc(es, e2s, axis, &angle);
      memcpy(ru, wu.r, sizeof ru), memcpy(rf, wf.r, sizeof rf), memcpy(v, he, sizeof v);
      tfp_og_turn_rows(ru, axis, angle), tfp_og_turn_rows(rf, axis, angle), tfp_og_turn_vector(v, axis, angle);
      tfp_og_sub(H2, E2, h2e);
      tfp_og_arc(v, h2e, axis, &angle);
      tfp_og_turn_rows(rf, axis, angle);
      /* the locals these worlds are, and the corrections that make them from the drive's */
      double lu[3][3], lf[3][3], lh[3][3];
      memcpy(lu, ru, sizeof lu);
      tfp_og_rmul_t(rf, ru, lf);
      tfp_og_rmul_t(Dr, rf, lh);
      tfp_og_rmul_t(lu, L[0].r, want[0]), tfp_og_rmul_t(lf, L[1].r, want[1]), tfp_og_rmul_t(lh, L[2].r, want[2]);
    }
  }
  /* each correction from the last frame's by at most `limit` */
  double most = 0;
  bool any = false, cut = false;
  for (unsigned j = 0; j < 3; ++j) {
    double next[3][3];
    bool held;
    const double moved = tfp_og_towards(s->c[j], want[j], limit, next, &held);
    cut = cut || held;
    if (moved > most) most = moved;
    memcpy(s->c[j], next, sizeof next);
    if (fabs(next[0][0] - 1) + fabs(next[1][1] - 1) + fabs(next[2][2] - 1) > 1e-12) any = true;
  }
  out.step_degrees = (float)(most * 180 / TFP_OG_PI), out.weight = (float)w;
  s->active = any;
  if (!any) {
    if (!(s->weight > 0)) s->has_side = false;
    out.reason = going ? TFP_OG_EASING : reason == TFP_OG_EASING ? TFP_OG_IDLE : reason;
    return out;
  }
  /* the written locals: the corrections on the drive's rotations, the drive's positions */
  TfpGraftMatrix *const m[3] = {upper, fore, hand};
  TfpOgFrame now[3];
  for (unsigned j = 0; j < 3; ++j) {
    now[j] = L[j];
    tfp_og_rmul(s->c[j], L[j].r, now[j].r);
    for (unsigned i = 0; i < 3; ++i)
      for (unsigned k = 0; k < 3; ++k) m[j]->values[4 * i + k] = (float)now[j].r[i][k];
  }
  const TfpOgFrame nf = tfp_og_then(&now[1], &now[0]), nh = tfp_og_then(&now[2], &nf);
  double d[3];
  tfp_og_sub(nh.p, H, d), out.shift_m = (float)tfp_og_len(d);
  if (target) tfp_og_sub(nh.p, T.p, d), out.to_target_m = (float)tfp_og_len(d);
  out.reason = going ? (s->weight >= 1 && !cut ? TFP_OG_HELD : TFP_OG_EASING) : reason;
  return out;
}
#endif
