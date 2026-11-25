/* Round 38: first person in an exosuit or vehicle seat under the camera lock. The arithmetic only: no engine reads.
 *
 * Seen in the game 2026-11-25 (feel.55, session 0410a21a, an EXO-45 cockpit; ~/.cache/tfp-round37/user-mech.webp):
 * the game's request stays kind 4 while the diver sits in the suit, the avatar's seated bit (flags[14] & 4) is set,
 * layer 14 leaves the on-foot state (25, not 36) and the ragdoll flag is clear. The lock held that as if the body
 * were thrown: the eye at the bobbing head node plus 0.18 m along the game camera's direction, the seated view eased
 * on 180 ms, the thrown head kick on top, and Show torso drew the diver's own sleeve end-on from a lens beside its
 * shoulder. The first-person arm rig was never on screen (the game parks it outside kind 6). Round 37 handed such a
 * seat to the game's third-person camera; the user wants first person kept, so round 38 keeps it and fixes the view:
 *
 * tfp_vehicle_seat: seated, off foot and not in physics (round 37's rule, validated on ~40 logs as never firing on
 * foot). A thrown, dead or diving body is off foot but not seated (held as before); a valve is seated on foot (the
 * held seated view as before); a body in physics in a seat (killed in it) stays the lock's body that is down.
 *
 * TfpSeatAnchor: the eye is fixed in the body root's frame, not on the bobbing head node. After entry the head's
 * offset from the root is watched; once it has stayed within TFP_SEAT_ANCHOR_STEADY_M of one place for
 * TFP_SEAT_ANCHOR_SETTLE_MS (and that long after entry) it is frozen. Until then the head's own offset is used (the
 * entry glide covers that time). A head that moves TFP_SEAT_ANCHOR_LOST_M from the frozen place (the diver re-seated)
 * starts the settling again.
 *
 * tfp_seat_eye: root + R_root (anchor) + forward m along the root's level forward (row 1 of the basis, z dropped) +
 * up m straight up. Never along the game camera's direction: looking sideways in the cockpit does not swing the eye.
 *
 * TfpSeatGlide: from where the view was to a target read again in every frame. The way is a smoothstep, from rest to
 * rest, over sqrt(2 d / 14 m/s^2) held to 300 to 600 ms: the cockpit's 3.6 m in 600 ms, a step of half a metre in
 * 300. Beyond 8 m it is a cut: no glide begins. The rotation takes the same weight by the shortest arc. The frame at
 * its end writes nothing: the target is shown as it is. */
#ifndef TFP_CAMERA_VEHICLE_H
#define TFP_CAMERA_VEHICLE_H
#include "camera_math.h"

#define TFP_SEAT_GLIDE_ACCEL  14.0f   /* m/s^2: what the duration is worked out from */
#define TFP_SEAT_GLIDE_MIN_MS 300.0f
#define TFP_SEAT_GLIDE_MAX_MS 600.0f
#define TFP_SEAT_GLIDE_CUT_M  8.0f    /* further: a cut */

#define TFP_SEAT_ANCHOR_SETTLE_MS 300.0
#define TFP_SEAT_ANCHOR_STEADY_M  .02f
#define TFP_SEAT_ANCHOR_LOST_M    .30f

static inline bool tfp_vehicle_seat(bool seated, bool off_foot, bool ragdoll) { return seated && off_foot && !ragdoll; }

/* ---- the anchor ---- */
typedef struct TfpSeatAnchor { float candidate[3], frozen[3]; double since, entered; bool started, known, settled; } TfpSeatAnchor;

static inline void tfp_seat_anchor_reset(TfpSeatAnchor *a) { memset(a, 0, sizeof *a); }

static inline float tfp_seat_apart(const float *a, const float *b) {
  const float d[3] = {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
  return sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}
/* One frame: `head` is the head node's offset from the root in the root's frame. `out`: the offset the eye is built
 * on. False: `head` is not a number (nothing is written, nothing changes). */
static inline bool tfp_seat_anchor_step(TfpSeatAnchor *a, const float *head, double tick, float *out) {
  if (!tfp_finite3(head) || !isfinite(tick)) return false;
  if (!a->started || tick < a->entered) { tfp_seat_anchor_reset(a); a->started = true; a->entered = tick; }
  if (a->settled && tfp_seat_apart(head, a->frozen) > TFP_SEAT_ANCHOR_LOST_M) { a->settled = false; a->known = false; }
  if (a->settled) { memcpy(out, a->frozen, 12); return true; }
  if (!a->known || tick < a->since || tfp_seat_apart(head, a->candidate) > TFP_SEAT_ANCHOR_STEADY_M) {
    memcpy(a->candidate, head, 12); a->since = tick; a->known = true;
  } else if (tick-a->since >= TFP_SEAT_ANCHOR_SETTLE_MS && tick-a->entered >= TFP_SEAT_ANCHOR_SETTLE_MS) {
    memcpy(a->frozen, a->candidate, 12); a->settled = true;
    memcpy(out, a->frozen, 12); return true;
  }
  memcpy(out, head, 12);
  return true;
}

/* ---- the eye ---- */
/* `basis`: the root's axis rows (right, forward, up) in the world; `offset` in that frame. False: the root has no
 * level forward (it points straight up or down) or a value is not a number; nothing is written. */
static inline bool tfp_seat_eye(const float *root, const float *basis, const float *offset, float forward_m, float up_m, float *eye) {
  if (!tfp_finite3(root) || !tfp_finite3(basis) || !tfp_finite3(basis+3) || !tfp_finite3(basis+6) || !tfp_finite3(offset)
      || !isfinite(forward_m) || !isfinite(up_m)) return false;
  float level[2] = {basis[3], basis[4]};
  const float length = sqrtf(level[0]*level[0]+level[1]*level[1]);
  if (!(length > .0001f)) return false;
  level[0] /= length; level[1] /= length;
  float out[3];
  for (unsigned c = 0; c < 3; ++c) {
    out[c] = root[c];
    for (unsigned axis = 0; axis < 3; ++axis) out[c] += offset[axis]*basis[axis*3+c];
  }
  out[0] += level[0]*forward_m; out[1] += level[1]*forward_m; out[2] += up_m;
  if (!tfp_finite3(out)) return false;
  memcpy(eye, out, 12);
  return true;
}
/* A point in the world as an offset from `from` along the root's axes (right, forward, up). */
static inline void tfp_seat_local(const float *basis, const float *from, const float *point, float *out) {
  for (unsigned axis = 0; axis < 3; ++axis) {
    out[axis] = 0;
    for (unsigned c = 0; c < 3; ++c) out[axis] += (point[c]-from[c])*basis[axis*3+c];
  }
}

/* ---- the glide ---- */
/* The glide's duration for `metres`; 0: no glide (a cut). */
static inline float tfp_seat_glide_ms(float metres) {
  if (!(metres >= 0) || !(metres <= TFP_SEAT_GLIDE_CUT_M)) return 0;
  const float ms = sqrtf(2*metres/TFP_SEAT_GLIDE_ACCEL)*1000;
  return fminf(TFP_SEAT_GLIDE_MAX_MS, fmaxf(TFP_SEAT_GLIDE_MIN_MS, ms));
}

typedef struct TfpSeatGlide { float from[3]; TfpQ from_q; double start; float ms, metres; bool active; } TfpSeatGlide;

static inline void tfp_seat_glide_reset(TfpSeatGlide *g) { memset(g, 0, sizeof *g); g->from_q = tfp_q_identity(); }

/* Begins a glide from `from` (and `from_q`) at `tick` (ms), its length taken from where the target is now. False:
 * no glide (a cut, or a start or target that is not a number); the glide is then off. */
static inline bool tfp_seat_glide_begin(TfpSeatGlide *g, const float *from, TfpQ from_q, const float *target, double tick) {
  tfp_seat_glide_reset(g);
  if (!tfp_finite3(from) || !tfp_finite3(target) || !tfp_q_valid(from_q) || !isfinite(tick)) return false;
  const float metres = tfp_seat_apart(target, from), ms = tfp_seat_glide_ms(metres);
  if (!(ms > 0)) return false;
  memcpy(g->from, from, sizeof g->from); g->from_q = tfp_q_normalised(from_q); g->start = tick; g->ms = ms; g->metres = metres; g->active = true;
  return true;
}

/* One frame at `tick`: true, `out` and `out_q` are written (the glide is under way); false, nothing is written (no
 * glide, or it has ended: the caller shows the target untouched). A target rotation that is not one keeps the
 * start's; a clock that went back is the start. */
static inline bool tfp_seat_glide_apply(TfpSeatGlide *g, const float *target, TfpQ target_q, double tick, float *out, TfpQ *out_q) {
  if (!g->active) return false;
  double passed = tick-g->start;
  if (!(passed >= 0)) passed = 0;
  if (passed >= g->ms) { g->active = false; return false; }
  const float t = (float)(passed/g->ms), w = t*t*(3-2*t);
  for (unsigned i = 0; i < 3; ++i) out[i] = g->from[i]+(target[i]-g->from[i])*w;
  *out_q = tfp_q_valid(target_q) ? tfp_slerp(g->from_q, tfp_q_normalised(target_q), w) : g->from_q;
  return true;
}
#endif
