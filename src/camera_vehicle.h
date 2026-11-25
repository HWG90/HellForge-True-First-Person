/* Round 37: an exosuit or vehicle seat under the camera lock. The arithmetic only: no engine reads here.
 *
 * Seen in the game 2026-11-25 (feel.55, session 0410a21a, an EXO-45 cockpit): the game's request stays kind 4 while
 * the diver sits in the suit, the avatar's seated bit (flags[14] & 4) is set, layer 14 leaves the on-foot state (25,
 * not 36) and the ragdoll flag is clear. Retail's helper refuses every head-camera frame that is not on foot, so it
 * hands such a seat to the game; the lock held it as if the body were thrown, and put the eye at the seated diver's
 * head inside the cockpit: the diver's own helmet and a stretched arm in front of the lens, and no view outside.
 * The lock now does what retail does there, without retail's cut: the camera glides to the game's own camera, and
 * back to the head camera when the seat is left.
 *
 * tfp_vehicle_seat: seated, off foot and not in physics. A thrown, dead or diving body is off foot but not seated (the
 * lock keeps holding it); a valve is seated on foot (the held seated view as before); a body in physics in a seat
 * (killed in it) stays the lock's body that is down.
 *
 * TfpSeatGlide: from where the view was to a target read again in every frame (the game's camera walks on while the
 * glide runs). The way is a smoothstep, from rest to rest, over sqrt(2 d / 14 m/s^2) held to 300 to 600 ms: the
 * cockpit's 3.6 m in 600 ms, a step of half a metre in 300. Beyond 8 m it is a cut: no glide begins. The rotation
 * takes the same weight by the shortest arc. The frame at its end writes nothing: the game's own frame is forwarded,
 * which is the target itself, so the glide ends exactly there. */
#ifndef TFP_CAMERA_VEHICLE_H
#define TFP_CAMERA_VEHICLE_H
#include "camera_math.h"

#define TFP_SEAT_GLIDE_ACCEL  14.0f   /* m/s^2: what the duration is worked out from */
#define TFP_SEAT_GLIDE_MIN_MS 300.0f
#define TFP_SEAT_GLIDE_MAX_MS 600.0f
#define TFP_SEAT_GLIDE_CUT_M  8.0f    /* further: a cut */

static inline bool tfp_vehicle_seat(bool seated, bool off_foot, bool ragdoll) { return seated && off_foot && !ragdoll; }

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
  if (!isfinite(from[0]) || !isfinite(from[1]) || !isfinite(from[2]) || !isfinite(target[0]) || !isfinite(target[1]) || !isfinite(target[2])
      || !tfp_q_valid(from_q) || !isfinite(tick)) return false;
  const float d[3] = {target[0]-from[0], target[1]-from[1], target[2]-from[2]};
  const float metres = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]), ms = tfp_seat_glide_ms(metres);
  if (!(ms > 0)) return false;
  memcpy(g->from, from, sizeof g->from); g->from_q = tfp_q_normalised(from_q); g->start = tick; g->ms = ms; g->metres = metres; g->active = true;
  return true;
}

/* One frame at `tick`: true, `out` and `out_q` are written (the glide is under way); false, nothing is written (no
 * glide, or it has ended: the caller forwards the target untouched). A target rotation that is not one keeps the
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
