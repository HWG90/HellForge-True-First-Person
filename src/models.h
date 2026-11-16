#ifndef TFP_MODELS_H
#define TFP_MODELS_H
#include "backend.h"
#include "engine.h"
#define FP_MODEL_TARGETS 80
#define FP_MODEL_MESHES 512
#define FP_MODEL_UNIT_MESHES 128
#define FP_MODEL_REPAIRS 33
typedef struct FpModelTarget {
  uint32_t ref;
  uintptr_t object, parent, world;
  bool hide, shadow, rig, held_weapon;
  EngRef handle;
} FpModelTarget;
typedef struct FpMeshLease {
  uint32_t ref, index;
  uintptr_t object, parent, world, array, mesh;
  EngRef handle;
  uint8_t bits, before, written;
  bool wanted, rig_pad;
} FpMeshLease;
/* A native-cache redraw is not a temporary default-pass property lease. Copy
 * every cached byte before the first mesh setter can update that cache. */
typedef struct FpDefaultRepair {
  FpSnapshot owner;
  uint32_t ref, count;
  uintptr_t object, parent, world, array, cache, body_object, camera_object;
  uintptr_t meshes[FP_MODEL_UNIT_MESHES];
  uint8_t cached[FP_MODEL_UNIT_MESHES], pending[FP_MODEL_UNIT_MESHES];
  bool active, started;
} FpDefaultRepair;
typedef struct FpModels {
  FpSnapshot origin;
  uint64_t generation, next_scan;
  EngRef world, inventory;
  int scan, target_count, count;
  uint64_t signature;
  FpModelTarget targets[FP_MODEL_TARGETS];
  FpMeshLease rows[FP_MODEL_MESHES];
  FpDefaultRepair repairs[FP_MODEL_REPAIRS];
  uintptr_t kept_meshes[2048];unsigned kept_count;
  unsigned writes, restores;
  char status[160];
} FpModels;
void fp_models_init(FpModels *);
bool fp_models_update(FpModels *, const FpMemory *, const FpSnapshot *,
                      bool wanted, uint64_t now);
#endif
