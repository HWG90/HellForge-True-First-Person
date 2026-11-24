/* When the binder opens a Trace window and when it tells the continuity to look for jumps (src/trace_edges.h). */
#include "trace_edges.h"
#include <math.h>
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
  /* The window the eye opens itself (round 13): how much the presented eye's step changed from one frame
   * to the next, as a vector; 0 until there are three frames no further apart than the gap. */
  {
    TfpTraceSelf s = {0};
    const float still[3] = {.1f, .2f, 1.6f}, out[3] = {.1f, .9f, 1.6f}, on[3] = {.1f, 1.18f, 1.6f};
    CHECK(tfp_trace_self_step(&s, still, 1000, 250) == 0 && tfp_trace_self_step(&s, still, 1033, 250) == 0);
    CHECK(tfp_trace_self_step(&s, still, 1066, 250) == 0);
    /* a melee as the soak of 2026-11-24 had it: the eye stands, then 0.7 m and 0.28 m along the view */
    float jump = tfp_trace_self_step(&s, out, 1100, 250);
    CHECK(jump > .699f && jump < .701f && jump >= TFP_TRACE_SELF_M);
    jump = tfp_trace_self_step(&s, on, 1133, 250);
    CHECK(jump > .419f && jump < .421f);
    /* an even glide is no jump, whatever its speed; nor is its first frame after a pause */
    TfpTraceSelf g = {0};
    for (int i = 0; i < 6; i++) { const float at[3] = {.3f * (float)i, 0, 0}; CHECK(tfp_trace_self_step(&g, at, 2000 + 33.0 * i, 250) < .001f); }
    const float far[3] = {9, 9, 9}, further[3] = {9, 9.5f, 9};
    CHECK(tfp_trace_self_step(&g, far, 2000 + 33.0 * 5 + 400, 250) == 0); /* a pause: the frames before it are not neighbours */
    CHECK(tfp_trace_self_step(&g, further, 2000 + 33.0 * 5 + 433, 250) == 0); /* two frames only since */
    /* a step back of the same length is a jump (the vector, not the lengths) */
    TfpTraceSelf b = {0};
    const float p0[3] = {0, 0, 0}, p1[3] = {.1f, 0, 0};
    tfp_trace_self_step(&b, p0, 0, 250); tfp_trace_self_step(&b, p1, 33, 250);
    jump = tfp_trace_self_step(&b, p0, 66, 250);
    CHECK(jump > .199f && jump < .201f);
    /* not a number: nothing, and forgotten */
    const float bad[3] = {0, NAN, 0};
    CHECK(tfp_trace_self_step(&b, bad, 99, 250) == 0 && tfp_trace_self_step(&b, p0, 132, 250) == 0 && tfp_trace_self_step(&b, p1, 165, 250) == 0);
  }
  printf("trace edges: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
