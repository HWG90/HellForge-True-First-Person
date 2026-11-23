/* Continuity of the presented eye (src/camera_continuity.h): what is taken up, what is left alone, how the
 * correction ends. The figures in the comments are the game's, from the feel.22 session of 2026-11-22. */
#include "camera_continuity.h"
#include <stdio.h>
#include "reference/soak-6-steps.h"
#include "reference/soak-8-steps.h"
#include "reference/soak-9-runs.h"
#include "reference/soak-11-steps.h"
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
#define ROWS(a) a, (int)(sizeof a/sizeof *a)
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
  printf("camera continuity: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
