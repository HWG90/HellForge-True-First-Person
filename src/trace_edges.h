/* When the binder opens a window of "Trace:" lines (the frames just before and the half second after: src/mod_view.c
 * trace_note, src/camera_helper.h tfp_camera_trace_edge), and when it tells the continuity that the rig may be
 * re-seated (tfp_camera_continuity_event). The arithmetic only.
 *
 * A lease or a hold begun or ended does both, as before this file. A change of the body's action (the motion
 * runtime's: idle, sprint, crouch, prone, dive, ...) opens a window and does nothing else: until round 10 (2026-11-24)
 * an action that began and ended inside the native view left no traced frame (the Stable soak of that day: 23 of 32
 * dives, 28 of 36 in the Bodycam soak), and a landing was judged only when a State line happened to fall near it.
 * The continuity is not told: a dive is the eye's own motion, and to look for jumps in it changes what is shown. */
#ifndef TFP_TRACE_EDGES_H
#define TFP_TRACE_EDGES_H
#include <math.h>
#include <stdbool.h>

enum { TFP_TRACE_EDGE_EVENT = 1,   /* tell the continuity: the rig may be re-seated */
       TFP_TRACE_EDGE_WINDOW = 2   /* open a window of Trace lines */ };
/* action: the last action that was known; has_action: one was */
typedef struct TfpTraceEdges { bool leased, holding, has_action; int action; } TfpTraceEdges;
static inline void tfp_trace_edges_reset(TfpTraceEdges *e) { e->leased = e->holding = e->has_action = false; e->action = 0; }
/* Once a tick. `action`: the body's action where the motion runtime is sampling it (its phase active), a negative
 * number where it is not known; the runtime keeps its last sampled action while there is no lease, and that is not
 * an action of this tick. Returns TFP_TRACE_EDGE_* bits. */
static inline unsigned tfp_trace_edges_step(TfpTraceEdges *e, bool leased, bool holding, int action) {
  unsigned out = leased != e->leased || holding != e->holding ? TFP_TRACE_EDGE_EVENT | TFP_TRACE_EDGE_WINDOW : 0u;
  e->leased = leased; e->holding = holding;
  if (action >= 0) {
    if (e->has_action && action != e->action) out |= TFP_TRACE_EDGE_WINDOW;
    e->action = action; e->has_action = true;
  }
  return out;
}

/* A window the eye opens itself (round 13, 2026-11-24). An action that neither changes the body's action nor writes a
 * State line left no traced frame: 16 of 17 melees in the Stable soak of that day (the eye 0.7 m in a frame), and
 * both seconds in which it went 1.6 m back and forth. So the camera helper also opens a window in the frame in which
 * the eye it presents changes its step by TFP_TRACE_SELF_M or more, as a vector in the world: the judge's "shown"
 * (tools/harness/matrix.py measure). Not against the body root: a root the game re-seats by itself under an eye that
 * goes on is no jump of the eye (tests/camera_smooth_test.cpp scenario 18). A run or a fall changes its step by far
 * less in a frame; a teleport opens one, and may. The log only: nothing that is shown asks this.
 * At 30 frames a second, where the harness runs. A faster game spreads the same move over more frames, and each
 * changes the step by less: at 120 a second only a move of about 2.4 m in 33 ms would open a window.
 * at: the presented eye in the world. Returns the change of step in metres; 0 until three frames
 * no further apart than max_gap_ms are known. */
#define TFP_TRACE_SELF_M 0.15f
typedef struct TfpTraceSelf { float at[2][3]; double tick; unsigned known; } TfpTraceSelf; /* at[1]: the frame before; at[0]: the one before that */
static inline float tfp_trace_self_step(TfpTraceSelf *s, const float *at, double tick, double max_gap_ms) {
  float jump = 0;
  if (!(at[0] == at[0] && at[1] == at[1] && at[2] == at[2])) { s->known = 0; return 0; } /* not a number: forgotten */
  if (s->known && !(tick >= s->tick && tick - s->tick <= max_gap_ms)) s->known = 0;
  if (s->known >= 2) {
    float sum = 0;
    for (int i = 0; i < 3; i++) { const float d = (at[i] - s->at[1][i]) - (s->at[1][i] - s->at[0][i]); sum += d * d; }
    jump = sum > 0 ? sqrtf(sum) : 0;
    if (!(jump == jump)) jump = 0;
  }
  for (int i = 0; i < 3; i++) { s->at[0][i] = s->at[1][i]; s->at[1][i] = at[i]; }
  s->tick = tick;
  if (s->known < 2) s->known++;
  return jump;
}
#endif
