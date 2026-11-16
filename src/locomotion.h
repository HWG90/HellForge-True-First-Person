/* Character root, camera and rig bases, sprint; camera-space to parent-space
 * conversion. Translation of retail src/first_person/locomotion.lua at
 * 0.5.41-smooth.66 (commit 1d0d453), on game build 25480438;
 * tests/vectors/locomotion_vectors.h holds the retail file's own answers.
 * Line numbers in locomotion.c are the Lua file's.
 *
 * All arithmetic is in double, as Lua's. The engine's matrices are floats in
 * memory; they are widened to double on reading (exactly, as retail's
 * PORT51.number(floats[i])) and nothing here narrows a double back to float:
 * the answers are doubles for the motion model and the pose backend, which
 * narrows at the helper call.
 *
 * The three pure functions take what retail takes. A table field is a bit in
 * `present`; a field of a type that is not a number is passed as NaN. For
 * pitch, yaw, roll (retail: `value.pitch or 0`) false counts as absent; for
 * dx, dy, dz only nil is absent, and an absent one ends retail's ipairs: the
 * components after it are not looked at. A matrix is 16 doubles in the
 * engine's order (rows are axes). An entry of another type than number is
 * NaN; a nil entry cannot be expressed (retail raises Lua's own arithmetic
 * error for it, and no matrix on the live path has one). */
#ifndef TFP_LOCOMOTION_H
#define TFP_LOCOMOTION_H
#include "action_state.h"

enum { TFP_LOCO_DX = 1, TFP_LOCO_DY = 2, TFP_LOCO_DZ = 4, TFP_LOCO_PITCH = 8, TFP_LOCO_YAW = 16, TFP_LOCO_ROLL = 32,
       TFP_LOCO_ALL = 63, TFP_LOCOMOTION_ERROR = 200 };
typedef struct TfpLocomotionValue { double dx, dy, dz, pitch, yaw, roll; unsigned present; } TfpLocomotionValue;
typedef struct TfpLocomotionParent { double dx, dy, dz, qx, qy, qz, qw; } TfpLocomotionParent;

/* M.rotation_to_parent(value, camera, parent) -> {qx, qy, qz, qw} in q[0..3].
 * false with retail's text (retail raises it). With all three angles zero the
 * answer is the identity and the matrices are not looked at. */
bool tfp_locomotion_rotation_to_parent(const TfpLocomotionValue *value, const double camera[16], const double parent[16],
                                       double q[4], const char **error);
/* M.to_parent(value, camera, parent). value NULL: not a table. */
bool tfp_locomotion_to_parent(const TfpLocomotionValue *value, const double camera[16], const double parent[16],
                              TfpLocomotionParent *out, const char **error);
/* M.sprinting(action, verified_lifecycle, flags). action NULL: not a table.
 * verified_lifecycle: retail's "== true". flags NULL: not a string. */
bool tfp_locomotion_sprinting(const TfpActionState *action, bool verified_lifecycle, const uint8_t *flags, size_t flags_size);

/* ---- the sampler, M.new(backend):sample(lease) ---- */
/* backend:on_foot(s, action), backend.lua:351-372, on this build. s NULL / action NULL: not a table. */
bool tfp_on_foot(const FpMemory *memory, const TfpSnapshot *s, const TfpActionState *action);

/* What the Lua module takes from its backend: the reader (backend:_read,
 * _ptr, _u32, _u8) and four methods. */
typedef struct TfpLocomotionBackend {
  const FpMemory *memory;
  void *user;
  bool (*player_snapshot)(void *user, TfpSnapshot *out, char *error, size_t error_size);          /* backend:player_snapshot() */
  bool (*alignment_executable)(void *user, uintptr_t *exe, char *error, size_t error_size);       /* backend:_alignment_executable(profile.pose) */
  bool (*action_state)(void *user, const TfpSnapshot *s, TfpActionState *out);                    /* backend:action_state(s); false: nil */
  bool (*on_foot)(void *user, const TfpSnapshot *s, const TfpActionState *action);                /* backend:on_foot(s, action); action NULL: nil */
} TfpLocomotionBackend;
/* The backend of this repository over `memory`: tfp_snapshot, tfp_alignment_executable, the action-state sampler
 * `reader` (the backend's one sampler, shared with its other users) and tfp_on_foot. host, memory and reader must
 * outlive the backend. */
typedef struct TfpLocomotionHost { const FpMemory *memory; TfpActionStateReader *reader; TfpActionStateBackend actions; } TfpLocomotionHost;
void tfp_locomotion_backend(TfpLocomotionBackend *backend, TfpLocomotionHost *host, const FpMemory *memory, TfpActionStateReader *reader);

/* The lease as the sampler looks at it (locomotion.lua:124-126, 133). */
typedef struct TfpLocomotionLease {
  bool current;              /* lease == backend.alignment_lease */
  bool done, cleanup_only;   /* Lua truth of lease.done, lease.cleanup_only */
  const TfpSnapshot *origin; /* lease.origin: the snapshot whose player_identity is compared */
  uintptr_t request;         /* lease.request, compared with the snapshot's record */
  uintptr_t rig_object;      /* lease.alignment.context.object */
} TfpLocomotionLease;

typedef struct TfpLocomotionSample {
  TfpSnapshot player;        /* owner: the snapshot whose player_identity retail returns */
  double x, y, z, yaw, pitch;
  bool ads, shoulder_aim, sprint, reload;
  uint8_t stance_flags[24];
  bool has_action;           /* false: backend:action_state answered nil; resource, verified and the layers are then nil */
  char action_resource[17];
  bool action_verified;
  bool has_l1, has_l3, has_l14;
  int l1, l3, l14;
  double camera_basis[16], parent_basis[16];
  uintptr_t body_object;
} TfpLocomotionSample;
extern const char tfp_locomotion_sprint_evidence[];

/* self:sample(lease). lease NULL: not a table. true and *out, or false with
 * retail's text (without the Lua runtime's "file:line: " prefix) and *out
 * untouched. */
bool tfp_locomotion_sample(const TfpLocomotionBackend *backend, const TfpLocomotionLease *lease, TfpLocomotionSample *out,
                           char *error, size_t error_size);
#endif
