/* See wire_engine.h. Line references: "rt:N" retail runtime.lua, "hv:N" head_visibility.lua,
 * "ap:N" arm_pieces.lua, "gh:N" grip_hand.lua, "census N" a line of
 * docs/research-0.5.34/live-sdk-census-g1-9472b065.tsv. No Windows. */
#include "wire_engine.h"
#include "wire_off.h"
#include "head_visibility.h"
#include "arm_pieces.h"
#include "grip_hand.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef TfpRtValue V;
#define nil tfp_wire_nil
#define vbool tfp_wire_bool
#define BIT(module) (1u << (module))
#define HEAD BIT(TFP_WE_HEAD)
#define SLEEVES BIT(TFP_WE_SLEEVES)
#define GRIP BIT(TFP_WE_GRIP)
#define ARMS (SLEEVES | GRIP)

/* ---- the engine functions of build 25480438 -------------------------------------------------
 * path, exe RVA of its C body, census line, who needs it. "bridge": src/engine.c's own marshalling
 * of a Vector3 / Quaternion argument or result calls it. "check": only an in-game check calls it. */
static const TfpWireEngineFunction functions[] = {
  {"Application.main_world", 0x3f8310, 282, HEAD | ARMS},
  {"World.units", 0x3eaf80, 123, HEAD},
  {"Unit.set_mesh_visibility", 0x4128e0, 2150, HEAD | GRIP},
  {"Unit.has_mesh", 0x409490, 2215, HEAD},
  {"Unit.mesh", 0x409730, 2212, HEAD | GRIP},
  {"Mesh.name", 0x489590, 1037, HEAD},
  {"Mesh.node", 0x48b700, 1024, HEAD},
  {"Mesh.num_materials", 0x488390, 1034, HEAD | GRIP},
  {"Mesh.has_material", 0x488500, 1032, HEAD},
  {"Unit.num_scene_graph_items", 0x408660, 2184, HEAD | ARMS},   /* head: check */
  {"Unit.num_meshes", 0x409300, 2220, HEAD | GRIP},              /* head: check */
  {"World.link_unit", 0x3eb860, 132, SLEEVES},
  {"Unit.alive", 0x40b820, 2196, ARMS},
  {"Unit.world_position", 0x407810, 2132, ARMS},
  {"Unit.world_rotation", 0x407a10, 2222, ARMS},
  {"Unit.set_local_pose", 0x406960, 2147, ARMS},
  {"Mesh.visibility", 0x4892e0, 1039, GRIP},
  {"Matrix4x4.from_quaternion_position", 0x44a750, 1473, ARMS},
  {"Vector3", 0, 949, ARMS},                                     /* bridge: the constructor table */
  {"Vector3.x", 0x438070, 969, ARMS},                            /* bridge */
  {"Vector3.y", 0x4380f0, 970, ARMS},                            /* bridge */
  {"Vector3.z", 0x438170, 992, ARMS},                            /* bridge */
  {"Quaternion.from_elements", 0x442aa0, 1179, ARMS},            /* bridge */
  {"Quaternion.to_elements", 0x4432b0, 1161, ARMS},              /* bridge */
  {"Unit.scene_graph_parent", 0x408770, 2186, ARMS},             /* check */
  {"Unit.world_pose", 0x407c10, 2124, ARMS},                     /* check */
  {"Matrix4x4.scale", 0x44de80, 1504, ARMS},                     /* check */
  {"Quaternion.multiply", 0x4448f0, 1189, ARMS},                 /* check (value maths) from here on */
  {"Quaternion.rotate", 0x447740, 1170, ARMS},
  {"Quaternion.axis_angle", 0x444000, 1182, ARMS},
  {"Quaternion.inverse", 0x445380, 1183, ARMS},
  {"Quaternion.lerp", 0x445b00, 1164, ARMS},
  {"Matrix4x4.multiply", 0x44c6b0, 1502, ARMS},
  {"Matrix4x4.inverse", 0x44c7f0, 1505, ARMS},
  {"Matrix4x4.rotation", 0x44d2f0, 1508, ARMS},
  {"Matrix4x4.translation", 0x44d0d0, 1479, ARMS},
  {"Vector3.cross", 0x439080, 980, ARMS},
  {"Vector3.dot", 0x438ee0, 961, ARMS},
  {"Vector3.length", 0x438b40, 981, ARMS},
  {"Vector3.normalize", 0x438d10, 967, ARMS},
  {"Vector3.lerp", 0x439740, 955, ARMS},
};
const TfpWireEngineFunction *tfp_wire_engine_functions(size_t *count) {
  *count = sizeof functions / sizeof *functions;
  return functions;
}
static const char *const module_names[TFP_WE_MODULES] = {"head visibility", "model shadows", "arm pieces", "grip hand"};
const char *tfp_wire_engine_module_name(TfpWireEngineModule module) { return module_names[module]; }

/* ---- state ---------------------------------------------------------------------------------- */
/* One engine value a module holds: its userdata address (what retail's host calls the handle's
 * address, hv:27-51) and a registry reference to pass it back with. A handle is the address of
 * its slot. Unit handles from World.units' list get their reference only when one is used. */
typedef struct Slot { uintptr_t pointer; TfpEngRef ref; uint32_t list_index; uint8_t state; } Slot;
enum { SLOT_FREE, SLOT_TRANSIENT, SLOT_KEPT };
enum { KEPT_MAX = 65536, UNITS_MAX = 131072 /* hv:374 */, SLOTS = KEPT_MAX + UNITS_MAX, INDEX_BITS = 19, INDEX = 1 << INDEX_BITS };
enum { RECENT = 8 };

struct TfpWireEngine;
typedef struct ArmUser { struct TfpWireEngine *st; TfpWireEngineModule module; } ArmUser;

typedef struct TfpWireEngine {
  TfpWire *w;
  TfpWireEngineApi api;
  TfpWireEngineMemory memory;
  TfpRtHost off;                       /* the answers of wire_off.c, as they were before bind */
  TfpWireEngineState state[TFP_WE_MODULES];
  bool vm, told_waiting, told_no_exe;
  uint64_t generation, exe;
  uint32_t frame;
  char why[320];                       /* the default memory's last text */
  /* handles */
  Slot *slots;
  uint32_t *index, *transient, *spare;
  uintptr_t *pointers;
  const struct TfpEngineUnitObject **units;
  uint32_t used, transient_count, spare_count, kept_count;
  TfpEngRef units_list;
  bool told_full;
  /* the modules */
  TfpEngineIface head_engine;
  TfpHeadIo head_io;
  TfpModelShadowIo models_io;
  TfpModelShadow models;
  bool has_models;
  TfpHead *head;
  ArmUser arm_user[TFP_WE_MODULES];
  TfpArmEngine arm_engine[TFP_WE_MODULES];
  TfpArmBackend arm_backend;
  TfpArmPiecesModule pieces_module;
  TfpArmPieces pieces;
  TfpGripHand *grip;
  bool has_pieces, has_grip;
  TfpArmHandles arm_handles;
  TfpArmHandle *arm_rows;
  size_t arm_room;
  /* the snapshots head visibility was handed, to find the backend's own again */
  struct { TfpOpsSnapshot ops; TfpVisSnapshot vis; } recent[RECENT];
  unsigned recent_next;
  TfpOpsAction actions[4];
  unsigned action_next;
  /* checks */
  bool maths_ok, arms_checked, link_checked, link_failed, told_arms_waiting, told_head_off;
  uint32_t arms_retry;
  TfpWireEngineBody body;              /* of the last arm update: whose units a link is read back for */
  bool has_body;
  /* the session log */
  double grip_frames_seen, grip_corr, grip_max_m, grip_max_deg;
  char result_tag[3];
} St;
#define ST(w) ((St *)(w)->ext[TFP_WIRE_EXT_ENGINE])
#define W(h) ((TfpWire *)(h)->user)

static void sayf(St *st, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void sayf(St *st, const char *format, ...) {
  char line[900];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  tfp_wire_say(st->w, line);
}
/* The outcome of one in-game check, one line per module of `mask`. Returns ok. */
static bool verdict(St *st, unsigned mask, bool ok, const char *check, const char *observed, const char *expected) {
  for (int m = 0; m < TFP_WE_MODULES; m++) {
    if (!(mask & BIT(m))) continue;
    if (ok) sayf(st, "Check: %s: %s ok observed=%s", module_names[m], check, observed);
    else sayf(st, "Refused: %s: %s observed=%s expected=%s", module_names[m], check, observed, expected);
  }
  return ok;
}
static void cut(char *out, size_t size, const char *text) {
  size_t n = strlen(text);
  if (n >= size) n = size - 1;
  memcpy(out, text, n);
  out[n] = 0;
}

/* ---- the bridge ----------------------------------------------------------------------------- */
static bool ecall(St *st, const char *path, const char *sig, ...) {
  va_list args;
  va_start(args, sig);
  bool ok = st->vm && st->api.call(st->api.user, path, sig, args);
  va_end(args);
  return ok;
}
static void eunref(St *st, TfpEngRef *ref) { if (*ref) st->api.unref(st->api.user, ref); *ref = 0; }
/* The text of a call that failed (a raised Lua error, a result of the wrong kind). */
static bool efail(St *st, char *text, size_t size) {
  cut(text, size, st->vm ? st->api.error(st->api.user) : "The engine's Lua state is not available");
  return false;
}
static bool etext(char *text, size_t size, const char *why) { cut(text, size, why); return false; }

/* ---- handles -------------------------------------------------------------------------------- */
static uint32_t slot_hash(uintptr_t pointer) { return (uint32_t)(((uint64_t)pointer * UINT64_C(0x9E3779B97F4A7C15)) >> (64 - INDEX_BITS)); }
static Slot *slot_find(St *st, uintptr_t pointer) {
  for (uint32_t at = slot_hash(pointer);; at = (at + 1) & (INDEX - 1)) {
    uint32_t i = st->index[at];
    if (!i) return NULL;
    if (st->slots[i - 1].pointer == pointer) return &st->slots[i - 1];
  }
}
static void slot_index(St *st, Slot *slot) {
  uint32_t at = slot_hash(slot->pointer);
  while (st->index[at]) at = (at + 1) & (INDEX - 1);
  st->index[at] = (uint32_t)(slot - st->slots) + 1;
}
static void handles_reset(St *st) { /* a new Lua state: every reference of the old one is dead */
  memset(st->index, 0, sizeof *st->index * INDEX);
  st->used = st->transient_count = st->spare_count = st->kept_count = 0;
  st->units_list = 0;
  st->told_full = false;
}
static bool slot_keep(St *st, Slot *slot) {
  if (slot->state == SLOT_KEPT) return true;
  if (st->kept_count == KEPT_MAX) return false;
  slot->state = SLOT_KEPT;
  st->kept_count++;
  return true;
}
/* The handle of an engine value. `ref` (may be 0) is taken over or released. NULL: the table is full. */
static Slot *slot_intern(St *st, uintptr_t pointer, TfpEngRef ref, bool keep, uint32_t list_index) {
  Slot *slot = slot_find(st, pointer);
  if (slot) {
    if (ref && !slot->ref) slot->ref = ref;
    else eunref(st, &ref);
    if (!slot->ref) slot->list_index = list_index;
  } else {
    if (st->spare_count) slot = &st->slots[st->spare[--st->spare_count]];
    else if (st->used < SLOTS) slot = &st->slots[st->used++];
    else { eunref(st, &ref); return NULL; }
    slot->pointer = pointer, slot->ref = ref, slot->list_index = list_index, slot->state = SLOT_TRANSIENT;
    st->transient[st->transient_count++] = (uint32_t)(slot - st->slots);
    slot_index(st, slot);
  }
  if (keep && !slot_keep(st, slot)) return NULL;
  return slot;
}
static Slot *slot_of(const St *st, const void *handle) {
  const Slot *slot = handle;
  if (!st->slots || !slot || slot < st->slots || slot >= st->slots + st->used) return NULL;
  if (((const char *)slot - (const char *)st->slots) % sizeof *slot || slot->state == SLOT_FREE) return NULL;
  return &st->slots[slot - st->slots];
}
/* The reference to pass a handle to the engine with. A handle used once stays for good. */
static bool slot_ref(St *st, const void *handle, TfpEngRef *out, char *text, size_t size) {
  Slot *slot = slot_of(st, handle);
  *out = 0;
  if (!st->vm) return etext(text, size, "The engine's Lua state is not available");
  if (!slot) return etext(text, size, "An engine handle from an earlier Lua state or inventory was used");
  if (!slot->ref) {
    TfpEngRef ref = 0;
    if (!st->units_list || !st->api.index(st->api.user, st->units_list, (int)slot->list_index, &ref) || !ref ||
        st->api.pointer(st->api.user, ref) != slot->pointer) {
      eunref(st, &ref);
      return etext(text, size, "A Unit of the engine's inventory could not be referenced again");
    }
    slot->ref = ref;
  }
  if (!slot_keep(st, slot)) return etext(text, size, "The engine handle table is full");
  *out = slot->ref;
  return true;
}
/* A value the engine answered, as a handle that stays. NULL with true: nil. */
static bool slot_take(St *st, TfpEngRef ref, const void **handle, char *text, size_t size) {
  *handle = NULL;
  if (!ref) return true;
  uintptr_t pointer = st->api.pointer(st->api.user, ref);
  int type = st->api.type(st->api.user, ref);
  if (!pointer || (type != 2 && type != 7)) { eunref(st, &ref); return etext(text, size, "The engine answered a value that is not a handle"); }
  Slot *slot = slot_intern(st, pointer, ref, true, 0);
  if (!slot) {
    if (!st->told_full) { st->told_full = true; sayf(st, "Refused: the engine handle table is full (%u kept)", st->kept_count); }
    return etext(text, size, "The engine handle table is full");
  }
  *handle = slot;
  return true;
}
/* Units head visibility published in its handle table stay; the rest of the last inventory goes. */
static void handles_publish(St *st) {
  size_t count = 0;
  unsigned long long table = 0;
  if (!st->head || !tfp_head_sdk_handles(st->head, &count, &table)) return;
  for (size_t i = 0; i < count; i++) {
    Slot *slot = slot_of(st, tfp_head_sdk_handle(st->head, i).unit);
    if (slot) slot_keep(st, slot);
  }
}
static void handles_drop_inventory(St *st) {
  handles_publish(st);
  for (uint32_t i = 0; i < st->transient_count; i++) {
    Slot *slot = &st->slots[st->transient[i]];
    if (slot->state != SLOT_TRANSIENT) continue;
    eunref(st, &slot->ref);
    slot->state = SLOT_FREE, slot->pointer = 0;
    st->spare[st->spare_count++] = st->transient[i];
  }
  st->transient_count = 0;
  memset(st->index, 0, sizeof *st->index * INDEX);
  for (uint32_t i = 0; i < st->used; i++) if (st->slots[i].state == SLOT_KEPT) slot_index(st, &st->slots[i]);
  eunref(st, &st->units_list);
}
double tfp_wire_engine_address(const TfpWire *w, const void *handle) {
  const St *st = ST(w);
  const Slot *slot = st ? slot_of(st, handle) : NULL;
  return slot ? (double)slot->pointer : 0;
}

/* ---- shared engine calls -------------------------------------------------------------------- */
static bool main_world(St *st, const void **world, char *text, size_t size) {
  TfpEngRef ref = 0;
  *world = NULL;
  if (!ecall(st, "Application.main_world", ">r", &ref)) return efail(st, text, size);
  return slot_take(st, ref, world, text, size);
}
/* A result that must be a Boolean (hv:191, 196: anything else is "unavailable", reported as a failed call). */
static bool truth(St *st, TfpEngRef *ref, const char *what, bool *out, char *text, size_t size) {
  bool is = *ref && st->api.type(st->api.user, *ref) == 1 && st->api.boolean(st->api.user, *ref, out);
  eunref(st, ref);
  if (!is) { snprintf(text, size, "%s did not answer a Boolean", what); return false; }
  return true;
}
/* `== true` of the Lua (ap:50, gh:488): any other answer is not true. */
static bool exactly_true(St *st, TfpEngRef *ref) {
  bool value = false;
  bool is = *ref && st->api.type(st->api.user, *ref) == 1 && st->api.boolean(st->api.user, *ref, &value) && value;
  eunref(st, ref);
  return is;
}

/* ---- TfpEngineIface: head visibility -------------------------------------------------------- */
#define HEAD_ERROR error->text, sizeof error->text
static bool hi_main_world(void *user, TfpEngineWorld *world, TfpEngineError *error) {
  const void *handle = NULL;
  bool ok = main_world(user, &handle, HEAD_ERROR);
  *world = handle;
  return ok;
}
static bool hi_units(void *user, TfpEngineWorld world, const TfpEngineUnit **units, size_t *count, TfpEngineError *error) {
  St *st = user;
  TfpEngRef world_ref, list = 0;
  *units = NULL, *count = 0;
  if (!slot_ref(st, world, &world_ref, HEAD_ERROR)) return false;
  if (!ecall(st, "World.units", "r>r", world_ref, &list)) return efail(st, HEAD_ERROR);
  if (!list || st->api.type(st->api.user, list) != 5) { eunref(st, &list); return true; }
  handles_drop_inventory(st);
  st->units_list = list;
  int n = st->api.userdata_array(st->api.user, list, st->pointers, UNITS_MAX);
  if (n < 0) return true;
  *units = (const TfpEngineUnit *)st->units;
  *count = (size_t)n;
  if (n > UNITS_MAX) return true; /* hv:374 refuses a list this long before it looks at an entry */
  for (int i = 0; i < n; i++) {
    Slot *slot = st->pointers[i] ? slot_intern(st, st->pointers[i], 0, false, (uint32_t)i + 1) : NULL;
    if (!slot) return etext(HEAD_ERROR, st->pointers[i] ? "The engine handle table is full" : "World.units answered a value that is not a Unit");
    st->units[i] = (const struct TfpEngineUnitObject *)slot;
  }
  return true;
}
static bool hi_set_mesh_visibility(void *user, TfpEngineUnit unit, int mesh_index, bool visible, const char *pass_name, TfpEngineError *error) {
  St *st = user;
  TfpEngRef ref;
  if (!slot_ref(st, unit, &ref, HEAD_ERROR)) return false;
  return ecall(st, "Unit.set_mesh_visibility", "ribs", ref, mesh_index, visible ? 1 : 0, pass_name) || efail(st, HEAD_ERROR);
}
static bool hi_has_mesh(void *user, TfpEngineUnit unit, const char *mesh_name, bool *present, TfpEngineError *error) {
  St *st = user;
  TfpEngRef ref, answer = 0;
  if (!slot_ref(st, unit, &ref, HEAD_ERROR)) return false;
  if (!ecall(st, "Unit.has_mesh", "rs>r", ref, mesh_name, &answer)) return efail(st, HEAD_ERROR);
  return truth(st, &answer, "Unit.has_mesh", present, HEAD_ERROR);
}
static bool hi_mesh(void *user, TfpEngineUnit unit, const char *mesh_name, TfpEngineMesh *mesh, TfpEngineError *error) {
  St *st = user;
  TfpEngRef ref, answer = 0;
  const void *handle = NULL;
  *mesh = NULL;
  if (!slot_ref(st, unit, &ref, HEAD_ERROR)) return false;
  if (!ecall(st, "Unit.mesh", "rs>r", ref, mesh_name, &answer)) return efail(st, HEAD_ERROR);
  if (!slot_take(st, answer, &handle, HEAD_ERROR)) return false;
  *mesh = handle;
  return true;
}
static bool mesh_number(St *st, const char *path, const void *mesh, double *out, char *text, size_t size) {
  TfpEngRef ref;
  if (!slot_ref(st, mesh, &ref, text, size)) return false;
  return ecall(st, path, "r>n", ref, out) || efail(st, text, size);
}
static bool hi_mesh_name(void *user, TfpEngineMesh mesh, double *name, TfpEngineError *error) { return mesh_number(user, "Mesh.name", mesh, name, HEAD_ERROR); }
static bool hi_mesh_node(void *user, TfpEngineMesh mesh, double *node, TfpEngineError *error) { return mesh_number(user, "Mesh.node", mesh, node, HEAD_ERROR); }
static bool hi_mesh_materials(void *user, TfpEngineMesh mesh, double *count, TfpEngineError *error) {
  return mesh_number(user, "Mesh.num_materials", mesh, count, HEAD_ERROR);
}
static bool hi_mesh_has_material(void *user, TfpEngineMesh mesh, const char *material_name, bool *present, TfpEngineError *error) {
  St *st = user;
  TfpEngRef ref, answer = 0;
  if (!slot_ref(st, mesh, &ref, HEAD_ERROR)) return false;
  if (!ecall(st, "Mesh.has_material", "rs>r", ref, material_name, &answer)) return efail(st, HEAD_ERROR);
  return truth(st, &answer, "Mesh.has_material", present, HEAD_ERROR);
}
const TfpEngineIface *tfp_wire_engine_head_iface(const TfpWire *w) { return ST(w) ? &ST(w)->head_engine : NULL; }

/* ---- TfpArmEngine: sleeves and grips -------------------------------------------------------- */
#define ARM_ERROR error->text, sizeof error->text
#define AU St *st = ((ArmUser *)user)->st
/* A pose is the rotation in m[0..3] and the position in m[4..6] (tfp_wire_engine_maths). */
static float pose_angle_degrees(const float q[4]) {
  double w = fabs((double)q[3]);
  return (float)(2 * acos(w > 1 ? 1 : w) * 57.29577951308232);
}
static bool ai_main_world(void *user, TfpArmWorld *world, TfpArmError *error) {
  AU;
  const void *handle = NULL;
  bool ok = main_world(st, &handle, ARM_ERROR);
  *world = handle;
  return ok;
}
/* The link word of a unit's node: low 16 bits the mode (2: linked to a bone of the parent unit),
 * high 16 the bone (ap:73-77). */
static bool link_word(St *st, const void *unit, double node, uint32_t *word) {
  Slot *slot = slot_of(st, unit);
  uint64_t object, links;
  uint32_t count;
  char why[160];
  if (!slot || !st->has_body || !(node >= 1 && node <= 255)) return false;
  if (!st->memory.object(st->memory.user, (uint32_t)(slot->pointer / 4), st->body.has_owner, st->body.avatar_owner, st->body.has_owner,
                         st->body.avatar_index, &object, why, sizeof why))
    return false;
  if (!st->memory.read(st->memory.user, object + 0x70, &count, 4) || node > count) return false;
  if (!st->memory.read(st->memory.user, object + 0x98, &links, 8)) return false;
  return st->memory.read(st->memory.user, links + ((uint64_t)node - 1) * 4, word, 4);
}
static bool ai_link_unit(void *user, TfpArmWorld world, TfpArmUnit child, double child_node, TfpArmUnit parent, double parent_node,
                         TfpArmError *error) {
  AU;
  TfpEngRef world_ref, child_ref, parent_ref;
  if (st->link_failed) return etext(ARM_ERROR, "World.link_unit is off: its first link did not read back");
  if (!slot_ref(st, world, &world_ref, ARM_ERROR) || !slot_ref(st, child, &child_ref, ARM_ERROR) || !slot_ref(st, parent, &parent_ref, ARM_ERROR))
    return false;
  uint32_t before = 0, after = 0;
  bool first = !st->link_checked, had = first && link_word(st, child, child_node, &before);
  if (!ecall(st, "World.link_unit", "rrnrn", world_ref, child_ref, child_node, parent_ref, parent_node)) return efail(st, ARM_ERROR);
  if (!first) return true;
  /* The argument order (world, child, child node, parent, parent node) was never used on this
   * build: the first link must show in the child's link table before another one is made. */
  uint32_t expected = 2u | ((uint32_t)(parent_node - 1) << 16);
  bool have = link_word(st, child, child_node, &after);
  char observed[96], wanted[64];
  if (have) snprintf(observed, sizeof observed, "%08x (before %s%08x)", after, had ? "" : "unread ", before);
  else snprintf(observed, sizeof observed, "unreadable");
  snprintf(wanted, sizeof wanted, "%08x (mode 2, bone %.0f)", expected, parent_node - 1);
  if (verdict(st, SLEEVES, have && after == expected, "link readback", observed, wanted)) { st->link_checked = true; return true; }
  st->link_failed = true;
  /* Retail's undo is to link the bone back to where the game had it (ap:96-108). */
  if (had && have && after != before && (before & 0xffff) == 2) {
    bool again = ecall(st, "World.link_unit", "rrnrn", world_ref, child_ref, child_node, parent_ref, (double)(before >> 16) + 1);
    uint32_t now = 0;
    bool read = link_word(st, child, child_node, &now);
    sayf(st, "Undo: arm pieces: the link was put back to bone %u: %s, link word now %08x%s", before >> 16, again ? "called" : "the call failed", now,
         read ? (now == before ? " (as before)" : " (not as before)") : " (unreadable)");
  }
  return etext(ARM_ERROR, "World.link_unit did not read back; the sleeves are off");
}
static bool ai_alive(void *user, TfpArmUnit unit, bool *alive, TfpArmError *error) {
  AU;
  TfpEngRef ref, answer = 0;
  if (!slot_ref(st, unit, &ref, ARM_ERROR)) return false;
  if (!ecall(st, "Unit.alive", "r>r", ref, &answer)) return efail(st, ARM_ERROR);
  *alive = exactly_true(st, &answer);
  return true;
}
static bool unit_number(St *st, const char *path, const void *unit, double *out, char *text, size_t size) {
  TfpEngRef ref;
  if (!slot_ref(st, unit, &ref, text, size)) return false;
  return ecall(st, path, "r>n", ref, out) || efail(st, text, size);
}
static bool ai_items(void *user, TfpArmUnit unit, double *count, TfpArmError *error) {
  return unit_number(((ArmUser *)user)->st, "Unit.num_scene_graph_items", unit, count, ARM_ERROR);
}
static bool ai_world_position(void *user, TfpArmUnit unit, double node, TfpArmVector3 *position, TfpArmError *error) {
  AU;
  TfpEngRef ref;
  float v[3];
  if (!slot_ref(st, unit, &ref, ARM_ERROR)) return false;
  if (!ecall(st, "Unit.world_position", "rn>v", ref, node, v)) return efail(st, ARM_ERROR);
  position->x = v[0], position->y = v[1], position->z = v[2];
  return true;
}
static bool ai_world_rotation(void *user, TfpArmUnit unit, double node, TfpArmQuaternion *rotation, TfpArmError *error) {
  AU;
  TfpEngRef ref;
  float q[4];
  if (!slot_ref(st, unit, &ref, ARM_ERROR)) return false;
  if (!ecall(st, "Unit.world_rotation", "rn>q", ref, node, q)) return efail(st, ARM_ERROR);
  rotation->x = q[0], rotation->y = q[1], rotation->z = q[2], rotation->w = q[3];
  return true;
}
/* Unit.world_pose as its rotation and its position (the engine's own two readers of the same node). */
static bool ai_world_pose(void *user, TfpArmUnit unit, double node, TfpArmMatrix4x4 *pose, TfpArmError *error) {
  TfpArmQuaternion q;
  TfpArmVector3 p;
  if (!ai_world_rotation(user, unit, node, &q, error) || !ai_world_position(user, unit, node, &p, error)) return false;
  memset(pose, 0, sizeof *pose);
  pose->m[0] = q.x, pose->m[1] = q.y, pose->m[2] = q.z, pose->m[3] = q.w, pose->m[4] = p.x, pose->m[5] = p.y, pose->m[6] = p.z;
  return true;
}
static bool ai_set_local_pose(void *user, TfpArmUnit unit, double node, const TfpArmMatrix4x4 *pose, TfpArmError *error) {
  ArmUser *u = user;
  St *st = u->st;
  TfpEngRef ref, matrix = 0;
  if (u->module == TFP_WE_SLEEVES && st->link_failed) return etext(ARM_ERROR, "The sleeves are off: the first link did not read back");
  if (!slot_ref(st, unit, &ref, ARM_ERROR)) return false;
  if (!ecall(st, "Matrix4x4.from_quaternion_position", "qv>r", &pose->m[0], &pose->m[4], &matrix) || !matrix) return efail(st, ARM_ERROR);
  bool ok = ecall(st, "Unit.set_local_pose", "rnr", ref, node, matrix);
  eunref(st, &matrix);
  if (!ok) return efail(st, ARM_ERROR);
  if (u->module == TFP_WE_GRIP) { /* the session log: the largest offset a grip wrote this second */
    double m = sqrt((double)pose->m[4] * pose->m[4] + (double)pose->m[5] * pose->m[5] + (double)pose->m[6] * pose->m[6]);
    double degrees = pose_angle_degrees(pose->m);
    if (m > st->grip_max_m) st->grip_max_m = m;
    if (degrees > st->grip_max_deg) st->grip_max_deg = degrees;
  }
  return true;
}
static bool ai_num_meshes(void *user, TfpArmUnit unit, double *count, TfpArmError *error) {
  return unit_number(((ArmUser *)user)->st, "Unit.num_meshes", unit, count, ARM_ERROR);
}
static bool ai_mesh(void *user, TfpArmUnit unit, double mesh_index, TfpArmMesh *mesh, TfpArmError *error) {
  AU;
  TfpEngRef ref, answer = 0;
  const void *handle = NULL;
  *mesh = NULL;
  if (!slot_ref(st, unit, &ref, ARM_ERROR)) return false;
  if (!ecall(st, "Unit.mesh", "rn>r", ref, mesh_index, &answer)) return efail(st, ARM_ERROR);
  if (!slot_take(st, answer, &handle, ARM_ERROR)) return false;
  *mesh = handle;
  return true;
}
static bool ai_set_mesh_visibility(void *user, TfpArmUnit unit, double mesh_index, bool visible, TfpArmError *error) {
  AU;
  TfpEngRef ref;
  if (!slot_ref(st, unit, &ref, ARM_ERROR)) return false;
  return ecall(st, "Unit.set_mesh_visibility", "rnb", ref, mesh_index, visible ? 1 : 0) || efail(st, ARM_ERROR);
}
static bool ai_mesh_visibility(void *user, TfpArmMesh mesh, bool *visible, TfpArmError *error) {
  AU;
  TfpEngRef ref, answer = 0;
  if (!slot_ref(st, mesh, &ref, ARM_ERROR)) return false;
  if (!ecall(st, "Mesh.visibility", "r>r", ref, &answer)) return efail(st, ARM_ERROR);
  *visible = exactly_true(st, &answer);
  return true;
}
static bool ai_mesh_materials(void *user, TfpArmMesh mesh, double *count, TfpArmError *error) {
  return mesh_number(((ArmUser *)user)->st, "Mesh.num_materials", mesh, count, ARM_ERROR);
}
const TfpArmEngine *tfp_wire_engine_arm_iface(const TfpWire *w, TfpWireEngineModule module) { return ST(w) ? &ST(w)->arm_engine[module] : NULL; }

/* ---- the value maths ------------------------------------------------------------------------
 * tools/retail_ref/arms_common.lua line for line (tests/arms_fx.h is the same in C): products and
 * sums in double, each result rounded to float. Build with -ffp-contract=off. */
typedef TfpArmVector3 V3;
typedef TfpArmQuaternion Q4;
typedef TfpArmMatrix4x4 M4;
#define MATHS (void)user; (void)error
static Q4 m_q(const M4 *m) { return (Q4){m->m[0], m->m[1], m->m[2], m->m[3]}; }
static V3 m_p(const M4 *m) { return (V3){m->m[4], m->m[5], m->m[6]}; }
static M4 m_of(Q4 q, V3 p) {
  M4 m;
  memset(&m, 0, sizeof m);
  m.m[0] = q.x, m.m[1] = q.y, m.m[2] = q.z, m.m[3] = q.w, m.m[4] = p.x, m.m[5] = p.y, m.m[6] = p.z;
  return m;
}
static Q4 q_mul(Q4 a, Q4 b) { /* Quaternion.multiply(second, first) */
  double ax = a.x, ay = a.y, az = a.z, aw = a.w, bx = b.x, by = b.y, bz = b.z, bw = b.w;
  Q4 r;
  r.x = (float)(aw * bx + ax * bw + ay * bz - az * by);
  r.y = (float)(aw * by - ax * bz + ay * bw + az * bx);
  r.z = (float)(aw * bz + ax * by - ay * bx + az * bw);
  r.w = (float)(aw * bw - ax * bx - ay * by - az * bz);
  return r;
}
static V3 q_rot(Q4 a, V3 p) {
  Q4 r = q_mul(q_mul(a, (Q4){p.x, p.y, p.z, 0}), (Q4){-a.x, -a.y, -a.z, a.w});
  return (V3){r.x, r.y, r.z};
}
static double v_len(V3 a) {
  double x = a.x, y = a.y, z = a.z;
  return (float)sqrt(x * x + y * y + z * z);
}
static V3 v_norm(V3 a) {
  double n = 1 / v_len(a);
  return (V3){(float)(a.x * n), (float)(a.y * n), (float)(a.z * n)};
}
static M4 m_mul(const M4 *a, const M4 *b) { /* Matrix4x4.multiply(a, b): a, then b */
  V3 p = q_rot(m_q(b), m_p(a)), t = m_p(b);
  return m_of(q_mul(m_q(b), m_q(a)), (V3){(float)((double)p.x + t.x), (float)((double)p.y + t.y), (float)((double)p.z + t.z)});
}
static M4 m_inv(const M4 *a) {
  Q4 q = m_q(a), i = {-q.x, -q.y, -q.z, q.w};
  V3 p = m_p(a);
  return m_of(i, q_rot(i, (V3){-p.x, -p.y, -p.z}));
}
static Q4 q_axis(V3 axis, double angle) {
  V3 a = v_norm(axis);
  double s = sin(angle / 2);
  return (Q4){(float)(a.x * s), (float)(a.y * s), (float)(a.z * s), (float)cos(angle / 2)};
}
static Q4 q_lerp(Q4 a, Q4 b, double t) {
  if (t >= 1) return b;
  if (t <= 0) return a;
  double ax = a.x, ay = a.y, az = a.z, aw = a.w;
  double x = ax + (b.x - ax) * t, y = ay + (b.y - ay) * t, z = az + (b.z - az) * t, w = aw + (b.w - aw) * t;
  double n = sqrt(x * x + y * y + z * z + w * w);
  return (Q4){(float)(x / n), (float)(y / n), (float)(z / n), (float)(w / n)};
}
static V3 v_cross(V3 a, V3 b) {
  double ax = a.x, ay = a.y, az = a.z;
  return (V3){(float)(ay * b.z - az * b.y), (float)(az * b.x - ax * b.z), (float)(ax * b.y - ay * b.x)};
}
static V3 v_lerp(V3 a, V3 b, double t) {
  double ax = a.x, ay = a.y, az = a.z;
  return (V3){(float)(ax + (b.x - ax) * t), (float)(ay + (b.y - ay) * t), (float)(az + (b.z - az) * t)};
}
static double v_dot(V3 a, V3 b) {
  double ax = a.x, ay = a.y, az = a.z;
  return (float)(ax * b.x + ay * b.y + az * b.z);
}
static bool mv_call(void *user, double x, double y, double z, V3 *out, TfpArmError *error) { MATHS; *out = (V3){(float)x, (float)y, (float)z}; return true; }
static bool mv_length(void *user, V3 v, double *out, TfpArmError *error) { MATHS; *out = v_len(v); return true; }
static bool mv_dot(void *user, V3 a, V3 b, double *out, TfpArmError *error) { MATHS; *out = v_dot(a, b); return true; }
static bool mv_cross(void *user, V3 a, V3 b, V3 *out, TfpArmError *error) { MATHS; *out = v_cross(a, b); return true; }
static bool mv_normalize(void *user, V3 v, V3 *out, TfpArmError *error) { MATHS; *out = v_norm(v); return true; }
static bool mv_lerp(void *user, V3 a, V3 b, double t, V3 *out, TfpArmError *error) { MATHS; *out = v_lerp(a, b, t); return true; }
static bool mv_x(void *user, V3 v, double *out, TfpArmError *error) { MATHS; *out = v.x; return true; }
static bool mv_y(void *user, V3 v, double *out, TfpArmError *error) { MATHS; *out = v.y; return true; }
static bool mv_z(void *user, V3 v, double *out, TfpArmError *error) { MATHS; *out = v.z; return true; }
static bool mv_add(void *user, V3 a, V3 b, V3 *out, TfpArmError *error) {
  MATHS; *out = (V3){(float)((double)a.x + b.x), (float)((double)a.y + b.y), (float)((double)a.z + b.z)}; return true;
}
static bool mv_sub(void *user, V3 a, V3 b, V3 *out, TfpArmError *error) {
  MATHS; *out = (V3){(float)((double)a.x - b.x), (float)((double)a.y - b.y), (float)((double)a.z - b.z)}; return true;
}
static bool mv_mul(void *user, V3 v, double n, V3 *out, TfpArmError *error) { MATHS; *out = (V3){(float)(v.x * n), (float)(v.y * n), (float)(v.z * n)}; return true; }
static bool mv_unm(void *user, V3 v, V3 *out, TfpArmError *error) { MATHS; *out = (V3){-v.x, -v.y, -v.z}; return true; }
static bool mq_identity(void *user, Q4 *out, TfpArmError *error) { MATHS; *out = (Q4){0, 0, 0, 1}; return true; }
static bool mq_axis_angle(void *user, V3 axis, double angle, Q4 *out, TfpArmError *error) { MATHS; *out = q_axis(axis, angle); return true; }
static bool mq_multiply(void *user, Q4 a, Q4 b, Q4 *out, TfpArmError *error) { MATHS; *out = q_mul(a, b); return true; }
static bool mq_rotate(void *user, Q4 q, V3 v, V3 *out, TfpArmError *error) { MATHS; *out = q_rot(q, v); return true; }
static bool mq_lerp(void *user, Q4 a, Q4 b, double t, Q4 *out, TfpArmError *error) { MATHS; *out = q_lerp(a, b, t); return true; }
static bool mq_inverse(void *user, Q4 q, Q4 *out, TfpArmError *error) { MATHS; *out = (Q4){-q.x, -q.y, -q.z, q.w}; return true; }
static bool mq_to_elements(void *user, Q4 q, double out[4], TfpArmError *error) { MATHS; out[0] = q.x, out[1] = q.y, out[2] = q.z, out[3] = q.w; return true; }
static bool mq_from_elements(void *user, double x, double y, double z, double w, Q4 *out, TfpArmError *error) {
  MATHS; *out = (Q4){(float)x, (float)y, (float)z, (float)w}; return true;
}
static bool mm_identity(void *user, M4 *out, TfpArmError *error) { MATHS; *out = m_of((Q4){0, 0, 0, 1}, (V3){0, 0, 0}); return true; }
static bool mm_from(void *user, Q4 q, V3 position, M4 *out, TfpArmError *error) { MATHS; *out = m_of(q, position); return true; }
static bool mm_multiply(void *user, const M4 *a, const M4 *b, M4 *out, TfpArmError *error) { MATHS; *out = m_mul(a, b); return true; }
static bool mm_inverse(void *user, const M4 *m, M4 *out, TfpArmError *error) { MATHS; *out = m_inv(m); return true; }
static bool mm_translation(void *user, const M4 *m, V3 *out, TfpArmError *error) { MATHS; *out = m_p(m); return true; }
static bool mm_rotation(void *user, const M4 *m, Q4 *out, TfpArmError *error) { MATHS; *out = m_q(m); return true; }
void tfp_wire_engine_maths(TfpArmEngine *e) {
  e->Vector3_call = mv_call, e->Vector3_length = mv_length, e->Vector3_dot = mv_dot, e->Vector3_cross = mv_cross;
  e->Vector3_normalize = mv_normalize, e->Vector3_lerp = mv_lerp, e->Vector3_x = mv_x, e->Vector3_y = mv_y, e->Vector3_z = mv_z;
  e->Vector3_op_add = mv_add, e->Vector3_op_sub = mv_sub, e->Vector3_op_mul = mv_mul, e->Vector3_op_unm = mv_unm;
  e->Quaternion_identity = mq_identity, e->Quaternion_axis_angle = mq_axis_angle, e->Quaternion_multiply = mq_multiply;
  e->Quaternion_rotate = mq_rotate, e->Quaternion_lerp = mq_lerp, e->Quaternion_inverse = mq_inverse;
  e->Quaternion_to_elements = mq_to_elements, e->Quaternion_from_elements = mq_from_elements;
  e->Matrix4x4_identity = mm_identity, e->Matrix4x4_from_quaternion_position = mm_from, e->Matrix4x4_multiply = mm_multiply;
  e->Matrix4x4_inverse = mm_inverse, e->Matrix4x4_translation = mm_translation, e->Matrix4x4_rotation = mm_rotation;
}

/* ---- memory --------------------------------------------------------------------------------- */
static bool default_read(void *user, uint64_t at, void *out, size_t size) {
  St *st = user;
  TfpOpsText why;
  why.present = false, why.text[0] = 0;
  if (tfp_ops_read(&st->w->ops, at, out, size, &why)) return true;
  cut(st->why, sizeof st->why, why.text[0] ? why.text : "Memory unavailable");
  return false;
}
static bool default_object(void *user, uint32_t unit, bool has_owner, uint64_t owner, bool has_index, uint32_t index, uint64_t *object, char *text,
                           size_t size) {
  St *st = user;
  TfpOpsContext context;
  TfpOpsText why;
  why.present = false, why.text[0] = 0;
  if (!tfp_ops_alignment_context(&st->w->ops, unit, has_owner, owner, has_index, index, false, &context, &why))
    return etext(text, size, why.text[0] ? why.text : "Unit unavailable");
  *object = context.object;
  return true;
}
static bool mread(St *st, uint64_t at, void *out, size_t size) {
  if (st->memory.read(st->memory.user, at, out, size)) return true;
  if (st->memory.read != default_read) cut(st->why, sizeof st->why, "Memory unavailable");
  return false;
}
static bool mobject(St *st, const TfpWireEngineBody *s, uint32_t unit, uint64_t *object) {
  return st->memory.object(st->memory.user, unit, s->has_owner, s->avatar_owner, s->has_owner, s->avatar_index, object, st->why, sizeof st->why);
}
static void hex_of(const uint8_t *bytes, size_t n, char *out) {
  for (size_t i = 0; i < n; i++) snprintf(out + i * 2, 3, "%02x", bytes[i]);
}

/* ---- in-game checks: head visibility --------------------------------------------------------
 * Read-only. The first failure ends them with one Refused line. */
bool tfp_wire_engine_check_head(TfpWire *w, const TfpWireEngineBody *s) {
  St *st = ST(w);
  const TfpHeadBuild *b = &tfp_head_build_25480438;
  const unsigned M = HEAD;
  char observed[200], expected[200], text[200];
  /* (e) the three fields of the build table that rested on the kit's records (VISIBILITY-TRANSLATION.md, open issue 3) */
  uint8_t code[22];
  uint32_t id = 0;
  bool read = mread(st, st->exe + b->registry_code, code, sizeof code);
  if (read) hex_of(code, sizeof code, observed); else snprintf(observed, sizeof observed, "unreadable");
  hex_of(b->registry_code_bytes, sizeof code, expected);
  if (!verdict(st, M, read && !memcmp(code, b->registry_code_bytes, sizeof code), "unit registry code", observed, expected)) return false;
  for (int i = 0; i < 2; i++) {
    uint32_t at = i ? b->context_shadow : b->context_default, value = i ? b->context_shadow_value : b->context_default_value;
    read = mread(st, st->exe + at, &id, 4);
    if (read) snprintf(observed, sizeof observed, "%08x at exe+%#x", id, at); else snprintf(observed, sizeof observed, "unreadable at exe+%#x", at);
    snprintf(expected, sizeof expected, "%08x", value);
    if (!verdict(st, M, read && id == value, i ? "shadow pass id" : "default pass id", observed, expected)) return false;
  }
  /* (d) the World handle: its userdata holds the native world (hv:51) */
  const void *world = NULL;
  uint64_t native = 0;
  text[0] = 0;
  if (!main_world(st, &world, text, sizeof text) || !world) return verdict(st, M, false, "world handle", text[0] ? text : "nil", "a World userdata");
  uint64_t world_at = (uint64_t)slot_of(st, world)->pointer;
  read = mread(st, world_at, &native, 8);
  snprintf(observed, sizeof observed, read ? "%#llx behind the userdata at %#llx" : "unreadable (%llx) at %#llx", (unsigned long long)native,
           (unsigned long long)world_at);
  snprintf(expected, sizeof expected, "%#llx (the snapshot's world)", (unsigned long long)s->world);
  if (!verdict(st, M, read && native == s->world, "world handle", observed, expected)) return false;
  /* (d) a Unit handle: its address is the unit reference times 4 plus tag bits (hv:49) */
  const TfpEngineUnit *units = NULL;
  size_t count = 0, matches = 0;
  TfpEngineError error = {{0}};
  const void *body = NULL;
  if (!hi_units(st, world, &units, &count, &error) || !units || count > UNITS_MAX) {
    snprintf(observed, sizeof observed, "%.160s (%zu units)", error.text[0] ? error.text : "no list", count);
    return verdict(st, M, false, "unit inventory", observed, "a list of at most 131072 Units");
  }
  for (size_t i = 0; i < count; i++)
    if (floor((double)slot_of(st, units[i])->pointer / 4) == (double)s->body) { if (!matches++) body = units[i]; }
  if (matches) snprintf(observed, sizeof observed, "%#llx (tag %u) once among %zu units", (unsigned long long)slot_of(st, body)->pointer,
                        (unsigned)(slot_of(st, body)->pointer & 3), count);
  else snprintf(observed, sizeof observed, "none of %zu units (the first is %#llx)", count, count ? (unsigned long long)slot_of(st, units[0])->pointer : 0ull);
  if (matches > 1) snprintf(observed, sizeof observed, "%zu of %zu units", matches, count);
  snprintf(expected, sizeof expected, "one handle whose address / 4 is the body's reference %#x", s->body);
  if (!verdict(st, M, matches == 1, "unit handle", observed, expected)) return false;
  /* The unit object the backend reaches natively must be the unit the handle names. */
  uint64_t object = 0, meshes_at = 0, first_mesh = 0;
  uint32_t self = 0, nodes = 0, meshes = 0;
  double engine_nodes = -1, engine_meshes = -1;
  bool have = mobject(st, s, s->body, &object) && mread(st, object + 8, &self, 4) && mread(st, object + 0x70, &nodes, 4) &&
              mread(st, object + 0xC8, &meshes, 4);
  bool asked = unit_number(st, "Unit.num_scene_graph_items", body, &engine_nodes, text, sizeof text) &&
               unit_number(st, "Unit.num_meshes", body, &engine_meshes, text, sizeof text);
  if (!have) snprintf(observed, sizeof observed, "unreadable (%.160s)", st->why);
  else if (!asked) snprintf(observed, sizeof observed, "%s", text);
  else snprintf(observed, sizeof observed, "object %#llx: reference %#x, %u nodes, %u meshes; engine: %.0f nodes, %.0f meshes", (unsigned long long)object, self,
                nodes, meshes, engine_nodes, engine_meshes);
  snprintf(expected, sizeof expected, "reference %#x and the engine's counts at +0x70 and +0xC8", s->body);
  if (!verdict(st, M, have && asked && self == s->body && (double)nodes == engine_nodes && (double)meshes == engine_meshes && meshes >= 1 && meshes <= 128,
               "unit object", observed, expected))
    return false;
  /* (d) a Mesh handle: its address is the mesh object (hv:50), the first entry of the unit's mesh array */
  TfpEngRef body_ref, mesh_ref = 0;
  have = mread(st, object + 0xD0, &meshes_at, 8) && mread(st, meshes_at, &first_mesh, 8);
  asked = slot_ref(st, body, &body_ref, text, sizeof text) && (ecall(st, "Unit.mesh", "ri>r", body_ref, 1, &mesh_ref) || efail(st, text, sizeof text));
  uintptr_t mesh_at = asked && mesh_ref ? st->api.pointer(st->api.user, mesh_ref) : 0;
  int mesh_type = asked && mesh_ref ? st->api.type(st->api.user, mesh_ref) : 0;
  eunref(st, &mesh_ref);
  if (!have) snprintf(observed, sizeof observed, "mesh array unreadable (%.160s)", st->why);
  else if (!asked) snprintf(observed, sizeof observed, "%s", text);
  else snprintf(observed, sizeof observed, "%#llx (Lua type %d)", (unsigned long long)mesh_at, mesh_type);
  snprintf(expected, sizeof expected, "%#llx (the first mesh object of the unit)", (unsigned long long)first_mesh);
  return verdict(st, M, have && asked && mesh_at && mesh_at == first_mesh, "mesh handle", observed, expected);
}

/* ---- in-game checks: the value maths --------------------------------------------------------
 * The 27 value functions are computed here, not by the engine: the engine is asked once for a few
 * values that tell its conventions apart, and must agree. Pure calls. */
static double q_gap(Q4 a, const float b[4]) { /* a rotation and its negation are the same rotation */
  double same = 0, flipped = 0;
  const float av[4] = {a.x, a.y, a.z, a.w};
  for (int i = 0; i < 4; i++) {
    if (fabs((double)av[i] - b[i]) > same) same = fabs((double)av[i] - b[i]);
    if (fabs((double)av[i] + b[i]) > flipped) flipped = fabs((double)av[i] + b[i]);
  }
  return same < flipped ? same : flipped;
}
static double v_gap(V3 a, const float b[3]) {
  double x = fabs((double)a.x - b[0]), y = fabs((double)a.y - b[1]), z = fabs((double)a.z - b[2]);
  return x > y ? (x > z ? x : z) : (y > z ? y : z);
}
bool tfp_wire_engine_check_maths(TfpWire *w) {
  St *st = ST(w);
  const Q4 a = q_axis((V3){.3f, -.5f, .8f}, 1.1), b = q_axis((V3){-.7f, .2f, .4f}, 1.25);
  const V3 p = {.31f, -1.7f, .9f}, t = {-2.5f, .4f, 1.2f};
  const float af[4] = {a.x, a.y, a.z, a.w}, bf[4] = {b.x, b.y, b.z, b.w}, pf[3] = {p.x, p.y, p.z}, tf[3] = {t.x, t.y, t.z};
  const float axis[3] = {.3f, -.5f, .8f};
  const M4 ma = m_of(a, p), mb = m_of(b, t), mul = m_mul(&ma, &mb), inv = m_inv(&ma);
  float q[4], v[3];
  double n = 0, worst = 0, gap;
  const char *worst_name = "none", *failed = NULL;
  int compared = 0;
  TfpEngRef ea = 0, eb = 0, em = 0, ei = 0;
#define GAP(name, value) do { gap = (value); compared++; if (gap > worst) { worst = gap; worst_name = name; } } while (0)
#define ASK(name, ...) if (!failed && !ecall(st, name, __VA_ARGS__)) failed = name
  ASK("Quaternion.multiply", "qq>q", af, bf, q); if (!failed) GAP("Quaternion.multiply", q_gap(q_mul(a, b), q));
  ASK("Quaternion.rotate", "qv>v", af, pf, v); if (!failed) GAP("Quaternion.rotate", v_gap(q_rot(a, p), v));
  ASK("Quaternion.axis_angle", "vn>q", axis, 1.1, q); if (!failed) GAP("Quaternion.axis_angle", q_gap(a, q));
  ASK("Quaternion.inverse", "q>q", af, q); if (!failed) GAP("Quaternion.inverse", q_gap((Q4){-a.x, -a.y, -a.z, a.w}, q));
  ASK("Quaternion.lerp", "qqn>q", af, bf, .25, q); if (!failed) GAP("Quaternion.lerp", q_gap(q_lerp(a, b, .25), q));
  ASK("Vector3.cross", "vv>v", pf, tf, v); if (!failed) GAP("Vector3.cross", v_gap(v_cross(p, t), v));
  ASK("Vector3.lerp", "vvn>v", pf, tf, .25, v); if (!failed) GAP("Vector3.lerp", v_gap(v_lerp(p, t, .25), v));
  ASK("Vector3.normalize", "v>v", pf, v); if (!failed) GAP("Vector3.normalize", v_gap(v_norm(p), v));
  ASK("Vector3.dot", "vv>n", pf, tf, &n); if (!failed) GAP("Vector3.dot", fabs(n - v_dot(p, t)));
  ASK("Vector3.length", "v>n", pf, &n); if (!failed) GAP("Vector3.length", fabs(n - v_len(p)));
  ASK("Matrix4x4.from_quaternion_position", "qv>r", af, pf, &ea);
  ASK("Matrix4x4.from_quaternion_position", "qv>r", bf, tf, &eb);
  ASK("Matrix4x4.rotation", "r>q", ea, q); if (!failed) GAP("Matrix4x4.rotation", q_gap(a, q));
  ASK("Matrix4x4.translation", "r>v", ea, v); if (!failed) GAP("Matrix4x4.translation", v_gap(p, v));
  ASK("Matrix4x4.multiply", "rr>r", ea, eb, &em);
  ASK("Matrix4x4.rotation", "r>q", em, q); if (!failed) GAP("Matrix4x4.multiply (rotation)", q_gap(m_q(&mul), q));
  ASK("Matrix4x4.translation", "r>v", em, v); if (!failed) GAP("Matrix4x4.multiply (position)", v_gap(m_p(&mul), v));
  ASK("Matrix4x4.inverse", "r>r", ea, &ei);
  ASK("Matrix4x4.rotation", "r>q", ei, q); if (!failed) GAP("Matrix4x4.inverse (rotation)", q_gap(m_q(&inv), q));
  ASK("Matrix4x4.translation", "r>v", ei, v); if (!failed) GAP("Matrix4x4.inverse (position)", v_gap(m_p(&inv), v));
#undef ASK
#undef GAP
  eunref(st, &ea), eunref(st, &eb), eunref(st, &em), eunref(st, &ei);
  char observed[320];
  if (failed) snprintf(observed, sizeof observed, "stingray.%s failed: %s", failed, st->api.error(st->api.user));
  else snprintf(observed, sizeof observed, "largest difference %.3g (%s) over %d comparisons", worst, worst_name, compared);
  /* 1e-4: far above float rounding, far below what any other convention gives (order, handedness, sign: 0.1 and more). */
  return verdict(st, ARMS, !failed && worst <= 1e-4, "value maths", observed, "the engine's own answers within 0.0001");
}

/* ---- in-game checks: the arms ---------------------------------------------------------------
 * Read-only. Facts both modules stand on are judged once and reported for each. */
enum { RIG_BONES = 63, WEAPON_BONE = 8, MAX_BONES = 255 };
static const uint32_t SHOULDERS[2] = {0x03752C98, 0x1620B2CE}, COLLARS[2] = {0xC4787B4E, 0xB2BCD839};
enum { NAME_CHEST = 0x37EF0820, NAME_ATTACH = 0x4FDD3337, NAME_HAND = 0x5C0C68D3, NAME_RIGHT = 0x248004F5, NAME_GRIP = 0x67C94E4B };
static int name_at(const uint32_t *names, uint32_t count, uint32_t name) { /* the last of them, as the Lua's table has it */
  for (uint32_t i = count; i-- > 0;) if (names[i] == name) return (int)i;
  return -1;
}
static void log_bones(St *st, const char *what, const char *kind, const uint32_t *words, uint32_t count) {
  for (uint32_t first = 0; first < count; first += 56) {
    char line[700];
    int used = snprintf(line, sizeof line, "Bones: %s %s %u-%u of %u:", what, kind, first, first + 55 < count ? first + 55 : count - 1, count);
    for (uint32_t i = first; i < count && i < first + 56; i++) used += snprintf(line + used, sizeof line - (size_t)used, " %08x", words[i]);
    tfp_wire_say(st->w, line);
  }
}
static bool unit_words(St *st, uint64_t object, uint32_t offset, uint32_t count, uint32_t *out) {
  uint64_t at = 0;
  return mread(st, object + offset, &at, 8) && mread(st, at, out, (size_t)count * 4);
}
/* The chain of own-unit parents of a rig bone (link mode 1), up to the root. */
static int chain(const uint32_t *links, uint32_t count, uint32_t bone, uint32_t *out, int room) {
  int n = 0;
  while (n < room && bone < count && (links[bone] & 0xffff) == 1 && (links[bone] >> 16) < count && (links[bone] >> 16) != bone) {
    bone = links[bone] >> 16;
    out[n++] = bone;
  }
  return n;
}
static bool in_chain(const uint32_t *list, int n, uint32_t bone) {
  for (int i = 0; i < n; i++) if (list[i] == bone) return true;
  return false;
}
static const void *handle_of(const TfpArmHandles *handles, uint32_t ref) {
  for (size_t i = 0; i < handles->count; i++) if (handles->rows[i].ref == (double)ref) return handles->rows[i].unit;
  return NULL;
}
int tfp_wire_engine_check_arms(TfpWire *w, const TfpWireEngineBody *s, const TfpArmHandles *handles, bool *sleeves, bool *grip) {
  St *st = ST(w);
  const void *rig = handle_of(handles, s->viewmodel), *body = handle_of(handles, s->body);
  uint64_t rig_object = 0, body_object = 0;
  char observed[420], text[200];
  *sleeves = *grip = false;
  if (!rig || !body) return -1;
  if (!mobject(st, s, s->viewmodel, &rig_object) || !mobject(st, s, s->body, &body_object)) {
    snprintf(observed, sizeof observed, "unreadable (%.160s)", st->why);
    verdict(st, ARMS, false, "rig and body objects", observed, "the two unit objects");
    return 0;
  }
  /* (a) +0x1D0, the parent unit: what hangs on the rig says so there. Nothing hanging yet: ask again later. */
  unsigned hanging = 0, on_weapon_bone = 0;
  for (size_t i = 0; i < handles->count; i++) {
    uint32_t ref = (uint32_t)handles->rows[i].ref, word = 0;
    uint64_t object = 0, parent = 0, links = 0;
    if (!handles->rows[i].unit || ref == s->viewmodel || ref == s->body || !mobject(st, s, ref, &object)) continue;
    if (!mread(st, object + 0x1D0, &parent, 8) || (parent & UINT64_C(0xFFFFFFFFFFFF)) != (rig_object & UINT64_C(0xFFFFFFFFFFFF))) continue;
    hanging++;
    if (mread(st, object + 0x98, &links, 8) && mread(st, links, &word, 4) && word == (2u | (WEAPON_BONE << 16))) on_weapon_bone++;
  }
  if (!hanging) {
    if (!st->told_arms_waiting) {
      st->told_arms_waiting = true;
      sayf(st, "Waiting: arm pieces, grip hand: none of %zu units names the rig as its parent at +0x1D0 yet; the checks run when one does", handles->count);
    }
    return -1;
  }
  bool common = true, pieces_ok = true, grip_ok = true;
  snprintf(observed, sizeof observed, "%u of %zu units have the rig object %#llx there; %u of them linked to rig bone %d", hanging, handles->count,
           (unsigned long long)rig_object, on_weapon_bone, WEAPON_BONE);
  verdict(st, ARMS, true, "+0x1D0 parent unit", observed, "");
  /* (a) +0x70, the node count, against the engine's */
  uint32_t rig_count = 0, body_count = 0;
  double engine_rig = -1, engine_body = -1;
  bool have = mread(st, rig_object + 0x70, &rig_count, 4) && mread(st, body_object + 0x70, &body_count, 4);
  bool asked = unit_number(st, "Unit.num_scene_graph_items", rig, &engine_rig, text, sizeof text) &&
               unit_number(st, "Unit.num_scene_graph_items", body, &engine_body, text, sizeof text);
  if (!have) snprintf(observed, sizeof observed, "unreadable (%.160s)", st->why);
  else if (!asked) snprintf(observed, sizeof observed, "%s", text);
  else snprintf(observed, sizeof observed, "rig %u (engine %.0f), body %u (engine %.0f)", rig_count, engine_rig, body_count, engine_body);
  if (!verdict(st, ARMS, have && asked && (double)rig_count == engine_rig && (double)body_count == engine_body && rig_count >= 1 && rig_count <= MAX_BONES &&
               body_count >= 1 && body_count <= MAX_BONES, "+0x70 node count", observed, "Unit.num_scene_graph_items of both, 1 to 255"))
    return 0;
  /* (c) +0xA0, the name hashes, logged once so that a refusal can be mended from the log */
  static uint32_t rig_names[MAX_BONES], body_names[MAX_BONES], rig_links[MAX_BONES];
  if (!unit_words(st, rig_object, 0xA0, rig_count, rig_names) || !unit_words(st, body_object, 0xA0, body_count, body_names) ||
      !unit_words(st, rig_object, 0x98, rig_count, rig_links)) {
    snprintf(observed, sizeof observed, "unreadable (%.160s)", st->why);
    verdict(st, ARMS, false, "+0xA0 names and +0x98 links", observed, "one 32-bit word per node behind each pointer");
    return 0;
  }
  log_bones(st, "rig", "names", rig_names, rig_count);
  log_bones(st, "rig", "links", rig_links, rig_count);
  log_bones(st, "body", "names", body_names, body_count);
  /* (b) the rig: 63 bones (gh:102) */
  snprintf(observed, sizeof observed, "%u", rig_count);
  grip_ok &= verdict(st, GRIP, rig_count == RIG_BONES, "rig bone count", observed, "63");
  /* (c) the names the sleeves need: both shoulders on the rig and the body, chest and collars on the body (ap:26-30) */
  int used = 0;
  bool names_ok = true;
  for (int i = 0; i < 2; i++) {
    int on_rig = name_at(rig_names, rig_count, SHOULDERS[i]), on_body = name_at(body_names, body_count, SHOULDERS[i]);
    int collar = name_at(body_names, body_count, COLLARS[i]);
    used += snprintf(observed + used, sizeof observed - (size_t)used, "%s shoulder %08x rig %d body %d, collar %08x body %d; ", i ? "right" : "left", SHOULDERS[i],
                     on_rig, on_body, COLLARS[i], collar);
    names_ok &= on_rig >= 0 && on_body >= 0 && collar >= 0;
  }
  int chest = name_at(body_names, body_count, NAME_CHEST);
  snprintf(observed + used, sizeof observed - (size_t)used, "chest %08x body %d (-1: not there)", (unsigned)NAME_CHEST, chest);
  pieces_ok &= verdict(st, SLEEVES, names_ok && chest >= 0, "sleeve bone names", observed, "every one found");
  /* (b, c) the names the grips need: the weapon bone on rig bone 8, the body's hands (gh:99-100, 134) */
  int attach = name_at(rig_names, rig_count, NAME_ATTACH), left = name_at(body_names, body_count, NAME_HAND), right = name_at(body_names, body_count, NAME_RIGHT);
  snprintf(observed, sizeof observed, "weapon bone %08x on rig bone %d, left hand %08x on body bone %d, right hand %08x on body bone %d", (unsigned)NAME_ATTACH, attach,
           (unsigned)NAME_HAND, left, (unsigned)NAME_RIGHT, right);
  grip_ok &= verdict(st, GRIP, attach == WEAPON_BONE && left >= 0, "grip bone names", observed, "the weapon bone on rig bone 8 and the left hand on the body");
  /* (a) +0x98, the link words, against the engine's own parent of each rig node */
  unsigned agree = 0, differ = 0, asked_bones = 0;
  int offset = 99, first_bad = -1;
  double first_answer = 0;
  TfpEngRef rig_ref;
  if (!slot_ref(st, rig, &rig_ref, text, sizeof text)) { verdict(st, ARMS, false, "+0x98 link words", text, "a usable rig handle"); return 0; }
  for (uint32_t bone = 0; bone < rig_count; bone++) {
    double parent = 0;
    if ((rig_links[bone] & 0xffff) != 1) continue;
    asked_bones++;
    if (!ecall(st, "Unit.scene_graph_parent", "rn>n", rig_ref, (double)bone + 1, &parent)) { differ++; if (first_bad < 0) first_bad = (int)bone, first_answer = NAN; continue; }
    int d = (int)(parent - (double)(rig_links[bone] >> 16));
    if (offset == 99 && (d == 0 || d == 1) && parent == floor(parent)) offset = d;
    if (d == offset && parent == floor(parent)) agree++;
    else { differ++; if (first_bad < 0) first_bad = (int)bone, first_answer = parent; }
  }
  if (differ) snprintf(observed, sizeof observed, "%u of %u bones differ; first: bone %d has link word %08x, the engine says parent %.17g", differ, asked_bones, first_bad,
                       rig_links[first_bad], first_answer);
  else snprintf(observed, sizeof observed, "Unit.scene_graph_parent(rig, bone + 1) is the word's high half + %d on all %u own-parent bones of %u", offset, agree, rig_count);
  common &= verdict(st, ARMS, !differ && agree >= 8, "+0x98 link words", observed, "the engine's parent for every bone whose word is in mode 1");
  /* (b) the arm bones 10 / 12 / 13 and 30 / 32 / 34 (gh:101-102): each hand under its elbow under its shoulder, the arms apart */
  uint32_t left_chain[64], right_chain[64];
  int ln = chain(rig_links, rig_count, 13, left_chain, 64), rn = chain(rig_links, rig_count, 34, right_chain, 64);
  used = snprintf(observed, sizeof observed, "13");
  for (int i = 0; i < ln && used < 180; i++) used += snprintf(observed + used, sizeof observed - (size_t)used, "<%u", left_chain[i]);
  used += snprintf(observed + used, sizeof observed - (size_t)used, "; 34");
  for (int i = 0; i < rn && used < 380; i++) used += snprintf(observed + used, sizeof observed - (size_t)used, "<%u", right_chain[i]);
  bool arms_ok = in_chain(left_chain, ln, 12) && in_chain(left_chain, ln, 10) && in_chain(right_chain, rn, 32) && in_chain(right_chain, rn, 30) &&
                 !in_chain(left_chain, ln, 30) && !in_chain(right_chain, rn, 10);
  grip_ok &= verdict(st, GRIP, arms_ok, "arm bone chain", observed, "13 under 12 under 10, 34 under 32 under 30, neither arm under the other");
  /* (b) their lengths, read through the engine: an arm is an arm, and the two are alike */
  if (rig_count == RIG_BONES) {
    static const double nodes[6] = {11, 13, 14, 31, 33, 35};
    float at[6][3];
    asked = true;
    for (int i = 0; i < 6 && asked; i++) asked = ecall(st, "Unit.world_position", "rn>v", rig_ref, nodes[i], at[i]) || efail(st, text, sizeof text);
    double length[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4 && asked; i++) {
      const float *from = at[i < 2 ? i : i + 1], *to = at[i < 2 ? i + 1 : i + 2];
      length[i] = sqrt(pow((double)to[0] - from[0], 2) + pow((double)to[1] - from[1], 2) + pow((double)to[2] - from[2], 2));
    }
    bool sane = asked;
    for (int i = 0; i < 4; i++) sane = sane && length[i] > .08 && length[i] < .6;
    sane = sane && fabs(length[0] - length[2]) < .03 && fabs(length[1] - length[3]) < .03;
    if (asked) snprintf(observed, sizeof observed, "left %.3f + %.3f m, right %.3f + %.3f m", length[0], length[1], length[2], length[3]);
    else snprintf(observed, sizeof observed, "%s", text);
    grip_ok &= verdict(st, GRIP, sane, "arm bone lengths", observed, "upper and lower arm 0.08 to 0.6 m each, left and right within 0.03 m");
  }
  /* The poses are handled as rotation and position: the bones they are read from must carry no scale. */
  {
    const void *unit[2] = {rig, body};
    const double node[2] = {rig_count > 10 ? 11 : 1, chest >= 0 ? chest + 1 : 1};
    float scale[2][3] = {{0, 0, 0}, {0, 0, 0}};
    asked = true;
    for (int i = 0; i < 2 && asked; i++) {
      TfpEngRef ref, pose = 0;
      asked = slot_ref(st, unit[i], &ref, text, sizeof text) && (ecall(st, "Unit.world_pose", "rn>r", ref, node[i], &pose) || efail(st, text, sizeof text)) &&
              (ecall(st, "Matrix4x4.scale", "r>v", pose, scale[i]) || efail(st, text, sizeof text));
      eunref(st, &pose);
    }
    bool rigid = asked;
    for (int i = 0; i < 6; i++) rigid = rigid && fabs((double)scale[i / 3][i % 3] - 1) <= 1e-3;
    if (asked) snprintf(observed, sizeof observed, "rig node %.0f (%.5f, %.5f, %.5f), body node %.0f (%.5f, %.5f, %.5f)", node[0], scale[0][0], scale[0][1], scale[0][2],
                        node[1], scale[1][0], scale[1][1], scale[1][2]);
    else snprintf(observed, sizeof observed, "%s", text);
    common &= verdict(st, ARMS, rigid, "pose scale", observed, "1 within 0.001 on each axis");
  }
  *sleeves = common && pieces_ok && st->maths_ok;
  *grip = common && grip_ok && st->maths_ok;
  return *sleeves || *grip ? 1 : 0;
}

/* ---- the modules' non-engine halves --------------------------------------------------------- */
static void identity_of(TfpVisIdentity *out, char level, uint64_t key) {
  char text[24];
  tfp_wire_identity_text(text, level, key);
  out->present = true, out->len = (uint16_t)strlen(text);
  memcpy(out->bytes, text, out->len);
}
/* backend.lua:157-290: a player snapshot has no weapon fields and its identity is its player identity. */
static void vis_snapshot(const TfpOpsSnapshot *s, TfpVisSnapshot *t) {
  memset(t, 0, sizeof *t);
  t->base = (double)s->base, t->record = (double)s->record, t->head = s->head, t->tail = s->tail, t->camera_kind = s->camera_kind;
  t->request_weapon = s->request_weapon, t->world = (double)s->world, t->avatar_owner = (double)s->avatar_owner;
  t->avatar_index = s->avatar_index, t->body = s->body;
  t->has_viewmodel = true, t->viewmodel = s->viewmodel;
  t->safe = s->safe, t->seated = s->seated, t->weapon_transition = s->weapon_transition, t->head_camera_transition = s->head_camera_transition;
  t->controller_active = s->controller_active, t->aiming = s->aiming;
  t->has_controller_bytes = true, t->controller_len = 6; memcpy(t->controller_bytes, s->controller_bytes, 6);
  t->has_flags = true, t->flags_len = 24; memcpy(t->flags, s->flags, 24);
  identity_of(&t->player_identity, 'P', tfp_ops_player_key(s));
  if (s->full) {
    t->has_weapon = t->has_weapon_unit = t->has_weapon_component_descriptor = t->has_weapon_descriptor = true;
    t->weapon = s->weapon, t->weapon_unit = s->weapon_unit, t->weapon_component_descriptor = (double)s->weapon_component_descriptor;
    t->weapon_descriptor_len = 20; memcpy(t->weapon_descriptor, s->weapon_descriptor, 20);
    identity_of(&t->identity, 'W', tfp_ops_identity_key(s));
  } else t->identity = t->player_identity;
}
static void remember(St *st, const TfpOpsSnapshot *s, const TfpVisSnapshot *t) {
  st->recent[st->recent_next % RECENT].ops = *s;
  st->recent[st->recent_next++ % RECENT].vis = *t;
}
/* The backend's snapshot behind one head visibility hands back (the newest that reads the same). */
static const TfpOpsSnapshot *recalled(St *st, const TfpVisSnapshot *t) {
  for (unsigned i = 0; i < RECENT; i++) {
    const TfpVisSnapshot *v = &st->recent[(st->recent_next + RECENT - 1 - i) % RECENT].vis;
    if (v->player_identity.present && v->base == t->base && v->record == t->record && v->head == t->head && v->tail == t->tail &&
        v->camera_kind == t->camera_kind && v->body == t->body && v->world == t->world && v->avatar_owner == t->avatar_owner &&
        v->avatar_index == t->avatar_index && v->safe == t->safe && tfp_vis_identity_equal(&v->player_identity, &t->player_identity) &&
        tfp_vis_identity_equal(&v->identity, &t->identity) && !memcmp(v->flags, t->flags, 24))
      return &st->recent[(st->recent_next + RECENT - 1 - i) % RECENT].ops;
  }
  return NULL;
}
static void vis_text(TfpVisText *why, const char *text) { if (why) { why->present = true; cut(why->text, sizeof why->text, text); } }
static bool vis_address(double at, uint64_t *out, TfpVisText *why) {
  if (tfp_wire_address_of(at, out)) return true;
  vis_text(why, "Memory address is not a number");
  return false;
}
static bool io_read(void *user, double at, size_t size, uint8_t *out, TfpVisText *why) {
  St *st = user;
  uint64_t a;
  if (!vis_address(at, &a, why)) return false;
  if (mread(st, a, out, size)) return true;
  vis_text(why, st->why);
  return false;
}
static bool io_ptr(void *user, double at, double *out, TfpVisText *why) {
  uint64_t value = 0;
  if (!io_read(user, at, 8, (uint8_t *)&value, why)) return false;
  *out = (double)value;
  return true;
}
static bool io_u32(void *user, double at, double *out, TfpVisText *why) {
  uint32_t value = 0;
  if (!io_read(user, at, 4, (uint8_t *)&value, why)) return false;
  *out = value;
  return true;
}
static bool io_u8(void *user, double at, double *out, TfpVisText *why) {
  uint8_t value = 0;
  if (!io_read(user, at, 1, &value, why)) return false;
  *out = value;
  return true;
}
static bool io_executable(void *user, double *exe, TfpVisText *why) {
  St *st = user;
  TfpOpsText text;
  uint64_t base = 0;
  text.present = false, text.text[0] = 0;
  if (!tfp_ops_alignment_executable(&st->w->ops, TFP_OPS_EXECUTABLE_POSE, &base, &text)) { vis_text(why, text.text); return false; }
  *exe = (double)base;
  return true;
}
typedef TfpOpsResult (*Snapshot)(TfpOps *, TfpOpsSnapshot *, TfpOpsText *);
static bool io_snapshot(St *st, Snapshot take, TfpVisSnapshot *out, TfpVisText *why) {
  TfpOpsSnapshot s;
  TfpOpsText text;
  text.present = false, text.text[0] = 0;
  why->present = false;
  TfpOpsResult r = take(&st->w->ops, &s, &text);
  if (r != TFP_OPS_TRUE) {
    if (text.present || r == TFP_OPS_RAISED) vis_text(why, text.text);
    return false;
  }
  vis_snapshot(&s, out);
  remember(st, &s, out);
  return true;
}
static bool io_player_snapshot(void *user, TfpVisSnapshot *out, TfpVisText *why) { return io_snapshot(user, tfp_ops_player_snapshot, out, why); }
static bool io_pin_player_snapshot(void *user, TfpVisSnapshot *out, TfpVisText *why) { return io_snapshot(user, tfp_ops_pin_player_snapshot, out, why); }
static bool io_full_snapshot(void *user, TfpVisSnapshot *out, TfpVisText *why) { return io_snapshot(user, tfp_ops_snapshot, out, why); }
static int io_backpack(void *user, const TfpVisSnapshot *s, TfpHeadBackpack *out, TfpVisText *why) {
  St *st = user;
  const TfpOpsSnapshot *ops = recalled(st, s);
  TfpOpsBackpack pack;
  TfpOpsText text;
  text.present = false, text.text[0] = 0;
  if (!ops) { vis_text(why, "Backpack snapshot is not one of this frame's"); return -1; }
  TfpOpsResult r = tfp_ops_backpack(&st->w->ops, ops, &pack, &text);
  if (r == TFP_OPS_RAISED) { vis_text(why, text.text); return -1; }
  if (r != TFP_OPS_TRUE) return 0;
  /* backend.lua:314: identity = tostring(entity)..':'..descriptor */
  out->ref = pack.ref;
  out->identity.present = true;
  out->identity.len = (uint16_t)snprintf((char *)out->identity.bytes, 16, "%u:", pack.entity);
  memcpy(out->identity.bytes + out->identity.len, pack.descriptor, 20);
  out->identity.len += 20;
  return 1;
}
static bool io_action_state(void *user, const TfpVisSnapshot *s, TfpHeadActionState *out) {
  St *st = user;
  const TfpOpsSnapshot *ops = recalled(st, s);
  TfpOpsAction *action = &st->actions[st->action_next++ % 4];
  TfpOpsText text;
  TfpOpsSnapshot owner;
  if (!ops || tfp_ops_action_state(&st->w->ops, ops, action, &text) != TFP_OPS_TRUE) return false;
  /* action.owner is the player identity it was read for (action_state.lua:110): the same key from the same fields. */
  memset(&owner, 0, sizeof owner);
  owner.base = action->owner_base, owner.context = action->owner_context, owner.camera = action->owner_camera, owner.world = action->owner_world;
  owner.controller = action->owner_controller, owner.player = action->owner_player, owner.local_entity = action->owner_light;
  owner.avatar_owner = action->owner_avatar_owner, owner.avatar_index = action->owner_avatar_index, owner.avatar = action->owner_avatar;
  owner.body = action->owner_body, owner.avatar_entry = action->owner_entry;
  memcpy(owner.avatar_descriptor, action->owner_descriptor, 20);
  identity_of(&out->owner, 'P', tfp_ops_player_key(&owner));
  out->body = action->body;
  out->native = action;
  return true;
}
static int io_on_foot(void *user, const TfpVisSnapshot *s, const TfpHeadActionState *state) {
  St *st = user;
  const TfpOpsSnapshot *ops = recalled(st, s);
  return ops && state->native && tfp_ops_on_foot(&st->w->ops, ops, state->native) ? 1 : 0;
}
/* A closure runtime.lua gave the module (rt:687-696): 1 true, 0 any other answer, -1 it raised. */
static int entry(St *st, TfpRtEntry which, const TfpRtTable *table, V *value) {
  TfpRtArgs args;
  V r[2] = {nil(), nil()};
  memset(&args, 0, sizeof args);
  args.table = table;
  TfpRtStatus status = tfp_rt_call(&st->w->rt, which, &args, r);
  if (value) *value = r[0];
  if (status == TFP_RT_RAISED) return -1;
  return status == TFP_RT_OK && r[0].kind == TFP_RT_TRUE ? 1 : 0;
}
static int io_foreign_camera(void *user) { /* backend.foreign_camera(): 0 false, 1 any other answer, -1 raised */
  V value = nil();
  int r = entry(user, TFP_RT_E_foreign_camera, NULL, &value);
  if (r < 0) return -1;
  return r == 1 || (value.kind != TFP_RT_FALSE && value.kind != TFP_RT_NIL) ? 1 : 0;
}
static bool io_models_foreign_camera(void *user, int *value, TfpVisText *why) {
  int r = io_foreign_camera(user);
  if (r < 0) { vis_text(why, "foreign_camera raised"); return false; }
  *value = r;
  return true;
}
static double io_address(void *user, const void *handle) { return tfp_wire_engine_address(((St *)user)->w, handle); }
static double io_clock(void *user) { St *st = user; return st->api.ticks(st->api.user); }
static void io_log(void *user, const char *text) { tfp_wire_say(((St *)user)->w, text); }
static bool io_pose_offset(void *user) { return tfp_wire_pose_offset(((St *)user)->w); }
static bool io_enabled(void *user) { return ((St *)user)->w->controller.enabled; }
static int io_fallback_ready(void *user, const TfpVisSnapshot *s) { (void)s; return entry(user, TFP_RT_E_fallback_ready, NULL, NULL); }
static bool io_pinned_ready(void *user, const TfpVisSnapshot *s) {
  St *st = user;
  const TfpOpsSnapshot *ops = recalled(st, s);
  TfpRtTable table;
  if (!ops) return false;
  tfp_wire_snapshot_table(st->w, ops, &table);
  return entry(st, TFP_RT_E_pinned_ready, &table, NULL) == 1;
}
static bool io_full_body(void *user) { return entry(user, TFP_RT_E_full_body, NULL, NULL) == 1; }
static bool io_hide_torso(void *user) { return entry(user, TFP_RT_E_hide_torso, NULL, NULL) == 1; }
static bool io_show_torso(void *user) { return entry(user, TFP_RT_E_show_torso, NULL, NULL) == 1; }
static bool io_seated_head(void *user) { return entry(user, TFP_RT_E_seated_head, NULL, NULL) == 1; }
static bool whole(double value, double limit, uint64_t *out) {
  if (!(value >= 0 && value <= limit && value == floor(value))) return false;
  *out = (uint64_t)value;
  return true;
}
static bool io_alignment_context(void *user, double viewmodel, double avatar_owner, double avatar_index, TfpModelContext *out, TfpVisText *why) {
  St *st = user;
  TfpOpsContext context;
  TfpOpsText text;
  uint64_t unit, owner, index;
  text.present = false, text.text[0] = 0;
  if (!whole(viewmodel, 4294967295.0, &unit) || !tfp_wire_address_of(avatar_owner, &owner) || !whole(avatar_index, 4294967295.0, &index)) {
    vis_text(why, "Unit reference invalid");
    return false;
  }
  if (!tfp_ops_alignment_context(&st->w->ops, (uint32_t)unit, true, owner, true, (uint32_t)index, false, &context, &text)) { vis_text(why, text.text); return false; }
  out->unit = context.unit, out->index = context.index, out->generation = context.generation, out->object = (double)context.object;
  out->units = (double)context.units, out->generations = (double)context.generations;
  return true;
}
static bool io_descriptor(void *user, double world, double id, uint8_t out[24], TfpVisText *why) {
  St *st = user;
  TfpOpsText text;
  uint64_t at, entity;
  text.present = false, text.text[0] = 0;
  if (!tfp_wire_address_of(world, &at) || !whole(id, 4294967295.0, &entity)) { vis_text(why, "Descriptor address invalid"); return false; }
  if (!tfp_ops_descriptor(&st->w->ops, at, (uint32_t)entity, out, &text)) { vis_text(why, text.text); return false; }
  return true;
}

/* TfpArmBackend */
static bool ab_object(void *user, bool has_unit, double unit, bool has_owner, double owner, bool has_index, double index, double *object, TfpArmError *error) {
  St *st = user;
  uint64_t u = 0, o = 0, i = 0, at = 0;
  if (!has_unit || !whole(unit, 4294967295.0, &u) || (has_owner && !tfp_wire_address_of(owner, &o)) || (has_index && !whole(index, 4294967295.0, &i)))
    return etext(ARM_ERROR, "Unit reference invalid");
  if (!st->memory.object(st->memory.user, (uint32_t)u, has_owner, o, has_index, (uint32_t)i, &at, ARM_ERROR)) return false;
  *object = (double)at;
  return true;
}
static bool ab_read(void *user, double at, double size, uint8_t *out, TfpArmError *error) {
  St *st = user;
  uint64_t a, n;
  if (!tfp_wire_address_of(at, &a) || !whole(size, 1020, &n)) return etext(ARM_ERROR, "Memory address is not a number");
  return mread(st, a, out, (size_t)n) || etext(ARM_ERROR, st->why);
}
static bool ab_ptr(void *user, double at, double *out, TfpArmError *error) {
  uint64_t value = 0;
  if (!ab_read(user, at, 8, (uint8_t *)&value, error)) return false;
  *out = (double)value;
  return true;
}
static bool ab_u32(void *user, double at, double *out, TfpArmError *error) {
  uint32_t value = 0;
  if (!ab_read(user, at, 4, (uint8_t *)&value, error)) return false;
  *out = value;
  return true;
}
static bool ab_clock(void *user, double *now, TfpArmError *error) { (void)error; *now = tfp_wire_clock(((St *)user)->w); return true; }

/* ---- the runtime host ----------------------------------------------------------------------- */
#define SH(h) ST(W(h))
static void body_of(const TfpOpsSnapshot *s, TfpWireEngineBody *out) {
  out->body = s->body, out->viewmodel = s->viewmodel, out->avatar_index = s->avatar_index;
  out->world = s->world, out->avatar_owner = s->avatar_owner, out->has_owner = true;
}
static void refuse_arms_without_head(St *st) {
  if (st->told_head_off) return;
  st->told_head_off = true;
  sayf(st, "Refused: arm pieces: head visibility is off, so there are no unit handles to place sleeves with");
  sayf(st, "Refused: grip hand: head visibility is off, so there are no unit handles to place hands with");
  for (int m = TFP_WE_SLEEVES; m <= TFP_WE_GRIP; m++) if (st->state[m] != TFP_WE_WAITING) st->state[m] = TFP_WE_REFUSED;
}
/* The read-only checks of head visibility, once a player exists. */
static void head_preflight(St *st) {
  TfpOpsSnapshot s;
  TfpOpsText why;
  TfpWireEngineBody body;
  if (tfp_ops_player_snapshot(&st->w->ops, &s, &why) != TFP_OPS_TRUE) return;
  body_of(&s, &body);
  if (tfp_wire_engine_check_head(st->w, &body)) {
    st->state[TFP_WE_HEAD] = TFP_WE_READY;
    sayf(st, "Ready: head visibility: the in-game checks passed; it owns the head, torso, shoulder and shadow passes while the view is on");
  } else {
    st->state[TFP_WE_HEAD] = TFP_WE_REFUSED;
    refuse_arms_without_head(st);
  }
}
static bool rt_model_shadows_new(TfpRtHost *h) {
  St *st = SH(h);
  if (st->has_models) tfp_model_shadow_free(&st->models);
  tfp_model_shadow_init(&st->models, &st->models_io, TFP_VIS_APPEARANCE_25480438); /* not the retail rows (model_shadow_source.h:104) */
  st->has_models = true;
  return true;
}
static bool rt_head_new(TfpRtHost *h) {
  St *st = SH(h);
  if (st->head) tfp_head_free(st->head);
  st->head = tfp_head_new(&st->head_io, &st->head_engine, st->has_models ? &st->models : NULL, &tfp_head_build_25480438);
  return st->head ? true : tfp_wire_raise(h, "Head visibility could not be created");
}
static bool head_on(St *st) { return st->state[TFP_WE_HEAD] == TFP_WE_READY && st->head; }
typedef bool (*HeadCall)(TfpHead *, TfpVisText *);
static bool head_call(TfpRtHost *h, HeadCall call, V *o1, V *o2) {
  St *st = SH(h);
  TfpVisText why;
  why.present = false;
  bool ok = call(st->head, &why);
  handles_publish(st);
  *o1 = vbool(ok);
  *o2 = why.present ? tfp_wire_text(tfp_wire_keep(st->w, why.text)) : nil();
  return true;
}
static bool rt_head_update(TfpRtHost *h, V *o1, V *o2) {
  St *st = SH(h);
  if (st->state[TFP_WE_HEAD] == TFP_WE_RESOLVED && st->head) head_preflight(st);
  return head_on(st) ? head_call(h, tfp_head_update, o1, o2) : st->off.head_update(h, o1, o2);
}
static bool rt_head_stop(TfpRtHost *h, V *o1, V *o2) { return head_on(SH(h)) ? head_call(h, tfp_head_stop, o1, o2) : SH(h)->off.head_stop(h, o1, o2); }
static bool rt_head_shutdown(TfpRtHost *h, V *o1, V *o2) {
  return head_on(SH(h)) ? head_call(h, tfp_head_shutdown, o1, o2) : SH(h)->off.head_shutdown(h, o1, o2);
}
static bool rt_head_is_clean(TfpRtHost *h, V *o1) {
  St *st = SH(h);
  if (!head_on(st)) return st->off.head_is_clean(h, o1);
  *o1 = vbool(tfp_head_is_clean(st->head));
  return true;
}
static bool rt_head_result(TfpRtHost *h, TfpRtTable *o1) {
  St *st = SH(h);
  TfpHeadResult r;
  if (!head_on(st)) return st->off.head_result(h, o1);
  tfp_head_result(st->head, &r);
  memset(o1, 0, sizeof *o1);
  o1->present = true, o1->ref = &st->result_tag[0];
  o1->f[TFP_RT_TARGET_PHASE] = tfp_wire_text(tfp_head_phase_name(r.phase));
  return true;
}
/* head:sdk_handles(): the one table {[unit reference] = Unit} both arm modules are handed (rt:551, 557). */
static bool rt_head_sdk_handles(TfpRtHost *h, V *o1) {
  St *st = SH(h);
  size_t count = 0;
  unsigned long long table = 0;
  *o1 = nil();
  if (!head_on(st) || !tfp_head_sdk_handles(st->head, &count, &table)) return true;
  if (count > st->arm_room) {
    TfpArmHandle *rows = realloc(st->arm_rows, sizeof *rows * count);
    if (!rows) return true;
    st->arm_rows = rows, st->arm_room = count;
  }
  for (size_t i = 0; i < count; i++) {
    TfpHeadHandle row = tfp_head_sdk_handle(st->head, i);
    st->arm_rows[i] = (TfpArmHandle){row.ref, (TfpArmUnit)row.unit};
  }
  st->arm_handles.identity = (uintptr_t)table, st->arm_handles.count = count, st->arm_handles.rows = st->arm_rows;
  *o1 = tfp_wire_ref(&st->arm_handles);
  return true;
}

static void arm_snapshot(const TfpOpsSnapshot *s, TfpArmSnapshot *t) {
  memset(t, 0, sizeof *t);
  t->has_body = t->has_viewmodel = t->has_avatar_owner = t->has_avatar_index = t->has_request_weapon = true;
  t->body = s->body, t->viewmodel = s->viewmodel, t->avatar_owner = (double)s->avatar_owner, t->avatar_index = s->avatar_index;
  t->request_weapon = s->request_weapon;
  t->has_weapon = s->full, t->weapon = s->weapon;
  t->aiming = s->aiming, t->shoulder_aim = s->shoulder_aim, t->weapon_transition = s->weapon_transition;
}
static void arms_preflight(St *st, const TfpWireEngineBody *body, const TfpArmHandles *handles) {
  bool verdicts[TFP_WE_MODULES] = {false, false, false, false};
  if (st->arms_checked || st->frame < st->arms_retry) return;
  int r = tfp_wire_engine_check_arms(st->w, body, handles, &verdicts[TFP_WE_SLEEVES], &verdicts[TFP_WE_GRIP]);
  if (r < 0) { st->arms_retry = st->frame + 60; return; }
  st->arms_checked = true;
  for (int m = TFP_WE_SLEEVES; m <= TFP_WE_GRIP; m++) {
    if (st->state[m] != TFP_WE_RESOLVED) continue;
    st->state[m] = verdicts[m] ? TFP_WE_READY : TFP_WE_REFUSED;
    if (verdicts[m]) sayf(st, "Ready: %s: the in-game checks passed%s", module_names[m], m == TFP_WE_SLEEVES ? "; the first link is read back before a second" : "");
  }
}
static bool arm_update(TfpRtHost *h, const TfpRtTable *snapshot, bool wanted, V handles, TfpWireEngineModule module) {
  St *st = SH(h);
  const TfpWireSnap *snap = tfp_wire_snap_of(st->w, snapshot);
  const TfpArmHandles *table = handles.kind == TFP_RT_REF && handles.ref == &st->arm_handles ? &st->arm_handles : NULL;
  TfpArmSnapshot s;
  if (snap) {
    arm_snapshot(&snap->s, &s);
    body_of(&snap->s, &st->body);
    st->has_body = true;
  }
  if (st->state[TFP_WE_HEAD] == TFP_WE_REFUSED) refuse_arms_without_head(st);
  if (st->state[module] == TFP_WE_RESOLVED && wanted && snap && table) arms_preflight(st, &st->body, table);
  if (st->state[module] != TFP_WE_READY) return true; /* the off answer (wire_off.c:113) */
  if (module == TFP_WE_SLEEVES) {
    if (st->has_pieces) tfp_arm_pieces_update(&st->pieces, snap ? &s : NULL, wanted, table);
    if (st->link_failed) st->state[TFP_WE_SLEEVES] = TFP_WE_REFUSED;
  } else if (st->has_grip) tfp_grip_hand_update(st->grip, snap ? &s : NULL, wanted, table);
  return true;
}
static bool rt_arm_pieces_update(TfpRtHost *h, const TfpRtTable *s, bool wanted, V handles) { return arm_update(h, s, wanted, handles, TFP_WE_SLEEVES); }
static bool rt_grip_hand_update(TfpRtHost *h, const TfpRtTable *s, bool wanted, V handles) { return arm_update(h, s, wanted, handles, TFP_WE_GRIP); }
/* The modules exist from on_enable on (rt:698-706 would leave them out for good): they come up when the engine's
 * Lua state is caught and the checks have passed, and are idle and clean until then. */
static bool rt_arm_pieces_new(TfpRtHost *h, V *o1) {
  St *st = SH(h);
  tfp_arm_pieces_module_init(&st->pieces_module);
  tfp_arm_pieces_init(&st->pieces, &st->pieces_module, &st->arm_backend, &st->arm_engine[TFP_WE_SLEEVES]);
  st->has_pieces = true;
  *o1 = vbool(true);
  return true;
}
static bool rt_grip_hand_new(TfpRtHost *h, V *o1) {
  St *st = SH(h);
  tfp_grip_hand_init(st->grip, &st->arm_backend, &st->arm_engine[TFP_WE_GRIP]);
  st->has_grip = true;
  st->grip_frames_seen = 0;
  *o1 = vbool(true);
  return true;
}
static bool pieces_on(St *st) { return st->state[TFP_WE_SLEEVES] == TFP_WE_READY && st->has_pieces; }
static bool grip_on(St *st) { return st->state[TFP_WE_GRIP] == TFP_WE_READY && st->has_grip; }
static bool rt_arm_pieces_stop(TfpRtHost *h, V *o1, V *o2) {
  St *st = SH(h);
  if (!pieces_on(st)) return st->off.arm_pieces_stop(h, o1, o2);
  *o1 = vbool(tfp_arm_pieces_stop(&st->pieces)), *o2 = nil();
  if (st->link_failed) st->state[TFP_WE_SLEEVES] = TFP_WE_REFUSED;
  return true;
}
static bool rt_grip_hand_stop(TfpRtHost *h, V *o1, V *o2) {
  St *st = SH(h);
  if (!grip_on(st)) return st->off.grip_hand_stop(h, o1, o2);
  *o1 = vbool(tfp_grip_hand_stop(st->grip)), *o2 = nil();
  return true;
}
static bool rt_arm_pieces_is_clean(TfpRtHost *h, V *o1) {
  St *st = SH(h);
  if (!pieces_on(st)) return st->off.arm_pieces_is_clean(h, o1);
  *o1 = vbool(tfp_arm_pieces_is_clean(&st->pieces));
  return true;
}
static bool rt_grip_hand_is_clean(TfpRtHost *h, V *o1) {
  St *st = SH(h);
  if (!grip_on(st)) return st->off.grip_hand_is_clean(h, o1);
  *o1 = vbool(tfp_grip_hand_is_clean(st->grip));
  return true;
}
static bool opaque(St *st, int which, TfpRtTable *o1) { /* result(): only handed on (rt:1163-1164) */
  memset(o1, 0, sizeof *o1);
  o1->present = true, o1->ref = &st->result_tag[which];
  return true;
}
static bool rt_arm_pieces_result(TfpRtHost *h, TfpRtTable *o1) { return opaque(SH(h), 1, o1); }
static bool rt_grip_hand_result(TfpRtHost *h, TfpRtTable *o1) { return opaque(SH(h), 2, o1); }
/* profile.lua:84, 91, 93 (camera section): seated_head, body_sleeves, grip_hand are all true. */
static V rt_profile_true(TfpRtHost *h) { (void)h; return vbool(true); }

/* ---- the engine's Lua state arriving and leaving -------------------------------------------- */
static void resolve(St *st) {
  for (int m = 0; m < TFP_WE_MODULES; m++) {
    unsigned count = 0;
    char problem[200] = "";
    if (m == TFP_WE_SHADOWS) continue;
    for (size_t i = 0; i < sizeof functions / sizeof *functions && !problem[0]; i++) {
      const TfpWireEngineFunction *f = &functions[i];
      if (!(f->modules & BIT(m))) continue;
      count++;
      uintptr_t at = f->rva ? st->api.cfunction(st->api.user, f->path) : 0;
      if (!st->api.has(st->api.user, f->path)) snprintf(problem, sizeof problem, "stingray.%s is missing", f->path);
      else if (!f->rva) continue;
      else if (!at) snprintf(problem, sizeof problem, "stingray.%s is not the engine's C function (a script replaced it), expected exe+%#x", f->path, f->rva);
      else if (at != st->exe + f->rva) {
        if (at >= st->exe && at - st->exe < UINT64_C(0x10000000)) snprintf(problem, sizeof problem, "stingray.%s is at exe+%#llx, expected exe+%#x", f->path,
                                                                           (unsigned long long)(at - st->exe), f->rva);
        else snprintf(problem, sizeof problem, "stingray.%s is at %#llx outside the executable, expected exe+%#x", f->path, (unsigned long long)at, f->rva);
      }
    }
    if (problem[0]) {
      st->state[m] = TFP_WE_REFUSED;
      sayf(st, "Refused: %s: %s", module_names[m], problem);
    } else {
      st->state[m] = TFP_WE_RESOLVED;
      sayf(st, "Ready: %s: %u engine functions are build 25480438's own (exe at %#llx); its in-game checks run before its first write", module_names[m], count,
           (unsigned long long)st->exe);
    }
  }
  st->maths_ok = false;
  if (st->state[TFP_WE_SLEEVES] == TFP_WE_RESOLVED || st->state[TFP_WE_GRIP] == TFP_WE_RESOLVED) {
    st->maths_ok = tfp_wire_engine_check_maths(st->w);
    if (!st->maths_ok) for (int m = TFP_WE_SLEEVES; m <= TFP_WE_GRIP; m++) if (st->state[m] == TFP_WE_RESOLVED) st->state[m] = TFP_WE_REFUSED;
  }
}
static void lost(St *st) {
  sayf(st, "The engine's Lua state went away: head visibility, sleeves and grips are off until a state is caught again");
  st->vm = false;
  handles_reset(st);
  for (int m = 0; m < TFP_WE_MODULES; m++) if (m != TFP_WE_SHADOWS) st->state[m] = TFP_WE_WAITING;
  /* What the modules held named values of the dead state: they start over (nothing can be restored in a state that is gone). */
  if (st->head) {
    tfp_head_free(st->head);
    st->head = tfp_head_new(&st->head_io, &st->head_engine, st->has_models ? &st->models : NULL, &tfp_head_build_25480438);
  }
  if (st->has_pieces) tfp_arm_pieces_init(&st->pieces, &st->pieces_module, &st->arm_backend, &st->arm_engine[TFP_WE_SLEEVES]);
  if (st->has_grip) tfp_grip_hand_init(st->grip, &st->arm_backend, &st->arm_engine[TFP_WE_GRIP]);
  st->grip_frames_seen = 0;
  st->arms_checked = st->link_checked = st->link_failed = st->told_arms_waiting = st->told_head_off = false;
  st->arms_retry = 0;
  st->arm_handles.count = 0;
}
void tfp_wire_engine_begin(TfpWire *w) {
  St *st = ST(w);
  if (!st) return;
  st->frame++;
  bool ready = st->api.ready(st->api.user);
  uint64_t generation = st->api.generation(st->api.user);
  if (st->vm && (!ready || generation != st->generation)) lost(st);
  if (!ready) {
    if (!st->told_waiting) {
      st->told_waiting = true;
      sayf(st, "Waiting: the engine's Lua state is not caught yet; head visibility, sleeves and grips answer \"off\" until it is");
    }
    return;
  }
  if (st->vm) return;
  st->exe = st->api.exe(st->api.user);
  if (!st->exe) {
    if (!st->told_no_exe) { st->told_no_exe = true; sayf(st, "Waiting: the base of helldivers2.exe is not known, so no engine function can be compared"); }
    return;
  }
  st->vm = true, st->generation = generation;
  st->told_waiting = st->told_no_exe = false;
  resolve(st);
}
TfpWireEngineState tfp_wire_engine_state(const TfpWire *w, TfpWireEngineModule module) { return ST(w) ? ST(w)->state[module] : TFP_WE_WAITING; }
void tfp_wire_engine_force(TfpWire *w, TfpWireEngineModule module, TfpWireEngineState state) { if (ST(w)) ST(w)->state[module] = state; }

/* ---- the session log ------------------------------------------------------------------------ */
void tfp_wire_engine_frame(TfpWire *w) {
  St *st = ST(w);
  TfpGripHandResult grip;
  if (!st || !grip_on(st)) return;
  tfp_grip_hand_result(st->grip, &grip);
  if (grip.frames > st->grip_frames_seen) st->grip_corr += grip.frames - st->grip_frames_seen; /* updates in which a hand was placed (gh:680) */
  st->grip_frames_seen = grip.frames;
}
static const char *state_word(TfpWireEngineState state) { return state == TFP_WE_REFUSED ? "refused" : state == TFP_WE_READY ? "ready" : "waiting"; }
static void slug(const char *text, char *out, size_t size) { /* a reason as one word */
  size_t n = 0;
  for (; text[n] && n + 1 < size; n++) out[n] = text[n] == ' ' ? '_' : text[n];
  out[n] = 0;
}
size_t tfp_wire_engine_second(TfpWire *w, char *out, size_t size) {
  St *st = ST(w);
  char head[64], sleeves[80], grip[80], reason[48];
  if (!st || !size) return 0;
  if (head_on(st)) {
    TfpHeadResult r;
    tfp_head_result(st->head, &r);
    snprintf(head, sizeof head, "%s:%.0f", tfp_head_phase_name(r.phase), r.hidden);
  } else snprintf(head, sizeof head, "%s", state_word(st->state[TFP_WE_HEAD]));
  if (pieces_on(st)) {
    TfpArmPiecesResult r;
    tfp_arm_pieces_result(&st->pieces, &r);
    slug(r.reason, reason, sizeof reason);
    if (r.hung) snprintf(sleeves, sizeof sleeves, "linked:%.0f", r.pieces);
    else snprintf(sleeves, sizeof sleeves, "%s:%.0f", reason, r.pieces);
  } else snprintf(sleeves, sizeof sleeves, "%s", state_word(st->state[TFP_WE_SLEEVES]));
  if (grip_on(st)) {
    TfpGripHandResult r;
    tfp_grip_hand_result(st->grip, &r);
    slug(r.reason, reason, sizeof reason);
    snprintf(grip, sizeof grip, "%s", !strcmp(r.reason, "active") ? "on" : reason);
  } else snprintf(grip, sizeof grip, "%s", state_word(st->state[TFP_WE_GRIP]));
  /* corr: updates this second in which a hand was placed; max: the largest offset written to a linked bone this
   * second, its length in metres and its turn in degrees. */
  int n = snprintf(out, size, "head=%s sleeves=%s grip=%s corr=%.0f max=%.3fm/%.1fdeg", head, sleeves, grip, st->grip_corr, st->grip_max_m, st->grip_max_deg);
  st->grip_corr = st->grip_max_m = st->grip_max_deg = 0;
  return n < 0 ? 0 : (size_t)n < size ? (size_t)n : size - 1;
}

/* ---- binding -------------------------------------------------------------------------------- */
bool tfp_wire_engine_bind(TfpWire *w, const TfpWireEngineApi *api, const TfpWireEngineMemory *memory) {
  St *st = calloc(1, sizeof *st);
  if (st) {
    st->slots = calloc(SLOTS, sizeof *st->slots);
    st->index = calloc(INDEX, sizeof *st->index);
    st->transient = calloc(SLOTS, sizeof *st->transient);
    st->spare = calloc(SLOTS, sizeof *st->spare);
    st->pointers = calloc(UNITS_MAX, sizeof *st->pointers);
    st->units = calloc(UNITS_MAX, sizeof *st->units);
    st->grip = calloc(1, sizeof *st->grip);
  }
  if (!st || !st->slots || !st->index || !st->transient || !st->spare || !st->pointers || !st->units || !st->grip) {
    if (st) free(st->slots), free(st->index), free(st->transient), free(st->spare), free(st->pointers), free(st->units), free(st->grip);
    free(st);
    tfp_wire_say(w, "Refused: head visibility, model shadows, arm pieces, grip hand: no memory for the engine binder");
    return false;
  }
  st->w = w, st->api = *api, st->off = w->host;
  if (memory) st->memory = *memory;
  else st->memory = (TfpWireEngineMemory){st, default_read, default_object};
  w->ext[TFP_WIRE_EXT_ENGINE] = st;
  /* Filled once: head visibility compares the nine pointers between frames (hv:569-574). */
  st->head_engine = (TfpEngineIface){st, hi_main_world, hi_units, hi_set_mesh_visibility, hi_has_mesh, hi_mesh, hi_mesh_name, hi_mesh_node, hi_mesh_materials,
                                     hi_mesh_has_material};
  st->head_io = (TfpHeadIo){st, io_read, io_ptr, io_executable, io_player_snapshot, io_pin_player_snapshot, io_backpack, io_action_state, io_on_foot,
                            io_foreign_camera, io_address, io_clock, io_log, io_pose_offset, io_enabled, io_fallback_ready, io_pinned_ready, io_full_body,
                            io_hide_torso, io_show_torso, io_seated_head};
  st->models_io = (TfpModelShadowIo){st, io_full_snapshot, io_alignment_context, io_read, io_ptr, io_u32, io_u8, io_descriptor, io_models_foreign_camera};
  st->arm_backend = (TfpArmBackend){st, ab_object, ab_read, ab_ptr, ab_u32, ab_clock};
  for (int m = TFP_WE_SLEEVES; m <= TFP_WE_GRIP; m++) {
    TfpArmEngine *e = &st->arm_engine[m];
    st->arm_user[m] = (ArmUser){st, (TfpWireEngineModule)m};
    e->user = &st->arm_user[m];
    e->Application_main_world = ai_main_world, e->Unit_alive = ai_alive, e->Unit_num_scene_graph_items = ai_items, e->Unit_world_pose = ai_world_pose;
    e->Unit_world_position = ai_world_position, e->Unit_world_rotation = ai_world_rotation, e->Unit_set_local_pose = ai_set_local_pose;
    if (m == TFP_WE_SLEEVES) e->World_link_unit = ai_link_unit;
    else {
      e->Unit_num_meshes = ai_num_meshes, e->Unit_mesh = ai_mesh, e->Unit_set_mesh_visibility = ai_set_mesh_visibility;
      e->Mesh_visibility = ai_mesh_visibility, e->Mesh_num_materials = ai_mesh_materials;
    }
    tfp_wire_engine_maths(e);
  }
  st->state[TFP_WE_SHADOWS] = TFP_WE_READY;
  TfpRtHost *h = &w->host;
  h->model_shadows_new = rt_model_shadows_new;
  h->head_new = rt_head_new, h->head_stop = rt_head_stop, h->head_update = rt_head_update, h->head_result = rt_head_result;
  h->head_is_clean = rt_head_is_clean, h->head_shutdown = rt_head_shutdown, h->head_sdk_handles = rt_head_sdk_handles;
  h->arm_pieces_new = rt_arm_pieces_new, h->arm_pieces_update = rt_arm_pieces_update, h->arm_pieces_stop = rt_arm_pieces_stop;
  h->arm_pieces_is_clean = rt_arm_pieces_is_clean, h->arm_pieces_result = rt_arm_pieces_result;
  h->grip_hand_new = rt_grip_hand_new, h->grip_hand_update = rt_grip_hand_update, h->grip_hand_stop = rt_grip_hand_stop;
  h->grip_hand_is_clean = rt_grip_hand_is_clean, h->grip_hand_result = rt_grip_hand_result;
  h->profile_body_sleeves = h->profile_grip_hand = h->profile_seated_head = rt_profile_true;
  sayf(st, "Ready: model shadows: no engine function; reads the appearance rows at avatar owner +%#x", (unsigned)TFP_VIS_APPEARANCE_25480438);
  return true;
}
