/* Continuity of the presented eye (src/camera_continuity.h): what is taken up, what is left alone, how the
 * correction ends. The figures in the comments are the game's, from the feel.22 session of 2026-11-22. */
#include "camera_continuity.h"
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static const TfpContinuityTuning T = TFP_CONTINUITY_DEFAULT;
enum { HEAD = 1, NATIVE = 2 };
typedef struct Run { TfpContinuity c; double tick; float shown[3], step, change, correction, travelled; bool first; } Run;
static void start(Run *r) { memset(r, 0, sizeof *r); r->first = true; }
/* One frame; keeps the largest step of the presented eye and the largest change of that step (what "Cut:" measures). */
static void frame(Run *r, float x, float y, float z, uint32_t source, bool detect, bool quiet, float dt_ms) {
  const float wanted[3] = {x, y, z}; float out[3];
  r->tick += dt_ms;
  r->correction = tfp_continuity_step(&r->c, wanted, source, detect, quiet, r->tick, 250, &T, out);
  if (!r->first) {
    const float d[3] = {out[0]-r->shown[0], out[1]-r->shown[1], out[2]-r->shown[2]};
    const float step = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    const float change = fabsf(step-r->step);
    if (step > r->travelled) r->travelled = step;
    if (change > r->change) r->change = change;
    r->step = step;
  }
  r->first = false; memcpy(r->shown, out, sizeof out);
}
int main(void) {
  Run r;
  /* A still eye and an eye moving evenly: nothing is added, at 30 and at 144 frames a second. */
  for (int rate = 0; rate < 2; ++rate) {
    const float dt = rate ? 1000.0f/144 : 1000.0f/30;
    start(&r);
    for (int i = 0; i < 60; ++i) { frame(&r, 1, 2, 3, NATIVE, true, false, dt); CHECK(r.correction == 0); }
    start(&r);
    for (int i = 0; i < 60; ++i) { frame(&r, 3.0f*i*dt/1000, 0, 0, NATIVE, true, false, dt); CHECK(r.correction == 0); }
    /* Head bob while sprinting (3 cm at 3 Hz) and a dive's start (0 to 3 m/s over 150 ms): the eye's own motion. */
    start(&r);
    for (int i = 0; i < 90; ++i) { frame(&r, 0, 0, .03f*sinf(6.2832f*3*i*dt/1000), NATIVE, true, false, dt); CHECK(r.correction == 0); }
    start(&r);
    for (int i = 0; i < 60; ++i) { const float t = i*dt/1000, v = t < .15f ? 20*t*t/2 : 20*.15f*.15f/2+3*(t-.15f);
      frame(&r, v, 0, 0, NATIVE, true, false, dt); CHECK(r.correction == 0); }
  }
  /* The rig re-seated in one frame (0.218 m, the swap's third frame): the eye does not move in that frame and
   * reaches the rig's eye within the longest catch-up; at 30 frames a second no step changes by 0.03 m. */
  start(&r);
  for (int i = 0; i < 10; ++i) frame(&r, 0, 16.820f, 0, NATIVE, true, false, 33.3f);
  frame(&r, 0, 17.038f, 0, NATIVE, true, false, 33.3f);
  CHECK(fabsf(r.shown[1]-16.820f) < 1e-4f && fabsf(r.correction-.218f) < 1e-4f && r.c.jumps == 1);
  for (int i = 0; i < 15; ++i) frame(&r, 0, 17.038f, 0, NATIVE, true, false, 33.3f);
  CHECK(fabsf(r.shown[1]-17.038f) < 1e-4f && r.correction == 0 && !r.c.active);
  CHECK(r.change < .03f && r.travelled < .06f);
  /* Two frames in a row (0.39 m then 0.216 m, the native view after its gun was destroyed): one glide. */
  start(&r);
  for (int i = 0; i < 10; ++i) frame(&r, 0, 17.038f, 8.930f, NATIVE, true, false, 33.3f);
  frame(&r, 0, 17.421f, 8.840f, NATIVE, true, false, 33.3f);
  frame(&r, 0, 17.636f, 8.821f, NATIVE, true, false, 33.3f);
  CHECK(r.c.jumps == 2 && r.travelled < .02f);
  for (int i = 0; i < 14; ++i) frame(&r, 0, 17.636f, 8.821f, NATIVE, true, false, 33.3f);
  CHECK(fabsf(r.shown[1]-17.636f) < 1e-4f && r.correction == 0);
  CHECK(r.change < .03f);
  /* The same at 90 frames a second: the measure scales with the frame time squared. */
  start(&r);
  for (int i = 0; i < 10; ++i) frame(&r, 0, 17.038f, 8.930f, NATIVE, true, false, 11.1f);
  frame(&r, 0, 17.421f, 8.840f, NATIVE, true, false, 11.1f);
  for (int i = 0; i < 3; ++i) frame(&r, 0, 17.421f, 8.840f, NATIVE, true, false, 11.1f);
  frame(&r, 0, 17.636f, 8.821f, NATIVE, true, false, 11.1f);
  for (int i = 0; i < 45; ++i) frame(&r, 0, 17.636f, 8.821f, NATIVE, true, false, 11.1f);
  CHECK(fabsf(r.shown[1]-17.636f) < 1e-4f && r.change < .005f);
  /* A change of camera takes up any difference, small or large; 5 cm are gone within 110 ms. */
  start(&r);
  for (int i = 0; i < 10; ++i) frame(&r, 0, 0, 1.6f, HEAD, false, false, 16.7f);
  frame(&r, .03f, .04f, 1.6f, NATIVE, true, false, 16.7f);
  CHECK(fabsf(r.shown[0]) < 1e-4f && fabsf(r.shown[1]) < 1e-4f && r.c.edges == 1 && r.c.jumps == 0);
  for (int i = 0; i < 7; ++i) frame(&r, .03f, .04f, 1.6f, NATIVE, true, false, 16.7f);
  CHECK(r.correction == 0 && fabsf(r.shown[0]-.03f) < 1e-4f && r.change < .008f);
  /* Out and back in one frame (the game's request replaced for one frame, the rig standing still while the body
   * moves on, then the new request): the eye stays within 2 cm of its way. */
  start(&r);
  for (int i = 0; i < 10; ++i) frame(&r, 0, 0, 1.5f, NATIVE, true, false, 33.3f);
  frame(&r, 0, -.3f, 1.5f, NATIVE, true, false, 33.3f);
  frame(&r, 0, 0, 1.5f, NATIVE, true, false, 33.3f);
  for (int i = 0; i < 10; ++i) frame(&r, 0, 0, 1.5f, NATIVE, true, false, 33.3f);
  CHECK(r.travelled < .02f && r.change < .02f && r.correction == 0);
  /* A jump while the eye is moving (2 m/s before and after): it keeps moving at 2 m/s and catches up. */
  start(&r);
  float x = 0;
  for (int i = 0; i < 12; ++i) { x += 2*.0333f; frame(&r, x, 0, 0, NATIVE, true, false, 33.3f); }
  r.change = 0; /* the run's own first step from rest is not the subject */
  x += 2*.0333f+.25f; frame(&r, x, 0, 0, NATIVE, true, false, 33.3f);
  CHECK(fabsf(r.step-2*.0333f) < .002f && fabsf(r.correction-.25f) < .002f);
  for (int i = 0; i < 14; ++i) { x += 2*.0333f; frame(&r, x, 0, 0, NATIVE, true, false, 33.3f); }
  CHECK(r.correction == 0 && fabsf(r.shown[0]-x) < 1e-4f && r.change < .03f);
  /* The eye stopping dead from 2 m/s is a new speed, followed, not a jump; so is its slowing down. */
  start(&r);
  x = 0;
  for (int i = 0; i < 12; ++i) { x += 2*.0333f; frame(&r, x, 0, 0, NATIVE, true, false, 33.3f); }
  for (int i = 0; i < 6; ++i) { frame(&r, x, 0, 0, NATIVE, true, false, 33.3f); CHECK(r.correction == 0); }
  /* A sustained fast start (0 to 3 m/s in one frame) is held back for three frames at most, then followed. */
  start(&r);
  for (int i = 0; i < 10; ++i) frame(&r, 0, 0, 0, NATIVE, true, false, 33.3f);
  x = 0;
  for (int i = 0; i < 4; ++i) { x += 3*.0333f; frame(&r, x, 0, 0, NATIVE, true, false, 33.3f); }
  CHECK(r.c.jumps == 3 && r.correction < .31f);
  for (int i = 0; i < 14; ++i) { x += 3*.0333f; frame(&r, x, 0, 0, NATIVE, true, false, 33.3f); }
  CHECK(r.c.jumps == 3 && r.correction == 0 && fabsf(r.shown[0]-x) < 1e-4f);
  /* The head camera's eye turns about the head with the view: within that source nothing is looked for. */
  start(&r);
  for (int i = 0; i < 10; ++i) frame(&r, .18f, 0, 1.6f, HEAD, false, false, 33.3f);
  frame(&r, .18f*cosf(.6f), .18f*sinf(.6f), 1.6f, HEAD, false, false, 33.3f);
  CHECK(r.correction == 0 && r.c.jumps == 0);
  /* Farther than 1.5 m is a real teleport: shown at once, nothing carried. More than 1 m is not carried in full. */
  start(&r);
  for (int i = 0; i < 5; ++i) frame(&r, 0, 0, 0, NATIVE, true, false, 33.3f);
  frame(&r, 2, 0, 0, NATIVE, true, false, 33.3f);
  CHECK(r.correction == 0 && r.shown[0] == 2 && r.c.restarts == 1);
  frame(&r, 2, 0, 0, HEAD, false, false, 33.3f);
  frame(&r, 2, 1.3f, 0, NATIVE, true, false, 33.3f);
  CHECK(fabsf(r.correction-1) < 1e-4f && fabsf(r.shown[1]-.3f) < 1e-4f);
  /* A pause longer than the helper's frame gap, and a value that is not finite: started again. */
  start(&r);
  for (int i = 0; i < 5; ++i) frame(&r, 0, 0, 0, NATIVE, true, false, 33.3f);
  frame(&r, .3f, 0, 0, NATIVE, true, false, 300);
  CHECK(r.correction == 0 && r.shown[0] == .3f);
  frame(&r, NAN, 0, 0, NATIVE, true, false, 33.3f);
  CHECK(r.correction == 0 && !r.c.known);
  frame(&r, .9f, 0, 0, NATIVE, true, false, 33.3f);
  CHECK(r.correction == 0 && r.shown[0] == .9f);
  /* Full aim: nothing is taken up, and what is carried is gone within 80 ms. */
  start(&r);
  for (int i = 0; i < 5; ++i) frame(&r, 0, 0, 0, NATIVE, true, false, 16.7f);
  frame(&r, .5f, 0, 0, NATIVE, true, false, 16.7f);
  CHECK(fabsf(r.correction-.5f) < 1e-4f);
  for (int i = 0; i < 5; ++i) frame(&r, .5f, 0, 0, NATIVE, true, true, 16.7f);
  CHECK(r.correction < .05f);
  frame(&r, .5f, 0, 0, NATIVE, true, true, 16.7f);
  CHECK(r.correction == 0);
  frame(&r, .8f, 0, 0, NATIVE, true, true, 16.7f);
  CHECK(r.correction == 0 && r.shown[0] == .8f);
  frame(&r, .8f, .4f, 0, HEAD, false, true, 16.7f);
  CHECK(r.correction == 0);
  /* The same instant twice gives the same eye. */
  start(&r);
  for (int i = 0; i < 5; ++i) frame(&r, 0, 0, 0, NATIVE, true, false, 33.3f);
  frame(&r, .2f, 0, 0, NATIVE, true, false, 33.3f);
  const float once = r.shown[0];
  frame(&r, .2f, 0, 0, NATIVE, true, false, 0);
  CHECK(r.shown[0] == once);
  printf("camera continuity: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
