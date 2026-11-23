/* The glide of the head camera's eye to the incoming weapon's eye at a swap (src/camera_glide.h). */
#include "camera_glide.h"
#include <stdio.h>
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

int main(void) {
  retail();
  printf("camera glide: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
