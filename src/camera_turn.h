/* Beyond retail, round 49: the direction a frame is shown with, across a change of camera (docs/design/specs/
 * 2026-11-24-residual-jumps.md "Round 49"). No engine reads, no pointers kept.
 *
 * THE DIRECTION. In the native view (camera kind 6) the rotation the game passes is local to the rig eye the camera
 * hangs on; the frame is shown with the rig eye's axes times that argument. In the ordinary camera (kind 4) the
 * argument is the rotation in the world. tfp_turn_world and tfp_turn_forward give the native frame's direction in the
 * world: the same product the Trace row's turn= is written from, and the one the orphaned native frame builds its eye
 * along (src/camera_helper.c orphan_head). Not the camera node's world matrix: in orphaned frames that one was 36 to
 * 117 degrees off what the frames either side were shown with (soaks 60 and 62), and nothing proves it elsewhere.
 *
 * THE CARRY. At a change between kind 6 and kind 4 the game's direction can differ from the one shown the frame before
 * by 26 to 73 degrees, with no mouse input, and stay there (soak 60, 19:34:53.811: 29.0, then 39.5 back into the
 * native view; soak 62, 19:45:06.406: 72.7). Why the game's two cameras point apart is not known (the third-person
 * camera's parallax fits the small cases near geometry and does not fit 73 degrees on open ground). What is done here
 * is the least that takes the cut out of the picture: the difference between the direction last shown and the new one
 * is carried, as a rotation in the view's own frame, and eased out from rest to rest. So:
 *   - the frame of the change is shown the way the frame before it was;
 *   - the view then turns to the game's direction at TFP_TURN_RATE_DEG_S at most (the ease's steepest point), in
 *     TFP_TURN_LEAST_MS at least: 29 degrees take 218 ms, 73 take 548;
 *   - what the game's own direction does meanwhile (the mouse, recoil) goes through whole: the carry is applied on top
 *     of the game's rotation of each frame, it is not a hold;
 *   - a difference under TFP_TURN_SKIP_DEG carries nothing, and a frame nothing is carried in gets the identity back,
 *     bit for bit, which the caller does not multiply by: such frames are main's;
 *   - a difference over TFP_TURN_MOST_DEG is another view (a camera cut the game means) and is shown as it is;
 *   - a second change while a carry is under way starts again from what is shown then.
 * During the ease the picture points off the game's own aim by what is still carried. */
#ifndef TFP_CAMERA_TURN_H
#define TFP_CAMERA_TURN_H
#include "camera_math.h"

#define TFP_TURN_SKIP_DEG 0.5f      /* under this at a change of camera: nothing carried */
#define TFP_TURN_MOST_DEG 120.0f    /* over this: another view, shown as it is */
#define TFP_TURN_RATE_DEG_S 200.0f  /* the ease's steepest point turns no faster */
#define TFP_TURN_LEAST_MS 120.0f    /* and no ease is shorter (the held direction's own ease, LOCK_CARRY_MS) */

/* shown: the rotation last shown, in the world, without the kick; kind, tick: of that frame; known: there is one.
 * from: the carry as the ease began (in the view's own frame: shown = the game's rotation times the carry); started,
 * duration_ms: of the ease; active: one is under way. degrees: what the last change of camera began with (0: nothing
 * was carried), for the log. */
typedef struct TfpTurn { TfpQ shown, from; double tick, started; float duration_ms, degrees; uint32_t kind; bool known, active; } TfpTurn;

static inline void tfp_turn_reset(TfpTurn *t) { memset(t, 0, sizeof *t); t->shown = t->from = tfp_q_identity(); }
static inline bool tfp_turn_none(TfpQ carry) { return carry.x == 0 && carry.y == 0 && carry.z == 0 && carry.w == 1; }
/* How far a rotation turns, degrees (0 to 180). From the vector part: through the arc cosine of w one float step
 * below 1 already reads 0.04 degrees. */
static inline float tfp_turn_degrees(TfpQ q) {
  return 2*atan2f(sqrtf(q.x*q.x+q.y*q.y+q.z*q.z), fabsf(q.w))*180/TFP_PI;
}
/* The rotation of a rig eye from its three axis rows (right, forward, up) in the world. */
static inline bool tfp_turn_basis_q(const float *basis, TfpQ *q) {
  float m[16] = {0};
  for (unsigned a = 0; a < 3; ++a) for (unsigned c = 0; c < 3; ++c) m[a*4+c] = basis[a*3+c];
  m[15] = 1;
  return tfp_matrix_q(m, q);
}
/* THE DIRECTION: the rotation a native frame is shown with, in the world; false where the axes are not a rotation. */
static inline bool tfp_turn_world(const float *basis, TfpQ local, TfpQ *world) {
  TfpQ parent;
  if (!tfp_q_valid(local) || !tfp_turn_basis_q(basis, &parent)) return false;
  *world = tfp_q_normalised(tfp_q_mul(parent, tfp_q_normalised(local)));
  return tfp_q_valid(*world);
}
/* The forward axis of that rotation, in the world: the argument's own forward taken through the axis rows. (The rows
 * are used as they are, as the Trace row always did: no proof of a rotation is asked for.) */
static inline void tfp_turn_forward(const float *basis, TfpQ local, float *forward) {
  const float ahead[3] = {0, 1, 0}; float own[3] = {0, 0, 0};
  tfp_q_rotate(local, ahead, own);
  for (unsigned c = 0; c < 3; ++c) { forward[c] = 0; for (unsigned a = 0; a < 3; ++a) forward[c] += own[a]*basis[a*3+c]; }
}
/* How long the ease of `degrees` takes: rest to rest (x*x*(3-2x), steepest at 1.5 times the mean), so that its
 * steepest point is TFP_TURN_RATE_DEG_S. */
static inline float tfp_turn_duration_ms(float degrees) {
  const float ms = 1.5f*degrees/TFP_TURN_RATE_DEG_S*1000;
  return ms > TFP_TURN_LEAST_MS ? ms : TFP_TURN_LEAST_MS;
}
/* What is carried `passed_ms` into an ease of `duration_ms` that began with `from`. */
static inline TfpQ tfp_turn_carry_at(TfpQ from, float passed_ms, float duration_ms) {
  if (!(duration_ms > 0) || !(passed_ms < duration_ms)) return tfp_q_identity();
  const float x = passed_ms > 0 ? passed_ms/duration_ms : 0;
  const TfpQ carry = tfp_slerp(tfp_q_identity(), from, 1-x*x*(3-2*x));
  return tfp_q_valid(carry) ? carry : tfp_q_identity();
}
/* One frame the lock shows with the game's own direction. `game`: the rotation the game means in the world (kind 4:
 * its argument; kind 6: tfp_turn_world), used only where `game_known`; a frame that does not know it carries on with
 * an ease under way and is no start for the next change. `kind`: 4 or 6. Returns the carry: the rotation to apply
 * after the game's own, in the view's frame (argument * carry), exactly the identity where nothing is carried.
 * `began`, when given: the degrees a change of camera in this frame began an ease with, else 0. */
static inline TfpQ tfp_turn_step(TfpTurn *t, TfpQ game, bool game_known, uint32_t kind, double tick, uint32_t max_gap_ms, float *began) {
  if (began) *began = 0;
  const bool near = t->known && tick >= t->tick && tick-t->tick <= max_gap_ms;
  if (t->active && !(tick >= t->tick && tick-t->tick <= max_gap_ms)) t->active = false; /* a pause: nothing is carried over it */
  if (game_known && near && kind != t->kind && (kind == 4 || kind == 6) && (t->kind == 4 || t->kind == 6)) {
    const TfpQ apart = tfp_q_normalised(tfp_q_mul(tfp_q_inv(game), t->shown));
    const float degrees = tfp_turn_degrees(apart);
    if (tfp_q_valid(apart) && degrees >= TFP_TURN_SKIP_DEG && degrees <= TFP_TURN_MOST_DEG) {
      t->from = apart.w < 0 ? tfp_q(-apart.x, -apart.y, -apart.z, -apart.w) : apart; t->started = tick;
      t->duration_ms = tfp_turn_duration_ms(degrees); t->degrees = degrees; t->active = true;
      if (began) *began = degrees;
    } else { t->active = false; t->degrees = 0; }
  }
  TfpQ carry = tfp_q_identity();
  if (t->active) {
    carry = tfp_turn_carry_at(t->from, (float)(tick-t->started), t->duration_ms);
    if (tfp_turn_none(carry) || tfp_turn_degrees(carry) < .02f) { carry = tfp_q_identity(); t->active = false; }
  }
  if (game_known) { t->shown = tfp_turn_none(carry) ? game : tfp_q_normalised(tfp_q_mul(game, carry)); t->known = tfp_q_valid(t->shown); }
  else t->known = false;
  t->kind = kind; t->tick = tick;
  return carry;
}
/* The argument with the carry on it; the argument itself, bit for bit, where nothing is carried. */
static inline TfpQ tfp_turn_apply(TfpQ argument, TfpQ carry) {
  return tfp_turn_none(carry) ? argument : tfp_q_normalised(tfp_q_mul(argument, carry));
}
#endif
