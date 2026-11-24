/* The eye going back and forth (src/camera_alternation.h): which frames the rule counts. It acts on nothing; these
 * are the counts the "Alternation:" line reports. */
#include "camera_alternation.h"
#include <stdio.h>
#include "reference/soak-6-steps.h"
#include "reference/soak-19-steps.h"
#include "reference/soak-21-alternation.h"
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
typedef struct Seen { int frames, first; float largest; } Seen; /* frames counted, the first of them (-1: none), the largest move among them */
/* Logged rows, the eye in the root's frame as the helper gives it. */
static Seen logged(const SoakFrame *rows, int count) {
  TfpAlternation a; memset(&a, 0, sizeof a); Seen s = {0, -1, 0};
  for (int i = 0; i < count; ++i) {
    const SoakFrame *r = &rows[i]; float relative[3] = {0}, move = 0;
    for (int x = 0; x < 3; ++x) for (int k = 0; k < 3; ++k) relative[x] += (r->wanted[k]-r->root[k])*r->axes[x*3+k];
    if (!tfp_alternation_step(&a, relative, (uint32_t)r->source, false, 1000.0+(double)r->ms, 250, &move)) continue;
    if (!s.frames++) s.first = i;
    if (move > s.largest) s.largest = move;
  }
  return s;
}
/* Made up: the eye at x for one frame each, `dt` ms apart. */
static Seen made(const float *x, int count, float dt, bool quiet) {
  TfpAlternation a; memset(&a, 0, sizeof a); Seen s = {0, -1, 0};
  for (int i = 0; i < count; ++i) {
    const float eye[3] = {x[i], 1.5f, .1f}; float move = 0;
    if (!tfp_alternation_step(&a, eye, 2, quiet, 1000.0+(double)i*(double)dt, 250, &move)) continue;
    if (!s.frames++) s.first = i;
    if (move > s.largest) s.largest = move;
  }
  return s;
}
int main(void) {
  /* The logged end of the alternation of soak 21: rows 0 to 7 go back and forth 0.19 to 0.21 m, from row 8 the eye
   * stands. Row 1 is the first move, rows 2 and 3 the first two returns: counted from row 3, through the last flip
   * (row 7) and the two frames within 70 ms of it (rows 8 and 9), seven frames. */
  {
    const Seen s = logged(soak21_alternation, (int)(sizeof soak21_alternation/sizeof *soak21_alternation));
    printf("  soak 21, 03:13:53.289 on: %d frames counted, the first row %d, largest move %.3f m\n", s.frames, s.first, (double)s.largest);
    CHECK(s.frames == 7 && s.first == 3);
    CHECK(s.largest > .19f && s.largest < .24f); /* the flips in these rows are 0.19 to 0.23 m */
  }
  /* Real motion in the logs: a sprint through a swap, a dive out of a sprint. Nothing is counted. */
  CHECK(logged(soak6_sprint_in_swap, (int)(sizeof soak6_sprint_in_swap/sizeof *soak6_sprint_in_swap)).frames == 0);
  CHECK(logged(soak19_stop, (int)(sizeof soak19_stop/sizeof *soak19_stop)).frames == 0);
  static const float times[] = {8, 16.7f, 33.3f, 50};
  for (unsigned d = 0; d < 4; ++d) {
    float x[40]; const float dt = times[d];
    /* back and forth 0.2 m every frame: counted from the second return (the fourth frame) to the end */
    for (int i = 0; i < 40; ++i) x[i] = (i & 1) ? .2f : 0;
    Seen s = made(x, 40, dt, false);
    CHECK(s.frames == 37 && s.first == 3 && fabsf(s.largest-.2f) < 1e-5f);
    CHECK(made(x, 40, dt, true).frames == 0);                 /* never in full aim */
    /* once there and back: one return, nothing counted */
    for (int i = 0; i < 40; ++i) x[i] = i == 10 ? .2f : 0;
    CHECK(made(x, 40, dt, false).frames == 0);
    /* a sprint: 7 m/s one way (a move every frame from 8 ms on at 0.056 m), never a return */
    for (int i = 0; i < 40; ++i) x[i] = 7*dt/1000*(float)i;
    CHECK(made(x, 40, dt, false).frames == 0);
    /* under the step: 0.04 m back and forth is not a move */
    for (int i = 0; i < 40; ++i) x[i] = (i & 1) ? .04f : 0;
    CHECK(made(x, 40, dt, false).frames == 0);
    /* a return that misses by more than a quarter: 0.2 m out, 0.14 m back, each time (the eye walks off) */
    for (int i = 0; i < 40; ++i) x[i] = (float)(i/2)*.06f+((i & 1) ? .2f : 0);
    CHECK(made(x, 40, dt, false).frames == 0);
    /* three places in turn */
    for (int i = 0; i < 40; ++i) x[i] = (float)(i%3)*.2f;
    CHECK(made(x, 40, dt, false).frames == 0);
    /* it stops: counted for 70 ms after the last move and no longer */
    for (int i = 0; i < 40; ++i) x[i] = i < 10 && (i & 1) ? .2f : 0;
    s = made(x, 40, dt, false);
    CHECK(s.first == 3 && s.frames == 8+(int)floorf(70/dt)); /* frames 3 to 10 (frame 10 is the last move back), then those within 70 ms */
  }
  /* A flip every 33.3 ms seen at 8.33 ms a frame (the rig set at a lower rate than the view): still a going back and
   * forth, the moves 33.3 ms apart. A flip every 100 ms is not: each place lasts longer than the hold. */
  {
    float x[160];
    for (int i = 0; i < 160; ++i) x[i] = ((i/4) & 1) ? .2f : 0;
    const Seen s = made(x, 160, 8.33f, false);
    CHECK(s.first == 12 && s.frames == 160-12);
    for (int i = 0; i < 160; ++i) x[i] = ((i/12) & 1) ? .2f : 0;
    CHECK(made(x, 160, 8.33f, false).frames == 0);
  }
  /* What starts it again: another camera, a pause longer than the gap, time going backwards, a value that is not
   * finite. The same instant again changes nothing. */
  {
    TfpAlternation a; memset(&a, 0, sizeof a); float move = 0; double tick = 1000; bool on = false;
    const float there[3] = {.2f, 1.5f, .1f}, here[3] = {0, 1.5f, .1f}, bad[3] = {NAN, 0, 0};
    for (int i = 0; i < 6; ++i, tick += 33.3) on = tfp_alternation_step(&a, (i & 1) ? there : here, 2, false, tick, 250, &move);
    CHECK(on && a.returns == 4);
    CHECK(tfp_alternation_step(&a, here, 2, false, tick-50, 250, &move) == false && a.returns == 0 && a.spots == 1); /* time going backwards */
    for (int i = 0; i < 6; ++i, tick += 33.3) on = tfp_alternation_step(&a, (i & 1) ? there : here, 2, false, tick, 250, &move);
    CHECK(on);
    const TfpAlternation before = a;
    CHECK(tfp_alternation_step(&a, here, 2, false, a.tick, 250, &move) && move == 0 && memcmp(&a, &before, sizeof a) == 0); /* the same instant again */
    CHECK(!tfp_alternation_step(&a, here, 1, false, tick, 250, &move) && a.returns == 0);        /* another camera */
    for (int i = 0; i < 6; ++i) { tick += 33.3; on = tfp_alternation_step(&a, (i & 1) ? there : here, 1, false, tick, 250, &move); }
    CHECK(on);
    CHECK(!tfp_alternation_step(&a, there, 1, false, tick+251, 250, &move) && a.returns == 0);   /* a pause */
    tick += 251;
    for (int i = 0; i < 6; ++i) { tick += 33.3; on = tfp_alternation_step(&a, (i & 1) ? there : here, 1, false, tick, 250, &move); }
    CHECK(on);
    CHECK(!tfp_alternation_step(&a, bad, 1, false, tick+33.3, 250, &move) && !a.known);          /* not finite */
  }
  printf("camera alternation: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
