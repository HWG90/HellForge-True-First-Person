/* Session and lease state machine of True First Person: when the native
 * first-person view is entered, held, let go and re-entered. Translation of
 * the manual ("pose_offset", persistent) branch of retail
 * src/first_person/controller.lua at 0.5.41-smooth.66, including its status
 * texts, the arms / weapon placement it keeps (get_placement, placement_target,
 * set_placement, :102-135) and its status line (:138-147).
 * tests/vectors/controller_vectors.h holds that file's own answers.
 * The timed test modes of the retail file are not translated. The backend
 * owns game calls and lease validity; this unit owns timing and decisions. */
#ifndef TFP_CONTROLLER_H
#define TFP_CONTROLLER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What the controller looks at in a snapshot (snapshot() or player_snapshot()
 * of the backend). identity and player_identity are retail's strings reduced
 * to keys; a player-level view carries its player key in both. */
typedef struct TfpView {
  bool available;                 /* false: the snapshot failed; error holds why (may be empty) */
  uint64_t identity, player_identity;
  bool aiming, controller_active, safe;
  uint32_t camera_kind;
  uint64_t record;                /* 0: none */
  uint8_t guard;                  /* controller byte 5 */
  const char *reason;             /* why safe is false, when the backend knows; may be NULL */
  char error[128];
} TfpView;
/* weapon_updates / zero_restores: the helper's running counts (stats.weapon_updates, .zero_restores);
 * has_* false: the backend does not report one (nil in retail). The controller clears a TfpPoseStats
 * before it asks, so a backend that knows nothing of them leaves them absent. */
typedef struct TfpPoseStats {
  bool armed, expired; double target_weight, blend; int error;
  bool has_weapon_updates, has_zero_restores; double weapon_updates, zero_restores;
} TfpPoseStats;
/* A lease on the native view. Owned and filled by the backend. */
typedef struct TfpLease {
  uint64_t request;               /* the camera request it entered with; 0: unknown */
  bool cleanup_only, pose_owner_retired;
  /* The last error number the pose helper reported for this lease, from a status poll or from
   * its final statistics when the backend disarmed it. Retail reads lease.pose_error and falls
   * back to lease.pose_stats.error; the backend folds both into this pair. */
  bool has_pose_error; int pose_error;
  /* lease.pose_stats or lease.alignment_stats: the statistics the backend last recorded on the lease
   * (a status poll, or the final ones when it disarmed the helper). Read once the release is
   * acknowledged (:165). has_pose_stats false: neither is there. */
  bool has_pose_stats; TfpPoseStats pose_stats;
} TfpLease;
/* ---- the action hold (a probe; docs/design/ACTION-CAMERA.md, "Action hold") ----
 * What the controller asks when the snapshot no longer allows the native view while a lease runs: is
 * the game's own first-person request still this lease's, so that only an action took `safe` away?
 * Unlike a full snapshot this view also answers with empty hands. */
typedef enum TfpHoldWeapon { TFP_HOLD_WEAPON_NONE, TFP_HOLD_WEAPON_SAME, TFP_HOLD_WEAPON_OTHER } TfpHoldWeapon;
typedef struct TfpHoldView {
  bool available;                 /* false: the local player or its camera request is not proven (the camera off the avatar included) */
  uint64_t player_identity;
  uint32_t camera_kind;
  uint64_t record;                /* the top camera request */
  bool safe;                      /* the player-level snapshot's own `safe` */
  bool seated, ragdoll, full_aim, foreign_camera;
  TfpHoldWeapon weapon;           /* what is in the hands, against the lease's weapon */
} TfpHoldView;
typedef struct TfpPhasePacket { bool available; uint64_t identity, player_identity, epoch; double progress; } TfpPhasePacket;

/* why buffers: a refusal's text, empty when the backend gives none. */
typedef struct TfpBackendApi {
  void *user;
  void (*snapshot)(void *user, TfpView *out);
  void (*player_snapshot)(void *user, TfpView *out);
  bool (*camera_only_weapon)(void *user, const TfpView *current);
  bool (*prepare_pose)(void *user, const TfpView *current, char *why, size_t size);
  TfpLease *(*enter_pose)(void *user, const TfpView *current, uint32_t remaining_ms, bool blend_entry, char *why, size_t size);
  bool (*pose_status)(void *user, TfpLease *lease, TfpPoseStats *out, char *why, size_t size);
  bool (*release)(void *user, TfpLease *lease, char *why, size_t size);
  void (*presentation_transition)(void *user, TfpPhasePacket *out);   /* may be NULL */
  bool (*set_pose_transition)(void *user, TfpLease *lease, const TfpPhasePacket *packet, char *why, size_t size); /* may be NULL */
  double (*clock)(void *user);    /* monotonic seconds */
  /* The action hold's view for this lease. May be NULL (and is never called while the hold is off). */
  void (*hold_view)(void *user, const TfpLease *lease, TfpHoldView *out);
} TfpBackendApi;

/* ---- arms / weapon placement (:23, :102-135) ----
 * A placement table as Lua sees it, field by field: x, y, z in metres, yaw, pitch, roll in degrees. */
typedef enum TfpAxisKind { TFP_AXIS_NIL, TFP_AXIS_NUMBER, TFP_AXIS_OTHER } TfpAxisKind; /* nil or false; a number; anything else */
enum { TFP_AXIS_X, TFP_AXIS_Y, TFP_AXIS_Z, TFP_AXIS_YAW, TFP_AXIS_PITCH, TFP_AXIS_ROLL, TFP_AXIS_N };
typedef struct TfpPlacementValue { TfpAxisKind kind[TFP_AXIS_N]; double v[TFP_AXIS_N]; } TfpPlacementValue;
/* The backend's side of placement. A set_* answers retail's `accepted == true`; why takes its second
 * result, or the text it raised (the controller calls it under pcall), empty when there is none. */
typedef struct TfpPlacementApi {
  void *user;
  bool (*pose_placement)(void *user, TfpPlacementValue *out);     /* backend.pose_placement; false: nil */
  const char *(*placement_weapon_key)(void *user);                /* backend.placement_weapon_key; NULL: nil */
  bool (*set_pose_placement)(void *user, TfpLease *lease, const double value[TFP_AXIS_N], char *why, size_t size);
  bool (*set_placement_defaults)(void *user, const double value[TFP_AXIS_N], char *why, size_t size);
} TfpPlacementApi;
#define TFP_PLACEMENT_REACH 0.5      /* controller.lua:116, metres */
#define TFP_PLACEMENT_ANGLE 90.0     /* controller.lua:123, degrees */

typedef enum TfpPhase { TFP_PHASE_IDLE, TFP_PHASE_WAITING, TFP_PHASE_RUNNING, TFP_PHASE_CLEANUP_PENDING,
                        TFP_PHASE_DISABLED } TfpPhase;
typedef struct TfpController {
  const TfpBackendApi *backend;
  TfpPhase phase;
  bool closing, enabled, manual;
  TfpLease *lease;
  char label[256], wait_reason[160], stop_reason[160], suspend_reason[160];
  bool has_stop_reason;
  struct { uint64_t identity, player_identity; bool aiming; } initial;
  double started, next_manual_entry, guard_until;
  bool has_ads_return, has_pose, pose_aiming;
  struct { uint64_t identity, player_identity; } ads_return;
  unsigned runs, enter_attempts, release_attempts, samples, observed_native_without_aim, reacquisitions, aim_transitions;
  TfpPlacementValue placement;       /* :23 {x=.15,y=0,z=-.12} until a set_placement is accepted */
  /* summary.weapon_updates, .zero_restores, .blend, .target_weight and pose_seen (:81-95) */
  double weapon_updates, zero_restores, seen_weapon_updates, seen_zero_restores, blend, target_weight;
  bool has_blend;                    /* false: summary.blend is nil (no statistics since the session began) */
  /* The action hold. Not in retail; all false / 0 unless tfp_controller_set_hold switched it on. */
  bool hold_enabled, holding;
  double hold_started;
  bool hold_empty; double hold_empty_since; /* a hold with no action flag and nothing in the hands, and since when */
  unsigned holds, hold_resumes, hold_exits;
  char hold_why[160];                /* holding: the exit that is held back; else why the last hold ended or was not taken */
} TfpController;

#define TFP_GUARD_HOLD 0.75          /* controller.lua:19 */
#define TFP_FIRST_ENTRY_DELAY 0.1    /* controller.lua:210 */
#define TFP_ENTRY_SPACING 0.25       /* controller.lua:456 */
#define TFP_HOLD_CAP 4.0             /* seconds a hold may last; not in retail */
#define TFP_HOLD_EMPTY_CAP 1.0       /* seconds on end a hold may last with no action flag and empty hands; not in retail */

void tfp_controller_init(TfpController *c, const TfpBackendApi *backend);
bool tfp_controller_start(TfpController *c);                       /* F11 on */
bool tfp_controller_stop(TfpController *c, const char *reason);    /* F11 off; reason NULL: "manual stop" */
bool tfp_controller_shutdown(TfpController *c);
bool tfp_controller_suspend(TfpController *c, const char *reason);
bool tfp_controller_update(TfpController *c);
bool tfp_controller_retry_cleanup(TfpController *c);
/* self:get_placement(): backend.pose_placement, else the controller's own; angles that are not there are 0. */
void tfp_controller_get_placement(const TfpController *c, const TfpPlacementApi *api, TfpPlacementValue *out);
/* self:placement_target(): false is nil. true: the backend's weapon key and the lease whose
 * origin.identity is retail's second result (the binder renders it; a TfpLease has no origin). */
bool tfp_controller_placement_target(const TfpController *c, const TfpPlacementApi *api, const char **key, TfpLease **lease);
/* self:set_placement(value). value NULL: not a table. false: why holds retail's refusal text. With a
 * running lease the backend's set_pose_placement is asked, otherwise set_placement_defaults. */
bool tfp_controller_set_placement(TfpController *c, const TfpPlacementApi *api, const TfpPlacementValue *value, char *why, size_t size);
/* self:status(): the status line, cut to size. Answers the length it wanted (as snprintf). */
int tfp_controller_status(const TfpController *c, char *out, size_t size);
/* The action hold (not in retail; off after tfp_controller_init, and then nothing below differs from retail).
 * On: while a lease runs, an exit whose only reason is an action is held back for as long as
 *   - the hold view is available and names the lease's own player,
 *   - the top camera request is kind 6 and is the lease's own record,
 *   - the avatar is not a ragdoll, not seated, not in full aim, and no other camera owns the view,
 *   - it is not the case that the player-level view is safe again with another weapon in the hands,
 *   - it is not the case that the player-level view is safe (no action flag) while this update's full snapshot is
 *     there and unsafe (the character is off the verified on-foot lifecycle: that is not an action's flag),
 *   - the player-level view has not been safe with empty hands for TFP_HOLD_EMPTY_CAP on end (a dropped weapon;
 *     a stim's middle is shorter),
 *   - the hold is younger than TFP_HOLD_CAP.
 * The lease resumes, with no exit and no entry, on the first update whose full snapshot is safe with the
 * lease's weapon identity, kind 6 and the lease's record. Anything else takes the exit of today. */
void tfp_controller_set_hold(TfpController *c, bool enabled);
/* The switch is on and a lease runs: an action would be held (the pose helper has to tolerate its first frame). */
static inline bool tfp_controller_hold_armed(const TfpController *c) { return c->hold_enabled && c->lease && c->phase == TFP_PHASE_RUNNING; }
static inline bool tfp_controller_is_clean(const TfpController *c) { return !c->lease && !c->enabled; }
#endif
