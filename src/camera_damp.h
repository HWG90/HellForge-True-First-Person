/* Camera damping (beyond retail): how much the view is steadied against the body's own movement. One option, a
 * percentage, gives one time constant; the kick of the view's rotation is low-passed with it (src/camera_lock.h),
 * and so is the eye of the head camera, here: what is low-passed is the head's place against the body root, and
 * the lag of that is what the eye is moved by. No engine reads here. */
#ifndef TFP_CAMERA_DAMP_H
#define TFP_CAMERA_DAMP_H
#include "camera_math.h"

#define TFP_CAMERA_DAMPING_MS 300.0f /* the time constant at 100 % */
#define TFP_EYE_DAMP_LAG_M .10f      /* the most the presented eye is ever behind the true one, under the camera damping option */
#define TFP_EYE_DAMP_RELEASE_MS 40.0f /* the time constant a carried lag is let go on when damping stops */
#define TFP_EYE_DAMP_GONE_M .001f    /* a lag let go this far is gone */
#define TFP_EYE_DAMP_LIMIT_M_S .55f  /* metres a second the limit in force goes down by when a change of camera style lowers it */

/* The option's percentage as a time constant in milliseconds: 0 none, 100 the longest, linear between. */
static inline float tfp_camera_damping_ms(double percent) {
  if (!(percent > 0)) return 0;
  return (float)(percent < 100 ? percent : 100)*TFP_CAMERA_DAMPING_MS/100;
}

/* The camera style (the Bodycam option of the Locked view tab): which of two cameras the lock gives.
 *   Bodycam: the view moves with the head, like a body camera. The kick sliders and the camera damping option
 *            apply as they are; this is what the mod did before the option existed.
 *   Stable (the option off, the default): a steady, traditional first-person camera. The sliders for the kick on
 *            foot and the camera damping are not used; the numbers below are, and they are all of them. */
#define TFP_STABLE_FOOT_KICK 0.0f       /* share of the kick strength on foot: none. Walking, strafing, crouching, a reload, a grenade or a stim do not turn the view */
#define TFP_STABLE_THROWN_KICK .5f      /* share of the "when thrown" strength a thrown or ragdolled body still gives (its limit is not changed) */
#define TFP_STABLE_KICK_DAMP_MS 250.0f  /* the time constant that kick is low-passed with, so being thrown reads as a sway, not a jolt */
#define TFP_STABLE_EYE_DAMP_MS 350.0f   /* the time constant the head camera's eye follows the head with: the head's bobbing and jerks in an action mostly do not reach the view */
#define TFP_STABLE_EYE_LAG_M .25f       /* the most that eye is behind the head: a crouch or going prone (0.5 m, and staying there) is followed within about a second */
/* foot_kick, thrown_kick: what the two kick strengths are multiplied by; kick_damp_ms: the kick's low-pass (0: none);
 * eye_damp_ms, eye_lag_m: the head camera eye's low-pass (0: none) and the most it is behind. */
typedef struct TfpCameraStyle { float foot_kick, thrown_kick, kick_damp_ms, eye_damp_ms, eye_lag_m; } TfpCameraStyle;
/* `damping_ms`: the camera damping option as a time constant (tfp_camera_damping_ms), which only Bodycam uses. */
static inline TfpCameraStyle tfp_camera_style(bool bodycam, float damping_ms) {
  if (bodycam) return (TfpCameraStyle){1.0f, 1.0f, damping_ms, damping_ms, TFP_EYE_DAMP_LAG_M};
  return (TfpCameraStyle){TFP_STABLE_FOOT_KICK, TFP_STABLE_THROWN_KICK, TFP_STABLE_KICK_DAMP_MS, TFP_STABLE_EYE_DAMP_MS, TFP_STABLE_EYE_LAG_M};
}

/* eye: the point last presented, relative to the body root in the root's frame; behind: that point minus the true
 * one; tick: when, in milliseconds; releasing: the lag is being let go (tfp_eye_damp_release), `eye` is not kept;
 * limit, ms: the lag limit and the time constant in force, which are the ones given except after a change of camera
 * style that lowered the limit (tfp_eye_damp_follow) */
typedef struct TfpEyeDamp { float eye[3], behind[3], limit, ms; double tick; bool known, releasing; } TfpEyeDamp;
static inline void tfp_eye_damp_reset(TfpEyeDamp *d) {
  for (unsigned i = 0; i < 3; ++i) d->eye[i] = d->behind[i] = 0;
  d->limit = d->ms = 0; d->tick = 0; d->known = false; d->releasing = false;
}
/* One head-camera frame. `wanted` is the true eye relative to the body root, `damp_ms` the time constant (0: no
 * damping), `lag_m` the most the presented eye is ever behind the true one (a camera style's eye_lag_m; not above
 * 0: no damping), `out` the eye to present. Returns how far `out` is behind `wanted`; 0: it is `wanted` itself.
 * It starts again from the true eye after a pause longer than `max_gap_ms`, after an input that is not a number and
 * after a frame without damping. The caller resets it in every frame that is not a head-camera frame, so the change
 * to the head camera itself is not damped here (src/camera_continuity.h takes that).
 * A `lag_m` below the one of the frame before (the camera style went from Stable to Bodycam while a lag was carried)
 * is not in force at once: the clamp would pull the eye by the difference, up to 0.15 m, in that frame. The limit
 * in force goes down to it at TFP_EYE_DAMP_LIMIT_M_S, and until it is there the time constant is not shortened
 * either, so the eye is never moved faster than the camera it comes from moved it. A constant rate and not the
 * 40 ms of tfp_eye_damp_release: that takes more than half of the difference in the first frame at 30 frames a
 * second, this takes 0.018 m, the same way in every frame, and is there within 300 ms. A limit that goes up and a
 * longer time constant pull nothing and are in force at once. With the same `damp_ms` and `lag_m` in every frame
 * the two in force are those, and nothing here differs by a bit. */
static inline float tfp_eye_damp_follow(TfpEyeDamp *d, const float *wanted, double tick, uint32_t max_gap_ms, float damp_ms, float lag_m, float *out) {
  for (unsigned i = 0; i < 3; ++i) out[i] = wanted[i];
  if (!isfinite(wanted[0]) || !isfinite(wanted[1]) || !isfinite(wanted[2]) || !isfinite(tick) || !(damp_ms > 0) || !isfinite(damp_ms) || !(lag_m > 0)) {
    tfp_eye_damp_reset(d); return 0;
  }
  if (!d->known || tick < d->tick || tick-d->tick > max_gap_ms) {
    for (unsigned i = 0; i < 3; ++i) { d->eye[i] = wanted[i]; d->behind[i] = 0; }
    d->limit = lag_m; d->ms = damp_ms; d->tick = tick; d->known = true; d->releasing = false; return 0;
  }
  if (!(lag_m < d->limit)) { d->limit = lag_m; d->ms = damp_ms; }
  else { /* on the way down to a lower limit */
    d->limit -= TFP_EYE_DAMP_LIMIT_M_S*(float)(tick-d->tick)/1000;
    if (d->limit < lag_m) d->limit = lag_m;
    if (damp_ms > d->ms) d->ms = damp_ms;
  }
  if (d->releasing) { /* damping again before the lag was gone: from where the point was shown */
    for (unsigned i = 0; i < 3; ++i) d->eye[i] = wanted[i]+d->behind[i];
    d->releasing = false;
  }
  const float k = 1-expf(-(float)(tick-d->tick)/d->ms); /* the same instant again: 0, and the same answer */
  float behind[3], square = 0;
  for (unsigned i = 0; i < 3; ++i) { d->eye[i] += (wanted[i]-d->eye[i])*k; behind[i] = wanted[i]-d->eye[i]; square += behind[i]*behind[i]; }
  float lag = sqrtf(square);
  if (!isfinite(lag)) { tfp_eye_damp_reset(d); return 0; }
  if (lag > d->limit) { /* pulled along at the limit */
    for (unsigned i = 0; i < 3; ++i) d->eye[i] = wanted[i]-behind[i]*(d->limit/lag);
    lag = d->limit;
  }
  d->tick = tick;
  for (unsigned i = 0; i < 3; ++i) d->behind[i] = d->eye[i]-wanted[i];
  if (!(lag > 0)) return 0;
  for (unsigned i = 0; i < 3; ++i) out[i] = d->eye[i];
  return lag;
}
/* The same with the camera damping option's own limit, TFP_EYE_DAMP_LAG_M. */
static inline float tfp_eye_damp_step(TfpEyeDamp *d, const float *wanted, double tick, uint32_t max_gap_ms, float damp_ms, float *out) {
  return tfp_eye_damp_follow(d, wanted, tick, max_gap_ms, damp_ms, TFP_EYE_DAMP_LAG_M, out);
}
/* One frame in which damping has stopped (the body left 'on foot', full aim began) while a lag is carried: the lag
 * is let go on TFP_EYE_DAMP_RELEASE_MS, not dropped in one frame, and nothing new is damped: `behind` takes what to
 * add to the true point this frame, whatever that point does. Returns its length; 0: nothing is carried (any more)
 * and the state is forgotten. `slowly`: and by no more than TFP_EYE_DAMP_LIMIT_M_S (tfp_eye_damp_release_slowly). */
static inline float tfp_eye_damp_let_go(TfpEyeDamp *d, double tick, uint32_t max_gap_ms, bool slowly, float *behind) {
  behind[0] = behind[1] = behind[2] = 0;
  if (!d->known || !isfinite(tick) || tick < d->tick || tick-d->tick > max_gap_ms) { tfp_eye_damp_reset(d); return 0; }
  float keep = expf(-(float)(tick-d->tick)/TFP_EYE_DAMP_RELEASE_MS);
  if (slowly) {
    const float carried = sqrtf(d->behind[0]*d->behind[0]+d->behind[1]*d->behind[1]+d->behind[2]*d->behind[2]);
    const float most = TFP_EYE_DAMP_LIMIT_M_S*(float)(tick-d->tick)/1000;
    if (carried*(1-keep) > most) keep = (carried-most)/carried;
  }
  float square = 0;
  for (unsigned i = 0; i < 3; ++i) { d->behind[i] *= keep; square += d->behind[i]*d->behind[i]; }
  const float lag = sqrtf(square);
  if (!isfinite(lag) || lag < TFP_EYE_DAMP_GONE_M) { tfp_eye_damp_reset(d); return 0; }
  for (unsigned i = 0; i < 3; ++i) behind[i] = d->behind[i];
  d->tick = tick; d->releasing = true;
  return lag;
}
static inline float tfp_eye_damp_release(TfpEyeDamp *d, double tick, uint32_t max_gap_ms, float *behind) {
  return tfp_eye_damp_let_go(d, tick, max_gap_ms, false, behind);
}
/* The same where the damping stopped by an option and nothing needs the true eye at once (the camera style went to
 * Bodycam with the camera damping option at 0, or that option was set to 0, while a lag was carried): the 40 ms
 * alone would take 0.14 m of a carried 0.25 m in one frame at 30 frames a second, so the lag is let go by no more
 * than TFP_EYE_DAMP_LIMIT_M_S as well, the rate a lowered limit goes down at. */
static inline float tfp_eye_damp_release_slowly(TfpEyeDamp *d, double tick, uint32_t max_gap_ms, float *behind) {
  return tfp_eye_damp_let_go(d, tick, max_gap_ms, true, behind);
}

/* Round 30, the whip (docs/design/specs/2026-11-24-residual-jumps.md "Round 30"). The head camera's eye is built on the
 * head, and the head rides the body root: in a dive it is 0.7 m in front of it. When the root's heading flicks under a
 * view that holds (soak 36, 07:03:01.039: 34 then 43 degrees in two frames and 13, 11, 9 back, the view within half a
 * degree) the head swings round the root on that arc and the eye with it: 0.43 and 0.54 m a frame and back. The low-pass
 * above works in the root's frame, where the head does not move, so it lets the whole arc through.
 * Here the part of the eye's move that is the root's turn alone (where the point that rides the root is, less where it
 * would be with the root's axes of the frame before) is carried instead of shown, in the frame of a whip and in the
 * frames after it while the root goes on turning under the held view; then what is carried is let out from rest to rest
 * (1 - smoothstep) over a time that keeps the step's change at TFP_EYE_WHIP_STEP_CHANGE_M a frame (at the whip's own
 * frame time), and no shorter than TFP_EYE_WHIP_LET_OUT_MS. A whip begins where the root's heading turns at
 * TFP_EYE_WHIP_START_RAD_S or more while the view's turns at TFP_EYE_WHIP_VIEW_RAD_S or less, and the point rode the
 * root (it moved against the root less than TFP_EYE_WHIP_RODE of what the turn moved it: a root that turns round a
 * head that stays where it was is no whip); it goes on while the root turns at TFP_EYE_WHIP_ON_RAD_S. Rates, so a
 * frame time of 8 or 50 ms finds the same whips. The player turning (view and root together) is never one; the view
 * turning alone moves no root; a dive's arc, a jump and a crouch are not turns of the root. What is carried is at most
 * TFP_EYE_WHIP_MOST_M. The caller asks only in head-camera frames of a body on foot (a ragdoll, a throw, full aim are
 * not), where a whip under way is still let out but no new one begins. */
#define TFP_EYE_WHIP_START_RAD_S 9.0f /* 0.30 rad in a frame of 33.3 ms */
#define TFP_EYE_WHIP_ON_RAD_S 1.5f    /* 0.05 rad in a frame of 33.3 ms */
#define TFP_EYE_WHIP_VIEW_RAD_S 1.5f
#define TFP_EYE_WHIP_STEP_CHANGE_M .03f
#define TFP_EYE_WHIP_LET_OUT_MS 250.0
#define TFP_EYE_WHIP_MOST_M 1.0f
#define TFP_EYE_WHIP_RODE .25f
/* carried: what is added to the eye (world); from, let_from, let_ms: the let-out under way; basis: the root's axes of
 * the frame before (rows); view_yaw: the view's heading then (radians; not a number: not known); dt: that frame's time. */
typedef struct TfpEyeWhip { float carried[3], from[3], basis[9], against[3], view_yaw; double tick, dt, let_from, let_ms; bool known, turning, letting; uint64_t whips; } TfpEyeWhip;
static inline void tfp_eye_whip_reset(TfpEyeWhip *w) {
  const uint64_t whips = w->whips;
  for (unsigned i = 0; i < 3; ++i) w->carried[i] = w->from[i] = 0;
  for (unsigned i = 0; i < 9; ++i) w->basis[i] = 0;
  for (unsigned i = 0; i < 3; ++i) w->against[i] = 0;
  w->view_yaw = NAN; w->tick = w->dt = w->let_from = w->let_ms = 0; w->known = w->turning = w->letting = false; w->whips = whips;
}
static inline float tfp_eye_whip_wrap(float a) { return a-2*TFP_PI*roundf(a/(2*TFP_PI)); }
/* The heading of a body root's axes (rows: right, forward, up): the right axis's, which a pitch of the body (a dive)
 * leaves level; NAN where it is not near level (a roll: a body off foot). */
static inline float tfp_eye_whip_root_yaw(const float *basis) {
  return basis[0]*basis[0]+basis[1]*basis[1] >= .25f ? atan2f(basis[1], basis[0]) : NAN;
}
/* The view's heading; NAN looking (nearly) straight up or down. */
static inline float tfp_eye_whip_view_yaw(const float *forward) {
  return forward[0]*forward[0]+forward[1]*forward[1] >= .01f ? atan2f(forward[1], forward[0]) : NAN;
}
/* One head-camera frame. `offset`: the point that rides the root (the damped head) less the root's place, in the world;
 * `basis`: the root's axes (rows: right, forward, up); `view_yaw`: the view's heading (tfp_eye_whip_view_yaw); `begin`:
 * a whip may begin or go on (a body on foot). `add` takes what to add to the eye; returns its length. It starts again
 * from nothing after a pause over `max_gap_ms` or an input that is not a number; the same instant again changes
 * nothing. In a frame of a whip what is carried takes the point's move by the root's turn alone: the point where it is
 * less where it would be with the root's axes of the frame before (the same place against the root). */
static inline float tfp_eye_whip_step(TfpEyeWhip *w, const float *offset, const float *basis, float view_yaw, bool begin, double tick, uint32_t max_gap_ms, float *add) {
  add[0] = add[1] = add[2] = 0;
  bool finite = isfinite(tick) && isfinite(offset[0]) && isfinite(offset[1]) && isfinite(offset[2]);
  for (unsigned i = 0; i < 9; ++i) finite = finite && isfinite(basis[i]);
  if (!finite) { tfp_eye_whip_reset(w); return 0; }
  if (!w->known || tick < w->tick || tick-w->tick > max_gap_ms) {
    tfp_eye_whip_reset(w); w->known = true; w->tick = tick; w->view_yaw = view_yaw;
    for (unsigned i = 0; i < 9; ++i) w->basis[i] = basis[i];
    for (unsigned axis = 0; axis < 3; ++axis) { w->against[axis] = 0; for (unsigned i = 0; i < 3; ++i) w->against[axis] += offset[i]*basis[axis*3+i]; }
    return 0;
  }
  float against[3] = {0}, swing[3] = {0}; /* the point against the root, in its axes; its move by the root's turn alone */
  for (unsigned axis = 0; axis < 3; ++axis) for (unsigned i = 0; i < 3; ++i) against[axis] += offset[i]*basis[axis*3+i];
  if (tick > w->tick) {
    const float root_turn = tfp_eye_whip_wrap(tfp_eye_whip_root_yaw(basis)-tfp_eye_whip_root_yaw(w->basis));
    const float view_turn = tfp_eye_whip_wrap(view_yaw-w->view_yaw);
    float swung = 0, slid = 0;
    for (unsigned i = 0; i < 3; ++i) { float before = 0; /* where it would be with the axes of the frame before */
      for (unsigned axis = 0; axis < 3; ++axis) before += against[axis]*w->basis[axis*3+i];
      swing[i] = offset[i]-before; swung += swing[i]*swing[i]; slid += (against[i]-w->against[i])*(against[i]-w->against[i]); }
    /* it rode the root: it moved against the root less than TFP_EYE_WHIP_RODE of what the turn moved it (a root that
     * turns round a head that stays where it was, soak 27's flip of 180 degrees at 13:04:44.367, is no whip) */
    const bool rode = slid <= TFP_EYE_WHIP_RODE*TFP_EYE_WHIP_RODE*swung;
    const float seconds = (float)(tick-w->tick)/1000, root_rate = fabsf(root_turn)/seconds;
    const bool held = isfinite(root_rate) && isfinite(view_turn) && fabsf(view_turn) <= TFP_EYE_WHIP_VIEW_RAD_S*seconds;
    if (begin && held && rode && (root_rate >= TFP_EYE_WHIP_START_RAD_S || (w->turning && root_rate >= TFP_EYE_WHIP_ON_RAD_S))) {
      for (unsigned i = 0; i < 3; ++i) w->carried[i] -= swing[i];
      const float most = sqrtf(w->carried[0]*w->carried[0]+w->carried[1]*w->carried[1]+w->carried[2]*w->carried[2]);
      if (most > TFP_EYE_WHIP_MOST_M) for (unsigned i = 0; i < 3; ++i) w->carried[i] *= TFP_EYE_WHIP_MOST_M/most;
      if (!w->turning) ++w->whips;
      w->turning = true; w->letting = false;
    } else if (w->turning || w->letting) {
      if (w->turning) { /* the whip is over: let out from the last frame of it, in a time that keeps the step's change at
                         * TFP_EYE_WHIP_STEP_CHANGE_M a frame of that frame's time, and no shorter than TFP_EYE_WHIP_LET_OUT_MS */
        const float length = sqrtf(w->carried[0]*w->carried[0]+w->carried[1]*w->carried[1]+w->carried[2]*w->carried[2]);
        for (unsigned i = 0; i < 3; ++i) w->from[i] = w->carried[i];
        w->turning = false; w->letting = true; w->let_from = w->tick;
        w->let_ms = fmax(TFP_EYE_WHIP_LET_OUT_MS, w->dt*sqrt(6.0*length/TFP_EYE_WHIP_STEP_CHANGE_M));
      }
      const double x = w->let_ms > 0 ? (tick-w->let_from)/w->let_ms : 1;
      const float weight = x >= 1 ? 0 : (float)(1-x*x*(3-2*x));
      for (unsigned i = 0; i < 3; ++i) w->carried[i] = w->from[i]*weight;
      if (!(weight > 0)) { w->letting = false; for (unsigned i = 0; i < 3; ++i) w->carried[i] = w->from[i] = 0; }
    }
    w->dt = tick-w->tick; w->tick = tick; w->view_yaw = view_yaw;
    for (unsigned i = 0; i < 9; ++i) w->basis[i] = basis[i];
    for (unsigned i = 0; i < 3; ++i) w->against[i] = against[i];
  }
  const float length = sqrtf(w->carried[0]*w->carried[0]+w->carried[1]*w->carried[1]+w->carried[2]*w->carried[2]);
  if (!(length > 0)) return 0;
  for (unsigned i = 0; i < 3; ++i) add[i] = w->carried[i];
  return length;
}
#endif
