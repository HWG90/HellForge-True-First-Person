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
  /* Who says "aiming" (tfp_arm_aiming). A motion runtime in phase ACTIVE: its action, whatever the flags say. */
  CHECK(tfp_arm_aiming(true, true, false, true, false, false) && !tfp_arm_aiming(true, false, false, true, true, true));
  CHECK(tfp_arm_aiming(true, true, true, false, false, false)); /* (active wins over holding: the runtime is the nearer witness) */
  /* Not ACTIVE and the controller holding through an action: the body's arms show, not aiming. */
  CHECK(!tfp_arm_aiming(false, true, true, true, true, true));
  /* Not ACTIVE, not holding (no running lease in the game's own aim view, or a tick whose sample was refused): the
   * player's own aim flags, never plain "not aiming". */
  CHECK(tfp_arm_aiming(false, false, false, true, true, false) && !tfp_arm_aiming(false, false, false, true, false, true));
  /* The flags cannot be read this tick: the last answer stands. */
  CHECK(tfp_arm_aiming(false, false, false, false, false, true) && !tfp_arm_aiming(false, false, false, false, true, false));
  { /* the two cases of the finding, tick by tick at 60 fps */
    float weight = 0; bool aiming = true;
    /* (a) aim held, the lease gone (full aim, the 4 s cap, F11 pressed in the game's aim view): the copy stays hidden */
    for (int i = 0; i < 120; ++i) { aiming = tfp_arm_aiming(false, false, false, true, true, aiming); weight = tfp_arm_weight_step(weight, aiming, 16); }
    CHECK(weight == 0);
    /* (b) aim held with a running lease; one tick's sample is refused, with and without readable flags: no twitch */
    for (int i = 0; i < 60; ++i) {
      const bool refused = i == 20 || i == 40;
      aiming = tfp_arm_aiming(!refused, true, false, i != 40, true, aiming); weight = tfp_arm_weight_step(weight, aiming, 16);
      CHECK(weight == 0);
    }
    /* released: the copy comes back in 120 ms */
    for (int i = 0; i < 8; ++i) { aiming = tfp_arm_aiming(false, false, false, true, false, aiming); weight = tfp_arm_weight_step(weight, aiming, 16); }
    CHECK(weight == 1);
  }
  printf("arm weight: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
