/* When the binder opens a Trace window and when it tells the continuity to look for jumps (src/trace_edges.h). */
#include "trace_edges.h"
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
enum { IDLE = 0, SPRINT = 3, DIVE = 9, NONE = -1 };
int main(void) {
  TfpTraceEdges e = {0};
  /* Nothing leased, nothing held, no action known: nothing. */
  CHECK(tfp_trace_edges_step(&e, false, false, NONE) == 0);
  /* A lease or a hold begun or ended: the rig may be re-seated (the continuity is told) and a window is opened. As
   * it was before the action was looked at. */
  CHECK(tfp_trace_edges_step(&e, true, false, NONE) == (TFP_TRACE_EDGE_EVENT | TFP_TRACE_EDGE_WINDOW));
  CHECK(tfp_trace_edges_step(&e, true, false, NONE) == 0);
  CHECK(tfp_trace_edges_step(&e, true, true, NONE) == (TFP_TRACE_EDGE_EVENT | TFP_TRACE_EDGE_WINDOW));
  CHECK(tfp_trace_edges_step(&e, true, false, NONE) == (TFP_TRACE_EDGE_EVENT | TFP_TRACE_EDGE_WINDOW));
  /* The first action known is no change of action. */
  CHECK(tfp_trace_edges_step(&e, true, false, IDLE) == 0);
  CHECK(tfp_trace_edges_step(&e, true, false, IDLE) == 0);
  /* The action changes with the lease as it is (a dive begun in the native view: the Stable soak of 2026-11-24 had
   * 32 dives, and 23 of them had no traced frame at all): a window, and the continuity is NOT told. A dive is the
   * eye's own motion; looking for jumps in it is a change of what the player sees, and this is the log only. */
  CHECK(tfp_trace_edges_step(&e, true, false, DIVE) == TFP_TRACE_EDGE_WINDOW);
  CHECK(tfp_trace_edges_step(&e, true, false, DIVE) == 0);
  CHECK(tfp_trace_edges_step(&e, true, false, IDLE) == TFP_TRACE_EDGE_WINDOW);
  /* While the action is not known (the motion runtime keeps its last one without a lease) nothing is said, and the
   * action known before is kept: the same one again afterwards is no change. */
  CHECK(tfp_trace_edges_step(&e, false, false, NONE) == (TFP_TRACE_EDGE_EVENT | TFP_TRACE_EDGE_WINDOW));
  CHECK(tfp_trace_edges_step(&e, false, false, NONE) == 0);
  CHECK(tfp_trace_edges_step(&e, true, false, IDLE) == (TFP_TRACE_EDGE_EVENT | TFP_TRACE_EDGE_WINDOW));
  CHECK(tfp_trace_edges_step(&e, true, false, IDLE) == 0);
  /* Both in one tick: both bits, once. */
  CHECK(tfp_trace_edges_step(&e, true, true, SPRINT) == (TFP_TRACE_EDGE_EVENT | TFP_TRACE_EDGE_WINDOW));
  CHECK(tfp_trace_edges_step(&e, true, true, SPRINT) == 0);
  /* Forgotten (the view switched off): the next action known is the first again. */
  tfp_trace_edges_reset(&e);
  CHECK(tfp_trace_edges_step(&e, false, false, DIVE) == 0);
  printf("trace edges: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
