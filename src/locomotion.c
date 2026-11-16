/* See locomotion.h. Line references are to the retail files
 * src/first_person/locomotion.lua and backend.lua at 0.5.41-smooth.66. The
 * order of reads and checks and the error texts are retail's. Build with
 * -ffp-contract=off: every product and sum below is one IEEE operation in Lua.
 *
 * ======================================================================
 * Build constants, game build 25480438. Each line names the retail value and
 * how this build's value was established.
 *   build_map = tools/build_map.py --retail <kit> --to-here
 *   image     = bytes read from native-work/evidence/exe-25480438.bin
 *   layout    = structure layout, the same number in retail; the named file of
 *               this repository reads the same field on this build
 * ====================================================================== */
#include "locomotion.h"
#include "profile.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* -- executable (locomotion.lua:104, 111, 129-131; profile.lua pose.executable, motion.world_pose) -- */
#define B_EXE_REGISTRY      FP_REGISTRY          /* 0x1a100f0; retail unit_registry 0x1ab76f8; build_map; profile.h; the same VALUE as
                                                    camera_helper.c B_EXE_REGISTRY */
#define B_EXE_GRAPH_GETTER  UINT32_C(0x2bd870)   /* retail 0x28d3c0; build_map; native_contract.h graph_getter; the same VALUE as
                                                    camera_helper.c B_EXE_GRAPH_GETTER */
#define B_EXE_WORLD_POSE    UINT32_C(0x1fd220)   /* retail world_pose.rva 0x1f30e0; build_map; native_contract.h unit_world_matrix; the
                                                    same VALUE as camera_helper.c B_EXE_UNIT_WORLD */
#define B_HEX_WORLD_POSE    "40534883ec204863dae89206eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3cc"
                                                 /* 48 bytes, as many as retail compares. Image at exe+0x1fd220. Retail's differ in the call
                                                    displacement (e2d5eaff, here 9206eaff) and in byte 48, which lies after the function's
                                                    ret: c3 in retail, cc here. The first 47 = camera_helper.c B_HEX_UNIT_WORLD */
/* -- game (backend.lua:351-372, on_foot) -- */
#define B_AVATAR_FLAGS      FP_AVATAR_FLAGS      /* 0x53e880; retail 0x53e888; build_map; profile.h; = camera_helper.c B_AVATAR_FLAGS */
#define B_AVATAR_STRIDE     FP_AVATAR_STRIDE     /* 0x1238;   retail 0x1240;   build_map; profile.h; = camera_helper.c B_AVATAR_STRIDE */
#define B_GAME_STATE        FP_STATE             /* 0x3326340; retail 0x2ce1508; build_map; profile.h; = camera_helper.c B_GAME_STATE; action.c */
#define B_STATE_SCENE       FP_STATE_SCENE       /* 0xac21c;  retail 0xac29c;  build_map; profile.h; = camera_helper.c B_STATE_SCENE; action.c */
/* -- structure layout shared by both builds -- */
#define L_REGISTRY_OBJECTS   0x88  /* layout; backend.c fp_unit_object; camera_helper.c L_REGISTRY_BLOCK */
#define L_REGISTRY_CAPACITY  0x98  /* layout; backend.c; camera_helper.c L_REGISTRY_CAPACITY */
#define L_REGISTRY_GENERATIONS 0xA0 /* layout; backend.c */
#define L_VT_GRAPH_GETTER    0xE8  /* layout; scene.c fp_graph; camera_helper.c L_VT_GRAPH_GETTER; image (the table that holds the getters) */
#define L_UNIT_GRAPH         0x60  /* layout; scene.c; camera_helper.c L_UNIT_GRAPH; the getter's own code is lea rax,[rcx+0x60] */
#define L_GRAPH_COUNT        0x10  /* layout; scene.c; camera_helper.c L_GRAPH_COUNT */
#define L_GRAPH_WORLDS       0x28  /* layout; scene.c; camera_helper.c L_GRAPH_WORLDS; the world-pose wrapper adds [rax+0x28] */
#define L_MATRIX             0x40  /* layout; camera_helper.c L_MATRIX; the world-pose wrapper shifts the index by 6 */
#define L_CONTEXT_BASIS      0x0C  /* layout; camera_helper.c L_CONTEXT_BASIS (the 0.5.34 port read the camera matrix there in the game) */
#define L_RIG_PARENT_NODE    4     /* same; the rig's node 4 ("sibling under root4"); scene.c reads the same 63-node rig */
#define L_RIG_MINIMUM        50    /* same; rig.count > 50; camera_helper.c L_RIG_NODES is 63 */
#define L_CAMERA_KIND_FPV    6     /* same; snapshot.c */
/* -- animation states of resource 4D1C334D294DFA97 (locomotion.lua:88-93; backend.lua:353-355) -- */
#define L_STATE_ON_FOOT      36    /* same; action.c; camera_helper.c L_STATE_ON_FOOT */
#define L_STATE_BRIDGE       3     /* same; action.c; camera_helper.c L_STATE_BRIDGE */
#define L_STATE_SPRINT_CARRY 71    /* same; action.c reads l3 == 71 as sprint on this build */
/* -- OPEN on this build: no file of this repository and no record reads them here. Until someone establishes them,
 *    the two tests below answer "no" (sprint is then seen through l3 == 71 only). -- */
#define B_SPRINT_GAIT_ESTABLISHED   0   /* flag byte 9, bit value 4: native sprint gait (0x84; walk 0x88, jog 0x90). Labelled on
                                           retail 2026-10-25. The byte itself is the posture byte placement.c reads on this build. */
#define L_FLAG_GAIT                 9
#define L_FLAG_GAIT_SPRINT          4
#define B_BRIDGE_SPRINT_ESTABLISHED 0   /* l14 == 3 with l3 == 68 and l1 == 75 or 78: the bridge graph's sprint. Labelled on retail. */
#define L_STATE_BRIDGE_CARRY        68
#define L_STATE_BRIDGE_ENTRY        75
#define L_STATE_BRIDGE_STEADY       78
/* TEST ONLY, never defined in a shipped build: tests/run-vectors-locomotion.sh also builds this file with
 * -DTFP_LOCOMOTION_TEST_OPEN_CONSTANTS, which switches the two open tests on so that the logic can be compared with
 * retail's answers. */
#ifdef TFP_LOCOMOTION_TEST_OPEN_CONSTANTS
#undef B_SPRINT_GAIT_ESTABLISHED
#undef B_BRIDGE_SPRINT_ESTABLISHED
#define B_SPRINT_GAIT_ESTABLISHED 1
#define B_BRIDGE_SPRINT_ESTABLISHED 1
#endif

#define TFP_LOW UINT64_C(65536)
#define TFP_HIGH UINT64_C(0x800000000000)
static const double PI = 3.14159265358979323846;
static const double RAD = 1.74532925199432957692e-2; /* math.rad(x) = x * this constant (LuaJIT lib_math.c) */

const char tfp_locomotion_sprint_evidence[] = "native sprint gait, labelled mission L3=71 or verified bridge L1=75/78,L3=68"; /* :154 */

/* math.min / math.max as LuaJIT computes them: the second argument wins on a tie and on NaN. */
static double lua_min(double a, double b) { return a < b ? a : b; }
static double lua_max(double a, double b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ pure */
static bool finite_number(double n) { return n == n && fabs(n) < 1e8; }                          /* :6 */
static double dot(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; } /* :7 */
static bool fail(const char **error, const char *text) {
  if (error) *error = text;
  return false;
}
/* basis(values), :9-20. The three axes are rows 0, 1, 2 of the matrix. */
static bool basis(const double *values, const double *axes[3], double lengths[3], const char **error) {
  for (int i = 0; i < 3; i++) {
    axes[i] = values + i * 4;
    for (int n = 0; n < 3; n++)
      if (!finite_number(axes[i][n])) return fail(error, "Non-finite transform basis");
    lengths[i] = dot(axes[i], axes[i]);
    if (!(lengths[i] > .25 && lengths[i] < 4)) return fail(error, "Transform scale changed");
  }
  for (int i = 0; i < 2; i++)
    for (int j = i + 1; j < 3; j++)
      if (!(fabs(dot(axes[i], axes[j])) < .03 * sqrt(lengths[i] * lengths[j]))) return fail(error, "Transform basis is skewed");
  return true;
}
static void multiply(const double *a, const double *b, double *out) { /* :21-28; a[3] is retail's a[4] */
  out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
static double handed(const double *axes[3], const double *lengths) { /* :29-33 */
  const double *a = axes[0], *b = axes[1], *c = axes[2];
  return (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) /
         sqrt(lengths[0] * lengths[1] * lengths[2]);
}
/* rotation(axis, radians), :40-46: the camera axis in the parent's normalized orientation. */
static bool rotation(const double *c[3], const double *cl, const double *p[3], const double *pl, int axis, double radians,
                     double *out, const char **error) {
  double v[3];
  for (int i = 0; i < 3; i++) v[i] = dot(c[axis], p[i]) / sqrt(cl[axis] * pl[i]);
  double length = sqrt(dot(v, v));
  if (!(length > .95 && length < 1.05)) return fail(error, "Motion rotation axis changed");
  double sine = sin(radians / 2) / length;
  out[0] = v[0] * sine; out[1] = v[1] * sine; out[2] = v[2] * sine; out[3] = cos(radians / 2);
  return true;
}

bool tfp_locomotion_rotation_to_parent(const TfpLocomotionValue *value, const double camera[16], const double parent[16],
                                       double q[4], const char **error) { /* :34-63 */
  if (error) *error = NULL;
  double pitch = value->present & TFP_LOCO_PITCH ? value->pitch : 0, yaw = value->present & TFP_LOCO_YAW ? value->yaw : 0,
         roll = value->present & TFP_LOCO_ROLL ? value->roll : 0; /* :35 */
  const double angles[3] = {pitch, yaw, roll};
  for (int i = 0; i < 3; i++) /* :36 */
    if (!(finite_number(angles[i]) && fabs(angles[i]) <= PI / 2)) return fail(error, "Motion angle out of bounds");
  if (pitch == 0 && yaw == 0 && roll == 0) { q[0] = 0; q[1] = 0; q[2] = 0; q[3] = 1; return true; } /* :37 */
  const double *c[3], *p[3];
  double cl[3], pl[3], first[4], second[4], third[4], both[4], out[4];
  if (!basis(camera, c, cl, error) || !basis(parent, p, pl, error)) return false; /* :38 */
  if (!(handed(c, cl) > .95 && handed(p, pl) > .95)) return fail(error, "Motion rotation basis is mirrored"); /* :39 */
  /* :50. yaw about camera axis 3, then pitch about axis 1, then roll about axis 2 */
  if (!rotation(c, cl, p, pl, 2, yaw, first, error) || !rotation(c, cl, p, pl, 0, pitch, second, error)) return false;
  multiply(first, second, both);
  if (!rotation(c, cl, p, pl, 1, roll, third, error)) return false;
  multiply(both, third, out);
  double length = sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]); /* :51 */
  if (!(finite_number(length) && length > .9 && length < 1.1)) return fail(error, "Motion quaternion unavailable"); /* :52 */
  for (int i = 0; i < 4; i++) out[i] = out[i] / length; /* :53 */
  if (out[3] < 0)
    for (int i = 0; i < 4; i++) out[i] = -out[i]; /* :54 */
  double angle = 2 * acos(lua_max(-1, lua_min(1, out[3]))); /* :55 */
  double maximum = 120 * RAD - .00001;                       /* :56 */
  if (angle > maximum) { /* :57-61 */
    double vector_length = sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    double scale = sin(maximum / 2) / vector_length;
    out[0] = out[0] * scale; out[1] = out[1] * scale; out[2] = out[2] * scale; out[3] = cos(maximum / 2);
  }
  q[0] = out[0]; q[1] = out[1]; q[2] = out[2]; q[3] = out[3];
  return true;
}

bool tfp_locomotion_to_parent(const TfpLocomotionValue *value, const double camera[16], const double parent[16],
                              TfpLocomotionParent *result, const char **error) { /* :64-79 */
  if (error) *error = NULL;
  if (!value) return fail(error, "Motion vector unavailable"); /* :65 */
  const double *c[3], *p[3];
  double cl[3], lengths[3], world[3] = {0, 0, 0}, q[4];
  if (!basis(camera, c, cl, error) || !basis(parent, p, lengths, error)) return false; /* :66 */
  const double moved[3] = {value->dx, value->dy, value->dz};
  for (int i = 0; i < 3; i++) { /* :68-72; ipairs ends at the first nil */
    if (!(value->present & (1u << i))) break;
    double n = moved[i];
    if (!(finite_number(n) && fabs(n) <= .15)) return fail(error, "Motion vector out of bounds");
    double length = sqrt(dot(c[i], c[i]));
    for (int j = 0; j < 3; j++) world[j] = world[j] + c[i][j] * n / length;
  }
  double dx = dot(world, p[0]) / lengths[0], dy = dot(world, p[1]) / lengths[1], dz = dot(world, p[2]) / lengths[2]; /* :73 */
  double limit = lua_max(lua_max(lua_max(1, fabs(dx) / .15), fabs(dy) / .15), fabs(dz) / .15);                       /* :74 */
  dx = dx / limit; dy = dy / limit; dz = dz / limit; /* :75 */
  if (!tfp_locomotion_rotation_to_parent(value, camera, parent, q, error)) return false; /* :76 */
  result->dx = dx; result->dy = dy; result->dz = dz;
  result->qx = q[0]; result->qy = q[1]; result->qz = q[2]; result->qw = q[3];
  return true;
}

static bool sprint_gait(const uint8_t *flags, size_t size) { /* :85-87 */
  if (!B_SPRINT_GAIT_ESTABLISHED) return false;
  return flags && size == 24 && flags[L_FLAG_GAIT] / L_FLAG_GAIT_SPRINT % 2 == 1;
}
static bool layer_is(const TfpActionState *action, int layer, int state) { return action->has[layer] && action->states[layer] == state; }
bool tfp_locomotion_sprinting(const TfpActionState *action, bool verified_lifecycle, const uint8_t *flags, size_t flags_size) { /* :88-93 */
  return action && action->resource_match && action->layer_count == 31 &&
         (layer_is(action, 14, L_STATE_ON_FOOT) || (layer_is(action, 14, L_STATE_BRIDGE) && verified_lifecycle)) &&
         (sprint_gait(flags, flags_size) || layer_is(action, 3, L_STATE_SPRINT_CARRY) ||
          (B_BRIDGE_SPRINT_ESTABLISHED && layer_is(action, 14, L_STATE_BRIDGE) && layer_is(action, 3, L_STATE_BRIDGE_CARRY) &&
           (layer_is(action, 1, L_STATE_BRIDGE_ENTRY) || layer_is(action, 1, L_STATE_BRIDGE_STEADY))));
}

/* ----------------------------------------------------------------- reads */
typedef struct Reader { const FpMemory *m; char why[TFP_LOCOMOTION_ERROR]; } Reader;
static bool refuse(Reader *r, const char *text) {
  if (!*r->why) snprintf(r->why, sizeof r->why, "%s", text);
  return false;
}
static bool bytes(Reader *r, uintptr_t at, void *out, size_t size) { /* backend:_read, backend.lua:65-79 */
  if ((uint64_t)at < TFP_LOW || !size || size > 1024 || (uint64_t)at + size >= TFP_HIGH) return refuse(r, "Invalid read");
  return fp_read(r->m, at, out, size) || refuse(r, "Memory unavailable");
}
static bool u8(Reader *r, uintptr_t at, uint8_t *out) { return bytes(r, at, out, 1); }
static bool u32(Reader *r, uintptr_t at, uint32_t *out) { return bytes(r, at, out, 4); }
static unsigned long long lua_hex(uint64_t value) { /* string.format('%x') of the double retail made of the value; see action_state.c */
  double n = (double)(uint32_t)value + (double)(uint32_t)(value >> 32) * 4294967296.0;
  return (unsigned long long)(n < 9223372036854775808.0 ? (int64_t)n : (int64_t)(n - 18446744073709551616.0));
}
static bool ptr(Reader *r, uintptr_t at, uintptr_t *out) { /* backend:_ptr, backend.lua:82-85 */
  uint64_t value = 0;
  if (!bytes(r, at, &value, 8)) return false;
  if (value < TFP_LOW || value >= TFP_HIGH) {
    if (!*r->why)
      snprintf(r->why, sizeof r->why, "Pointer unavailable at0x%llx (value0x%llx)", (unsigned long long)at, lua_hex(value));
    return false;
  }
  *out = (uintptr_t)value;
  return true;
}
static int hex_digit(char c) { return c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10; }

bool tfp_on_foot(const FpMemory *memory, const TfpSnapshot *s, const TfpActionState *action) { /* backend.lua:351-372 */
  Reader r = {memory, ""};
  uint8_t now[4];
  uintptr_t state = 0, again = 0;
  uint32_t scene = 0, check = 0;
  if (!action || !action->resource_match || action->layer_count != 31) return false;
  if (layer_is(action, 14, L_STATE_ON_FOOT)) return true;
  if (!layer_is(action, 14, L_STATE_BRIDGE) || !s || (s->flags[0] & 6) != 2) return false;
  if (!(s->avatar_index < 8)) return false; /* 'Bridge control owner unavailable' */
  uintptr_t flags = s->avatar_owner + B_AVATAR_FLAGS + (uintptr_t)s->avatar_index * B_AVATAR_STRIDE;
  if (s->flags_address != flags || !bytes(&r, flags, now, 4) || memcmp(now, s->flags, 4)) return false; /* 'Bridge combat controls changed' */
  if (!ptr(&r, s->base + B_GAME_STATE, &state) || !u32(&r, state + B_STATE_SCENE, &scene)) return false;
  if (scene != 3 && scene != 4) return false;
  /* 'Bridge scene or combat controls changed': each read only when the one before it agreed */
  if (!u32(&r, state + B_STATE_SCENE, &check) || check != scene) return false;
  if (!ptr(&r, s->base + B_GAME_STATE, &again) || again != state) return false;
  if (!bytes(&r, flags, now, 4) || memcmp(now, s->flags, 4)) return false;
  return true;
}

/* The repository's own backend. */
static bool own_player_snapshot(void *user, TfpSnapshot *out, char *error, size_t error_size) {
  return tfp_snapshot(((TfpLocomotionHost *)user)->memory, TFP_SNAPSHOT_PLAYER, out, error, error_size);
}
static bool own_alignment_executable(void *user, uintptr_t *exe, char *error, size_t error_size) {
  return tfp_alignment_executable(((TfpLocomotionHost *)user)->memory, exe, error, error_size);
}
static bool own_action_state(void *user, const TfpSnapshot *s, TfpActionState *out) { /* backend.lua:344-350, the default layers */
  TfpLocomotionHost *host = user;
  return tfp_action_state_sample(host->reader, &host->actions, s, NULL, 0, out, NULL, 0);
}
static bool own_on_foot(void *user, const TfpSnapshot *s, const TfpActionState *action) {
  return tfp_on_foot(((TfpLocomotionHost *)user)->memory, s, action);
}
void tfp_locomotion_backend(TfpLocomotionBackend *backend, TfpLocomotionHost *host, const FpMemory *memory, TfpActionStateReader *reader) {
  host->memory = memory;
  host->reader = reader;
  tfp_action_state_backend(&host->actions, memory);
  backend->memory = memory;
  backend->user = host;
  backend->player_snapshot = own_player_snapshot;
  backend->alignment_executable = own_alignment_executable;
  backend->action_state = own_action_state;
  backend->on_foot = own_on_foot;
}

typedef struct Unit { uintptr_t object, graph, generations, objects; uint32_t count, index, generation; } Unit;
/* unit(exe, ref), :103-115 */
static bool unit(Reader *r, uintptr_t exe, uint32_t ref, Unit *out) {
  uintptr_t registry = 0, vt = 0, getter = 0;
  uint32_t capacity = 0;
  uint8_t generation = 0;
  if (!ptr(r, exe + B_EXE_REGISTRY, &registry)) return false;
  out->index = ref % 0x400000;
  out->generation = ref / 0x400000 % 256;
  if (!u32(r, registry + L_REGISTRY_CAPACITY, &capacity)) return false;
  if (!(capacity > 0 && capacity <= 0x400000 && out->index < capacity)) return refuse(r, "Motion Unit index unavailable");
  if (!ptr(r, registry + L_REGISTRY_GENERATIONS, &out->generations) || !ptr(r, registry + L_REGISTRY_OBJECTS, &out->objects) ||
      !ptr(r, out->objects + (uintptr_t)out->index * 8, &out->object) || !u8(r, out->generations + out->index, &generation)) return false;
  if (generation != out->generation) return refuse(r, "Motion Unit retired");
  if (!ptr(r, out->object, &vt) || !ptr(r, vt + L_VT_GRAPH_GETTER, &getter)) return false;
  if (getter != exe + B_EXE_GRAPH_GETTER) return refuse(r, "Motion graph getter changed");
  out->graph = out->object + L_UNIT_GRAPH;
  if (!u32(r, out->graph + L_GRAPH_COUNT, &out->count)) return false;
  if (!(out->count > 0 && out->count <= 512)) return refuse(r, "Motion graph bounds changed");
  return true;
}
/* valid(record), :116-119: 1 yes, 0 no, -1 a read refused */
static int still(Reader *r, const Unit *u) {
  uint8_t generation = 0;
  uintptr_t object = 0;
  if (!u8(r, u->generations + u->index, &generation)) return -1;
  if (generation != u->generation) return 0;
  if (!ptr(r, u->objects + (uintptr_t)u->index * 8, &object)) return -1;
  return object == u->object;
}
/* matrix(address), :98-102: 16 floats widened to double, and its three axes must make a basis. */
static bool matrix(Reader *r, uintptr_t address, double *out) {
  float floats[16];
  const double *axes[3];
  double lengths[3];
  const char *why = NULL;
  if (!bytes(r, address, floats, sizeof floats)) return false;
  for (int i = 0; i < 16; i++) out[i] = (double)floats[i];
  return basis(out, axes, lengths, &why) || refuse(r, why);
}

bool tfp_locomotion_sample(const TfpLocomotionBackend *backend, const TfpLocomotionLease *lease, TfpLocomotionSample *out,
                           char *error, size_t error_size) { /* :120-159 */
  Reader r = {backend->memory, ""};
  TfpSnapshot s, after;
  TfpLocomotionSample result;
  TfpActionState action;
  Unit body, rig;
  char why[TFP_LOCOMOTION_ERROR] = "";
  uintptr_t exe = 0, worlds = 0;
  double root[16];
  uint8_t code[64];
  bool ok = false;
  memset(&result, 0, sizeof result);
  do {
    if (!backend->player_snapshot(backend->user, &s, why, sizeof why)) { refuse(&r, *why ? why : "assertion failed!"); break; } /* :122 */
    if (!(s.safe && s.camera_kind == L_CAMERA_KIND_FPV)) { refuse(&r, "Motion requires a safe native FPV camera"); break; }      /* :123 */
    if (!(lease && lease->current && !lease->done && !lease->cleanup_only && lease->origin && tfp_same_player(lease->origin, &s) &&
          lease->request == s.record)) { refuse(&r, "Motion camera lease changed"); break; }                                      /* :124-126 */
    if (!backend->alignment_executable(backend->user, &exe, why, sizeof why)) { refuse(&r, why); break; }                         /* :127 */
    size_t n = strlen(B_HEX_WORLD_POSE) / 2;                                                                                       /* :130-131 */
    if (!bytes(&r, exe + B_EXE_WORLD_POSE, code, n)) break;
    size_t at = 0;
    for (; at < n; at++)
      if (code[at] != (uint8_t)(hex_digit(B_HEX_WORLD_POSE[at * 2]) * 16 + hex_digit(B_HEX_WORLD_POSE[at * 2 + 1]))) break;
    if (at < n) { refuse(&r, "Motion world-pose getter changed"); break; }
    if (!unit(&r, exe, s.body, &body) || !unit(&r, exe, s.viewmodel, &rig)) break;                                                /* :132 */
    if (!(rig.count > L_RIG_MINIMUM && rig.object == lease->rig_object)) { refuse(&r, "Motion rig ownership changed"); break; }   /* :133 */
    if (!ptr(&r, body.graph + L_GRAPH_WORLDS, &worlds) || !matrix(&r, worlds, root)) break;                                       /* :134 */
    if (!ptr(&r, rig.graph + L_GRAPH_WORLDS, &worlds) || !matrix(&r, worlds + L_RIG_PARENT_NODE * L_MATRIX, result.parent_basis)) break; /* :135 */
    if (!matrix(&r, s.context + L_CONTEXT_BASIS, result.camera_basis)) break;                                                     /* :136 */
    double x = root[12], y = root[13], z = root[14];                                                                               /* :137 */
    if (!(finite_number(x) && finite_number(y) && finite_number(z))) { refuse(&r, "Motion root position invalid"); break; }       /* :138 */
    const double *forward = result.camera_basis + 4;                                                                              /* :139 */
    double horizontal = sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
    if (!(horizontal > .00001)) { refuse(&r, "Motion camera facing unavailable"); break; }                                        /* :140 */
    int alive = still(&r, &body);                                                                                                 /* :141 */
    if (alive > 0) alive = still(&r, &rig);
    if (alive < 0) break;
    if (!alive) { refuse(&r, "Motion Unit changed during observation"); break; }
    *why = 0;
    if (!backend->player_snapshot(backend->user, &after, why, sizeof why)) { refuse(&r, *why ? why : "assertion failed!"); break; } /* :142 */
    if (!(tfp_same_player(&after, &s) && after.record == s.record && after.safe && after.viewmodel == s.viewmodel)) {             /* :143-144 */
      refuse(&r, "Motion owner changed during observation");
      break;
    }
    bool has_action = backend->action_state(backend->user, &s, &action);                                                          /* :145 */
    bool lifecycle = backend->on_foot(backend->user, &s, has_action ? &action : NULL);                                            /* :148 */
    result.sprint = tfp_locomotion_sprinting(has_action ? &action : NULL, lifecycle, s.flags, sizeof s.flags);
    result.player = s;                                                                                                             /* :149-155 */
    result.x = x; result.y = y; result.z = z;
    result.yaw = atan2(-forward[0], forward[1]);
    result.pitch = atan2(forward[2], horizontal);
    result.ads = s.controller_active;
    result.shoulder_aim = s.shoulder_aim && !s.controller_active;
    memcpy(result.stance_flags, s.flags, 24);
    result.has_action = has_action;
    if (has_action) {
      memcpy(result.action_resource, action.resource, sizeof result.action_resource);
      result.action_verified = action.resource_match;
      result.has_l1 = action.has[1]; result.l1 = action.states[1];
      result.has_l3 = action.has[3]; result.l3 = action.states[3];
      result.has_l14 = action.has[14]; result.l14 = action.states[14];
    }
    result.reload = s.reloading;
    result.body_object = body.object;
    ok = true;
  } while (0);
  if (!ok) { /* :157 */
    if (error && error_size) snprintf(error, error_size, "%s", *r.why ? r.why : "Memory unavailable");
    return false;
  }
  if (error && error_size) *error = 0;
  *out = result;
  return true;
}
