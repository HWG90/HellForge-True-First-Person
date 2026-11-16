#ifndef TFP_POSE_POLICY_H
#define TFP_POSE_POLICY_H
#include "camera_policy.h"
#include "depth.h"
#include "placement.h"
#include "scene.h"
typedef enum FpPoseDepthPhase {
  FP_POSE_DEPTH_EMPTY,
  FP_POSE_DEPTH_LIVE,
  FP_POSE_DEPTH_RECOVER,
  FP_POSE_DEPTH_RELEASE
} FpPoseDepthPhase;
/* Scalar source-availability response only; never an animation/Unit pose. */
typedef struct FpPoseDepthResponse {
  float source, value, bias_start;
  uint64_t sample_at, start;
  FpPoseDepthPhase phase;
  bool sampled;
} FpPoseDepthResponse;
typedef struct FpPoseCarry {
  FpSnapshot owner;
  FpGraph rig;
  uintptr_t weapon_object;
  uint64_t applied_at, depth_at;
  float weight, ads_weight, hip_depth;
  bool valid, depth_ready;
  FpPoseDepthResponse depth_response;
} FpPoseCarry;
typedef struct FpPosePolicy {
  SRWLOCK lock;
  FpMemory memory;
  FpSnapshot origin;
  FpSlotPlatform platform;
  FpDispatchLease lease;
  FpCameraPolicy *camera;
  uint64_t token, start;
  uintptr_t request, object, weapon_object;
  FpPlacement placement, ads;
  float weight, target, start_weight, ads_weight, start_ads_weight;
  float hip_depth;
  uint64_t applied_at, depth_at;
  bool transition_ready, depth_ready, applied_ready;
  FpSnapshot applied_owner;
  FpGraph applied_rig;
  FpPoseCarry carry;
  FpPoseDepthResponse depth_response;
  float baseline[4], rotation_baseline[4], motion[6];
  FpQuat view;
  bool shifted, rotated;
  uint64_t positions, rotations, restored;
  FpDepth depth;
} FpPosePolicy;
void fp_pose_policy_init(FpPosePolicy *);
bool fp_pose_policy_open(FpPosePolicy *, const FpMemory *, const FpSnapshot *,
                         FpCameraPolicy *, char *, size_t);
bool fp_pose_policy_close(FpPosePolicy *);
/* Request-only retirement: no retired pose is restored. Retains only the
 * last applied scalar/depth for <=500 ms, requalified on the next open.
 * Disable, owner/weapon change, actions, vehicles and foreign slots use close.
 */
bool fp_pose_policy_handoff(FpPosePolicy *);
bool fp_pose_policy_place(FpPosePolicy *, uint64_t, const FpPlacement *,
                          const FpPlacement *);
#endif
