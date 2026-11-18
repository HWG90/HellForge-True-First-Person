/* Translation of retail src/first_person/arm_pieces.lua (0.5.41-smooth.66,
 * commit 1d0d453). Numbers in comments and in the engine calls are that
 * file's lines. See arm_pieces.h and docs/translation/ARMS-TRANSLATION.md.
 *
 * A raised Lua error is a longjmp to the pcall the retail function wraps its
 * body in (arm_engine_calls.h). Where the Lua evaluates several engine calls
 * inside one expression they are written out one by one here, in Lua's order
 * (left to right, arguments before the call). */
#include "arm_pieces.h"
#include <math.h>

#define CHUNK "src/first_person/arm_pieces.lua"
#define K (&a->calls)
#define LUA_PI 3.14159265358979323846
/* :26-31 */
#define CHEST 0x37EF0820
#define SPINE_A 0x58D886FB
#define SPINE_B 0x5A4BBAA2
#define GROUP 0x315CD161
#define MAX_NODES 255
#define RESCAN .25
#define TOLERANCE .02

void tfp_arm_pieces_module_init(TfpArmPiecesModule *module) { /* :27-30 */
  memset(module, 0, sizeof *module);
  module->sides[0].collar = 0xC4787B4E; module->sides[0].shoulder = 0x03752C98;   /* left */
  module->sides[1].collar = 0xB2BCD839; module->sides[1].shoulder = 0x1620B2CE;   /* right */
}

void tfp_arm_pieces_init(TfpArmPieces *a, TfpArmPiecesModule *module, const TfpArmBackend *backend, const TfpArmEngine *engine) { /* :33-39 */
  memset(a, 0, sizeof *a);
  a->module = module;
  a->calls.e = engine;
  a->calls.b = backend;
  a->calls.chunk = CHUNK;
  snprintf(a->reason, sizeof a->reason, "idle");
}

static void bind(TfpArmPieces *a) { /* :41-48 */
  const TfpArmEngine *e = a->calls.e;
  bool vector = e->Vector3_call || e->Vector3_length || e->Vector3_dot || e->Vector3_cross || e->Vector3_normalize || e->Vector3_lerp ||
                e->Vector3_x || e->Vector3_y || e->Vector3_z;
  bool operators = e->Vector3_op_add && e->Vector3_op_sub && e->Vector3_op_mul && e->Vector3_op_unm;   /* not in the Lua: see the header */
  if (!(vector && e->World_link_unit && e->Unit_set_local_pose && e->Unit_world_pose && e->Quaternion_axis_angle && e->Quaternion_rotate &&
        e->Quaternion_multiply && e->Matrix4x4_from_quaternion_position && operators))
    arm_fail(K, 43, "Arm piece SDK unavailable");
}
static bool usable(TfpArmPieces *a, TfpArmUnit handle, double count) { /* :49-51 */
  return handle != NULL && arm_Unit_alive(K, 50, handle) == true && arm_Unit_num_scene_graph_items(K, 50, handle) == count;
}
static double parent(TfpArmPieces *a, double address) { /* :55-58 */
  uint8_t b[8];
  arm_read(K, address + 0x1D0, 8, b);
  return (double)b[0] + (double)b[1] * 256 + (double)b[2] * 65536 + (double)b[3] * 16777216 + (double)b[4] * 4294967296.0 +
         (double)b[5] * 1099511627776.0;
}
/* Bone name hashes of one unit (:60-70). The Lua builds {hash -> index}: of two bones with one name the later wins. */
typedef struct Names { size_t count; uint8_t bytes[MAX_NODES * 4]; } Names;
static double names(TfpArmPieces *a, double address, Names *out) {
  double count = arm_u32(K, address + 0x70);
  if (!(count > 0 && count <= MAX_NODES)) arm_fail(K, 62, "Arm piece bone count out of range");
  double at = arm_ptr(K, address + 0xA0);
  arm_read(K, at, count * 4, out->bytes);
  out->count = (size_t)count;
  return count;
}
static bool named(const Names *n, double hash, double *index) {
  for (size_t i = n->count; i-- > 0;)
    if (arm_word(n->bytes, i) == hash) { *index = (double)i; return true; }
  return false;
}

static void release(TfpArmPieces *a) { /* :82-94 */
  if (a->hung) {
    a->hung = false;
    for (int p = 0; p < a->piece_count; p++) {
      TfpArmPiece *piece = &a->pieces[p];
      if (usable(a, piece->handle, piece->count)) {
        for (int b = 0; b < piece->bone_count; b++) {
          TfpArmMatrix4x4 identity = arm_Matrix4x4_identity(K, 89);
          arm_Unit_set_local_pose(K, 89, piece->handle, piece->bones[b].index + 1, identity);
        }
      }
      piece->has_placed = false;
    }
  }
}
static void restore(TfpArmPieces *a) { /* :96-108 */
  bool was = a->hung;
  release(a);
  if (!was || !a->has_identity) return;
  TfpArmWorld world = arm_Application_main_world(K, 100);
  for (int p = 0; p < a->piece_count; p++) {
    TfpArmPiece *piece = &a->pieces[p];
    if (usable(a, piece->handle, piece->count) && usable(a, a->identity.rig, a->identity.rig_count)) {
      for (int b = 0; b < piece->bone_count; b++) {
        const TfpArmPiecesBone *bone = &piece->bones[b];
        if (bone->has_home) arm_World_link_unit(K, 104, world, piece->handle, bone->index + 1, a->identity.rig, bone->home + 1);
      }
    }
  }
}
static void forget(TfpArmPieces *a) { /* :109-111 */
  a->piece_count = 0;
  a->has_identity = false;
  a->hung = false;
  a->has_next_scan = false;
  a->has_confirm_at = false;
}

static void scan(TfpArmPieces *a, const TfpArmSnapshot *s, const TfpArmHandles *handles) { /* :113-161 */
  static const double order[TFP_ARM_PIECES_BONES] = {CHEST, 0, SPINE_A, SPINE_B, GROUP};
  Names rig_names, body_names, piece_names;
  TfpArmPiece found[TFP_ARM_PIECES_MAX];
  int found_count = 0;
  a->scans = a->scans + 1;
  double rig_address = arm_object(K, s, s->has_viewmodel, s->viewmodel);
  double body_address = arm_object(K, s, s->has_body, s->body);
  double rig_count = names(a, rig_address, &rig_names);
  double body_count = names(a, body_address, &body_names);
  for (int i = 0; i < 2; i++) { /* :119-121 */
    TfpArmPiecesSide *side = &a->module->sides[i];
    side->has_rig_shoulder = named(&rig_names, side->shoulder, &side->rig_shoulder);
    side->has_body_shoulder = named(&body_names, side->shoulder, &side->body_shoulder);
  }
  for (size_t row = 0; row < handles->count; row++) { /* :122 */
    double ref = handles->rows[row].ref, address = 0;
    TfpArmUnit handle = handles->rows[row].unit;
    if (!handle) continue;                                  /* a Lua table holds no nil */
    if (!(ref != s->viewmodel && ref != s->body)) continue;
    if (!arm_object_protected(K, s, ref, &address)) continue;
    if (!(parent(a, address) == rig_address)) continue;
    double count = names(a, address, &piece_names);
    int side = -1;
    double unused;
    for (int i = 0; i < 2; i++) {
      const TfpArmPiecesSide *candidate = &a->module->sides[i];
      if (named(&piece_names, candidate->collar, &unused) && candidate->has_rig_shoulder && candidate->has_body_shoulder) side = i;
    }
    if (side >= 0 && usable(a, handle, count)) {
      uint8_t home[MAX_NODES * 4];                          /* game_links (:72-80) */
      double links_at = arm_ptr(K, address + 0x98);
      arm_read(K, links_at, count * 4, home);
      TfpArmPiece piece;
      memset(&piece, 0, sizeof piece);
      /* The chest goes first: it is the bone the re-link check watches. */
      for (int h = 0; h < TFP_ARM_PIECES_BONES; h++) {
        double hash = h == 1 ? a->module->sides[side].collar : order[h], index, source;
        if (named(&piece_names, hash, &index) && named(&body_names, hash, &source)) {
          /* Once hung, the link table shows this module's link: keep what the game had. */
          const TfpArmPiecesHomes *known = NULL;
          if (a->has_identity)
            for (int i = 0; i < a->identity.homes_count; i++) if (a->identity.homes[i].ref == ref) known = &a->identity.homes[i];
          bool has_target = arm_low(home, (size_t)index) == 2;
          double target = arm_high(home, (size_t)index);
          if (known) {
            has_target = false;
            for (int i = 0; i < known->count; i++)
              if (known->bones[i].index == index) { has_target = known->bones[i].has_home; target = known->bones[i].home; }
          }
          piece.bones[piece.bone_count++] = (TfpArmPiecesBone){index, source, has_target, has_target ? target : 0};
        }
      }
      double chest;
      if (piece.bone_count > 0 && named(&piece_names, CHEST, &chest) && chest == piece.bones[0].index) {
        if (found_count == TFP_ARM_PIECES_MAX) {            /* not in the Lua: its table has no end */
          snprintf(a->calls.error.text, sizeof a->calls.error.text, "Arm piece table full");
          arm_raise(K);
        }
        piece.ref = ref; piece.handle = handle; piece.count = count; piece.side = side;
        found[found_count++] = piece;
      }
    }
  }
  /* :152-160 */
  a->identity.homes_count = found_count;
  for (int p = 0; p < found_count; p++) {
    TfpArmPiecesHomes *homes = &a->identity.homes[p];
    homes->ref = found[p].ref;
    homes->count = found[p].bone_count;
    for (int b = 0; b < found[p].bone_count; b++) {
      homes->bones[b].index = found[p].bones[b].index;
      homes->bones[b].has_home = found[p].bones[b].has_home;
      homes->bones[b].home = found[p].bones[b].home;
    }
  }
  a->has_identity = true;
  a->identity.has_body_ref = s->has_body; a->identity.body_ref = s->body;
  a->identity.has_rig_ref = s->has_viewmodel; a->identity.rig_ref = s->viewmodel;
  a->identity.body = arm_handle(handles, s->has_body, s->body);
  a->identity.rig = arm_handle(handles, s->has_viewmodel, s->viewmodel);
  a->identity.has_body_chest = named(&body_names, CHEST, &a->identity.body_chest);
  a->identity.has_body_spine = named(&body_names, SPINE_B, &a->identity.body_spine);
  a->identity.body_count = body_count; a->identity.rig_count = rig_count;
  a->identity.handles = handles->identity;
  memcpy(a->pieces, found, sizeof *found * (size_t)found_count);
  a->piece_count = found_count;
}
static void hang(TfpArmPieces *a) { /* :162-171 */
  TfpArmWorld world = arm_Application_main_world(K, 163);
  for (int p = 0; p < a->piece_count; p++) {
    TfpArmPiece *piece = &a->pieces[p];
    for (int b = 0; b < piece->bone_count; b++) {
      const TfpArmPiecesSide *side = &a->module->sides[piece->side];
      arm_number(K, side->has_rig_shoulder, 166, "rig_shoulder");
      arm_World_link_unit(K, 166, world, piece->handle, piece->bones[b].index + 1, a->identity.rig, side->rig_shoulder + 1);
    }
    piece->has_placed = false;
  }
  a->hung = true; a->links = a->links + 1;
}

/* Shortest rotation taking one direction to another (:175-180). false: nil. */
static bool arc(TfpArmPieces *a, TfpArmVector3 from, TfpArmVector3 to, TfpArmVector3 half_turn_axis, TfpArmQuaternion *out) {
  TfpArmVector3 axis = arm_Vector3_cross(K, 176, from, to);
  double sine = arm_Vector3_length(K, 177, axis);
  double cosine = arm_Vector3_dot(K, 177, from, to);
  if (sine < 1e-6) {
    if (!(cosine < 0)) return false;
    *out = arm_Quaternion_axis_angle(K, 178, half_turn_axis, LUA_PI);
    return true;
  }
  TfpArmVector3 unit = arm_Vector3_normalize(K, 179, axis);
  *out = arm_Quaternion_axis_angle(K, 179, unit, atan2(sine, cosine));
  return true;
}
static TfpArmVector3 across(TfpArmPieces *a, TfpArmVector3 up, TfpArmVector3 line) { /* :181 */
  double along = arm_Vector3_dot(K, 181, up, line);
  TfpArmVector3 part = arm_mul(K, line, along);
  return arm_sub(K, up, part);
}
/* :184-203. false: nil. */
static bool torso_turn(TfpArmPieces *a, TfpArmQuaternion *out) {
  const TfpArmPiecesSide *left = &a->module->sides[0], *right = &a->module->sides[1];
  if (!(left->has_body_shoulder && right->has_body_shoulder && left->has_rig_shoulder && right->has_rig_shoulder &&
        a->identity.has_body_chest && a->identity.has_body_spine))
    return false;
  TfpArmUnit body = a->identity.body, rig = a->identity.rig;
  TfpArmVector3 p = arm_Unit_world_position(K, 189, body, right->body_shoulder + 1);
  TfpArmVector3 q = arm_Unit_world_position(K, 189, body, left->body_shoulder + 1);
  TfpArmVector3 body_line = arm_sub(K, p, q);
  p = arm_Unit_world_position(K, 190, rig, right->rig_shoulder + 1);
  q = arm_Unit_world_position(K, 190, rig, left->rig_shoulder + 1);
  TfpArmVector3 rig_line = arm_sub(K, p, q);
  p = arm_Unit_world_position(K, 191, body, a->identity.body_chest + 1);
  q = arm_Unit_world_position(K, 191, body, a->identity.body_spine + 1);
  TfpArmVector3 spine = arm_sub(K, p, q);
  if (arm_Vector3_length(K, 192, body_line) < .05 || arm_Vector3_length(K, 192, rig_line) < .05 || arm_Vector3_length(K, 192, spine) < .02)
    return false;
  body_line = arm_Vector3_normalize(K, 193, body_line);
  rig_line = arm_Vector3_normalize(K, 193, rig_line);
  TfpArmVector3 world_up = arm_Vector3(K, 194, 0, 0, 1);
  TfpArmQuaternion turn, roll;
  bool has_turn = arc(a, body_line, rig_line, world_up, &turn);
  TfpArmVector3 body_up = across(a, spine, body_line);
  world_up = arm_Vector3(K, 196, 0, 0, 1);
  TfpArmVector3 rig_up = across(a, world_up, rig_line);
  if (arm_Vector3_length(K, 197, body_up) < 1e-4 || arm_Vector3_length(K, 197, rig_up) < 1e-4) {
    if (has_turn) *out = turn;
    return has_turn;
  }
  body_up = arm_Vector3_normalize(K, 198, body_up);
  rig_up = arm_Vector3_normalize(K, 198, rig_up);
  if (has_turn) body_up = arm_Quaternion_rotate(K, 199, turn, body_up);
  bool has_roll = arc(a, body_up, rig_up, rig_line, &roll);
  if (has_roll && has_turn) { *out = arm_Quaternion_multiply(K, 201, roll, turn); return true; }
  if (has_roll) *out = roll;
  else if (has_turn) *out = turn;
  else *out = arm_Quaternion_identity(K, 202);
  return true;
}

typedef struct UpdateCall { TfpArmPieces *a; const TfpArmSnapshot *s; bool active; const TfpArmHandles *handles; } UpdateCall;
static void set_reason(TfpArmPieces *a, const char *text) { snprintf(a->reason, sizeof a->reason, "%s", text); }
static void update_body(void *argument) { /* :206-269 */
  UpdateCall *call = argument;
  TfpArmPieces *a = call->a;
  const TfpArmSnapshot *s = call->s;
  const TfpArmHandles *handles = call->handles;
  bind(a);
  if (!call->active || !s || !handles || !arm_handle(handles, s->has_body, s->body) || !arm_handle(handles, s->has_viewmodel, s->viewmodel)) {
    release(a); a->has_quick = false; set_reason(a, "waiting"); return;
  }
  double now = arm_clock(K);
  /* The pieces arrive within a quarter of a second of the view beginning: look every update for the first 0.6 s. */
  if (!a->has_quick || now < a->quick - .6) { a->has_quick = true; a->quick = now + .6; }
  bool changed = !a->has_identity || !(a->identity.has_body_ref && a->identity.body_ref == s->body) ||
                 !(a->identity.has_rig_ref && a->identity.rig_ref == s->viewmodel) || a->identity.handles != handles->identity ||
                 a->identity.body != arm_handle(handles, s->has_body, s->body) || a->identity.rig != arm_handle(handles, s->has_viewmodel, s->viewmodel);
  if (changed) { release(a); forget(a); }
  bool lost = false;
  for (int p = 0; p < a->piece_count; p++) lost = lost || !usable(a, a->pieces[p].handle, a->pieces[p].count);
  if (lost) { a->hung = false; forget(a); }
  /* No engine event announces the pieces arriving on the rig. */
  if (!a->has_identity || a->piece_count == 0 || (a->has_confirm_at && now >= a->confirm_at)) {
    if ((a->piece_count == 0 && now < a->quick) || !a->has_next_scan || now >= a->next_scan || now < a->next_scan - RESCAN) {
      int before = a->piece_count;
      bool confirming = a->has_identity && before > 0;
      scan(a, s, handles);
      a->has_next_scan = true; a->next_scan = now + RESCAN;
      if (confirming) a->has_confirm_at = false;
      else if (a->piece_count > 0) { a->has_confirm_at = true; a->confirm_at = now + .5; }
      if (a->piece_count != before) a->hung = false;
    }
  }
  if (a->piece_count == 0) { set_reason(a, "no pieces on the rig"); return; }
  if (!usable(a, a->identity.body, a->identity.body_count) || !usable(a, a->identity.rig, a->identity.rig_count)) {
    set_reason(a, "body or rig unavailable"); return;
  }
  bool stale = !a->hung;
  for (int p = 0; p < a->piece_count; p++) { /* :239-245 */
    TfpArmPiece *piece = &a->pieces[p];
    if (piece->has_placed) {
      const TfpArmPiecesSide *side = &a->module->sides[piece->side];
      arm_number(K, side->has_rig_shoulder, 241, "rig_shoulder");
      TfpArmMatrix4x4 shoulder = arm_Unit_world_pose(K, 241, a->identity.rig, side->rig_shoulder + 1);
      TfpArmMatrix4x4 expected = arm_Matrix4x4_multiply(K, 241, piece->placed, shoulder);
      TfpArmVector3 want = arm_Matrix4x4_translation(K, 241, expected);
      TfpArmMatrix4x4 chest = arm_Unit_world_pose(K, 242, piece->handle, piece->bones[0].index + 1);
      TfpArmVector3 have = arm_Matrix4x4_translation(K, 242, chest);
      TfpArmVector3 apart = arm_sub(K, have, want);
      if (arm_Vector3_length(K, 243, apart) > TOLERANCE) stale = true;
    }
  }
  if (stale) hang(a);
  TfpArmQuaternion turn;
  if (!torso_turn(a, &turn)) { set_reason(a, "torso frame unavailable"); return; }
  for (int i = 0; i < 2; i++) { /* :249-255 */
    TfpArmPiecesSide *side = &a->module->sides[i];
    if (side->has_body_shoulder && side->has_rig_shoulder) {
      side->body_at = arm_Unit_world_position(K, 251, a->identity.body, side->body_shoulder + 1);
      side->rig_at = arm_Unit_world_position(K, 252, a->identity.rig, side->rig_shoulder + 1);
      TfpArmMatrix4x4 shoulder = arm_Unit_world_pose(K, 253, a->identity.rig, side->rig_shoulder + 1);
      side->inverse = arm_Matrix4x4_inverse(K, 253, shoulder);
    }
  }
  for (int p = 0; p < a->piece_count; p++) { /* :256-267 */
    TfpArmPiece *piece = &a->pieces[p];
    for (int b = 0; b < piece->bone_count; b++) {
      const TfpArmPiecesBone *bone = &piece->bones[b];
      const TfpArmPiecesSide *side = &a->module->sides[piece->side];
      TfpArmVector3 from = arm_Unit_world_position(K, 259, a->identity.body, bone->source + 1);
      TfpArmVector3 offset = arm_sub(K, from, side->body_at);
      TfpArmVector3 turned = arm_Quaternion_rotate(K, 259, turn, offset);
      TfpArmVector3 at = arm_add(K, side->rig_at, turned);
      TfpArmQuaternion rotation = arm_Unit_world_rotation(K, 260, a->identity.body, bone->source + 1);
      TfpArmQuaternion facing = arm_Quaternion_multiply(K, 260, turn, rotation);
      TfpArmMatrix4x4 world = arm_Matrix4x4_from_quaternion_position(K, 261, facing, at);
      TfpArmMatrix4x4 pose = arm_Matrix4x4_multiply(K, 261, world, side->inverse);
      arm_Unit_set_local_pose(K, 262, piece->handle, bone->index + 1, pose);
      if (b == 0) { piece->placed = pose; piece->has_placed = true; }
    }
  }
  a->frames = a->frames + 1; set_reason(a, "active");
}
bool tfp_arm_pieces_update(TfpArmPieces *a, const TfpArmSnapshot *s, bool active, const TfpArmHandles *handles) { /* :205-272 */
  UpdateCall call = {a, s, active, handles};
  bool ok = arm_protect(K, update_body, &call);
  if (!ok) { set_reason(a, a->calls.error.text); a->errors = a->errors + 1; }
  return ok;
}
static void stop_body(void *argument) { /* :274 */
  TfpArmPieces *a = argument;
  if (a->hung) { bind(a); restore(a); }
  forget(a);
}
bool tfp_arm_pieces_stop(TfpArmPieces *a) { /* :273-277 */
  bool ok = arm_protect(K, stop_body, a);
  if (!ok) { forget(a); set_reason(a, a->calls.error.text); }
  else set_reason(a, "idle");
  return true;
}
bool tfp_arm_pieces_is_clean(const TfpArmPieces *a) { return a->hung == false; } /* :278 */
void tfp_arm_pieces_result(const TfpArmPieces *a, TfpArmPiecesResult *out) { /* :279-281 */
  out->pieces = a->piece_count; out->hung = a->hung; out->scans = a->scans; out->links = a->links; out->frames = a->frames;
  out->errors = a->errors; out->reason = a->reason;
}
