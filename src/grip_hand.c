/* Translation of retail src/first_person/grip_hand.lua (0.5.41-smooth.66,
 * commit 1d0d453). Numbers in comments and in the engine calls are that
 * file's lines. See grip_hand.h and docs/translation/ARMS-TRANSLATION.md.
 *
 * A raised Lua error is a longjmp to the pcall the retail function wraps its
 * body in (arm_engine_calls.h). Where the Lua evaluates several engine calls
 * inside one expression they are written out one by one here, in Lua's order
 * (left to right, arguments before the call). */
#include "grip_hand.h"
#include <math.h>

#define CHUNK "src/first_person/grip_hand.lua"
#define K (&g->calls)
/* :99-134 */
#define GRIP 0x67C94E4B
#define HAND_NAME 0x5C0C68D3
#define RIGHT_NAME 0x248004F5
#define ATTACH 0x4FDD3337
#define RIGHT_SHOULDER 30
#define RIGHT_ELBOW 32
#define RIGHT_HAND 34
#define SHOULDER 10
#define ELBOW 12
#define HAND 13
#define RIG_NODES 63
#define FULL .20
#define NONE .32
#define RESCAN .25
#define MAX_NODES 255
#define STRETCH .95
#define PINNED .02
#define EASE .15
#define CONFIRM .5
#define PERIOD 1
#define RAD(degrees) ((degrees) * 0.017453292519943295769)   /* math.rad */
#define TURNING RAD(400)
#define SLIDING 1.5
#define CALM .25
#define RIG_TURN RAD(45)
#define RIG_SLIDE .15
#define RIG_CALM .5
#define AWAY_TURN RAD(12)
#define AWAY_SLIDE .03
#define STIR_TURN RAD(30)
#define STIR_SLIDE .1
#define SETTLE 3
static const double SWAP[3] = {.1, .25, .5};
#define QUICK .6
#define ASIDE .2
/* Piece bones the sleeve module places itself: never offset here. */
static bool sleeve(double name) {
  return name == 0x37EF0820 || name == 0xC4787B4E || name == 0xB2BCD839 || name == 0x58D886FB || name == 0x5A4BBAA2 || name == 0x315CD161;
}
#define HUGE_NUMBER HUGE_VAL

/* math.min and math.max as the reference LuaJIT computes them (a NaN in the second place wins). */
static double lua_min(double a, double b) { return a < b ? a : b; }
static double lua_max(double a, double b) { return a > b ? a : b; }
/* x^2 is the C library's pow, which is not always x*x; the pointer keeps the compiler from folding it. */
static double (*volatile lua_pow)(double, double) = pow;
static double squared(double x) { return lua_pow(x, 2); }
/* a*b where a sum follows: through a volatile, so that no compiler setting fuses the product into the addition. */
static double times(double a, double b) { volatile double product = a * b; return product; }

/* ---- M.solve (:137-150) and M.weight (:153-157) ---- */
static bool solve(TfpArmCalls *k, TfpArmVector3 S, TfpArmVector3 E, TfpArmVector3 H, TfpArmVector3 T, TfpArmVector3 *elbow, TfpArmVector3 *reached) {
  TfpArmVector3 d = arm_sub(k, E, S);
  double upper = arm_Vector3_length(k, 138, d);
  d = arm_sub(k, H, E);
  double lower = arm_Vector3_length(k, 138, d);
  TfpArmVector3 to = arm_sub(k, T, S);
  double first = upper + lower - .001;
  d = arm_sub(k, H, S);
  double limit = lua_min(first, lua_max((upper + lower) * STRETCH, arm_Vector3_length(k, 140, d)));
  first = fabs(upper - lower) + .001;
  double reach = lua_max(first, lua_min(limit, arm_Vector3_length(k, 141, to)));
  d = arm_sub(k, E, S);
  TfpArmVector3 normal = arm_Vector3_cross(k, 142, to, d);
  if (arm_Vector3_length(k, 143, to) < 1e-5 || arm_Vector3_length(k, 143, normal) < 1e-5) return false;
  TfpArmVector3 direction = arm_Vector3_normalize(k, 144, to);
  double along = (times(upper, upper) - times(lower, lower) + times(reach, reach)) / (2 * reach);
  double out = sqrt(lua_max(0, times(upper, upper) - times(along, along)));
  TfpArmVector3 unit = arm_Vector3_normalize(k, 147, normal);
  TfpArmVector3 side = arm_Vector3_cross(k, 147, unit, direction);
  side = arm_Vector3_normalize(k, 147, side);
  d = arm_sub(k, E, S);
  if (arm_Vector3_dot(k, 148, side, d) < 0) side = arm_unm(k, side);
  TfpArmVector3 part = arm_mul(k, direction, along);
  TfpArmVector3 sum = arm_add(k, S, part);
  part = arm_mul(k, side, out);
  *elbow = arm_add(k, sum, part);
  part = arm_mul(k, direction, reach);
  *reached = arm_add(k, S, part);
  return true;
}
typedef struct SolveCall { TfpArmCalls *k; TfpArmVector3 S, E, H, T, *elbow, *reached; bool solved; } SolveCall;
static void solve_body(void *argument) {
  SolveCall *c = argument;
  const TfpArmEngine *e = c->k->e;
  /* V is an argument here, not a table bind() has checked: without one the first `V.length` cannot be looked up. */
  if (!(e->Vector3_call || e->Vector3_length || e->Vector3_dot || e->Vector3_cross || e->Vector3_normalize || e->Vector3_lerp || e->Vector3_x ||
        e->Vector3_y || e->Vector3_z))
    arm_fail(c->k, 138, "attempt to index local 'V' (a nil value)");
  c->solved = solve(c->k, c->S, c->E, c->H, c->T, c->elbow, c->reached);
}
TfpGripSolve tfp_grip_hand_solve(const TfpArmEngine *engine, TfpArmVector3 S, TfpArmVector3 E, TfpArmVector3 H, TfpArmVector3 T,
                                 TfpArmVector3 *elbow, TfpArmVector3 *reached, TfpArmError *error) {
  TfpArmCalls calls;
  SolveCall call = {&calls, S, E, H, T, elbow, reached, false};
  memset(&calls, 0, sizeof calls);
  calls.e = engine; calls.chunk = CHUNK;
  if (!arm_protect(&calls, solve_body, &call)) { *error = calls.error; return TFP_GRIP_SOLVE_RAISED; }
  return call.solved ? TFP_GRIP_SOLVED : TFP_GRIP_SOLVE_NIL;
}
double tfp_grip_hand_weight(double distance) {
  if (distance <= FULL) return 1;
  if (distance >= NONE) return 0;
  return (NONE - distance) / (NONE - FULL);
}

/* ---- M.new (:159-187) ---- */
void tfp_grip_hand_init(TfpGripHand *g, const TfpArmBackend *backend, const TfpArmEngine *engine) {
  memset(g, 0, sizeof *g);
  g->calls.e = engine;
  g->calls.b = backend;
  g->calls.chunk = CHUNK;
  snprintf(g->reason, sizeof g->reason, "idle");
  g->aim = -1;
}
static void set_text(char *out, const char *text) { snprintf(out, TFP_GRIP_REASON, "%s", text); }

static void bind(TfpGripHand *g) { /* :189-196 */
  const TfpArmEngine *e = g->calls.e;
  bool unit = e->Unit_alive || e->Unit_num_scene_graph_items || e->Unit_world_pose || e->Unit_world_position || e->Unit_world_rotation ||
              e->Unit_set_local_pose || e->Unit_num_meshes || e->Unit_mesh || e->Unit_set_mesh_visibility;
  bool vector = e->Vector3_call || e->Vector3_length || e->Vector3_dot || e->Vector3_cross || e->Vector3_normalize || e->Vector3_lerp ||
                e->Vector3_x || e->Vector3_y || e->Vector3_z;
  bool operators = e->Vector3_op_add && e->Vector3_op_sub && e->Vector3_op_mul && e->Vector3_op_unm;   /* not in the Lua: see the header */
  if (!(unit && vector && e->Quaternion_axis_angle && e->Quaternion_rotate && e->Quaternion_multiply && e->Quaternion_lerp &&
        e->Matrix4x4_from_quaternion_position && e->Matrix4x4_rotation && e->Quaternion_inverse && e->Quaternion_to_elements &&
        e->Quaternion_from_elements && operators))
    arm_fail(K, 191, "Grip hand SDK unavailable");
}
static bool usable(TfpGripHand *g, TfpArmUnit handle, double count) { /* :197-199 */
  return handle != NULL && arm_Unit_alive(K, 198, handle) == true && arm_Unit_num_scene_graph_items(K, 198, handle) == count;
}
/* Which arm and which part of it a rig bone belongs to: walk up the rig's own parents (:212-229). 0: none. */
static void rig_segments(TfpGripHand *g, double address, uint8_t *segment, uint8_t *side) {
  uint8_t links[RIG_NODES * 4];
  double at_links = arm_ptr(K, address + 0x98);
  arm_read(K, at_links, RIG_NODES * 4, links);
  for (int index = 0; index < RIG_NODES; index++) {
    double at = index;
    bool has = true;
    int steps = 0;
    segment[index] = 0; side[index] = 0;
    while (has && steps < RIG_NODES) {
      if (at == HAND) { segment[index] = TFP_GRIP_HAND; side[index] = TFP_GRIP_LEFT; break; }
      if (at == ELBOW) { segment[index] = TFP_GRIP_ELBOW; side[index] = TFP_GRIP_LEFT; break; }
      if (at == SHOULDER) { segment[index] = TFP_GRIP_SHOULDER; side[index] = TFP_GRIP_LEFT; break; }
      if (at == RIGHT_HAND) { segment[index] = TFP_GRIP_HAND; side[index] = TFP_GRIP_RIGHT; break; }
      if (at == RIGHT_ELBOW) { segment[index] = TFP_GRIP_ELBOW; side[index] = TFP_GRIP_RIGHT; break; }
      if (at == RIGHT_SHOULDER) { segment[index] = TFP_GRIP_SHOULDER; side[index] = TFP_GRIP_RIGHT; break; }
      if (!(at < RIG_NODES)) arm_fail(K, 225, "attempt to index local 'link' (a nil value)");   /* links[at] of a parent beyond the table */
      double low = arm_low(links, (size_t)at), high = arm_high(links, (size_t)at);
      if (low == 1 && high != at) at = high; else has = false;
      steps = steps + 1;
    }
  }
}

/* What a lookup gathers before it is taken over (:230-301). */
typedef struct Scan {
  TfpGripHand *g;
  const TfpArmSnapshot *s;
  const TfpArmHandles *handles;
  double rig_address;
  uint8_t segment[RIG_NODES], side[RIG_NODES], rig_names[RIG_NODES * 4];
  size_t body_names_count;               /* on_body: empty until the body's names were read */
  uint8_t body_names[MAX_NODES * 4];
  int weapon_count, found_count, bone_count;
  TfpGripCandidate weapons[TFP_GRIP_CANDIDATES];
  TfpGripPiece found[TFP_GRIP_PIECES];
  TfpGripBone bones[TFP_GRIP_BONES];
  bool has_hung;
  TfpArmUnit hung_handle;
  double hung_count;
  /* the unit of the current pcall */
  double ref;
  TfpArmUnit handle;
} Scan;
/* on_body[name]: the SDK index of the body's bone of that name (the last of them), 0 for none. */
static double on_body(const Scan *scan, double name) {
  for (size_t i = scan->body_names_count; i-- > 0;)
    if (arm_word(scan->body_names, i) == name) return (double)i + 1;
  return 0;
}
static _Noreturn void full(TfpGripHand *g) { /* not in the Lua: its tables have no end */
  snprintf(g->calls.error.text, sizeof g->calls.error.text, "Grip hand table full");
  arm_raise(K);
}
static void scan_body(void *argument) { /* :241-255 */
  Scan *scan = argument;
  TfpGripHand *g = scan->g;
  const TfpArmSnapshot *s = scan->s;
  double address = arm_object(K, s, s->has_body, s->body);
  double count = arm_u32(K, address + 0x70);
  TfpArmUnit handle = arm_handle(scan->handles, s->has_body, s->body);
  if (!(count > 0 && count <= MAX_NODES && handle)) arm_fail(K, 244, "assertion failed!");
  double at = arm_ptr(K, address + 0xA0);
  arm_read(K, at, count * 4, scan->body_names);
  scan->body_names_count = (size_t)count;
  double hand = on_body(scan, HAND_NAME);
  if (hand != 0) { g->body = handle; g->body_hand = hand; g->body_count = count; }
  g->body_right = on_body(scan, RIGHT_NAME); g->has_body_right = g->body_right != 0;
  g->body_attach = on_body(scan, ATTACH); g->has_body_attach = g->body_attach != 0;
  /* The bone the right upper arm hangs on (its own parent, link mode 1), else the upper arm. */
  double upper = on_body(scan, arm_word(scan->rig_names, RIGHT_SHOULDER));
  if (upper != 0) {
    uint8_t links[MAX_NODES * 4];
    at = arm_ptr(K, address + 0x98);
    arm_read(K, at, count * 4, links);
    double low = arm_low(links, (size_t)upper - 1), high = arm_high(links, (size_t)upper - 1);
    g->body_ref = low == 1 && high < count ? high + 1 : upper;
    g->has_body_ref = true;
  }
}
static void scan_unit(void *argument) { /* :258-281 */
  Scan *scan = argument;
  TfpGripHand *g = scan->g;
  uint8_t names[MAX_NODES * 4], links[MAX_NODES * 4], parent[8];
  double address = arm_object(K, scan->s, true, scan->ref);
  double count = arm_u32(K, address + 0x70);
  if (!(count > 0 && count <= MAX_NODES)) arm_fail(K, 261, "assertion failed!");
  size_t bones = (size_t)count;
  double at = arm_ptr(K, address + 0xA0);
  arm_read(K, at, count * 4, names);
  for (size_t index = 0; index < bones; index++) {
    if (arm_word(names, index) == GRIP) {
      if (scan->weapon_count == TFP_GRIP_CANDIDATES) full(g);
      scan->weapons[scan->weapon_count++] = (TfpGripCandidate){scan->handle, (double)index + 1, count, false, {0, 0, 0}, 0};
      break;
    }
  }
  arm_read(K, address + 0x1D0, 8, parent);
  if ((double)parent[0] + (double)parent[1] * 256 + (double)parent[2] * 65536 + (double)parent[3] * 16777216 + (double)parent[4] * 4294967296.0 +
      (double)parent[5] * 1099511627776.0 != scan->rig_address)
    return;
  at = arm_ptr(K, address + 0x98);
  arm_read(K, at, count * 4, links);
  if (g->has_rig_attach && arm_low(links, 0) == 2 && arm_high(links, 0) == g->rig_attach - 1) {
    scan->has_hung = true; scan->hung_handle = scan->handle; scan->hung_count = count;
  }
  int first = scan->bone_count, made = 0;
  for (size_t index = 0; index < bones; index++) {
    double high = arm_high(links, index);
    uint8_t part = arm_low(links, index) == 2 && high < RIG_NODES ? scan->segment[(size_t)high] : 0;
    if (part && !sleeve(arm_word(names, index))) {
      double finger = 0;
      if (part == TFP_GRIP_HAND && high != HAND && high != RIGHT_HAND && g->body) finger = on_body(scan, arm_word(scan->rig_names, (size_t)high));
      if (first + made == TFP_GRIP_BONES) full(g);
      scan->bones[first + made++] = (TfpGripBone){(uint32_t)high + 1, (uint16_t)(index + 1), (uint16_t)finger, part, scan->side[(size_t)high]};
    }
  }
  if (made > 0) {
    if (scan->found_count == TFP_GRIP_PIECES) full(g);
    scan->found[scan->found_count++] = (TfpGripPiece){scan->handle, count, first, made};
    scan->bone_count = first + made;
  }
}
static void scan(TfpGripHand *g, const TfpArmSnapshot *s, const TfpArmHandles *handles) { /* :230-307 */
  Scan scratch;
  Scan *scan = &scratch;
  g->scans = g->scans + 1;
  scan->g = g; scan->s = s; scan->handles = handles;
  scan->weapon_count = scan->found_count = scan->bone_count = 0;
  scan->has_hung = false;
  scan->body_names_count = 0;
  scan->rig_address = arm_object(K, s, s->has_viewmodel, s->viewmodel);
  rig_segments(g, scan->rig_address, scan->segment, scan->side);
  /* Finger bones carry the same names on the rig and on the body. */
  double at = arm_ptr(K, scan->rig_address + 0xA0);
  arm_read(K, at, RIG_NODES * 4, scan->rig_names);
  g->body = NULL; g->body_hand = 0; g->body_count = 0;
  g->has_body_right = g->has_body_attach = g->has_rig_attach = g->has_body_ref = false;
  for (int index = 0; index < RIG_NODES; index++)
    if (arm_word(scan->rig_names, (size_t)index) == ATTACH) { g->rig_attach = index + 1; g->has_rig_attach = true; }
  arm_protect(K, scan_body, scan);
  for (size_t row = 0; row < handles->count; row++) { /* :256 */
    scan->ref = handles->rows[row].ref; scan->handle = handles->rows[row].unit;
    if (!scan->handle) continue;                           /* a Lua table holds no nil */
    if ((!s->has_body || scan->ref != s->body) && scan->ref != s->viewmodel && arm_Unit_alive(K, 257, scan->handle) == true) arm_protect(K, scan_unit, scan);
  }
  g->complete = false;
  bool right_whole = false, right_any = false;
  for (int p = 0; p < scan->found_count; p++) {
    for (int b = 0; b < scan->found[p].bones; b++) {
      const TfpGripBone *bone = &scan->bones[scan->found[p].first + b];
      g->complete = g->complete || bone->rig == HAND + 1;
      right_whole = right_whole || bone->rig == RIGHT_HAND + 1;
      right_any = right_any || bone->side == TFP_GRIP_RIGHT;
    }
  }
  g->whole[TFP_GRIP_LEFT] = g->complete; g->whole[TFP_GRIP_RIGHT] = right_whole;
  g->right_partial = right_any && !right_whole;            /* armour there, arm not yet */
  /* A grip bone found again keeps where it was last frame (the trailing-hand comparison needs it). */
  for (int w = 0; w < scan->weapon_count; w++) {
    TfpGripCandidate *weapon = &scan->weapons[w];
    for (int o = 0; o < g->candidate_count; o++) {
      const TfpGripCandidate *old = &g->candidates[o];
      if (old->handle == weapon->handle && old->node == weapon->node) { weapon->has_at = old->has_at; memcpy(weapon->at, old->at, sizeof weapon->at); }
    }
  }
  memcpy(g->candidates, scan->weapons, sizeof *scan->weapons * (size_t)scan->weapon_count);
  memcpy(g->pieces, scan->found, sizeof *scan->found * (size_t)scan->found_count);
  memcpy(g->bones, scan->bones, sizeof *scan->bones * (size_t)scan->bone_count);
  g->candidate_count = scan->weapon_count; g->piece_count = scan->found_count; g->bone_count = scan->bone_count;
  g->seen_handles = handles->identity; g->has_seen_handles = true;
  if (g->gun_turned && g->has_gun && !(scan->has_hung && scan->hung_handle == g->gun.handle)) { /* :302-305 */
    if (usable(g, g->gun.handle, g->gun.count)) {
      TfpArmMatrix4x4 identity = arm_Matrix4x4_identity(K, 303);
      arm_Unit_set_local_pose(K, 303, g->gun.handle, 1, identity);
    }
    g->gun_turned = false; g->has_rest = false;
  }
  g->has_gun = scan->has_hung; g->gun.handle = scan->hung_handle; g->gun.count = scan->hung_count;
}
/* The weapon keeps a written offset just as the arm pieces do: clear it once (:309-315). */
static void gun_release(TfpGripHand *g) {
  if (g->gun_turned) {
    g->gun_turned = false;
    if (g->has_gun && usable(g, g->gun.handle, g->gun.count)) {
      TfpArmMatrix4x4 identity = arm_Matrix4x4_identity(K, 312);
      arm_Unit_set_local_pose(K, 312, g->gun.handle, 1, identity);
    }
  }
  g->has_rest = false; g->rest_keep = 0; g->has_spin_until = false; g->has_rest_ref = false; g->has_stirred = false;
}
static TfpGripElements elements(TfpGripHand *g, TfpArmMatrix4x4 pose) { /* :316-319 */
  TfpGripElements out = {{0}};
  TfpArmVector3 p = arm_Matrix4x4_translation(K, 317, pose);
  TfpArmQuaternion rotation = arm_Matrix4x4_rotation(K, 317, pose);
  TfpArmElements q = arm_Quaternion_to_elements(K, 317, rotation);
  out.v[0] = arm_Vector3_x(K, 318, p); out.v[1] = arm_Vector3_y(K, 318, p); out.v[2] = arm_Vector3_z(K, 318, p);
  out.v[3] = q.x; out.v[4] = q.y; out.v[5] = q.z; out.v[6] = q.w;
  return out;
}
static double turned_between(const TfpGripElements *a, const TfpGripElements *b) {
  return 2 * acos(lua_min(1, fabs(times(a->v[3], b->v[3]) + times(a->v[4], b->v[4]) + times(a->v[5], b->v[5]) + times(a->v[6], b->v[6]))));
}
static double slid_between(const TfpGripElements *a, const TfpGripElements *b, int from) {
  return sqrt(squared(a->v[from] - b->v[from]) + squared(a->v[from + 1] - b->v[from + 1]) + squared(a->v[from + 2] - b->v[from + 2]));
}
/* True while the rig plays an animation of its own (:332-347). */
static bool rig_moving(TfpGripHand *g, TfpArmUnit rig, double now) {
  TfpArmMatrix4x4 pose = arm_Unit_world_pose(K, 333, rig, RIGHT_HAND + 1);
  TfpArmMatrix4x4 hand = arm_Matrix4x4_inverse(K, 333, pose);
  pose = arm_Unit_world_pose(K, 334, rig, g->rig_attach);
  pose = arm_Matrix4x4_multiply(K, 334, pose, hand);
  TfpGripElements at = elements(g, pose);
  pose = arm_Unit_world_pose(K, 335, rig, HAND + 1);
  pose = arm_Matrix4x4_multiply(K, 335, pose, hand);
  TfpArmVector3 left = arm_Matrix4x4_translation(K, 335, pose);
  double x = arm_Vector3_x(K, 336, left), y = arm_Vector3_y(K, 336, left), z = arm_Vector3_z(K, 336, left);
  at.v[7] = x; at.v[8] = y; at.v[9] = z;
  bool has_before = g->has_rig_at;
  TfpGripElements before = g->rig_at;
  double elapsed = g->has_rig_time ? now - g->rig_time : 0;
  g->rig_at = at; g->has_rig_at = true; g->rig_time = now; g->has_rig_time = true;
  if (has_before && elapsed > 0 && elapsed < .25) {
    double slid = slid_between(&at, &before, 0);
    double other = slid_between(&at, &before, 7);
    double turned = turned_between(&at, &before);
    if (turned > RIG_TURN * elapsed || slid > RIG_SLIDE * elapsed || other > RIG_SLIDE * elapsed) { g->rig_until = now + RIG_CALM; g->has_rig_until = true; }
  }
  if (g->has_rig_until && now < g->rig_until - RIG_CALM - .01) g->has_rig_until = false;
  return g->has_rig_until && now < g->rig_until;
}
static TfpArmMatrix4x4 from_elements(TfpGripHand *g, int line, const TfpGripElements *e) {
  TfpArmQuaternion q = arm_Quaternion_from_elements(K, line, e->v[3], e->v[4], e->v[5], e->v[6]);
  TfpArmVector3 p = arm_Vector3(K, line, e->v[0], e->v[1], e->v[2]);
  return arm_Matrix4x4_from_quaternion_position(K, line, q, p);
}
/* The body's right hand as its own weapon bone sees it (:348-395). *spinning: the second value, `rel`, was returned. */
static TfpArmMatrix4x4 in_hand(TfpGripHand *g, double now, bool aimed, bool *spinning, TfpArmMatrix4x4 *rel_out) {
  TfpArmUnit body = g->body;
  *spinning = false;
  TfpArmMatrix4x4 hand = arm_Unit_world_pose(K, 349, body, g->body_right);
  TfpArmMatrix4x4 pose = arm_Unit_world_pose(K, 349, body, g->body_attach);
  pose = arm_Matrix4x4_inverse(K, 349, pose);
  TfpArmMatrix4x4 rel = arm_Matrix4x4_multiply(K, 349, hand, pose);
  TfpGripElements at = elements(g, rel);
  bool has_before = g->has_rel_at;
  TfpGripElements before = g->rel_at;
  double elapsed = g->has_rel_time ? now - g->rel_time : 0;
  g->rel_at = at; g->has_rel_at = true; g->rel_time = now; g->has_rel_time = true;
  bool has_seen = false;
  TfpArmMatrix4x4 seen;
  memset(&seen, 0, sizeof seen);
  if (g->has_body_ref) { /* :353 */
    hand = arm_Unit_world_pose(K, 353, body, g->body_right);
    pose = arm_Unit_world_pose(K, 353, body, g->body_ref);
    pose = arm_Matrix4x4_inverse(K, 353, pose);
    seen = arm_Matrix4x4_multiply(K, 353, hand, pose);
    has_seen = true;
  }
  bool has_seen_before = g->has_ref_at;
  TfpGripElements seen_before = g->ref_at;
  if (has_seen) { g->ref_at = elements(g, seen); g->has_ref_at = true; }
  else g->has_ref_at = false;
  if (has_before && elapsed > 0 && elapsed < .25) {
    double slid = slid_between(&at, &before, 0);
    double turned = turned_between(&at, &before);
    if (g->has_gun && !aimed && (turned > TURNING * elapsed || slid > SLIDING * elapsed)) {
      if (!g->has_rest) { g->rest = before; g->has_rest = true; g->rest_ref = seen_before; g->has_rest_ref = has_seen_before; }
      g->rest_keep = 1; g->spin_until = now + CALM; g->has_spin_until = true;
    }
    if (g->has_rest) {
      double moved = 0, shifted = 0;
      if (g->has_ref_at && has_seen_before) { moved = turned_between(&g->ref_at, &seen_before); shifted = slid_between(&g->ref_at, &seen_before, 0); }
      if (turned > STIR_TURN * elapsed || slid > STIR_SLIDE * elapsed || moved > STIR_TURN * elapsed || shifted > STIR_SLIDE * elapsed ||
          !g->has_stirred || now < g->stirred) { g->stirred = now; g->has_stirred = true; }
      double angle = turned_between(&at, &g->rest), gap = slid_between(&at, &g->rest, 0);
      bool away = angle > AWAY_TURN || gap > AWAY_SLIDE;
      if (!away && g->has_ref_at && g->has_rest_ref) {
        angle = turned_between(&g->ref_at, &g->rest_ref); gap = slid_between(&g->ref_at, &g->rest_ref, 0);
        away = angle > AWAY_TURN || gap > AWAY_SLIDE;
      }
      if (away && now - g->stirred < SETTLE) { g->rest_keep = 1; g->spin_until = now + CALM; g->has_spin_until = true; }
    }
  }
  if (!g->has_rest) return rel;
  if (aimed) { gun_release(g); return rel; }  /* never shift a weapon that is being aimed or that the rig is animating */
  if (g->has_spin_until && now < g->spin_until - CALM - .01) g->has_spin_until = false;
  if (!(g->has_spin_until && now < g->spin_until)) g->rest_keep = g->rest_keep - (elapsed > 0 ? lua_min(EASE, elapsed) / EASE : 1);
  if (g->rest_keep <= 0 || !(g->has_gun && usable(g, g->gun.handle, g->gun.count))) { gun_release(g); return rel; }
  TfpArmMatrix4x4 held = from_elements(g, 385, &g->rest);
  if (has_seen && g->has_rest_ref) {
    TfpArmMatrix4x4 was = from_elements(g, 387, &g->rest_ref);
    was = arm_Matrix4x4_inverse(K, 388, was);
    was = arm_Matrix4x4_multiply(K, 388, seen, was);
    held = arm_Matrix4x4_multiply(K, 388, was, held);
  }
  if (g->rest_keep < 1) {
    TfpArmQuaternion a = arm_Matrix4x4_rotation(K, 391, rel);
    TfpArmQuaternion b = arm_Matrix4x4_rotation(K, 391, held);
    TfpArmQuaternion q = arm_Quaternion_lerp(K, 391, a, b, g->rest_keep);
    TfpArmVector3 from = arm_Matrix4x4_translation(K, 392, rel);
    TfpArmVector3 to = arm_Matrix4x4_translation(K, 392, held);
    TfpArmVector3 p = arm_Vector3_lerp(K, 392, from, to, g->rest_keep);
    held = arm_Matrix4x4_from_quaternion_position(K, 392, q, p);
  }
  *spinning = true; *rel_out = rel;
  return held;
}
/* Shortest rotation taking one direction to another (:397-403). */
static TfpArmQuaternion arc(TfpGripHand *g, TfpArmVector3 from, TfpArmVector3 to) {
  from = arm_Vector3_normalize(K, 398, from);
  to = arm_Vector3_normalize(K, 398, to);
  TfpArmVector3 axis = arm_Vector3_cross(K, 399, from, to);
  double sine = arm_Vector3_length(K, 400, axis);
  double cosine = arm_Vector3_dot(K, 400, from, to);
  if (sine < 1e-6) return arm_Quaternion_identity(K, 401);
  TfpArmVector3 unit = arm_Vector3_normalize(K, 402, axis);
  return arm_Quaternion_axis_angle(K, 402, unit, atan2(sine, cosine));
}
/* Linked bones keep whatever local pose they were given: clear it once when the correction ends (:405-419).
 * only: TFP_GRIP_LEFT, TFP_GRIP_RIGHT, or -1 for both. */
static void release(TfpGripHand *g, int only) {
  for (int name = only < 0 ? TFP_GRIP_LEFT : only; name <= (only < 0 ? TFP_GRIP_RIGHT : only); name++) {
    if (g->drawn[name]) {
      g->drawn[name] = false;
      for (int p = 0; p < g->piece_count; p++) {
        const TfpGripPiece *piece = &g->pieces[p];
        if (usable(g, piece->handle, piece->count)) {
          for (int b = 0; b < piece->bones; b++) {
            const TfpGripBone *bone = &g->bones[piece->first + b];
            if (bone->side == name) {
              TfpArmMatrix4x4 identity = arm_Matrix4x4_identity(K, 412);
              arm_Unit_set_local_pose(K, 412, piece->handle, bone->node, identity);
            }
          }
        }
      }
    }
  }
  g->written = g->drawn[TFP_GRIP_LEFT] || g->drawn[TFP_GRIP_RIGHT];
}
/* shape_of(bone): a finger's pose relative to the hand. false: nil. */
typedef struct Shape {
  bool (*of)(TfpGripHand *g, const struct Shape *shape, const TfpGripBone *bone, TfpArmMatrix4x4 *out);
  int line;
  bool pinned;
  TfpArmMatrix4x4 from_hand;
} Shape;
static TfpArmMatrix4x4 move(TfpGripHand *g, TfpArmQuaternion old_rotation, TfpArmVector3 old_position, TfpArmQuaternion new_rotation,
                            TfpArmVector3 new_position) { /* :448-451 */
  TfpArmMatrix4x4 old = arm_Matrix4x4_from_quaternion_position(K, 449, old_rotation, old_position);
  old = arm_Matrix4x4_inverse(K, 449, old);
  TfpArmMatrix4x4 new = arm_Matrix4x4_from_quaternion_position(K, 450, new_rotation, new_position);
  return arm_Matrix4x4_multiply(K, 450, old, new);
}
static const struct { int shoulder, elbow, hand; } ARMS[2] = {{SHOULDER, ELBOW, HAND}, {RIGHT_SHOULDER, RIGHT_ELBOW, RIGHT_HAND}};
/* Bend one arm at shoulder and elbow so its hand is at T turned as `turn` (:423-473). *reached: the second value. */
static bool pose_arm(TfpGripHand *g, int name, TfpArmUnit rig, TfpArmVector3 S, TfpArmVector3 E, TfpArmVector3 H, TfpArmVector3 T,
                     TfpArmQuaternion turn, TfpArmQuaternion hand_old, const Shape *shape_of, TfpArmVector3 *reached_out) {
  if (!g->whole[name]) { g->partial = g->partial + 1; return false; }
  TfpArmVector3 E2, reached;
  if (!solve(K, S, E, H, T, &E2, &reached)) return false;
  T = reached;                           /* short of the target when the arm would have to straighten */
  TfpArmQuaternion shoulder_old = arm_Unit_world_rotation(K, 432, rig, ARMS[name].shoulder + 1);
  TfpArmQuaternion elbow_old = arm_Unit_world_rotation(K, 432, rig, ARMS[name].elbow + 1);
  TfpArmVector3 a = arm_sub(K, E, S);
  TfpArmVector3 b = arm_sub(K, E2, S);
  TfpArmQuaternion swing = arc(g, a, b);
  a = arm_sub(K, H, E);
  a = arm_Quaternion_rotate(K, 434, swing, a);
  b = arm_sub(K, T, E2);
  TfpArmQuaternion bend = arc(g, a, b);
  /* What the hand turns beyond the swung forearm, and the part of it around the forearm's axis. */
  TfpArmQuaternion carried = arm_Quaternion_multiply(K, 436, bend, swing);
  TfpArmQuaternion q = arm_Quaternion_inverse(K, 437, hand_old);
  q = arm_Quaternion_multiply(K, 437, turn, q);
  TfpArmQuaternion inverse = arm_Quaternion_inverse(K, 437, carried);
  TfpArmQuaternion extra = arm_Quaternion_multiply(K, 437, q, inverse);
  a = arm_sub(K, T, E2);
  TfpArmVector3 axis = arm_Vector3_normalize(K, 438, a);
  TfpArmElements e = arm_Quaternion_to_elements(K, 439, extra);
  double w = e.w;
  double along = times(e.x, arm_Vector3_x(K, 440, axis));
  along = along + times(e.y, arm_Vector3_y(K, 440, axis));
  along = along + times(e.z, arm_Vector3_z(K, 440, axis));
  double size = sqrt(times(along, along) + times(w, w));
  TfpArmQuaternion roll = arm_Quaternion_identity(K, 442);
  if (size > 1e-6) {
    if (w < 0) { along = -along; w = -w; }
    double x = arm_Vector3_x(K, 445, axis) * along / size;
    double y = arm_Vector3_y(K, 445, axis) * along / size;
    double z = arm_Vector3_z(K, 445, axis) * along / size;
    roll = arm_Quaternion_from_elements(K, 445, x, y, z, w / size);
  }
  /* For each part: the rigid move from its animated pose to its solved pose. */
  TfpArmMatrix4x4 moves[TFP_GRIP_HAND + 1];
  q = arm_Quaternion_multiply(K, 453, swing, shoulder_old);
  moves[TFP_GRIP_SHOULDER] = move(g, shoulder_old, S, q, S);
  q = arm_Quaternion_multiply(K, 454, carried, elbow_old);
  q = arm_Quaternion_multiply(K, 454, roll, q);
  moves[TFP_GRIP_ELBOW] = move(g, elbow_old, E, q, E2);
  moves[TFP_GRIP_HAND] = move(g, hand_old, H, turn, T);
  TfpArmMatrix4x4 hand_new = arm_Matrix4x4_from_quaternion_position(K, 457, turn, T);
  for (int p = 0; p < g->piece_count; p++) {
    const TfpGripPiece *piece = &g->pieces[p];
    for (int i = 0; i < piece->bones; i++) {
      const TfpGripBone *bone = &g->bones[piece->first + i];
      if (bone->side == name) {
        TfpArmMatrix4x4 base = arm_Unit_world_pose(K, 461, rig, bone->rig);
        TfpArmMatrix4x4 shape, pose;
        if (bone->finger && shape_of->of(g, shape_of, bone, &shape)) {
          pose = arm_Matrix4x4_multiply(K, 464, shape, hand_new);
          base = arm_Matrix4x4_inverse(K, 464, base);
          pose = arm_Matrix4x4_multiply(K, 464, pose, base);
          arm_Unit_set_local_pose(K, 464, piece->handle, bone->node, pose);
        } else {
          pose = arm_Matrix4x4_multiply(K, 466, base, moves[bone->part]);
          base = arm_Matrix4x4_inverse(K, 466, base);
          pose = arm_Matrix4x4_multiply(K, 466, pose, base);
          arm_Unit_set_local_pose(K, 466, piece->handle, bone->node, pose);
        }
      }
    }
  }
  g->drawn[name] = true; g->written = true;
  *reached_out = T;
  return true;
}
/* The rig's own two-material mesh, off while the left arm is corrected (:475-494). */
static void rig_screen(TfpGripHand *g, TfpArmUnit rig, bool hide) {
  const TfpArmEngine *e = g->calls.e;
  if (!(e->Mesh_visibility || e->Mesh_num_materials) || !e->Unit_set_mesh_visibility || !e->Unit_num_meshes) return;
  if (g->has_hidden && (!hide || g->hidden.rig != rig)) {
    if (usable(g, g->hidden.rig, RIG_NODES))
      for (int i = 0; i < g->hidden.count; i++) arm_Unit_set_mesh_visibility(K, 480, g->hidden.rig, g->hidden.meshes[i], true);
    g->has_hidden = false;
  }
  if (hide && !g->has_hidden && rig) {
    double meshes[TFP_GRIP_MESHES];
    int count = 0;
    double limit = arm_Unit_num_meshes(K, 486, rig);
    for (double index = 1; index <= limit; index = index + 1) {
      TfpArmMesh mesh = arm_Unit_mesh(K, 487, rig, index);
      if (arm_Mesh_visibility(K, 488, mesh) == true && arm_Mesh_num_materials(K, 488, mesh) == 2) {
        arm_Unit_set_mesh_visibility(K, 489, rig, index, false);
        if (count == TFP_GRIP_MESHES) full(g);
        meshes[count++] = index;
      }
    }
    g->hidden.rig = rig; g->hidden.count = count;
    memcpy(g->hidden.meshes, meshes, sizeof *meshes * (size_t)count);
    g->has_hidden = true;
  }
}

/* known[handle]; NULL: nil. */
static TfpGripKnown *known_of(TfpGripHand *g, TfpArmUnit handle) {
  for (int i = 0; i < g->known_count; i++) if (g->known[i].handle == handle) return &g->known[i];
  return NULL;
}
/* shapes = {}: a table no remembered weapon holds. */
static void new_shapes(TfpGripHand *g) {
  for (int set = 0; set <= TFP_GRIP_KNOWN; set++) {
    bool held = false;
    for (int i = 0; i < g->known_count; i++) held = held || g->known[i].shapes == set;
    if (!held) { memset(g->sets[set].has, 0, sizeof g->sets[set].has); g->shapes = set; return; }
  }
}
static bool left_shape(TfpGripHand *g, const Shape *shape, const TfpGripBone *bone, TfpArmMatrix4x4 *out) { /* :549-555 */
  TfpGripShapes *shapes = &g->sets[g->shapes];
  if (shape->pinned) {
    TfpArmMatrix4x4 finger = arm_Unit_world_pose(K, 551, g->body, bone->finger);
    shapes->pose[bone->finger] = arm_Matrix4x4_multiply(K, 551, finger, shape->from_hand);
    shapes->has[bone->finger] = true;
  }
  if (!shapes->has[bone->finger]) return false;
  *out = shapes->pose[bone->finger];
  return true;
}
static bool body_shape(TfpGripHand *g, const Shape *shape, const TfpGripBone *bone, TfpArmMatrix4x4 *out) { /* :581, :612 */
  TfpArmMatrix4x4 finger = arm_Unit_world_pose(K, shape->line, g->body, bone->finger);
  *out = arm_Matrix4x4_multiply(K, shape->line, finger, shape->from_hand);
  return true;
}
static const char *left_arm(TfpGripHand *g, TfpArmUnit rig, double now, double calm) { /* :496-558 */
  TfpArmVector3 S = arm_Unit_world_position(K, 497, rig, SHOULDER + 1);
  TfpArmVector3 E = arm_Unit_world_position(K, 497, rig, ELBOW + 1);
  TfpArmVector3 H = arm_Unit_world_position(K, 497, rig, HAND + 1);
  /* The grip bone in use is the one the body's left hand has been on (see the header). */
  bool has_held = false;
  TfpArmVector3 held = {0, 0, 0};
  if (usable(g, g->body, g->body_count)) { held = arm_Unit_world_position(K, 499, g->body, g->body_hand); has_held = true; }
  int grip = -1, kept = -1;
  double off = 0, kept_off = 0;
  for (int c = 0; c < g->candidate_count; c++) {
    TfpGripCandidate *candidate = &g->candidates[c];
    TfpArmVector3 at = arm_Unit_world_position(K, 502, candidate->handle, candidate->node);
    double d = HUGE_NUMBER;
    if (has_held) {
      TfpArmVector3 apart = arm_sub(K, at, held);
      d = arm_Vector3_length(K, 505, apart);
      if (candidate->has_at) {
        TfpArmVector3 before = arm_Vector3(K, 507, candidate->at[0], candidate->at[1], candidate->at[2]);
        apart = arm_sub(K, before, held);
        d = lua_min(d, arm_Vector3_length(K, 507, apart));
      }
    }
    double x = arm_Vector3_x(K, 509, at), y = arm_Vector3_y(K, 509, at), z = arm_Vector3_z(K, 509, at);
    candidate->at[0] = x; candidate->at[1] = y; candidate->at[2] = z; candidate->has_at = true;
    candidate->d = d;
    if (grip < 0 || d < off) { grip = c; off = d; }
    if (g->has_current && candidate->handle == g->current.handle && candidate->node == g->current.node) { kept = c; kept_off = d; }
  }
  /* Keep the bone in use unless the hand is on another. */
  if (kept >= 0 && !(kept_off > PINNED && off <= PINNED)) { grip = kept; off = kept_off; }
  const TfpGripKnown *memory = NULL;
  if ((kept < 0 || !g->on) && !(grip >= 0 && off <= PINNED)) {
    for (int c = 0; c < g->candidate_count; c++) {
      const TfpGripCandidate *candidate = &g->candidates[c];
      const TfpGripKnown *seen = known_of(g, candidate->handle);
      if (seen && seen->node == candidate->node && seen->count == candidate->count) { grip = c; off = candidate->d; memory = seen; }
    }
  }
  if (grip < 0) {
    g->has_current = false; g->on = false; g->has_off = false; g->has_distance = false;
    release(g, TFP_GRIP_LEFT);
    return "no grip bone";
  }
  if (kept < 0 || grip != kept || (memory && !g->on)) {
    if (memory) { g->on = true; g->pin = 1; g->shapes = memory->shapes; g->recalled = g->recalled + 1; }
    else { g->on = false; new_shapes(g); }
  }
  const TfpGripCandidate *chosen = &g->candidates[grip];
  g->current.handle = chosen->handle; g->current.node = chosen->node; g->has_current = true;
  bool pinned = off <= PINNED;
  if (pinned) {
    g->on = true;
    TfpGripKnown *seen = known_of(g, chosen->handle);
    if (!seen || seen->node != chosen->node || seen->shapes != g->shapes) {
      if (!seen) {
        if (g->known_count == TFP_GRIP_KNOWN) {            /* not in the Lua: the oldest remembered weapon makes room */
          memmove(g->known, g->known + 1, sizeof *g->known * (TFP_GRIP_KNOWN - 1));
          g->known_count--;
        }
        seen = &g->known[g->known_count++];
      }
      *seen = (TfpGripKnown){chosen->handle, chosen->node, chosen->count, g->shapes};
    }
  }
  g->has_off = off != HUGE_NUMBER; g->off = off;
  TfpArmVector3 at = arm_Unit_world_position(K, 535, chosen->handle, chosen->node);
  at = arm_sub(K, at, H);
  double distance = arm_Vector3_length(K, 535, at);
  g->distance = distance; g->has_distance = true;
  if (g->piece_count == 0) return "no arm pieces on the rig";
  double step = g->has_last ? lua_max(0, lua_min(EASE, now - g->last)) / EASE : 1;
  g->last = now; g->has_last = true;
  g->pin = g->on ? lua_min(1, g->pin + step) : lua_max(0, g->pin - step);
  double weight = tfp_grip_hand_weight(distance) * g->pin * calm;
  if (weight <= 0) {
    release(g, TFP_GRIP_LEFT);
    return calm <= 0 ? "weapon is being handled" : g->on ? "hand away from the grip" : "third-person hand is not on a grip";
  }
  at = arm_Unit_world_position(K, 545, chosen->handle, chosen->node);
  TfpArmVector3 T = arm_Vector3_lerp(K, 545, H, at, weight);
  TfpArmQuaternion hand_old = arm_Unit_world_rotation(K, 546, rig, HAND + 1);
  TfpArmQuaternion rotation = arm_Unit_world_rotation(K, 547, chosen->handle, chosen->node);
  TfpArmQuaternion turn = arm_Quaternion_lerp(K, 547, hand_old, rotation, weight);
  Shape shape = {left_shape, 551, pinned, {{0}}};
  if (pinned) {
    TfpArmMatrix4x4 hand = arm_Unit_world_pose(K, 548, g->body, g->body_hand);
    shape.from_hand = arm_Matrix4x4_inverse(K, 548, hand);
  }
  TfpArmVector3 reached;
  if (!pose_arm(g, TFP_GRIP_LEFT, rig, S, E, H, T, turn, hand_old, &shape, &reached)) { release(g, TFP_GRIP_LEFT); return "arm is straight"; }
  return "active";
}
static const char *right_arm(TfpGripHand *g, TfpArmUnit rig, double now, double calm, bool aimed) { /* :559-590 */
  if (!(g->has_rig_attach && g->has_body_right && g->has_body_attach && usable(g, g->body, g->body_count))) {
    release(g, TFP_GRIP_RIGHT); gun_release(g);
    return "no third-person right hand";
  }
  TfpArmVector3 S = arm_Unit_world_position(K, 563, rig, RIGHT_SHOULDER + 1);
  TfpArmVector3 E = arm_Unit_world_position(K, 563, rig, RIGHT_ELBOW + 1);
  TfpArmVector3 H = arm_Unit_world_position(K, 563, rig, RIGHT_HAND + 1);
  /* Only for a weapon held in one hand (the left arm's pass found no grip for the body's left hand). */
  bool one_handed = !strcmp(g->reason, "third-person hand is not on a grip") || !strcmp(g->reason, "no grip bone");
  bool spinning;
  TfpArmMatrix4x4 rel;
  TfpArmMatrix4x4 held = in_hand(g, now, aimed || !one_handed || rig_moving(g, rig, now), &spinning, &rel);
  TfpArmMatrix4x4 attach = arm_Unit_world_pose(K, 570, rig, g->rig_attach);
  TfpArmMatrix4x4 carried = arm_Matrix4x4_multiply(K, 571, held, attach);
  TfpArmVector3 target = arm_Matrix4x4_translation(K, 572, carried);
  TfpArmVector3 apart = arm_sub(K, target, H);
  double distance = arm_Vector3_length(K, 573, apart);
  g->right_distance = distance; g->has_right_distance = true;
  double weight = tfp_grip_hand_weight(distance) * calm;
  if (weight <= 0) {
    release(g, TFP_GRIP_RIGHT); gun_release(g);
    return calm <= 0 ? "weapon is being handled" : "hand away from the weapon";
  }
  TfpArmQuaternion hand_old = arm_Unit_world_rotation(K, 577, rig, RIGHT_HAND + 1);
  Shape shape = {body_shape, 581, false, {{0}}};
  TfpArmMatrix4x4 hand = arm_Unit_world_pose(K, 578, g->body, g->body_right);
  shape.from_hand = arm_Matrix4x4_inverse(K, 578, hand);
  TfpArmQuaternion rotation = arm_Matrix4x4_rotation(K, 579, carried);
  TfpArmQuaternion turn = arm_Quaternion_lerp(K, 579, hand_old, rotation, weight);
  TfpArmVector3 T = arm_Vector3_lerp(K, 580, H, target, weight);
  TfpArmVector3 reached;
  if (!pose_arm(g, TFP_GRIP_RIGHT, rig, S, E, H, T, turn, hand_old, &shape, &reached)) {
    release(g, TFP_GRIP_RIGHT); gun_release(g);
    return "arm is straight";
  }
  if (spinning) {
    /* The weapon goes where the hand really ended up, so it cannot leave the hand. */
    TfpArmMatrix4x4 pose = arm_Matrix4x4_inverse(K, 586, rel);
    TfpArmMatrix4x4 ended = arm_Matrix4x4_from_quaternion_position(K, 586, turn, reached);
    pose = arm_Matrix4x4_multiply(K, 586, pose, ended);
    attach = arm_Matrix4x4_inverse(K, 586, attach);
    pose = arm_Matrix4x4_multiply(K, 586, pose, attach);
    arm_Unit_set_local_pose(K, 586, g->gun.handle, 1, pose);
    g->gun_turned = true; g->spun = g->spun + 1;
  }
  return "active";
}
/* A one-handed spin: the left hand goes where the body's is (:596-615). */
static void left_aside(TfpGripHand *g, TfpArmUnit rig, double now) {
  double elapsed = g->has_aside_time ? now - g->aside_time : 0;
  g->aside_time = now; g->has_aside_time = true;
  bool is_free = !strcmp(g->reason, "third-person hand is not on a grip");
  bool spinning = is_free && g->has_rest && g->has_rest_ref && g->has_spin_until && now < g->spin_until;
  if (spinning) { g->aside_rest = g->rest; g->has_aside_rest = true; g->aside_ref = g->rest_ref; }
  double step = elapsed > 0 && elapsed < .25 ? elapsed / ASIDE : 0;
  if (spinning) g->aside = lua_min(1, g->aside + step);
  else g->aside = step > 0 ? lua_max(0, g->aside - step) : 0;
  if (g->aside <= 0 || !(is_free && g->has_aside_rest && g->has_body_ref && g->has_rig_attach && usable(g, g->body, g->body_count))) {
    g->aside = spinning ? g->aside : 0;
    return;
  }
  TfpArmVector3 S = arm_Unit_world_position(K, 605, rig, SHOULDER + 1);
  TfpArmVector3 E = arm_Unit_world_position(K, 605, rig, ELBOW + 1);
  TfpArmVector3 H = arm_Unit_world_position(K, 605, rig, HAND + 1);
  TfpArmMatrix4x4 hand = arm_Unit_world_pose(K, 606, g->body, g->body_hand);
  TfpArmMatrix4x4 root = arm_Unit_world_pose(K, 606, g->body, g->body_ref);
  root = arm_Matrix4x4_inverse(K, 606, root);
  TfpArmMatrix4x4 seen = arm_Matrix4x4_multiply(K, 606, hand, root);
  TfpArmMatrix4x4 was = from_elements(g, 604, &g->aside_ref);
  was = arm_Matrix4x4_inverse(K, 607, was);
  TfpArmMatrix4x4 first = arm_Matrix4x4_multiply(K, 607, seen, was);
  TfpArmMatrix4x4 rest = from_elements(g, 604, &g->aside_rest);
  TfpArmMatrix4x4 attach = arm_Unit_world_pose(K, 607, rig, g->rig_attach);
  rest = arm_Matrix4x4_multiply(K, 607, rest, attach);
  TfpArmMatrix4x4 pose = arm_Matrix4x4_multiply(K, 607, first, rest);
  double weight = times(g->aside, g->aside) * (3 - times(2, g->aside));
  TfpArmQuaternion hand_old = arm_Unit_world_rotation(K, 609, rig, HAND + 1);
  Shape shape = {body_shape, 612, false, {{0}}};
  hand = arm_Unit_world_pose(K, 610, g->body, g->body_hand);
  shape.from_hand = arm_Matrix4x4_inverse(K, 610, hand);
  TfpArmVector3 to = arm_Matrix4x4_translation(K, 611, pose);
  TfpArmVector3 T = arm_Vector3_lerp(K, 611, H, to, weight);
  TfpArmQuaternion rotation = arm_Matrix4x4_rotation(K, 611, pose);
  TfpArmQuaternion turn = arm_Quaternion_lerp(K, 611, hand_old, rotation, weight);
  TfpArmVector3 reached;
  if (pose_arm(g, TFP_GRIP_LEFT, rig, S, E, H, T, turn, hand_old, &shape, &reached)) set_text(g->reason, "moved aside");
}

/* tostring(s.weapon or s.request_weapon)..(s.weapon_transition==true and ' moving' or '') */
static void weapon_text(const TfpArmSnapshot *s, char *out, size_t size) {
  bool has = s->has_weapon || s->has_request_weapon;
  double value = s->has_weapon ? s->weapon : s->request_weapon;
  char number[32];
  if (!has) snprintf(number, sizeof number, "nil");
  else if (value != value) snprintf(number, sizeof number, "nan");
  else if (value == HUGE_VAL || value == -HUGE_VAL) snprintf(number, sizeof number, value > 0 ? "inf" : "-inf");
  else snprintf(number, sizeof number, "%.14g", value);
  snprintf(out, size, "%s%s", number, s->weapon_transition ? " moving" : "");
}
typedef struct UpdateCall { TfpGripHand *g; const TfpArmSnapshot *s; bool active; const TfpArmHandles *handles; } UpdateCall;
static void update_body(void *argument) { /* :617-661 */
  UpdateCall *call = argument;
  TfpGripHand *g = call->g;
  const TfpArmSnapshot *s = call->s;
  const TfpArmHandles *handles = call->handles;
  if (!call->active || !s || !handles) {
    if (g->written) { bind(g); release(g, -1); }
    if (g->gun_turned) bind(g);
    gun_release(g);
    g->has_rel_at = g->has_rel_time = g->has_ref_at = false;
    g->aside = 0; g->has_aside_time = false;
    g->has_rig_at = g->has_rig_time = g->has_rig_until = false;
    if (g->has_hidden) { bind(g); rig_screen(g, NULL, false); }
    g->was_active = false; g->has_last = false; g->soon_count = 0;
    set_text(g->reason, "waiting"); set_text(g->right, "waiting"); g->has_right = true;
    return;
  }
  bind(g);
  TfpArmUnit rig = arm_handle(handles, s->has_viewmodel, s->viewmodel);
  if (!usable(g, rig, RIG_NODES)) {
    release(g, -1); gun_release(g); rig_screen(g, NULL, false);
    set_text(g->reason, "rig unavailable"); set_text(g->right, g->reason); g->has_right = true;
    return;
  }
  double now = arm_clock(K);
  bool lost = false;
  for (int c = 0; c < g->candidate_count; c++) lost = lost || !usable(g, g->candidates[c].handle, g->candidates[c].count);
  for (int p = 0; p < g->piece_count; p++) lost = lost || !usable(g, g->pieces[p].handle, g->pieces[p].count);
  bool missing = g->candidate_count == 0 || !g->complete || g->right_partial;
  bool aiming = s->aiming || s->shoulder_aim;
  bool turned = g->was_active && g->aim != (int)aiming;
  g->aim = aiming;
  if (!g->was_active) { g->began = now; g->has_began = true; }
  if (!g->was_active || turned) { g->confirm = now + CONFIRM; g->has_confirm = true; }
  char id[sizeof g->weapon_id];
  weapon_text(s, id, sizeof id);
  bool swapped = g->was_active && !(g->has_weapon_id && !strcmp(id, g->weapon_id));
  memcpy(g->weapon_id, id, sizeof id); g->has_weapon_id = true;
  if (swapped) {
    for (int index = 0; index < 3; index++) g->soon[index] = now + SWAP[index];
    g->soon_count = 3;
    g->has_current = false; g->on = false; g->pin = 0;
    gun_release(g);
    g->swaps = g->swaps + 1;
  }
  if (g->soon_count > 0 && (now >= g->soon[0] || now < g->soon[0] - 1)) {
    memmove(g->soon, g->soon + 1, sizeof *g->soon * 2);
    g->soon_count--;
    swapped = true;
  }
  bool again = g->has_confirm && (now >= g->confirm || now < g->confirm - CONFIRM - .01);
  if (again) g->has_confirm = false;
  again = again || turned || swapped || (g->has_periodic && (now >= g->periodic || now < g->periodic - PERIOD - .01));
  if (!g->was_active || !(g->has_seen_handles && g->seen_handles == handles->identity) || lost || again ||
      (missing && ((g->has_began && now >= g->began && now - g->began < QUICK) || !g->has_next_scan || now >= g->next_scan ||
                   now < g->next_scan - RESCAN))) {
    release(g, -1);                      /* skips pieces that are gone; the live ones must not keep an offset */
    scan(g, s, handles);
    g->next_scan = now + RESCAN; g->has_next_scan = true;
    g->periodic = now + PERIOD; g->has_periodic = true;
  }
  g->was_active = true;
  double calm = 1;
  set_text(g->reason, left_arm(g, rig, now, calm));
  set_text(g->right, right_arm(g, rig, now, calm, s->aiming)); g->has_right = true;
  left_aside(g, rig, now);
  if (g->drawn[TFP_GRIP_LEFT] != g->has_hidden || (g->has_hidden && g->hidden.rig != rig)) rig_screen(g, rig, g->drawn[TFP_GRIP_LEFT]);
  if (!strcmp(g->reason, "active") || !strcmp(g->reason, "moved aside") || !strcmp(g->right, "active")) g->frames = g->frames + 1;
}
bool tfp_grip_hand_update(TfpGripHand *g, const TfpArmSnapshot *s, bool active, const TfpArmHandles *handles) { /* :616-664 */
  UpdateCall call = {g, s, active, handles};
  bool ok = arm_protect(K, update_body, &call);
  if (!ok) { set_text(g->reason, g->calls.error.text); g->errors = g->errors + 1; }
  return ok;
}
static void stop_release(void *argument) { /* :666 */
  TfpGripHand *g = argument;
  if (g->written) { bind(g); release(g, -1); }
}
static void stop_gun(void *argument) { /* :667 */
  TfpGripHand *g = argument;
  if (g->gun_turned) bind(g);
  gun_release(g);
}
static void stop_screen(void *argument) { /* :670 */
  TfpGripHand *g = argument;
  if (g->has_hidden) { bind(g); rig_screen(g, NULL, false); }
}
bool tfp_grip_hand_stop(TfpGripHand *g) { /* :665-681 */
  arm_protect(K, stop_release, g);
  arm_protect(K, stop_gun, g);
  g->has_gun = false; g->gun_turned = false; g->has_rel_at = g->has_rel_time = g->has_ref_at = g->has_body_ref = false;
  g->aside = 0; g->has_aside_time = g->has_aside_rest = false;
  g->has_rig_at = g->has_rig_time = g->has_rig_until = false;
  arm_protect(K, stop_screen, g);
  g->has_hidden = false;
  g->candidate_count = g->piece_count = g->bone_count = 0;
  g->has_seen_handles = g->has_next_scan = false; g->was_active = false; g->written = false;
  g->complete = false; g->has_confirm = false; g->aim = -1; g->has_periodic = false;
  g->body = NULL; g->body_hand = 0; g->body_count = 0;
  g->has_body_right = g->has_body_attach = g->has_rig_attach = false;
  g->drawn[TFP_GRIP_LEFT] = g->drawn[TFP_GRIP_RIGHT] = false;
  g->has_current = false; g->on = false; g->pin = 0; g->has_last = false;
  g->known_count = 0; new_shapes(g);
  g->has_began = false;
  set_text(g->reason, "idle");
  return true;
}
bool tfp_grip_hand_is_clean(const TfpGripHand *g) { return g->written == false && !g->has_hidden && g->gun_turned == false; } /* :682 */
void tfp_grip_hand_result(const TfpGripHand *g, TfpGripHandResult *out) { /* :683-688 */
  double fingers = 0;
  for (int b = 0; b < g->bone_count; b++) if (g->bones[b].finger) fingers = fingers + 1;
  memset(out, 0, sizeof *out);
  out->frames = g->frames; out->scans = g->scans; out->weapons = g->candidate_count; out->pieces = g->piece_count; out->bones = g->bone_count;
  out->fingers = fingers; out->has_distance = g->has_distance; out->distance = g->distance; out->has_off = g->has_off; out->off = g->off;
  out->errors = g->errors; out->reason = g->reason; out->right = g->has_right ? g->right : NULL; out->rig_screen_hidden = g->has_hidden;
  out->steady = 1; out->handled = 0; out->turning = 0; out->sliding = 0;
  out->swaps = g->swaps; out->recalled = g->recalled; out->partial = g->partial; out->spun = g->spun;
  out->gun = g->has_gun; out->has_arm_root = g->has_body_ref; out->arm_root = g->body_ref; out->gun_turned = g->gun_turned;
  out->has_right_distance = g->has_right_distance; out->right_distance = g->right_distance;
}
