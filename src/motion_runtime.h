/* Per-update presentation motion: samples locomotion, eases the per-action
 * saved placement, runs the bob / sway model, converts to the rig parent's
 * space, fades over an ADS handoff, softens a pop and submits the result to
 * the owned native pose. Translation of retail
 * src/first_person/motion_runtime.lua at 0.5.41-smooth.66 (commit 1d0d453);
 * tests/vectors/motion_runtime_vectors.h holds that file's own answers. Line
 * numbers in motion_runtime.c are the Lua file's.
 * docs/translation/MOTION-RUNTIME-TRANSLATION.md lists what C cannot express and why.
 *
 * The three pure modules the Lua file loads are called directly: the motion
 * model (motion_math), the action selection (action_select) and locomotion's
 * to_parent. Everything the Lua object does not own is reached through
 * TfpMotionRuntimeHost: controller, backend, placement editor, locomotion
 * sampler, options, clock and log. All arithmetic is in double; build with
 * -ffp-contract=off.
 *
 * How Lua values are spelled here:
 *  - a lease is a pointer; two leases are the same lease when the pointers are
 *    equal (retail compares the tables). Its fields are read through the
 *    pointer at the moment retail reads them. The runtime keeps the pointer of
 *    the lease it last ran with and reads its origin once more on the first
 *    update / reset after the controller left it: a lease must stay readable
 *    until that call has returned, and its address must not be given to a
 *    different lease before then;
 *  - an identity string is a key: equal keys, equal strings (as transition.h);
 *    has_* false stands for "not a string";
 *  - a table that may be nil, and a call retail does not protect with pcall,
 *    answer with TfpMotionRuntimeGot; a protected call answers bool (false:
 *    it raised, or its answer has the wrong type);
 *  - a text that may be nil is a pointer that may be NULL;
 *  - placements, previews and options are values: the runtime keeps copies,
 *    where retail keeps the table it was handed. */
#ifndef TFP_MOTION_RUNTIME_H
#define TFP_MOTION_RUNTIME_H
#include "action_select.h"
#include "locomotion.h"
#include "motion_math.h"
#include "transition.h"

#define TFP_MOTION_RUNTIME_TEXT 256

typedef enum TfpMotionRuntimeGot { TFP_MOTION_GOT_NIL, TFP_MOTION_GOT_VALUE, TFP_MOTION_GOT_RAISED } TfpMotionRuntimeGot;
typedef enum TfpMotionRuntimeAnswer { TFP_MOTION_RUNTIME_FALSE, TFP_MOTION_RUNTIME_TRUE, TFP_MOTION_RUNTIME_RAISED } TfpMotionRuntimeAnswer;
typedef enum TfpMotionTri { TFP_MOTION_TRI_NIL, TFP_MOTION_TRI_FALSE, TFP_MOTION_TRI_TRUE } TfpMotionTri;

/* controller.lease / backend.alignment_lease, the fields this module reads. */
typedef struct TfpMotionRuntimeLease {
  bool done, cleanup_only, pose_blend_entry;  /* Lua truth of the fields */
  bool has_origin;                            /* lease.origin is a table */
  bool has_identity, has_player_identity;     /* origin.identity / origin.player_identity are strings */
  uint64_t identity, player_identity;
} TfpMotionRuntimeLease;

/* The seven numbers of a submitted modifier, in retail's `axes` order. */
enum { TFP_MOTION_DX, TFP_MOTION_DY, TFP_MOTION_DZ, TFP_MOTION_QX, TFP_MOTION_QY, TFP_MOTION_QZ, TFP_MOTION_QW, TFP_MOTION_AXES };
typedef struct TfpMotionRuntimeModifier {
  double axis[TFP_MOTION_AXES];
  bool authored_ads;                          /* value.authored_ads == true */
} TfpMotionRuntimeModifier;

typedef struct TfpMotionRuntimeControllerResult { bool pose_offset, running; } TfpMotionRuntimeControllerResult; /* mode=='pose_offset', phase=='running' */

typedef struct TfpMotionRuntimeHost {
  void *user;
  /* Fields retail reads from its collaborators; read live, where retail reads them. */
  const TfpMotionRuntimeLease *controller_lease;  /* controller.lease; NULL: nil */
  const TfpMotionRuntimeLease *alignment_lease;   /* backend.alignment_lease */
  const char *placement_weapon_key;               /* backend.placement_weapon_key, handed back to action_placement */
  double action_placement_revision;               /* backend.action_placement_revision or 0 */

  /* clock(): false when it raised or did not answer a number. Required. */
  bool (*clock)(void *user, double *seconds);
  /* log(text). NULL: none given. */
  void (*log)(void *user, const char *text);
  /* options(): false when it raised or did not answer a table; true with every field of *out written.
   * NULL: no options were given (retail then uses {enabled=true, scale=1}). A fixed options table is a
   * function that always answers it. */
  bool (*options)(void *user, TfpMotionOptions *out);

  /* controller:result(). NIL: the answer is not a table. */
  TfpMotionRuntimeGot (*controller_result)(void *user, TfpMotionRuntimeControllerResult *out);
  /* controller:is_enabled(). VALUE: a true value; NIL: false or nil. */
  TfpMotionRuntimeGot (*controller_is_enabled)(void *user);
  /* controller:get_placement(). In *out a field that is nil is left out of `present`. */
  TfpMotionRuntimeGot (*controller_get_placement)(void *user, TfpActionPlacement *out);
  /* controller:set_placement(value) == true. value NULL: nil. *why may stay NULL. Protected. */
  bool (*controller_set_placement)(void *user, const TfpActionPlacement *value, const char **why);

  /* backend:set_pose_motion(lease, value) == true. Protected. */
  bool (*set_pose_motion)(void *user, const TfpMotionRuntimeLease *lease, const TfpMotionRuntimeModifier *value, const char **why);
  /* backend:set_ads_placement(lease, ads) == true. ads NULL: nil. Protected. */
  bool (*set_ads_placement)(void *user, const TfpMotionRuntimeLease *lease, const TfpActionPlacement *ads, const char **why);
  /* backend:player_snapshot(): true when it answered a table whose controller_active and shoulder_aim are booleans. Protected. */
  bool (*player_snapshot)(void *user, bool *controller_active, bool *shoulder_aim);
  /* backend.action_placement(key, action). NULL: the backend has none. */
  TfpMotionRuntimeGot (*action_placement)(void *user, const char *key, TfpAct action, TfpActionPlacement *out);
  /* backend.action_mapping(). NULL: the backend has none. NIL: it answered nil. */
  TfpMotionRuntimeGot (*action_mapping)(void *user, const TfpActionMapping **out);
  /* backend.presentation_transition(): false when it raised or answered no table. A progress that is not
   * a number is NaN. NULL: not a function. Protected. */
  bool (*presentation_transition)(void *user, TfpTransitionPacket *out);
  /* backend.presentation_action(action, now) and backend.presentation_resume(identity, player_identity, now):
   * protected, answers unused. NULL: not a function. */
  void (*presentation_action)(void *user, TfpAct action, double now);
  void (*presentation_resume)(void *user, uint64_t identity, uint64_t player_identity, double now);

  /* editor:is_active(). true and *active (the answer == true); false: the answer was nil or the call
   * raised, with the text in *problem or NULL. NULL: no editor was given. Protected. */
  bool (*editor_is_active)(void *user, bool *active, const char **problem);
  /* editor:preview_rotation(): yaw, pitch, roll of *out. NULL: the editor has none. NIL: nil or false. */
  TfpMotionRuntimeGot (*editor_preview_rotation)(void *user, TfpActionPlacement *out);

  /* sampler:sample(lease), in the mod tfp_locomotion_sample. false with the text in *why or NULL. Protected. */
  bool (*sample)(void *user, const TfpMotionRuntimeLease *lease, TfpLocomotionSample *out, const char **why);
  /* dependencies.to_parent. NULL (the mod): tfp_locomotion_to_parent. An answer field that is not a
   * number is NaN. false: it raised, with its text. Protected. */
  bool (*to_parent)(void *user, const TfpLocomotionValue *value, const double camera[16], const double parent[16],
                    TfpLocomotionParent *out, const char **error);
} TfpMotionRuntimeHost;

typedef enum TfpMotionRuntimePhase {
  TFP_MOTION_PHASE_NONE /* nil */, TFP_MOTION_PHASE_IDLE, TFP_MOTION_PHASE_ACTIVE, TFP_MOTION_PHASE_NEUTRAL, TFP_MOTION_PHASE_UNAVAILABLE,
  TFP_MOTION_PHASE_DISABLED, TFP_MOTION_PHASE_EDITOR_FROZEN, TFP_MOTION_PHASE_CAPTURE
} TfpMotionRuntimePhase;
const char *tfp_motion_runtime_phase_name(TfpMotionRuntimePhase phase); /* 'idle' ...; "" for nil */

/* self:result(): retail's `state` table. A has_* false, a TRI_NIL and TFP_ACT_NONE are fields that are nil. */
typedef struct TfpMotionRuntimeState {
  TfpMotionRuntimePhase phase;
  uint64_t applied, coalesced, softened, read_failures, write_refusals, time_held;
  double dx, dy, dz, qx, qy, qz, qw;
  bool has_reason;
  char reason[TFP_MOTION_RUNTIME_TEXT];
  bool has_sprint_weight, has_bob_weight, has_shoulder_weight, has_ads_handoff, has_speed, has_gait_phase;
  double sprint_weight, bob_weight, shoulder_weight, ads_handoff, speed, gait_phase;
  TfpMotionTri sprint_target, sprint_held, capture_ready, capture_native_ads, ads, shoulder_aim, sprint_raw;
  TfpAct action;
  bool has_l1, has_l3, has_l14;
  int l1, l3, l14;
  bool has_sprint_evidence;           /* the text is tfp_locomotion_sprint_evidence */
  bool has_reset, has_rebased;        /* with has_*: TFP_MOTION_RESET_NONE / TFP_MOTION_REBASE_NONE is retail's false */
  TfpMotionReset reset;
  TfpMotionRebase rebased;
} TfpMotionRuntimeState;

typedef struct TfpMotionRuntimePlacement { bool has; TfpActionPlacement value; } TfpMotionRuntimePlacement;

/* The upvalues of Runtime.new, under retail's names. */
typedef struct TfpMotionRuntime {
  const TfpMotionRuntimeHost *host;
  TfpMotionMath model;
  bool has_owner; uint64_t owner_key; TfpSnapshot owner;      /* sample.owner as the model's key: a new key whenever the player identity differs from the last sample the model saw */
  const TfpMotionRuntimeLease *profile_lease; double profile_revision; TfpAct profile_action;
  TfpMotionRuntimePlacement profile_target, profile_value;
  bool has_profile_epoch; uint64_t profile_epoch; double profile_used;
  struct { bool has; uint64_t identity, player_identity; double at; double value[TFP_MOTION_AXES]; TfpMotionTri ads; } continuity;
  const TfpMotionRuntimeLease *last_lease;
  bool has_last_time; double last_time;
  bool has_last_options; TfpMotionOptions last_options;
  bool has_last_message; char last_message[TFP_MOTION_RUNTIME_TEXT];
  const TfpMotionRuntimeLease *sent_lease; double sent[TFP_MOTION_AXES];
  TfpMotionRuntimePhase sent_phase; TfpMotionTri sent_ads, sent_authored;
  struct { bool has; double from[TFP_MOTION_AXES], at, progress; } handoff;
  TfpMotionTri previous_ads;
  bool has_handoff_epoch; uint64_t handoff_epoch;
  const TfpMotionRuntimeLease *shown_lease; double shown[TFP_MOTION_AXES];
  struct { bool has; double from[TFP_MOTION_AXES], progress; } guard;
  bool has_frame_time; double frame_time, frame_step;
  TfpMotionRuntimeState state;
  char answer[TFP_MOTION_RUNTIME_TEXT];                        /* the text of the last answer */
} TfpMotionRuntime;

/* Runtime.new(controller, backend, editor, log, clock, options, {motion = Motion.new(model_options)}).
 * model_options NULL: Motion.new(). false with retail's text: host or its clock missing
 * ('Motion runtime dependencies required'), or the model refused its options. */
bool tfp_motion_runtime_init(TfpMotionRuntime *rt, const TfpMotionRuntimeHost *host, const TfpMotionOptions *model_options,
                             const char **error);
/* self:update(). TRUE, with *reason the second answer or NULL; RAISED when a call retail does not
 * protect raised or answered nil where retail then indexes it (retail raises a Lua error; the state is
 * left as retail leaves it). *reason is valid until the next call on the runtime. */
TfpMotionRuntimeAnswer tfp_motion_runtime_update(TfpMotionRuntime *rt, const char **reason);
/* self:capture_neutral(native_ads). */
TfpMotionRuntimeAnswer tfp_motion_runtime_capture_neutral(TfpMotionRuntime *rt, bool native_ads, const char **reason);
bool tfp_motion_runtime_reset(TfpMotionRuntime *rt);            /* self:reset() */
void tfp_motion_runtime_result(const TfpMotionRuntime *rt, TfpMotionRuntimeState *out); /* self:result() */
#endif
