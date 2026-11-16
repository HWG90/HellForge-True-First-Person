/* The engine script functions the two arm modules call (retail: the `stingray`
 * table handed to arm_pieces.lua and grip_hand.lua as `sr`), the backend's
 * readers they use and their clock, as tables of pointers the mod fills in.
 * One pointer per engine function, named after it; handles are opaque.
 * Nothing here depends on any other header of the project.
 *
 * Every function returns false when the Lua call raised: `error->text` is
 * then the error's text. A pointer left NULL is a function the engine does
 * not have (`sr.Unit.mesh == nil`); a table none of whose functions is set is
 * a table the engine does not have (`sr.Mesh == nil`). The four vector
 * operators and the backend's functions must always be set.
 *
 * Vector3, Quaternion and Matrix4x4 are the engine's own values. The modules
 * never look inside one: they are made, combined and taken apart only by the
 * functions below, in the order the Lua calls them. Scene-graph node and mesh
 * indices count from 1 and are Lua numbers (double), as are all scalars.
 * docs/ARMS-TRANSLATION.md lists every function with its Lua signature. */
#ifndef TFP_ARM_ENGINE_IFACE_H
#define TFP_ARM_ENGINE_IFACE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFP_ARM_ERROR_TEXT 256
typedef struct TfpArmError { char text[TFP_ARM_ERROR_TEXT]; } TfpArmError;

/* Engine userdata. NULL is Lua nil. */
typedef const struct TfpArmWorldObject *TfpArmWorld;
typedef const struct TfpArmUnitObject *TfpArmUnit;
typedef const struct TfpArmMeshObject *TfpArmMesh;
/* Engine values (32-bit floats, the engine's layout). */
typedef struct TfpArmVector3 { float x, y, z; } TfpArmVector3;
typedef struct TfpArmQuaternion { float x, y, z, w; } TfpArmQuaternion;
typedef struct TfpArmMatrix4x4 { float m[16]; } TfpArmMatrix4x4;

typedef struct TfpArmEngine {
  void *user;
  /* stingray.Application.main_world() -> World or nil */
  bool (*Application_main_world)(void *user, TfpArmWorld *world, TfpArmError *error);
  /* stingray.World.link_unit(world, child, child_node, parent, parent_node) */
  bool (*World_link_unit)(void *user, TfpArmWorld world, TfpArmUnit child, double child_node, TfpArmUnit parent, double parent_node,
                          TfpArmError *error);

  /* stingray.Unit.alive(unit) -> boolean. *alive: the answer was exactly `true`. */
  bool (*Unit_alive)(void *user, TfpArmUnit unit, bool *alive, TfpArmError *error);
  /* stingray.Unit.num_scene_graph_items(unit) -> number */
  bool (*Unit_num_scene_graph_items)(void *user, TfpArmUnit unit, double *count, TfpArmError *error);
  /* stingray.Unit.world_pose(unit, node) -> Matrix4x4 */
  bool (*Unit_world_pose)(void *user, TfpArmUnit unit, double node, TfpArmMatrix4x4 *pose, TfpArmError *error);
  /* stingray.Unit.world_position(unit, node) -> Vector3 */
  bool (*Unit_world_position)(void *user, TfpArmUnit unit, double node, TfpArmVector3 *position, TfpArmError *error);
  /* stingray.Unit.world_rotation(unit, node) -> Quaternion */
  bool (*Unit_world_rotation)(void *user, TfpArmUnit unit, double node, TfpArmQuaternion *rotation, TfpArmError *error);
  /* stingray.Unit.set_local_pose(unit, node, pose) */
  bool (*Unit_set_local_pose)(void *user, TfpArmUnit unit, double node, const TfpArmMatrix4x4 *pose, TfpArmError *error);
  /* stingray.Unit.num_meshes(unit) -> number                                    (grip_hand only) */
  bool (*Unit_num_meshes)(void *user, TfpArmUnit unit, double *count, TfpArmError *error);
  /* stingray.Unit.mesh(unit, mesh_index) -> Mesh or nil                         (grip_hand only) */
  bool (*Unit_mesh)(void *user, TfpArmUnit unit, double mesh_index, TfpArmMesh *mesh, TfpArmError *error);
  /* stingray.Unit.set_mesh_visibility(unit, mesh_index, visible): no pass name  (grip_hand only) */
  bool (*Unit_set_mesh_visibility)(void *user, TfpArmUnit unit, double mesh_index, bool visible, TfpArmError *error);

  /* stingray.Mesh.visibility(mesh) -> boolean. *visible: the answer was exactly `true`.  (grip_hand only) */
  bool (*Mesh_visibility)(void *user, TfpArmMesh mesh, bool *visible, TfpArmError *error);
  /* stingray.Mesh.num_materials(mesh) -> number                                 (grip_hand only) */
  bool (*Mesh_num_materials)(void *user, TfpArmMesh mesh, double *count, TfpArmError *error);

  /* stingray.Vector3(x, y, z) -> Vector3 */
  bool (*Vector3_call)(void *user, double x, double y, double z, TfpArmVector3 *out, TfpArmError *error);
  /* stingray.Vector3.length(v) -> number */
  bool (*Vector3_length)(void *user, TfpArmVector3 v, double *out, TfpArmError *error);
  /* stingray.Vector3.dot(a, b) -> number */
  bool (*Vector3_dot)(void *user, TfpArmVector3 a, TfpArmVector3 b, double *out, TfpArmError *error);
  /* stingray.Vector3.cross(a, b) -> Vector3 */
  bool (*Vector3_cross)(void *user, TfpArmVector3 a, TfpArmVector3 b, TfpArmVector3 *out, TfpArmError *error);
  /* stingray.Vector3.normalize(v) -> Vector3 */
  bool (*Vector3_normalize)(void *user, TfpArmVector3 v, TfpArmVector3 *out, TfpArmError *error);
  /* stingray.Vector3.lerp(a, b, t) -> Vector3                                   (grip_hand only) */
  bool (*Vector3_lerp)(void *user, TfpArmVector3 a, TfpArmVector3 b, double t, TfpArmVector3 *out, TfpArmError *error);
  /* stingray.Vector3.x(v), .y(v), .z(v) -> number                               (grip_hand only) */
  bool (*Vector3_x)(void *user, TfpArmVector3 v, double *out, TfpArmError *error);
  bool (*Vector3_y)(void *user, TfpArmVector3 v, double *out, TfpArmError *error);
  bool (*Vector3_z)(void *user, TfpArmVector3 v, double *out, TfpArmError *error);
  /* The Vector3 userdata's operators: a + b, a - b, v * number, -v. */
  bool (*Vector3_op_add)(void *user, TfpArmVector3 a, TfpArmVector3 b, TfpArmVector3 *out, TfpArmError *error);
  bool (*Vector3_op_sub)(void *user, TfpArmVector3 a, TfpArmVector3 b, TfpArmVector3 *out, TfpArmError *error);
  bool (*Vector3_op_mul)(void *user, TfpArmVector3 v, double n, TfpArmVector3 *out, TfpArmError *error);
  bool (*Vector3_op_unm)(void *user, TfpArmVector3 v, TfpArmVector3 *out, TfpArmError *error);

  /* stingray.Quaternion.identity() -> Quaternion */
  bool (*Quaternion_identity)(void *user, TfpArmQuaternion *out, TfpArmError *error);
  /* stingray.Quaternion.axis_angle(axis, radians) -> Quaternion */
  bool (*Quaternion_axis_angle)(void *user, TfpArmVector3 axis, double angle, TfpArmQuaternion *out, TfpArmError *error);
  /* stingray.Quaternion.multiply(second, first) -> Quaternion: applies first, then second */
  bool (*Quaternion_multiply)(void *user, TfpArmQuaternion a, TfpArmQuaternion b, TfpArmQuaternion *out, TfpArmError *error);
  /* stingray.Quaternion.rotate(q, v) -> Vector3 */
  bool (*Quaternion_rotate)(void *user, TfpArmQuaternion q, TfpArmVector3 v, TfpArmVector3 *out, TfpArmError *error);
  /* stingray.Quaternion.lerp(a, b, t) -> Quaternion                             (grip_hand only) */
  bool (*Quaternion_lerp)(void *user, TfpArmQuaternion a, TfpArmQuaternion b, double t, TfpArmQuaternion *out, TfpArmError *error);
  /* stingray.Quaternion.inverse(q) -> Quaternion                                (grip_hand only) */
  bool (*Quaternion_inverse)(void *user, TfpArmQuaternion q, TfpArmQuaternion *out, TfpArmError *error);
  /* stingray.Quaternion.to_elements(q) -> x, y, z, w                            (grip_hand only) */
  bool (*Quaternion_to_elements)(void *user, TfpArmQuaternion q, double out[4], TfpArmError *error);
  /* stingray.Quaternion.from_elements(x, y, z, w) -> Quaternion                 (grip_hand only) */
  bool (*Quaternion_from_elements)(void *user, double x, double y, double z, double w, TfpArmQuaternion *out, TfpArmError *error);

  /* stingray.Matrix4x4.identity() -> Matrix4x4 */
  bool (*Matrix4x4_identity)(void *user, TfpArmMatrix4x4 *out, TfpArmError *error);
  /* stingray.Matrix4x4.from_quaternion_position(q, position) -> Matrix4x4 */
  bool (*Matrix4x4_from_quaternion_position)(void *user, TfpArmQuaternion q, TfpArmVector3 position, TfpArmMatrix4x4 *out, TfpArmError *error);
  /* stingray.Matrix4x4.multiply(a, b) -> Matrix4x4: a, then b */
  bool (*Matrix4x4_multiply)(void *user, const TfpArmMatrix4x4 *a, const TfpArmMatrix4x4 *b, TfpArmMatrix4x4 *out, TfpArmError *error);
  /* stingray.Matrix4x4.inverse(m) -> Matrix4x4 */
  bool (*Matrix4x4_inverse)(void *user, const TfpArmMatrix4x4 *m, TfpArmMatrix4x4 *out, TfpArmError *error);
  /* stingray.Matrix4x4.translation(m) -> Vector3 */
  bool (*Matrix4x4_translation)(void *user, const TfpArmMatrix4x4 *m, TfpArmVector3 *out, TfpArmError *error);
  /* stingray.Matrix4x4.rotation(m) -> Quaternion                                (grip_hand only) */
  bool (*Matrix4x4_rotation)(void *user, const TfpArmMatrix4x4 *m, TfpArmQuaternion *out, TfpArmError *error);
} TfpArmEngine;

/* The backend (backend.lua) and the clock given to M.new. Addresses and unit references are Lua
 * numbers. `has_*` false: the snapshot field is nil. */
typedef struct TfpArmBackend {
  void *user;
  /* backend:_alignment_context({viewmodel=unit, avatar_owner=..., avatar_index=...}).object */
  bool (*object)(void *user, bool has_unit, double unit, bool has_avatar_owner, double avatar_owner, bool has_avatar_index,
                 double avatar_index, double *object, TfpArmError *error);
  /* backend:_read(at, size) -> exactly `size` bytes (size <= 1020 here) */
  bool (*read)(void *user, double at, double size, uint8_t *out, TfpArmError *error);
  /* backend:_ptr(at) -> number */
  bool (*ptr)(void *user, double at, double *out, TfpArmError *error);
  /* backend:_u32(at) -> number */
  bool (*u32)(void *user, double at, double *out, TfpArmError *error);
  /* deps.clock() -> seconds */
  bool (*clock)(void *user, double *now, TfpArmError *error);
} TfpArmBackend;

/* The snapshot `s` both modules are updated with, reduced to the fields they read. */
typedef struct TfpArmSnapshot {
  bool has_body, has_viewmodel, has_avatar_owner, has_avatar_index, has_weapon, has_request_weapon;
  double body, viewmodel, avatar_owner, avatar_index, weapon, request_weapon;
  bool aiming, shoulder_aim, weapon_transition;      /* the field is exactly `true` */
} TfpArmSnapshot;

/* The table head_visibility's sdk_handles() returns: {[unit reference] = Unit}. `identity` stands for
 * the Lua table object: both modules look everything up again when they are handed another table, and
 * do not when the same table comes with other rows. `rows` are walked where the Lua walks pairs(); the
 * order of pairs() is not defined, so any order is as right as retail's. */
typedef struct TfpArmHandle { double ref; TfpArmUnit unit; } TfpArmHandle;
typedef struct TfpArmHandles { uintptr_t identity; size_t count; const TfpArmHandle *rows; } TfpArmHandles;
#endif
