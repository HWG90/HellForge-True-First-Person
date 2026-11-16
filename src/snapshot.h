/* The player and weapon snapshot of retail True First Person 0.5.41-smooth.66
 * (src/first_person/backend.lua:157-290, "_top", "_map", "_descriptor",
 * "_snapshot"), read on build 25480438. Field names, the order of reads and
 * checks, and the error texts are retail's. Reads only.
 *
 * Not here yet (they arrive with the camera part): the action-state and
 * on-foot test and the foreign-camera test, which the full retail snapshot
 * applies last. Until then `safe` equals retail's `engine_safe`. */
#ifndef TFP_SNAPSHOT_H
#define TFP_SNAPSHOT_H
#include "backend.h"

typedef struct TfpSnapshot {
  uintptr_t base, context, camera, record, world, controller, player;
  uintptr_t avatar_owner, avatar_entry, flags_address, weapon_component_descriptor;
  uint32_t head, tail, camera_kind, request_avatar, request_weapon, request_body;
  uint32_t local_entity, avatar, avatar_index, body, viewmodel, weapon, weapon_unit;
  uint32_t mask;                       /* controller bytes 0..3 */
  uint8_t controller_bytes[6], flags[24], avatar_descriptor[20], weapon_descriptor[20];
  bool controller_active, blocked, request_local, aiming, shoulder_aim, seated, reloading;
  bool safe, engine_safe, weapon_transition, head_camera_transition, weapon_ready, full;
} TfpSnapshot;
typedef enum TfpSnapshotScope { TFP_SNAPSHOT_PLAYER, TFP_SNAPSHOT_FULL } TfpSnapshotScope;

/* Game image identity and code pins, retail scope "identity": a pin that
 * cannot be read yet is tolerated, a readable one must match. tfp_snapshot
 * runs this first on every call, as retail's _snapshot does; a caller that
 * takes many snapshots in one frame may cache nothing yet (retail caches the
 * result per frame; that arrives with the backend). */
bool tfp_module_identity(const FpMemory *memory, char *error, size_t error_size);
/* true and *out filled, or false with retail's error text and *out untouched. */
bool tfp_snapshot(const FpMemory *memory, TfpSnapshotScope scope, TfpSnapshot *out,
                  char *error, size_t error_size);
/* Retail's player_identity and identity strings, as comparisons. */
bool tfp_same_player(const TfpSnapshot *a, const TfpSnapshot *b);
bool tfp_same_identity(const TfpSnapshot *a, const TfpSnapshot *b);
#endif
