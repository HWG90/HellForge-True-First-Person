/* The glide of the head camera's eye to the incoming weapon's eye. The helper holds the eye of the native view
 * through a weapon swap as a difference from the head camera's own eye (eye_delta, in the body root's frame); when
 * the weapon identity changes and the incoming weapon's eye is remembered, that difference is moved to the incoming
 * one's while the swap has the view in the head camera, so the native view returns to a camera that is already
 * there. The arithmetic only: no engine reads here. */
#ifndef TFP_CAMERA_GLIDE_H
#define TFP_CAMERA_GLIDE_H
#include "camera_math.h"

/* Retail's glide, one frame: `value` goes to `target` on a time constant of 70 ms; `dt` in seconds. True: it moved
 * (by more than 0.000001 m along an axis). 1-exp(-t/70 ms) of the way after t whatever the frames, so it never ends
 * (1.4 % is left after 300 ms), and it starts at full speed: the first frame is its largest, 38 % of the way at 33 ms
 * a frame. */
static inline bool tfp_eye_glide_retail(float *value, const float *target, float dt) {
  const float step = dt > 0 ? 1-expf(-dt/.07f) : 0; bool moved = false;
  for (unsigned axis = 0; axis < 3; ++axis) {
    const float change = (target[axis]-value[axis])*step;
    if (fabsf(change) > 1e-6f) moved = true;
    value[axis] += change;
  }
  return moved;
}
#endif
