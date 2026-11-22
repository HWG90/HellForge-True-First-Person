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
  /* A sustained change of speed where jumps are looked for (0 to 3 m/s, 0 to 6 m/s, 2.5 to 0 and back to 2.5 m/s
   * sideways): taken up for 70 ms at most, at any frame rate; in the frame the new speed is believed the presented
   * eye keeps its speed (no step change beyond the curve's own), and it has caught up within the longest catch-up. */
  for (int rate = 0; rate < 3; ++rate) for (int fast = 0; fast < 2; ++fast) {
    const float dt = rate == 0 ? 1000.0f/30 : rate == 1 ? 1000.0f/60 : 1000.0f/144, v = fast ? 6.0f : 3.0f;
    start(&r);
    for (int i = 0; i < 10; ++i) frame(&r, 0, 0, 0, NATIVE, true, false, dt);
    x = 0; float held = 0; int taken = 0;
    for (int i = 0; i < (int)(600/dt); ++i) { x += v*dt/1000; const uint64_t before = r.c.jumps; frame(&r, x, 0, 0, NATIVE, true, false, dt);
      if (r.c.jumps != before) { ++taken; held = (i+1)*dt; } }
    /* (below the limit of 0.04 m in a frame nothing is taken up at all: 3 m/s at 144 frames a second) */
    CHECK(v*dt/1000 < T.pop_m ? taken == 0 : (taken >= 1 && held <= 70+dt && held > 70-dt));
    CHECK(r.correction == 0 && fabsf(r.shown[0]-x) < 1e-4f);
    /* raw, the frame the eye starts shows a step change of v*dt: 0.1 m and 0.2 m at 30 frames a second */
    CHECK(r.change < (taken ? .5f*v*dt/1000 : T.pop_m) && r.change < .081f);
  }
  /* Where jumps are not looked for (the native view away from any event) a change of speed is the eye's own motion:
   * nothing is taken up, at any speed, and the eye is where the game put it in every frame. */
  for (int rate = 0; rate < 2; ++rate) {
    const float dt = rate ? 1000.0f/144 : 1000.0f/30;
    start(&r);
    for (int i = 0; i < 10; ++i) frame(&r, 0, 0, 0, NATIVE, false, false, dt);
    x = 0;
    for (int i = 0; i < 30; ++i) { x += 6*dt/1000; frame(&r, x, 0, 0, NATIVE, false, false, dt); CHECK(r.correction == 0 && r.shown[0] == x); }
    for (int i = 0; i < 30; ++i) { const float angle = 15*i*dt/1000; /* an eye 0.3 m from the body root, turning at 15 rad/s */
      frame(&r, x+.3f*sinf(angle), .3f*(1-cosf(angle)), 0, NATIVE, false, false, dt); CHECK(r.correction == 0); }
    CHECK(r.c.jumps == 0);
    /* a change of source is still taken up there */
    frame(&r, x+.2f, 0, 0, HEAD, false, false, dt);
    CHECK(r.c.edges == 1 && r.correction > .1f);
  }
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
  /* A rig eye that flickers (seen in the soak of 2026-11-22, 0.6.0-feel.37, 19:46:23.8 to 19:46:24.3, the body
   * standing: after an entry the rig eye went back and forth between two places 0.19 m apart from one frame to the
   * next, then stood). The logged wanted eye, frame for frame. The flips are taken up; what was wrong is what came
   * when the run's time was over: the last flip's 0.2 m in one frame was believed as a speed of 6 m/s, and the
   * presented eye went off at it while the wanted eye stood (0.143 m in the frame at 266 ms after the entry, 0.134 m
   * and 0.204 m in those at 466 and 499 ms, 0.43 m carried). No speed is believed from one frame's step. */
  {
    static const struct { float ms; uint32_t source; float eye[3]; } logged[] = {
    {0.0f, HEAD, {-6.6584f, 40.2668f, 3.4523f}},
    {33.4f, HEAD, {-6.6442f, 40.1923f, 3.4680f}},
    {66.7f, HEAD, {-6.6352f, 40.1471f, 3.4728f}},
    {100.2f, HEAD, {-6.6293f, 40.1200f, 3.4717f}},
    {133.6f, HEAD, {-6.6251f, 40.1041f, 3.4687f}},
    {166.1f, HEAD, {-6.6220f, 40.0951f, 3.4656f}},
    {199.4f, HEAD, {-6.6195f, 40.0901f, 3.4625f}},
    {233.7f, HEAD, {-6.6176f, 40.0876f, 3.4594f}},
    {267.0f, HEAD, {-6.6165f, 40.0865f, 3.4566f}},
    {300.7f, NATIVE, {-6.6161f, 40.1443f, 3.6391f}},
    {334.7f, NATIVE, {-6.6340f, 40.1444f, 3.5983f}},
    {367.4f, NATIVE, {-6.6276f, 40.1443f, 3.6013f}},
    {401.8f, NATIVE, {-6.8099f, 40.1450f, 3.4960f}},
    {434.9f, NATIVE, {-6.6377f, 40.1444f, 3.5962f}},
    {468.1f, NATIVE, {-6.7985f, 40.1449f, 3.5040f}},
    {501.8f, NATIVE, {-6.6430f, 40.1443f, 3.5945f}},
    {534.8f, NATIVE, {-6.6255f, 40.1443f, 3.6053f}},
    {568.2f, NATIVE, {-6.6502f, 40.1443f, 3.5917f}},
    {601.6f, NATIVE, {-6.6833f, 40.1444f, 3.5719f}},
    {634.8f, NATIVE, {-6.7030f, 40.1445f, 3.5598f}},
    {667.1f, NATIVE, {-6.8838f, 40.1450f, 3.4554f}},
    {700.9f, NATIVE, {-6.8699f, 40.1449f, 3.4635f}},
    {734.2f, NATIVE, {-6.8697f, 40.1449f, 3.4636f}},
    {767.7f, NATIVE, {-6.7408f, 40.1445f, 3.5380f}}};
    start(&r);
    float before[3] = {0}, worst_still = 0, carried = 0; double last = 0;
    for (unsigned i = 0; i < sizeof logged/sizeof *logged; ++i) {
      const float moved = sqrtf((logged[i].eye[0]-before[0])*(logged[i].eye[0]-before[0])+(logged[i].eye[1]-before[1])*(logged[i].eye[1]-before[1])
          +(logged[i].eye[2]-before[2])*(logged[i].eye[2]-before[2]));
      frame(&r, logged[i].eye[0], logged[i].eye[1], logged[i].eye[2], logged[i].source, true, false, i ? (float)(logged[i].ms-last) : 33.3f);
      last = logged[i].ms; memcpy(before, logged[i].eye, sizeof before);
      if (i && logged[i].source == NATIVE && logged[i-1].source == NATIVE) {
        if (moved < .03f && r.step > worst_still) worst_still = r.step; /* the wanted eye all but stood in this frame */
        if (r.correction > carried) carried = r.correction;
      }
    }
    CHECK(worst_still < .05f);
    CHECK(carried < .25f);
  }
  /* The same made by hand: back and forth by 0.19 m for ten frames, then still. While it flickers and after, the
   * presented eye does not go anywhere: under 0.03 m in any frame, and at the rig's eye once the catch-up is over. At
   * 30 and at 60 frames a second. */
  for (int rate = 0; rate < 2; ++rate) {
    const float dt = rate ? 1000.0f/60 : 1000.0f/30;
    start(&r);
    for (int i = 0; i < 10; ++i) frame(&r, 0, 0, 1.5f, NATIVE, true, false, dt);
    for (int i = 0; i < 10; ++i) frame(&r, i%2 ? 0 : .17f, 0, i%2 ? 1.5f : 1.4f, NATIVE, true, false, dt);
    for (int i = 0; i < 40; ++i) frame(&r, 0, 0, 1.5f, NATIVE, true, false, dt);
    CHECK(r.travelled < .03f);
    CHECK(r.correction == 0 && fabsf(r.shown[0]) < 1e-4f && fabsf(r.shown[2]-1.5f) < 1e-4f);
  }
  printf("camera continuity: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
