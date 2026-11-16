/* One presentation clock for camera displacement, native rig additions and
 * lens. Translation of retail src/first_person/transition.lua at
 * 0.5.41-smooth.66; tests/vectors/transition_vectors.h holds that file's own
 * answers. Pure scalar state. */
#ifndef TFP_TRANSITION_H
#define TFP_TRANSITION_H
#include <stdbool.h>
#include <stdint.h>

typedef enum TfpTransitionReason {
  TFP_REASON_NONE, TFP_REASON_INITIAL, TFP_REASON_WEAPON, TFP_REASON_ADS_ENTER, TFP_REASON_ADS_LEAVE,
  TFP_REASON_ZOOM, TFP_REASON_SHOULDER, TFP_REASON_ACTION, TFP_REASON_SEAT, TFP_REASON_CAMERA,
  TFP_REASON_REACQUIRE
} TfpTransitionReason;
#define TFP_TRANSITION_NONE (-1) /* "no action" / "no zoom identity" (nil in retail) */

/* What the clock looks at in a snapshot. The two identities are retail's
 * player_identity and identity strings reduced to keys: equal keys, equal strings. */
typedef struct TfpTransitionInput {
  uint64_t player_identity, identity;
  bool controller_active, seated, shoulder_aim;
  uint32_t camera_kind;
  int action, zoom_identity;
} TfpTransitionInput;
typedef struct TfpTransitionPacket {
  uint64_t epoch;
  double progress, started, duration;
  bool ads, continuous;
  TfpTransitionReason reason, merged;
  uint64_t identity, player_identity;
} TfpTransitionPacket;
typedef struct TfpTransition {
  double duration, max_gap, max_step, merge_until;
  uint64_t epoch;
  bool has_packet, has_previous;
  TfpTransitionPacket packet;
  struct { uint64_t identity, player_identity; bool ads, shoulder_aim, seated; uint32_t camera_kind;
           int action, zoom_identity; double time; } previous;
} TfpTransition;

void tfp_transition_init(TfpTransition *t); /* duration 0.18, max gap 0.25, step a third, merge below 0.5 */
/* input NULL: snapshot unavailable. true and *out filled when a packet exists. */
bool tfp_transition_update(TfpTransition *t, const TfpTransitionInput *input, double now, bool enabled,
                           bool blocked, TfpTransitionPacket *out);
bool tfp_transition_action(TfpTransition *t, int action, double now, TfpTransitionPacket *out);
bool tfp_transition_resume(TfpTransition *t, uint64_t identity, uint64_t player_identity, double now,
                           TfpTransitionPacket *out);
bool tfp_transition_result(const TfpTransition *t, TfpTransitionPacket *out);
void tfp_transition_reset(TfpTransition *t);
/* The native camera's positive firearm-ADS classification (transition.lua:9). */
bool tfp_transition_native_ads(bool controller_active, bool seated, const uint8_t controller_bytes[6]);
#endif
