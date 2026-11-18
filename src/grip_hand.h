/* The hands of the first-person arms on the weapon. Translation of retail
 * src/first_person/grip_hand.lua at 0.5.41-smooth.66 (commit 1d0d453), whole
 * file. tests/vectors/grip_hand_vectors.h holds the retail file's own answers
 * and engine calls. What differs from the Lua and why:
 * docs/translation/ARMS-TRANSLATION.md.
 *
 * Every engine function goes through TfpArmEngine (arm_engine_iface.h), the
 * backend's readers and the clock through TfpArmBackend. Bone indices,
 * counts and times are `double`, as in Lua. */
#ifndef TFP_GRIP_HAND_H
#define TFP_GRIP_HAND_H
#include "arm_engine_calls.h"

/* Retail's tables have no limits; these are far above anything recorded (4 pieces, 44 bones, 2 grip bones). */
#define TFP_GRIP_CANDIDATES 32         /* weapon units carrying a grip bone */
#define TFP_GRIP_PIECES 32             /* arm pieces hung on the rig */
#define TFP_GRIP_BONES 1024            /* their offset bones, all pieces together */
#define TFP_GRIP_KNOWN 32              /* weapon units whose grip is remembered */
#define TFP_GRIP_MESHES 256            /* rig meshes hidden at once */
#define TFP_GRIP_REASON (TFP_ARM_ERROR_TEXT + 80)

enum { TFP_GRIP_SHOULDER = 1, TFP_GRIP_ELBOW, TFP_GRIP_HAND };   /* bone.part */
enum { TFP_GRIP_LEFT, TFP_GRIP_RIGHT };                           /* bone.side */
/* node: SDK index on the piece. rig: SDK index of the rig bone it is linked to. finger: SDK index of the body
 * bone of the same name, 0 for none. */
typedef struct TfpGripBone { uint32_t rig; uint16_t node, finger; uint8_t part, side; } TfpGripBone;
typedef struct TfpGripPiece { TfpArmUnit handle; double count; int first, bones; } TfpGripPiece;
typedef struct TfpGripCandidate { TfpArmUnit handle; double node, count; bool has_at; double at[3]; double d; } TfpGripCandidate;
/* A `shapes` table: by body finger bone, its pose relative to the body's hand (a Matrix4x4Box each). */
typedef struct TfpGripShapes { bool has[256]; TfpArmMatrix4x4 pose[256]; } TfpGripShapes;
typedef struct TfpGripKnown { TfpArmUnit handle; double node, count; int shapes; } TfpGripKnown;
typedef struct TfpGripElements { double v[10]; } TfpGripElements;   /* position, quaternion; rig_at carries a second position */

typedef struct TfpGripHand {
  TfpArmCalls calls;
  /* self */
  double frames, scans, errors, partial, recalled, swaps, spun;
  bool has_distance, has_off, has_right_distance, has_right;
  double distance, off, right_distance;
  char reason[TFP_GRIP_REASON], right[TFP_GRIP_REASON];
  /* upvalues of M.new, in its order */
  int candidate_count, piece_count, bone_count;
  TfpGripCandidate candidates[TFP_GRIP_CANDIDATES];
  TfpGripPiece pieces[TFP_GRIP_PIECES];
  TfpGripBone bones[TFP_GRIP_BONES];
  bool has_seen_handles, has_next_scan, was_active, written;
  uintptr_t seen_handles;
  double next_scan;
  bool complete, has_confirm, right_partial, whole[2];
  double confirm;
  int aim;                               /* -1 nil, 0 false, 1 true */
  bool has_periodic;
  double periodic;
  TfpArmUnit body;                       /* NULL: nil */
  double body_hand, body_count;
  bool has_body_right, has_body_attach, has_rig_attach;
  double body_right, body_attach, rig_attach;
  bool has_gun, gun_turned, has_rest, has_rel_at, has_rel_time, has_spin_until;
  struct { TfpArmUnit handle; double count; } gun;
  TfpGripElements rest, rel_at;
  double rest_keep, rel_time, spin_until;
  double aside;
  bool has_aside_time, has_aside_rest;   /* aside_ref is there whenever aside_rest is */
  double aside_time;
  TfpGripElements aside_rest, aside_ref;
  bool has_rig_at, has_rig_time, has_rig_until;
  TfpGripElements rig_at;
  double rig_time, rig_until;
  bool has_weapon_id;
  char weapon_id[48];
  int soon_count;
  double soon[3];
  bool has_began;
  double began;
  int known_count;
  TfpGripKnown known[TFP_GRIP_KNOWN];
  bool has_stirred;
  double stirred;
  bool has_body_ref, has_ref_at, has_rest_ref;
  double body_ref;
  TfpGripElements ref_at, rest_ref;
  bool drawn[2];
  bool has_hidden;
  struct { TfpArmUnit rig; int count; double meshes[TFP_GRIP_MESHES]; } hidden;
  bool has_current, on;
  struct { TfpArmUnit handle; double node; } current;
  double pin;
  bool has_last;
  double last;
  int shapes;                            /* which of `sets` the `shapes` upvalue is */
  TfpGripShapes sets[TFP_GRIP_KNOWN + 1];
} TfpGripHand;

typedef struct TfpGripHandResult {
  double frames, scans, weapons, pieces, bones, fingers, errors, steady, handled, turning, sliding, swaps, recalled, partial, spun;
  bool has_distance, has_off, has_right_distance, has_arm_root;
  double distance, off, right_distance, arm_root;
  bool rig_screen_hidden, gun, gun_turned;
  const char *reason, *right;            /* right NULL: nil (no update yet) */
} TfpGripHandResult;

/* M.new(backend, sr, {clock=...}): the clock is backend->clock. */
void tfp_grip_hand_init(TfpGripHand *g, const TfpArmBackend *backend, const TfpArmEngine *engine);
/* self:update(s, active, handles). s or handles NULL: not a table. Returns the pcall's result. */
bool tfp_grip_hand_update(TfpGripHand *g, const TfpArmSnapshot *s, bool active, const TfpArmHandles *handles);
bool tfp_grip_hand_stop(TfpGripHand *g);
bool tfp_grip_hand_is_clean(const TfpGripHand *g);
void tfp_grip_hand_result(const TfpGripHand *g, TfpGripHandResult *out);

/* M.solve(V, S, E, H, T): the two-bone reach. NIL: no bend side. RAISED: an engine function raised (error). */
typedef enum TfpGripSolve { TFP_GRIP_SOLVE_NIL, TFP_GRIP_SOLVED, TFP_GRIP_SOLVE_RAISED } TfpGripSolve;
TfpGripSolve tfp_grip_hand_solve(const TfpArmEngine *engine, TfpArmVector3 S, TfpArmVector3 E, TfpArmVector3 H, TfpArmVector3 T,
                                 TfpArmVector3 *elbow, TfpArmVector3 *reached, TfpArmError *error);
/* M.weight(distance) */
double tfp_grip_hand_weight(double distance);
#endif
