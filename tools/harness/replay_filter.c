/* The continuity filter alone (src/camera_continuity.h) over frames read from "Trace:" lines: tools/harness/replay.py
 * builds and runs this, once against the header of the build that wrote the log and once against the present one.
 *
 *   replay_filter fit|run < rows
 *
 * A row: tick(ms) source detect quiet fresh  wanted x y z  root x y z  logged eye x y z  root axes (9, rows; "run" only)
 *        quiet: full aim (the filter takes nothing up); fresh: 1 the filter starts from nothing in this row (the helper
 *        forgot it), 2 the log carries no correction in this row and the filter's was not known: it is let go before
 *        the row, and the rest of the state (the eye's speed, the root's course) is kept
 * Out:   presented eye x y z  correction  taken(bits: 1 jump, 2 edge, 4 root, 8 restart)  root axes (9)  misfit(m)
 *
 * The Trace lines of 0.6.0-feel.39 and before do not say how the root's axes stood. "fit" looks for them: in each
 * frame the turn of the axes from the frame before (any axis) with which this filter presents the eye the log
 * shows, the smallest such turn, or the turn of the frame before again (a root that goes on turning). Where nothing
 * is carried and nothing taken up every turn fits, and none is found: the axes stay as they were. "run" takes the
 * axes as given. A pause of more than 250 ms (the helper's frame gap) starts the filter again, as in the game. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "camera_continuity.h"

typedef struct Row { double tick; unsigned source; int detect, quiet, fresh; float wanted[3], root[3], logged[3]; } Row;
static const TfpContinuityTuning tuning = TFP_CONTINUITY_DEFAULT;

static float present(TfpContinuity *c, const Row *row, const float *basis, float *shown) {
  float relative[3] = {0}, out[3];
  for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) relative[a] += (row->wanted[k]-row->root[k])*basis[a*3+k];
  const float carried = tfp_continuity_step_in(c, relative, row->wanted, basis, row->source, row->detect != 0, row->quiet != 0, row->tick, 250, &tuning, out);
  for (int k = 0; k < 3; ++k) { shown[k] = row->root[k]; for (int a = 0; a < 3; ++a) shown[k] += out[a]*basis[a*3+k]; }
  return carried;
}
static void turned(const float *angle, const float *from, float *basis) {
  float turn[9];
  tfp_continuity_turned(angle, turn);
  for (int i = 0; i < 9; ++i) basis[i] = 0;
  for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) for (int k = 0; k < 3; ++k) basis[a*3+k] += turn[a*3+b]*from[b*3+k];
}
/* How far the eye presented with the axes turned by `angle` is from the logged one; a little is added for the turn's
 * distance from `prior`, so that of the turns that fit equally the nearest is kept. */
static float misfit(const TfpContinuity *c, const Row *row, const float *from, const float *angle, const float *prior, float *apart) {
  TfpContinuity copy = *c; float basis[9], shown[3], d = 0, far = 0;
  turned(angle, from, basis);
  present(&copy, row, basis, shown);
  for (int k = 0; k < 3; ++k) { d += (shown[k]-row->logged[k])*(shown[k]-row->logged[k]); far += (angle[k]-prior[k])*(angle[k]-prior[k]); }
  if (apart) *apart = sqrtf(d);
  return sqrtf(d)+.002f*sqrtf(far);
}
static float search(const TfpContinuity *c, const Row *row, const float *from, const float *prior, float *angle) {
  float best = misfit(c, row, from, angle, prior, NULL);
  for (float size = .32f; size > .0004f; size /= 2) {
    bool better = true;
    for (int round = 0; better && round < 40; ++round) { better = false;
      for (int axis = 0; axis < 3; ++axis) for (int way = -1; way <= 1; way += 2) {
        float next[3] = {angle[0], angle[1], angle[2]}; next[axis] += way*size;
        const float value = misfit(c, row, from, next, prior, NULL);
        if (value < best-1e-6f) { best = value; memcpy(angle, next, sizeof next); better = true; }
      }
    }
  }
  return best;
}

/* The fit of one stretch of frames without a pause: a few courses of the axes are followed side by side (each frame
 * can fit more than one turn, and which was the right one shows frames later); the one that is nearest the log over
 * the whole stretch is kept. */
enum { BEAM = 6, STARTS = 27, CHILDREN = 3 };
typedef struct Course { TfpContinuity c; float basis[9], last[3], cost; float *axes; } Course;
typedef struct Child { float angle[3], value, apart; int parent; } Child;
static void fit_stretch(const TfpContinuity *begin, const Row *rows, int count, float *axes) {
  Course *beam = calloc(BEAM, sizeof *beam), *next = calloc(BEAM, sizeof *next); int alive = 1;
  for (int i = 0; i < BEAM; ++i) { beam[i].axes = calloc((size_t)count*9, sizeof(float)); next[i].axes = calloc((size_t)count*9, sizeof(float)); }
  beam[0].c = *begin; for (int i = 0; i < 9; ++i) beam[0].basis[i] = i%4 ? 0.0f : 1.0f;
  for (int n = 0; n < count; ++n) {
    Child children[BEAM*CHILDREN]; int made = 0;
    for (int e = 0; e < alive; ++e) {
      Child found[STARTS]; int have = 0; const float none[3] = {0, 0, 0};
      for (int s = 0; s < STARTS; ++s) {
        float angle[3] = {0, 0, 0}; const float *prior = none;
        if (s == 1) { memcpy(angle, beam[e].last, sizeof angle); prior = beam[e].last; }
        if (s >= 2) angle[2] = (s-14)*.05f;
        Child child; child.parent = e;
        search(&beam[e].c, &rows[n], beam[e].basis, prior, angle);
        memcpy(child.angle, angle, sizeof angle);
        child.value = beam[e].cost+misfit(&beam[e].c, &rows[n], beam[e].basis, angle, s == 1 ? beam[e].last : none, &child.apart);
        bool same = false;
        for (int k = 0; k < have && !same; ++k) { float d = 0; for (int i = 0; i < 3; ++i) d += (found[k].angle[i]-angle[i])*(found[k].angle[i]-angle[i]);
          if (d < .03f*.03f) { same = true; if (child.value < found[k].value) found[k] = child; } }
        if (!same) found[have++] = child;
      }
      for (int pick = 0; pick < CHILDREN && have > 0; ++pick) { int best = 0;
        for (int k = 1; k < have; ++k) if (found[k].value < found[best].value) best = k;
        children[made++] = found[best]; found[best] = found[--have]; }
    }
    int kept = 0;
    for (; kept < BEAM && made > 0; ++kept) { int best = 0;
      for (int k = 1; k < made; ++k) if (children[k].value < children[best].value) best = k;
      const Child child = children[best]; children[best] = children[--made];
      const Course *from = &beam[child.parent]; Course *to = &next[kept]; float shown[3], *const keep = to->axes;
      *to = *from; to->axes = keep; memcpy(to->axes, from->axes, (size_t)n*9*sizeof(float));
      turned(child.angle, from->basis, to->basis); memcpy(to->last, child.angle, sizeof to->last); to->cost = child.value;
      present(&to->c, &rows[n], to->basis, shown);
      memcpy(to->axes+(size_t)n*9, to->basis, sizeof to->basis);
    }
    Course *swap = beam; beam = next; next = swap; alive = kept;
  }
  memcpy(axes, beam[0].axes, (size_t)count*9*sizeof(float));
  for (int i = 0; i < BEAM; ++i) { free(beam[i].axes); free(next[i].axes); }
  free(beam); free(next);
}

int main(int argc, char **argv) {
  const bool fit = argc > 1 && !strcmp(argv[1], "fit");
  int count = 0, room = 1024; Row *rows = malloc((size_t)room*sizeof *rows); float *axes = malloc((size_t)room*9*sizeof *axes);
  for (;;) {
    Row row;
    if (scanf("%lf %u %d %d %d %f %f %f %f %f %f %f %f %f", &row.tick, &row.source, &row.detect, &row.quiet, &row.fresh, row.wanted, row.wanted+1,
              row.wanted+2, row.root, row.root+1, row.root+2, row.logged, row.logged+1, row.logged+2) != 14) break;
    if (count == room) { room *= 2; rows = realloc(rows, (size_t)room*sizeof *rows); axes = realloc(axes, (size_t)room*9*sizeof *axes); }
    for (int i = 0; i < 9; ++i) if (scanf("%f", &axes[count*9+i]) != 1) return 1;
    rows[count++] = row;
  }
  TfpContinuity c; memset(&c, 0, sizeof c);
  for (int from = 0; from < count;) {
    int to = from+1;
    while (to < count && rows[to].tick-rows[to-1].tick <= 250 && rows[to].fresh != 1) ++to;
    if (rows[from].fresh == 1) memset(&c, 0, sizeof c);
    if (fit) fit_stretch(&c, rows+from, to-from, axes+from*9);
    for (int n = from; n < to; ++n) {
      const uint64_t jumps = c.jumps, edges = c.edges, reseats = c.reseats, restarts = c.restarts; float shown[3], apart = 0;
      if (rows[n].fresh == 2) { c.active = false; c.run = 0; }
      const float carried = present(&c, &rows[n], axes+n*9, shown);
      for (int k = 0; k < 3; ++k) apart += (shown[k]-rows[n].logged[k])*(shown[k]-rows[n].logged[k]);
      printf("%.4f %.4f %.4f %.4f %u", (double)shown[0], (double)shown[1], (double)shown[2], (double)carried,
             (c.jumps != jumps ? 1u : 0u) | (c.edges != edges ? 2u : 0u) | (c.reseats != reseats ? 4u : 0u) | (c.restarts != restarts ? 8u : 0u));
      for (int i = 0; i < 9; ++i) printf(" %.6f", (double)axes[n*9+i]);
      printf(" %.4f\n", (double)sqrtf(apart));
    }
    from = to;
  }
  free(rows); free(axes);
  return 0;
}
