#ifndef TFP_CAMERA_POLICY_H
#define TFP_CAMERA_POLICY_H
#define WIN32_LEAN_AND_MEAN
#include "action.h"
#include "backend.h"
#include "correction.h"
#include "dispatch_lease.h"
#include "motion.h"
#include "presentation.h"
#include "slot_platform.h"
#include <stdatomic.h>
#include <windows.h>
typedef struct FpEyeOptions {
  float right, forward, up;
  bool kick;
  FpMotionOptions motion;
} FpEyeOptions;
typedef struct FpCameraPolicy {
  SRWLOCK lock;
  FpMemory memory;
  FpSnapshot origin;
  FpSlotPlatform platform;
  FpDispatchLease lease;
  uint64_t token;
  uintptr_t slot, body_object, camera_object;
  FpMotion motion;
  FpQuat view;
  FpSnapshot history_weapon;
  uintptr_t history_gun, history_rig;
  bool history_valid;
  FpCorrection correction;
  FpActionReader action_reader;
  FpEyeOptions options;
  uint64_t sequence, sampled, free_until;
  double previous, motion_previous, ticks_per_ms, native_at;
  float relative_head[3], blend;
  bool tracking, native_seen;
  bool closing, opening, sprint;
  uint64_t adjusted, forwarded, native_forwarded;
  bool frame_valid;
  atomic_bool shared_valid, fallback_active;
  FpMotionOutput last_motion;
  atomic_uint shared_sequence;
  _Atomic float shared[10];
} FpCameraPolicy;
void fp_camera_policy_init(FpCameraPolicy *);
bool fp_camera_policy_open(FpCameraPolicy *, const FpMemory *,
                           const FpSnapshot *, char *, size_t);
bool fp_camera_policy_close(FpCameraPolicy *);
bool fp_camera_policy_options(FpCameraPolicy *, const FpEyeOptions *);
void fp_camera_policy_frame(FpCameraPolicy *, uint64_t sequence, bool opening);
void fp_camera_policy_actions(FpCameraPolicy *, bool sprint);
bool fp_camera_policy_free_aim(FpCameraPolicy *, uint32_t milliseconds);
bool fp_camera_policy_read_frame(const FpCameraPolicy *, float motion[6],
                                 FpQuat *view);
bool fp_camera_policy_fallback_ready(const FpCameraPolicy *);
#endif
