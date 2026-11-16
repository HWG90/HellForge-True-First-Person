#ifndef TFP_SCENE_H
#define TFP_SCENE_H
#include "backend.h"
#include "presentation.h"
typedef struct FpGraph {
  uintptr_t object, world, hashes, links, locals, worlds;
  uint32_t ref, count;
} FpGraph;
typedef struct FpBodySample {
  FpGraph graph;
  uint32_t head, head_link;
} FpBodySample;
typedef struct FpCameraSpace {
  FpQuat rotation;
  float position[3];
  uintptr_t graph, links, parent, worlds;
  uint32_t index, link, parent_ref, parent_node;
} FpCameraSpace;
bool fp_graph(const FpMemory *, uint32_t, FpGraph *);
bool fp_graph_rig(const FpMemory *, uint32_t, FpGraph *);
bool fp_body_points(const FpMemory *, uint32_t, float root[3], float head[3],
                    float basis[9]);
bool fp_body_sample(const FpMemory *, uint32_t, float root[3], float head[3],
                    float basis[9], FpBodySample *);
bool fp_body_sample_current(const FpMemory *, const FpBodySample *);
bool fp_camera_space(const FpMemory *, const FpSnapshot *, FpCameraSpace *);
bool fp_camera_space_same(const FpCameraSpace *, const FpCameraSpace *);
bool fp_camera_native_parent(const FpMemory *, const FpSnapshot *,
                             FpCameraSpace *);
/* Current native camera50 / gun8 attachment readiness, with closing owner and
 * graph checks. No engine calls or pose writes; not same-evaluation proof.
 * Accepts local kind4/6 snapshots but never treats pending links as ready.
 * rig_out is required and is cleared on failure. */
bool fp_rig_attachments(const FpMemory *, const FpSnapshot *, FpGraph *rig_out);
#endif
