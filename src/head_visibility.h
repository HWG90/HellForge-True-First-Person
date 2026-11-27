/* Head visibility: owns the camera and shadow pass bits of the local player's
 * head, torso, shoulder and backpack meshes, of the body's shadow, and of the
 * first-person rig, its arm pieces and the weapon tree; restores every bit it
 * changed. Translation of retail src/first_person/head_visibility.lua at
 * 0.5.41-smooth.66 (commit 1d0d453), `Head.new` and everything inside it.
 * tests/vectors/head_visibility_vectors.h holds the retail file's own answers.
 * What differs from the Lua and why: docs/translation/VISIBILITY-TRANSLATION.md.
 *
 * Engine calls go through engine_iface.h; memory, snapshots, the options and
 * the clock through TfpHeadIo. Addresses and unit references are `double`, as
 * in Lua. Tables the Lua walks with pairs() are walked in ascending key order. */
#ifndef TFP_HEAD_VISIBILITY_H
#define TFP_HEAD_VISIBILITY_H
#include "engine_iface.h"
#include "model_shadow_source.h"

/* What backend:backpack(s) returns, reduced to the fields read. */
typedef struct TfpHeadBackpack { double ref; TfpVisIdentity identity; } TfpHeadBackpack;
/* What backend:action_state(s) returns: the two fields read here; `native` is the caller's own
 * and is handed back to on_foot. */
typedef struct TfpHeadActionState { TfpVisIdentity owner; double body; const void *native; } TfpHeadActionState;

/* Everything but the engine. "May be NULL": the Lua field is not a function. */
typedef struct TfpHeadIo {
  void *user;
  /* backend:_read(at,size), backend:_ptr(at), backend:_alignment_executable(backend.profile.pose).
   * false: the call raised; `why` holds the text. */
  bool (*read)(void *user, double at, size_t size, uint8_t *out, TfpVisText *why);
  bool (*ptr)(void *user, double at, double *out, TfpVisText *why);
  bool (*alignment_executable)(void *user, double *exe, TfpVisText *why);
  /* backend:player_snapshot() / backend:pin_player_snapshot(). false: nil (with `why` when a text
   * came with it, or the call raised). pin_player_snapshot may be NULL; player_snapshot NULL is
   * "attempt to call a nil value". */
  bool (*player_snapshot)(void *user, TfpVisSnapshot *out, TfpVisText *why);
  bool (*pin_player_snapshot)(void *user, TfpVisSnapshot *out, TfpVisText *why);
  /* backend:backpack(s). May be NULL. 1: a backpack; 0: nil; -1: the call raised (`why`). */
  int (*backpack)(void *user, const TfpVisSnapshot *s, TfpHeadBackpack *out, TfpVisText *why);
  /* backend:action_state(s). May be NULL. false: not a table, or the call raised. */
  bool (*action_state)(void *user, const TfpVisSnapshot *s, TfpHeadActionState *out);
  /* backend:on_foot(s,state). May be NULL. 1: true; anything else: not true, or the call raised. */
  int (*on_foot)(void *user, const TfpVisSnapshot *s, const TfpHeadActionState *state);
  /* backend.foreign_camera(). May be NULL. 0: false; 1: any other answer; -1: the call raised. */
  int (*foreign_camera)(void *user);
  /* The address the Lua host gives for an engine userdata (compat51 `ffi.cast('uintptr_t', value)`):
   * a Unit's is its 32-bit reference times 4 (plus tag bits), a Mesh's is the mesh object, a World's
   * holds the pointer to the native world. */
  double (*userdata_address)(void *user, const void *handle);
  double (*clock)(void *user);                       /* seconds (retail: GetTickCount64()/1000) */
  void (*log)(void *user, const char *text);         /* may be NULL */
  bool (*controller_result_is_pose_offset)(void *user);   /* controller:result().mode == 'pose_offset' */
  bool (*controller_is_enabled)(void *user);
  /* deps.fallback_ready(s). May be NULL. 1: true; 0: anything else; -1: the call raised. */
  int (*fallback_ready)(void *user, const TfpVisSnapshot *s);
  bool (*pinned_ready)(void *user, const TfpVisSnapshot *s);   /* deps.pinned_ready(s) == true. May be NULL. */
  bool (*full_body)(void *user);                     /* deps.*() == true. Each may be NULL. */
  bool (*hide_torso)(void *user);
  bool (*show_torso)(void *user);
  bool (*seated_head)(void *user);
  /* Beyond retail, round 38 (docs/design/specs/2026-11-25-exosuit-seat.md). May be NULL: never a seat.
   * seat: this snapshot's body is in an exosuit's (or a vehicle's) seat with first person kept (the camera lock's
   * seat rule: seated, off foot, not in physics). The whole diver is then out of the camera's sight: the 30
   * appearance slots, the body's own meshes and the backpack, camera and shadow passes; the first-person rig is
   * left to the game. Asked once per update with the snapshot the update took.
   * cockpit: while in a seat, the unit reference of the exosuit to see through (its body meshes lose the camera
   * pass; their shadow stays), or false for none (the option is off, or no suit is proven to be the one the diver
   * sits in). The unit must carry the exosuit's resource and be in the body's world, or it is left alone.
   * Both are asked only for frames of camera kind 4 (the seat itself, and the glide out of it).
   * Round 43: in a seat the units hung on the body (its child list, unit +0x1D8 first child, +0x1E0 next sibling,
   * at most 8 deep, 32 to a list, 64 in all) are hidden too, camera and shadow: the sample pouch, the holstered
   * sidearm, the knife, slung weapons. Left: the first-person rig and what hangs on it, the exosuit, the units the
   * slots or the backpack already name (their own children are walked), units of another type or world. Once per
   * seat entry, and when the set changes in it, `log` gets one line per unit seen and why it was or was not taken. */
  bool (*seat)(void *user, const TfpVisSnapshot *s);
  bool (*cockpit)(void *user, const TfpVisSnapshot *s, double *unit);
  /* Beyond retail, round 44. May be NULL: never. The camera holds this snapshot's body's eye at the native view's (a
   * throw's or a swap's hold, or its release; src/camera_clear.h): Show torso does not draw the torso meanwhile. */
  bool (*eye_held)(void *user, const TfpVisSnapshot *s);
} TfpHeadIo;

/* What depends on the game build: the code pins of helldivers2.exe that qualify the SDK setter and the
 * registry (offset from the executable base, expected bytes), the registry pointer, the two functions a
 * renderer's method table must hold, and where the appearance rows start in the avatar owner. Structure
 * layouts (unit +0x08 reference, +0x18 world, +0xC8 mesh count, +0xD0 mesh array, +0x1D0 parent, +0x358
 * resource; mesh +0xDC pass mask; registry +0x88; method table +0xC8; appearance row 0x1B8, refs at +0xDC)
 * are the same numbers in both builds. */
typedef struct TfpHeadBuild {
  uint32_t binding; uint8_t binding_bytes[8];                 /* :113 Unit.set_mesh_visibility thunk */
  uint32_t mask_layout; uint8_t mask_layout_bytes[14];        /* :114 pass-mask setter prologue */
  uint32_t context_a; uint8_t context_a_bytes[24];            /* :115 pass-name comparison */
  uint32_t context_b; uint8_t context_b_bytes[26];            /* :116 pass-mask update */
  uint32_t context_default, context_default_value;            /* :117 the id the 'default' pass is compared with */
  uint32_t context_shadow, context_shadow_value;              /* :117 ... and 'shadow_caster' */
  uint32_t registry_code; uint8_t registry_code_bytes[22];    /* :119 the function that reads the unit registry */
  uint32_t unit_registry;                                     /* :123 pointer to the unit registry */
  uint32_t renderer_method_6, renderer_method_0;              /* :142 method table +0xC8+48 and +0xC8 */
  uint32_t appearance;                                        /* :393 appearance rows in the avatar owner */
} TfpHeadBuild;
/* Retail 26568081: the numbers of the retail file; the vectors are replayed with these. */
extern const TfpHeadBuild tfp_head_build_retail;
/* HellForge's build 25480438: see docs/translation/VISIBILITY-TRANSLATION.md for where each number comes from. */
extern const TfpHeadBuild tfp_head_build_25480438;

typedef enum TfpHeadPhase { TFP_HEAD_IDLE, TFP_HEAD_ACTIVE, TFP_HEAD_UNAVAILABLE, TFP_HEAD_CLEANUP_PENDING } TfpHeadPhase;
const char *tfp_head_phase_name(TfpHeadPhase phase);

/* head:result(). */
typedef struct TfpHeadResult {
  TfpHeadPhase phase;
  double hidden, hidden_head, hidden_torso, hidden_shoulders, hidden_body, hidden_backpack;
  double preserved_partial, writes, restores, retired, foreign_masks;
  bool cleanup_pending;
  TfpVisText error_message;
  bool has_model_shadow_error; const char *model_shadow_error; size_t model_shadow_error_len;   /* valid until the next update */
  double shadows_disabled, steady_observations, full_observations;
  /* Beyond retail, round 38: the last update was a seat (TfpHeadIo.seat); rows owned of the limb slots (1, 3 to 7)
   * and of the exosuit's meshes; the suit being seen through (0: none). */
  bool seat;
  double hidden_limbs, hidden_cockpit, cockpit_unit;
  /* Beyond retail, round 43: rows owned of units hung on the body (gear: the sample pouch, the holstered sidearm, ...)
   * in a seat; see the seat branch of resolve() in head_visibility.c. */
  double hidden_gear;
} TfpHeadResult;

typedef struct TfpHead TfpHead;
/* Head.new(backend, log, stingray, deps). `engine` NULL: no stingray table. `models` NULL: no
 * deps.model_shadows (when given, it must have been set up with build->appearance). io and engine are
 * read on every call, so the pointers they hold may change. */
TfpHead *tfp_head_new(const TfpHeadIo *io, const TfpEngineIface *engine, TfpModelShadow *models, const TfpHeadBuild *build);
void tfp_head_free(TfpHead *h);
void tfp_head_set_parts(unsigned parts);   /* beyond retail: bit 0 head, 1 torso, 2 shoulders; 7 = retail */
/* Each returns Lua's first result and puts the second in `why` (absent: nil; may be NULL). */
bool tfp_head_update(TfpHead *h, TfpVisText *why);     /* head:update(controller) */
bool tfp_head_stop(TfpHead *h, TfpVisText *why);
bool tfp_head_shutdown(TfpHead *h, TfpVisText *why);
bool tfp_head_is_clean(const TfpHead *h);
void tfp_head_result(TfpHead *h, TfpHeadResult *out);
/* head:status(); returns the length the whole text needs. */
size_t tfp_head_status(TfpHead *h, char *out, size_t size);
/* head:render_appearance(): false when nothing is observed (the Lua returns nothing). The 120
 * appearance bytes stay valid until the next update or stop. */
bool tfp_head_render_appearance(const TfpHead *h, const uint8_t **appearance, TfpVisIdentity *owner, double *world);
/* head:sdk_handles(): the table {[unit reference] = SDK Unit}, in ascending order of reference.
 * Returns false while the table is nil. `unit` NULL is Lua false (a reference two units claim).
 * `*table` changes whenever the Lua replaces the table (consumers use its identity as a trigger). */
typedef struct TfpHeadHandle { double ref; TfpEngineUnit unit; } TfpHeadHandle;
bool tfp_head_sdk_handles(const TfpHead *h, size_t *count, unsigned long long *table);
TfpHeadHandle tfp_head_sdk_handle(const TfpHead *h, size_t index);

/* Beyond retail, for the log only (docs/design/specs/2026-11-24-helmet-in-view.md): what the last update, stop or
 * shutdown saw of the camera ("default") pass of the meshes it wants out of the camera's sight (head, torso,
 * shoulders, backpack, the rig's shoulder pads; not the rows that only own a shadow pass). Counted where the call
 * already reads and writes; nothing is read or written for it.
 *   camera_rows   such meshes in this update's full observation (0 on the steady path, which reads no row)
 *   on_new        found drawn, never seen before by this module (the first update on a body)
 *   on_again      found drawn, and hidden when this module last looked: the game or another mod switched the pass
 *                 back on between two updates. on_again_head: those of the head.
 *   on_still      found drawn, and drawn when it last looked (it was left drawn then too)
 *   turned_off    camera passes this call switched off
 *   left_on       still drawn when the update ended (an outside change this module leaves alone until the view,
 *                 the weapon or the aim changes: retail's rule)
 *   put_back      rows the call gave back because it ended in a stop (the view is not one it hides in, or a check failed)
 *   full          the update took the full observation path */
typedef struct TfpHeadWatch { double camera_rows, on_new, on_again, on_again_head, on_still, turned_off, left_on, put_back; bool full; } TfpHeadWatch;
void tfp_head_watch(const TfpHead *h, TfpHeadWatch *out);

/* For tests and diagnostics: the state the Lua keeps in upvalues. */
typedef struct TfpHeadOwnedRow {
  const char *key, *part;
  double bits, before, written, last, current, reset_epoch, reset_bits;
  bool has_last, has_reset_epoch, restoring_default, shadow_suppressed, shadow_only;
} TfpHeadOwnedRow;
typedef struct TfpHeadDebug {
  size_t owned, ignored;
  bool steady, has_epoch, has_cache_request, has_cache_time, has_torso_steady_since;
  double epoch_serial, epoch_started, update_serial, cache_time, torso_steady_since;
  TfpVisIdentity identity;
  const char *cache_request;
} TfpHeadDebug;
void tfp_head_debug(const TfpHead *h, TfpHeadDebug *out);
void tfp_head_debug_owned(const TfpHead *h, size_t index, TfpHeadOwnedRow *out);
double tfp_head_debug_ignored(const TfpHead *h, size_t index, const char **key);
#endif
