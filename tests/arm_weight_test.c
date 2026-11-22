/* The arm drive's weight while aiming (src/arm_weight.h): the copy of the body's arms gives way to the game's own aim pose. */
#include "arm_weight.h"
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
int main(void) {
  float w = 1;
  /* Not aiming: stays 1. */
  for (int i = 0; i < 10; ++i) w = tfp_arm_weight_step(w, false, 16);
  CHECK(w == 1);
  /* Aiming: falls to 0 in about 120 ms, never below, never a step larger than the time allows. */
  float last = w; int frames = 0;
  while (w > 0 && frames < 100) { w = tfp_arm_weight_step(w, true, 16); CHECK(w <= last && last - w <= 16.0f/120.0f + 1e-6f); last = w; ++frames; }
  CHECK(w == 0 && frames >= 7 && frames <= 9);
  /* Released: back to 1 the same way. */
  frames = 0;
  while (w < 1 && frames < 100) { w = tfp_arm_weight_step(w, false, 16); ++frames; }
  CHECK(w == 1 && frames >= 7 && frames <= 9);
  /* A long gap (a hitch, the first tick) does not overshoot; a bad value starts from the target. */
  CHECK(tfp_arm_weight_step(1, true, 5000) == 0 && tfp_arm_weight_step(0, false, 5000) == 1);
  CHECK(tfp_arm_weight_step(0.0f/0.0f, true, 16) == 0 && tfp_arm_weight_step(-3, false, 0) == 0 && tfp_arm_weight_step(7, true, 0) == 1);
  printf("arm weight: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
