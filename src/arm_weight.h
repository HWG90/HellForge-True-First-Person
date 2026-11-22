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
/* Whether the player is aiming, for the weight, each tick. The witnesses in order:
 *   - a motion runtime in phase ACTIVE (a pose lease runs and this tick's sample was taken): its action is AIM or ADS;
 *   - else the controller is holding the lease through an action: not aiming (the body's arms show through it);
 *   - else (no running lease while the game shows its own first-person aim: a hold ended by full aim or by its cap,
 *     the view switched on in the game's aim view; or a tick whose sample was refused) the player's own aim flags,
 *     the ones src/aim_markers.c uses, when they could be read this tick;
 *   - else what the previous tick answered. Never plain "not aiming": that showed the body's arms over the game's
 *     aim pose (the rifle sunk), for the whole aim or as a twitch. */
static inline bool tfp_arm_aiming(bool active, bool runtime_aim, bool holding, bool flags_read, bool flags_aim, bool previous) {
  if (active) return runtime_aim;
  if (holding) return false;
  return flags_read ? flags_aim : previous;
}
#endif
