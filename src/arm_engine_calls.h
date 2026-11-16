/* How arm_pieces.c and grip_hand.c call the engine: one wrapper per pointer of
 * arm_engine_iface.h, so the translated code reads as the Lua does.
 *
 * A raised Lua error is a longjmp to the pcall the retail code wraps the call
 * in (arm_protect); its text is in k->error. `line` is the retail line of the
 * call: a function the engine does not have fails there with the text LuaJIT
 * gives ("<chunk>:<line>: attempt to call field 'alive' (a nil value)").
 * Private to the two translations. */
#ifndef TFP_ARM_ENGINE_CALLS_H
#define TFP_ARM_ENGINE_CALLS_H
#include "arm_engine_iface.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct TfpArmCalls {
  const TfpArmEngine *e;
  const TfpArmBackend *b;
  const char *chunk;
  void *jump;                  /* jmp_buf of the innermost pcall */
  TfpArmError error;
} TfpArmCalls;

static inline _Noreturn void arm_raise(TfpArmCalls *k) { longjmp(*(jmp_buf *)k->jump, 1); }
/* A runtime error of the Lua itself, or a failed assert: "<chunk>:<line>: <message>". */
static inline _Noreturn void arm_fail(TfpArmCalls *k, int line, const char *format, ...) {
  va_list args;
  int used = snprintf(k->error.text, sizeof k->error.text, "%s:%d: ", k->chunk, line);
  va_start(args, format);
  vsnprintf(k->error.text + used, sizeof k->error.text - (size_t)used, format, args);
  va_end(args);
  arm_raise(k);
}
static inline _Noreturn void arm_no_function(TfpArmCalls *k, int line, const char *field) {
  arm_fail(k, line, "attempt to call field '%s' (a nil value)", field);
}
/* Arithmetic on a field that is nil: `piece.side.rig_shoulder+1`. */
static inline void arm_number(TfpArmCalls *k, bool present, int line, const char *field) {
  if (!present) arm_fail(k, line, "attempt to perform arithmetic on field '%s' (a nil value)", field);
}
/* pcall(body): false when it raised. */
static inline bool arm_protect(TfpArmCalls *k, void (*body)(void *), void *argument) {
  jmp_buf here;
  void *outer = k->jump;
  k->jump = &here;
  bool ok = setjmp(here) == 0;
  if (ok) body(argument);
  k->jump = outer;
  return ok;
}

#define ARM_HAVE(pointer, field) do { if (!k->e->pointer) arm_no_function(k, line, field); } while (0)
#define ARM_DONE(call) do { if (!(call)) arm_raise(k); } while (0)

static inline TfpArmWorld arm_Application_main_world(TfpArmCalls *k, int line) {
  TfpArmWorld world = NULL;
  ARM_HAVE(Application_main_world, "main_world");
  ARM_DONE(k->e->Application_main_world(k->e->user, &world, &k->error));
  return world;
}
static inline void arm_World_link_unit(TfpArmCalls *k, int line, TfpArmWorld world, TfpArmUnit child, double child_node, TfpArmUnit parent,
                                       double parent_node) {
  ARM_HAVE(World_link_unit, "link_unit");
  ARM_DONE(k->e->World_link_unit(k->e->user, world, child, child_node, parent, parent_node, &k->error));
}
static inline bool arm_Unit_alive(TfpArmCalls *k, int line, TfpArmUnit unit) {
  bool alive = false;
  ARM_HAVE(Unit_alive, "alive");
  ARM_DONE(k->e->Unit_alive(k->e->user, unit, &alive, &k->error));
  return alive;
}
static inline double arm_Unit_num_scene_graph_items(TfpArmCalls *k, int line, TfpArmUnit unit) {
  double count = 0;
  ARM_HAVE(Unit_num_scene_graph_items, "num_scene_graph_items");
  ARM_DONE(k->e->Unit_num_scene_graph_items(k->e->user, unit, &count, &k->error));
  return count;
}
static inline TfpArmMatrix4x4 arm_Unit_world_pose(TfpArmCalls *k, int line, TfpArmUnit unit, double node) {
  TfpArmMatrix4x4 out;
  memset(&out, 0, sizeof out);
  ARM_HAVE(Unit_world_pose, "world_pose");
  ARM_DONE(k->e->Unit_world_pose(k->e->user, unit, node, &out, &k->error));
  return out;
}
static inline TfpArmVector3 arm_Unit_world_position(TfpArmCalls *k, int line, TfpArmUnit unit, double node) {
  TfpArmVector3 out = {0, 0, 0};
  ARM_HAVE(Unit_world_position, "world_position");
  ARM_DONE(k->e->Unit_world_position(k->e->user, unit, node, &out, &k->error));
  return out;
}
static inline TfpArmQuaternion arm_Unit_world_rotation(TfpArmCalls *k, int line, TfpArmUnit unit, double node) {
  TfpArmQuaternion out = {0, 0, 0, 0};
  ARM_HAVE(Unit_world_rotation, "world_rotation");
  ARM_DONE(k->e->Unit_world_rotation(k->e->user, unit, node, &out, &k->error));
  return out;
}
static inline void arm_Unit_set_local_pose(TfpArmCalls *k, int line, TfpArmUnit unit, double node, TfpArmMatrix4x4 pose) {
  ARM_HAVE(Unit_set_local_pose, "set_local_pose");
  ARM_DONE(k->e->Unit_set_local_pose(k->e->user, unit, node, &pose, &k->error));
}
static inline double arm_Unit_num_meshes(TfpArmCalls *k, int line, TfpArmUnit unit) {
  double count = 0;
  ARM_HAVE(Unit_num_meshes, "num_meshes");
  ARM_DONE(k->e->Unit_num_meshes(k->e->user, unit, &count, &k->error));
  return count;
}
static inline TfpArmMesh arm_Unit_mesh(TfpArmCalls *k, int line, TfpArmUnit unit, double index) {
  TfpArmMesh mesh = NULL;
  ARM_HAVE(Unit_mesh, "mesh");
  ARM_DONE(k->e->Unit_mesh(k->e->user, unit, index, &mesh, &k->error));
  return mesh;
}
static inline void arm_Unit_set_mesh_visibility(TfpArmCalls *k, int line, TfpArmUnit unit, double index, bool visible) {
  ARM_HAVE(Unit_set_mesh_visibility, "set_mesh_visibility");
  ARM_DONE(k->e->Unit_set_mesh_visibility(k->e->user, unit, index, visible, &k->error));
}
static inline bool arm_Mesh_visibility(TfpArmCalls *k, int line, TfpArmMesh mesh) {
  bool visible = false;
  ARM_HAVE(Mesh_visibility, "visibility");
  ARM_DONE(k->e->Mesh_visibility(k->e->user, mesh, &visible, &k->error));
  return visible;
}
static inline double arm_Mesh_num_materials(TfpArmCalls *k, int line, TfpArmMesh mesh) {
  double count = 0;
  ARM_HAVE(Mesh_num_materials, "num_materials");
  ARM_DONE(k->e->Mesh_num_materials(k->e->user, mesh, &count, &k->error));
  return count;
}

/* Vector3(x,y,z): the table itself is called, so a missing table fails as a call of the upvalue `V`. */
static inline TfpArmVector3 arm_Vector3(TfpArmCalls *k, int line, double x, double y, double z) {
  TfpArmVector3 out = {0, 0, 0};
  if (!k->e->Vector3_call) arm_fail(k, line, "attempt to call upvalue 'V' (a table value)");
  ARM_DONE(k->e->Vector3_call(k->e->user, x, y, z, &out, &k->error));
  return out;
}
static inline double arm_Vector3_length(TfpArmCalls *k, int line, TfpArmVector3 v) {
  double out = 0;
  ARM_HAVE(Vector3_length, "length");
  ARM_DONE(k->e->Vector3_length(k->e->user, v, &out, &k->error));
  return out;
}
static inline double arm_Vector3_dot(TfpArmCalls *k, int line, TfpArmVector3 a, TfpArmVector3 b) {
  double out = 0;
  ARM_HAVE(Vector3_dot, "dot");
  ARM_DONE(k->e->Vector3_dot(k->e->user, a, b, &out, &k->error));
  return out;
}
static inline TfpArmVector3 arm_Vector3_cross(TfpArmCalls *k, int line, TfpArmVector3 a, TfpArmVector3 b) {
  TfpArmVector3 out = {0, 0, 0};
  ARM_HAVE(Vector3_cross, "cross");
  ARM_DONE(k->e->Vector3_cross(k->e->user, a, b, &out, &k->error));
  return out;
}
static inline TfpArmVector3 arm_Vector3_normalize(TfpArmCalls *k, int line, TfpArmVector3 v) {
  TfpArmVector3 out = {0, 0, 0};
  ARM_HAVE(Vector3_normalize, "normalize");
  ARM_DONE(k->e->Vector3_normalize(k->e->user, v, &out, &k->error));
  return out;
}
static inline TfpArmVector3 arm_Vector3_lerp(TfpArmCalls *k, int line, TfpArmVector3 a, TfpArmVector3 b, double t) {
  TfpArmVector3 out = {0, 0, 0};
  ARM_HAVE(Vector3_lerp, "lerp");
  ARM_DONE(k->e->Vector3_lerp(k->e->user, a, b, t, &out, &k->error));
  return out;
}
static inline double arm_Vector3_x(TfpArmCalls *k, int line, TfpArmVector3 v) {
  double out = 0;
  ARM_HAVE(Vector3_x, "x");
  ARM_DONE(k->e->Vector3_x(k->e->user, v, &out, &k->error));
  return out;
}
static inline double arm_Vector3_y(TfpArmCalls *k, int line, TfpArmVector3 v) {
  double out = 0;
  ARM_HAVE(Vector3_y, "y");
  ARM_DONE(k->e->Vector3_y(k->e->user, v, &out, &k->error));
  return out;
}
static inline double arm_Vector3_z(TfpArmCalls *k, int line, TfpArmVector3 v) {
  double out = 0;
  ARM_HAVE(Vector3_z, "z");
  ARM_DONE(k->e->Vector3_z(k->e->user, v, &out, &k->error));
  return out;
}
/* a + b, a - b, v * n, -v */
static inline TfpArmVector3 arm_add(TfpArmCalls *k, TfpArmVector3 a, TfpArmVector3 b) {
  TfpArmVector3 out = {0, 0, 0};
  ARM_DONE(k->e->Vector3_op_add(k->e->user, a, b, &out, &k->error));
  return out;
}
static inline TfpArmVector3 arm_sub(TfpArmCalls *k, TfpArmVector3 a, TfpArmVector3 b) {
  TfpArmVector3 out = {0, 0, 0};
  ARM_DONE(k->e->Vector3_op_sub(k->e->user, a, b, &out, &k->error));
  return out;
}
static inline TfpArmVector3 arm_mul(TfpArmCalls *k, TfpArmVector3 v, double n) {
  TfpArmVector3 out = {0, 0, 0};
  ARM_DONE(k->e->Vector3_op_mul(k->e->user, v, n, &out, &k->error));
  return out;
}
static inline TfpArmVector3 arm_unm(TfpArmCalls *k, TfpArmVector3 v) {
  TfpArmVector3 out = {0, 0, 0};
  ARM_DONE(k->e->Vector3_op_unm(k->e->user, v, &out, &k->error));
  return out;
}

static inline TfpArmQuaternion arm_Quaternion_identity(TfpArmCalls *k, int line) {
  TfpArmQuaternion out = {0, 0, 0, 0};
  ARM_HAVE(Quaternion_identity, "identity");
  ARM_DONE(k->e->Quaternion_identity(k->e->user, &out, &k->error));
  return out;
}
static inline TfpArmQuaternion arm_Quaternion_axis_angle(TfpArmCalls *k, int line, TfpArmVector3 axis, double angle) {
  TfpArmQuaternion out = {0, 0, 0, 0};
  ARM_HAVE(Quaternion_axis_angle, "axis_angle");
  ARM_DONE(k->e->Quaternion_axis_angle(k->e->user, axis, angle, &out, &k->error));
  return out;
}
static inline TfpArmQuaternion arm_Quaternion_multiply(TfpArmCalls *k, int line, TfpArmQuaternion a, TfpArmQuaternion b) {
  TfpArmQuaternion out = {0, 0, 0, 0};
  ARM_HAVE(Quaternion_multiply, "multiply");
  ARM_DONE(k->e->Quaternion_multiply(k->e->user, a, b, &out, &k->error));
  return out;
}
static inline TfpArmVector3 arm_Quaternion_rotate(TfpArmCalls *k, int line, TfpArmQuaternion q, TfpArmVector3 v) {
  TfpArmVector3 out = {0, 0, 0};
  ARM_HAVE(Quaternion_rotate, "rotate");
  ARM_DONE(k->e->Quaternion_rotate(k->e->user, q, v, &out, &k->error));
  return out;
}
static inline TfpArmQuaternion arm_Quaternion_lerp(TfpArmCalls *k, int line, TfpArmQuaternion a, TfpArmQuaternion b, double t) {
  TfpArmQuaternion out = {0, 0, 0, 0};
  ARM_HAVE(Quaternion_lerp, "lerp");
  ARM_DONE(k->e->Quaternion_lerp(k->e->user, a, b, t, &out, &k->error));
  return out;
}
static inline TfpArmQuaternion arm_Quaternion_inverse(TfpArmCalls *k, int line, TfpArmQuaternion q) {
  TfpArmQuaternion out = {0, 0, 0, 0};
  ARM_HAVE(Quaternion_inverse, "inverse");
  ARM_DONE(k->e->Quaternion_inverse(k->e->user, q, &out, &k->error));
  return out;
}
typedef struct TfpArmElements { double x, y, z, w; } TfpArmElements;
static inline TfpArmElements arm_Quaternion_to_elements(TfpArmCalls *k, int line, TfpArmQuaternion q) {
  double out[4] = {0, 0, 0, 0};
  ARM_HAVE(Quaternion_to_elements, "to_elements");
  ARM_DONE(k->e->Quaternion_to_elements(k->e->user, q, out, &k->error));
  return (TfpArmElements){out[0], out[1], out[2], out[3]};
}
static inline TfpArmQuaternion arm_Quaternion_from_elements(TfpArmCalls *k, int line, double x, double y, double z, double w) {
  TfpArmQuaternion out = {0, 0, 0, 0};
  ARM_HAVE(Quaternion_from_elements, "from_elements");
  ARM_DONE(k->e->Quaternion_from_elements(k->e->user, x, y, z, w, &out, &k->error));
  return out;
}

static inline TfpArmMatrix4x4 arm_Matrix4x4_identity(TfpArmCalls *k, int line) {
  TfpArmMatrix4x4 out;
  memset(&out, 0, sizeof out);
  ARM_HAVE(Matrix4x4_identity, "identity");
  ARM_DONE(k->e->Matrix4x4_identity(k->e->user, &out, &k->error));
  return out;
}
static inline TfpArmMatrix4x4 arm_Matrix4x4_from_quaternion_position(TfpArmCalls *k, int line, TfpArmQuaternion q, TfpArmVector3 position) {
  TfpArmMatrix4x4 out;
  memset(&out, 0, sizeof out);
  ARM_HAVE(Matrix4x4_from_quaternion_position, "from_quaternion_position");
  ARM_DONE(k->e->Matrix4x4_from_quaternion_position(k->e->user, q, position, &out, &k->error));
  return out;
}
static inline TfpArmMatrix4x4 arm_Matrix4x4_multiply(TfpArmCalls *k, int line, TfpArmMatrix4x4 a, TfpArmMatrix4x4 b) {
  TfpArmMatrix4x4 out;
  memset(&out, 0, sizeof out);
  ARM_HAVE(Matrix4x4_multiply, "multiply");
  ARM_DONE(k->e->Matrix4x4_multiply(k->e->user, &a, &b, &out, &k->error));
  return out;
}
static inline TfpArmMatrix4x4 arm_Matrix4x4_inverse(TfpArmCalls *k, int line, TfpArmMatrix4x4 m) {
  TfpArmMatrix4x4 out;
  memset(&out, 0, sizeof out);
  ARM_HAVE(Matrix4x4_inverse, "inverse");
  ARM_DONE(k->e->Matrix4x4_inverse(k->e->user, &m, &out, &k->error));
  return out;
}
static inline TfpArmVector3 arm_Matrix4x4_translation(TfpArmCalls *k, int line, TfpArmMatrix4x4 m) {
  TfpArmVector3 out = {0, 0, 0};
  ARM_HAVE(Matrix4x4_translation, "translation");
  ARM_DONE(k->e->Matrix4x4_translation(k->e->user, &m, &out, &k->error));
  return out;
}
static inline TfpArmQuaternion arm_Matrix4x4_rotation(TfpArmCalls *k, int line, TfpArmMatrix4x4 m) {
  TfpArmQuaternion out = {0, 0, 0, 0};
  ARM_HAVE(Matrix4x4_rotation, "rotation");
  ARM_DONE(k->e->Matrix4x4_rotation(k->e->user, &m, &out, &k->error));
  return out;
}

/* ---- the backend ---- */
static inline double arm_object(TfpArmCalls *k, const TfpArmSnapshot *s, bool has_ref, double ref) {
  double object = 0;
  ARM_DONE(k->b->object(k->b->user, has_ref, ref, s->has_avatar_owner, s->avatar_owner, s->has_avatar_index, s->avatar_index, &object, &k->error));
  return object;
}
/* pcall(object, s, ref) */
static inline bool arm_object_protected(TfpArmCalls *k, const TfpArmSnapshot *s, double ref, double *object) {
  TfpArmError ignored;
  return k->b->object(k->b->user, true, ref, s->has_avatar_owner, s->avatar_owner, s->has_avatar_index, s->avatar_index, object, &ignored);
}
static inline void arm_read(TfpArmCalls *k, double at, double size, uint8_t *out) { ARM_DONE(k->b->read(k->b->user, at, size, out, &k->error)); }
static inline double arm_ptr(TfpArmCalls *k, double at) {
  double out = 0;
  ARM_DONE(k->b->ptr(k->b->user, at, &out, &k->error));
  return out;
}
static inline double arm_u32(TfpArmCalls *k, double at) {
  double out = 0;
  ARM_DONE(k->b->u32(k->b->user, at, &out, &k->error));
  return out;
}
static inline double arm_clock(TfpArmCalls *k) {
  double now = 0;
  ARM_DONE(k->b->clock(k->b->user, &now, &k->error));
  return now;
}
/* handles[ref]: NULL when there is no such row (or ref is nil). */
static inline TfpArmUnit arm_handle(const TfpArmHandles *handles, bool has_ref, double ref) {
  if (!has_ref) return NULL;
  for (size_t i = 0; i < handles->count; i++) if (handles->rows[i].ref == ref) return handles->rows[i].unit;
  return NULL;
}
/* Words of a bone table as the Lua splits them. */
static inline double arm_word(const uint8_t *bytes, size_t index) {
  const uint8_t *b = bytes + index * 4;
  return (double)b[0] + (double)b[1] * 256 + (double)b[2] * 65536 + (double)b[3] * 16777216;
}
static inline double arm_low(const uint8_t *bytes, size_t index) { return (double)bytes[index * 4] + (double)bytes[index * 4 + 1] * 256; }
static inline double arm_high(const uint8_t *bytes, size_t index) { return (double)bytes[index * 4 + 2] + (double)bytes[index * 4 + 3] * 256; }
#endif
