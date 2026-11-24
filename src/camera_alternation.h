/* THE ALTERNATION, watched only. A wanted eye that goes back and forth between two places every frame is no motion
 * (docs/design/specs/2026-11-23-gate-flip.md: the game's unzoom gate flipping moves the rig eye about 0.2 m along the
 * view each frame, for seconds, standing). This header only says whether a frame belongs to such a going back and
 * forth. Nothing is held, moved or smoothed by it: the helper counts the frames for the "Alternation:" line.
 *
 * The rule (the detection of the guard tried in ~/.cache/tfp-altdiag/proto, without its wrapper round the filter):
 *  - a move is a frame whose eye is at least TFP_ALTERNATION_STEP from the place the eye was at;
 *  - a move is a return when it ends within TFP_ALTERNATION_BACK of its own length of the place the eye left in the
 *    move before, and that move was no more than TFP_ALTERNATION_HOLD_MS earlier;
 *  - from the second return in a row (TFP_ALTERNATION_RETURNS) the frames count, until a move that is no return or
 *    TFP_ALTERNATION_HOLD_MS without a move; never in full aim (quiet);
 *  - another camera, time going backwards, a pause longer than the helper's frame gap or a value that is not finite
 *    starts again; the same instant again (a frame the game sets twice) changes nothing.
 * Not seen by it: three places in turn, places that last longer than the hold, moves under the step, a return that
 * misses by more than a quarter of the move. */
#ifndef TFP_CAMERA_ALTERNATION_H
#define TFP_CAMERA_ALTERNATION_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TFP_ALTERNATION_STEP .05f
#define TFP_ALTERNATION_BACK .25f
#define TFP_ALTERNATION_RETURNS 2u
#define TFP_ALTERNATION_HOLD_MS 70.0

typedef struct TfpAlternation {
  bool known; uint32_t source, spots, returns; double tick, moved; /* moved: the time of the last move */
  float spot[2][3];                                                /* where the eye is; where it was before its last move */
} TfpAlternation;

static inline float tfp_alternation_apart(const float *a, const float *b) {
  const float d[3] = {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
  return sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}
/* One frame's wanted eye (in the body root's frame, as the continuity is given it). True: the frame belongs to a going
 * back and forth (the guard would hold it). `*move`: how far the eye moved in this frame if it was a move, else 0. */
static inline bool tfp_alternation_step(TfpAlternation *a, const float *wanted, uint32_t source, bool quiet, double tick, uint32_t max_gap_ms, float *move) {
  const bool fine = isfinite(wanted[0]) && isfinite(wanted[1]) && isfinite(wanted[2]) && isfinite(tick);
  *move = 0;
  if (!fine || !a->known || source != a->source || tick < a->tick || tick-a->tick > max_gap_ms) {
    memset(a, 0, sizeof *a);
    if (fine) { a->known = true; a->spots = 1; a->moved = tick; a->tick = tick; a->source = source; memcpy(a->spot[0], wanted, sizeof a->spot[0]); }
    return false;
  }
  if (tick > a->tick) {
    const float far = tfp_alternation_apart(wanted, a->spot[0]);
    if (far >= TFP_ALTERNATION_STEP) {
      const bool back = a->spots >= 2 && tick-a->moved <= TFP_ALTERNATION_HOLD_MS && tfp_alternation_apart(wanted, a->spot[1]) <= TFP_ALTERNATION_BACK*far;
      a->returns = back ? a->returns+1 : 0;
      memcpy(a->spot[1], a->spot[0], sizeof a->spot[0]); memcpy(a->spot[0], wanted, sizeof a->spot[0]); a->spots = 2; a->moved = tick;
      *move = far;
    } else if (tick-a->moved > TFP_ALTERNATION_HOLD_MS) a->returns = 0;
  }
  a->tick = tick;
  return !quiet && a->returns >= TFP_ALTERNATION_RETURNS;
}
#endif
