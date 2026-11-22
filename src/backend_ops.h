/* The game-facing backend of True First Person: every method of retail
 * src/first_person/backend.lua at 0.5.41-smooth.66 (commit 1d0d453), on its
 * shipped path (`Backend.new(log)`, no fixture `io`), for build 25480438, and
 * the animation-state sampler of src/first_person/action_state.lua that
 * `Backend:action_state` runs. tests/vectors/backend_ops_vectors.h holds the
 * retail files' own answers; docs/translation/BACKEND-OPS-TRANSLATION.md has the notes.
 *
 * Nothing here touches the process directly: memory, module bases, page
 * queries, engine calls and the log go through TfpOpsHost, so the same object
 * runs in the tests and in the DLL.
 *
 * How Lua values are spelled in C:
 *  - a method's first result is a TfpOpsResult: true, false, nil, or RAISED
 *    when a Lua error left the method (the text is then the error's);
 *  - its second result is a TfpOpsText: present (a string) or not (nil);
 *  - a snapshot / lease / token argument that is nil is a NULL pointer;
 *  - a lease and a token are tables in Lua and compared by identity: here the
 *    caller owns their storage and the address is the identity. A lease must
 *    stay where it is for as long as the backend can name it (until another
 *    lease replaced it as `alignment_lease` / `warm_lease`), and its storage
 *    must not be reused for another lease while this backend lives;
 *  - retail's identity strings are compared field by field
 *    (tfp_ops_same_identity, tfp_ops_same_player). Where a string has to be
 *    carried by value (a presentation phase packet) it is a 64-bit key. */
#ifndef TFP_BACKEND_OPS_H
#define TFP_BACKEND_OPS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum TfpOpsResult { TFP_OPS_RAISED = -2, TFP_OPS_NIL = -1, TFP_OPS_FALSE = 0, TFP_OPS_TRUE = 1 } TfpOpsResult;
typedef struct TfpOpsText { bool present; char text[320]; } TfpOpsText;

/* ---- what a snapshot holds (backend.lua:157-290) ---- */
typedef struct TfpOpsAction {            /* action_state.lua:110-114 */
  uint64_t body_object, graph, instance;
  uint32_t body, layer_count;
  uint8_t resource[8];                   /* memory order; retail prints it reversed, upper case */
  bool resource_match, sprint_hypothesis;
  uint64_t sampled;                      /* bit L: layer L was sampled */
  uint64_t unknown;                      /* bit L: its active state is not in its definition */
  int16_t states[33];                    /* by layer, 1-based; -1: no active state */
  /* owner: the player identity it was read for */
  uint64_t owner_base, owner_context, owner_camera, owner_world, owner_controller, owner_player, owner_avatar_owner, owner_entry;
  uint32_t owner_light, owner_avatar_index, owner_avatar, owner_body;
  uint8_t owner_descriptor[20];
} TfpOpsAction;
typedef struct TfpOpsSnapshot {
  uint64_t base, context, camera, record, world, controller, player, avatar_owner, avatar_entry, flags_address;
  uint64_t weapon_component_descriptor;
  uint32_t head, tail, camera_kind, request_avatar, request_weapon, local_entity, avatar, avatar_index, body, viewmodel;
  uint32_t weapon, weapon_unit, camera_unit;
  uint8_t controller_bytes[6], flags[24], avatar_descriptor[20], weapon_descriptor[20];
  bool controller_active, blocked, request_local, aiming, shoulder_aim, seated, reloading, safe;
  bool weapon_transition, head_camera_transition;
  bool full;                             /* a full snapshot: weapon fields, identity is the weapon-level one */
  bool pin_camera, foreign_camera, weapon_ready, engine_safe, native_pose_eligible;
  bool has_reason, has_action, has_action_error;
  char reason[160], action_error[160];
  TfpOpsAction action;
} TfpOpsSnapshot;
bool tfp_ops_same_player(const TfpOpsSnapshot *a, const TfpOpsSnapshot *b);    /* player_identity */
bool tfp_ops_same_identity(const TfpOpsSnapshot *a, const TfpOpsSnapshot *b);  /* identity */
uint64_t tfp_ops_player_key(const TfpOpsSnapshot *s);    /* never 0 */
uint64_t tfp_ops_identity_key(const TfpOpsSnapshot *s);  /* never 0; a player snapshot's is its player key */

/* ---- placements, motion, phase packets ---- */
enum { TFP_OPS_X = 1, TFP_OPS_Y = 2, TFP_OPS_Z = 4, TFP_OPS_YAW = 8, TFP_OPS_PITCH = 16, TFP_OPS_ROLL = 32 };
typedef struct TfpOpsPlacement { double x, y, z, yaw, pitch, roll; unsigned present; } TfpOpsPlacement;
typedef struct TfpOpsMotion { double dx, dy, dz, q[4]; unsigned q_present; bool authored_ads; } TfpOpsMotion;
typedef struct TfpOpsPhase {             /* transition.lua's packet, the fields backend.lua:935 looks at */
  bool has_epoch, has_progress, has_started, has_duration;
  double epoch, progress, started, duration;
  int ads, continuous;                   /* 1 true, 0 false, -1 not a Boolean */
  uint64_t identity, player_identity;    /* keys; 0: empty or not a string */
  int reason, action;                    /* 0 / -1: nil */
} TfpOpsPhase;

/* ---- the pose (editor) helper and the alignment helper, as backend.lua calls them ---- */
struct TfpOpsPoseHelper;
typedef struct TfpOpsHandle { const struct TfpOpsPoseHelper *owner; bool disarmed; } TfpOpsHandle;
typedef struct TfpOpsHelperConfig {      /* _alignment_config / _pose_config */
  uint64_t game, exe, context, request, avatar_owner, weapon_descriptor;
  uint32_t avatar_index, avatar, weapon, body, viewmodel, weapon_unit;
  double duration_ms, blend_ms, offset_x, offset_y, offset_z;
  bool pose, steady_entry;               /* pose false: the four pose fields are absent */
} TfpOpsHelperConfig;
typedef struct TfpOpsHelperStats {
  int armed, expired;                    /* as the helper reports them; normalised: 1 or 0 as retail's true / false */
  bool normalised, has_error;
  double error, target_weight, blend;
} TfpOpsHelperStats;
/* Each function answers Lua's first result; `why` takes the second when it is a
 * text (or the raised error). A NULL owned_handle / transition is retail's
 * "not a function". */
typedef struct TfpOpsPoseHelper {
  void *user;
  TfpOpsResult (*prepare)(void *user, const TfpOpsHelperConfig *config, void **native, TfpOpsText *why);
  TfpOpsResult (*begin)(void *user, const TfpOpsHelperConfig *config, void *native, TfpOpsHandle **handle, TfpOpsText *why);
  TfpOpsResult (*owned_handle)(void *user, TfpOpsHandle **handle, TfpOpsText *why);
  TfpOpsResult (*finish)(void *user, TfpOpsHandle *handle, bool *has_stats, TfpOpsHelperStats *stats, TfpOpsText *why);
  TfpOpsResult (*stats)(void *user, TfpOpsHandle *handle, TfpOpsHelperStats *stats, TfpOpsText *why);
  TfpOpsResult (*set_placement)(void *user, TfpOpsHandle *handle, double x, double y, double z, TfpOpsText *why);
  TfpOpsResult (*set_zoom_policy)(void *user, TfpOpsHandle *handle, bool active, TfpOpsText *why);
  TfpOpsResult (*set_ads_placement)(void *user, TfpOpsHandle *handle, double x, double y, double z, TfpOpsText *why);
  TfpOpsResult (*transition)(void *user, TfpOpsHandle *handle, const TfpOpsPhase *token, TfpOpsText *why);
  TfpOpsResult (*set_motion)(void *user, TfpOpsHandle *handle, double dx, double dy, double dz, double qx, double qy, double qz,
                             double qw, bool authored_ads, TfpOpsText *why);
} TfpOpsPoseHelper;

/* ---- the host ---- */
typedef enum TfpOpsModule { TFP_OPS_GAME, TFP_OPS_EXE } TfpOpsModule;
typedef enum TfpOpsNative {
  TFP_OPS_ALLOWED,       /* uint8_t (*)(void *controller, uint32_t weapon)                    */
  TFP_OPS_ENTER,         /* void (*)(void *context, uint32_t avatar, uint32_t weapon)         */
  TFP_OPS_EXIT,          /* void (*)(void *context)                                           */
  TFP_OPS_PRESENTATION,  /* void (*)(void *avatar_owner, uint32_t avatar, uint8_t, uint8_t)   */
  TFP_OPS_BODY_EXISTS,   /* int (*)(uint32_t body, uint32_t event)                            */
  TFP_OPS_BODY_TRIGGER,  /* void (*)(uint32_t body, uint32_t event)                           */
  TFP_OPS_DETACH         /* void (*)(void *world, uint32_t camera_unit)                       */
} TfpOpsNative;
typedef struct TfpOpsRegion { bool answered; uint32_t state, kind, protect; uint64_t allocation; } TfpOpsRegion; /* VirtualQuery */
typedef enum TfpOpsForeign { TFP_OPS_FOREIGN_FALSE, TFP_OPS_FOREIGN_TRUE, TFP_OPS_FOREIGN_OTHER, TFP_OPS_FOREIGN_RAISED } TfpOpsForeign;
typedef struct TfpOpsHost {
  void *user;
  bool wine;                             /* ntdll exports wine_get_version (backend.lua:62) */
  uint64_t (*module)(void *user, TfpOpsModule which);                       /* GetModuleHandleA; 0: none */
  bool (*read)(void *user, uint64_t at, void *out, size_t size);            /* all of it or false */
  bool (*write)(void *user, uint64_t at, const void *bytes, size_t size);
  void (*query)(void *user, uint64_t at, TfpOpsRegion *out);
  /* false: the call raised, `raised` says what. args in the order of the signatures above. */
  bool (*call)(void *user, TfpOpsNative which, uint64_t address, const uint64_t args[4], uint64_t *result, TfpOpsText *raised);
  double (*clock)(void *user);           /* backend.lua has no use for time; kept for the callers that share the host */
  void (*log)(void *user, const char *text);                                /* may be NULL */
  /* The fields runtime.lua installs on the backend. NULL: not installed. */
  TfpOpsForeign (*foreign_camera)(void *user, TfpOpsText *raised);
  TfpOpsResult (*camera_before_entry)(void *user, TfpOpsText *why);
  TfpOpsResult (*placement_resolver)(void *user, const char *key, TfpOpsPlacement *out, TfpOpsText *raised); /* TRUE: *out; NIL: none */
  TfpOpsResult (*presentation_transition)(void *user, TfpOpsPhase *out, TfpOpsText *raised);                 /* TRUE: *out; NIL: none */
  /* Host.require('…/pose.lua').new(Profile.pose) and the alignment one. NULL result: raised. */
  const TfpOpsPoseHelper *(*load_pose)(void *user, TfpOpsText *raised);
  const TfpOpsPoseHelper *(*load_alignment)(void *user, TfpOpsText *raised);
} TfpOpsHost;

/* ---- profile.lua, the part that is not build data ---- */
typedef struct TfpOpsProfile {
  bool has_camera, keep_aim_view, seated_head;
  double camera_forward, camera_up, camera_smoothing_ms, camera_blend_ms, camera_max_gap_ms;
  double pose_duration_ms, pose_blend_ms, pose_offset_x, pose_offset_y, pose_offset_z, alignment_duration_ms;
} TfpOpsProfile;
#define TFP_OPS_PROFILE_SHIPPED {true, true, true, .18, .04, 100, 120, 250, 30500, 150, .15, 0, -.12, 10500}

/* ---- leases and tokens ---- */
typedef struct TfpOpsContext {           /* _alignment_context / _pose_context */
  uint64_t exe, object, registry, units, generations, owner, weapon_descriptor, weapon_object;
  uint32_t unit, index, generation, avatar_index, weapon_unit, weapon_index, weapon_generation;
  uint8_t weapon_bytes[20];
  bool has_owner, has_avatar_index, pose;
} TfpOpsContext;
typedef enum TfpOpsKind { TFP_OPS_KIND_ALIGNMENT, TFP_OPS_KIND_POSE, TFP_OPS_KIND_BODY } TfpOpsKind;
struct TfpOps;
typedef struct TfpOpsToken {
  const struct TfpOps *owner; TfpOpsKind kind; bool issued, used;
  TfpOpsSnapshot identity;               /* the snapshot whose identity (and body) it was prepared for */
  TfpOpsContext context;
  const TfpOpsPoseHelper *helper; void *native;
  struct { uint64_t executable, table, query, trigger; uint32_t event; } api;
} TfpOpsToken;
typedef struct TfpOpsLease {
  const struct TfpOps *owner;            /* warm (bootstrap) leases only */
  TfpOpsSnapshot origin;
  uint64_t request;
  bool bootstrap, done, cleanup_only, has_error, has_alignment;
  char error[320];
  int8_t pose_blend_entry, entry_aim_changed, entry_controller_changed, body_event_aim_changed, body_event_controller_changed; /* -1: nil */
  struct { const TfpOpsPoseHelper *helper; void *prepared; TfpOpsContext context; bool ended; TfpOpsKind kind; TfpOpsHandle *handle; } alignment;
  bool has_alignment_stats, has_pose_stats, has_pose_error, pose_owner_retired, has_pose_retire_reason;
  TfpOpsHelperStats alignment_stats, pose_stats;
  double pose_error;
  char pose_retire_reason[320];
  unsigned orphan_polls;                 /* 0: nil */
  bool body_pose_attempted, body_pose_released;
} TfpOpsLease;

typedef struct TfpOpsConfig {  /* camera_config / fov_config / blur_config / fird_fov_config */
  uint64_t game, exe, context, camera, avatar_owner, request;
  uint32_t avatar_index, avatar, body, weapon, camera_unit, camera_kind;
  bool has_request, has_weapon, is_camera, has_right, presentation_enabled;
  double forward, up, right, smoothing_ms, blend_ms, max_gap_ms;
} TfpOpsConfig;
typedef struct TfpOpsBackpack { uint32_t ref, entity; uint8_t descriptor[20]; } TfpOpsBackpack;
typedef struct TfpOpsBinding { const void *identity; uint64_t address; bool is_function; } TfpOpsBinding; /* an engine Lua function */
typedef enum TfpOpsSetting { TFP_OPS_SETTING_DEFAULT, TFP_OPS_SETTING_DOF, TFP_OPS_SETTING_VIGNETTE, TFP_OPS_SETTING_OTHER } TfpOpsSetting;

typedef struct TfpOpsLayerMap { bool has; uint64_t def; uint32_t count; uint64_t at[2048]; } TfpOpsLayerMap;
typedef struct TfpOps {
  const TfpOpsHost *host;
  TfpOpsProfile profile;
  const TfpOpsPoseHelper *alignment, *pose;
  /* the hook-like plain fields */
  bool support_grip_tracking, fird_camera_active, has_fird_camera_offset;
  double fird_camera_offset[3];
  /* state retail keeps on the instance */
  uint64_t frame_epoch;
  bool frame_active;
  struct { bool has; uint64_t base; } frame_module_base, frame_executable_base;
  struct { bool has; uint64_t base, epoch; } frame_game_checks[3], frame_executable_checks[3];
  TfpOpsLease *alignment_lease, *warm_lease, *presentation_pose_lease;
  bool has_pose_placement, has_placement_weapon_key, has_presentation_pose_epoch;
  TfpOpsPlacement pose_placement;
  char placement_weapon_key[17];
  double presentation_pose_epoch;
  struct { bool has; uint64_t frame; const TfpOpsLease *lease; const TfpOpsPoseHelper *helper; const TfpOpsHandle *handle; TfpOpsPhase token; } publication;
  struct { bool has, hammer; uint64_t world; uint32_t unit; uint8_t descriptor[20]; } camera_only_identity;
  struct { bool has, has_indices, has_table, has_index[2]; uint64_t base, table_pointer; const void *get, *set; uint8_t header[16]; uint32_t index[2]; } renderer;
  unsigned aim_view_keeps, seated_detaches;
  bool has_tokens[3];                    /* by TfpOpsKind: the pose_tokens / alignment_tokens / body_tokens table exists */
  struct { bool has; uint64_t body, instance, graph; uint32_t ref, generation, count; uint8_t resource[8], header[12], definitions[128];
           TfpOpsLayerMap maps[5]; } actions;
  char why[320];                         /* the error being raised */
  bool hold;                             /* not in retail: tfp_ops_set_hold */
} TfpOps;

void tfp_ops_init(TfpOps *b, const TfpOpsHost *host, const TfpOpsPoseHelper *alignment, const TfpOpsPoseHelper *pose); /* Backend.new */
void tfp_ops_begin_frame(TfpOps *b);
void tfp_ops_end_frame(TfpOps *b);
TfpOpsResult tfp_ops_camera_ready(TfpOps *b, TfpOpsText *why);
TfpOpsResult tfp_ops_snapshot(TfpOps *b, TfpOpsSnapshot *out, TfpOpsText *why);            /* TRUE or NIL */
TfpOpsResult tfp_ops_player_snapshot(TfpOps *b, TfpOpsSnapshot *out, TfpOpsText *why);
TfpOpsResult tfp_ops_pin_player_snapshot(TfpOps *b, TfpOpsSnapshot *out, TfpOpsText *why);
/* Not in retail: the action hold's view (controller.h TfpHoldView). The player snapshot, which needs no
 * weapon, with what it does not carry: the avatar's ragdoll flag, what the wield row holds now (0: nothing
 * readable or nothing in the hands) and whether another camera owns the view. Reads only. TRUE or NIL. */
typedef struct TfpOpsHold { TfpOpsSnapshot player; bool ragdoll, foreign_camera; uint32_t wielded; } TfpOpsHold;
TfpOpsResult tfp_ops_hold_snapshot(TfpOps *b, TfpOpsHold *out, TfpOpsText *why);
/* Not in retail: the action hold's switch for release (off after tfp_ops_init, and then release is retail's). On, release
 * also ends a request retail never meets: the lease's own record, still the top request, kind 6, for the lease's
 * avatar, whose weapon field the game rewrote while an action was held. Retail's release calls that "replaced" and
 * is done without an exit, which is right when the mod exits on an action's first tick and wrong after a hold: the
 * request would stay on top with no owner. And a lease whose own request is still on top with the lease's weapon in
 * it, for which neither the full nor the retiring snapshot can be had (an action flag is set and the wield row has
 * moved on), gets the game's exit on the first poll, proven by the player's snapshot, where retail waits four. */
void tfp_ops_set_hold(TfpOps *b, bool on);
TfpOpsResult tfp_ops_backpack(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsBackpack *out, TfpOpsText *why); /* TRUE, NIL (none), RAISED */
TfpOpsResult tfp_ops_camera_only_weapon(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsText *why);
TfpOpsResult tfp_ops_action_state(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsAction *out, TfpOpsText *why); /* TRUE or NIL */
bool tfp_ops_on_foot(TfpOps *b, const TfpOpsSnapshot *s, const TfpOpsAction *action);
TfpOpsResult tfp_ops_camera_config(TfpOps *b, TfpOpsConfig *out, TfpOpsText *why);
TfpOpsResult tfp_ops_fov_config(TfpOps *b, const TfpOpsLease *lease, TfpOpsConfig *out, TfpOpsText *why);
TfpOpsResult tfp_ops_blur_config(TfpOps *b, const TfpOpsLease *lease, TfpOpsConfig *out, TfpOpsText *why);
TfpOpsResult tfp_ops_fird_fov_config(TfpOps *b, TfpOpsConfig *out, TfpOpsText *why);
TfpOpsResult tfp_ops_warm_start(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsLease *lease, TfpOpsText *why);
TfpOpsResult tfp_ops_warm_finish(TfpOps *b, TfpOpsLease *lease, TfpOpsText *why);
TfpOpsResult tfp_ops_invoke_presentation(TfpOps *b, const TfpOpsSnapshot *expected, bool enabled, TfpOpsText *why);
TfpOpsResult tfp_ops_prepare_pose(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, TfpOpsText *why);
TfpOpsResult tfp_ops_prepare_alignment(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, TfpOpsText *why);
TfpOpsResult tfp_ops_prepare_body_pose(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, TfpOpsText *why);
TfpOpsResult tfp_ops_enter(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, TfpOpsLease *lease, TfpOpsText *why);
/* blend_entry: 1 true, 0 false, -1 nil; any other value: not a Boolean (refused). */
TfpOpsResult tfp_ops_enter_pose(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, double remaining_ms, int blend_entry,
                                TfpOpsLease *lease, TfpOpsText *why);
TfpOpsResult tfp_ops_set_placement_defaults(TfpOps *b, const TfpOpsPlacement *placement, TfpOpsText *why);
TfpOpsResult tfp_ops_set_pose_placement(TfpOps *b, TfpOpsLease *lease, const TfpOpsPlacement *placement, TfpOpsText *why);
TfpOpsResult tfp_ops_set_pose_zoom_policy(TfpOps *b, TfpOpsLease *lease, bool active, TfpOpsText *why);
TfpOpsResult tfp_ops_set_ads_placement(TfpOps *b, TfpOpsLease *lease, const TfpOpsPlacement *value, TfpOpsText *why);
TfpOpsResult tfp_ops_set_pose_transition(TfpOps *b, TfpOpsLease *lease, const TfpOpsPhase *token, TfpOpsText *why);
TfpOpsResult tfp_ops_set_pose_motion(TfpOps *b, TfpOpsLease *lease, const TfpOpsMotion *value, TfpOpsText *why);
TfpOpsResult tfp_ops_pose_status(TfpOps *b, TfpOpsLease *lease, TfpOpsHelperStats *out, TfpOpsText *why);      /* TRUE or NIL */
TfpOpsResult tfp_ops_alignment_status(TfpOps *b, TfpOpsLease *lease, TfpOpsHelperStats *out, TfpOpsText *why);
TfpOpsResult tfp_ops_release_body_pose(TfpOps *b, TfpOpsLease *lease, TfpOpsToken *token, TfpOpsText *why);
TfpOpsResult tfp_ops_keep_aim_view(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsText *why);
TfpOpsResult tfp_ops_detach_seated_camera(TfpOps *b, TfpOpsText *why);
TfpOpsResult tfp_ops_release(TfpOps *b, TfpOpsLease *lease, TfpOpsText *why);
/* TRUE / FALSE: the Boolean; NIL: unavailable, with the text. */
TfpOpsResult tfp_ops_renderer_dof_value(TfpOps *b, TfpOpsBinding getter, TfpOpsBinding setter, TfpOpsSetting setting, TfpOpsText *why);

/* The readers and checks other modules call directly (`backend:_read`, `_u8`, `_u32`, `_ptr`, `_map`,
 * `_descriptor`, `_module`, `_alignment_executable`, `_invalidate_frame_checks`, `_alignment_context`,
 * `_pose_context`). false: raised, with the text. profile_pose: Profile.pose's executable record, else
 * Profile.alignment's. */
typedef enum TfpOpsScope { TFP_OPS_SCOPE_FULL, TFP_OPS_SCOPE_IDENTITY, TFP_OPS_SCOPE_ENTRY } TfpOpsScope;
typedef enum TfpOpsExecutable { TFP_OPS_EXECUTABLE_ALIGNMENT, TFP_OPS_EXECUTABLE_POSE, TFP_OPS_EXECUTABLE_CAMERA } TfpOpsExecutable;
bool tfp_ops_module(TfpOps *b, TfpOpsScope scope, uint64_t *base, TfpOpsText *why);
bool tfp_ops_alignment_executable(TfpOps *b, TfpOpsExecutable profile, uint64_t *base, TfpOpsText *why);
void tfp_ops_invalidate_frame_checks(TfpOps *b);
bool tfp_ops_read(TfpOps *b, uint64_t at, void *out, size_t size, TfpOpsText *why);
bool tfp_ops_u8(TfpOps *b, uint64_t at, uint8_t *out, TfpOpsText *why);
/* has_limit false: retail's default, 4096. */
bool tfp_ops_map(TfpOps *b, uint64_t at, uint32_t id, uint32_t limit, bool has_limit, uint32_t *index, TfpOpsText *why);
bool tfp_ops_descriptor(TfpOps *b, uint64_t world, uint32_t id, uint8_t out[24], TfpOpsText *why);
bool tfp_ops_u32(TfpOps *b, uint64_t at, uint32_t *out, TfpOpsText *why);
bool tfp_ops_ptr(TfpOps *b, uint64_t at, uint64_t *out, TfpOpsText *why);
bool tfp_ops_alignment_context(TfpOps *b, uint32_t unit, bool has_owner, uint64_t owner, bool has_avatar_index, uint32_t avatar_index,
                               bool profile_pose, TfpOpsContext *out, TfpOpsText *why);
bool tfp_ops_pose_context(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsContext *out, TfpOpsText *why);
/* tostring() of a Lua number as the reference LuaJIT prints it. */
void tfp_ops_number(double value, char *out, size_t size);
#endif
