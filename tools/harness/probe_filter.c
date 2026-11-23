/* Made-up scenes through the continuity filter alone (src/camera_continuity.h): tools/harness/probes.py builds and
 * runs this against a filter's header and holds the numbers against the ones recorded for main.
 *
 *   probe_filter            every scene at 8, 16.7, 33.3 and 50 ms a frame, one line each:
 *                           name  ms-a-frame  change  carried  settled
 *
 * change:  the most the presented eye's step in the world changes from one frame to the next, as a vector,
 *          | (c-b) - (b-a) | (what tools/harness/replay.py counts), from the scene's event on
 * carried: the largest correction carried
 * settled: ms from the event to the last frame more than 0.005 m is carried (0: never)
 *
 * The scenes (the body root at the feet, the eye 1.5 m up; the root's axes do not turn; jumps are looked for):
 *   reseat-standing         the rig eye re-seated by 0.15 m in one frame, the body standing
 *   reseat-walking          the same, the body walking at 4 m/s
 *   reseat-two-creep        re-seated by 0.09 m in each of two frames, then creeping on by 0.5 mm a frame
 *   reseat-three-creep      by 0.05 m in each of three frames, then creeping on
 *   reseat-against          an eye going 2 m/s against the root re-seated by 0.15 m against its way, going on at 2 m/s
 *   reseat-along            the same re-seated along its way
 *   sprint-start            no re-seat: the body goes from 0 to 6 m/s in 300 ms and the eye leans 0.1 m forward with it
 *   head-to-native-sprint   a change of camera at 6 m/s, the head camera's eye 0.87 m ahead of the root and the rig's
 *                           0.28 m behind it (1.17 m apart); jumps are not looked for in the head camera
 *   flicker-N               the rig eye back and forth by 0.19 m from frame to frame, N frames (2 to 12), then at rest
 *   dying-down              a motion dying down: 0.26, 0.21, 0.136, 0.083, 0.053 m a frame and on by 0.62 a frame
 *   root-reseat             the body walking backwards at 3 m/s; the root is 0.6 m further on in one frame and the eye
 *                           goes on as it went, following a frame later
 * Scenes with a correction being carried (the body stands; a change of camera to a rig eye 0.4 or 0.8 m in front of
 * the head camera's; recorded in probes-carried.txt, with the numbers of the filter that has THE LENT SPEED):
 *   carried-jump-0.4, -0.8    three frames after the change of camera the rig eye is re-seated by 0.15 m to the side:
 *                             a jump taken up while the catch-up runs; measured from that frame on
 *   carried-camera-0.4, -0.8  three frames after it the view goes back to the head camera, whose eye is now 0.3 m to
 *                             the side of where it was: a change of camera while the catch-up runs; measured from there
 *   lent-0.4, -0.8            the head camera's eye going down against the root at 5 m/s when the view changes; the
 *                             rig eye is re-seated by 0.27 m forwards in its first frame, stands in the second, then
 *                             goes to the side by half the head camera's step, 0.6 and 0.2 of that, and rests;
 *                             measured from the change of camera on */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "camera_continuity.h"

static const TfpContinuityTuning tuning = TFP_CONTINUITY_DEFAULT;
enum { HEAD = 1, NATIVE = 2 };
typedef struct Probe { TfpContinuity c; double tick, event; float one[3], two[3], change, carried, settled; int frames; bool counting; } Probe;

static void begin(Probe *p) { memset(p, 0, sizeof *p); p->tick = 1000; }
/* One frame: the eye against the root (the root's axes are the world's), the root in the world. */
static void frame(Probe *p, const float *relative, const float *root, uint32_t source, bool detect, float dt) {
  static const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  float eye[3], out[3], shown[3];
  for (int k = 0; k < 3; ++k) eye[k] = root[k]+relative[k];
  const float carried = tfp_continuity_step_in(&p->c, relative, eye, basis, source, detect, false, p->tick, 250, &tuning, out);
  for (int k = 0; k < 3; ++k) shown[k] = root[k]+out[k];
  if (p->counting) {
    if (p->frames >= 2) { float d = 0;
      for (int k = 0; k < 3; ++k) { const float e = shown[k]-2*p->one[k]+p->two[k]; d += e*e; }
      if (sqrtf(d) > p->change) p->change = sqrtf(d); }
    if (carried > p->carried) p->carried = carried;
    if (carried > .005f) p->settled = (float)(p->tick-p->event);
  }
  memcpy(p->two, p->one, sizeof p->two); memcpy(p->one, shown, sizeof p->one);
  ++p->frames; p->tick += dt;
}
/* From the next frame on the scene is measured; that frame is the event. */
static void event(Probe *p) { p->counting = true; p->event = p->tick; }
static void report(const char *name, float dt, const Probe *p) {
  printf("%s %.1f %.4f %.4f %.0f\n", name, (double)dt, (double)p->change, (double)p->carried, (double)p->settled);
}

/* A re-seat of `size` m spread evenly over `frames` frames, the body going at `walk` m/s, the eye at `eye_speed` m/s
 * against the root before and after; afterwards it creeps on by `creep` m a frame. */
static void reseat(const char *name, float dt, float size, int frames, float walk, float eye_speed, float creep) {
  Probe p; begin(&p);
  float root[3] = {0, 0, 0}, relative[3] = {.1f, .3f, 1.5f};
  const int warm = (int)(1000/dt)+4, after = (int)(1600/dt);
  for (int n = -warm; n <= after; ++n) {
    root[1] += walk*dt/1000; relative[1] += eye_speed*dt/1000;
    if (n == 0) event(&p);
    if (n >= 0 && n < frames) relative[1] += size/(float)frames;
    if (n >= frames) relative[1] += creep;
    frame(&p, relative, root, NATIVE, true, dt);
  }
  report(name, dt, &p);
}
static void sprint_start(float dt) {
  Probe p; begin(&p);
  float root[3] = {0, 0, 0}, relative[3] = {.1f, .3f, 1.5f}, t = 0;
  const int warm = (int)(1000/dt)+4, after = (int)(1600/dt);
  for (int n = -warm; n <= after; ++n) {
    if (n == 0) event(&p);
    if (n >= 0) { t += dt/1000; const float part = t < .3f ? t/.3f : 1;
      root[1] += 6*part*dt/1000; relative[1] = .3f+.1f*part*part*(3-2*part); }
    frame(&p, relative, root, NATIVE, true, dt);
  }
  report("sprint-start", dt, &p);
}
static void head_to_native(float dt) {
  Probe p; begin(&p);
  float root[3] = {0, 0, 0};
  const float head[3] = {.15f, .87f, .70f}, rig[3] = {-.06f, -.28f, .78f};
  const int warm = (int)(1000/dt)+4, after = (int)(1600/dt);
  for (int n = -warm; n <= after; ++n) {
    root[1] += 6*dt/1000;
    if (n == 0) event(&p);
    frame(&p, n < 0 ? head : rig, root, n < 0 ? HEAD : NATIVE, n >= 0, dt);
  }
  report("head-to-native-sprint", dt, &p);
}
static void flicker(float dt, int flips) {
  Probe p; begin(&p); char name[32];
  const float root[3] = {0, 0, 0}; float relative[3] = {.1f, .3f, 1.5f};
  const int warm = (int)(1000/dt)+4, after = (int)(1600/dt);
  for (int n = -warm; n <= after; ++n) {
    if (n == 0) event(&p);
    relative[1] = .3f+(n >= 0 && n < flips && !(n&1) ? .19f : 0);
    frame(&p, relative, root, NATIVE, true, dt);
  }
  snprintf(name, sizeof name, "flicker-%d", flips);
  report(name, dt, &p);
}
static void dying_down(float dt) {
  Probe p; begin(&p);
  static const float steps[5] = {.26f, .21f, .136f, .083f, .053f};
  const float root[3] = {0, 0, 0}; float relative[3] = {.1f, .3f, 1.5f}, step = 0;
  const int warm = (int)(1000/dt)+4, after = (int)(1600/dt);
  for (int n = -warm; n <= after; ++n) {
    if (n == 0) event(&p);
    if (n >= 0) { step = n < 5 ? steps[n] : step*.62f; relative[1] += step; }
    frame(&p, relative, root, NATIVE, true, dt);
  }
  report("dying-down", dt, &p);
}
static void root_reseat(float dt) {
  Probe p; begin(&p);
  float root[3] = {0, 0, 0}, relative[3] = {.1f, .3f, 1.5f};
  const int warm = (int)(1000/dt)+4, after = (int)(1600/dt);
  for (int n = -warm; n <= after; ++n) {
    root[1] -= 3*dt/1000;
    if (n == 0) { event(&p); root[1] -= .6f; relative[1] += .6f; } /* the eye where it was going in the world */
    if (n == 1) relative[1] -= .6f;                                 /* and with the root again */
    frame(&p, relative, root, NATIVE, true, dt);
  }
  report("root-reseat", dt, &p);
}
/* A change of camera `apart` m forwards with the body standing, and three frames later, while the catch-up runs, a
 * re-seat of the rig eye by 0.15 m to the side (`camera` false) or the view back in the head camera, its eye 0.3 m to
 * the side of where it was (`camera` true). */
static void carried(float dt, float apart, bool camera) {
  Probe p; begin(&p); char name[32];
  const float root[3] = {0, 0, 0}; float head[3] = {.1f, .3f, 1.5f}, rig[3] = {.1f, .3f+apart, 1.5f};
  const int warm = (int)(1000/dt)+4, after = (int)(1600/dt);
  for (int n = -warm; n <= after; ++n) {
    if (n == 3) { event(&p); if (camera) head[0] += .3f; else rig[0] += .15f; }
    const bool native = n >= 0 && !(camera && n >= 3);
    frame(&p, native ? rig : head, root, native ? NATIVE : HEAD, native, dt);
  }
  snprintf(name, sizeof name, "carried-%s-%.1f", camera ? "camera" : "jump", (double)apart);
  report(name, dt, &p);
}
static void lent(float dt, float apart) {
  Probe p; begin(&p); char name[32];
  const float root[3] = {0, 0, 0}, v = 5, h = dt/1000; float head[3] = {.1f, .3f, 1.6f}, rig[3] = {0, 0, 0};
  const int warm = (int)(400/dt)+4, after = (int)(1600/dt);
  for (int n = -warm; n <= after; ++n) {
    if (n < 0) head[2] -= v*h;
    if (n == 0) { event(&p); rig[0] = head[0]; rig[1] = head[1]+apart; rig[2] = head[2]; }
    if (n == 1) rig[1] += .27f;
    if (n >= 3 && n <= 5) rig[0] += (n == 3 ? 1 : n == 4 ? .6f : .2f)*.5f*v*h;
    frame(&p, n < 0 ? head : rig, root, n < 0 ? HEAD : NATIVE, n >= 0, dt);
  }
  snprintf(name, sizeof name, "lent-%.1f", (double)apart);
  report(name, dt, &p);
}

int main(void) {
  static const float times[4] = {8.0f, 16.7f, 33.3f, 50.0f};
  for (int i = 0; i < 4; ++i) {
    const float dt = times[i];
    reseat("reseat-standing", dt, .15f, 1, 0, 0, 0);
    reseat("reseat-walking", dt, .15f, 1, 4, 0, 0);
    reseat("reseat-two-creep", dt, .18f, 2, 0, 0, .0005f);
    reseat("reseat-three-creep", dt, .15f, 3, 0, 0, .0005f);
    reseat("reseat-against", dt, -.15f, 1, 0, 2, 0);
    reseat("reseat-along", dt, .15f, 1, 0, 2, 0);
    sprint_start(dt);
    head_to_native(dt);
    for (int flips = 2; flips <= 12; ++flips) flicker(dt, flips);
    dying_down(dt);
    root_reseat(dt);
    for (int far = 0; far < 2; ++far) { carried(dt, far ? .8f : .4f, false); carried(dt, far ? .8f : .4f, true); lent(dt, far ? .8f : .4f); }
  }
  return 0;
}
