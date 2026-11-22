/* Camera lock (beyond retail): the arithmetic of the kick the view takes from the body's own movement.
 * The head's rotation is taken relative to the view, so turning the view with the mouse (the head follows)
 * gives nothing; what the head does on its own (a dive, a landing, being thrown) reaches the view by `gain`,
 * limited to `max_degrees`, and settles with the time constant `settle_ms`. No engine reads here. */
#ifndef TFP_CAMERA_LOCK_H
#define TFP_CAMERA_LOCK_H
#include "camera_math.h"

typedef struct TfpKickTuning { float gain, max_degrees, settle_ms; } TfpKickTuning;
/* rest: the slowly following head rotation, in the view's frame; view: last frame's view, for its own turning speed */
typedef struct TfpKick { TfpQ rest, view; bool known; } TfpKick;
#define TFP_KICK_VIEW_STILL 60.0f /* degrees a second of the view's own turning at which nothing is added any more */

static inline void tfp_kick_reset(TfpKick *k) { k->rest = k->view = tfp_q_identity(); k->known = false; }
/* One frame. `view` is the game's own view rotation in the world (without any kick), `head` the head's.
 * Returns the kick as a rotation in the view's own frame; identity when there is nothing to add. */
static inline TfpQ tfp_kick_step(TfpKick *k, TfpQ view, TfpQ head, float dt, const TfpKickTuning *t) {
  if (!tfp_q_valid(view) || !tfp_q_valid(head) || !(dt > 0) || dt > .25f) { k->known = false; return tfp_q_identity(); }
  const TfpQ relative = tfp_q_normalised(tfp_q_mul(tfp_q_inv(tfp_q_normalised(view)), tfp_q_normalised(head)));
  if (!k->known) { k->rest = relative; k->view = view; k->known = true; return tfp_q_identity(); }
  /* The view's own turning (the mouse, recoil) moves it against the head: that is not a body movement. */
  float still = 1 - tfp_degrees_between(view, k->view)/dt/TFP_KICK_VIEW_STILL; k->view = view;
  if (!(still > 0)) still = 0;
  if (still < 1) k->rest = tfp_slerp(k->rest, relative, 1-still);
  const float settle = t->settle_ms > 1 ? t->settle_ms/1000 : .001f;
  k->rest = tfp_slerp(k->rest, relative, 1-expf(-dt/settle));
  if (!tfp_q_valid(k->rest)) { k->known = false; return tfp_q_identity(); }
  if (!(t->gain > 0) || !(t->max_degrees > 0)) return tfp_q_identity();
  const TfpQ moved = tfp_q_normalised(tfp_q_mul(relative, tfp_q_inv(k->rest)));
  const float degrees = tfp_degrees_between(moved, tfp_q_identity());
  if (!(degrees > .0001f)) return tfp_q_identity();
  float share = (t->gain < 1 ? t->gain : 1)*still;
  if (degrees*share > t->max_degrees) share = t->max_degrees/degrees;
  const TfpQ kick = tfp_slerp(tfp_q_identity(), moved, share);
  return tfp_q_valid(kick) ? kick : tfp_q_identity();
}
static inline TfpQ tfp_kick_apply(TfpQ view, TfpQ kick) { return tfp_q_normalised(tfp_q_mul(view, kick)); }
#endif
