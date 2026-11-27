/* Round 44: the head camera's eye kept in front of the head while a throw or a swap holds the native eye. The
 * arithmetic only: the camera helper (src/camera_helper.c) asks it in every head-camera frame of the lock.
 *
 * The user, on 0.6.0-feel.58: "Still possible for the camera to fall too far behind the body when throwing items
 * causing your back and cape to be seen." The Trace rows of three throws (tests/reference/throw/) show why. The first
 * head-camera frame's eye is the last native frame's eye to the last digit, and for the next 300 to 500 ms it stays
 * where it is in the body root's frame (b9c6: 0.074 m behind the root, 1.688 m up, to the millimetre) while the head
 * node moves: that is the swap's held eye (eye_delta, kept through mask 2 and TFP_ACTION_EYE_IDLE_MS of mask 0, then
 * let go over 300 to 450 ms), not the head camera's own placement, which puts the eye 0.18 m in front of the head
 * (b9c6 at 35.350, after the hold: 0.35 m in front of the root). The native eye of a grenade or a throwable sits
 * 0.18 to 0.31 m behind the head node (b9c6 0.210, 7a3c 0.176, 5608 prone 0.307). In the native view that is the
 * game's own first-person camera; in the head camera the third-person body is drawn with only the head, torso and
 * shoulders hidden, so from there the cape (and with Show torso the back) is in view. The veil moved the eye clear
 * for the two frames of the leave and then let that move out over 300 ms, back behind the head (b9c6: +0.24 m along
 * the view from the root to -0.12 m); prone it moved nothing (a move over TFP_CAMERA_VEIL_MOST).
 *
 * So, in a head-camera frame of the lock with the body on foot (not down, not off foot, not in a seat or a seated
 * view), with head visibility active (the veil armed):
 *  - THE PUSH (tfp_clear_step): the eye the frame is built on (after the swap's hold and glide, the continuity's
 *    correction and the aim anchor; before the continuity, the damping and the veil) is moved along the level view
 *    until it is TFP_CAMERA_CLEAR_M in front of the head node. Before the continuity, so the continuity, the swap's
 *    glides and the eased way back into the native view all start from the eye that was shown. In a frame the veil is
 *    open for (the leave, where the veil makes the same move) the push is taken whole; anywhere else it changes by at
 *    most TFP_CAMERA_CLEAR_STEP_M a frame (the veil's let-in step, under the judge's 0.05 m): when the body leaves
 *    its feet (a dive, a throw of the body) it is eased out, and eased in again when it lands. Down (ragdoll, dead,
 *    the head far from the root), in any other camera and without the lock it is dropped: those frames are as before.
 *  - A run of such frames whose first frame has the eye more than TFP_CAMERA_CLEAR_FAR_M behind the head is a body
 *    that has left the camera (a swap in the middle of a dive or a lunge: round 26's 0.43 to 0.45 m), not a held eye:
 *    nothing is pushed in that run, as the veil refuses a move over TFP_CAMERA_VEIL_MOST.
 *  - THE FLOOR (tfp_clear_floor): after the continuity, the damping, the veil, its let-out and the carry, the eye
 *    shown is at least as far in front of the head as the push meant (TFP_CAMERA_CLEAR_M once it has caught up):
 *    the continuity's take-up of the leave and the Stable damping's lag would otherwise leave it behind for a few
 *    frames. So the veil's let-out ends at the larger of the held eye and the head plus the clearance. Not in a kept
 *    leave's frames (the let-in and the carry of rounds 30 and 32 keep their caps there).
 * Only the position changes; the eye the swap holds and remembers (eye_memory) is the native one, so a glide back
 * to it ends where the native view will be, and the continuity takes the native view's own eye up when it returns. */
#ifndef TFP_CAMERA_CLEAR_H
#define TFP_CAMERA_CLEAR_H
#include <math.h>
#include <stdbool.h>

#define TFP_CAMERA_CLEAR_M .10f      /* TFP_CAMERA_VEIL_CLEAR: how far in front of the head node, along the level view */
#define TFP_CAMERA_CLEAR_STEP_M .04f /* TFP_CAMERA_VEIL_LET_IN_M: the most the push changes in a frame outside the veil */
#define TFP_CAMERA_CLEAR_FAR_M .40f  /* TFP_CAMERA_VEIL_MOST: further behind the head than this at a run's start, nothing is pushed */

typedef enum TfpClearMode {
  TFP_CLEAR_OFF = 0, /* not a head-camera frame of the lock with head visibility active, or a seat: dropped at once */
  TFP_CLEAR_DOWN,    /* the body is down (ragdoll, dead, the head far): dropped at once */
  TFP_CLEAR_LET_GO,  /* off foot (a dive, a body thrown): eased out */
  TFP_CLEAR_ON       /* on foot: kept clear */
} TfpClearMode;
/* push: the push of the last frame; run: on foot in a run of head-camera frames; far: that run began with the eye more
 * than TFP_CAMERA_CLEAR_FAR_M behind the head; caught: the push has reached what the run wants (it follows it whole). */
typedef struct TfpClear { float push; bool run, far, caught; } TfpClear;

/* How far `eye` is in front of `head` along `h`, the level view direction (unit length, z 0). */
static inline float tfp_clear_ahead(const float *eye, const float *head, const float *h) { return (eye[0]-head[0])*h[0]+(eye[1]-head[1])*h[1]; }

/* One frame. `ahead`: tfp_clear_ahead of the eye the frame is built on, before the push. `whole`: a frame the veil is
 * open for. Returns the push, in metres along the level view (0 or more). Once caught up, the push is what keeps the
 * eye at TFP_CAMERA_CLEAR_M: the eye shown is the larger of the eye and the clearance, which moves no more than the
 * eye or the head does. Before that (a run that began outside the veil, or off foot) it moves by at most
 * TFP_CAMERA_CLEAR_STEP_M a frame. */
static inline float tfp_clear_step(TfpClear *c, TfpClearMode mode, float ahead, bool whole) {
  if (mode == TFP_CLEAR_OFF || mode == TFP_CLEAR_DOWN || !isfinite(c->push)) { *c = (TfpClear){0}; return 0; }
  float target = 0;
  if (mode == TFP_CLEAR_ON && isfinite(ahead)) {
    if (!c->run) { c->run = true; c->far = ahead < -TFP_CAMERA_CLEAR_FAR_M; c->caught = false; }
    if (!c->far) target = fmaxf(0, TFP_CAMERA_CLEAR_M-ahead);
    if (whole) c->caught = true;
  } else c->run = c->far = c->caught = false; /* off foot: the next run on foot is judged again where it starts */
  if (c->caught) c->push = target;
  else {
    c->push += fmaxf(-TFP_CAMERA_CLEAR_STEP_M, fminf(TFP_CAMERA_CLEAR_STEP_M, target-c->push));
    if (c->run && fabsf(target-c->push) < 1e-6f) c->caught = true;
  }
  if (!(c->push > 1e-6f)) c->push = 0;
  return c->push;
}

/* THE FLOOR. `meant`: how far in front of the head the push meant the eye to be (the eye the frame was built on,
 * tfp_clear_ahead before the push, plus the push; at most TFP_CAMERA_CLEAR_M). Moves `eye` along `h` until it is
 * that far in front of `head`. Returns the move (0: none). */
static inline float tfp_clear_floor(float *eye, const float *head, const float *h, float meant) {
  const float least = fminf(TFP_CAMERA_CLEAR_M, meant), now = tfp_clear_ahead(eye, head, h);
  if (!isfinite(least) || !isfinite(now) || !(now < least)) return 0;
  const float move = least-now;
  eye[0] += h[0]*move; eye[1] += h[1]*move;
  return move;
}
#endif
