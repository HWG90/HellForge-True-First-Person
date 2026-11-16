#include "recovery.h"
#include <math.h>
#include <string.h>

#define MAX_ATTEMPTS 4      /* recovery.lua:26 */
#define BASE_DELAY .25      /* :27 */
#define MAX_DELAY 2.0       /* :27 */
#define STABLE_SECONDS 2.0  /* :28 */

static bool core(TfpPolicy name) { return name == TFP_POLICY_CAMERA || name == TFP_POLICY_FOV; } /* :7 */
static bool positive(const TfpProof *p) { return p && p->fresh && p->safe && p->same_player && !p->foreign; } /* :8 */

TfpRecoveryClass tfp_recovery_classify(TfpPolicy name, const TfpFailure *state, bool clean, const TfpProof *proof) { /* :11 */
  if (!clean) return TFP_CLASS_CLEANUP;
  if (state && state->temporary_read && state->native_error == 3 && positive(proof)) return TFP_CLASS_RETRY;
  if (!core(name)) return TFP_CLASS_UNAVAILABLE;
  return TFP_CLASS_FATAL;
}

void tfp_recovery_init(TfpRecovery *r) { memset(r, 0, sizeof *r); }
void tfp_recovery_reset(TfpRecovery *r) { /* :33 */
  memset(r->rows, 0, sizeof r->rows);
  r->session++;
}
void tfp_recovery_owner_reset(TfpRecovery *r) { /* :36 */
  r->session++;
  for (int i = 0; i < TFP_POLICY_COUNT; i++) {
    TfpRecoveryRow *row = r->rows + i;
    if (row->present && (row->classification == TFP_CLASS_UNAVAILABLE ||
                         row->pending_classification == TFP_CLASS_UNAVAILABLE)) {
      row->phase = TFP_PHASE_UNAVAILABLE;
      row->next_retry = -1;
      row->session = r->session;
    } else memset(row, 0, sizeof *row);
  }
}
static double delay(int attempts) { return fmin(MAX_DELAY, BASE_DELAY * pow(2, attempts - 1)); }
/* Shared tail of failure and cleanup: where a row goes once it is clean. */
static void settle(TfpRecoveryRow *row, double now) {
  if (row->classification == TFP_CLASS_RETRY && row->attempts < MAX_ATTEMPTS) {
    row->phase = TFP_PHASE_WAITING;
    row->next_retry = now + delay(row->attempts);
    row->proof_deadline = now + 10;
  } else if (row->classification == TFP_CLASS_RETRY) row->phase = TFP_PHASE_EXHAUSTED;
  else if (row->classification == TFP_CLASS_FATAL) row->phase = TFP_PHASE_FATAL;
  else row->phase = TFP_PHASE_UNAVAILABLE;
}
TfpRecoveryRow tfp_recovery_failure(TfpRecovery *r, TfpPolicy name, const TfpFailure *state, bool clean,
                                    const TfpProof *proof, double now) { /* :48 */
  TfpRecoveryRow *row = r->rows + name;
  if (!row->present) {
    memset(row, 0, sizeof *row);
    row->present = true;
    row->next_retry = -1;
  }
  row->attempts++;
  row->has_stable_since = false;
  row->classification = tfp_recovery_classify(name, state, clean, proof);
  row->pending_classification = tfp_recovery_classify(name, state, true, proof);
  row->core = core(name);
  row->session = r->session;
  row->proof_deadline = now + 10;
  if (!clean) {
    row->phase = TFP_PHASE_CLEANUP;
    row->next_retry = -1;
  } else {
    /* :58-63: only the waiting branch sets a retry time; every other branch clears it. */
    row->next_retry = -1;
    settle(row, now);
  }
  return *row;
}
bool tfp_recovery_cleanup(TfpRecovery *r, TfpPolicy name, bool done, double now) { /* :66 */
  TfpRecoveryRow *row = r->rows + name;
  if (!row->present || row->phase != TFP_PHASE_CLEANUP) return true;
  if (!done) return false;
  row->classification = row->pending_classification;
  settle(row, now); /* :71-76: unlike failure, the other branches leave next_retry as it was */
  return true;
}
static TfpRecoveryPhase action(TfpRecoveryRow *row, double now, bool advance) { /* :78 */
  if (!row->present || row->phase == TFP_PHASE_HEALTHY) return TFP_ACTION_UPDATE;
  if (row->phase == TFP_PHASE_CLEANUP) return TFP_PHASE_CLEANUP;
  if (row->phase == TFP_PHASE_WAITING || row->phase == TFP_PHASE_WAITING_PROOF) {
    if (now < row->next_retry) return TFP_ACTION_WAIT;
    if (advance) row->phase = TFP_PHASE_RETRYING;
    return TFP_ACTION_UPDATE;
  }
  if (row->phase == TFP_PHASE_RETRYING) return TFP_ACTION_UPDATE;
  return row->phase;
}
TfpRecoveryPhase tfp_recovery_action(TfpRecovery *r, TfpPolicy name, double now) { return action(r->rows + name, now, true); }
bool tfp_recovery_needs_proof(const TfpRecovery *r, TfpPolicy name) { /* :93 */
  return r->rows[name].present && r->rows[name].phase == TFP_PHASE_RETRYING;
}
bool tfp_recovery_has_policies(const TfpRecovery *r) {
  for (int i = 0; i < TFP_POLICY_COUNT; i++) if (r->rows[i].present) return true;
  return false;
}
bool tfp_recovery_revalidate(TfpRecovery *r, TfpPolicy name, const TfpProof *proof, double now) { /* :98 */
  TfpRecoveryRow *row = r->rows + name;
  if (!row->present || row->phase != TFP_PHASE_RETRYING) return true;
  if (positive(proof)) return true;
  if (now >= row->proof_deadline) {
    row->phase = TFP_PHASE_EXHAUSTED;
    row->next_retry = -1;
  } else {
    row->phase = TFP_PHASE_WAITING_PROOF;
    row->next_retry = now + .25;
  }
  return false;
}
void tfp_recovery_success(TfpRecovery *r, TfpPolicy name, double now, bool active) { /* :106 */
  TfpRecoveryRow *row = r->rows + name;
  if (!row->present || (row->phase != TFP_PHASE_RETRYING && row->phase != TFP_PHASE_HEALTHY) || !active) return;
  row->phase = TFP_PHASE_HEALTHY;
  if (!row->has_stable_since) {
    row->has_stable_since = true;
    row->stable_since = now;
  }
  if (now >= row->stable_since && now - row->stable_since >= STABLE_SECONDS) memset(row, 0, sizeof *row);
}
bool tfp_recovery_blocks_view(const TfpRecovery *r, double now, TfpPolicy *name, TfpRecoveryPhase *wanted) { /* :113 */
  for (int i = 0; i < TFP_POLICY_COUNT; i++) {
    TfpRecoveryRow row = r->rows[i];
    if (!row.present || !row.core) continue;
    TfpRecoveryPhase want = action(&row, now, false);
    if (want != TFP_ACTION_UPDATE) {
      if (name) *name = (TfpPolicy)i;
      if (wanted) *wanted = want;
      return true;
    }
  }
  return false;
}
