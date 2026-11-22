/* Beyond retail and beyond the retired controller's per-action machine: how much of the body's arm animation the arm
 * drive shows. While the player aims the game's own first-person aim pose is the right one (seen in the game
 * 2026-11-23: with the body's arms copied, the rifle sank to the bottom of the view instead of rising to the
 * shoulder; the retired controller released its transfer on aim for the same reason). The weight moves linearly,
 * 120 ms from end to end, so nothing steps. */
#ifndef TFP_ARM_WEIGHT_H
#define TFP_ARM_WEIGHT_H
#include <math.h>
#include <stdbool.h>
#define TFP_ARM_WEIGHT_MS 120.0f
static inline float tfp_arm_weight_step(float weight, bool aiming, float dt_ms) {
  const float target = aiming ? 0.0f : 1.0f;
  if (!(weight >= 0)) weight = isnan(weight) ? target : 0;   /* a NaN starts from the target */
  if (weight > 1) weight = 1;
  if (!(dt_ms > 0)) return weight;
  const float step = dt_ms/TFP_ARM_WEIGHT_MS;
  if (weight < target) { weight += step; if (weight > target) weight = target; }
  else if (weight > target) { weight -= step; if (weight < target) weight = target; }
  return weight;
}
#endif
