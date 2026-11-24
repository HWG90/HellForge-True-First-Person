/* The glide of the head camera's eye to the incoming weapon's eye at a swap (src/camera_glide.h). */
#include "camera_glide.h"
#include <stdio.h>
#include <stdlib.h>
#include "reference/soak-6-steps.h"
#include "reference/soak-19-glide.h"
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static float apart(const float *a, const float *b) {
  return sqrtf((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}
static bool same(const float *a, const float *b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }
static const double FRAMES[4] = {8, 16.7, 33.3, 50}; /* ms a frame */
/* A glide of `size` metres: from START to this. */
static const float START[3] = {.02f, -.05f, .03f};
static void end_of(float size, float *end) { end[0] = START[0]+size*.36f; end[1] = START[1]+size*.80f; end[2] = START[2]-size*.48f; }
/* A logged eye in the root's frame: a row of `rows`. */
static void logged_row(const SoakFrame *rows, int row, float *eye) {
  const SoakFrame *f = &rows[row];
  for (unsigned a = 0; a < 3; ++a) { eye[a] = 0; for (unsigned k = 0; k < 3; ++k) eye[a] += (f->wanted[k]-f->root[k])*f->axes[a*3+k]; }
}

/* ---- retail's glide: what the code did before the glide was cut out of the helper, and does without the lock ---- */
static void retail(void) {
  float end[3]; end_of(.47f, end);
  /* It starts at full speed: the first frame is the largest, 38 % of the way at 33 ms a frame, 0.178 m of 0.47 m. */
  {
    float eye[3]; memcpy(eye, START, 12);
    CHECK(tfp_eye_glide_retail(eye, end, .0333f));
    const float first = apart(eye, START);
    CHECK(fabsf(first/.47f-.3786f) < .001f && first > .177f && first < .179f);
    float before[3]; memcpy(before, eye, 12);
    CHECK(tfp_eye_glide_retail(eye, end, .0333f) && apart(eye, before) < first);
  }
  /* 1-exp(-t/70 ms) of the way after t, whatever the frames: 95 % after 210 ms, and 1.4 % still to go after 300. */
  for (unsigned f = 0; f < 4; ++f) {
    float eye[3]; memcpy(eye, START, 12); double ms = 0;
    while (ms+FRAMES[f] <= 300.5) { tfp_eye_glide_retail(eye, end, (float)(FRAMES[f]/1000)); ms += FRAMES[f];
      CHECK(fabs(apart(eye, START)/.47-(1-exp(-ms/70))) < 1e-4); }
    CHECK(apart(eye, end) > .004f && apart(eye, end) < .011f);
  }
  /* The same instant again, or a time that is no time: nothing moves. */
  {
    float eye[3]; memcpy(eye, START, 12);
    CHECK(!tfp_eye_glide_retail(eye, end, 0) && same(eye, START));
    CHECK(!tfp_eye_glide_retail(eye, end, -.01f) && same(eye, START));
    memcpy(eye, end, 12); CHECK(!tfp_eye_glide_retail(eye, end, .0333f) && same(eye, end)); /* there already */
  }
  /* The logged glide (tests/reference/soak-19-glide.h) is this one: from row 12, with the logged frame times, a glide
   * of 0.629 m steps by what the log shows in rows 13 to 15 (0.242, 0.146 and 0.091 m; the damping and the head's own
   * motion are in the log as well, so within 0.012 m). */
  {
    float eye[3] = {0, 0, 0}; const float end19[3] = {0, .629f, 0};
    for (int row = 13; row <= 15; ++row) {
      float before[3], was[3], is[3]; memcpy(before, eye, 12);
      tfp_eye_glide_retail(eye, end19, (soak19_glide[row].ms-soak19_glide[row-1].ms)/1000);
      logged_row(soak19_glide, row-1, was); logged_row(soak19_glide, row, is);
      CHECK(fabsf(apart(eye, before)-apart(is, was)) < .012f);
    }
  }
}

/* ---- the glide under the camera lock ---- */
/* How long a glide of `size` takes (ms) and its two rates (m/s^2), worked out here from the header's four numbers. */
static double glide_ms(double size) {
  const double ms = 1000*sqrt(2*size/(TFP_EYE_GLIDE_SHARE*TFP_EYE_GLIDE_ACCEL));
  return ms < TFP_EYE_GLIDE_MIN_MS ? TFP_EYE_GLIDE_MIN_MS : ms > TFP_EYE_GLIDE_MAX_MS ? TFP_EYE_GLIDE_MAX_MS : ms;
}
static double rate_up(double size) { const double t = glide_ms(size)/1000; return 2*size/(TFP_EYE_GLIDE_SHARE*t*t); }
static double rate_down(double size) { const double t = glide_ms(size)/1000; return 2*size/((1-TFP_EYE_GLIDE_SHARE)*t*t); }
/* How much of the way a glide has gone when `part` of its time is over. */
static double gone(double part) {
  const double p = TFP_EYE_GLIDE_SHARE;
  return part >= 1 ? 1 : part < p ? part*part/p : 1-(1-part)*(1-part)/(1-p);
}
typedef struct Played { float first, largest_change, largest_step; double over_ms; bool grew_then_shrank; } Played;
/* A glide of `size` from START at `frame` ms a frame, until 100 ms after it is over; every frame is checked against
 * `gone`. over_ms: the time of the first frame in which the eye is the target itself. */
static Played play(float size, double frame) {
  TfpEyeGlide g; tfp_eye_glide_reset(&g);
  float end[3], eye[3], step[3] = {0, 0, 0}, last = 0; end_of(size, end); memcpy(eye, START, 12);
  Played p = {0, 0, 0, -1, true};
  for (int i = 1; p.over_ms < 0 || i*frame <= p.over_ms+100; ++i) {
    float before[3], now[3], change[3]; memcpy(before, eye, 12);
    const bool moved = tfp_eye_glide_step(&g, eye, end, (float)(frame/1000));
    for (unsigned k = 0; k < 3; ++k) { now[k] = eye[k]-before[k]; change[k] = now[k]-step[k]; }
    const float length = sqrtf(now[0]*now[0]+now[1]*now[1]+now[2]*now[2]), changed = sqrtf(change[0]*change[0]+change[1]*change[1]+change[2]*change[2]);
    CHECK(moved == (length > 1e-6f) || length < 2e-6f);
    CHECK(fabs(apart(eye, START)-size*gone(i*frame/glide_ms(size))) < 2e-5);
    if (i == 1) p.first = length;
    if (changed > p.largest_change) p.largest_change = changed;
    if (length > p.largest_step) p.largest_step = length;
    /* it gathers way for its share of the time and loses it from there (the frame the share ends in can go either way) */
    if (i*frame <= glide_ms(size)*TFP_EYE_GLIDE_SHARE && length < last-1e-6f) p.grew_then_shrank = false;
    if ((i-1)*frame >= glide_ms(size)*TFP_EYE_GLIDE_SHARE && length > last+1e-6f) p.grew_then_shrank = false;
    if (p.over_ms < 0 && same(eye, end)) p.over_ms = i*frame;
    if (p.over_ms >= 0) CHECK(same(eye, end));
    if (p.over_ms >= 0 && i*frame > p.over_ms) CHECK(!moved);
    memcpy(step, now, 12); last = length;
    if (i > 10000) { CHECK(!"the glide never ends"); break; }
  }
  return p;
}
static void gentle(void) {
  const float sizes[4] = {.07f, .15f, .26f, .47f};
  /* The time a glide takes follows from its length: 150 ms at least and 250 ms at most, a frame at 20 frames a second
   * less than a swap leaves (nine frames at 30 frames a second in the soak of tests/reference/soak-19-glide.h). The
   * longest way gone at 14 m/s^2 in that is 0.2625 m; the helper asks for no longer one (GATE), but a longer one is
   * still a glide of 250 ms. */
  CHECK(fabs(glide_ms(.07)-150) < .01 && fabs(glide_ms(.15)-189) < .5 && fabs(glide_ms(.26)-248.8) < .5 && glide_ms(.47) == 250);
  CHECK(fabsf(TFP_EYE_GLIDE_FAR_M-.2625f) < 1e-6f && fabs(rate_up(.2625)-14) < 1e-4 && fabs(rate_down(.2625)-21) < 1e-4);
  /* It begins gently and ends at the target itself, at every frame time. */
  for (unsigned s = 0; s < 4; ++s) for (unsigned f = 0; f < 4; ++f) {
    const double dt = FRAMES[f]/1000;
    const Played p = play(sizes[s], FRAMES[f]);
    /* zero speed at the start: the first frame is what the speeding-up rate gives from rest, and no more */
    CHECK(p.first <= .5*rate_up(sizes[s])*dt*dt*1.001+1e-6);
    /* continuous velocity: no frame's step differs from the one before it by more than the slowing-down rate gives */
    CHECK(p.largest_change <= rate_down(sizes[s])*dt*dt*1.001+2e-5);
    CHECK(p.grew_then_shrank);
    /* over when its time is over, to the frame, and not a frame sooner */
    CHECK(p.over_ms >= glide_ms(sizes[s])-.01 && p.over_ms < glide_ms(sizes[s])+FRAMES[f]);
  }
  /* In figures: 0.25 m at 33 ms a frame began with 0.095 m on retail's glide (and was a jump to the continuity
   * filter); it begins with under 0.009 m, and no step changes by more than 0.025 m. */
  {
    const Played p = play(.25f, 33.3);
    float eye[3], end[3]; memcpy(eye, START, 12); end_of(.25f, end); tfp_eye_glide_retail(eye, end, .0333f);
    CHECK(apart(eye, START) > .094f && p.first < .009f && p.largest_change < .025f && p.largest_step < .07f);
  }
  /* The same place at the same time whatever the frames: 200 ms as 25, 4, 2 frames and one. */
  {
    const double frames[4] = {8, 50, 100, 200}; float at[4][3], end[3]; end_of(.25f, end);
    for (unsigned f = 0; f < 4; ++f) {
      TfpEyeGlide g; tfp_eye_glide_reset(&g); memcpy(at[f], START, 12);
      for (int i = 1; i*frames[f] <= 200.001; ++i) tfp_eye_glide_step(&g, at[f], end, (float)(frames[f]/1000));
      CHECK(apart(at[f], at[0]) < 1e-5f);
    }
    CHECK(fabs(apart(at[0], START)-.25*gone(200.0/glide_ms(.25))) < 1e-5);
  }
}

/* A glide of 0.25 m played at `frame` ms a frame; at `change_ms` the target becomes `second`. Returns the largest
 * change of step from the frame of the change on; `at_ms`, when above 0: `eye` takes the eye at that time. over_ms:
 * the first frame in which the eye is `second` itself. */
static float retarget(double frame, double change_ms, const float *second, double at_ms, float *at, double *over_ms) {
  TfpEyeGlide g; tfp_eye_glide_reset(&g);
  float first[3], eye[3], step[3] = {0, 0, 0}, largest = 0; end_of(.25f, first); memcpy(eye, START, 12);
  *over_ms = -1;
  for (int i = 1; i*frame <= change_ms+700; ++i) {
    float before[3], change[3]; memcpy(before, eye, 12);
    const bool changed = (i-1)*frame >= change_ms-.001; /* the frame before was the last with the first target */
    tfp_eye_glide_step(&g, eye, changed ? second : first, (float)(frame/1000));
    for (unsigned k = 0; k < 3; ++k) { change[k] = eye[k]-before[k]-step[k]; step[k] = eye[k]-before[k]; }
    const float size = sqrtf(change[0]*change[0]+change[1]*change[1]+change[2]*change[2]);
    if (changed && size > largest) largest = size;
    if (changed && *over_ms < 0 && same(eye, second)) *over_ms = i*frame;
    if (at && fabs(i*frame-at_ms) < .001) memcpy(at, eye, 12);
  }
  CHECK(same(eye, second));
  return largest;
}
static void target_changes(void) {
  double over = 0;
  /* A second swap inside the first: 100 ms into a glide of 0.25 m (the eye is going 1.4 m/s) the target becomes one
   * 0.3 m to the other side of the start. The eye keeps its speed in that frame and turns round over the frames after:
   * at 60 frames a second no step changes by more than 0.012 m, where a glide begun from rest in that frame would have
   * dropped the 0.023 m a frame it had. */
  const float other[3] = {START[0]-.3f*.6f, START[1]-.3f*.8f, START[2]};
  CHECK(retarget(16.7, 100, other, 0, NULL, &over) < .012f);
  CHECK(over > 100 && over <= 100+TFP_EYE_GLIDE_MAX_MS+16.7);
  for (unsigned f = 0; f < 4; ++f) { /* 0.37 m to the other side within the 250 ms, with the turning round: 44 m/s^2 at most */
    const double dt = FRAMES[f]/1000;
    CHECK(retarget(FRAMES[f], 100, other, 0, NULL, &over) <= 45*dt*dt);
    CHECK(over <= 100+TFP_EYE_GLIDE_MAX_MS+2*FRAMES[f]);
  }
  /* The same further along the same way (the target moves on by 0.2 m), and after the end, from rest again. (The
   * change is seen in the first frame at or after its time.) */
  float further[3]; end_of(.45f, further);
  for (unsigned f = 0; f < 4; ++f) {
    const double dt = FRAMES[f]/1000;
    CHECK(retarget(FRAMES[f], 100, further, 0, NULL, &over) <= 25*dt*dt);
    /* at 250 and at 400 ms the first glide is over (244 ms): glides from rest, 0.2 m on and 0.53 m back across the start */
    CHECK(retarget(FRAMES[f], 250, further, 0, NULL, &over) <= rate_down(.20)*dt*dt*1.001+2e-5);
    CHECK(retarget(FRAMES[f], 250, other, 0, NULL, &over) <= rate_down(.53)*dt*dt*1.001+2e-5);
    CHECK(retarget(FRAMES[f], 400, other, 0, NULL, &over) <= rate_down(.53)*dt*dt*1.001+2e-5);
    CHECK(retarget(FRAMES[f], 400, START, 0, NULL, &over) <= rate_down(.25)*dt*dt*1.001+2e-5); /* back where it came from, as any glide from rest */
  }
  /* Whatever the frames: the target changes at 100 ms; the eye at 200 and at 300 ms is the same at 10, 20, 50 and
   * 100 ms a frame. */
  {
    const double frames[4] = {10, 20, 50, 100}; float at200[4][3], at300[4][3];
    for (unsigned f = 0; f < 4; ++f) {
      retarget(frames[f], 100, other, 200, at200[f], &over); retarget(frames[f], 100, other, 300, at300[f], &over);
      CHECK(apart(at200[f], at200[0]) < 2e-5f && apart(at300[f], at300[0]) < 2e-5f);
    }
  }
  /* A target that changes in every frame for a while (between two points 0.05 m apart): the eye goes on without a
   * jump, and is at the target 250 ms after the last change at the latest. */
  for (unsigned f = 0; f < 4; ++f) {
    TfpEyeGlide g; tfp_eye_glide_reset(&g); const double dt = FRAMES[f]/1000;
    float a[3], b[3], eye[3], step[3] = {0, 0, 0}, largest = 0; end_of(.25f, a); end_of(.30f, b); memcpy(eye, START, 12);
    int i = 1, last_change = 0;
    for (; i*FRAMES[f] <= 250; ++i) {
      float before[3], change[3]; memcpy(before, eye, 12);
      tfp_eye_glide_step(&g, eye, i & 1 ? a : b, (float)dt); last_change = i;
      for (unsigned k = 0; k < 3; ++k) { change[k] = eye[k]-before[k]-step[k]; step[k] = eye[k]-before[k]; }
      const float size = sqrtf(change[0]*change[0]+change[1]*change[1]+change[2]*change[2]); if (size > largest) largest = size;
    }
    const float *end = last_change & 1 ? a : b;
    for (; (i-last_change)*FRAMES[f] <= TFP_EYE_GLIDE_MAX_MS+FRAMES[f]; ++i) {
      float before[3], change[3]; memcpy(before, eye, 12);
      tfp_eye_glide_step(&g, eye, end, (float)dt);
      for (unsigned k = 0; k < 3; ++k) { change[k] = eye[k]-before[k]-step[k]; step[k] = eye[k]-before[k]; }
      const float size = sqrtf(change[0]*change[0]+change[1]*change[1]+change[2]*change[2]); if (size > largest) largest = size;
    }
    CHECK(largest <= 45*dt*dt && same(eye, end));
  }
}

/* The eye 133 ms into a glide of 0.25 m at 33.3 ms a frame (four frames; it is going 1.9 m/s). */
static void under_way(TfpEyeGlide *g, float *eye, float *last_step) {
  float end[3], before[3]; end_of(.25f, end); memcpy(eye, START, 12); tfp_eye_glide_reset(g);
  for (int i = 0; i < 4; ++i) { memcpy(before, eye, 12); tfp_eye_glide_step(g, eye, end, .0333f); }
  *last_step = apart(eye, before);
}
static void left_mid_glide(void) {
  TfpEyeGlide g; float eye[3], end[3], last = 0; end_of(.25f, end);
  const float limit = (float)(.5*rate_up(.25)*.0333*.0333*1.001+1e-6);
  /* The view leaves the head camera mid-glide and the helper forgets the glide: the next one begins from rest,
   * wherever the eye then is. */
  under_way(&g, eye, &last); CHECK(last > .05f);
  tfp_eye_glide_reset(&g);
  { float before[3]; memcpy(before, eye, 12); CHECK(tfp_eye_glide_step(&g, eye, end, .0333f) && apart(eye, before) < limit); }
  /* Not forgotten, but the eye is not where the glide left it (the helper set it anew from the native view): the
   * same, from where it is. */
  under_way(&g, eye, &last);
  { const float anew[3] = {.3f, -.2f, .1f}; float before[3]; memcpy(eye, anew, 12); memcpy(before, eye, 12);
    CHECK(tfp_eye_glide_step(&g, eye, end, .0333f) && apart(eye, before) <= .5f*(float)rate_up(apart(anew, end))*.0333f*.0333f*1.001f+1e-6f);
    for (int i = 0; i < 12; ++i) tfp_eye_glide_step(&g, eye, end, .0333f);
    CHECK(same(eye, end)); }
  /* Frames missed in between (one frame of 199.8 ms, inside the helper's frame gap): the eye is where six frames
   * would have brought it. */
  {
    TfpEyeGlide other; float one[3], six[3], unused = 0;
    under_way(&g, one, &unused); under_way(&other, six, &unused);
    tfp_eye_glide_step(&g, one, end, .1998f);
    for (int i = 0; i < 6; ++i) tfp_eye_glide_step(&other, six, end, .0333f);
    CHECK(apart(one, six) < 1e-5f && same(one, end));
  }
  /* The same instant again, a time that is no time, a target that is no number: nothing moves. A target that is no
   * number forgets the glide as well. */
  under_way(&g, eye, &last);
  { float before[3]; memcpy(before, eye, 12);
    CHECK(!tfp_eye_glide_step(&g, eye, end, 0) && same(eye, before));
    CHECK(!tfp_eye_glide_step(&g, eye, end, -.01f) && same(eye, before));
    CHECK(!tfp_eye_glide_step(&g, eye, end, NAN) && same(eye, before));
    CHECK(tfp_eye_glide_step(&g, eye, end, .0333f) && fabsf(apart(eye, before)-last) < .03f); /* and it goes on as it went */
    const float bad[3] = {0, NAN, 0}; memcpy(before, eye, 12);
    CHECK(!tfp_eye_glide_step(&g, eye, bad, .0333f) && same(eye, before));
    CHECK(tfp_eye_glide_step(&g, eye, end, .0333f) && apart(eye, before) < limit); }
}
static void comes_to_rest(void) {
  /* No eye to go to any more while a glide is under way (the swap went back to the weapon it came from): the eye
   * comes to rest at the slowing-down rate, it does not stop dead. From 1.9 m/s that is 0.09 s and 0.08 m. */
  const double down = TFP_EYE_GLIDE_ACCEL*TFP_EYE_GLIDE_SHARE/(1-TFP_EYE_GLIDE_SHARE);
  for (unsigned f = 0; f < 4; ++f) {
    TfpEyeGlide g; float eye[3], from[3], last = 0, end[3]; end_of(.25f, end); const double dt = FRAMES[f]/1000;
    under_way(&g, eye, &last); memcpy(from, eye, 12);
    float step = last*(float)(FRAMES[f]/33.3), largest = 0; int frames = 0; bool shrank = true;
    for (int i = 1; i <= 400/FRAMES[f]; ++i) {
      float before[3]; memcpy(before, eye, 12);
      const bool moved = tfp_eye_glide_rest(&g, eye, (float)dt);
      const float now = apart(eye, before);
      if (i > 1 && now > step+1e-6f) shrank = false;
      if (i > 1 && step-now > largest) largest = step-now;
      if (moved) frames = i;
      step = now;
    }
    CHECK(shrank && largest <= down*dt*dt*1.001+2e-5);
    CHECK(frames*FRAMES[f] > 60 && frames*FRAMES[f] < 100+FRAMES[f]);
    CHECK(apart(eye, from) > .07f && apart(eye, from) < .10f);
    /* the way it was going: towards the target, and not past it */
    CHECK(apart(eye, end) < apart(from, end));
    /* and from rest a new glide begins gently */
    { float before[3]; memcpy(before, eye, 12);
      CHECK(tfp_eye_glide_step(&g, eye, end, (float)dt) && apart(eye, before) <= .5*rate_up(apart(before, end))*dt*dt*1.001+1e-6); }
  }
  /* Nothing under way, a glide that is over, an eye that was set anew: nothing moves. */
  {
    TfpEyeGlide g; tfp_eye_glide_reset(&g); float eye[3], end[3], last = 0; end_of(.25f, end); memcpy(eye, START, 12);
    CHECK(!tfp_eye_glide_rest(&g, eye, .0333f) && same(eye, START));
    for (int i = 0; i < 12; ++i) tfp_eye_glide_step(&g, eye, end, .0333f);
    CHECK(same(eye, end) && !tfp_eye_glide_rest(&g, eye, .0333f) && same(eye, end));
    under_way(&g, eye, &last); memcpy(eye, START, 12);
    CHECK(!tfp_eye_glide_rest(&g, eye, .0333f) && same(eye, START));
  }
}
/* GATE: under the lock a way of no more than 0.2625 m is glided gently and a longer one on retail's 70 ms. */
static void gate(void) {
  for (unsigned f = 0; f < 4; ++f) {
    const float dt = (float)(FRAMES[f]/1000);
    TfpEyeGlide g; tfp_eye_glide_reset(&g); TfpEyeGlide plain; tfp_eye_glide_reset(&plain);
    float near_end[3], far_end[3], eye[3], other[3]; end_of(.26f, near_end); end_of(.27f, far_end);
    /* 0.26 m: the gentle glide, frame for frame */
    memcpy(eye, START, 12); memcpy(other, START, 12);
    for (int i = 0; i*FRAMES[f] < 400; ++i) {
      CHECK(tfp_eye_glide_lock_step(&g, eye, near_end, dt) == tfp_eye_glide_step(&plain, other, near_end, dt) && same(eye, other));
      if (!i) CHECK(apart(eye, START) <= .5*rate_up(.26)*dt*dt*1.001+1e-6);
    }
    CHECK(same(eye, near_end));
    /* 0.27 m: retail's, to the bit, for as long as it moves */
    tfp_eye_glide_reset(&g); memcpy(eye, START, 12); memcpy(other, START, 12);
    for (int i = 0; i*FRAMES[f] < 400; ++i)
      CHECK(tfp_eye_glide_lock_step(&g, eye, far_end, dt) == tfp_eye_glide_retail(other, far_end, dt) && same(eye, other));
    CHECK(apart(eye, far_end) > 0 && apart(eye, far_end) < .002f);
    /* Which of the two is settled when the glide begins. A retail one whose target comes near stays retail's while it
     * moves (the eye is 0.001 m from it and going): it does not turn gentle half-way. */
    memcpy(other, eye, 12);
    CHECK(tfp_eye_glide_lock_step(&g, eye, near_end, dt) == tfp_eye_glide_retail(other, near_end, dt) && same(eye, other));
    /* A gentle one whose target moves away stays gentle, with the speed it has. */
    tfp_eye_glide_reset(&g); tfp_eye_glide_reset(&plain); memcpy(eye, START, 12); memcpy(other, START, 12);
    float far_away[3]; end_of(.60f, far_away);
    for (int i = 0; i*FRAMES[f] < 100; ++i) { tfp_eye_glide_lock_step(&g, eye, near_end, dt); tfp_eye_glide_step(&plain, other, near_end, dt); }
    for (int i = 0; i*FRAMES[f] < 300; ++i)
      CHECK(tfp_eye_glide_lock_step(&g, eye, far_away, dt) == tfp_eye_glide_step(&plain, other, far_away, dt) && same(eye, other));
    CHECK(same(eye, far_away));
    /* A gentle glide that is over and a new target: a glide begun from rest, settled anew. 0.60 m back to the
     * start is retail's. */
    memcpy(other, eye, 12);
    CHECK(tfp_eye_glide_lock_step(&g, eye, START, dt) == tfp_eye_glide_retail(other, START, dt) && same(eye, other));
    CHECK(fabs(apart(eye, far_away)-.60*(1-exp(-FRAMES[f]/70))) < 1e-4);
    /* An eye that is not where the last frame left it is a glide begun: settled anew, here retail's (0.60 m). */
    memcpy(eye, START, 12); memcpy(other, START, 12);
    CHECK(tfp_eye_glide_lock_step(&g, eye, far_away, dt) == tfp_eye_glide_retail(other, far_away, dt) && same(eye, other));
    /* and once a retail glide moves no more, the next one is settled anew: a near target, gently */
    tfp_eye_glide_reset(&g); memcpy(eye, START, 12);
    for (int i = 0; i < 4000 && tfp_eye_glide_lock_step(&g, eye, far_end, dt); ++i) {}
    CHECK(!tfp_eye_glide_lock_step(&g, eye, far_end, dt));
    { float before[3], next[3]; memcpy(before, eye, 12); end_of(.40f, next);
      CHECK(tfp_eye_glide_lock_step(&g, eye, next, dt) && apart(eye, before) <= .5*rate_up(apart(before, next))*dt*dt*1.001+1e-6); }
    /* Nothing to come to rest in a retail glide: it stops in the frame, as it does without the lock. */
    tfp_eye_glide_reset(&g); memcpy(eye, START, 12);
    tfp_eye_glide_lock_step(&g, eye, far_end, dt); memcpy(other, eye, 12);
    CHECK(!tfp_eye_glide_rest(&g, eye, dt) && same(eye, other));
    /* A target that is no number: nothing moves. */
    { const float bad[3] = {0, NAN, 0}; CHECK(!tfp_eye_glide_lock_step(&g, eye, bad, dt) && same(eye, other)); }
  }
}
/* The logged swaps (tests/reference/soak-19-glide.h), with their frame times. */
static void logged(void) {
  /* soak19_glide, 0.629 m: longer than the gate allows, so under the lock it is retail's glide to the bit (0.242 m in
   * its first frame, 0.009 m left in row 21, the head camera's last). */
  {
    TfpEyeGlide g; tfp_eye_glide_reset(&g);
    float eye[3] = {0, 0, 0}, retail_eye[3] = {0, 0, 0}; const float end[3] = {0, .629f, 0};
    for (int row = 13; row <= 21; ++row) {
      const float dt = (soak19_glide[row].ms-soak19_glide[row-1].ms)/1000;
      CHECK(tfp_eye_glide_lock_step(&g, eye, end, dt) == tfp_eye_glide_retail(retail_eye, end, dt) && same(eye, retail_eye));
      if (row == 13) CHECK(eye[1] > .23f);
    }
    CHECK(apart(eye, end) > .008f && apart(eye, end) < .010f);
  }
  /* soak19_glide_standing, 0.209 m, the body standing: the log's first glide frame (row 19) is 0.079 m, a jump to the
   * continuity filter, which carried 0.116 m for it. Gently that frame is under 0.008 m, no step changes by more than
   * 0.024 m, and the eye is at the target itself in row 25, 234 ms after the weapon identity changed and ten rows
   * before the native view is back (retail's had 0.007 m left there). */
  {
    TfpEyeGlide g; tfp_eye_glide_reset(&g);
    float was[3], is[3], eye[3] = {0, 0, 0}, retail_eye[3] = {0, 0, 0}, step = 0, largest = 0; const float end[3] = {0, .209f, 0};
    logged_row(soak19_glide_standing, 18, was); logged_row(soak19_glide_standing, 19, is);
    CHECK(fabsf(apart(is, was)-.079f) < .002f);
    for (int row = 19; row <= 25; ++row) {
      const float dt = (soak19_glide_standing[row].ms-soak19_glide_standing[row-1].ms)/1000, before = eye[1];
      tfp_eye_glide_lock_step(&g, eye, end, dt); tfp_eye_glide_retail(retail_eye, end, dt);
      if (row == 19) CHECK(eye[1] < .008f && fabsf(retail_eye[1]-.079f) < .003f);
      if (fabsf(eye[1]-before-step) > largest) largest = fabsf(eye[1]-before-step);
      step = eye[1]-before;
      if (row == 24) CHECK(apart(eye, end) > 0);
    }
    CHECK(largest < .024f && same(eye, end) && apart(retail_eye, end) > .006f && apart(retail_eye, end) < .008f);
  }
}

/* ---- the held eye let go (round 19): tfp_eye_glide_release ---- */
/* The judge's limit on a frame's change of step: 0.06 m at 30 frames a second (54 m/s^2 there), scaled by the square of
 * the frame time or, where that is less, in proportion to it. */
static double judge_limit(double ms) { const double r = ms/33.3; return .06*(r < r*r ? r : r*r); }
static double release_ms(double size) {
  const double ms = 1000*sqrt(2*size/(TFP_EYE_GLIDE_SHARE*TFP_EYE_RELEASE_ACCEL));
  return ms < TFP_EYE_RELEASE_MIN_MS ? TFP_EYE_RELEASE_MIN_MS : ms > TFP_EYE_RELEASE_MAX_MS ? TFP_EYE_RELEASE_MAX_MS : ms;
}
static double release_down(double size) { const double t = release_ms(size)/1000; return 2*size/((1-TFP_EYE_GLIDE_SHARE)*t*t); }
static void release(void) {
  /* The time: 300 ms at least and 450 at most; the Scythe's 0.44 m is 404 ms, gathering way at 9 m/s^2 and losing it
   * at 13.5 (under the 14 the swap's glide gathers way at, which the filter follows without taking anything up). */
  CHECK(release_ms(.05) == 300 && fabs(release_ms(.44)-404.1) < .5 && release_ms(1.0) == 450);
  CHECK(fabs(release_down(.44)-13.5) < .01 && release_down(1.0) < 25);
  const float sizes[5] = {.05f, .10f, .30f, .44f, .60f};
  for (unsigned s = 0; s < 5; ++s) for (unsigned f = 0; f < 4; ++f) {
    const double dt = FRAMES[f]/1000;
    TfpEyeGlide g; tfp_eye_glide_reset(&g);
    float end[3], eye[3], step[3] = {0, 0, 0}, first = 0, largest = 0; end_of(sizes[s], end); memcpy(eye, START, 12);
    double over = -1; bool toward = true; float before_left = apart(eye, end);
    for (int i = 1; i*FRAMES[f] <= 700; ++i) {
      float before[3], now[3], change[3]; memcpy(before, eye, 12);
      tfp_eye_glide_release(&g, eye, end, (float)dt);
      for (unsigned k = 0; k < 3; ++k) { now[k] = eye[k]-before[k]; change[k] = now[k]-step[k]; }
      const float changed = sqrtf(change[0]*change[0]+change[1]*change[1]+change[2]*change[2]);
      if (i == 1) first = apart(eye, before);
      if (changed > largest) largest = changed;
      /* the same place at the same time as the shape of the swap's glide, on the release's time */
      CHECK(fabs(apart(eye, START)-sizes[s]*gone(i*FRAMES[f]/release_ms(sizes[s]))) < 2e-5);
      if (apart(eye, end) > before_left+1e-6f) toward = false;
      before_left = apart(eye, end);
      if (over < 0 && same(eye, end)) over = i*FRAMES[f];
      memcpy(step, now, 12);
    }
    /* from rest, to rest at the target itself, never back, and no frame's step changes by more than its slowing-down
     * rate gives, which is well inside the judge's limit at every frame time */
    CHECK(first <= .5*release_down(sizes[s])*(1-TFP_EYE_GLIDE_SHARE)/TFP_EYE_GLIDE_SHARE*dt*dt*1.001+1e-6);
    CHECK(largest <= release_down(sizes[s])*dt*dt*1.001+2e-5 && largest <= judge_limit(FRAMES[f]));
    CHECK(toward && over >= release_ms(sizes[s])-.01 && over < release_ms(sizes[s])+FRAMES[f]);
  }
  /* A new target while the release is under way (a hold that comes while letting go): from where the eye is with the
   * speed it has, on the release's rates, to the new target itself. */
  for (unsigned f = 0; f < 4; ++f) {
    const double dt = FRAMES[f]/1000;
    TfpEyeGlide g; tfp_eye_glide_reset(&g); float eye[3], end[3], other[3] = {.05f, .12f, -.02f}, last[3] = {0, 0, 0}, before[3], now[3];
    end_of(.44f, end); memcpy(eye, START, 12); float largest = 0;
    for (int i = 1; i*FRAMES[f] <= 900; ++i) {
      memcpy(before, eye, 12); tfp_eye_glide_release(&g, eye, i*FRAMES[f] <= 150 ? end : other, (float)dt);
      float change[3]; for (unsigned k = 0; k < 3; ++k) { now[k] = eye[k]-before[k]; change[k] = now[k]-last[k]; }
      const float changed = sqrtf(change[0]*change[0]+change[1]*change[1]+change[2]*change[2]);
      if (changed > largest) largest = changed;
      memcpy(last, now, 12);
    }
    CHECK(largest <= judge_limit(FRAMES[f]) && same(eye, other));
  }
  /* The release is a glide like the others to the rest of the header: after it, tfp_eye_glide_lock_step to a new target
   * begins anew (GATE), and an eye set anew begins from rest. */
  {
    TfpEyeGlide g; tfp_eye_glide_reset(&g); float eye[3], end[3], before[3]; end_of(.44f, end); memcpy(eye, START, 12);
    for (int i = 0; i < 20; ++i) tfp_eye_glide_release(&g, eye, end, .0333f);
    CHECK(same(eye, end) && g.release);
    const float near[3] = {end[0]+.1f, end[1], end[2]}; memcpy(before, eye, 12);
    CHECK(tfp_eye_glide_lock_step(&g, eye, near, .0333f) && !g.release && apart(eye, before) <= .5f*(float)rate_up(.1)*.0333f*.0333f*1.001f+1e-6f);
    tfp_eye_glide_reset(&g); memcpy(eye, START, 12);
    for (int i = 0; i < 4; ++i) tfp_eye_glide_release(&g, eye, end, .0333f);
    const float anew[3] = {.3f, -.2f, .1f}; memcpy(eye, anew, 12); memcpy(before, eye, 12);
    CHECK(tfp_eye_glide_release(&g, eye, end, .0333f) && apart(eye, before) < .006f);
  }
  /* A swap's glide that goes on from a release under way (tfp_eye_glide_lock_step to a new target while the eye is
   * still being let go) is a swap's glide from then on, not taken for a release: tfp_eye_glide_releasing says no, so the
   * helper does not send the next frames, or a later swap, through the release's rates (review of round 19, M6). */
  {
    TfpEyeGlide g; tfp_eye_glide_reset(&g); float eye[3], end[3]; end_of(.44f, end); memcpy(eye, START, 12);
    for (int i = 0; i < 4; ++i) tfp_eye_glide_release(&g, eye, end, .0333f);
    CHECK(tfp_eye_glide_releasing(&g));
    const float near[3] = {eye[0]+.1f, eye[1], eye[2]};
    CHECK(tfp_eye_glide_lock_step(&g, eye, near, .0333f) && g.active && !g.release && !tfp_eye_glide_releasing(&g));
    for (int i = 0; i < 3; ++i) { tfp_eye_glide_lock_step(&g, eye, near, .0333f); CHECK(!tfp_eye_glide_releasing(&g)); }
  }
  /* No number, no time: nothing moves; no number forgets the glide. */
  {
    TfpEyeGlide g; tfp_eye_glide_reset(&g); float eye[3], end[3], before[3]; end_of(.44f, end); memcpy(eye, START, 12);
    for (int i = 0; i < 4; ++i) tfp_eye_glide_release(&g, eye, end, .0333f);
    memcpy(before, eye, 12);
    CHECK(!tfp_eye_glide_release(&g, eye, end, 0) && same(eye, before));
    CHECK(!tfp_eye_glide_release(&g, eye, end, NAN) && same(eye, before));
    const float bad[3] = {0, NAN, 0};
    CHECK(!tfp_eye_glide_release(&g, eye, bad, .0333f) && same(eye, before) && !g.active);
  }
}

/* ---- the hold lapses while a swap's glide is under way (round 19 review: BLOCKING 1, IMPORTANT 2, MINOR 4) ---- */
/* How far `p` is from the triangle a, b, c (from the nearest of its sides where it has no area): 0 inside it. */
static float dot3(const float *x, const float *y) { return x[0]*y[0]+x[1]*y[1]+x[2]*y[2]; }
static float to_side(const float *p, const float *x, const float *y) {
  float d[3], q[3], r[3]; for (unsigned k = 0; k < 3; ++k) { d[k] = y[k]-x[k]; q[k] = p[k]-x[k]; }
  const float l = dot3(d, d), s = l > 0 ? fminf(1, fmaxf(0, dot3(q, d)/l)) : 0;
  for (unsigned k = 0; k < 3; ++k) r[k] = q[k]-s*d[k];
  return sqrtf(dot3(r, r));
}
static float outside_span(const float *p, const float *a, const float *b, const float *c) {
  const float sides = fminf(to_side(p, a, b), fminf(to_side(p, b, c), to_side(p, c, a)));
  float u[3], v[3], w[3], n[3]; for (unsigned k = 0; k < 3; ++k) { u[k] = b[k]-a[k]; v[k] = c[k]-a[k]; w[k] = p[k]-a[k]; }
  n[0] = u[1]*v[2]-u[2]*v[1]; n[1] = u[2]*v[0]-u[0]*v[2]; n[2] = u[0]*v[1]-u[1]*v[0];
  const float area = dot3(n, n);
  if (!(area > 1e-10f)) return sides;
  const float uu = dot3(u, u), uv = dot3(u, v), vv = dot3(v, v), wu = dot3(w, u), wv = dot3(w, v), den = uu*vv-uv*uv;
  const float s = (vv*wu-uv*wv)/den, t = (uu*wv-uv*wu)/den;
  return s >= 0 && t >= 0 && s+t <= 1 ? fabsf(dot3(w, n))/sqrtf(area) : sides;
}
/* A swap's glide under the lock (tfp_eye_glide_lock_step) from `a` to `b`, `frames_in` frames under way when the hold
 * lapses; then tfp_eye_glide_lapse to the head camera's own eye (0) until it is over. The swap's glide runs to its end
 * (the gentle one: its own end; retail's: within TFP_EYE_GLIDE_SETTLED_M of its target), and the release begins from
 * rest after it. Returns false when the swap's glide was over before the lapse (nothing to judge). */
typedef struct LapseRun { float outside, over_bound, lapse_step, step_before, lapse_ms, late_ms; bool ended; } LapseRun;
static bool lapse_run(const float *a, const float *b, int frames_in, double ms, LapseRun *r) {
  const float dt = (float)(ms/1000), zero[3] = {0, 0, 0};
  TfpEyeGlide g; tfp_eye_glide_reset(&g); float eye[3], before[3], step[3] = {0, 0, 0}; memcpy(eye, a, 12);
  for (int i = 0; i < frames_in; ++i) {
    memcpy(before, eye, 12); tfp_eye_glide_lock_step(&g, eye, b, dt);
    for (unsigned k = 0; k < 3; ++k) step[k] = eye[k]-before[k];
  }
  if (!g.retail && !(g.active && g.elapsed < g.duration)) return false;
  memset(r, 0, sizeof *r); r->outside = outside_span(eye, a, b, zero); r->over_bound = -1;
  r->step_before = sqrtf(dot3(step, step));
  int n = 0;
  for (; n < 4000; ++n) {
    memcpy(before, eye, 12);
    const bool going = tfp_eye_glide_lapse(&g, eye, zero, dt);
    float now[3], change[3]; for (unsigned k = 0; k < 3; ++k) { now[k] = eye[k]-before[k]; change[k] = now[k]-step[k]; }
    const float changed = sqrtf(dot3(change, change)), had = sqrtf(dot3(step, step));
    if (n == 0) r->lapse_step = sqrtf(dot3(now, now));
    r->outside = fmaxf(r->outside, outside_span(eye, a, b, zero));
    /* No frame's step changes by more than retail's own glide slows it in the frame, or than the gentle glides' rates
     * give (the swap's 21 m/s^2 at most, the release's 25 for the longest ways), or than stopping retail's glide within
     * TFP_EYE_GLIDE_SETTLED_M of its target. */
    const float bound = fmaxf(fmaxf(had*(1-expf(-dt/.07f)), 25*dt*dt), TFP_EYE_GLIDE_SETTLED_M)+1e-5f;
    r->over_bound = fmaxf(r->over_bound, changed-bound);
    memcpy(step, now, 12);
    if (!going) break;
  }
  r->lapse_ms = (float)((n+1)*ms); r->ended = same(eye, zero);
  /* how much longer than the swap's glide (the gentle one's 250 ms at most; retail's to within TFP_EYE_GLIDE_SETTLED_M
   * of its target, 70 ms * ln(way / 1 mm)) and the release's 450 ms, with two frames for the frame times */
  const float swap_ms = apart(a, b) > TFP_EYE_GLIDE_FAR_M ? 70*logf(apart(a, b)/TFP_EYE_GLIDE_SETTLED_M) : TFP_EYE_GLIDE_MAX_MS;
  r->late_ms = r->lapse_ms-(swap_ms+TFP_EYE_RELEASE_MAX_MS+2*(float)ms);
  return true;
}
static void lapse_mid_swap(void) {
  const float u[3] = {.36f, .80f, -.48f}, side[3] = {.80f, -.36f, 0}; /* the swap's way, and across it */
  for (int retail = 0; retail < 2; ++retail) for (unsigned f = 0; f < 4; ++f) {
    float outside = 0, over = -1, lapse_ms = 0, late = -1e9f; int runs = 0, ended = 0;
    for (float sa = -.8f; sa <= .81f; sa += .1f) for (float sb = -.8f; sb <= .81f; sb += .1f) for (int across = 0; across < 2; ++across) {
      float a[3], b[3];
      for (unsigned k = 0; k < 3; ++k) { a[k] = sa*u[k]; b[k] = sb*u[k]+(across ? .06f*side[k] : 0); }
      if ((apart(a, b) > TFP_EYE_GLIDE_FAR_M) != retail || apart(a, b) < .02f) continue;
      for (int k = 1; k <= 40; ++k) {
        LapseRun r; if (!lapse_run(a, b, k, FRAMES[f], &r)) break;
        ++runs; if (r.ended) ++ended;
        outside = fmaxf(outside, r.outside); over = fmaxf(over, r.over_bound); lapse_ms = fmaxf(lapse_ms, r.lapse_ms); late = fmaxf(late, r.late_ms);
      }
    }
    if (getenv("TFP_LAPSE_TABLE")) printf("  lapse mid-%s glide, %4.1f ms a frame: %d runs, outside the span %.6f m, over the bound %+.6f m, longest %.0f ms\n",
        retail ? "retail" : "gentle", FRAMES[f], runs, outside, over, lapse_ms);
    /* within the span of the eye, the swap's target and the head camera's eye (to 0.05 mm, for float; 0.01 mm seen);
     * no frame over the bound; at the head camera's eye at the end, within the swap's glide and the release's 450 ms */
    CHECK(runs > 100 && ended == runs && outside <= 5e-5f && over <= 0 && late <= 0);
  }
  /* The lapse frame of a retail glide under way is no dead stop: the eye goes on by retail's own step (review of round
   * 19, "B lapse": from rest it stopped dead, 0.05 m of step change at 33 ms a frame). */
  for (unsigned f = 0; f < 4; ++f) for (int k = 1; k <= 3; ++k) {
    float a[3], b[3]; memcpy(a, START, 12); end_of(.81f, b);
    LapseRun r; CHECK(lapse_run(a, b, k, FRAMES[f], &r));
    CHECK(fabsf(r.lapse_step-r.step_before*expf(-(float)FRAMES[f]/70)) < 1e-4f);
  }
  /* MINOR 4 of that review: a gentle swap glide whose target is at the head camera's eye or near it, under way when the
   * hold lapses, does not overshoot it (a release that took over its speed went 0.05 to 0.085 m past). */
  for (unsigned f = 0; f < 4; ++f) for (float near = 0; near <= .031f; near += .01f) for (int k = 1; k <= 12; ++k) {
    const float a[3] = {.18f*u[0], .18f*u[1], .18f*u[2]}, b[3] = {near*u[0], near*u[1], near*u[2]};
    LapseRun r; if (!lapse_run(a, b, k, FRAMES[f], &r)) break;
    CHECK(r.outside <= 5e-5f && r.ended);
  }
  /* Nothing under way (no glide, a gentle one that is over, an eye set anew): the release from rest at once, its
   * first step no more than half a frame of gathering way at TFP_EYE_RELEASE_ACCEL. */
  {
    const float FROM_REST = .5f*TFP_EYE_RELEASE_ACCEL*.0333f*.0333f*1.001f;
    TfpEyeGlide g; tfp_eye_glide_reset(&g); float eye[3], before[3]; const float zero[3] = {0, 0, 0}; memcpy(eye, START, 12);
    memcpy(before, eye, 12); CHECK(tfp_eye_glide_lapse(&g, eye, zero, .0333f) && tfp_eye_glide_releasing(&g) && apart(eye, before) <= FROM_REST);
    float end[3]; end_of(.2f, end); tfp_eye_glide_reset(&g); memcpy(eye, START, 12);
    for (int i = 0; i < 12; ++i) tfp_eye_glide_lock_step(&g, eye, end, .0333f);
    CHECK(same(eye, end)); memcpy(before, eye, 12);
    CHECK(tfp_eye_glide_lapse(&g, eye, zero, .0333f) && tfp_eye_glide_releasing(&g) && apart(eye, before) <= FROM_REST);
    end_of(.2f, end); tfp_eye_glide_reset(&g); memcpy(eye, START, 12);
    for (int i = 0; i < 3; ++i) tfp_eye_glide_lock_step(&g, eye, end, .0333f);
    eye[0] += .05f; memcpy(before, eye, 12); /* set anew */
    CHECK(tfp_eye_glide_lapse(&g, eye, zero, .0333f) && tfp_eye_glide_releasing(&g) && apart(eye, before) <= FROM_REST);
    /* no number: nothing moves, and the glide is forgotten */
    const float bad[3] = {0, NAN, 0}; memcpy(before, eye, 12);
    CHECK(!tfp_eye_glide_lapse(&g, eye, bad, .0333f) && same(eye, before) && !g.active && !g.retail);
  }
}

int main(void) {
  retail();
  gentle();
  target_changes();
  left_mid_glide();
  comes_to_rest();
  gate();
  logged();
  release();
  lapse_mid_swap();
  printf("camera glide: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
