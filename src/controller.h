/* Session and lease state machine of True First Person: when the native
 * first-person view is entered, held, let go and re-entered. Translation of
 * the manual ("pose_offset", persistent) branch of retail
 * src/first_person/controller.lua at 0.5.41-smooth.66, including its status
 * texts. tests/vectors/controller_vectors.h holds that file's own answers.
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
typedef struct TfpPoseStats { bool armed, expired; double target_weight, blend; int error; } TfpPoseStats;
/* A lease on the native view. Owned and filled by the backend. */
typedef struct TfpLease {
  uint64_t request;               /* the camera request it entered with; 0: unknown */
  bool cleanup_only, pose_owner_retired;
  bool has_pose_error; int pose_error;      /* set by a successful pose_status */
} TfpLease;
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
} TfpBackendApi;

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
} TfpController;

#define TFP_GUARD_HOLD 0.75          /* controller.lua:19 */
#define TFP_FIRST_ENTRY_DELAY 0.1    /* controller.lua:210 */
#define TFP_ENTRY_SPACING 0.25       /* controller.lua:456 */

void tfp_controller_init(TfpController *c, const TfpBackendApi *backend);
bool tfp_controller_start(TfpController *c);                       /* F11 on */
bool tfp_controller_stop(TfpController *c, const char *reason);    /* F11 off; reason NULL: "manual stop" */
bool tfp_controller_shutdown(TfpController *c);
bool tfp_controller_suspend(TfpController *c, const char *reason);
bool tfp_controller_update(TfpController *c);
bool tfp_controller_retry_cleanup(TfpController *c);
static inline bool tfp_controller_is_clean(const TfpController *c) { return !c->lease && !c->enabled; }
#endif
