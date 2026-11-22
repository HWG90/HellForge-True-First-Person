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
/* The same with the body root given (its place and its turn about the vertical): the eye and the root are in the
 * world, the filter gets the eye in the root's frame and in the world, and what is kept is the presented eye's step
 * in the world. */
typedef struct Walk { TfpContinuity c; double tick; float shown[3], step, change, correction, travelled; bool first; } Walk;
static void begin_walk(Walk *w) { memset(w, 0, sizeof *w); w->first = true; }
static void walk(Walk *w, const float *eye, const float *root, float yaw, uint32_t source, bool detect, float dt_ms) {
  const float cy = cosf(yaw), sy = sinf(yaw), basis[9] = {cy, sy, 0, -sy, cy, 0, 0, 0, 1};
  float relative[3] = {0}, out[3], world[3];
  for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (eye[k]-root[k])*basis[a*3+k];
  w->tick += dt_ms;
  w->correction = tfp_continuity_step_in(&w->c, relative, eye, basis, source, detect, false, w->tick, 250, &T, out);
  for (int k = 0; k < 3; ++k) { world[k] = root[k]; for (int a = 0; a < 3; ++a) world[k] += out[a]*basis[a*3+k]; }
  if (!w->first) {
    const float d[3] = {world[0]-w->shown[0], world[1]-w->shown[1], world[2]-w->shown[2]};
    const float step = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    const float change = fabsf(step-w->step);
    if (step > w->travelled) w->travelled = step;
    if (change > w->change) w->change = change;
    w->step = step;
  }
  w->first = false; memcpy(w->shown, world, sizeof world);
}
/* `thrown` false: a rig re-seat of 0.15 m (the eye in the root's frame, so in the world too; `way`: to the right, the
 * left, forwards, backwards, up, down) while the body runs at `v` m/s along its
 * own forward and turns at `w` rad/s, the eye `ahead` m in front of the root: the largest change of the presented
 * eye's step in the world from one frame to the next (as a vector), and the most carried. `placed`: the filter is
 * given the world; without it the root is taken to move evenly, which is the filter as it was before it looked at
 * the root at all. `thrown`: no re-seat; in that frame the body is thrown instead (6 m/s backwards and 5 m/s up, then
 * falling, still turning) and its eye swings 0.2 m back and forth against the root, 20 rad/s. */
static void turning(bool thrown, int way, float ahead, float v, float w, float dt, bool placed, float *change, float *carried) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  float root[3] = {0, 0, 0}, relative[3] = {.1f, ahead, 1.6f}, yaw = 0, one[3] = {0}, two[3] = {0}; double tick = 1000;
  const int event = (int)(600/dt)+2, frames = event+(int)(900/dt); float flying[3] = {0, 0, 0};
  *change = *carried = 0;
  for (int i = 0; i < frames; ++i) {
    const float h = dt/1000;
    yaw += w*h;
    if (thrown && i == event) { flying[0] = sinf(yaw)*6; flying[1] = -cosf(yaw)*6; flying[2] = 5; }
    if (thrown && i >= event) { flying[2] -= 9.8f*h; for (int k = 0; k < 3; ++k) root[k] += flying[k]*h; relative[1] = ahead+.2f*sinf((i-event)*h*20); }
    else { root[0] += -sinf(yaw)*v*h; root[1] += cosf(yaw)*v*h; }
    if (!thrown && i == event) relative[way/2] += way%2 ? -.15f : .15f;
    const float cy = cosf(yaw), sy = sinf(yaw), basis[9] = {cy, sy, 0, -sy, cy, 0, 0, 0, 1};
    float eye[3], out[3], shown[3];
    for (int k = 0; k < 3; ++k) { eye[k] = root[k]; for (int a = 0; a < 3; ++a) eye[k] += relative[a]*basis[a*3+k]; }
    const float correction = tfp_continuity_step_in(&c, relative, placed ? eye : NULL, placed ? basis : NULL, NATIVE, true, false, tick, 250, &T, out);
    for (int k = 0; k < 3; ++k) { shown[k] = root[k]; for (int a = 0; a < 3; ++a) shown[k] += out[a]*basis[a*3+k]; }
    tick += dt;
    if (i > 3) { const float d[3] = {shown[0]-2*one[0]+two[0], shown[1]-2*one[1]+two[1], shown[2]-2*one[2]+two[2]};
      const float size = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if (size > *change) *change = size; }
    if (correction > *carried) *carried = correction;
    memcpy(two, one, sizeof two); memcpy(one, shown, sizeof one);
  }
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
  /* The speed believed when a run's time is over, at every frame time (the check of the soak fixes, 2026-11-22: with
   * 35 to 70 ms a frame the run holds two jumps when its time ends, and the middle of three alternating flips is a
   * flip; 0.16 m in a frame while the wanted eye stood, 0.35 to 0.64 m carried).
   * A flicker between two places is no speed at all: N flips of 0.19 m, then still. While the wanted eye stands no
   * frame of the presented eye is over 0.06 m, and never more than 0.25 m is carried, flickering or after. With the
   * world given (the root standing) and without. */
  {
    static const float times[] = {4, 8, 16.7f, 33.3f, 40, 50, 66, 100};
    float worst_still = 0, worst_carried = 0, worst_speed = 0, worst_fast = 0;
    for (unsigned d = 0; d < sizeof times/sizeof *times; ++d) for (int flips = 2; flips <= 12; ++flips) for (int placed = 0; placed < 2; ++placed) {
      const float dt = times[d], root[3] = {0, 0, 0}; float eye[3] = {0, .1f, 1.6f}, still = 0, carried = 0;
      Walk w; begin_walk(&w); start(&r);
#define FLICKER_FRAME() do { if (placed) walk(&w, eye, root, 0, NATIVE, true, dt); else frame(&r, eye[0], eye[1], eye[2], NATIVE, true, false, dt); } while (0)
#define FLICKER_OF(member) (placed ? w.member : r.member)
      for (int i = 0; i < 10; ++i) FLICKER_FRAME();
      for (int i = 0; i < flips; ++i) { eye[0] = i%2 ? 0 : .19f; FLICKER_FRAME(); if (FLICKER_OF(correction) > carried) carried = FLICKER_OF(correction); }
      for (int i = 0; i < (int)(1500/dt); ++i) { FLICKER_FRAME();
        if (FLICKER_OF(step) > still) still = FLICKER_OF(step);
        if (FLICKER_OF(correction) > carried) carried = FLICKER_OF(correction); }
      /* What is carried (0.19 m at most) is eased out, and that is the presented eye's own way: 1.3 m/s at its
       * fastest, which is more than 0.06 m in a frame only at 50 ms a frame and above. */
      const float allowed = fmaxf(.06f, 1.4f*dt/1000);
      if (still > allowed || carried > .25f) printf("  flicker, %g ms a frame, %d flips%s: %.3f m in a frame while the wanted eye stands, %.3f m carried\n",
                                                    (double)dt, flips, placed ? ", world given" : "", (double)still, (double)carried);
      CHECK(still <= allowed);
      CHECK(carried <= .25f);
      if (still/dt > worst_speed) worst_speed = still/dt;
      if (dt <= 40 && still > worst_fast) worst_fast = still;
      CHECK(FLICKER_OF(correction) == 0 && fabsf(FLICKER_OF(shown)[0]-eye[0]) < 1e-4f);
      if (still > worst_still) worst_still = still;
      if (carried > worst_carried) worst_carried = carried;
#undef FLICKER_FRAME
#undef FLICKER_OF
    }
    printf("flicker, 4 to 100 ms a frame, 2 to 12 flips: at most %.3f m in a frame while the wanted eye stands (%.3f m up to 40 ms a frame; "
           "%.2f m/s), %.3f m carried\n", (double)worst_still, (double)worst_fast, (double)worst_speed*1000, (double)worst_carried);
  }
  /* A genuine new speed is still believed after the run: the eye starts moving at 3 m/s where jumps are looked for
   * and keeps moving. The run holds it for its 70 ms (to the frame), then 3 m/s is believed (not less: the next frame
   * would be a jump again and a second run would hold the eye for another 70 ms), nothing more is taken up, and the
   * eye has caught up when the longest catch-up is over. Kept from before: 300 ms after the start the presented eye
   * is behind by no more than what the run held back and 0.04 m, and no frame of it is more than the motion's own
   * step and three quarters (the catch-up's own speed).
   * `seat`: the rig re-seated by 0.15 m in the frame the motion begins, the same way and the other way. One frame is
   * far off and the rest alike; the rest is the speed (the run's way divided by its time would give 5 m/s and 1 m/s). */
  {
    static const float times[] = {4, 8, 16.7f, 33.3f, 40, 50, 66, 100};
    float worst_gap = 0, worst_over = 0, worst_speed = 0;
    for (unsigned d = 0; d < sizeof times/sizeof *times; ++d) for (int seat = -1; seat <= 1; ++seat) {
      const float dt = times[d], v = 3, own = v*dt/1000;
      start(&r);
      for (int i = 0; i < 10; ++i) frame(&r, 0, 0, 1.6f, NATIVE, true, false, dt);
      x = seat*.15f; float gap = -1, over = 0, since = 0, last_jump = 0, believed = 0; uint64_t jumps = 0;
      for (int i = 0; i < (int)(900/dt); ++i) {
        x += own; since += dt; frame(&r, x, 0, 1.6f, NATIVE, true, false, dt);
        if (r.c.jumps != jumps) { jumps = r.c.jumps; last_jump = since; }
        if (r.step-own > over) over = r.step-own;
        if (gap < 0 && since >= 300) { gap = fabsf(x-r.shown[0]); believed = r.c.speed[0]; }
      }
      if (!seat) {
        /* (below 0.04 m in a frame, and below the measure at 100 ms a frame, nothing is taken up at all) */
        CHECK(last_jump <= 70+dt);
        CHECK(fabsf(believed-v) < .05f);
        CHECK(gap <= v*(70+dt)/1000+.04f);
        CHECK(over <= .75f*own);
        if (gap > worst_gap) worst_gap = gap;
        if (over > worst_over) worst_over = over;
        printf("  a start at 3 m/s, %g ms a frame: %.3f m behind 300 ms later, %.3f m over the motion's own step of %.3f m\n",
               (double)dt, (double)gap, (double)over, (double)own);
      } else {
        CHECK(fabsf(believed-v) < .3f);
        if (fabsf(believed-v) > worst_speed) worst_speed = fabsf(believed-v);
      }
      CHECK(r.correction == 0 && fabsf(r.shown[0]-x) < 1e-4f);
    }
    printf("a start at 3 m/s, 4 to 100 ms a frame: at most %.3f m behind 300 ms later, %.3f m over the motion's own step; with a re-seat "
           "in its first frame the speed believed is within %.2f m/s\n", (double)worst_gap, (double)worst_over, (double)worst_speed);
  }
  /* The body root re-seated by itself (seen in the soak of 2026-11-22, 0.6.0-feel.37, 19:45:12.826: a backwards dive
   * begun 170 ms after an entry; the wanted eye went on at its 0.08 m a frame in the world, the presented eye went
   * 0.70 m in that frame and 0.61 m of correction appeared). A step that is a jump in the root's frame only, with the
   * eye going on evenly in the world, is the root's: nothing is taken up, the eye is shown where the game put it.
   * Here: walking backwards at 3 m/s, the root 0.6 m further on in one frame. */
  {
    Walk w; begin_walk(&w);
    float eye[3] = {.1f, 0, 1.6f}, root[3] = {0, 0, 0};
    for (int i = 0; i < 12; ++i) { eye[1] -= .1f; root[1] -= .1f; walk(&w, eye, root, 0, NATIVE, true, 33.3f); }
    w.change = 0;
    eye[1] -= .1f; root[1] -= .7f; walk(&w, eye, root, 0, NATIVE, true, 33.3f);
    CHECK(w.correction == 0 && w.c.jumps == 0 && w.c.reseats == 1 && fabsf(w.step-.1f) < 1e-4f);
    /* One frame later the rig eye follows (0.44 m in that frame, both in the root's frame and in the world): that
     * is the eye re-seated, taken up and eased like any other. */
    eye[1] -= .1f+.44f; root[1] -= .1f; walk(&w, eye, root, 0, NATIVE, true, 33.3f);
    CHECK(w.c.jumps == 1 && w.c.reseats == 1 && fabsf(w.correction-.44f) < 1e-3f && fabsf(w.step-.1f) < 1e-3f);
    for (int i = 0; i < 16; ++i) { eye[1] -= .1f; root[1] -= .1f; walk(&w, eye, root, 0, NATIVE, true, 33.3f); }
    CHECK(w.correction == 0 && fabsf(w.shown[1]-eye[1]) < 1e-4f && w.change < .035f);
  }
  /* A correction that is being carried goes through it as it was: 0.2 m taken up, and two frames later the root
   * re-seated by 0.6 m, and by 0.6 m with a turn of half a circle (the eye 0.3 m from it, not moved). The curve
   * goes on in the world as if nothing had been; no step changes by more than the curve's own 0.03 m. */
  for (int turned = 0; turned < 2; ++turned) {
    Walk w; begin_walk(&w);
    float eye[3] = {0, .3f, 1.6f}, root[3] = {0, 0, 0}, yaw = 0;
    for (int i = 0; i < 12; ++i) walk(&w, eye, root, yaw, NATIVE, true, 33.3f);
    eye[0] += .2f; walk(&w, eye, root, yaw, NATIVE, true, 33.3f);
    walk(&w, eye, root, yaw, NATIVE, true, 33.3f);
    CHECK(w.c.jumps == 1 && w.correction > .15f);
    const float before = w.correction; w.change = 0;
    if (turned) { yaw = 3.14159265f; root[1] += .6f; } else root[1] -= .6f;
    walk(&w, eye, root, yaw, NATIVE, true, 33.3f);
    CHECK(w.c.jumps == 1 && w.c.reseats == 1 && w.correction < before && w.correction > before-.05f);
    for (int i = 0; i < 16; ++i) walk(&w, eye, root, yaw, NATIVE, true, 33.3f);
    CHECK(w.correction == 0 && fabsf(w.shown[0]-eye[0]) < 1e-4f && fabsf(w.shown[1]-eye[1]) < 1e-4f && w.change < .03f);
  }
  /* With the root going on evenly everything is as without the world: the rig re-seated by 0.218 m while the body
   * walks is taken up whole, the body's own travel is not a jump, and a body that is moved 5 m with its eye in one
   * frame (a respawn) is nothing. */
  {
    Walk w; begin_walk(&w);
    float eye[3] = {.1f, 0, 1.6f}, root[3] = {0, 0, 0};
    for (int i = 0; i < 12; ++i) { eye[1] -= .1f; root[1] -= .1f; walk(&w, eye, root, 0, NATIVE, true, 33.3f); CHECK(w.correction == 0); }
    eye[1] -= .1f; eye[0] += .218f; root[1] -= .1f; walk(&w, eye, root, 0, NATIVE, true, 33.3f);
    CHECK(w.c.jumps == 1 && w.c.reseats == 0 && fabsf(w.correction-.218f) < 1e-4f && fabsf(w.step-.1f) < 1e-4f);
    for (int i = 0; i < 16; ++i) { eye[1] -= .1f; root[1] -= .1f; walk(&w, eye, root, 0, NATIVE, true, 33.3f); }
    CHECK(w.correction == 0);
    eye[0] += 5; root[0] += 5; eye[1] -= .1f; root[1] -= .1f; walk(&w, eye, root, 0, NATIVE, true, 33.3f);
    CHECK(w.correction == 0 && w.c.jumps == 1 && w.c.reseats == 0 && w.c.restarts == 0 && fabsf(w.shown[0]-eye[0]) < 1e-5f);
    eye[1] -= .1f; root[1] -= .1f; walk(&w, eye, root, 0, NATIVE, true, 33.3f);
    CHECK(w.correction == 0 && w.c.jumps == 1);
  }
  /* Looking at the root must not cost a genuine rig re-seat anything (the check of the soak fixes, 2026-11-22). The
   * root branch compared the step with the eye's speed in the world, a 50 ms average; in a fast turn that speed
   * turns with the body, the average lags behind it, and the lag was read as "the root's part": part of the re-seat
   * was shown at once (the four rows below: 0.258, 0.115, 0.056 and 0.074 m of step change where the filter without
   * the root gave 0.168, 0.078, 0.020 and 0.036 m). The comparison is now with the root's own even course
   * (camera_continuity.h), and a root that turns and runs evenly has no part in a jump: with the world given the
   * re-seat is taken up as without, no worse than before plus 0.01 m, and as much is carried. */
  {
    static const struct { float ahead, v, w, dt; } rows[] = {{.8f, 0, 12, 33.3f}, {.8f, 6, 6, 33.3f}, {.8f, 6, 6, 16.7f}, {.25f, 6, 12, 16.7f}};
    for (unsigned i = 0; i < sizeof rows/sizeof *rows; ++i) {
      float change, carried, before, before_carried;
      turning(false, 2, rows[i].ahead, rows[i].v, rows[i].w, rows[i].dt, true, &change, &carried);
      turning(false, 2, rows[i].ahead, rows[i].v, rows[i].w, rows[i].dt, false, &before, &before_carried);
      printf("  re-seat of 0.15 m, eye %.2f m ahead, %g m/s, %g rad/s, %g ms a frame: step change %.3f m (without the root %.3f m), carried %.3f m (%.3f m)\n",
             (double)rows[i].ahead, (double)rows[i].v, (double)rows[i].w, (double)rows[i].dt, (double)change, (double)before, (double)carried, (double)before_carried);
      CHECK(change <= before+.01f);
      CHECK(carried >= before_carried-.01f);
    }
    /* and everywhere around them: 4 to 100 ms a frame, 0 to 6 m/s, 0 to 12 rad/s, the eye 0.25 and 0.8 m ahead, the
     * re-seat each way */
    static const float times[] = {4, 8, 16.7f, 33.3f, 50, 100}, speeds[] = {0, 2, 6}, turns[] = {0, 3, 6, 12}, aheads[] = {.25f, .8f};
    float worst = -1;
    for (unsigned d = 0; d < 6; ++d) for (unsigned v = 0; v < 3; ++v) for (unsigned t = 0; t < 4; ++t) for (unsigned a = 0; a < 2; ++a) for (int way = 0; way < 6; ++way) {
      float change, carried, before, before_carried;
      turning(false, way, aheads[a], speeds[v], turns[t], times[d], true, &change, &carried);
      turning(false, way, aheads[a], speeds[v], turns[t], times[d], false, &before, &before_carried);
      if (change > before+.01f) printf("  worse: way %d, eye %.2f m ahead, %g m/s, %g rad/s, %g ms a frame: %.3f m, without the root %.3f m\n",
                                       way, (double)aheads[a], (double)speeds[v], (double)turns[t], (double)times[d], (double)change, (double)before);
      CHECK(change <= before+.01f);
      if (change-before > worst) worst = change-before;
    }
    printf("re-seat in a turn, 864 cases: step change at most %.3f m over the filter without the root\n", (double)worst);
    /* A body thrown while it turns is not a root re-seated frame after frame: it has a new speed, which it keeps in
     * the world while its axes turn on. (With the course kept along the root's axes only, and kept as it was in
     * every frame the root was off it, ten frames in a row were read as re-seats and 0.68 m was carried for 0.58 m.) */
    float worst_change = -1, worst_carried = -1;
    for (unsigned d = 0; d < 6; ++d) for (unsigned v = 0; v < 3; ++v) for (unsigned t = 0; t < 4; ++t) {
      float change, carried, before, before_carried;
      turning(true, 0, .25f, speeds[v], turns[t], times[d], true, &change, &carried);
      turning(true, 0, .25f, speeds[v], turns[t], times[d], false, &before, &before_carried);
      if (change > before+.01f || carried > before_carried+.01f)
        printf("  worse, thrown: %g m/s, %g rad/s, %g ms a frame: step change %.3f m (%.3f m), carried %.3f m (%.3f m)\n",
               (double)speeds[v], (double)turns[t], (double)times[d], (double)change, (double)before, (double)carried, (double)before_carried);
      CHECK(change <= before+.01f);
      CHECK(carried <= before_carried+.01f);
      if (change-before > worst_change) worst_change = change-before;
      if (carried-before_carried > worst_carried) worst_carried = carried-before_carried;
    }
    printf("thrown in a turn, 72 cases: step change at most %.3f m and carried at most %.3f m over the filter without the root\n",
           (double)worst_change, (double)worst_carried);
  }
  /* Where jumps are not looked for the root's jump is not believed as a speed of the eye either: the frame after it,
   * with jumps looked for again, is an ordinary frame. */
  {
    Walk w; begin_walk(&w);
    float eye[3] = {.1f, 0, 1.6f}, root[3] = {0, 0, 0};
    for (int i = 0; i < 12; ++i) { eye[1] -= .1f; root[1] -= .1f; walk(&w, eye, root, 0, NATIVE, false, 33.3f); }
    eye[1] -= .1f; root[1] -= .7f; walk(&w, eye, root, 0, NATIVE, false, 33.3f);
    CHECK(w.correction == 0 && w.c.reseats == 1);
    eye[1] -= .1f; root[1] -= .1f; walk(&w, eye, root, 0, NATIVE, true, 33.3f);
    CHECK(w.correction == 0 && w.c.jumps == 0);
  }
  printf("camera continuity: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
