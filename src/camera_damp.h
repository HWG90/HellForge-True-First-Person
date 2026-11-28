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
/* Round 49: the frame the head camera's eye is damped in (lock_damp_eye in src/camera_helper.c, and
 * tools/harness/replay_filter.c). Until now that was the body root's own axes: the head's place against the root was
 * low-passed as the root sees it. A root that turns under a view that does not (the Stable soak of 2026-11-27, a
 * get-up from lying on the back in the head camera: the view fixed, the root's tilt of 15.5 degrees gone in one frame
 * and its heading turning 16.5 degrees a frame for eight; round 30's whip is the same) then moves the head 0.23 to
 * 0.33 m a frame in those axes where it moves 0.14 to 0.23 m in the world, the 0.25 m limit is reached in one frame,
 * sideways, and the lag swings round with the root: the eye's step changed by 0.29, 0.20, 0.31 and 0.13 m in four
 * frames (tests/reference/soak-60-standup.h). And the tilt going in one frame carried the head 0.30 m in the world
 * without moving it in the root's axes, so nothing of that was damped at all.
 * So the frame is level (its up is the world's), and it turns with the root's heading only as far as the view's
 * heading turns the same way in the same frame:
 *   the body turns with the view (the mouse turns both): the frame is the root's, as before; for a level root to
 *     the bit, the axes themselves are handed back;
 *   the root turns under a still view, or against it: the frame stands still, and what the head does in the world
 *     against the root's place is what is damped;
 *   only the view turns: the root did not, nor does the frame;
 *   the root's heading jumps by more than TFP_DAMP_FRAME_RESEAT in one frame (157 to 180 degrees out of an emote into
 *     a dive backwards): that is no turn, the game has set the axes anew, and the frame goes with them as it always
 *     did. The continuity reads such a frame in the root's axes (THE CUT, src/camera_continuity.h); with the frame
 *     left standing it took the cut up round the wrong way, 0.09 m of step change became 0.26 (soak 29, 16:16:30.8).
 * It starts at the root's heading in the first frame, and again after a pause longer than `max_gap_ms`, a clock that
 * went back or an input that is not a number. Where there is no view to compare with (none given, or straight up or
 * down) the frame turns with the root, as it always did. The lag that is let go when damping stops
 * (tfp_eye_damp_release) is carried in the same frame, so a root that flips under the view (a dive backwards) does
 * not swing it round either.
 * off: how far the root's heading has turned on from the frame's (radians); root_yaw, view_yaw: the headings of the
 * frame before; view_known: that frame had one for the view. */
#define TFP_DAMP_FRAME 1 /* tools/harness/replay_filter.c asks: an older header has none of this */
#define TFP_DAMP_FRAME_RESEAT (TFP_PI/2) /* radians in one frame: more is the game setting the root's axes anew, not a turn */
#define TFP_DAMP_FRAME_SAME 1e-5f /* radians (0.0006 degrees): two turns nearer than this are the same turn */
#define TFP_DAMP_FRAME_LEVEL_MIN 1e-4f /* a forward axis or a view with less than this of level length has no heading */
typedef struct TfpDampFrame { float off, root_yaw, view_yaw; double tick; bool known, view_known; } TfpDampFrame;
static inline void tfp_damp_frame_reset(TfpDampFrame *f) { f->off = f->root_yaw = f->view_yaw = 0; f->tick = 0; f->known = f->view_known = false; }
static inline float tfp_damp_frame_wrap(float angle) { /* into -pi..pi; the angles here are differences of two atan2f */
  if (angle > TFP_PI) angle -= 2*TFP_PI; else if (angle < -TFP_PI) angle += 2*TFP_PI;
  return angle;
}
/* One frame. `root_basis`: the root's axes (rows: right, forward, up); `view`: the view's forward direction in the
 * world, or NULL; `frame` takes the axes to damp in (rows as the root's). True: `frame` is `root_basis` itself, bit
 * for bit (a level root whose every turn so far the view made with it, a first frame, or nothing to work from). */
static inline bool tfp_damp_frame_step(TfpDampFrame *f, const float *root_basis, const float *view, double tick, uint32_t max_gap_ms, float *frame) {
  for (unsigned i = 0; i < 9; ++i) frame[i] = root_basis[i];
  bool finite = isfinite(tick);
  for (unsigned i = 0; i < 9; ++i) finite = finite && isfinite(root_basis[i]);
  if (!finite) { tfp_damp_frame_reset(f); return true; }
  const bool again = !f->known || tick < f->tick || tick-f->tick > max_gap_ms;
  /* a forward axis straight up or down has no heading: the one of the frame before stands (a first frame: the axes) */
  const bool headed = sqrtf(root_basis[3]*root_basis[3]+root_basis[4]*root_basis[4]) >= TFP_DAMP_FRAME_LEVEL_MIN;
  if (!headed && again) { tfp_damp_frame_reset(f); return true; }
  const float root_yaw = headed ? atan2f(root_basis[4], root_basis[3]) : f->root_yaw;
  const bool viewed = view && isfinite(view[0]) && isfinite(view[1]) && sqrtf(view[0]*view[0]+view[1]*view[1]) >= TFP_DAMP_FRAME_LEVEL_MIN;
  const float view_yaw = viewed ? atan2f(view[1], view[0]) : 0;
  if (again) f->off = 0;
  else {
    const float root_turn = tfp_damp_frame_wrap(root_yaw-f->root_yaw);
    const float view_turn = viewed && f->view_known ? tfp_damp_frame_wrap(view_yaw-f->view_yaw) : root_turn;
    float with = 0; /* how far the frame turns: the smaller of the two turns where they are the same way */
    if (fabsf(root_turn) > TFP_DAMP_FRAME_RESEAT) with = root_turn; /* the axes set anew: the frame goes with them */
    else if (root_turn > 0 && view_turn > 0) with = view_turn < root_turn ? view_turn : root_turn;
    else if (root_turn < 0 && view_turn < 0) with = view_turn > root_turn ? view_turn : root_turn;
    /* the view as far or further: off as it was, to the bit (two headings read back through atan2f differ in the last
     * digits where the turns were the same) */
    if (fabsf(root_turn-with) > TFP_DAMP_FRAME_SAME) f->off = tfp_damp_frame_wrap(f->off+(root_turn-with));
  }
  f->root_yaw = root_yaw; f->view_yaw = view_yaw; f->view_known = viewed; f->tick = tick; f->known = true;
  if (f->off == 0 && root_basis[6] == 0 && root_basis[7] == 0 && root_basis[8] == 1) return true; /* the level root's own */
  const float c = cosf(root_yaw-f->off), s = sinf(root_yaw-f->off);
  frame[0] = s; frame[1] = -c; frame[2] = 0;
  frame[3] = c; frame[4] = s; frame[5] = 0;
  frame[6] = 0; frame[7] = 0; frame[8] = 1;
  return false;
}
#endif
