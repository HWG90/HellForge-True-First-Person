/* Bounded recovery policy for the presentation policies. Translation of
 * retail src/first_person/recovery.lua at 0.5.41-smooth.66;
 * tests/vectors/recovery_vectors.h holds that file's own answers. */
#ifndef TFP_RECOVERY_H
#define TFP_RECOVERY_H
#include <stdbool.h>
#include <stdint.h>

typedef enum TfpPolicy { TFP_POLICY_FOV, TFP_POLICY_BLUR, TFP_POLICY_CAMERA, TFP_POLICY_HEAD,
                         TFP_POLICY_MOVEMENT, TFP_POLICY_COUNT } TfpPolicy;
typedef enum TfpRecoveryClass { TFP_CLASS_NONE, TFP_CLASS_CLEANUP, TFP_CLASS_RETRY, TFP_CLASS_UNAVAILABLE,
                                TFP_CLASS_FATAL } TfpRecoveryClass;
/* A row's phase; also what action() answers: UPDATE and WAIT only come from action(). */
typedef enum TfpRecoveryPhase { TFP_PHASE_NONE, TFP_PHASE_CLEANUP, TFP_PHASE_WAITING, TFP_PHASE_WAITING_PROOF,
                                TFP_PHASE_RETRYING, TFP_PHASE_EXHAUSTED, TFP_PHASE_FATAL, TFP_PHASE_UNAVAILABLE,
                                TFP_PHASE_HEALTHY, TFP_ACTION_UPDATE, TFP_ACTION_WAIT } TfpRecoveryPhase;
typedef struct TfpFailure { bool temporary_read; int native_error; } TfpFailure;   /* NULL: no state */
typedef struct TfpProof { bool fresh, safe, same_player, foreign; } TfpProof;      /* NULL: no proof */
typedef struct TfpRecoveryRow {
  bool present, core, has_stable_since;
  int attempts;
  TfpRecoveryPhase phase;
  TfpRecoveryClass classification, pending_classification;
  double next_retry, proof_deadline, stable_since; /* next_retry < 0: none */
  uint64_t session;
} TfpRecoveryRow;
typedef struct TfpRecovery { TfpRecoveryRow rows[TFP_POLICY_COUNT]; uint64_t session; } TfpRecovery;

TfpRecoveryClass tfp_recovery_classify(TfpPolicy name, const TfpFailure *state, bool clean, const TfpProof *proof);
void tfp_recovery_init(TfpRecovery *r);   /* 4 attempts, 0.25 s doubling to 2 s, healthy after 2 s */
void tfp_recovery_reset(TfpRecovery *r);
void tfp_recovery_owner_reset(TfpRecovery *r);
TfpRecoveryRow tfp_recovery_failure(TfpRecovery *r, TfpPolicy name, const TfpFailure *state, bool clean,
                                    const TfpProof *proof, double now);
bool tfp_recovery_cleanup(TfpRecovery *r, TfpPolicy name, bool done, double now);
TfpRecoveryPhase tfp_recovery_action(TfpRecovery *r, TfpPolicy name, double now);
bool tfp_recovery_needs_proof(const TfpRecovery *r, TfpPolicy name);
bool tfp_recovery_has_policies(const TfpRecovery *r);
bool tfp_recovery_revalidate(TfpRecovery *r, TfpPolicy name, const TfpProof *proof, double now);
void tfp_recovery_success(TfpRecovery *r, TfpPolicy name, double now, bool active);
/* true when a core policy (camera, fov) is not ready; *name and *wanted say which and why. */
bool tfp_recovery_blocks_view(const TfpRecovery *r, double now, TfpPolicy *name, TfpRecoveryPhase *wanted);
#endif
