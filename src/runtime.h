#ifndef TFP_RUNTIME_H
#define TFP_RUNTIME_H
#include "action.h"
#include "blur_policy.h"
#include "effects.h"
#include "fov_policy.h"
#include "lifecycle.h"
#include "markers.h"
#include "models.h"
#include "near_policy.h"
#include "pose_policy.h"
#include "preferences.h"
#include "settings.h"
#include "visibility_policy.h"
typedef struct FpRuntime {
  FpMemory memory;
  FpSettings settings;
  FpNativeLease native;
  FpCameraPolicy camera;
  FpPosePolicy pose;
  FpFovPolicy fov;
  FpNearPolicy clip;
  FpBlurPolicy blur;
  FpVisibilityPolicy visibility;
  FpByteOwner aim, movement;
  FpMarkerLease markers;
  FpActionReader actions;
  FpEffects effects;
  FpModels models;
  bool enabled, stopping, warming, ready, editor, blur_absent;
  uint64_t sequence, look_sequence, next_entry, next_blur, compare_until;
  uint64_t weapon_key, editor_weapon;
  FpSnapshot snapshot, editor_origin;
  FpActions action;
  FpAction editor_action;
  FpPlacement draft, current_placement;
  bool placement_ready;
  char status[256], issue[160];
} FpRuntime;
void fp_runtime_init(FpRuntime *, const FpMemory *, const FpSettings *);
void fp_runtime_enable(FpRuntime *, bool);
bool fp_runtime_clean(const FpRuntime *);
void fp_runtime_tick(FpRuntime *, double dt, uint64_t now, bool foreign,
                     bool look_allowed, bool backward);
bool fp_runtime_editor(FpRuntime *, bool);
bool fp_runtime_draft(FpRuntime *, FpAction, const FpPlacement *);
bool fp_runtime_free_aim(FpRuntime *);
bool fp_runtime_compare(FpRuntime *, uint64_t now);
#endif
