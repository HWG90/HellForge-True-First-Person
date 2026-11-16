#ifndef TFP_NATIVE_OBSERVER_H
#define TFP_NATIVE_OBSERVER_H
#include "backend.h"
#include "scene.h"

enum FpNativeObservedPart {
  FP_OBSERVE_CORE = 1u,
  FP_OBSERVE_CAMERA = 2u,
  FP_OBSERVE_BODY = 4u,
  FP_OBSERVE_RIG = 8u,
  FP_OBSERVE_LENS = 16u,
  FP_OBSERVE_WEAPON = 32u,
  FP_OBSERVE_ALL = 63u
};
typedef enum FpNativeObservationFailure {
  FP_OBSERVE_OK,
  FP_OBSERVE_BAD_ARGUMENT,
  FP_OBSERVE_WRONG_BUILD,
  FP_OBSERVE_OWNER_UNAVAILABLE,
  FP_OBSERVE_REQUEST_CHANGED,
  FP_OBSERVE_GRAPH_CHANGED,
  FP_OBSERVE_FIELDS_CHANGED
} FpNativeObservationFailure;

typedef struct FpNativeLensObservation {
  float near_range, far_range;
  uint32_t projection_type;
  float vertical_fov, frustum[4];
  uint32_t frustum_mode;
} FpNativeLensObservation;

typedef struct FpNativeObservation {
  uint32_t available, unavailable;
  FpNativeObservationFailure failure;
  /* Includes exact head/tail, selected request, camera UnitRef, controller
   * bytes and avatar flags. Numeric UnitRefs never become SDK handles. */
  FpSnapshot owner;
  uint32_t request_words[5];
  FpGraph camera_graph, camera_parent_graph, rig_graph;
  uint32_t camera_node, camera_link, camera_render_id;
  uintptr_t camera_parent, camera_parent_worlds;
  float camera_world[16];
  FpBodySample body_sample;
  float body_root[3], body_head[3], body_basis[9];
  /* Array rows correspond to known FPS nodes 0, 5 and 50, in that order. */
  float rig_world[3][16];
  float rig_hands[2][16];          /* native nodes13 and34 */
  float rig_weapon_attachment[16]; /* native socket node8 */
  FpNativeLensObservation lens;
  /* Optional current wielded weapon descriptor/Unit. The world matrix is
   * weapon graph node0. External link>>16 names the attachment parent node. */
  FpSnapshot weapon_owner;
  FpGraph weapon_graph, weapon_parent_graph;
  uint32_t weapon_link;
  uintptr_t weapon_parent, weapon_parent_worlds;
  float weapon_world[16], weapon_parent_world[16];
} FpNativeObservation;

/* A stateless READ-ONLY copy for the owned game thread. True means its core
 * owner/request remained stable and every available component passed closing
 * proofs; unavailable components stay zero and are explicitly flagged. False
 * discards all samples and sets failure. This does not establish that world
 * matrices and lens values belong to one native evaluation or render frame.
 * Sampling cadence, files and any user coordination belong to the caller. */
bool fp_native_observe(const FpMemory *, FpNativeObservation *);
#endif
