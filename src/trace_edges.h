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
#endif
