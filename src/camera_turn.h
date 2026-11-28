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
 *   - a second change while a carry is under way starts again from what is shown then;
 *   - THE PLAYER'S OWN TURN is not part of the difference. Into the native view the frame before is a whole frame
 *     earlier (33 ms in every logged change; out of it usually 0 to 1 ms apart, but 15 of ~660 logged 6 -> 4 changes are 5
 *     to 34 ms apart, three a whole 33.6 ms frame), so the
 *     game's new direction is off the one last shown by one frame of mouse turn as well, and carrying that froze a pan
 *     for a frame (100 degrees a second at 16.7 ms: 1.67, 0.00, 1.76, 1.90 degrees a frame where main turns 1.67 in
 *     each). So at a change either way the direction last shown is first advanced by the turn the game's
 *     own direction made over the ordinary camera's last frame, about the world's axes, in proportion to the time
 *     since; the difference is taken from there and the skip applies to what is left. A pan through agreeing cameras
 *     carries nothing; with the cameras 29 degrees apart the frame of the change is shown where the pan would have
 *     taken the last one and the 29 degrees ease out under the pan. What cannot be told apart is a turn that changes
 *     speed in that very frame: the change of speed times the frame is left over (a pan that stops dead leaves one
 *     frame of it, as much as before), and is carried when it is over the skip;
 *   - nothing is carried in a frame of full aim (tfp_turn_drop): the sights stay on the eye. A carry under way ends
 *     there, in one frame.
 * During the ease the picture points off the game's own aim by what is still carried. */
#ifndef TFP_CAMERA_TURN_H
#define TFP_CAMERA_TURN_H
#include "camera_math.h"

#define TFP_TURN_SKIP_DEG 0.5f      /* under this at a change of camera: nothing carried */
#define TFP_TURN_MOST_DEG 120.0f    /* over this: another view, shown as it is */
#define TFP_TURN_RATE_DEG_S 200.0f  /* the ease's steepest point turns no faster */
#define TFP_TURN_LEAST_MS 120.0f    /* and no ease is shorter (the held direction's own ease, LOCK_CARRY_MS) */
#define TFP_TURN_FRAME_MS 4.0       /* calls closer than this are one game frame (a camera set twice in a frame) */
#define TFP_TURN_AHEAD_MOST 4.0f    /* the player's own turn is carried forward over this many of its frames at most */
#define TFP_TURN_OWN_LEAST_DEG 0.01f /* a turn under this over a frame is a still view: the direction is not advanced */

/* shown: the rotation last shown, in the world, without the kick; kind, tick: of that frame; known: there is one.
 * from: the carry as the ease began (in the view's own frame: shown = the game's rotation times the carry); started,
 * duration_ms: of the ease; active: one is under way. degrees: what the last change of camera began with (0: nothing
 * was carried), for the log. game: the game's own direction in that frame (where known); prior, prior_tick: the
 * game's own direction a frame of the same camera earlier (prior_known), which the player's own turn is read from. */
typedef struct TfpTurn { TfpQ shown, from, game, prior; double tick, started, prior_tick; float duration_ms, degrees; uint32_t kind; bool known, active, prior_known; } TfpTurn;

static inline void tfp_turn_reset(TfpTurn *t) { memset(t, 0, sizeof *t); t->shown = t->from = t->game = t->prior = tfp_q_identity(); }
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
/* The game's own direction of this frame, kept for the next one: the frame before stays the `prior` while the camera
 * is the same and its direction known; a second call within one game frame replaces the first. Called with the old
 * frame still in t->kind, t->tick and `was_known`. */
static inline void tfp_turn_note(TfpTurn *t, TfpQ game, bool game_known, bool was_known, uint32_t kind, double tick) {
  if (!(game_known && was_known && kind == t->kind && tick >= t->tick)) t->prior_known = false;
  else if (tick-t->tick >= TFP_TURN_FRAME_MS) { t->prior = t->game; t->prior_tick = t->tick; t->prior_known = true; }
  if (game_known) t->game = game;
}
/* THE PLAYER'S OWN TURN: the direction last shown, advanced to `tick` by the turn the game's own direction made over
 * the last frame of the camera it was shown in. The direction itself, bit for bit, where there is no such frame or the
 * view stood still in it. */
static inline TfpQ tfp_turn_ahead(const TfpTurn *t, double tick, uint32_t max_gap_ms) {
  const double span = t->tick-t->prior_tick;
  if (!t->prior_known || !(span >= TFP_TURN_FRAME_MS) || !(span <= max_gap_ms) || !(tick > t->tick)) return t->shown;
  TfpQ own = tfp_q_normalised(tfp_q_mul(t->game, tfp_q_inv(t->prior))); /* about the world's axes: a pan is about up */
  if (own.w < 0) own = tfp_q(-own.x, -own.y, -own.z, -own.w);
  const float length = sqrtf(own.x*own.x+own.y*own.y+own.z*own.z), degrees = tfp_turn_degrees(own);
  if (!tfp_q_valid(own) || !(degrees >= TFP_TURN_OWN_LEAST_DEG) || !(length > 0)) return t->shown;
  float part = (float)((tick-t->tick)/span);
  if (part > TFP_TURN_AHEAD_MOST) part = TFP_TURN_AHEAD_MOST;
  const float half = atan2f(length, own.w)*part, s = sinf(half)/length;
  const TfpQ ahead = tfp_q_normalised(tfp_q_mul(tfp_q(own.x*s, own.y*s, own.z*s, cosf(half)), t->shown));
  return tfp_q_valid(ahead) ? ahead : t->shown;
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
    /* the frame before may be a frame earlier, either way (into the native view always; out of it usually 0-1 ms, but
     * the logs hold 6 -> 4 changes a whole frame apart): the player's own turn since is not the cameras'. The advance
     * scales with the time since that frame, so a change 0-1 ms apart is advanced by almost nothing. */
    const TfpQ last = tfp_turn_ahead(t, tick, max_gap_ms);
    const TfpQ apart = tfp_q_normalised(tfp_q_mul(tfp_q_inv(game), last));
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
  const bool was_known = t->known;
  if (game_known) { t->shown = tfp_turn_none(carry) ? game : tfp_q_normalised(tfp_q_mul(game, carry)); t->known = tfp_q_valid(t->shown); }
  else t->known = false;
  tfp_turn_note(t, game, t->known, was_known, kind, tick);
  t->kind = kind; t->tick = tick;
  return carry;
}
/* Round 55, THE DIRECTION SHOWN: the carry tfp_turn_step will give for this frame, told before the frame is played and
 * with the state left as it is, so the head camera's eye (and the push, the veil, head_shown) can be built along the
 * direction that will be shown (src/camera_helper.c lock_turn_preview). The same to the bit as that step, given the
 * same arguments and no step between. */
static inline TfpQ tfp_turn_peek(const TfpTurn *t, TfpQ game, bool game_known, uint32_t kind, double tick, uint32_t max_gap_ms) {
  TfpTurn ahead = *t;
  return tfp_turn_step(&ahead, game, game_known, kind, tick, max_gap_ms, NULL);
}
/* A frame that carries nothing by rule (full aim: the sights stay on the eye, as everywhere in the lock): shown with
 * the game's own direction. A carry under way ends, none begins, and the frame is the start for the next change. */
static inline void tfp_turn_drop(TfpTurn *t, TfpQ game, bool game_known, uint32_t kind, double tick) {
  const bool was_known = t->known;
  t->active = false; t->degrees = 0;
  t->shown = game; t->known = game_known && tfp_q_valid(game);
  tfp_turn_note(t, game, t->known, was_known, kind, tick);
  t->kind = kind; t->tick = tick;
}
/* The argument with the carry on it; the argument itself, bit for bit, where nothing is carried. */
static inline TfpQ tfp_turn_apply(TfpQ argument, TfpQ carry) {
  return tfp_turn_none(carry) ? argument : tfp_q_normalised(tfp_q_mul(argument, carry));
}
#endif
