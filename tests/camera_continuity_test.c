/* Continuity of the presented eye (src/camera_continuity.h): what is taken up, what is left alone, how the
 * correction ends. The figures in the comments are the game's, from the feel.22 session of 2026-11-22. */
#include "camera_continuity.h"
#include <stdio.h>
#include "reference/soak-6-steps.h"
#include "reference/soak-8-steps.h"
#include "reference/soak-9-runs.h"
#include "reference/soak-11-steps.h"
#include "reference/soak-14-steps.h"
#include "reference/soak-18-steps.h"
#include "reference/soak-19-steps.h"
#include "reference/soak-slowing-steps.h"
#include "reference/soak-21-alternation.h"
#include "reference/soak-30-cut.h"
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
/* Logged frames (tests/reference/soak-6-steps.h) through the filter: from frame `from` on, the most the presented
 * eye's step in the world changes from one frame to the next beyond what the body root's own step changes by, the
 * largest such change itself, and the most carried. */
typedef struct Soak { float beyond, change, carried, logged_change; } Soak;
static Soak soak(const SoakFrame *frames, int count, int from) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  Soak s = {0, 0, 0, 0}; float shown[3] = {0}, step = 0, root_step = 0, logged_step = 0;
  for (int i = 0; i < count; ++i) {
    const SoakFrame *f = &frames[i]; float relative[3] = {0}, out[3], world[3];
    for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (f->wanted[k]-f->root[k])*f->axes[a*3+k];
    const float carried = tfp_continuity_step_in(&c, relative, f->wanted, f->axes, (uint32_t)f->source, f->looked != 0, false, 1000.0+(double)f->ms, 250, &T, out);
    for (int k = 0; k < 3; ++k) { world[k] = f->root[k]; for (int a = 0; a < 3; ++a) world[k] += out[a]*f->axes[a*3+k]; }
    if (i) {
      float d = 0, r = 0, l = 0;
      for (int k = 0; k < 3; ++k) { d += (world[k]-shown[k])*(world[k]-shown[k]); r += (f->root[k]-f[-1].root[k])*(f->root[k]-f[-1].root[k]);
        l += (f->logged[k]-f[-1].logged[k])*(f->logged[k]-f[-1].logged[k]); }
      d = sqrtf(d); r = sqrtf(r); l = sqrtf(l);
      if (i > from) {
        const float change = fabsf(d-step), beyond = change-fabsf(r-root_step);
        if (change > s.change) s.change = change;
        if (beyond > s.beyond) s.beyond = beyond;
        if (fabsf(l-logged_step) > s.logged_change) s.logged_change = fabsf(l-logged_step);
        if (carried > s.carried) s.carried = carried;
      }
      step = d; root_step = r; logged_step = l;
    }
    memcpy(shown, world, sizeof world);
  }
  return s;
}
/* Logged rows (tests/reference/soak-8-steps.h; every row is one call of the filter) through the filter, judged as the
 * soak's judge does (tools/harness/replay.py). Two rows under 5 ms apart from another camera or with the same eye are
 * one frame, the later. For frames a, b, c, 5 to 80 ms apart, the presented eye's step changes by
 * | |c-b| - |b-a| |; a change of 0.06 m or more is the game's own when the wanted eye's step changes by at least 0.8
 * of it and under 0.03 m is carried in b and c, reduced when the wanted eye's changes by over 1.2 of it, and
 * otherwise the filter's. `step`: the largest of the filter's among the frames c from row `from` to row `to`
 * (0: none); `logged`: the same of the eye the log shows; `apart`: how far the presented eye is from the logged one
 * at most in those rows.
 * That measure does not see a change of direction (an eye that goes 0.04 m forwards and then 0.04 m back has changed
 * its step by 0.08 m and by nothing in length). `turned`, `logged_turned`: the same judged by the change of the step
 * as a vector, | (c-b) - (b-a) |, of the presented eye and of the wanted one: what replay.py counts now. */
typedef struct Judged { float step, logged, apart, turned, logged_turned; } Judged;
static float judged_step(const float (*eye)[3], const float (*wanted)[3], const float *carried, const float *ms, const int *row, int frames, int from, int to,
                         bool vector) {
  float worst = 0;
  for (int i = 2; i < frames; ++i) {
    if (row[i] < from || row[i] > to || ms[i-1]-ms[i-2] < 5 || ms[i-1]-ms[i-2] > 80 || ms[i]-ms[i-1] < 5 || ms[i]-ms[i-1] > 80) continue;
    float shown[2] = {0, 0}, own[2] = {0, 0};
    for (int k = 0; k < 3; ++k) for (int n = 0; n < 2; ++n) {
      shown[n] += (eye[i-n][k]-eye[i-n-1][k])*(eye[i-n][k]-eye[i-n-1][k]); own[n] += (wanted[i-n][k]-wanted[i-n-1][k])*(wanted[i-n][k]-wanted[i-n-1][k]); }
    float change = fabsf(sqrtf(shown[0])-sqrtf(shown[1])), game = fabsf(sqrtf(own[0])-sqrtf(own[1]));
    if (vector) { change = game = 0;
      for (int k = 0; k < 3; ++k) { const float e = eye[i][k]-2*eye[i-1][k]+eye[i-2][k], w = wanted[i][k]-2*wanted[i-1][k]+wanted[i-2][k]; change += e*e; game += w*w; }
      change = sqrtf(change); game = sqrtf(game); }
    if (change < .06f || (game >= .8f*change && carried[i-1] < .03f && carried[i] < .03f) || game > 1.2f*change) continue;
    if (change > worst) worst = change;
  }
  return worst;
}
static Judged judged(const SoakFrame *rows, int count, int from, int to) {
  enum { MOST = 128 };
  static float eye[MOST][3], logged[MOST][3], wanted[MOST][3], carried[MOST], was[MOST], ms[MOST]; static int row[MOST];
  TfpContinuity c; memset(&c, 0, sizeof c);
  Judged j = {0, 0, 0, 0, 0}; int frames = 0; float first = 0;
  for (int i = 0; i < count && i < MOST; ++i) {
    const SoakFrame *f = &rows[i]; float relative[3] = {0}, out[3], world[3], apart = 0, same = 0, correction = 0;
    for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (f->wanted[k]-f->root[k])*f->axes[a*3+k];
    const float length = tfp_continuity_step_in(&c, relative, f->wanted, f->axes, (uint32_t)f->source, f->looked != 0, false, 1000.0+(double)f->ms, 250, &T, out);
    for (int k = 0; k < 3; ++k) { world[k] = f->root[k]; for (int a = 0; a < 3; ++a) world[k] += out[a]*f->axes[a*3+k];
      apart += (world[k]-f->logged[k])*(world[k]-f->logged[k]); correction += (f->logged[k]-f->wanted[k])*(f->logged[k]-f->wanted[k]);
      if (i) same += (f->logged[k]-f[-1].logged[k])*(f->logged[k]-f[-1].logged[k]); }
    if (i >= from && i <= to && sqrtf(apart) > j.apart) j.apart = sqrtf(apart);
    const bool again = frames && f->ms-first >= 0 && f->ms-first < 5 && (f->source != f[-1].source || sqrtf(same) < .002f);
    if (!again) { ++frames; first = f->ms; }
    const int n = frames-1;
    memcpy(eye[n], world, sizeof world); memcpy(logged[n], f->logged, sizeof world); memcpy(wanted[n], f->wanted, sizeof world);
    carried[n] = length; was[n] = sqrtf(correction); ms[n] = f->ms; row[n] = i;
  }
  j.step = judged_step(eye, wanted, carried, ms, row, frames, from, to, false);
  j.logged = judged_step(logged, wanted, was, ms, row, frames, from, to, false);
  j.turned = judged_step(eye, wanted, carried, ms, row, frames, from, to, true);
  j.logged_turned = judged_step(logged, wanted, was, ms, row, frames, from, to, true);
  return j;
}
/* Logged rows through the filter to the end of a run of jumps, the row `at` (the first that is no jump). `off`: how
 * far the speed the filter believes after that row is from the wanted eye's real one, its way against the root over
 * the 100 ms from the run's last jump on (the row before `at`; m/s; the root stands in these rows). `grown`: the most the correction carried grows
 * from one row to the next after `at`, to row `to` (the wanted eye rests there: nothing is left to take up). */
typedef struct Rested { float off, grown, believed, real; } Rested;
static Rested rested(const SoakFrame *rows, int count, int at, int to) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  Rested r = {0, 0, 0, 0}; float believed[3] = {0}, then[3] = {0}, last[3] = {0}, before = 0, span = 0;
  for (int i = 0; i < count && i <= to; ++i) {
    const SoakFrame *f = &rows[i]; float relative[3] = {0}, out[3];
    for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (f->wanted[k]-f->root[k])*f->axes[a*3+k];
    const float carried = tfp_continuity_step_in(&c, relative, f->wanted, f->axes, (uint32_t)f->source, f->looked != 0, false, 1000.0+(double)f->ms, 250, &T, out);
    if (i == at-1) memcpy(then, relative, sizeof then);
    if (i == at) memcpy(believed, c.speed, sizeof believed);
    if (i >= at && f->ms-rows[at-1].ms <= 110) { memcpy(last, relative, sizeof last); span = (f->ms-rows[at-1].ms)/1000; }
    if (i > at && carried-before > r.grown) r.grown = carried-before;
    before = carried;
  }
  float off = 0, real = 0;
  for (int k = 0; k < 3; ++k) { const float v = span > 0 ? (last[k]-then[k])/span : 0; off += (believed[k]-v)*(believed[k]-v); real += v*v; }
  r.off = sqrtf(off); r.real = sqrtf(real); r.believed = tfp_continuity_length(believed);
  return r;
}
/* THE ONSET, made up: the body runs at 6 m/s and its root's axes begin to roll about its forward at `w` rad/s from
 * one frame on. `rides`: the eye, 1.25 m above the root, goes round with the axes; otherwise it goes straight on in
 * the world. `reseat`: the rig eye is re-seated by 0.15 m forwards two frames before. `change`: the most the presented
 * eye's step in the world changes from frame to frame (as a vector); `carried`: the most carried; `aside`: the most
 * the presented eye is to the side of the wanted one. */
static void rolling(bool rides, bool reseat, float w, float dt, float *change, float *carried, float *aside) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  float root[3] = {0, 0, 0}, straight[3] = {.1f, .3f, 1.25f}, angle = 0, one[3] = {0}, two[3] = {0}; double tick = 1000;
  const int event = (int)(600/dt)+2, frames = event+(int)(900/dt);
  *change = *carried = *aside = 0;
  for (int i = 0; i < frames; ++i) {
    const float h = dt/1000, moved = reseat && i >= event-2 ? .15f : 0, fixed[3] = {.1f, .3f+moved, 1.25f};
    if (i >= event) angle += w*h;
    root[1] += 6*h; straight[1] = root[1]+.3f+moved;
    const float ca = cosf(angle), sa = sinf(angle), basis[9] = {ca, 0, sa, 0, 1, 0, -sa, 0, ca};
    float eye[3], relative[3] = {0}, out[3], shown[3];
    for (int k = 0; k < 3; ++k) { eye[k] = straight[k]; if (rides) { eye[k] = root[k]; for (int a = 0; a < 3; ++a) eye[k] += fixed[a]*basis[a*3+k]; } }
    for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (eye[k]-root[k])*basis[a*3+k];
    const float correction = tfp_continuity_step_in(&c, relative, eye, basis, NATIVE, true, false, tick, 250, &T, out);
    for (int k = 0; k < 3; ++k) { shown[k] = root[k]; for (int a = 0; a < 3; ++a) shown[k] += out[a]*basis[a*3+k]; }
    tick += dt;
    if (i > 3) { const float d[3] = {shown[0]-2*one[0]+two[0], shown[1]-2*one[1]+two[1], shown[2]-2*one[2]+two[2]};
      const float size = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if (size > *change) *change = size; }
    if (correction > *carried) *carried = correction;
    if (fabsf(shown[0]-eye[0]) > *aside) *aside = fabsf(shown[0]-eye[0]);
    memcpy(two, one, sizeof two); memcpy(one, shown, sizeof one);
  }
}
/* A rig eye re-seated by 0.15 m in one frame that goes on the same way (`way` 1) or back (-1) by `part` of the measure
 * a frame for `frames` frames and then rests, the body standing: the most the presented eye's step changes from frame
 * to frame, and the most carried. */
static void tailed(float dt, float part, int frames, int way, float *change, float *carried) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  const int warm = (int)(1000/dt)+4, after = (int)(1600/dt); const float measure = fmaxf(T.pop_m, T.pop_accel*dt*dt/1e6f);
  float z = 1.5f, one = 0, two = 0;
  *change = *carried = 0;
  for (int i = -warm; i <= after; ++i) {
    const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (i == 0) z += .15f;
    if (i >= 1 && i <= frames) z += (float)way*part*measure;
    const float eye[3] = {0, .6f, z}; float out[3];
    const float length = tfp_continuity_step_in(&c, eye, eye, basis, NATIVE, true, false, 1000.0+(double)(i+warm)*(double)dt, 250, &T, out);
    if (i >= 0) { if (fabsf(out[2]-2*one+two) > *change) *change = fabsf(out[2]-2*one+two); if (length > *carried) *carried = length; }
    two = one; one = out[2];
  }
}
/* Logged rows through the filter. `jumps`: how many of the rows `from` to `to` were taken up as jumps; `aside`: how
 * far the presented eye is to the side of the wanted one at most in those rows: level, and across the way the body
 * goes in that row (its root's step in the world). An eye held back or let run on along the body's way is not aside. */
typedef struct Swung { int jumps; float aside; } Swung;
static Swung swung(const SoakFrame *rows, int count, int from, int to) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  Swung s = {0, 0};
  for (int i = 0; i < count && i <= to; ++i) {
    const SoakFrame *f = &rows[i]; float relative[3] = {0}, out[3], off[3]; const uint64_t jumps = c.jumps;
    for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (f->wanted[k]-f->root[k])*f->axes[a*3+k];
    tfp_continuity_step_in(&c, relative, f->wanted, f->axes, (uint32_t)f->source, f->looked != 0, false, 1000.0+(double)f->ms, 250, &T, out);
    if (i < from || !i) continue;
    for (int k = 0; k < 3; ++k) { off[k] = f->root[k]-f->wanted[k]; for (int a = 0; a < 3; ++a) off[k] += out[a]*f->axes[a*3+k]; }
    const float way[2] = {f->root[0]-f[-1].root[0], f->root[1]-f[-1].root[1]}, length = sqrtf(way[0]*way[0]+way[1]*way[1]);
    const float aside = length > 1e-4f ? fabsf(off[0]*way[1]-off[1]*way[0])/length : 0;
    if (aside > s.aside) s.aside = aside;
    s.jumps += (int)(c.jumps-jumps);
  }
  return s;
}
/* A rig re-seated in two frames, 0.09 m in each, that then creeps on the same way at 0.5 mm a frame (the body
 * stands, jumps are looked for). `back`: the most the presented eye goes the other way in one frame, against its
 * catch-up; `settled`: how long after the first of the two frames more than 0.005 m is still carried (ms). */
typedef struct Crept { float back, settled, carried; } Crept;
static Crept crept(float dt) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  Crept r = {0, 0, 0}; const int warm = (int)(1000/dt)+4, after = (int)(1600/dt); float shown = 0;
  for (int n = -warm; n <= after; ++n) {
    const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, moved = n < 0 ? 0 : n < 2 ? .09f*(float)(n+1) : .18f+.0005f*(float)(n-1);
    const float eye[3] = {0, .6f, 1.5f+moved}; float out[3];
    const float carried = tfp_continuity_step_in(&c, eye, eye, basis, NATIVE, true, false, 1000.0+(double)(n+warm)*(double)dt, 250, &T, out);
    if (n >= 0) { if (shown-out[2] > r.back) r.back = shown-out[2]; if (carried > .005f) r.settled = (float)n*dt; if (carried > r.carried) r.carried = carried; }
    shown = out[2];
  }
  return r;
}
/* An eye going at `v` m/s against the root that is re-seated by 0.15 m against its way in one frame and goes on a
 * little faster (0.3 m/s): the most carried. */
static float against(float v, float dt) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  const int warm = (int)(1000/dt)+4, after = (int)(1600/dt); float most = 0;
  for (int n = -warm; n <= after; ++n) {
    const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, t = (float)n*dt/1000, eye[3] = {0, .6f, 1.5f+(n < 0 ? v*t : -.15f+(v+.3f)*t)}; float out[3];
    const float carried = tfp_continuity_step_in(&c, eye, eye, basis, NATIVE, true, false, 1000.0+(double)(n+warm)*(double)dt, 250, &T, out);
    if (n >= 0 && carried > most) most = carried;
  }
  return most;
}
/* Logged rows through the filter, rows `from` to `to`: the correction summed over them and its most, of the filter
 * and of the log (the logged eye's distance from the wanted one). */
typedef struct Borne { float sum, most, logged_sum, logged_most; } Borne;
static Borne borne(const SoakFrame *rows, int count, int from, int to) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  Borne b = {0, 0, 0, 0};
  for (int i = 0; i < count && i <= to; ++i) {
    const SoakFrame *f = &rows[i]; float relative[3] = {0}, out[3], was = 0;
    for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (f->wanted[k]-f->root[k])*f->axes[a*3+k];
    const float carried = tfp_continuity_step_in(&c, relative, f->wanted, f->axes, (uint32_t)f->source, f->looked != 0, false, 1000.0+(double)f->ms, 250, &T, out);
    for (int k = 0; k < 3; ++k) was += (f->logged[k]-f->wanted[k])*(f->logged[k]-f->wanted[k]);
    if (i < from) continue;
    b.sum += carried; b.logged_sum += sqrtf(was);
    if (carried > b.most) b.most = carried;
    if (sqrtf(was) > b.logged_most) b.logged_most = sqrtf(was);
  }
  return b;
}
/* THE LENT SPEED, made up: the body stands. The head camera's eye goes down against the root at `v` m/s (jumps are not
 * looked for in it) and the view changes to a rig eye `apart` m in front of it. The rig eye is re-seated by 0.27 m
 * forwards in its first frame and stands in the second; with `moves` it then goes to the side, across the head
 * camera's way, by half the head camera's step in a frame, 0.6 of that and 0.2 of it, and rests. `change`: the most
 * the presented eye's step changes from frame to frame (as a vector) from the change of camera on; `carried`: the most
 * carried; `settled`: ms from the change of camera to the last frame more than 0.005 m is carried. */
static void lent(float v, float apart, bool moves, float dt, float *change, float *carried, float *settled) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  static const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  const int warm = (int)(400/dt)+4, after = (int)(1600/dt); const float h = dt/1000;
  float head[3] = {.1f, .6f, 1.6f}, rig[3] = {0, 0, 0}, one[3] = {0}, two[3] = {0};
  *change = *carried = *settled = 0;
  for (int n = -warm; n <= after; ++n) {
    float out[3];
    if (n < 0) head[2] -= v*h;
    if (n == 0) { rig[0] = head[0]; rig[1] = head[1]+apart; rig[2] = head[2]; }
    if (n == 1) rig[1] += .27f;
    if (moves && n >= 3 && n <= 5) rig[0] += (n == 3 ? 1 : n == 4 ? .6f : .2f)*.5f*v*h;
    const float *eye = n < 0 ? head : rig;
    const float length = tfp_continuity_step_in(&c, eye, eye, basis, n < 0 ? HEAD : NATIVE, n >= 0, false, 1000.0+(double)(n+warm)*(double)dt, 250, &T, out);
    if (n >= 0) { float d = 0;
      for (int k = 0; k < 3; ++k) { const float e = out[k]-2*one[k]+two[k]; d += e*e; }
      if (sqrtf(d) > *change) *change = sqrtf(d);
      if (length > *carried) *carried = length;
      if (length > .005f) *settled = (float)n*dt; }
    memcpy(two, one, sizeof two); memcpy(one, out, sizeof one);
  }
}
/* THE STOP, made up: the body stands, the native view, jumps looked for. The rig eye goes forwards at `v` m/s for
 * twelve frames and stops dead: `back` m in each of the next four frames (against the way it went when negative; a
 * stop's last millimetres have no way of their own), then rest. `change`: the most the presented eye's step changes
 * from frame to frame (as a vector) from the stop on; `past`: how far the presented eye goes on past the wanted one
 * at most; `carried`: the most carried from the stop on. */
static void stopped(float v, float back, float dt, float *change, float *past, float *carried) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  float eye[3] = {.1f, .3f, 1.5f}, one[3] = {0}, two[3] = {0};
  *change = *past = *carried = 0;
  for (int n = 0; n < 60; ++n) {
    float out[3];
    if (n && n <= 12) eye[1] += v*dt/1000; else if (n > 12 && n <= 16) eye[1] += back;
    const float length = tfp_continuity_step(&c, eye, NATIVE, true, false, 1000.0+(double)n*(double)dt, 250, &T, out);
    if (n > 12) { float d = 0; for (int k = 0; k < 3; ++k) { const float e = out[k]-2*one[k]+two[k]; d += e*e; }
      if (sqrtf(d) > *change) *change = sqrtf(d);
      if (out[1]-eye[1] > *past) *past = out[1]-eye[1];
      if (length > *carried) *carried = length; }
    memcpy(two, one, sizeof two); memcpy(one, out, sizeof one);
  }
}
/* The same after a change of camera: the head camera's eye goes forwards at `v` m/s (jumps are not looked for in it),
 * the view changes to a rig eye in the same place, and that eye goes `back` m in each frame from its second on. The
 * speed kept is then the camera before's (THE LENT SPEED) and is let down, not dropped. */
static void stopped_lent(float v, float back, float dt, float *change, float *carried) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  float eye[3] = {.1f, .3f, 1.5f}, one[3] = {0}, two[3] = {0};
  *change = *carried = 0;
  for (int n = 0; n < 60; ++n) {
    float out[3];
    if (n && n <= 12) eye[1] += v*dt/1000; else if (n > 13 && n <= 17) eye[1] += back;
    const float length = tfp_continuity_step(&c, eye, n <= 12 ? HEAD : NATIVE, n > 12, false, 1000.0+(double)n*(double)dt, 250, &T, out);
    if (n > 12) { float d = 0; for (int k = 0; k < 3; ++k) { const float e = out[k]-2*one[k]+two[k]; d += e*e; }
      if (sqrtf(d) > *change) *change = sqrtf(d);
      if (length > *carried) *carried = length; }
    memcpy(two, one, sizeof two); memcpy(one, out, sizeof one);
  }
}
/* THE HITCH, made up: the native view, jumps looked for. The rig eye goes forwards at `v` m/s for forty frames, goes
 * `hitch` m in each of the next `frames` frames (one frame of next to nothing is a hitch; four are a stop) and then
 * goes at `after` m/s for two seconds (`v`: on as it went). `placed`: the body walks at 3 m/s meanwhile, turned by
 * 0.3 rad, and the filter is given the world. From the first of those frames on: `change`, the most the presented
 * eye's step changes from frame to frame (as a vector), and `own`, the same of the wanted eye; `carried`, the most
 * carried; `settled`, ms to the last frame more than 0.001 m is carried; `against`, the most the presented eye moves
 * away from the wanted one in a frame nothing is taken up in (a catch-up only closes). */
typedef struct Hitch { float change, own, carried, settled, against; } Hitch;
static Hitch hitched(float v, float hitch, int frames, float after, bool placed, float dt) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  Hitch h = {0, 0, 0, 0, 0};
  const float cy = cosf(.3f), sy = sinf(.3f), basis[9] = {cy, sy, 0, -sy, cy, 0, 0, 0, 1};
  float eye[3] = {.1f, .3f, 1.5f}, one[3] = {0}, two[3] = {0}, wanted_one[3] = {0}, wanted_two[3] = {0}, gap[3] = {0};
  for (int n = 0; n <= 40+frames+(int)(2000/dt); ++n) {
    const float root[3] = {3*(float)n*dt/1000, 0, 0}; float out[3], world[3], now[3], d = 0, w = 0, grown = 0, size = 0;
    if (n && n <= 40) eye[1] += v*dt/1000; else if (n > 40 && n <= 40+frames) eye[1] += hitch; else if (n) eye[1] += after*dt/1000;
    for (int k = 0; k < 3; ++k) { world[k] = root[k]; for (int a = 0; a < 3; ++a) world[k] += eye[a]*basis[a*3+k]; }
    const uint64_t taken = c.jumps+c.edges; const double tick = 1000.0+(double)n*(double)dt;
    const float length = placed ? tfp_continuity_step_in(&c, eye, world, basis, NATIVE, true, false, tick, 250, &T, out)
                                : tfp_continuity_step(&c, eye, NATIVE, true, false, tick, 250, &T, out);
    for (int k = 0; k < 3; ++k) { const float e = out[k]-2*one[k]+two[k], f = eye[k]-2*wanted_one[k]+wanted_two[k]; d += e*e; w += f*f;
      now[k] = out[k]-eye[k]; grown += (now[k]-gap[k])*gap[k]; size += gap[k]*gap[k]; }
    if (n > 40) {
      if (sqrtf(d) > h.change) h.change = sqrtf(d);
      if (sqrtf(w) > h.own) h.own = sqrtf(w);
      if (length > h.carried) h.carried = length;
      if (length > .001f) h.settled = (float)(n-41)*dt;
      if (c.jumps+c.edges == taken && size > 1e-6f && grown/sqrtf(size) > h.against) h.against = grown/sqrtf(size);
    }
    memcpy(two, one, sizeof two); memcpy(one, out, sizeof one); memcpy(wanted_two, wanted_one, sizeof two); memcpy(wanted_one, eye, sizeof one);
    memcpy(gap, now, sizeof gap);
  }
  return h;
}
/* THE TRIAL, made up: as `hitched` with one frame of nothing, and then the frame that goes on as the eye went (a
 * frame's travel forwards), and after it `count` frames whose steps are `then` (forwards and sideways, in frames'
 * travel), and from there `on` frames' travel forwards a frame for two seconds (0: at rest). From the frame that goes
 * on: `beyond`, the furthest the presented eye is outside where the wanted eye has been at any time (along each axis
 * of the root's frame, the frame it moves in); `carried`, `settled` (ms, the frame that goes on being the first),
 * `change` and `own` as there. */
typedef struct Trial { float beyond, carried, settled, change, own; } Trial;
static Trial tried(float v, float dt, bool placed, const float (*then)[2], int count, float on, bool show) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  Trial r = {0, 0, 0, 0, 0};
  const float cy = cosf(.3f), sy = sinf(.3f), basis[9] = {cy, sy, 0, -sy, cy, 0, 0, 0, 1}, travel = v*dt/1000;
  float eye[3] = {.1f, .3f, 1.5f}, one[3] = {0}, two[3] = {0}, wanted_one[3] = {0}, wanted_two[3] = {0}, low[3], high[3];
  memcpy(low, eye, sizeof low); memcpy(high, eye, sizeof high);
  for (int n = 0; n <= 42+count+(int)(2000/dt); ++n) {
    const float root[3] = {3*(float)n*dt/1000, 0, 0}; float out[3], world[3], d = 0, w = 0;
    if ((n && n <= 40) || n == 42) eye[1] += travel;
    else if (n > 42 && n <= 42+count) { eye[1] += then[n-43][0]*travel; eye[0] += then[n-43][1]*travel; }
    else if (n > 42) eye[1] += on*travel;
    for (int k = 0; k < 3; ++k) { world[k] = root[k]; for (int a = 0; a < 3; ++a) world[k] += eye[a]*basis[a*3+k]; }
    const double tick = 1000.0+(double)n*(double)dt;
    const float length = placed ? tfp_continuity_step_in(&c, eye, world, basis, NATIVE, true, false, tick, 250, &T, out)
                                : tfp_continuity_step(&c, eye, NATIVE, true, false, tick, 250, &T, out);
    for (int k = 0; k < 3; ++k) { const float e = out[k]-2*one[k]+two[k], f = eye[k]-2*wanted_one[k]+wanted_two[k]; d += e*e; w += f*f;
      low[k] = fminf(low[k], eye[k]); high[k] = fmaxf(high[k], eye[k]); }
    if (n > 41) {
      for (int k = 0; k < 3; ++k) { if (out[k]-high[k] > r.beyond) r.beyond = out[k]-high[k]; if (low[k]-out[k] > r.beyond) r.beyond = low[k]-out[k]; }
      if (sqrtf(d) > r.change) r.change = sqrtf(d);
      if (sqrtf(w) > r.own) r.own = sqrtf(w);
      if (length > r.carried) r.carried = length;
      if (length > .001f) r.settled = (float)(n-41)*dt;
      if (show && n <= 50) printf("%s%.3f", n == 42 ? "" : ", ", (double)(out[1]-(.3f+40*travel)));
    }
    memcpy(two, one, sizeof two); memcpy(one, out, sizeof one); memcpy(wanted_two, wanted_one, sizeof two); memcpy(wanted_one, eye, sizeof one);
  }
  return r;
}
/* Logged rows through the filter (tests/reference/soak-slowing-steps.h: every row one frame). In rows `from` to `to`:
 * `change`, the most the presented eye's step in the world changes from row to row as a vector, whoever's it is (the
 * judge leaves out what it calls reduced); `sum`, the correction summed; `away`, the most the presented eye moves away
 * from the wanted one in a row in which nothing is taken up (a catch-up only closes); `logged`, `logged_sum`,
 * `logged_away`: the same of the eye the log shows. `apart`: how far the presented eye is from the logged one at most
 * in the rows before `from`; `rest`: the same in rows `from` to `to`; `past`: how far the presented eye comes on the
 * other side of the wanted eye from where the log shows it. */
typedef struct Slowing { float change, sum, away, logged, logged_sum, logged_away, apart, rest, past; } Slowing;
static Slowing slowing(const SoakFrame *rows, int count, int from, int to) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  Slowing s = {0, 0, 0, 0, 0, 0, 0, 0, 0}; float one[3] = {0}, two[3] = {0}, before = 0, logged_before = 0;
  for (int i = 0; i < count && i <= to; ++i) {
    const SoakFrame *f = &rows[i]; float relative[3] = {0}, out[3], world[3], apart = 0, was = 0, d = 0, l = 0, side = 0;
    const uint64_t taken = c.jumps+c.edges;
    for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (f->wanted[k]-f->root[k])*f->axes[a*3+k];
    const float carried = tfp_continuity_step_in(&c, relative, f->wanted, f->axes, (uint32_t)f->source, f->looked != 0, false, 1000.0+(double)f->ms, 250, &T, out);
    for (int k = 0; k < 3; ++k) { world[k] = f->root[k]; for (int a = 0; a < 3; ++a) world[k] += out[a]*f->axes[a*3+k];
      apart += (world[k]-f->logged[k])*(world[k]-f->logged[k]); was += (f->logged[k]-f->wanted[k])*(f->logged[k]-f->wanted[k]);
      side -= (world[k]-f->wanted[k])*(f->logged[k]-f->wanted[k]);
      if (i >= 2) { const float e = world[k]-2*one[k]+two[k], g = f->logged[k]-2*f[-1].logged[k]+f[-2].logged[k]; d += e*e; l += g*g; } }
    apart = sqrtf(apart); was = sqrtf(was);
    if (i < from) { if (apart > s.apart) s.apart = apart; }
    else {
      if (apart > s.rest) s.rest = apart;
      if (was > 1e-4f && side/was > s.past) s.past = side/was;
      if (sqrtf(d) > s.change) s.change = sqrtf(d);
      if (sqrtf(l) > s.logged) s.logged = sqrtf(l);
      s.sum += carried; s.logged_sum += was;
      if (c.jumps+c.edges == taken && carried-before > s.away) s.away = carried-before;
      if (c.jumps+c.edges == taken && was-logged_before > s.logged_away) s.logged_away = was-logged_before;
    }
    before = carried; logged_before = was;
    memcpy(two, one, sizeof two); memcpy(one, world, sizeof one);
  }
  return s;
}
/* THE LET-DOWN AT A SLOWING, made up: the body stands, the native view, jumps looked for. The rig eye rests and then
 * goes forwards at `v` m/s: a motion that begins, each frame a jump for the time of a run, then that speed believed,
 * the presented eye behind the rig eye and gathering way along the curve. With `ahead` the rig is re-seated 1.2 m
 * backwards in the next frame (a jump, taken up; the presented eye, gone on, is then in front of it). Then the rig eye
 * goes on at `slow` m/s, the same way: slowing down against the speed believed. From that frame on: `change`, the most
 * the presented eye's step changes from frame to frame (as a vector), and `own`, the same of the rig eye; `past`, how
 * far the presented eye comes in front of the rig eye (with `ahead`: further in front of it than it was); `away`, the
 * most it moves away from the rig eye in a frame in which nothing is taken up; `carried`, the most carried;
 * `settled`, ms to the last frame more than 0.005 m is carried. */
typedef struct Slowed { float change, own, past, away, carried, settled; } Slowed;
static Slowed slowed(float v, float slow, bool ahead, float dt) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  Slowed s = {0, 0, 0, 0, 0, 0}; float eye[3] = {.1f, .3f, 1.5f}, one[3] = {0}, two[3] = {0}, eye_one[3] = {0}, eye_two[3] = {0}, off = 0, start = 0;
  int believed = -1, at = -1;
  for (int n = 0; n < (int)(3000/dt); ++n) {
    float out[3]; const uint64_t taken = c.jumps+c.edges;
    if (believed < 0) { if (n > 10) eye[1] += v*dt/1000; }
    else if (ahead && n == believed+1) eye[1] -= 1.2f;
    else { eye[1] += slow*dt/1000; if (at < 0) at = n; }
    const float carried = tfp_continuity_step(&c, eye, NATIVE, true, false, 1000.0+(double)n*(double)dt, 250, &T, out);
    if (believed < 0 && n > 11 && c.jumps == taken && c.speed[1] > v/2) believed = n;
    if (at >= 0) { float d = 0, w = 0;
      for (int k = 0; k < 3; ++k) { const float e = out[k]-2*one[k]+two[k], g = eye[k]-2*eye_one[k]+eye_two[k]; d += e*e; w += g*g; }
      if (n == at) start = ahead ? off : 0;
      if (sqrtf(d) > s.change) s.change = sqrtf(d);
      if (sqrtf(w) > s.own) s.own = sqrtf(w);
      if (out[1]-eye[1]-start > s.past) s.past = out[1]-eye[1]-start;
      if (c.jumps+c.edges == taken && fabsf(out[1]-eye[1])-fabsf(off) > s.away) s.away = fabsf(out[1]-eye[1])-fabsf(off);
      if (carried > s.carried) s.carried = carried;
      if (carried > .005f) s.settled = (float)(n-at)*dt; }
    off = out[1]-eye[1];
    memcpy(two, one, sizeof two); memcpy(one, out, sizeof one); memcpy(eye_two, eye_one, sizeof two); memcpy(eye_one, eye, sizeof one);
  }
  return s;
}
/* THE LET-DOWN AT A SLOWING and the frames after it, made up: the body stands, the native view, jumps looked for. The
 * rig eye rests and then goes forwards at `v` m/s; each frame is a jump for the time of a run, then that speed is
 * believed. The next frame goes `part` of it (slowing down: the let-down begins where the step dropped is over the
 * measure). Then `scene`: 0, the rig is re-seated 1.2 m backwards in one frame and stands (a jump, taken up, and the
 * wanted eye is then behind the presented one); 1, the rig eye goes back at `v` (a run of jumps, a speed believed);
 * 2, it stands (a stop, followed); 3, the camera changes, the new eye 0.4 m to the side, and stands. From the frame
 * that slows down on: `change`, the most the presented eye's step changes from frame to frame (as a vector); `past`,
 * how far the presented eye comes in front of the wanted one along the way; `settled`, ms to the last frame more
 * than 0.005 m is carried. `jumped`: how much further in front of the wanted eye the presented one is after the
 * frame of the re-seat than the re-seat itself put it (scene 0). `let`: how far the let-down could go in the frame
 * that slows down (the step dropped less pop_accel, where it is over the measure). */
typedef struct Taken { float change, past, settled, jumped, let; } Taken;
static Taken taken(float v, float part, int scene, float dt) {
  static const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  TfpContinuity c; memset(&c, 0, sizeof c);
  Taken s = {0, -9, 0, 0, 0}; const float step = v*dt/1000, measure = fmaxf(T.pop_m, T.pop_accel*dt*dt/1e6f);
  float eye[3] = {.1f, .6f, 1.5f}, one[3] = {0}, two[3] = {0}, off = 0; int believed = -1;
  const int warm = (int)(1500/dt)+3, frames = 2*warm+(int)(2500/dt);
  if ((1-part)*step > measure) s.let = (1-part)*step-T.pop_accel*dt*dt/1e6f;
  for (int n = 0; n < frames; ++n) {
    float out[3]; const uint64_t jumps = c.jumps; const int m = believed < 0 ? 0 : n-believed; uint32_t source = NATIVE;
    if (n > 12) eye[1] += m <= 0 ? step : m == 1 ? part*step : scene == 1 ? -step : scene == 0 && m == 2 ? -1.2f : 0;
    if (scene == 3 && m == 2) eye[0] += .4f;
    if (scene == 3 && m >= 2) source = HEAD;
    const float carried = tfp_continuity_step_in(&c, eye, eye, basis, source, true, false, 1000.0+(double)n*(double)dt, 250, &T, out);
    if (believed < 0 && n > 13 && c.jumps == jumps && tfp_continuity_length(c.speed) > v/2) believed = n;
    if (believed >= 0 && n > believed) { float d = 0;
      for (int k = 0; k < 3; ++k) { const float e = out[k]-2*one[k]+two[k]; d += e*e; }
      if (sqrtf(d) > s.change) s.change = sqrtf(d);
      if (out[1]-eye[1] > s.past) s.past = out[1]-eye[1];
      if (carried > .005f) s.settled = (float)(n-believed)*dt;
      if (scene == 0 && n == believed+2) s.jumped = out[1]-eye[1]-(off+1.2f); }
    off = out[1]-eye[1];
    memcpy(two, one, sizeof two); memcpy(one, out, sizeof one);
  }
  return s;
}
#define ROWS(a) a, (int)(sizeof a/sizeof *a)

/* THE WAY BACK. Logged rows through the filter, started from nothing at the first: `step` the largest step of the
 * presented eye in the rows from `from` on, `carried` the most carried there, `off` how far the presented eye gets
 * from the middle of the two places (`a`, `b`: two of the rows) at most, `speed` the largest speed kept after a row
 * before `from` that is not the first two (m/s). */
typedef struct Flipped { float step, carried, off, speed; } Flipped;
static Flipped flipped_logged(const SoakFrame *rows, int count, int from, int a, int b) {
  TfpContinuity c; memset(&c, 0, sizeof c); Flipped f = {0, 0, 0, 0}; float shown[3] = {0, 0, 0};
  for (int i = 0; i < count; ++i) {
    const SoakFrame *r = &rows[i]; float relative[3] = {0}, out[3], world[3], step = 0, off = 0;
    for (int x = 0; x < 3; ++x) for (int k = 0; k < 3; ++k) relative[x] += (r->wanted[k]-r->root[k])*r->axes[x*3+k];
    const float carried = tfp_continuity_step_in(&c, relative, r->wanted, r->axes, (uint32_t)r->source, r->looked != 0, false, 1000.0+(double)r->ms, 250, &T, out);
    for (int k = 0; k < 3; ++k) { world[k] = r->root[k]; for (int x = 0; x < 3; ++x) world[k] += out[x]*r->axes[x*3+k];
      const float middle = (rows[a].wanted[k]+rows[b].wanted[k])/2;
      step += (world[k]-shown[k])*(world[k]-shown[k]); off += (world[k]-middle)*(world[k]-middle); }
    const float fast = sqrtf(c.speed[0]*c.speed[0]+c.speed[1]*c.speed[1]+c.speed[2]*c.speed[2]);
    if (i >= 2 && i < from && fast > f.speed) f.speed = fast;
    if (i >= from) { if (sqrtf(step) > f.step) f.step = sqrtf(step); if (carried > f.carried) f.carried = carried; if (sqrtf(off) > f.off) f.off = sqrtf(off); }
    memcpy(shown, world, sizeof world);
  }
  return f;
}
/* Made up: the eye goes back and forth between two places `far` apart along x every frame, `dt_ms` a frame; jumps
 * are not looked for during the first `blind_ms`, then they are (an event) while it goes on for 600 ms, then it stands
 * at the nearer place for 600 ms. `off`: the farthest the presented eye gets from the middle of the two places from
 * the event on; `step`: its largest step there; `speed`: the largest speed kept at a frame not looked at after the
 * first two (m/s). */
static Flipped flipped(float far, float dt_ms, float blind_ms) {
  Run r; start(&r); Flipped f = {0, 0, 0, 0}; float last = 0; int n = 0;
  for (float t = 0; t < blind_ms+1200; t += dt_ms, ++n) {
    const bool looked = t >= blind_ms, stands = t >= blind_ms+600;
    const float x = stands ? 0 : (n & 1) ? far : 0;
    frame(&r, x, 1.5f, 0.1f, NATIVE, looked, false, dt_ms);
    const float fast = sqrtf(r.c.speed[0]*r.c.speed[0]+r.c.speed[1]*r.c.speed[1]+r.c.speed[2]*r.c.speed[2]);
    if (!looked && n >= 2 && fast > f.speed) f.speed = fast;
    if (looked) { const float off = fabsf(r.shown[0]-far/2), step = fabsf(r.shown[0]-last);
      if (off > f.off) f.off = off;
      if (step > f.step) f.step = step;
      if (r.correction > f.carried) f.carried = r.correction; }
    last = r.shown[0];
  }
  return f;
}
/* THE CUT (round 19). Before the filter had it the head was never given: the same calls without it, so these checks
 * can be watched failing on the filter of 0.6.0-feel.51. */
#ifdef TFP_CONTINUITY_CUT_M
#define CUT_STEP(c, rel, world, basis, head, source, detect, tick, out) \
  tfp_continuity_step_cut(c, rel, world, basis, head, source, detect, false, tick, 250, &T, out)
#define CUTS(c) ((c)->cuts)
#define CUT_STEP_Q(c, rel, world, basis, head, source, detect, quiet, tick, out) \
  tfp_continuity_step_cut(c, rel, world, basis, head, source, detect, quiet, tick, 250, &T, out)
#else
#define CUT_STEP_Q(c, rel, world, basis, head, source, detect, quiet, tick, out) \
  ((void)(head), tfp_continuity_step_in(c, rel, world, basis, source, detect, quiet, tick, 250, &T, out))
#define CUT_STEP(c, rel, world, basis, head, source, detect, tick, out) \
  ((void)(head), tfp_continuity_step_in(c, rel, world, basis, source, detect, false, tick, 250, &T, out))
#define CUTS(c) ((void)(c), 0u)
#endif
/* The head node of a logged row, in the root's frame: the point the head camera's eye is built on, before the eye
 * damping (the wanted eye less the damping, less 0.18 m along the level view and 0.04 m up; replay.py head_node). */
static void head_node(const HeadFrame *f, float *head) {
  float level[2] = {f->view[0], f->view[1]}; float length = sqrtf(level[0]*level[0]+level[1]*level[1]);
  if (length < 1e-4f) { level[0] = f->axes[3]; level[1] = f->axes[4]; length = sqrtf(level[0]*level[0]+level[1]*level[1]); }
  const float node[3] = {f->wanted[0]-f->damp[0]-level[0]/length*.18f, f->wanted[1]-f->damp[1]-level[1]/length*.18f, f->wanted[2]-f->damp[2]-.04f};
  for (int a = 0; a < 3; ++a) { head[a] = 0; for (int k = 0; k < 3; ++k) head[a] += (node[k]-f->root[k])*f->axes[a*3+k]; }
}
/* Logged head-camera rows (tests/reference/soak-30-cut.h) through the filter, the head given where the helper gives it
 * (a head-camera frame of a body on foot; `always`: in every head-camera row, off foot too). The presented eye's step
 * is measured against the root's place in the world (the eye less the root: a root that turns does not move it, a
 * root that goes on does not count), and its change from frame to frame as a vector, | (c-b) - (b-a) |, for frame c:
 *   cut      that change in the frame `cut` (the game's pose cut)
 *   next     the same in the frame after it, less the wanted eye's own step against the root there (the arc's start)
 *   arc      the most that change is beyond the wanted eye's own, in the frames from `cut`+2 on (both steps after the cut)
 *   logged   the change in the frame `cut` as the log shows it
 *   cuts     cuts taken up; taken: jumps and changes of camera taken up from `cut`+2 on (the arc is followed)
 *   apart    the most the presented eye is from the wanted one where nothing is cut (0: the filter did nothing) */
typedef struct CutScene { float cut, next, arc, logged, apart; unsigned cuts, taken; } CutScene;
static CutScene cut_scene(const HeadFrame *rows, int count, int cut, bool always) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  CutScene s = {0, 0, 0, 0, 0, 0, 0}; float shown[3][3] = {{0}}, wanted[3][3] = {{0}}, logged[3][3] = {{0}};
  for (int i = 0; i < count; ++i) {
    const HeadFrame *f = &rows[i]; float relative[3] = {0}, head[3] = {0}, out[3], world[3], apart = 0;
    for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (f->wanted[k]-f->root[k])*f->axes[a*3+k];
    head_node(f, head);
    const bool given = f->kind == 4 && f->source == HEAD && (always || !f->off_foot);
    const uint64_t taken = c.jumps+c.edges;
    CUT_STEP(&c, relative, f->wanted, f->axes, given ? head : NULL, (uint32_t)f->source, f->looked != 0, 1000.0+(double)f->ms, out);
    if (i >= cut+2) s.taken += (unsigned)(c.jumps+c.edges-taken);
    for (int k = 0; k < 3; ++k) { world[k] = 0; for (int a = 0; a < 3; ++a) world[k] += out[a]*f->axes[a*3+k]; /* the eye less the root */
      apart += (world[k]+f->root[k]-f->wanted[k])*(world[k]+f->root[k]-f->wanted[k]); }
    for (int n = 0; n < 2; ++n) for (int k = 0; k < 3; ++k) { shown[n][k] = shown[n+1][k]; wanted[n][k] = wanted[n+1][k]; logged[n][k] = logged[n+1][k]; }
    for (int k = 0; k < 3; ++k) { shown[2][k] = world[k]; wanted[2][k] = f->wanted[k]-f->root[k]; logged[2][k] = f->logged[k]-f->root[k]; }
    if (CUTS(&c) == 0 && sqrtf(apart) > s.apart) s.apart = sqrtf(apart);
    if (i < 2) continue;
    float e = 0, w = 0, l = 0, own = 0;
    for (int k = 0; k < 3; ++k) { const float de = shown[2][k]-2*shown[1][k]+shown[0][k], dw = wanted[2][k]-2*wanted[1][k]+wanted[0][k];
      const float dl = logged[2][k]-2*logged[1][k]+logged[0][k], sw = wanted[2][k]-wanted[1][k];
      e += de*de; w += dw*dw; l += dl*dl; own += sw*sw; }
    e = sqrtf(e); w = sqrtf(w); l = sqrtf(l); own = sqrtf(own);
    if (i == cut) { s.cut = e; s.logged = l; }
    if (i == cut+1) s.next = e-own;
    if (i >= cut+2 && e-w > s.arc) s.arc = e-w;
  }
  s.cuts = (unsigned)CUTS(&c);
  return s;
}
/* A made-up pose cut out of an emote, `dt` ms a frame: the head stands against a standing root (the emote), then in one
 * frame is `far` m further on against the root, the root lunging at 5 m/s from that frame; after it the head goes on
 * against the root from rest at `accel` m/s^2 up to `top` m/s (a dive's own arc). `jump` false: no cut, a motion that only
 * begins (the head from rest at `accel` from the first frame). Measured as cut_scene does (the eye less the root):
 * the change of the presented eye's step in the frame of the cut, the most beyond the wanted eye's own after it, what
 * is carried at most, how long until it is under 0.01 m (ms from the cut), and the cuts taken. */
typedef struct MadeCut { float cut, after, carried, settled; unsigned cuts; } MadeCut;
static MadeCut made_cut(float far, float accel, float top, float dt_ms, bool jump) {
  TfpContinuity c; memset(&c, 0, sizeof c);
  MadeCut m = {0, 0, 0, 0, 0}; const float dt = dt_ms/1000; float head = 0, speed = 0, root = 0, shown[3] = {0}, wanted[3] = {0};
  const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; const int rest = (int)(300/dt_ms), total = rest+(int)(800/dt_ms);
  for (int i = 0; i < total; ++i) {
    if (i == rest && jump) head += far;
    if (i > rest || (!jump && i == rest)) { speed += accel*dt; if (speed > top) speed = top; head += speed*dt; }
    if (i >= rest) root += 5*dt;
    const float relative[3] = {.1f, head, 1.5f}, world[3] = {.1f, root+head, 1.5f}; float out[3];
    const float carried = CUT_STEP(&c, relative, world, basis, relative, HEAD, false, 1000.0+i*(double)dt_ms, out);
    if (carried > m.carried) m.carried = carried;
    if (i >= rest && carried > .01f) m.settled = (float)(i-rest+1)*dt_ms;
    for (int n = 0; n < 2; ++n) { shown[n] = shown[n+1]; wanted[n] = wanted[n+1]; }
    shown[2] = out[1]; wanted[2] = head;
    if (i < 2) continue;
    const float e = fabsf(shown[2]-2*shown[1]+shown[0]), w = fabsf(wanted[2]-2*wanted[1]+wanted[0]);
    if (i == rest) m.cut = e;
    if (i > rest+1 && e-w > m.after) m.after = e-w;
  }
  m.cuts = (unsigned)CUTS(&c);
  return m;
}
/* A plain head-camera scene for THE CUT's exclusions: the root at `root` turned by `yaw`, the head node at `node` in
 * the world, the eye 0.18 m ahead of it along the world's +y and 0.04 m up (a view that does not turn). One frame
 * through the filter; `give`: the head node is given (in the root's frame), as the helper gives it on foot. */
static float plain_frame(TfpContinuity *c, const float *root, float yaw, const float *node, uint32_t source, bool detect, bool quiet, double tick, bool give) {
  const float co = cosf(yaw), si = sinf(yaw), basis[9] = {co, si, 0, -si, co, 0, 0, 0, 1};
  const float eye[3] = {node[0], node[1]+.18f, node[2]+.04f}; float relative[3] = {0}, head[3] = {0}, out[3];
  for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) { relative[a] += (eye[k]-root[k])*basis[a*3+k]; head[a] += (node[k]-root[k])*basis[a*3+k]; }
  return CUT_STEP_Q(c, relative, eye, basis, give ? head : NULL, source, detect, quiet, tick, out);
}
/* How a cut's ease ends: a made-up cut of `far` m from rest, the head still after it (no arc), `dt_ms` a frame. The
 * presented eye's step change (| (c-b) - (b-a) |, in the root's frame) at most in the last frame of the ease and the
 * two after it, and at most anywhere after the cut's own frame. */
typedef struct CutEnd { float end, most; unsigned cuts; } CutEnd;
static CutEnd cut_end(float far, float dt_ms) {
  TfpContinuity c; memset(&c, 0, sizeof c); CutEnd e = {0, 0, 0};
  const float basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; const int rest = (int)(300/dt_ms), total = rest+(int)(800/dt_ms);
  float shown[3] = {0}; double ends = 1e9;
  for (int i = 0; i < total; ++i) {
    const float head = i >= rest ? far : 0; const float relative[3] = {.1f, head, 1.5f}; float out[3];
    const double tick = 1000.0+i*(double)dt_ms;
    CUT_STEP(&c, relative, relative, basis, relative, HEAD, false, tick, out);
    if (i == rest) ends = c.started+c.duration;
    for (int n = 0; n < 2; ++n) shown[n] = shown[n+1];
    shown[2] = out[1];
    if (i < 2 || i <= rest) continue;
    const float change = fabsf(shown[2]-2*shown[1]+shown[0]);
    if (change > e.most) e.most = change;
    if (tick >= ends-dt_ms && tick < ends+2*dt_ms && change > e.end) e.end = change;
  }
  e.cuts = (unsigned)CUTS(&c);
  return e;
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
  /* Farther than 2.5 m is a real teleport: shown at once, nothing carried. More than 1.6 m is not carried in full.
   * (1.5 m and 1.0 m until 0.6.0-feel.39: a change of camera in a sprint is 1.2 m and was cut at 1.0.) */
  start(&r);
  for (int i = 0; i < 5; ++i) frame(&r, 0, 0, 0, NATIVE, true, false, 33.3f);
  frame(&r, 3, 0, 0, NATIVE, true, false, 33.3f);
  CHECK(r.correction == 0 && r.shown[0] == 3 && r.c.restarts == 1);
  frame(&r, 3, 0, 0, HEAD, false, false, 33.3f);
  frame(&r, 3, 1.9f, 0, NATIVE, true, false, 33.3f);
  CHECK(fabsf(r.correction-1.6f) < 1e-4f && fabsf(r.shown[1]-.3f) < 1e-4f);
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
  /* A change of camera while sprinting (seen in the third soak of 2026-11-22: a swap in a sprint, the head camera's
   * eye 0.87 m ahead of the root and the rig's 0.29 m behind it): 1.17 m between the two eyes. All of it is carried
   * and eased; with a limit of 1.0 m the 0.17 m over it was shown at once and the eye stood still in the world for
   * that frame (0.162 m a frame before it, 0.009 m in it, 0.247 m after). */
  {
    TfpContinuity c; tfp_continuity_reset(&c);
    float out[3], eye[3] = {.15f, .87f, .70f};
    double tick = 1000;
    for (int i = 0; i < 20; ++i) { tfp_continuity_step(&c, eye, 1, false, false, tick, 100, &T, out); tick += 33.3; }
    const float rig[3] = {-.06f, -.28f, .78f};
    const float carried = tfp_continuity_step(&c, rig, 2, true, false, tick, 100, &T, out);
    CHECK(fabsf(out[0]-eye[0]) < .005f && fabsf(out[1]-eye[1]) < .005f && fabsf(out[2]-eye[2]) < .005f);
    CHECK(carried > 1.15f && carried < 1.19f);
  }
  /* The three largest steps of the soak of 2026-11-22 on 0.6.0-feel.39 (tests/reference/soak-6-steps.h; the root's
   * axes there are fitted, the log did not carry them). In all three the body root's axes turned while something was
   * carried or taken up, and the presented eye, kept against the root, went round with them:
   *  - 22:38:05.662, head-camera frames inside a swap: 0.149 m of the eye taken up while the body stood; in the next
   *    frame the body began to sprint and its axes turned by 0.26 rad in that one frame. The presented eye, 0.87 m
   *    from the root, went 0.176 m in a frame the root moved 0.041 m.
   *  - 22:33:17.3 to .7, the native view after a hold ended on a landing: the root turned on the spot at about
   *    10 rad/s for a third of a second while the rig eye stood in the world 0.6 m from it. In the root's frame the
   *    eye went round, every frame's step off the last by more than the measure: jump after jump, 0.61 m carried in
   *    the end, and 0.139 m in the frame the turning stopped.
   *  - 22:28:33.416, head-camera frames after a change of camera: the eye 1.68 m from a root whose axes turned by
   *    0.09 to 0.17 rad a frame; 0.048 then 0.141 m, which is 0.047 m beyond the root's own change of step.
   * The presented eye's step does not change by more than 0.05 m in a frame beyond what the root's own step does
   * (the filter of feel.39 on these frames: 0.133 m, 0.236 m with 0.617 m carried, and 0.047 m). */
  {
    const Soak sprint = soak(soak6_sprint_in_swap, (int)(sizeof soak6_sprint_in_swap/sizeof *soak6_sprint_in_swap), 2);
    printf("soak 6, sprint begun inside a swap: step change at most %.3f m (the log: %.3f m), %.3f m beyond the root's own, %.3f m carried\n",
           (double)sprint.change, (double)sprint.logged_change, (double)sprint.beyond, (double)sprint.carried);
    CHECK(sprint.beyond < .05f);
    /* from the frame the hold ended in; before it the body falls and nothing is looked for */
    const Soak turn = soak(soak6_turn_after_hold, (int)(sizeof soak6_turn_after_hold/sizeof *soak6_turn_after_hold), 7);
    printf("soak 6, the root turning on the spot after a hold: step change at most %.3f m (the log: %.3f m), %.3f m beyond the root's own, %.3f m carried\n",
           (double)turn.change, (double)turn.logged_change, (double)turn.beyond, (double)turn.carried);
    CHECK(turn.beyond < .05f);
    CHECK(turn.carried < .25f);
    /* from the change of camera (22:28:33.316) on */
    const Soak camera = soak(soak6_after_camera_change, (int)(sizeof soak6_after_camera_change/sizeof *soak6_after_camera_change), 16);
    printf("soak 6, after a change of camera: step change at most %.3f m (the log: %.3f m), %.3f m beyond the root's own, %.3f m carried\n",
           (double)camera.change, (double)camera.logged_change, (double)camera.beyond, (double)camera.carried);
    CHECK(camera.beyond < .03f);
  }
  /* The soaks of 2026-11-22 on 0.6.0-feel.40 (tests/reference/soak-8-steps.h; the root's axes are the logged ones):
   * eleven steps of 0.06 to 0.16 m were counted as the filter's by the lengths of the steps. Four of them came at
   * the end of a run of jumps, and a fifth had it on top of the body's own (below). NOT MENDED: three of the four are
   * still there, and the filter is only held to adding nothing to them.
   * A run ends at the first frame that is no jump, and that frame is followed as an ordinary one: the presented eye,
   * which went on at the kept speed through the run, takes the wanted eye's step in that frame (under the measure
   * off the kept speed, or it were a jump) while the curve begun at the last jump is at its steepest: up to 0.04 m
   * and 0.027 m at 30 frames a second, together over the 0.06 m that is counted.
   *  - 00:36:03.183, the native view after two quick swaps, the body standing: the rig eye went 0.070, 0.063 and
   *    0.060 m in three frames and stood again (the kept speed: 0.022 m a frame). 0.049 and 0.042 m off the kept
   *    speed were jumps; 0.039 m was 0.001 m under the measure, and the presented eye went 0.003 m and then 0.070 m.
   *  - 00:33:35.314, the native view after a swap in a sprint: the rig eye all but stopped, two frames 0.046 and
   *    0.053 m off the kept speed, the third 0.039 m: 0.143 m and then 0.081 m.
   *  - 00:26:06.399, the native view entered again after a swap in a dive: the rig re-seated by 0.5 m a frame after
   *    the change of camera, one jump; the frame after it, 0.02 m off the kept speed, was followed, and the one after
   *    that is the step: the rig eye's own slowing (0.025 m, followed as a motion slowing down) on the curve's
   *    0.030 m. 0.078 m by the lengths, 0.066 m now.
   *  - 00:30:10.988, the same after a swap in a sprint, 0.064 m: gone with the speed believed for a motion dying
   *    down (below), which gave the frames before it another speed.
   * The mending that was tried for these and taken out is THE RUN'S END IS NO SPEED, below. Its four checks were
   * "the log has the step and the filter has none of 0.06 m" for each; three are now "no more than the log's". */
  {
    const Judged three = judged(ROWS(soak9_three_frames), 2, 20);
    printf("soak 9, three frames of the rig: the filter's largest step %.3f m (the log: %.3f m)\n", (double)three.step, (double)three.logged);
    CHECK(three.logged > .06f && three.step < three.logged+.005f);
    /* from the frame after the body's own change of speed (row 20, below) */
    const Judged stop = judged(ROWS(soak9_sprint_swap), 21, 42);
    printf("soak 9, the rig eye stopping after a swap in a sprint: the filter's largest step %.3f m (the log: %.3f m)\n", (double)stop.step, (double)stop.logged);
    CHECK(stop.logged > .06f && stop.step < stop.logged+.005f);
    const Judged camera = judged(ROWS(soak8_swap_in_sprint), 2, 28);
    printf("soak 8, the native view entered after a swap in a sprint: the filter's largest step %.3f m (the log: %.3f m)\n", (double)camera.step, (double)camera.logged);
    CHECK(camera.logged > .06f && camera.step < .06f);
    const Judged dive = judged(ROWS(soak8_swap_in_dive), 24, 42);
    printf("soak 8, the native view entered after a swap in a dive: the filter's largest step %.3f m (the log: %.3f m)\n", (double)dive.step, (double)dive.logged);
    CHECK(dive.logged > .06f && dive.step < dive.logged+.005f);
  }
  /* THE RUN'S END IS NO SPEED. The way those four were to be mended (tried on the branch for 0.6.0-feel.41 and taken
   * out: the frame that ends a run taken up, and what the run's jump frames and it agree on believed as the eye's
   * new speed and handed to the curve) hid them from the count by lengths and made a fault of its own, which that
   * count could not see: the run's frames are the re-seat, and a re-seat is no speed.
   *  - A rig re-seated in two frames, 0.09 m in each, that then creeps on by 0.5 mm a frame: the two jumps and the
   *    creeping frame go the same way, none slower than the one before, so 0.09 m a frame was believed for an eye at
   *    rest and handed to the curve: at 16.7 ms a frame the presented eye was thrown 0.072 m backwards in one frame
   *    and 0.24 m in all, 0.41 m was carried for a re-seat of 0.18 m, and it took 400 ms (at 8 ms: 0.058 m a frame,
   *    0.41 m in all, 0.59 m carried). Without that hand-over it eases 0.18 m in 200 ms and never goes back.
   *  - In the second soak, twice (tests/reference/soak-9-runs.h): 00:27:21.652, the rig re-seated by 0.08 and
   *    0.49 m and standing: 2.38 m/s believed for an eye going 0.01 m/s, and what was carried grew from 0.446 to
   *    0.532 m over the next two frames while the wanted eye stood; 00:30:02.242, re-seated by 0.07, 0.14 and
   *    0.18 m: 2.80 m/s believed for 0.2 m/s, and the next two frames were jumps for it.
   *  - An eye going 2 m/s re-seated by 0.15 m against its way: the jump goes the other way from the frames after it,
   *    one frame agrees with itself, no speed was believed (0) and the eye that went on was taken up frame after
   *    frame: 0.21 to 0.23 m carried for 0.15 m.
   * What the eye does after the run is its speed. These hold the filter to what it does without that hand-over,
   * whatever is tried next for the four. */
  {
    static const float times[3] = {16.7f, 8.0f, 33.3f};
    for (int i = 0; i < 3; ++i) {
      const Crept r = crept(times[i]);
      printf("a rig re-seated in two frames that creeps on, %.1f ms a frame: %.4f m back in a frame at most, %.3f m carried, settled after %.0f ms\n",
             (double)times[i], (double)r.back, (double)r.carried, (double)r.settled);
      CHECK(r.back < .01f);
      CHECK(r.settled <= 250);
      const float carried = against(2, times[i]);
      printf("an eye going 2 m/s re-seated 0.15 m against its way, %.1f ms a frame: %.3f m carried\n", (double)times[i], (double)carried);
      CHECK(carried < .16f);
    }
    const Rested rest = rested(ROWS(soak9_reseat_then_rest), 16, 21);
    printf("soak 9, a rig re-seated in two frames and standing: %.2f m/s believed for %.2f m/s (%.2f m/s off), what is carried grows by %.3f m\n",
           (double)rest.believed, (double)rest.real, (double)rest.off, (double)rest.grown);
    CHECK(rest.off < .3f);
    CHECK(rest.grown < .002f);
    const Rested three = rested(ROWS(soak9_reseat_in_three), 15, 17);
    printf("soak 9, a rig re-seated in three frames and all but standing: %.2f m/s believed for %.2f m/s (%.2f m/s off), what is carried grows by %.3f m\n",
           (double)three.believed, (double)three.real, (double)three.off, (double)three.grown);
    CHECK(three.off < .3f);
    CHECK(three.grown < .002f);
  }
  /* The four steps again by the change of the step as a vector (what replay.py counts now), which sees a change of
   * direction: 0.068, 0.088 (00:33:35.513, six frames after the one named above, which is 0.074), 0.064 and 0.078 m
   * in the log. With the hand-over 00:36:03.183 was gone from the count by lengths and back four frames later as the
   * eye turning round, 0.074 m. The filter adds nothing to them by this measure either, and the one that is mended
   * is mended by it too. */
  {
    const Judged three = judged(ROWS(soak9_three_frames), 2, 20), stop = judged(ROWS(soak9_sprint_swap), 21, 42);
    /* the native view's rows: the head camera's before them have a step of their own by this measure (00:30:10.655) */
    const Judged camera = judged(ROWS(soak8_swap_in_sprint), 15, 28), dive = judged(ROWS(soak8_swap_in_dive), 24, 42);
    printf("the same four by the change of the step as a vector: %.3f, %.3f, %.3f and %.3f m (the log: %.3f, %.3f, %.3f and %.3f m)\n", (double)three.turned,
           (double)stop.turned, (double)camera.turned, (double)dive.turned, (double)three.logged_turned, (double)stop.logged_turned, (double)camera.logged_turned,
           (double)dive.logged_turned);
    CHECK(three.logged_turned > .06f && three.turned < three.logged_turned+.005f);
    CHECK(stop.logged_turned > .06f && stop.turned < stop.logged_turned+.005f);
    CHECK(camera.logged_turned > .06f && camera.turned < .06f);
    CHECK(dive.logged_turned > .06f && dive.turned < dive.logged_turned+.005f);
  }
  /* And one came from the speed believed when a run's time is over. 00:26:06.131, the head camera after a swap in
   * a dive: the head was thrown and its eye, against the root, went 0.26, 0.21, 0.136 and 0.083 m in four frames,
   * each slower than the one before. Of four frames that agree the middle was believed, 0.175 m a frame; the next
   * frame went 0.053 m, shorter than believed and the same way, so it was followed as the motion slowing down, and
   * the 0.12 m believed too much showed at once: the presented eye went 0.192 m and then 0.115 m while the wanted
   * eye's step changed by 0.032 m. A motion that is dying down is believed at its newest frame. */
  {
    const Judged thrown = judged(ROWS(soak8_swap_in_dive), 2, 23);
    printf("soak 8, the head thrown in a dive: the filter's largest step %.3f m (the log: %.3f m)\n", (double)thrown.step, (double)thrown.logged);
    CHECK(thrown.logged > .06f && thrown.step < .06f);
    /* tfp_continuity_believed by itself: dying down, the newest; otherwise as before (of two the slower, of three or
     * four the middle); a frame the other way ends what counts */
    TfpContinuity c; memset(&c, 0, sizeof c);
    const float dying[4] = {7.8f, 6.3f, 4.1f, 2.5f}, rising[4] = {2.5f, 4.1f, 6.3f, 7.8f}, alike[4] = {3, 9, 3.1f, 3.2f}, turned[4] = {-5, 6, 4, 2};
    for (int i = 0; i < 4; ++i) { c.recent[i][0] = dying[i]; c.recent[i][1] = rising[i]; c.recent[i][2] = alike[i]; }
    CHECK(tfp_continuity_believed(&c, 0, 4) == 2.5f && tfp_continuity_believed(&c, 0, 3) == 2.5f);
    CHECK(fabsf(tfp_continuity_believed(&c, 1, 4)-5.2f) < 1e-5f && tfp_continuity_believed(&c, 1, 3) == 6.3f && tfp_continuity_believed(&c, 1, 2) == 6.3f);
    CHECK(fabsf(tfp_continuity_believed(&c, 2, 4)-3.15f) < 1e-5f);
    for (int i = 0; i < 4; ++i) c.recent[i][0] = turned[i];
    CHECK(tfp_continuity_believed(&c, 0, 4) == 2 && tfp_continuity_believed(&c, 0, 1) == 0);
  }
  /* Six of the eleven are the body's own change of speed in one frame, which the filter passes on as it passes on
   * all the body's travel (positions are in the body's frame). The root alone changes its speed in one frame, the
   * eye a frame later: the first frame reads as the root re-seated and the eye goes on as it went; in the next the
   * root's new course is kept and the eye, against the root, has the new speed whole.
   *  - 00:21:53.294, a swap in a sprint: the root's step went from 0.237 to 0.053 m (the body stopped for two frames),
   *    the wanted eye's from 0.228 to 0.052 m a frame later, the presented eye's from 0.201 to 0.038 m: 0.163 m of
   *    the wanted eye's own 0.175 m. It is counted as the filter's only because 0.18 m of the change of camera was
   *    still carried. Two frames on (.362) the root went on again, 0.053, 0.110, 0.138 m: 0.072 m.
   *  - 00:33:34.846, the head camera in the same action: the root's step from 0.199 to 0.108 m, the presented eye's
   *    from 0.204 to 0.104 m a frame later.
   *  - 00:30:09.385, a sprint begun in the head camera: the root's step 0.000, 0.041, 0.091 m while its axes turned 0.14 and
   *    0.08 rad; here the end of a run added to it (0.100 m in the log).
   *  - 00:27:13.539, half a second into a dive backwards, and 00:38:46.845, a dive begun: the same, 0.094 and 0.098 m of the wanted eye's
   *    own 0.081 and 0.106 m; both are the last frames of their Trace windows and are not among the rows here.
   * The filter must not add to these. (To carry the root's change of speed over to the curve hides most of them, at
   * 0.06 m of eye ahead of the body for each m/s; the landing in soak6_turn_after_hold, a root that stops from
   * 7.5 m/s in one frame, then carries 0.79 m of eye into the ground. Not done.) */
  {
    const Judged sprint = judged(ROWS(soak8_stop_in_sprint), 2, 42);
    printf("soak 8, the body stopped in a sprint for two frames: the largest step %.3f m (the log: %.3f m)\n", (double)sprint.step, (double)sprint.logged);
    CHECK(sprint.step < sprint.logged+.005f);
    const Judged head = judged(ROWS(soak9_sprint_swap), 2, 20);
    printf("soak 9, the same in the head camera: the largest step %.3f m (the log: %.3f m)\n", (double)head.step, (double)head.logged);
    CHECK(head.step < head.logged+.005f);
    /* the root's own step changes by 0.050 m in that frame, and the end of a run is on top of it, not mended (above;
     * the check was "under the root's 0.050 m and the measure" while the hand-over was in) */
    const Judged begun = judged(ROWS(soak8_sprint_begun), 2, 15);
    printf("soak 8, a sprint begun: the largest step %.3f m (the log: %.3f m)\n", (double)begun.step, (double)begun.logged);
    CHECK(begun.logged > .09f && begun.step < begun.logged+.005f);
  }
  /* The soaks of 2026-11-22 on 0.6.0-feel.41 (tests/reference/soak-11-steps.h): the largest step the filter made in
   * them, 0.090 m at 02:22:52.808, a dive forwards out of a sprint. The body leaves the ground and rolls: its root's
   * axes turn by 0.06 rad a frame from one frame on, about the root's own forward, and the eye is 1.25 m above the
   * root: 0.07 m a frame sideways in the root's frame for an eye that goes straight on in the world, as the rig eye
   * did (its step in the world changed by 0.026 m). The frame the axes left their course in read as the root
   * re-seated, rightly. In the next the root kept its new turning, the filter took that for the course, and the eye
   * that had not gone round with the root was 0.068 m off the speed kept for it in the root's frame: a jump, taken
   * up, and two more after it, each holding the presented eye against axes that rolled on. It went 0.21 m to the
   * side in four frames (0.080, 0.060, 0.043, 0.027 m; the wanted eye 0.017 m a frame) and 0.16 m of the correction
   * was to the side of the body's way. With the root's turning left out that frame was 0.019 m off: no jump. Such a frame,
   * in which the root keeps a course it took up alone the frame before, is followed now (THE TURNING, the onset).
   * The presented eye's step changes by 0.039 m in that row now (0 below: none of 0.06 m is counted).
   * The step that is left in the next row (0.085 m; 0.078 m before) is the eye's own: the body lost a third of its speed in row 9
   * and the eye follows a frame later, 0.079 m. */
  {
    const Judged onset = judged(ROWS(soak11_dive_from_sprint), 9, 9);
    const Swung rolled = swung(ROWS(soak11_dive_from_sprint), 9, 13);
    const Judged after = judged(ROWS(soak11_dive_from_sprint), 10, 15);
    printf("soak 11, a dive out of a sprint, the frame the root keeps its new turning: the filter's step %.3f m (the log: %.3f m); %d jumps taken up in "
           "five rows, %.3f m of the correction to the side of the body's way; the largest step after it %.3f m (the log: %.3f m)\n", (double)onset.turned,
           (double)onset.logged_turned, rolled.jumps, (double)rolled.aside, (double)after.turned, (double)after.logged_turned);
    CHECK(onset.logged_turned > .085f && onset.turned < .06f);
    CHECK(rolled.jumps <= 1 && rolled.aside < .05f);
    CHECK(after.turned < after.logged_turned+.01f);
  }
  /* The same made up, at 8 to 50 ms a frame and 1.8, 4 and 8 rad/s (the soaks ran at 30 frames a second only). An eye
   * that goes straight on is not carried round with the roll: nothing is taken up for it, and a re-seat two frames
   * before the roll is eased as one without a roll is (0.15 m carried, a step change as in the probes). The filter
   * of 0.6.0-feel.41 on these: up to 0.83 m to the side and 1.01 m carried (8 rad/s at 8 ms), 0.113 m of step change
   * (1.8 rad/s at 50 ms); it was right in 14 of the 24. An eye that goes round with the roll is as before: nothing
   * taken up without a re-seat, and with one 0.15 m carried and never more. */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50}, turns[] = {1.8f, 4, 8};
    float worst = 0, most = 0, far = 0;
    for (unsigned t = 0; t < 3; ++t) for (unsigned d = 0; d < 4; ++d) for (int reseat = 0; reseat < 2; ++reseat) {
      float change, carried, aside;
      rolling(false, reseat != 0, turns[t], times[d], &change, &carried, &aside);
      if (change > .035f || carried > (reseat ? .1505f : .0005f) || aside > .005f)
        printf("  an eye going straight on, a roll of %g rad/s at %g ms a frame%s: step change %.3f m, %.3f m carried, %.3f m to the side\n",
               (double)turns[t], (double)times[d], reseat ? ", re-seated before" : "", (double)change, (double)carried, (double)aside);
      CHECK(change <= .035f && carried <= (reseat ? .1505f : .0005f) && aside <= .005f);
      if (change > worst) worst = change;
      if (carried > most) most = carried;
      if (aside > far) far = aside;
      rolling(true, reseat != 0, turns[t], times[d], &change, &carried, &aside);
      CHECK(carried <= (reseat ? .1505f : .0005f));
    }
    printf("a roll begun under an eye that goes straight on, 24 cases: step change at most %.3f m, %.3f m carried, %.3f m to the side\n", (double)worst,
           (double)most, (double)far);
  }
  /* A RUN'S LAST FRAME A HAIR UNDER THE MEASURE. The same soaks, 02:31:12.739 (tests/reference/soak-11-steps.h): the
   * native view entered after a swap, the body prone and still; the rig eye re-seated in three frames, 0.053, 0.052
   * and 0.045 m, and standing after them. 0.048 and 0.046 m off the kept speed were jumps; the third was 0.0396 m
   * off it against a measure of 0.0406 m at that frame time, ended the run and was followed: the presented eye went
   * 0.008 m and then 0.060 m, the frame's own 0.045 m on the 0.015 m of the curve begun a frame before, and its step
   * changed by 0.066 m where the wanted eye's changed by 0.006 m. This is what was behind three of the four steps
   * marked NOT MENDED above (0.039 m against 0.040 m each time). Once in a run such a frame, under the measure by a
   * tenth of it at most and going the way the jump before it went, is taken up with the run. Nothing is believed
   * for it: the kept speed stands, as in every frame of a run (THE RUN'S END IS NO SPEED holds as it is). */
  {
    const Judged third = judged(ROWS(soak11_reseat_in_three), 10, 15);
    printf("soak 11, a rig re-seated in three frames, the third a hair under the measure: the filter's largest step %.3f m (the log: %.3f m)\n",
           (double)third.turned, (double)third.logged_turned);
    CHECK(third.logged_turned > .06f && third.turned < .06f);
    /* and two of the three of the feel.40 soaks, by both measures (00:36:03.183 and 00:33:35.314; the third,
     * 00:26:06.399, is no run's end and stands) */
    const Judged three = judged(ROWS(soak9_three_frames), 2, 20), stop = judged(ROWS(soak9_sprint_swap), 21, 42);
    printf("soak 9, three frames of the rig and the rig eye stopping after a swap in a sprint: %.3f and %.3f m by the vector, %.3f and %.3f m by the lengths\n",
           (double)three.turned, (double)stop.turned, (double)three.step, (double)stop.step);
    CHECK(three.turned < .06f && three.step < .06f);
    CHECK(stop.turned < .06f && stop.step < .06f);
    /* Made up, at 8 to 50 ms a frame: 0.15 m in one frame and one more frame at 0.97 of the measure the same way. The
     * step changed by 0.040 to 0.112 m (the filter of 0.6.0-feel.41); now by 0.030 m at most, for one measure more
     * carried at most. Only once in a run: when the eye goes on like that for eight frames it is a motion, the second
     * such frame is followed as it was before, and no more than that one measure more is carried (without the
     * limit, at 8 ms a frame: 0.28 m more and 230 ms longer). A frame the other way, or 0.8 of the measure, is not
     * taken up: 0.15 m carried and no more. */
    static const float times[] = {8, 16.7f, 33.3f, 50};
    for (unsigned d = 0; d < 4; ++d) {
      const float measure = fmaxf(T.pop_m, T.pop_accel*times[d]*times[d]/1e6f); float change, carried, on, on_carried, back, back_carried, less, less_carried;
      tailed(times[d], .97f, 1, 1, &change, &carried);
      tailed(times[d], .97f, 8, 1, &on, &on_carried);
      tailed(times[d], .97f, 1, -1, &back, &back_carried);
      tailed(times[d], .8f, 1, 1, &less, &less_carried);
      printf("  0.15 m and a frame at 0.97 of the measure, %g ms a frame: step change %.4f m, %.3f m carried; eight such frames: %.4f m, %.3f m\n",
             (double)times[d], (double)change, (double)carried, (double)on, (double)on_carried);
      CHECK(change <= .03f && carried <= .15f+measure);
      CHECK(on_carried <= .15f+measure);
      CHECK(back_carried <= .1505f && less_carried <= .1505f);
    }
  }
  /* THE LENT SPEED. The soak of 2026-11-22 on 0.6.0-feel.42 (tests/reference/soak-14-steps.h), 04:00:51.667, the
   * largest of the filter's steps in it by far: 0.180 m. A dive begun and the native view entered in the same frame.
   * The head camera's eye was being thrown forwards against the root, 5 m/s (0.166 m a frame), and that speed was
   * kept across the change of camera (row 6), as every speed is. The rig eye was re-seated by 0.29 m in row 7 and
   * stood in row 8: both jumps against the kept speed, taken up, and in both the presented eye went on at it whole
   * (0.33 m on the head camera's way; 0.86 m carried). In row 9 the rig eye moved 0.086 m, 81 degrees off that way:
   * shorter than the step expected and not against it, so "the motion slowing down", followed, and the lent speed
   * was dropped whole in that frame: 0.180 m of step change where the rig eye's own was 0.076 m. Now the presented
   * eye is let down from a lent speed in the frames that are taken up, by pop_accel, as far as the speed whose step
   * in a frame is the measure. What is left in row 9 is the rig eye's own step (0.077 m), and less is carried. */
  {
    const Judged dive = judged(ROWS(soak14_dive_camera), 6, 20), first = judged(ROWS(soak14_dive_camera), 6, 8);
    const Borne load = borne(ROWS(soak14_dive_camera), 6, 20);
    printf("soak 14, a dive begun and the native view entered in the same frame: the filter's largest step %.3f m (the log: %.3f m); %.2f metre-frames "
           "carried in 15 rows, %.3f m at most (the log: %.2f and %.3f m)\n", (double)dive.turned, (double)dive.logged_turned, (double)load.sum,
           (double)load.most, (double)load.logged_sum, (double)load.logged_most);
    CHECK(dive.logged_turned > .17f && dive.turned < .08f);
    CHECK(first.turned < .06f); /* the three frames the eye is let down in: 0.039, 0.043 and 0.057 m */
    CHECK(load.sum < load.logged_sum-.5f && load.most < load.logged_most-.1f);
  }
  /* The same made up, at 8 to 50 ms a frame, the rig eye 0.4 and 0.8 m from the head camera's (the soaks ran at 30
   * frames a second only). A rig eye that stands has a step of nothing, which is "slowing down" against any speed:
   * the filter of 0.6.0-feel.42 dropped the lent speed whole in the rig eye's second frame, 0.177 m of step change at
   * 33.3 ms a frame and 0.283 m at 50 ms for 5 m/s (0.085 m at 16.7 ms; at 8 ms the step of 5 m/s is the measure
   * itself, 0.040 m, and nothing is let down). Now 0.097 and 0.120 m at most (0.065 m at 16.7 ms): two frames of
   * letting down are not enough for all of 5 m/s, and the rest is still dropped at once. Never more step, never
   * more carried and never later settled than without it, which the bounds here are (the filter of feel.42, measured:
   * the step, the most carried, the time to settle). A speed under that of the measure (1 m/s) is not touched:
   * every figure as it was. */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    static const float was[2][4][3] = {{{.0402f, .6740f, 384}, {.0851f, .6873f, 418}, {.1770f, .7358f, 466}, {.2799f, .8090f, 500}},
                                       {{.0402f, 1.0720f, 384}, {.0851f, 1.0789f, 418}, {.1777f, 1.1046f, 500}, {.2825f, 1.1454f, 500}}};
    static const float slow[2][4][2] = {{{.0082f, .6694f}, {.0180f, .6676f}, {.0408f, .6609f}, {.0682f, .6506f}},
                                        {{.0083f, 1.0692f}, {.0190f, 1.0665f}, {.0465f, 1.0564f}, {.0818f, 1.0402f}}};
    for (int far = 0; far < 2; ++far) for (unsigned d = 0; d < 4; ++d) {
      float change, carried, settled, still, still_carried, unused;
      lent(5, far ? .8f : .4f, true, times[d], &change, &carried, &settled);
      lent(5, far ? .8f : .4f, false, times[d], &still, &still_carried, &unused);
      printf("  5 m/s lent, the rig eye %.1f m off, %g ms a frame: step change %.4f m (%.4f m), %.3f m carried (%.3f m), settled in %.0f ms (%.0f ms)\n",
             far ? .8 : .4, (double)times[d], (double)change, (double)was[far][d][0], (double)carried, (double)was[far][d][1], (double)settled,
             (double)was[far][d][2]);
      CHECK(change <= was[far][d][0]+.0005f && carried <= was[far][d][1]+.0005f && settled <= was[far][d][2]+1);
      CHECK(times[d] < 16 || change < .77f*was[far][d][0]);
      CHECK(times[d] < 30 || (change < .125f && still < .125f && carried < was[far][d][1]-.02f && still_carried <= carried+.0005f));
      lent(1, far ? .8f : .4f, true, times[d], &change, &carried, &settled);
      CHECK(fabsf(change-slow[far][d][0]) < .0002f && fabsf(carried-slow[far][d][1]) < .0002f);
    }
  }
  /* THE STOP. The Bodycam soak of 2026-11-23 on 0.6.0-feel.44 (tests/reference/soak-18-steps.h), 06:18:40.922: the
   * native view after a swap, the body standing. The rig eye had gathered way for nine frames, 0.072 m a frame in the
   * end (1.9 m/s), and stopped dead in row 27: 0.004 m, and that against the way it had gone. A step shorter than the
   * one expected is the motion slowing down and is followed, but only "in the same direction": these 4 mm were a jump
   * of 0.064 m, taken up, and so were the next frame's 3 mm. The presented eye went on at 1.9 m/s past an eye that
   * stood, 0.117 m in two frames, until in row 29 the 3 mm happened to point forwards: slowing down, followed, and
   * the kept speed dropped whole on top of the catch-up begun: 0.076 m of step change, where the rig eye's own
   * change in that frame was 0.001 m. Now a step of next to nothing is the eye stopping whichever way it points:
   * row 27 is followed, what shows is the rig eye's own stop, and nothing is carried for it. */
  {
    const Judged stop = judged(ROWS(soak18_stop), 27, 30), before = judged(ROWS(soak18_stop), 0, 26);
    const Borne load = borne(ROWS(soak18_stop), 27, 30);
    printf("soak 18, a rig eye that stops dead: the filter's largest step %.3f m (the log: %.3f m); %.3f m carried at most in the four rows from "
           "the stop on (the log: %.3f m)\n", (double)stop.turned, (double)stop.logged_turned, (double)load.most, (double)load.logged_most);
    CHECK(before.apart < .005f); /* up to the stop the filter shows what the log shows */
    CHECK(stop.logged_turned > .07f && stop.turned < .06f);
    CHECK(load.logged_most > .11f && load.most < .005f);
  }
  /* The same made up, at 8 to 50 ms a frame (the soaks ran at 30 frames a second only): an eye going 2 or 4 m/s that
   * stops dead, its last steps 3 mm against its way. Where the step of that speed is over the measure (2 m/s from
   * 33.3 ms a frame, 4 m/s from 16.7 ms) the filter of 0.6.0-feel.44 took the stop up for the time of a run and then
   * believed it, the kept speed handed to the curve: the presented eye went 0.13 to 0.40 m past an eye that stood and
   * came back. Now it goes no further than the eye does, and the step changes by the eye's own stop (the speed times
   * the frame time), no more. Where that step is under the measure nothing was taken up and nothing is.
   * What is as it was, to the digit (the bounds are the filter of feel.44, measured): the last steps 3 mm along the
   * way; 20 mm against it, which is no step of nothing (a rig re-seated against the eye's way) and is taken up; and
   * a speed kept from the camera before, where a stop followed would drop that speed whole in one frame. */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    /* along the way: step change, past the eye, carried; 20 mm against it: the same; the speed lent: step change, carried */
    static const float was[2][4][8] = {
      {{0.0130f, 0.0000f, 0.0000f, 0.0360f, 0.0000f, 0.0000f, 0.0193f, 0.0160f}, {0.0304f, 0.0000f, 0.0000f, 0.0362f, 0.1917f, 0.1917f, 0.0389f, 0.0334f},
       {0.0636f, 0.0000f, 0.0000f, 0.0236f, 0.3212f, 0.3212f, 0.0266f, 0.1265f}, {0.0970f, 0.0000f, 0.0000f, 0.0347f, 0.3171f, 0.3171f, 0.0391f, 0.2462f}},
      {{0.0290f, 0.0000f, 0.0000f, 0.0330f, 0.2021f, 0.2021f, 0.0356f, 0.0320f}, {0.0638f, 0.0000f, 0.0000f, 0.0702f, 0.3238f, 0.3238f, 0.0422f, 0.2000f},
       {0.1302f, 0.0000f, 0.0000f, 0.0497f, 0.6405f, 0.6405f, 0.0513f, 0.1733f}, {0.1970f, 0.0000f, 0.0000f, 0.0805f, 0.6624f, 0.6624f, 0.0900f, 0.2582f}}};
    for (int fast = 0; fast < 2; ++fast) for (unsigned d = 0; d < 4; ++d) {
      const float v = fast ? 4 : 2, own = v*times[d]/1000, *w = was[fast][d];
      float change, past, carried, along, along_past, along_carried, far, far_past, far_carried, lent_change, lent_carried;
      stopped(v, -.003f, times[d], &change, &past, &carried);
      stopped(v, .003f, times[d], &along, &along_past, &along_carried);
      stopped(v, -.02f, times[d], &far, &far_past, &far_carried);
      stopped_lent(v, -.003f, times[d], &lent_change, &lent_carried);
      printf("  %g m/s stopped dead, %g ms a frame: step change %.4f m (the eye's own %.4f m), %.4f m past the eye, %.4f m carried; 3 mm along the "
             "way %.4f, %.4f, %.4f; 20 mm against it %.4f, %.4f, %.4f; the speed lent %.4f, %.4f\n", (double)v, (double)times[d], (double)change,
             (double)own, (double)past, (double)carried, (double)along, (double)along_past, (double)along_carried, (double)far, (double)far_past,
             (double)far_carried, (double)lent_change, (double)lent_carried);
      CHECK(change <= own+.0035f && past < .005f && carried < .005f);
      CHECK(fabsf(along-w[0]) < .0002f && fabsf(along_past-w[1]) < .0002f && fabsf(along_carried-w[2]) < .0002f);
      CHECK(fabsf(far-w[3]) < .0002f && fabsf(far_past-w[4]) < .0002f && fabsf(far_carried-w[5]) < .0002f);
      CHECK(fabsf(lent_change-w[6]) < .0002f && fabsf(lent_carried-w[7]) < .0002f);
    }
  }
  /* NOT MENDED. The same soak, 06:22:27.850 (soak18_sprint_turn), the one step of its 17 in a head-camera frame:
   * inside a swap the body begins to walk and its root's axes turn by 0.30 rad in one frame under an eye that goes
   * straight on. The eye's step is 0.058 m off the kept speed in the root's frame and 0.011 m with the turning left
   * out; the root is 0.078 m off its course by the turn and its own gain of speed together, so the frame does not
   * read as the root re-seated, and a jump in the root's frame alone is not read without the turning: taken up, and
   * the presented eye held to axes that turned, 0.082 m of step change for the eye's own 0.043 m. Read without the
   * turning (where the two readings are half the measure apart and the eye has not gone round with the root of late)
   * the frame is followed and shows 0.056 m; it is the only frame of nine logs that reading acts on, and made up
   * (a body that gains speed and turns in one frame, 7,680 cases) it was worse in 60 of the 496 it changed. Left as
   * it is; the filter is held to adding nothing to the logged step and to the rows after it. */
  {
    const Judged turn = judged(ROWS(soak18_sprint_turn), 27, 27), before = judged(ROWS(soak18_sprint_turn), 0, 26), after = judged(ROWS(soak18_sprint_turn), 28, 42);
    printf("soak 18, a body that begins to walk and turns under the head camera (not mended): the filter's step %.3f m (the log: %.3f m); after it %.3f m "
           "(the log: %.3f m)\n", (double)turn.turned, (double)turn.logged_turned, (double)after.turned, (double)after.logged_turned);
    CHECK(before.apart < .005f && turn.logged_turned > .08f);
    CHECK(turn.turned <= turn.logged_turned+.001f && after.turned <= after.logged_turned+.001f);
  }
  /* THE HITCH, made up, at 8 to 50 ms a frame: an eye going 2 or 4 m/s whose step is next to nothing for one frame
   * (nothing, 1 mm along its way, 1 mm or 6 mm against it) and which then goes on as it went; the body standing, and
   * walking with the world given. The hitch is the game's own and is followed as a stop (THE STOP), the kept speed
   * dropped whole; the frame after it was then a jump against a speed of nothing, and so were the next ones for the
   * time of a run: where the step of that speed is over the measure (2 m/s from 33.3 ms a frame, 4 m/s from 16.7 ms)
   * 0.25 to 0.61 m was carried and 450 to 500 ms went by until it was let out (the worst of the eight in each), and
   * while the run lasted the presented eye fell back behind an eye it was meant to catch up with, 0.04 to 0.15 m a
   * frame. Now the frame after a stop that goes on as the eye went
   * before it is followed: nothing is carried, and what shows is the hitch itself. */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50}, hitches[] = {0, .001f, -.001f, -.006f};
    for (int fast = 0; fast < 2; ++fast) for (unsigned d = 0; d < 4; ++d) {
      const float v = fast ? 4 : 2; Hitch worst = {0, 0, 0, 0, 0};
      for (unsigned k = 0; k < 4; ++k) for (int placed = 0; placed < 2; ++placed) {
        const Hitch h = hitched(v, hitches[k], 1, v, placed != 0, times[d]);
        if (h.change-h.own > worst.change-worst.own || !k) { worst.change = h.change; worst.own = h.own; }
        if (h.carried > worst.carried) worst.carried = h.carried;
        if (h.settled > worst.settled) worst.settled = h.settled;
        if (h.against > worst.against) worst.against = h.against;
        CHECK(h.carried <= v*times[d]/1000+.002f); /* about one frame's travel at most */
        CHECK(h.settled <= 100);
        CHECK(h.against < .005f);
        CHECK(h.change <= h.own+.002f);          /* the hitch itself and no more */
      }
      printf("  %g m/s, one frame's hitch, %g ms a frame: step change %.4f m (the eye's own %.4f m), %.4f m carried, settled in %.0f ms, "
             "%.4f m a frame against the catch-up\n", (double)v, (double)times[d], (double)worst.change, (double)worst.own, (double)worst.carried,
             (double)worst.settled, (double)worst.against);
    }
  }
  /* What is no hitch is as it was, to the digit (the bounds are the filter before THE HITCH, measured): an eye that
   * stops for four frames, 4 mm a frame against its way, and then goes back the way it came; one that stops and
   * creeps back at 7 mm a frame for good; and one that stands for two frames and then goes on (not mended: the
   * speed before a stop is kept for one frame). */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    /* back the way: step change, carried, ms to settle, against the catch-up; creeping: the same; two frames: the same */
    static const float was[2][4][12] = {
      {{0.0200f, 0.0000f, 0, 0.0000f, 0.0230f, 0.0000f, 0, 0.0000f, 0.0160f, 0.0000f, 0, 0.0000f},
       {0.0374f, 0.0000f, 0, 0.0000f, 0.0404f, 0.0000f, 0, 0.0000f, 0.0334f, 0.0000f, 0, 0.0000f},
       {0.0706f, 0.2012f, 500, 0.0317f, 0.0736f, 0.0000f, 0, 0.0000f, 0.0666f, 0.2190f, 466, 0.0349f},
       {0.1040f, 0.2313f, 600, 0.0476f, 0.1070f, 0.0000f, 0, 0.0000f, 0.1000f, 0.2454f, 500, 0.0510f}},
      {{0.0360f, 0.6313f, 496, 0.0388f, 0.0390f, 0.0000f, 0, 0.0000f, 0.0320f, 0.0000f, 0, 0.0000f},
       {0.0708f, 0.4259f, 534, 0.0457f, 0.0738f, 0.0000f, 0, 0.0000f, 0.0668f, 0.4581f, 501, 0.0494f},
       {0.1372f, 0.5239f, 599, 0.0899f, 0.1402f, 0.0000f, 0, 0.0000f, 0.1332f, 0.5433f, 533, 0.0936f},
       {0.2040f, 0.5786f, 650, 0.1369f, 0.2070f, 0.0000f, 0, 0.0000f, 0.2000f, 0.5925f, 550, 0.1407f}}};
    for (int fast = 0; fast < 2; ++fast) for (unsigned d = 0; d < 4; ++d) {
      const float v = fast ? 4 : 2, *w = was[fast][d];
      const Hitch back = hitched(v, -.004f, 4, -v, false, times[d]), creep = hitched(v, -.007f, 100000, 0, false, times[d]),
                  twice = hitched(v, 0, 2, v, false, times[d]);
      printf("  %g m/s, %g ms a frame: stopped and back the way %.4f, %.4f, %.0f, %.4f; stopped and creeping %.4f, %.4f, %.0f, %.4f; "
             "two frames' hitch %.4f, %.4f, %.0f, %.4f\n", (double)v, (double)times[d], (double)back.change, (double)back.carried,
             (double)back.settled, (double)back.against, (double)creep.change, (double)creep.carried, (double)creep.settled, (double)creep.against,
             (double)twice.change, (double)twice.carried, (double)twice.settled, (double)twice.against);
      CHECK(fabsf(back.change-w[0]) < .0002f && fabsf(back.carried-w[1]) < .0002f && fabsf(back.settled-w[2]) < 1 && fabsf(back.against-w[3]) < .0002f);
      CHECK(fabsf(creep.change-w[4]) < .0002f && fabsf(creep.carried-w[5]) < .0002f && fabsf(creep.settled-w[6]) < 1 && fabsf(creep.against-w[7]) < .0002f);
      CHECK(fabsf(twice.change-w[8]) < .0002f && fabsf(twice.carried-w[9]) < .0002f && fabsf(twice.settled-w[10]) < 1 && fabsf(twice.against-w[11]) < .0002f);
    }
  }
  /* The one hitch of the nine soak logs: 04:05:18.296 of the Stable soak of 0.6.0-feel.42 (soak14_hitch). A rig eye
   * dying down against a standing root, 0.052 m in row 5; 0.002 m in row 6; 0.056 m in row 7, the frame missed made
   * up; and at rest from row 8. Row 6 is followed as a stop; row 7 was a jump against a speed of nothing: 0.054 m
   * taken up and let out over the next four rows. Now row 7 goes on as the eye went before the stop and is followed:
   * nothing is carried, and the presented eye is the rig eye, its hitch and its last step with it. */
  {
    const Judged before = judged(ROWS(soak14_hitch), 0, 6), after = judged(ROWS(soak14_hitch), 7, 12);
    const Borne load = borne(ROWS(soak14_hitch), 7, 12);
    printf("soak 14, a rig eye that misses a frame: %.3f m carried at most from the frame after it on (the log: %.3f m)\n", (double)load.most,
           (double)load.logged_most);
    CHECK(before.apart < .005f);
    CHECK(load.logged_most > .05f && load.most < .005f);
    CHECK(after.turned < .06f && after.step < .06f); /* its last step is under what the soak's judge counts */
  }
  /* A stop that is no hitch: 07:04:11.051 of the Stable soak of 2026-11-23 on 0.6.0-feel.44 (soak19_stop), the one
   * frame of the tenth log THE HITCH acted on. The eye is thrown about in a dive, 0.2 m and more carried; row 37 ends
   * a run of jumps with a speed believed (1.9 m/s), row 38 all but stands (0.006 m, followed as a stop), row 39 goes
   * 0.051 m. That is within the measure of the step the speed before the stop would have made, and with the speed of
   * any stop kept for a frame row 39 was followed as the eye going on: 0.064 m of step change for the wanted eye's
   * 0.042 m, while the curve of the 0.25 m carried was at its steepest, a step the filter before did not make. The
   * speed before that stop was believed one frame earlier and no frame had gone on at it: no eye going steadily.
   * Such a speed is not kept at a stop; row 39 is a jump, as it was, and every row is the log's. */
  {
    const Judged all = judged(ROWS(soak19_stop), 0, 42), there = judged(ROWS(soak19_stop), 37, 42);
    const Borne load = borne(ROWS(soak19_stop), 39, 39);
    printf("soak 19, a stop straight after a run of jumps: the filter %.4f m from the log at most, %.3f m carried in the frame after it (the log: %.3f m), "
           "its largest step from there %.3f m (the log: %.3f m)\n", (double)all.apart, (double)load.most, (double)load.logged_most, (double)there.turned,
           (double)there.logged_turned);
    CHECK(all.apart < .005f);
    CHECK(load.logged_most > .3f && fabsf(load.most-load.logged_most) < .005f);
    CHECK(there.turned <= there.logged_turned+.001f);
  }
  /* THE TRIAL, made up (the second check's scene O and its like): the eye stops from a speed whose step is over the
   * measure, the next frame goes on as it went (followed: THE HITCH), and the frame after that does not: it goes back
   * by that step and the eye is at rest, the game's own flicker of one frame after a stop. With the frame that goes on
   * believed at once the step back was a jump of two steps against it, taken up: the presented eye ran one step more
   * forwards, to where the wanted eye never was (0.067 to 0.200 m beyond it), and came back over 200 to 400 ms with
   * twice a frame's travel carried. The filter before THE HITCH took the frame that goes on up and the step back too:
   * a frame's travel carried, 117 to 150 ms. Now the speed of the frame that goes on is on trial for one frame: the
   * step back is followed as the game's own, nothing is carried, and the presented eye is nowhere the wanted eye has
   * not been. The same for what else can come after that frame: half a step back, two steps back, a step sideways,
   * the flicker twice, back and on going back, on at twice the speed; held to that where a frame's travel is over
   * the measure (elsewhere no speed is dropped at the stop and nothing here acts). */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    static const float back[][2] = {{-1, 0}}, half[][2] = {{-.5f, 0}}, two[][2] = {{-2, 0}}, side[][2] = {{0, 1}}, twice[][2] = {{-1, 0}, {1, 0}, {-1, 0}};
    static const struct { const char *name; const float (*then)[2]; int count; float on; } scenes[] = {
      {"a step back, at rest", back, 1, 0}, {"half a step back, at rest", half, 1, 0}, {"two steps back, at rest", two, 1, 0},
      {"a step sideways, at rest", side, 1, 0}, {"back, on, back, at rest", twice, 3, 0}, {"a step back and on going back", back, 1, -1},
      {"on at twice the speed", NULL, 0, 2}};
    /* the filter before THE HITCH on "a step back, at rest": settled (ms), 2 and 4 m/s at each frame time; it carried a frame's travel */
    static const float was[2][4] = {{0, 134, 133, 150}, {128, 117, 133, 150}};
    for (unsigned s = 0; s < sizeof scenes/sizeof *scenes; ++s) for (int fast = 0; fast < 2; ++fast) for (unsigned d = 0; d < 4; ++d) {
      const float v = fast ? 4 : 2; Trial worst = {0, 0, 0, 0, 0};
      for (int placed = 0; placed < 2; ++placed) {
        const Trial r = tried(v, times[d], placed != 0, scenes[s].then, scenes[s].count, scenes[s].on, false);
        if (r.beyond > worst.beyond) worst.beyond = r.beyond;
        if (r.carried > worst.carried) worst.carried = r.carried;
        if (r.settled > worst.settled) worst.settled = r.settled;
        if (r.change-r.own > worst.change-worst.own || !placed) { worst.change = r.change; worst.own = r.own; }
        /* where a frame's travel is under the measure nothing was dropped at the stop and no frame is on trial: as it was */
        if (v*times[d]/1000 > fmaxf(T.pop_m, T.pop_accel*times[d]*times[d]/1e6f)) CHECK(r.beyond <= .01f); /* never where the wanted eye has not been */
        if (!s) { CHECK(r.carried <= v*times[d]/1000+.002f || r.carried <= .0602f); CHECK(r.settled <= was[fast][d]+1); }
      }
      printf("  %g m/s, %g ms a frame, stop, on, then %s: %.4f m beyond where the wanted eye has been, step change %.4f m (the eye's own %.4f m), "
             "%.4f m carried, settled in %.0f ms\n", (double)v, (double)times[d], scenes[s].name, (double)worst.beyond, (double)worst.change,
             (double)worst.own, (double)worst.carried, (double)worst.settled);
    }
    printf("  4 m/s, 33.3 ms a frame, stop, on, a step back, at rest: the presented eye from the frame that goes on (m; wanted 0.133, 0, 0): ");
    (void)tried(4, 33.3f, false, back, 1, 0, true); printf("\n");
  }
  /* THE LET-DOWN AT A SLOWING. The Stable soak of 2026-11-23 on 0.6.0-feel.45 (tests/reference/soak-slowing-steps.h).
   * 09:06:29.235 (soak20_swap_believed, row 18): the native view entered in a swap, the rig eye thrown forwards for
   * four frames, the run's time over in row 17 and 4.7 m/s believed (0.156 m a frame), the presented eye 0.87 m behind
   * and gathering that speed along the curve. In row 18 the rig eye goes 0.048 m, the same way: the motion slowing
   * down, followed, the speed believed dropped whole. The curve had been begun for an eye going 0.156 m a frame, and
   * under one going 0.048 m the presented eye was thrown back: 0.114 m of step change, 0.046 m further from the eye it
   * was to catch up with, and 0.061 m of step change in the frame after. 09:11:59.029 (soak20_dive_believed, row 24):
   * the same in a dive, 6.4 m/s believed and 0.044 m gone; 0.144 m, then 0.080 and 0.066 m, the presented eye 0.131 m
   * further behind in that frame. Now what the presented eye still had of the speed dropped, towards the wanted eye,
   * is let down by pop_accel: it goes on closing, and the step changes by 0.034 and 0.054 m at most.
   * 05:06:55.307 of the Stable soak of 0.6.0-feel.43 (soak15_run_slowing, row 16): the presented eye went on at a speed
   * believed, 4.3 m/s, through three frames the rig eye went the other way, and closed on it from 0.46 to 0.07 m; the
   * frame that slows down comes 0.05 m short of the rig eye: 0.105 m of step change for the rig eye's own 0.045 m.
   * 0.05 m is no room to let 0.13 m a frame down in; what fits is let down (0.098 m, half as much carried after). */
  {
    const Slowing swap = slowing(ROWS(soak20_swap_believed), 18, 20), dive = slowing(ROWS(soak20_dive_believed), 24, 27);
    const Slowing run = slowing(ROWS(soak15_run_slowing), 16, 18);
    printf("soak 20, a speed believed and the next frame slowing down, the presented eye behind: in a swap the largest change of the presented step "
           "%.3f m (the log: %.3f m), %.3f m away from the wanted eye in a frame followed (%.3f m), %.2f metre-frames in three rows (%.2f); in a dive "
           "%.3f m (%.3f m), %.3f m away (%.3f m), %.2f metre-frames in four rows (%.2f)\n", (double)swap.change, (double)swap.logged, (double)swap.away,
           (double)swap.logged_away, (double)swap.sum, (double)swap.logged_sum, (double)dive.change, (double)dive.logged, (double)dive.away,
           (double)dive.logged_away, (double)dive.sum, (double)dive.logged_sum);
    printf("soak 15, slowing down 0.05 m short of the wanted eye: %.3f m (the log: %.3f m), %.3f metre-frames in three rows (%.3f)\n", (double)run.change,
           (double)run.logged, (double)run.sum, (double)run.logged_sum);
    CHECK(swap.apart < .005f && dive.apart < .005f && run.apart < .005f); /* up to the frame that slows down the filter shows what the log shows */
    CHECK(swap.logged > .11f && swap.change < .04f);
    CHECK(dive.logged > .14f && dive.change < .06f);
    CHECK(run.logged > .10f && run.change < run.logged-.005f);
    CHECK(swap.logged_away > .04f && dive.logged_away > .13f && swap.away < .001f && dive.away < .001f && run.away < .001f);
    CHECK(swap.sum < swap.logged_sum-.4f && dive.sum < dive.logged_sum-.9f && run.sum < run.logged_sum-.04f);
    CHECK(swap.past < .001f && dive.past < .001f && run.past < .001f);
  }
  /* The same where the way to the wanted eye is short: 00:25:51.875 of the second soak of 2026-11-22 on 0.6.0-feel.40
   * (soak9_run_slowing, row 14), slowing down straight after three jumps, 0.46 m carried and three jumps more to come;
   * at row 18 (00:25:52.009) 0.06 m is left and the curve is closing fast. The step there is 0.095 m on the filter
   * of 0.6.0-feel.45 (0.100 m in the log, which had no let-down of a lent speed yet). What is let down goes no further
   * than the wanted eye, the curve's own speed counted (with the bare way as the room it came 0.007 m past it in row
   * 19): 0.081 m, less carried, never past. */
  {
    const Slowing near = slowing(ROWS(soak9_run_slowing), 14, 20);
    printf("soak 9, slowing down with a short way to the wanted eye: %.3f m (the log: %.3f m), %.4f m past the wanted eye, %.3f m away from it in a "
           "frame followed (%.3f m), %.2f metre-frames in seven rows (%.2f)\n", (double)near.change, (double)near.logged, (double)near.past,
           (double)near.away, (double)near.logged_away, (double)near.sum, (double)near.logged_sum);
    CHECK(near.apart < .005f);
    CHECK(near.logged > .09f && near.change < .085f);
    CHECK(near.past < .001f && near.away <= near.logged_away+.001f && near.sum < near.logged_sum-.05f);
  }
  /* NOT MENDED: the same frame with the presented eye AHEAD of the wanted one, which is the larger steps of the Stable
   * soak of 0.6.0-feel.44. 07:19:31.978 (soak19_prone_lent, row 8): going prone out of an emote, 7.7 m/s lent at the
   * change of camera, the presented eye let down for two frames and 0.32 m past a rig eye that all but stands; the
   * 5.3 m/s it still has are dropped at once, 0.182 m of step change for the rig eye's own 0.070 m. 07:13:24.300
   * (soak19_back_believed, row 19): 3.2 m/s believed, the rig eye re-seated 0.72 m the other way and the presented
   * eye 0.30 m in front of it; 0.074 m for 0.039 m. To let that speed down is to go on away from the wanted eye:
   * 0.14 m further for 5.3 m/s at 36 m/s^2 (docs/design/specs/2026-11-22-locked-view-design.md, round 7, what was
   * tried). Nothing of it is towards the wanted eye, and the filter is held to showing what the log shows. */
  {
    const Slowing prone = slowing(ROWS(soak19_prone_lent), 8, 20), back = slowing(ROWS(soak19_back_believed), 19, 20);
    printf("soak 19, slowing down with the presented eye ahead (not mended): going prone %.3f m (the log: %.3f m), walking backwards %.3f m (%.3f m); "
           "the filter %.4f and %.4f m from the log at most\n", (double)prone.change, (double)prone.logged, (double)back.change, (double)back.logged,
           (double)fmaxf(prone.apart, prone.rest), (double)fmaxf(back.apart, back.rest));
    CHECK(prone.apart < .005f && prone.rest < .005f && back.apart < .005f && back.rest < .005f);
    CHECK(prone.logged > .18f && back.logged > .07f);
  }
  /* The same made up, at 8 to 50 ms a frame (the soaks ran at 30 frames a second only): a motion that begins at 2, 4
   * or 6.5 m/s, believed when the run's time is over, and in the next frame the eye goes on at a quarter or a half of
   * it. The bounds are the filter of 0.6.0-feel.45, measured: the step change, how far the presented eye comes in
   * front of the wanted one, how far it moves away from it in a frame followed, the most carried, the time to settle.
   * Behind the wanted eye: where the step dropped is over the measure, less step and less away; nowhere more step, more
   * carried or later settled, and never in front of the wanted eye. Ahead of it (the rig re-seated 1.2 m backwards in
   * the frame after the speed is believed): every figure as it was, to the digit. */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    static const float was[2][3][2][4][5] = {
      {{{{0.0120f, 0.0000f, 0.0000f, 0.0000f, 0}, {0.0250f, 0.0000f, 0.0000f, 0.0000f, 0}, {0.0349f, 0.0000f, 0.0198f, 0.2190f, 233}, {0.0437f, 0.0000f, 0.0181f, 0.2454f, 250}},
        {{0.0080f, 0.0000f, 0.0000f, 0.0000f, 0}, {0.0167f, 0.0000f, 0.0000f, 0.0000f, 0}, {0.0218f, 0.0000f, 0.0198f, 0.2190f, 233}, {0.0437f, 0.0000f, 0.0181f, 0.2454f, 250}}},
       {{{0.0240f, 0.0000f, 0.0000f, 0.0000f, 0}, {0.0432f, 0.0000f, 0.0425f, 0.4581f, 351}, {0.0723f, 0.0000f, 0.0660f, 0.5433f, 333}, {0.0890f, 0.0000f, 0.0773f, 0.5925f, 300}},
        {{0.0160f, 0.0000f, 0.0000f, 0.0000f, 0}, {0.0265f, 0.0000f, 0.0425f, 0.4581f, 351}, {0.0417f, 0.0000f, 0.0660f, 0.5433f, 333}, {0.0890f, 0.0000f, 0.0773f, 0.5925f, 300}}},
       {{{0.0364f, 0.0000f, 0.0410f, 0.7070f, 376}, {0.0697f, 0.0000f, 0.0774f, 0.7989f, 367}, {0.1147f, 0.0000f, 0.1244f, 0.9521f, 333}, {0.1620f, 0.0000f, 0.1502f, 1.0335f, 300}},
        {{0.0234f, 0.0000f, 0.0410f, 0.7070f, 376}, {0.0426f, 0.0000f, 0.0774f, 0.7989f, 367}, {0.0754f, 0.0000f, 0.1244f, 0.9521f, 333}, {0.1620f, 0.0000f, 0.1502f, 1.0335f, 300}}}},
      {{{{0.0062f, 0.0000f, 0.0000f, 1.2074f, 376}, {0.0190f, 0.0000f, 0.0000f, 1.2149f, 367}, {0.0575f, 0.0000f, 0.0000f, 1.0202f, 333}, {0.0973f, 0.0000f, 0.0000f, 1.0143f, 300}},
        {{0.0028f, 0.0000f, 0.0000f, 1.2074f, 376}, {0.0117f, 0.0000f, 0.0000f, 1.2149f, 367}, {0.0409f, 0.0000f, 0.0000f, 1.0202f, 333}, {0.0768f, 0.0000f, 0.0000f, 1.0143f, 300}}},
       {{{0.0111f, 0.0000f, 0.0000f, 1.2162f, 376}, {0.0460f, 0.0000f, 0.0000f, 0.8398f, 351}, {0.0869f, 0.0000f, 0.0000f, 0.7613f, 333}, {0.1294f, 0.0000f, 0.0000f, 0.7508f, 300}},
        {{0.0031f, 0.0000f, 0.0000f, 1.2162f, 376}, {0.0293f, 0.0000f, 0.0000f, 0.8398f, 351}, {0.0536f, 0.0000f, 0.0000f, 0.7613f, 333}, {0.0794f, 0.0000f, 0.0000f, 0.7508f, 300}}},
       {{{0.0361f, 0.0000f, 0.0000f, 0.6968f, 336}, {0.0692f, 0.0000f, 0.0000f, 0.5745f, 334}, {0.1200f, 0.0000f, 0.0000f, 0.4312f, 266}, {0.1662f, 0.0000f, 0.0000f, 0.4188f, 250}},
        {{0.0231f, 0.0000f, 0.0000f, 0.6968f, 336}, {0.0421f, 0.0000f, 0.0000f, 0.5745f, 334}, {0.0659f, 0.0000f, 0.0000f, 0.4312f, 266}, {0.0850f, 0.0000f, 0.0000f, 0.4188f, 250}}}}};
    for (int ahead = 0; ahead < 2; ++ahead) for (int fast = 0; fast < 3; ++fast) for (int part = 0; part < 2; ++part) for (unsigned d = 0; d < 4; ++d) {
      const float v = fast == 0 ? 2 : fast == 1 ? 4 : 6.5f, slow = part ? v/2 : v/4, *w = was[ahead][fast][part][d];
      const float dropped = (v-slow)*times[d]/1000, measure = fmaxf(T.pop_m, T.pop_accel*times[d]*times[d]/1e6f);
      const Slowed g = slowed(v, slow, ahead != 0, times[d]);
      printf("  %g m/s believed, then %g m/s, the presented eye %s, %g ms a frame: step change %.4f m (%.4f m), %.4f m in front of the wanted eye "
             "(%.4f m), %.4f m away from it in a frame followed (%.4f m), %.4f m carried (%.4f m), settled in %.0f ms (%.0f ms)\n", (double)v, (double)slow,
             ahead ? "ahead" : "behind", (double)times[d], (double)g.change, (double)w[0], (double)g.past, (double)w[1], (double)g.away, (double)w[2],
             (double)g.carried, (double)w[3], (double)g.settled, (double)w[4]);
      if (ahead) { CHECK(fabsf(g.change-w[0]) < .0002f && fabsf(g.past-w[1]) < .0002f && fabsf(g.away-w[2]) < .0002f && fabsf(g.carried-w[3]) < .0002f
                         && fabsf(g.settled-w[4]) < 1); continue; }
      CHECK(g.change <= w[0]+.0005f && g.away <= w[2]+.0005f && g.carried <= w[3]+.0005f && g.settled <= w[4]+1);
      CHECK(g.past < .005f);
      if (dropped > measure+.009f) CHECK(g.change < w[0]-.007f && g.away < w[2]-.009f && g.carried < w[3]-.009f);
    }
  }
  /* THE LET-DOWN AT A SLOWING, and a frame TAKEN UP while it runs (made up; no log has it). The let-down's room was
   * measured against the wanted eye as it stood before the jump and what it went was added to the new correction: with
   * the rig re-seated 1.2 m backwards in the frame after the one that slows down, the presented eye went one frame more
   * away from a wanted eye that was now behind it, and that speed was dropped whole the frame after. 8 m/s believed, a
   * quarter of it for one frame, 33.3 ms a frame: the presented steps 0.048, 0.069, 0.120, -0.034 m, 0.154 m of step
   * change, 0.348 m past the wanted eye, settled in 333 ms (the filter of 0.6.0-feel.45, which throws the eye back in
   * the frame that slows down: 0.048, -0.091, 0.004, -0.023 m; 0.139 m, 0.072 m, 166 ms). Now the room is measured
   * anew in a frame taken up, against the wanted eye where the jump put it and the curve begun there: the let-down ends
   * where that eye is behind, and goes on where it is still ahead (a change of camera to the side).
   * `main`: the filter of 0.6.0-feel.45, measured: step change, m past the wanted eye, ms to settle, and how much
   * further past the wanted eye the frame of the re-seat left the presented one than the re-seat itself put it.
   * Held to: no more step change than that filter and the frame of the re-seat as it was there; no further past the
   * wanted eye than that filter by more than the let-down went in the frame that slows down (that way is the mend of
   * 09:06:29.235 itself, and the re-seat comes a frame after it); the time to settle as measured (what is further
   * past takes longer: 8 m/s at 16.7 to 50 ms 50 to 150 ms more than that filter, 5 m/s at 33.3 and 50 ms 33 and 50 ms). */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    static const float main_was[2][4][4] = {
      {{0.0447f, 0.5230f, 328, -0.0304f}, {0.0857f, 0.3207f, 217, -0.0408f}, {0.1391f, 0.0722f, 166, 0.0039f}, {0.2029f, 0.0359f, 150, 0.1137f}},
      {{0.0339f, 0.8206f, 384, 0.0087f}, {0.0538f, 0.6786f, 401, -0.0212f}, {0.0892f, 0.5360f, 400, 0.0077f}, {0.1179f, 0.5238f, 400, 0.0744f}}};
    static const float settles[2][4] = {{328, 267, 266, 300}, {384, 401, 433, 450}};
    for (int fast = 0; fast < 2; ++fast) for (unsigned d = 0; d < 4; ++d) {
      const float v = fast ? 5 : 8, *w = main_was[fast][d];
      const Taken g = taken(v, .25f, 0, times[d]);
      printf("  %g m/s believed, a quarter for one frame, the rig re-seated 1.2 m back and standing, %g ms a frame: step change %.4f m (0.6.0-feel.45: "
             "%.4f m), %.4f m past the wanted eye (%.4f m; the let-down went %.4f m before the re-seat), %.4f m further past it in the frame of the "
             "re-seat (%.4f m), settled in %.0f ms (%.0f ms)\n", (double)v, (double)times[d], (double)g.change, (double)w[0], (double)g.past, (double)w[1],
             (double)g.let, (double)g.jumped, (double)w[3], (double)g.settled, (double)w[2]);
      CHECK(g.change <= w[0]+.0005f);
      CHECK(g.jumped <= w[3]+.0005f);
      CHECK(g.past <= w[1]+g.let+.001f);
      CHECK(g.settled <= settles[fast][d]+1);
    }
  }
  /* The same let-down and what else can follow it. A change of camera in the frame after, the new eye 0.4 m to the side
   * and standing: taken up, and the wanted eye is still ahead along the let-down's way, so it goes on (ended there, the
   * step change at 8 m/s was 0.040, 0.066, 0.095 and 0.064 m). The rig eye going back at the speed it came at (5 m/s,
   * half of it for one frame): a run of jumps and that speed believed, the presented eye goes on forwards at the kept
   * speed through the run and is 0.56 to 0.75 m past the wanted eye on the filter of 0.6.0-feel.45; the let-down's own
   * frame is 0.043 m at 33.3 ms, and the eye is that much further on: 0.677 to 0.717 m (16.7 ms: 0.557 to 0.603 m;
   * 50 ms: 0.746 to 0.780 m), the step change 0.080 to 0.082 m and 0.165 to 0.170 m. A rig eye that stands after one
   * frame at half of 7 m/s (a dive's landing; a stop, followed, no frame taken up): 0.046 to 0.048 m, 0.069 to 0.076 m
   * and, at 50 ms, 0.128 to 0.121 m. Those two are the let-down itself while the wanted eye is still ahead (its first
   * frame; at 16.7 ms the frames of the run too, 0.022 m), not its going on past a wanted eye, and are held to what
   * they are. */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    static const struct { const char *name; float v, part; int scene; float is[4][3]; } scenes[] = {
      {"8 m/s believed, a quarter for one frame, a change of camera 0.4 m to the side", 8, .25f, 3,
       {{0.0132f, 0.0000f, 400}, {0.0235f, 0.0000f, 418}, {0.0318f, 0.0000f, 433}, {0.0913f, 0.0000f, 450}}},
      {"5 m/s believed, a quarter for one frame, a change of camera 0.4 m to the side", 5, .25f, 3,
       {{0.0324f, 0.0000f, 368}, {0.0193f, 0.0000f, 401}, {0.0453f, 0.0000f, 433}, {0.0441f, 0.0000f, 450}}},
      {"5 m/s believed, half for one frame, then back at 5 m/s", 5, .5f, 1,
       {{0.0203f, 0.7307f, 472}, {0.0211f, 0.6034f, 501}, {0.0823f, 0.7165f, 533}, {0.1698f, 0.7799f, 550}}},
      {"7 m/s believed, half for one frame, then standing", 7, .5f, 2,
       {{0.0251f, 0.0000f, 384}, {0.0477f, 0.0000f, 384}, {0.0761f, 0.0000f, 366}, {0.1210f, 0.0000f, 350}}}};
    for (unsigned l = 0; l < sizeof scenes/sizeof *scenes; ++l) for (unsigned d = 0; d < 4; ++d) {
      const float *w = scenes[l].is[d];
      const Taken g = taken(scenes[l].v, scenes[l].part, scenes[l].scene, times[d]);
      printf("  %s, %g ms a frame: step change %.4f m (%.4f m), %.4f m past the wanted eye (%.4f m), settled in %.0f ms (%.0f ms)\n", scenes[l].name,
             (double)times[d], (double)g.change, (double)w[0], (double)g.past, (double)w[1], (double)g.settled, (double)w[2]);
      CHECK(g.change <= w[0]+.0005f && g.past <= w[1]+.0005f && g.settled <= w[2]+1);
    }
  }
  /* THE WAY BACK: a frame not looked at that takes the eye back to where it was two frames ago is no speed. Until
   * 0.6.0-feel.46 a frame over the measure in which jumps are not looked for set the kept speed to its own step whole,
   * a flip of 0.2 m too (6 to 7 m/s at 33 ms a frame). The first frame looked at after it met that speed: the flip back
   * was a jump against it, taken up, and the presented eye went on at it (tests/reference/soak-21-alternation.h, the
   * soak of 2026-11-23, 03:13:53.490: 0.217 m past the far place, then 0.179 m more, 0.435 m carried, 0.54 m from the
   * middle of the two places). Now such a frame keeps no speed: the first frame looked at is a jump against an eye
   * that stands, and the presented eye stays between the two places. */
  {
    const Flipped g = flipped_logged(soak21_alternation, (int)(sizeof soak21_alternation/sizeof *soak21_alternation), 6, 4, 5);
    printf("  soak 21, the end of the alternation (03:13:53.490 on): the presented eye steps %.4f m at most (the log: 0.2168 m), is %.4f m from the "
           "middle of the two places at most (0.54 m), %.4f m carried at most (0.4465 m); the largest speed kept from a flip not looked at %.2f m/s "
           "(6.4 m/s)\n", (double)g.step, (double)g.off, (double)g.carried, (double)g.speed);
    CHECK(g.speed < .01f);  /* rows 2 to 5 are returns: no speed is kept from them */
    CHECK(g.step < .06f);   /* the log: 0.2168 and 0.1789 m */
    CHECK(g.off < .15f);    /* half the flip is 0.105 m; the log: 0.54 m */
    CHECK(g.carried < .25f); /* the flip itself, taken up; the log: 0.4465 m */
  }
  /* The same made up, at four frame times: 0.21 m back and forth every frame for 1.5 s with jumps not looked for, then
   * an event while it goes on. The filter of 0.6.0-feel.46 ended 1.0 to 1.5 m from the middle of the two places. */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    for (unsigned d = 0; d < 4; ++d) {
      const Flipped g = flipped(.21f, times[d], 1500);
      printf("  0.21 m back and forth every frame, not looked at for 1.5 s, then an event, %g ms a frame: %.4f m from the middle at most, largest step "
             "%.4f m, %.4f m carried at most, largest speed kept from a flip not looked at %.2f m/s\n", (double)times[d], (double)g.off, (double)g.step,
             (double)g.carried, (double)g.speed);
      CHECK(g.speed < .01f);
      CHECK(g.off < .16f);     /* never outside the two places by more than 0.055 m */
      CHECK(g.carried < .27f); /* a flip and what a frame adds, not a speed run on */
    }
  }
  /* What is no way back keeps its speed as before: a sprint (7 m/s, even), a dive (the eye going faster every frame), and a single step that is not undone, all with jumps not looked for. */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    static const float dive[] = {3.5521f, 6.7848f, 7.9920f, 8}; /* the speed kept at the end by the filter of 0.6.0-feel.46 (m/s; under the measure a frame is averaged in) */
    for (unsigned d = 0; d < 4; ++d) {
      Run r; start(&r); float x = 0; const float dt = times[d]/1000;
      for (int i = 0; i < 40; ++i) { x += 7*dt; frame(&r, x, 1.5f, 0, NATIVE, false, false, times[d]); }
      CHECK(fabsf(r.c.speed[0]-7) < .01f && r.correction == 0);
      start(&r); x = 0; float v = 0; /* 60 m/s^2 */
      for (int i = 0; i < 12 && v < 7.5f; ++i) { v += 60*dt; x += v*dt; frame(&r, x, 1.5f, 0, NATIVE, false, false, times[d]); }
      CHECK(fabsf(r.c.speed[0]-dive[d]) < .0005f && r.correction == 0);
      start(&r); /* stands, steps 0.2 m once, stands: the step's speed is kept for its frame as before (the next frame is a stop) */
      for (int i = 0; i < 5; ++i) frame(&r, 0, 1.5f, 0, NATIVE, false, false, times[d]);
      frame(&r, .2f, 1.5f, 0, NATIVE, false, false, times[d]);
      CHECK(fabsf(r.c.speed[0]-fminf(.2f/dt, 8)) < .01f);
    }
  }
  /* THE CUT (round 19): a move straight out of an emote, the head camera holding the view and the gun out of the hands.
   * The game cuts the pose in one frame and the head camera rode it: nothing was looked for (no event in 600 ms), so
   * the whole cut was shown (0.27 to 0.62 m of step change). The ten cuts of four soaks (eleven frames), the head
   * given as the helper gives it: the head node, not the eye (round 19 review). */
  {
    static const struct { const HeadFrame *rows; int count, cut, frames; const char *name; } scenes[] = {
      {soak30_sprint, (int)(sizeof soak30_sprint/sizeof *soak30_sprint), 6, 1, "feel.51 Stable, sprint out of an emote"},
      {soak30_dive_back, (int)(sizeof soak30_dive_back/sizeof *soak30_dive_back), 6, 1, "feel.51 Stable, dive-back out of an emote"},
      {soak29_dive, (int)(sizeof soak29_dive/sizeof *soak29_dive), 6, 1, "feel.51 Bodycam, dive out of an emote"},
      {soak29_dive_back, (int)(sizeof soak29_dive_back/sizeof *soak29_dive_back), 6, 1, "feel.51 Bodycam, dive-back out of an emote"},
      {soak27_dive_back, (int)(sizeof soak27_dive_back/sizeof *soak27_dive_back), 6, 2, "feel.50 Stable, dive-back, a cut in two frames (0.255 m, the least)"},
      {soak27_cut_130444, (int)(sizeof soak27_cut_130444/sizeof *soak27_cut_130444), 6, 1, "feel.50 Stable, 13:04:44"},
      {soak27_cut_130517, (int)(sizeof soak27_cut_130517/sizeof *soak27_cut_130517), 6, 1, "feel.50 Stable, 13:05:17"},
      {soak28_cut_160334, (int)(sizeof soak28_cut_160334/sizeof *soak28_cut_160334), 6, 1, "feel.51 Bodycam, 16:03:34"},
      {soak28_cut_160402, (int)(sizeof soak28_cut_160402/sizeof *soak28_cut_160402), 5, 1, "feel.51 Bodycam, 16:04:02 (in its 6th row)"},
      {soak30_cut_163801, (int)(sizeof soak30_cut_163801/sizeof *soak30_cut_163801), 6, 1, "feel.51 Stable, 16:38:01"},
    };
    for (unsigned n = 0; n < sizeof scenes/sizeof *scenes; ++n) {
      const CutScene s = cut_scene(scenes[n].rows, scenes[n].count, scenes[n].cut+scenes[n].frames-1, false);
      printf("  %s: the cut's step change %.4f m (the log %.4f), the frame after %.4f beyond the eye's own step, after that %.4f beyond its own; "
             "%u cuts, %u taken up in the arc\n", scenes[n].name, (double)s.cut, (double)s.logged, (double)s.next, (double)s.arc, s.cuts, s.taken);
      CHECK(s.logged > .15f);              /* what the log showed, against the root (soak 30 sprint: 0.203 m, the root's 0.16 m jump left out) */
      CHECK(s.cut < .04f);                 /* the cut is not shown: less than the measure */
      CHECK(s.next < .03f);                /* the frame after: no more than the head's own start, and the curve */
      CHECK(s.arc < .03f);                 /* the dive's own arc after it is shown as it is, the curve under it (ease_accel 24 m/s^2) */
      CHECK(s.cuts == (unsigned)scenes[n].frames);
      CHECK(s.taken == 0);                 /* followed, not taken up */
    }
  }
  /* What is no cut is as it was: a body thrown (off foot: the helper does not give the head; given all the same, the
   * head was moving the frame before, 2.7 m/s, and is no cut either), and the held eye let go in an emote (0.156 m from
   * rest, under TFP_CONTINUITY_CUT_M). */
  {
    const CutScene thrown = cut_scene(soak28_thrown, (int)(sizeof soak28_thrown/sizeof *soak28_thrown), 8, false);
    const CutScene thrown_given = cut_scene(soak28_thrown, (int)(sizeof soak28_thrown/sizeof *soak28_thrown), 8, true);
    const CutScene held = cut_scene(soak30_held_eye, (int)(sizeof soak30_held_eye/sizeof *soak30_held_eye), 6, false);
    CHECK(thrown.cuts == 0 && thrown.apart < 1e-4f);
    CHECK(thrown_given.cuts == 0 && thrown_given.apart < 1e-4f);
    CHECK(held.cuts == 0 && held.apart < 1e-4f);
  }
  /* Made up, at 8, 16.7, 33.3 and 50 ms a frame: a pose cut of 0.4 m out of an emote, then the dive's own arc from rest
   * at 30 m/s^2 against a root lunging at 5 m/s. The cut is taken up and eased like a change of camera; the arc is
   * shown as it is, the curve under it. And honest fast motion from rest is no cut at any frame time: the head
   * from rest at 60 m/s^2 to 7 m/s (a dive's lunge) and at 120 m/s^2 to 7.7 m/s (prone out of an emote, Stable 308 of
   * the feel.47 soak: 0.13 m and 0.26 m in two frames at 33 ms; 0.29 m in its first frame at 50 ms). */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    for (unsigned d = 0; d < 4; ++d) {
      const MadeCut m = made_cut(.4f, 30, 3, times[d], true), lunge = made_cut(0, 60, 7, times[d], false), fall = made_cut(0, 120, 7.7f, times[d], false);
      printf("  a 0.4 m pose cut out of an emote, %g ms a frame: step change %.4f m in its frame, %.4f beyond the arc's own after it, %.3f m carried, "
             "under 0.01 m after %.0f ms, %u cuts; from rest at 60 and 120 m/s^2: %u and %u cuts\n", (double)times[d], (double)m.cut, (double)m.after,
             (double)m.carried, (double)m.settled, m.cuts, lunge.cuts, fall.cuts);
      CHECK(m.cuts == 1);
      CHECK(m.cut < .04f);
      CHECK(m.after < fmaxf(.03f, 24*times[d]*times[d]/1e6f+.005f)); /* the curve's own acceleration at most */
      CHECK(m.settled <= 450); /* within the cut's curve: 447 ms for 0.4 m (the quartic's start at ease_accel) */
      CHECK(lunge.cuts == 0 && lunge.carried == 0);
      CHECK(fall.cuts == 0 && fall.carried == 0);
    }
  }
  /* Round 19 review. Each exclusion of THE CUT on its own, against the same scene with the cut taken (`taken`): the
   * head node stands 0.4 s in the world, then drops 0.4 m in one frame (33.3 ms a frame). */
#ifdef TFP_CONTINUITY_CUT_M
  {
    enum { PLAIN_STILL = 12 };
    const float root[3] = {0, 0, 0}, high[3] = {.1f, .05f, 1.6f}, low[3] = {.1f, .05f, 1.2f}, lower[3] = {.1f, .05f, .8f}, lowest[3] = {.1f, .05f, .4f};
    TfpContinuity c; double tick;
#define PLAIN_START() do { memset(&c, 0, sizeof c); tick = 1000; for (int i = 0; i < PLAIN_STILL; ++i, tick += 33.3) plain_frame(&c, root, 0, high, HEAD, false, false, tick, true); } while (0)
    PLAIN_START(); plain_frame(&c, root, 0, low, HEAD, false, false, tick, true);
    CHECK(c.cuts == 1 && c.edges == 0); /* taken: the scene is a cut */
    /* a third cut in a row is none: the frame after a cut can be a cut too (seen: 0.39 and then 0.32 m), not a third */
    PLAIN_START();
    plain_frame(&c, root, 0, low, HEAD, false, false, tick, true); tick += 33.3;
    plain_frame(&c, root, 0, lower, HEAD, false, false, tick, true); tick += 33.3;
    plain_frame(&c, root, 0, lowest, HEAD, false, false, tick, true);
    CHECK(c.cuts == 2);
    /* a frame in which jumps are looked for is no cut (the native view's rule takes it, or not) */
    PLAIN_START(); plain_frame(&c, root, 0, low, HEAD, true, false, tick, true);
    CHECK(c.cuts == 0);
    /* nor is one in full aim (quiet: nothing is taken up), and the head's move there counts as its speed: the frame
     * after it, out of full aim and the head on down as far, is no cut either (it was moving) */
    PLAIN_START(); plain_frame(&c, root, 0, low, HEAD, false, true, tick, true); tick += 33.3;
    CHECK(c.cuts == 0);
    plain_frame(&c, root, 0, lower, HEAD, false, false, tick, true);
    CHECK(c.cuts == 0);
    /* nor a change of camera in that frame: it is taken up as a change of camera, and counted so */
    PLAIN_START(); plain_frame(&c, root, 0, low, NATIVE, false, false, tick, true);
    CHECK(c.cuts == 0 && c.edges == 1);
    /* the head's speed is not known in the frame after a start, nor after a restart: no cut there */
    memset(&c, 0, sizeof c); tick = 1000;
    plain_frame(&c, root, 0, high, HEAD, false, false, tick, true); tick += 33.3;
    plain_frame(&c, root, 0, low, HEAD, false, false, tick, true);
    CHECK(c.cuts == 0 && !c.active);
    PLAIN_START(); tick += 400; /* longer than the frame gap: the filter starts again */
    plain_frame(&c, root, 0, high, HEAD, false, false, tick, true); tick += 33.3;
    plain_frame(&c, root, 0, low, HEAD, false, false, tick, true);
    CHECK(c.restarts == 1 && c.cuts == 0);
    /* the root moving alone, the head node and the eye still in the world (the game re-seating the root): the head
     * moves against the root, not in the world, and it is no cut. 0.3, 0.5 and 1.0 m, and the root's yaw flipped
     * 160 degrees under a head node 0.1 m off its axis. Round 19 review: 0.5 m was a cut, the eye 0.500 m off. */
    static const float alone[] = {.3f, .5f, 1.0f};
    for (unsigned n = 0; n < 3; ++n) {
      PLAIN_START(); const float moved[3] = {-alone[n], 0, 0};
      plain_frame(&c, moved, 0, high, HEAD, false, false, tick, true);
      CHECK(c.cuts == 0);
    }
    PLAIN_START(); plain_frame(&c, root, 2.8f, high, HEAD, false, false, tick, true);
    CHECK(c.cuts == 0);
    /* and the head given without the world (no root's place known): nothing says the head moved in the world */
    memset(&c, 0, sizeof c); tick = 1000;
    for (int i = 0; i <= PLAIN_STILL; ++i, tick += 33.3) {
      const float relative[3] = {.1f, .23f, i < PLAIN_STILL ? 1.64f : 1.24f}, head[3] = {.1f, .05f, i < PLAIN_STILL ? 1.6f : 1.2f}; float out[3];
      CUT_STEP(&c, relative, NULL, NULL, head, HEAD, false, tick, out);
    }
    CHECK(c.cuts == 0);
#undef PLAIN_START
  }
  /* The ease of a cut ends at rest: its acceleration comes down to nothing with it, so the eye does not stop short where
   * the ease ends (round 19 review: soak 30 16:37:35.009, 300 ms after a cut, the step change went 0.009 -> 0.029 m
   * there). A cut of 0.4 m from rest with the head still after it, at 8, 16.7, 33.3 and 50 ms a frame. */
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    for (unsigned d = 0; d < 4; ++d) {
      const CutEnd e = cut_end(.4f, times[d]);
      printf("  a 0.4 m cut eased, %g ms a frame: step change %.4f m at most where the ease ends, %.4f m at most anywhere\n", (double)times[d], (double)e.end, (double)e.most);
      CHECK(e.cuts == 1);
      CHECK(e.end < .5f*e.most+1e-4f); /* where it ends, under half the most it changes (the cubic's end: 0.95 to 1 of it) */
    }
  }
#endif
  /* THE CLIMB (round 24). Jumps looked for (the native view held through a vault), the eye going up against the root
   * at 3 m/s, then in one frame down at 4 m/s for 150 ms (the head crouching onto the ledge while the root rises), then
   * still. The run is longer than its time: without the traversal the speed it began against (up) is handed to the
   * curve and the presented eye goes on up after the wanted eye turned; in a clamber's frame it is not. At 8, 16.7,
   * 33.3 and 50 ms a frame: how far the presented eye goes above where the wanted eye turned, and what is carried
   * once the wanted eye is still. The frames of a run are still taken up and go on at the kept speed (0.2 to 0.36 m
   * above here): THE CLIMB takes away the hand-off only. In the head camera of a clamber nothing is looked for at all
   * (src/camera_traversal.h); the native view held through a low vault keeps its runs, which lower the rig's own dip. */
  for (unsigned d = 0; d < 4; ++d) {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    float above[2] = {0, 0}, still[2] = {0, 0};
    for (int climb = 0; climb < 2; ++climb) {
      TfpContinuity c; memset(&c, 0, sizeof c); float z = 1.5f, top = 0, out[3];
      for (double t = 0; t < 1.2; t += times[d]/1000.0) {
        const float v = t < .2 ? 3.0f : t < .35 ? -4.0f : 0.0f;
        z += v*times[d]/1000;
        if (t < .2) top = z;
        const float wanted[3] = {0, .2f, z};
        c.traversal = climb != 0;
        const float carried = tfp_continuity_step(&c, wanted, NATIVE, true, false, 1000+t*1000, 250, &T, out);
        if (out[2]-top > above[climb]) above[climb] = out[2]-top;
        if (t > .35 && carried > still[climb]) still[climb] = carried;
      }
    }
    printf("  a run that turns in a climb, %g ms a frame: the presented eye %.3f m above where the eye turned (%.3f m without THE CLIMB), "
           "%.3f m carried once it is still (%.3f m)\n", (double)times[d], (double)above[1], (double)above[0], (double)still[1], (double)still[0]);
    CHECK(above[1] <= above[0]+1e-4f && still[1] <= still[0]+1e-4f);
    CHECK(still[1] < still[0]-.1f); /* the frames of a run still go on at the kept speed (they are taken up): only the hand-off goes */
  }
  printf("camera continuity: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
