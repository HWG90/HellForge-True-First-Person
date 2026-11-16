#include "correction.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool same(const float *a, const float *b) { return !memcmp(a, b, 12); }
static void approximately(float a, float b) { assert(fabsf(a - b) < .000003f); }
static int compare(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}
int main(void) {
  FpCorrection s;
  fp_correction_reset(&s);
  float native[3] = {.12f, -.1f, 1.62f}, desired[3] = {0, .08f, 1.7f}, out[3];
  assert(fp_correction_apply(&s, desired, 1, 1000, out, 150, 250) &&
         same(out, desired));
  assert(fp_correction_remember(&s, native, 1, 1000));
  assert(fp_correction_apply(&s, desired, 1, 1001, out, 150, 250) &&
         same(out, native));
  assert(fp_correction_apply(&s, desired, 1, 1076, out, 500, 250));
  for (unsigned i = 0; i < 3; i++)
    approximately(out[i], native[i] + (desired[i] - native[i]) * .5f);
  assert(fp_correction_apply(&s, desired, 1, 1151, out, 500, 250) &&
         same(out, desired) && !s.active);
  const unsigned rates[] = {30, 60, 144, 240};
  float samples[4][9];
  for (unsigned r = 0; r < 4; r++) {
    double times[260];
    unsigned n = 0;
    times[n++] = 0;
    times[n++] = 150;
    for (unsigned f = 1; f < rates[r]; f++) {
      double t = f * 1000.0 / rates[r];
      if (t < 150)
        times[n++] = t;
    }
    double points[] = {37.125, 75.125, 112.125};
    for (unsigned i = 0; i < 3; i++)
      times[n++] = points[i];
    qsort(times, n, sizeof *times, compare);
    fp_correction_reset(&s);
    assert(fp_correction_remember(&s, native, 7, 2000.125));
    for (unsigned i = 0; i < n; i++) {
      assert(fp_correction_apply(&s, desired, 7, 2000.125 + times[i], out, 150,
                                 250));
      for (unsigned j = 0; j < 3; j++) {
        assert(isfinite(out[j]));
        assert(out[j] >= fminf(native[j], desired[j]) - .000001f &&
               out[j] <= fmaxf(native[j], desired[j]) + .000001f);
      }
      for (unsigned j = 0; j < 3; j++)
        if (times[i] == points[j])
          memcpy(samples[r] + j * 3, out, 12);
    }
    assert(same(out, desired) && !s.active);
  }
  for (unsigned r = 1; r < 4; r++)
    for (unsigned i = 0; i < 9; i++)
      approximately(samples[0][i], samples[r][i]);
  fp_correction_reset(&s);
  assert(fp_correction_remember(&s, native, 1, 3000));
  assert(fp_correction_apply(&s, desired, 1, 3000, out, 150, 250));
  assert(fp_correction_apply(&s, desired, 1, 3050, out, 150, 250));
  float held[3];
  memcpy(held, out, 12);
  for (unsigned i = 0; i < 100; i++)
    assert(fp_correction_apply(&s, desired, 1, 3050, out, 150, 250) &&
           same(out, held));
  for (unsigned mode = 0; mode < 3; mode++) {
    fp_correction_reset(&s);
    assert(fp_correction_remember(&s, native, 1, 4000));
    assert(fp_correction_apply(&s, desired, 1, 4000, out, 150, 250));
    assert(fp_correction_apply(&s, desired, mode == 2 ? 2 : 1,
                               mode == 0   ? 3999
                               : mode == 1 ? 4251
                                           : 4010,
                               out, 150, 250) &&
           same(out, desired) && !s.active);
  }
  fp_correction_reset(&s);
  assert(fp_correction_remember(&s, native, 1, 5000));
  float teleport[3] = {2, 0, 1.7f};
  assert(fp_correction_apply(&s, teleport, 1, 5001, out, 150, 250) &&
         same(out, teleport) && !s.active);
  float bad[3] = {NAN, 0, 0};
  assert(!fp_correction_remember(&s, bad, 1, 6000));
  assert(!fp_correction_remember(&s, native, 0, 6000));
  assert(!fp_correction_remember(&s, native, 1, NAN));
  assert(fp_correction_remember(&s, native, 1, 6000));
  assert(!fp_correction_apply(&s, desired, 1, 6001, out, 501, 250) &&
         same(out, desired));
  assert(fp_correction_remember(&s, native, 1, 6002));
  assert(!fp_correction_apply(&s, desired, 1, 6003, out, 150, 15) &&
         same(out, desired));
  assert(fp_correction_remember(&s, native, 1, 6004));
  assert(fp_correction_apply(&s, desired, 1, 6005, out, 0, 250) &&
         same(out, desired) && !s.active);
  puts("C camera correction: finite position-only handoff, exact endpoints, "
       "fractional 30/60/144/240Hz cadence, immutable duration, repeat "
       "timestamps, weapon/clock/hitch retirement and invalid-contract refusal "
       "passed");
  return 0;
}
