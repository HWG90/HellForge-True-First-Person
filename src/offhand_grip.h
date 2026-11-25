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
 *   - the hand is to be at the target (the grip node as the drive draws the gun) with the target's turn: the full reach
 *     is solved each frame and a share of each joint's correction of it is in force (the shorter way round), eased in
 *     and out over EASE_S at the quickest, a smoothstep, slower where that would turn a joint by more than EASE_BUDGET
 *     of the frame's allowance;
 *   - the elbow is put in the plane of shoulder, target and a pole that points down and a little out (the rig's own left
 *     elbow hangs that way at the hip), kept from the previous frame's side where the target comes near the pole's line;
 *     each frame's reach swings the arm the least way from the last reach, and its twist about each bone goes to the
 *     body's at TWIST_DPS at most: anchored to the body's, so a long hold cannot wind the elbow and wrist up, and a body
 *     arm jittering frame to frame does not twist the drawn one;
 *   - the arm stretches to STRETCH of its length at most; where the grip is further, the shoulder comes forward along the
 *     line to it by the rest, up to SHOULDER of the arm's length (round 26: the drawn shoulder is the body's, carried by
 *     the right hand's alignment, and a fore-end or vertical grip is 1.0 to 1.15 arm lengths from it; the feel.52 hand
 *     stopped 0.03 to 0.106 m short, the most being (1.15 - 0.97) x 0.5899 m exactly). It moves SHOULDER_MPS at most;
 *   - each written joint's rotation turns from the previous frame's by at most TURN_DPS a second and TURN_CAP in one
 *     frame, so no joint can turn over by a flip of the solve; going away, it returns to the drive's pose at that rate;
 *   - a target further than SHIFT_M from where the body's pose put the hand, or than REACH times the arm's length from
 *     the shoulder (REACH_KEEP once held, so a grip swaying about the limit does not flicker), is not taken: the arm
 *     eases back to the body's pose (the reason says which). Once held, the hand's distance has a keep threshold too,
 *     SHIFT_KEEP_M, and is not asked at all while the caller says the body's own hand is pinned on the target (round 36b:
 *     walking, the copied hand swings away from a grip the game still pins the body's hand on).
 * The fingers are the body's (the drive copies them relative to the hand), shaped for the gun's own handguard; on an
 * attachment's grip node the drive closes them round the grip (round 36b, src/grip_fingers.h). */
#ifndef TFP_OFFHAND_GRIP_H
#define TFP_OFFHAND_GRIP_H
#include "arm_graft.h"

/* Joints of the drive's map (src/arm_graft.h: joint i is rig node 10 + i for the arms). */
enum { TFP_OG_UPPER = 0, TFP_OG_FORE = 2, TFP_OG_HAND = 3 };
#define TFP_OG_EASE_S .15       /* weight 0 to 1 and back at the quickest: the retail module's EASE */
#define TFP_OG_EASE_BUDGET .5   /* the ease turns no joint by more than this share of the frame's allowance (round 26) */
#define TFP_OG_TURN_DPS 720.0   /* a written joint turns by at most this many degrees a second... */
#define TFP_OG_TURN_CAP 30.0    /* ...and never by more than this in one frame */
#define TFP_OG_TWIST_DPS 90.0   /* a held reach's twist about each bone goes to the body's at this many degrees a second */
#define TFP_OG_STRETCH .97      /* the arm is never straighter than this share of its length */
#define TFP_OG_REACH 1.15       /* a target further from the shoulder than this share of the arm's length is out of reach */
#define TFP_OG_REACH_KEEP 1.18  /* ...and one held already is let go only beyond this share (round 26) */
#define TFP_OG_SHOULDER .21     /* the shoulder comes forward by at most this share of the arm's length (round 26) */
#define TFP_OG_SHOULDER_MPS .6  /* ...moving by at most this many metres a second */
#define TFP_OG_SHIFT_M .60      /* a target further than this from the hand the body's pose drew is not taken... */
#define TFP_OG_SHIFT_KEEP_M .75 /* ...and one held already is let go only beyond this (round 36b; not asked while pinned) */
#define TFP_OG_PI 3.14159265358979323846

/* What a frame did (TfpOffhandResult.reason). */
enum {
  TFP_OG_IDLE = 0,     /* nothing wanted and nothing left to undo: the poses are the drive's, untouched */
  TFP_OG_HELD,         /* the hand is on the target at full weight */
  TFP_OG_EASING,       /* on its way to the target or back to the body's pose */
  TFP_OG_NO_TARGET,    /* wanted, but no target this frame: easing back */
  TFP_OG_OUT_OF_REACH, /* the target is beyond REACH (REACH_KEEP once held) of the arm's length from the shoulder */
  TFP_OG_TOO_FAR,      /* the target is more than SHIFT_M (SHIFT_KEEP_M once held, unpinned) from the hand the body's pose drew */
  TFP_OG_NOT_RIGID,    /* a pose was not rigid or a bone has no length: nothing was changed this frame */
  TFP_OG_REASONS
};
typedef struct TfpOffhand {
  bool active;            /* a correction is in the poses (not all three are none, or the shoulder is moved) */
  double weight, eased;   /* 0..1: the ease's progress, and the share of the reach in force (its smoothstep, or slower) */
  double c[3][3][3];      /* the corrections of the upper arm, forearm and hand: local rotation = c x the drive's */
  double o[3][3][3];      /* the local rotations written last frame (while active) */
  double shift[3];        /* the shoulder's move in force, in node 7's frame */
  bool has_side;
  double side[3];         /* the elbow's side of the last solve, for continuity */
  bool has_full, reaching;
  double full[3][3][3];   /* the last full reach: the three local rotations, and the shoulder's move */
  double full_shift[3];
  bool has_body;
  double body[3][3][3];   /* the drive's local rotations last frame */
  bool pinned;            /* set by the caller each frame: the body's own hand is on the target (the game pins it there);
                           * once held, the drawn hand's distance from the target is then not asked (round 36b) */
} TfpOffhand;
typedef struct TfpOffhandResult {
  unsigned reason;        /* TFP_OG_* */
  bool cut;               /* a joint or the shoulder was held back by its limit this frame */
  float weight;           /* the share of the reach in force */
  float to_target_m;      /* the hand as written from the target; 0 without a target */
  float shift_m;          /* the hand as written from where the drive's pose had it */
  float shoulder_m;       /* the shoulder as written from the drive's */
  float reach_share;      /* the target from the drive's shoulder, in arm lengths; 0 without a target */
  float step_degrees;     /* the largest turn of any of the three written joints from the previous frame */
  float limit_degrees;    /* what that turn was allowed to be this frame */
  float body_degrees;     /* the largest turn of the drive's own three joints from the previous frame */
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

static inline void tfp_og_identity(double m[3][3]) { memset(m, 0, 9 * sizeof(double)), m[0][0] = m[1][1] = m[2][2] = 1; }
static inline bool tfp_og_is_identity(const double m[3][3]) {
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j)
      if (m[i][j] != (i == j)) return false;
  return true;
}
static inline void tfp_offhand_reset(TfpOffhand *s) {
  memset(s, 0, sizeof *s);
  for (unsigned j = 0; j < 3; ++j) tfp_og_identity(s->c[j]);
}
/* The upper arm and forearm (world rotations ru, rf, turned in place) swung so the elbow is at E2 and the hand at H2,
 * each by the shortest arc from where they were: the upper arm (carrying the forearm) onto the elbow, then the forearm
 * onto the hand. elbow, hand: the drive's local positions of the forearm (in the upper arm) and the hand (in the forearm). */
static inline void tfp_og_swing(double ru[3][3], double rf[3][3], const double elbow[3], const double hand[3], const double S2[3],
                                const double E2[3], const double H2[3]) {
  double es[3], v[3], e2s[3], h2e[3], axis[3], angle;
  for (unsigned j = 0; j < 3; ++j) es[j] = elbow[0] * ru[0][j] + elbow[1] * ru[1][j] + elbow[2] * ru[2][j];
  for (unsigned j = 0; j < 3; ++j) v[j] = hand[0] * rf[0][j] + hand[1] * rf[1][j] + hand[2] * rf[2][j];
  tfp_og_sub(E2, S2, e2s);
  tfp_og_arc(es, e2s, axis, &angle);
  tfp_og_turn_rows(ru, axis, angle), tfp_og_turn_rows(rf, axis, angle), tfp_og_turn_vector(v, axis, angle);
  tfp_og_sub(H2, E2, h2e);
  tfp_og_arc(v, h2e, axis, &angle);
  tfp_og_turn_rows(rf, axis, angle);
}
/* The full reach of the hand onto target T: the three local rotations; the shoulder forward along the line to T by
 * what the arm lacks at STRETCH, up to SHOULDER of its length. The bones point where the reach puts them; the twist
 * about each is the body's (the drive's pose swung onto the reach, as main solved it), reached from the last full
 * reach's at TWIST_DPS at most (`twist`: radians this frame). Carried over whole from frame to frame, a grip circling
 * the shoulder left a little twist behind on every loop and the elbow and wrist wound up while the hand stayed on the
 * grip (review of round 26: 37 -> 173 degrees in 1000 one-second 3 cm loops); taken whole from the body, a body arm
 * jittering by 10 degrees a frame twisted the drawn arm by 12 in a frame. */
static inline void tfp_og_full(TfpOffhand *s, const TfpOgFrame L[3], const double S[3], double a, double b, const TfpOgFrame *T, double twist) {
  double to[3], dir[3], hint[3] = {-.35, -.15, -.92}, side[3];
  tfp_og_sub(T->p, S, to);
  const double dist = tfp_og_len(to);
  if (!(dist > 1e-6)) return; /* on the shoulder: the last full reach stays */
  for (unsigned i = 0; i < 3; ++i) dir[i] = to[i] / dist;
  const double most = (a + b) * TFP_OG_STRETCH, forward = fmin(fmax(dist - most, 0), (a + b) * TFP_OG_SHOULDER);
  const double reach = fmax(fabs(a - b) + 1e-4, fmin(dist - forward, most));
  double S2[3];
  for (unsigned i = 0; i < 3; ++i) S2[i] = S[i] + dir[i] * forward;
  /* the pole, across the reach; the last side keeps it where the pole comes near the reach's line */
  const double hn = tfp_og_len(hint);
  for (unsigned i = 0; i < 3; ++i) hint[i] /= hn;
  const double h = tfp_og_dot(hint, dir);
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
  double E2[3], H2[3], ru[3][3], rf[3][3];
  for (unsigned i = 0; i < 3; ++i) E2[i] = S2[i] + dir[i] * along + side[i] * up, H2[i] = S2[i] + dir[i] * reach;
  /* the body's twist: the drive's pose swung onto the reach */
  memcpy(ru, L[0].r, sizeof ru);
  tfp_og_rmul(L[1].r, ru, rf);
  tfp_og_swing(ru, rf, L[1].p, L[2].p, S2, E2, H2);
  if (s->has_full) {
    /* the last full reach swung onto this one, its twist turned towards the body's by `twist` at most (the two differ
     * only by a turn about each bone, so the bones stay where the reach puts them) */
    double cu[3][3], cf[3][3], tu[3][3], tf[3][3];
    bool held;
    memcpy(cu, s->full[0], sizeof cu);
    tfp_og_rmul(s->full[1], cu, cf);
    tfp_og_swing(cu, cf, L[1].p, L[2].p, S2, E2, H2);
    tfp_og_towards(cu, ru, twist, tu, &held);
    tfp_og_towards(cf, rf, twist, tf, &held);
    memcpy(ru, tu, sizeof ru), memcpy(rf, tf, sizeof rf);
  }
  memcpy(s->full[0], ru, sizeof ru);
  tfp_og_rmul_t(rf, ru, s->full[1]);
  tfp_og_rmul_t(T->r, rf, s->full[2]);
  for (unsigned i = 0; i < 3; ++i) s->full_shift[i] = dir[i] * forward;
  s->has_full = true;
}
/* One frame. upper, fore, hand: the drive's locals of rig nodes 10, 12, 13 (of node 7, 10 and 12), changed in place
 * when a correction is in force (the three rotations, and the upper arm's position by the shoulder's move). target:
 * the grip in node 7's frame, NULL for none. wanted: the hand is to be on the target (the caller's conditions:
 * switched on, the body's hand on that grip, not one-handed; aiming as well since round 36b). seconds: since the previous frame (0: the
 * same frame again, or unknown: nothing may change). */
static inline TfpOffhandResult tfp_offhand_step(TfpOffhand *s, TfpGraftMatrix *upper, TfpGraftMatrix *fore, TfpGraftMatrix *hand,
                                                const TfpGraftMatrix *target, bool wanted, double seconds) {
  TfpOffhandResult out = {.reason = TFP_OG_IDLE};
  if (!(seconds >= 0) || seconds > .25) seconds = 0; /* a NaN, a clock that ran back, a long pause: no step */
  const double limit = fmin(TFP_OG_TURN_DPS * seconds, TFP_OG_TURN_CAP) * (TFP_OG_PI / 180);
  out.limit_degrees = (float)(limit * 180 / TFP_OG_PI);
  if (!wanted && !s->active && !(s->weight > 0)) {
    s->weight = s->eased = 0, s->has_side = s->has_full = s->reaching = s->has_body = false;
    return out;
  }
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
  if (!(a > 1e-4) || !(b > 1e-4)) { out.reason = TFP_OG_NOT_RIGID; return out; }
  /* how far the drive's own joints turned since the last frame (for the log: what the body did, not this) */
  double body = 0;
  for (unsigned j = 0; j < 3; ++j) {
    if (s->has_body) body = fmax(body, tfp_og_angle(L[j].r, s->body[j]));
    memcpy(s->body[j], L[j].r, sizeof s->body[j]);
  }
  s->has_body = true, out.body_degrees = (float)(body * 180 / TFP_OG_PI);
  unsigned reason = TFP_OG_EASING;
  bool usable = target != NULL;
  TfpOgFrame T = {0};
  if (usable) {
    T = tfp_og_frame(target);
    double ts[3], th[3];
    tfp_og_sub(T.p, S, ts), tfp_og_sub(T.p, H, th);
    out.reach_share = (float)(tfp_og_len(ts) / (a + b));
    if (!(s->reaching && s->pinned) && tfp_og_len(th) > (s->reaching ? TFP_OG_SHIFT_KEEP_M : TFP_OG_SHIFT_M)) usable = false, reason = TFP_OG_TOO_FAR;
    else if (tfp_og_len(ts) > (s->reaching ? TFP_OG_REACH_KEEP : TFP_OG_REACH) * (a + b)) usable = false, reason = TFP_OG_OUT_OF_REACH;
  } else if (wanted) reason = TFP_OG_NO_TARGET;
  const bool going = wanted && usable;
  if (usable) tfp_og_full(s, L, S, a, b, &T, TFP_OG_TWIST_DPS * seconds * (TFP_OG_PI / 180));
  /* the full reach as corrections of the drive's rotations; how far the furthest joint is from the drive's */
  double full_c[3][3][3], most_c = 0;
  if (s->has_full)
    for (unsigned j = 0; j < 3; ++j) {
      tfp_og_rmul_t(s->full[j], L[j].r, full_c[j]);
      double id[3][3];
      tfp_og_identity(id);
      most_c = fmax(most_c, tfp_og_angle(full_c[j], id));
    }
  /* the ease: a smoothstep over EASE_S at the quickest, slowed so that it turns no joint by more than BUDGET of the
   * frame's allowance (a long reach eases over more frames, not in bigger steps) */
  if (seconds > 0) {
    double raw = going && s->has_full ? fmin(1, s->weight + seconds / TFP_OG_EASE_S) : fmax(0, s->weight - seconds / TFP_OG_EASE_S);
    double eased = raw * raw * (3 - 2 * raw);
    if (most_c > 1e-9) {
      const double most = TFP_OG_EASE_BUDGET * limit / most_c;
      if (fabs(eased - s->eased) > most) {
        eased = s->eased + (eased > s->eased ? most : -most);
        raw = eased >= 1 ? 1 : eased <= 0 ? 0 : .5 - sin(asin(1 - 2 * eased) / 3); /* the smoothstep's inverse */
      }
    }
    s->weight = raw, s->eased = eased;
  }
  const double w = s->has_full ? s->eased : 0;
  /* the corrections wanted (a share of the full reach, the shorter way), and the shoulder's move */
  double want[3][3][3], want_shift[3];
  for (unsigned j = 0; j < 3; ++j) {
    if (w >= 1) memcpy(want[j], full_c[j], sizeof want[j]);
    else if (w > 0) tfp_og_part(full_c[j], w, want[j]);
    else tfp_og_identity(want[j]);
  }
  for (unsigned i = 0; i < 3; ++i) want_shift[i] = w > 0 ? s->full_shift[i] * w : 0;
  /* each written joint from the last frame's by at most `limit`: what is drawn turns no faster, whatever the drive's own
   * pose did (a correction that only undoes the body's swing is not limited) */
  double most = 0, next[3][3][3];
  bool any = false, cut = false;
  for (unsigned j = 0; j < 3; ++j) {
    if (!(limit > 0)) { /* no time: the corrections stay */
      tfp_og_rmul(s->c[j], L[j].r, next[j]);
    } else {
      double from[3][3], to[3][3];
      bool held;
      if (s->active) memcpy(from, s->o[j], sizeof from);
      else memcpy(from, L[j].r, sizeof from);
      tfp_og_rmul(want[j], L[j].r, to);
      const double moved = tfp_og_towards(from, to, limit, next[j], &held);
      if (held) tfp_og_rmul_t(next[j], L[j].r, s->c[j]), cut = true;
      else memcpy(s->c[j], want[j], sizeof want[j]);
      if (moved > most) most = moved;
    }
    if (!tfp_og_is_identity(s->c[j])) any = true;
  }
  if (limit > 0) {
    double d[3];
    tfp_og_sub(want_shift, s->shift, d);
    const double gap = tfp_og_len(d), may = TFP_OG_SHOULDER_MPS * seconds;
    if (gap > may) {
      for (unsigned i = 0; i < 3; ++i) s->shift[i] += d[i] * (may / gap);
      cut = true;
    } else memcpy(s->shift, want_shift, sizeof want_shift);
  }
  if (s->shift[0] != 0 || s->shift[1] != 0 || s->shift[2] != 0) any = true;
  out.step_degrees = (float)(most * 180 / TFP_OG_PI), out.weight = (float)w, out.cut = cut;
  s->active = any;
  if (!any) {
    if (!(s->weight > 0)) s->has_side = s->has_full = false;
    s->reaching = going;
    out.reason = going ? TFP_OG_EASING : reason == TFP_OG_EASING ? TFP_OG_IDLE : reason;
    return out;
  }
  /* the written locals: the rotations limited above, the drive's positions, the upper arm's moved with the shoulder */
  TfpGraftMatrix *const m[3] = {upper, fore, hand};
  TfpOgFrame now[3];
  for (unsigned j = 0; j < 3; ++j) {
    now[j] = L[j];
    memcpy(now[j].r, next[j], sizeof next[j]);
    memcpy(s->o[j], next[j], sizeof next[j]);
    for (unsigned i = 0; i < 3; ++i)
      for (unsigned k = 0; k < 3; ++k) m[j]->values[4 * i + k] = (float)now[j].r[i][k];
  }
  for (unsigned i = 0; i < 3; ++i) now[0].p[i] += s->shift[i], upper->values[12 + i] = (float)now[0].p[i];
  out.shoulder_m = (float)tfp_og_len(s->shift);
  const TfpOgFrame nf = tfp_og_then(&now[1], &now[0]), nh = tfp_og_then(&now[2], &nf);
  double d[3];
  tfp_og_sub(nh.p, H, d), out.shift_m = (float)tfp_og_len(d);
  if (target) tfp_og_sub(nh.p, T.p, d), out.to_target_m = (float)tfp_og_len(d);
  s->reaching = going;
  out.reason = going ? (s->weight >= 1 && !cut ? TFP_OG_HELD : TFP_OG_EASING) : reason;
  return out;
}
#endif
