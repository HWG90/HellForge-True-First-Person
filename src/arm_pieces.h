/* Body-shaped sleeves on the first-person rig. Translation of retail
 * src/first_person/arm_pieces.lua at 0.5.41-smooth.66 (commit 1d0d453), whole
 * file. tests/vectors/arm_pieces_vectors.h holds the retail file's own
 * answers and engine calls. What differs from the Lua and why:
 * docs/ARMS-TRANSLATION.md.
 *
 * Every engine function goes through TfpArmEngine (arm_engine_iface.h), the
 * backend's readers and the clock through TfpArmBackend. Bone indices,
 * counts and times are `double`, as in Lua. */
#ifndef TFP_ARM_PIECES_H
#define TFP_ARM_PIECES_H
#include "arm_engine_calls.h"

#define TFP_ARM_PIECES_MAX 32          /* pieces found on the rig at once; retail has no limit */
#define TFP_ARM_PIECES_BONES 5         /* chest, collar, two spines, shoulder group */
#define TFP_ARM_PIECES_REASON (TFP_ARM_ERROR_TEXT + 80)

/* One entry of the module-level SIDES table: retail keeps what it found for a side in the module, not
 * in the instance, so every instance of the module shares it. */
typedef struct TfpArmPiecesSide {
  double collar, shoulder;                      /* name hashes */
  bool has_rig_shoulder, has_body_shoulder;     /* zero-based bone indices from the last lookup of any instance */
  double rig_shoulder, body_shoulder;
  TfpArmVector3 body_at, rig_at;                /* of the last placement */
  TfpArmMatrix4x4 inverse;
} TfpArmPiecesSide;
/* The Lua module table (what `mod.require` returns once). */
typedef struct TfpArmPiecesModule { TfpArmPiecesSide sides[2]; } TfpArmPiecesModule;

typedef struct TfpArmPiecesBone { double index, source; bool has_home; double home; } TfpArmPiecesBone;
typedef struct TfpArmPiece {
  double ref;
  TfpArmUnit handle;
  double count;
  int side;                                     /* index into the module's sides */
  int bone_count;
  TfpArmPiecesBone bones[TFP_ARM_PIECES_BONES];
  bool has_placed;                              /* the Matrix4x4Box of the chest's last pose */
  TfpArmMatrix4x4 placed;
} TfpArmPiece;
/* identity.homes[ref] = {[bone index] = home or false} */
typedef struct TfpArmPiecesHomes { double ref; int count; struct { double index; bool has_home; double home; } bones[TFP_ARM_PIECES_BONES]; } TfpArmPiecesHomes;

typedef struct TfpArmPieces {
  TfpArmPiecesModule *module;
  TfpArmCalls calls;
  /* self */
  double scans, links, frames, errors;
  char reason[TFP_ARM_PIECES_REASON];
  /* upvalues of M.new */
  int piece_count;
  TfpArmPiece pieces[TFP_ARM_PIECES_MAX];
  bool has_identity;
  struct {
    bool has_body_ref, has_rig_ref, has_body_chest, has_body_spine;
    double body_ref, rig_ref, body_chest, body_spine, body_count, rig_count;
    TfpArmUnit body, rig;
    uintptr_t handles;
    int homes_count;
    TfpArmPiecesHomes homes[TFP_ARM_PIECES_MAX];
  } identity;
  bool hung, has_next_scan, has_confirm_at, has_quick;
  double next_scan, confirm_at, quick;
} TfpArmPieces;

typedef struct TfpArmPiecesResult {
  double pieces, scans, links, frames, errors;
  bool hung;
  const char *reason;
} TfpArmPiecesResult;

void tfp_arm_pieces_module_init(TfpArmPiecesModule *module);
/* M.new(backend, sr, {clock=...}): the clock is backend->clock. */
void tfp_arm_pieces_init(TfpArmPieces *a, TfpArmPiecesModule *module, const TfpArmBackend *backend, const TfpArmEngine *engine);
/* self:update(s, active, handles). s or handles NULL: not a table. Returns the pcall's result. */
bool tfp_arm_pieces_update(TfpArmPieces *a, const TfpArmSnapshot *s, bool active, const TfpArmHandles *handles);
bool tfp_arm_pieces_stop(TfpArmPieces *a);
bool tfp_arm_pieces_is_clean(const TfpArmPieces *a);
void tfp_arm_pieces_result(const TfpArmPieces *a, TfpArmPiecesResult *out);
#endif
