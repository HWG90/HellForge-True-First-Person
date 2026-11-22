/* Camera lock (beyond retail): the arithmetic of the kick the view takes from the body's own movement.
 * The head's rotation is taken relative to the view, so turning the view with the mouse (the head follows)
 * gives nothing; what the head does on its own (a dive, a landing, being thrown) reaches the view by `gain`,
 * limited to `max_degrees`, and settles with the time constant `settle_ms`. With `damp_ms` above 0 the kick is
 * low-passed in time with that time constant before it is returned (the camera damping option, src/camera_damp.h):
 * the view then takes up a movement of the body over several frames, not in one. No engine reads here. */
#ifndef TFP_CAMERA_LOCK_H
#define TFP_CAMERA_LOCK_H
#include "camera_math.h"
#include "camera_damp.h"

typedef struct TfpKickTuning { float gain, max_degrees, settle_ms, damp_ms; } TfpKickTuning;
/* The tuning of one frame under a camera style (src/camera_damp.h): the on-foot pair of the sliders, or the thrown
 * pair for a body that is thrown, down or off foot, each strength times the style's share, with the style's
 * low-pass. Bodycam gives the sliders back as they are. */
static inline TfpKickTuning tfp_kick_style_tuning(const TfpCameraStyle *style, bool thrown, float gain, float max_degrees, float thrown_gain,
                                                  float thrown_max_degrees, float settle_ms) {
  return (TfpKickTuning){thrown ? thrown_gain*style->thrown_kick : gain*style->foot_kick, thrown ? thrown_max_degrees : max_degrees, settle_ms,
                         style->kick_damp_ms};
}
/* rest: the slowly following head rotation, in the view's frame; view: last frame's view, for its own turning speed;
 * shown: the kick last returned (what the damping goes on from) */
typedef struct TfpKick { TfpQ rest, view, shown; bool known; } TfpKick;
#define TFP_KICK_YAW_SHARE .2f /* how much of the head's sideways turn against the view counts; a nod and a tilt count whole */
#define TFP_KICK_VIEW_STILL 60.0f /* degrees a second of the view's own turning at which nothing is added any more */

static inline void tfp_kick_reset(TfpKick *k) { k->rest = k->view = k->shown = tfp_q_identity(); k->known = false; }
/* One frame of the kick as the head gives it, before any damping. `view` is the game's own view rotation in the
 * world (without any kick), `head` the head's. Returns the kick as a rotation in the view's own frame; identity when
 * there is nothing to add. */
static inline TfpQ tfp_kick_raw(TfpKick *k, TfpQ view, TfpQ head, float dt, const TfpKickTuning *t) {
  if (!tfp_q_valid(view) || !tfp_q_valid(head) || !(dt > 0) || dt > .25f) { k->known = false; return tfp_q_identity(); }
  const TfpQ relative = tfp_q_normalised(tfp_q_mul(tfp_q_inv(tfp_q_normalised(view)), tfp_q_normalised(head)));
  if (!k->known) { k->rest = relative; k->view = view; k->known = true; return tfp_q_identity(); }
  /* The view's own turning (the mouse, recoil) moves it against the head: that is not a body movement. */
  /* The turn is read from the vector part of the rotation between the two frames, which is exact for small angles.
   * Through acos of the dot product one step below 1 reads as 0.04 degrees, and a still view that carries rounding
   * (it is rebuilt from a matrix every frame) then turns 20 degrees a second at 2 ms a frame: the kick was weaker
   * and shorter the higher the frame rate. */
  const TfpQ turn = tfp_q_mul(tfp_q_inv(tfp_q_normalised(k->view)), tfp_q_normalised(view));
  const float half = sqrtf(turn.x*turn.x+turn.y*turn.y+turn.z*turn.z);
  const float turned = 2*asinf(half < 1 ? half : 1)*180/TFP_PI;
  float still = 1 - turned/dt/TFP_KICK_VIEW_STILL; k->view = view;
  if (!(still > 0)) still = 0;
  if (still < 1) k->rest = tfp_slerp(k->rest, relative, 1-still);
  const float settle = t->settle_ms > 1 ? t->settle_ms/1000 : .001f;
  k->rest = tfp_slerp(k->rest, relative, 1-expf(-dt/settle));
  if (!tfp_q_valid(k->rest)) { k->known = false; return tfp_q_identity(); }
  if (!(t->gain > 0) || !(t->max_degrees > 0)) return tfp_q_identity();
  TfpQ moved = tfp_q_normalised(tfp_q_mul(relative, tfp_q_inv(k->rest)));
  {
    /* The sideways part (about the view's up axis, Z) is the body twisting under the head in a strafe or a walk
     * backwards, and in the view it turns the aim: it counts for TFP_KICK_YAW_SHARE. A nod and a tilt count whole.
     * Worked on the rotation vector (axis times angle), where the three parts are separate for the small angles
     * a kick is made of. */
    if (moved.w < 0) moved.x = -moved.x, moved.y = -moved.y, moved.z = -moved.z, moved.w = -moved.w;
    const float sine = sqrtf(moved.x*moved.x+moved.y*moved.y+moved.z*moved.z);
    if (sine > 1e-6f) {
      const float angle = 2*atan2f(sine, moved.w), scale = angle/sine;
      const float rx = moved.x*scale, ry = moved.y*scale, rz = moved.z*scale*TFP_KICK_YAW_SHARE;
      const float turned_by = sqrtf(rx*rx+ry*ry+rz*rz);
      if (turned_by > 1e-6f) {
        const float half_sine = sinf(turned_by/2)/turned_by;
        moved.x = rx*half_sine, moved.y = ry*half_sine, moved.z = rz*half_sine, moved.w = cosf(turned_by/2);
      } else moved = tfp_q_identity();
    }
  }
  const float degrees = tfp_degrees_between(moved, tfp_q_identity());
  if (!(degrees > .0001f)) return tfp_q_identity();
  float share = (t->gain < 1 ? t->gain : 1)*still;
  if (degrees*share > t->max_degrees) share = t->max_degrees/degrees;
  const TfpQ kick = tfp_slerp(tfp_q_identity(), moved, share);
  return tfp_q_valid(kick) ? kick : tfp_q_identity();
}
/* One frame: tfp_kick_raw, then the damping. The rotation returned is the one to apply, and the one the caller takes
 * out of the rendered view again to find the game's own (it goes on from what it returned, not from the raw kick).
 * damp_ms 0: the raw kick itself. A frame that cannot be used (tfp_kick_raw's first line) and a reset give identity
 * and start the damping again from there. Always a unit rotation. */
static inline TfpQ tfp_kick_step(TfpKick *k, TfpQ view, TfpQ head, float dt, const TfpKickTuning *t) {
  const TfpQ raw = tfp_kick_raw(k, view, head, dt, t);
  if (!k->known || !(t->damp_ms > 0) || !isfinite(t->damp_ms)) { k->shown = k->known ? raw : tfp_q_identity(); return raw; }
  /* Frame-rate independent: the share of the way taken in dt. dt is above 0 here (tfp_kick_raw). */
  const TfpQ next = tfp_slerp(k->shown, raw, 1-expf(-dt*1000/t->damp_ms));
  k->shown = tfp_q_valid(next) ? tfp_q_normalised(next) : tfp_q_identity();
  return k->shown;
}
static inline TfpQ tfp_kick_apply(TfpQ view, TfpQ kick) { return tfp_q_normalised(tfp_q_mul(view, kick)); }
#endif
