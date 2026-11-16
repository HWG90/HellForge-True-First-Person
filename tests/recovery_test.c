#include "recovery.h"
#include "vectors/recovery_vectors.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool same_row(const TfpRecoveryRow *row, const RecoveryVector *v) {
  if (!v->has_row) return !row->present;
  return row->present && (int)row->phase == v->phase && row->attempts == v->attempts &&
         (int)row->classification == v->classification && (int)row->pending_classification == v->pending &&
         fabs((row->next_retry < 0 ? -1 : row->next_retry) - v->next_retry) < 1e-9 &&
         fabs(row->proof_deadline - v->proof_deadline) < 1e-9;
}

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(RECOVERY_VECTOR_COMMIT, commit) || strlen(RECOVERY_VECTOR_SHA256) != 64) {
    fprintf(stderr, "recovery vectors were cut from retail %s, expected %s\n", RECOVERY_VECTOR_COMMIT, commit);
    return 2;
  }
  TfpRecovery r;
  tfp_recovery_init(&r);
  unsigned count = sizeof recovery_vectors / sizeof *recovery_vectors;
  for (unsigned i = 0; i < count; i++) {
    const RecoveryVector *v = recovery_vectors + i;
    TfpFailure state = {v->temporary != 0, v->native_error};
    TfpProof proof = {v->proof[0], v->proof[1], v->proof[2], v->proof[3]};
    const TfpFailure *sp = v->has_state ? &state : NULL;
    const TfpProof *pp = v->has_proof ? &proof : NULL;
    TfpPolicy name = v->name >= 0 ? (TfpPolicy)v->name : TFP_POLICY_FOV;
    bool ok = true;
    switch (v->op) {
    case RV_CLASSIFY: ok = (int)tfp_recovery_classify(name, sp, v->clean, pp) == v->classification; break;
    case RV_FAILURE: { TfpRecoveryRow row = tfp_recovery_failure(&r, name, sp, v->clean, pp, v->now); ok = same_row(&row, v); break; }
    case RV_CLEANUP: ok = tfp_recovery_cleanup(&r, name, v->done, v->now) == (v->flag != 0); break;
    case RV_ACTION: ok = (int)tfp_recovery_action(&r, name, v->now) == v->wanted; break;
    case RV_NEEDS_PROOF: ok = tfp_recovery_needs_proof(&r, name) == (v->flag != 0); break;
    case RV_HAS_POLICIES: ok = tfp_recovery_has_policies(&r) == (v->flag != 0); break;
    case RV_REVALIDATE: ok = tfp_recovery_revalidate(&r, name, pp, v->now) == (v->flag != 0); break;
    case RV_SUCCESS: tfp_recovery_success(&r, name, v->now, v->active); ok = same_row(&r.rows[name], v); break;
    case RV_BLOCKS_VIEW: {
      TfpPolicy which = TFP_POLICY_COUNT;
      TfpRecoveryPhase wanted = TFP_PHASE_NONE;
      bool blocked = tfp_recovery_blocks_view(&r, v->now, &which, &wanted);
      /* Retail walks a hash table, so which blocked policy it names first is not defined; the answer and,
       * when only one core policy blocks, its name and reason are. */
      ok = blocked == (v->flag != 0);
      if (ok && blocked) {
        TfpPolicy other = which == TFP_POLICY_CAMERA ? TFP_POLICY_FOV : TFP_POLICY_CAMERA;
        TfpRecovery copy = r;
        memset(&copy.rows[which], 0, sizeof copy.rows[which]);
        bool both = tfp_recovery_blocks_view(&copy, v->now, NULL, NULL);
        if (!both) ok = (int)which == v->blocked_name && (int)wanted == v->wanted;
        else ok = v->blocked_name == (int)which || v->blocked_name == (int)other;
      }
      break;
    }
    case RV_RESET: tfp_recovery_reset(&r); break;
    case RV_OWNER_RESET: tfp_recovery_owner_reset(&r); break;
    case RV_ROW: ok = same_row(&r.rows[name], v); break;
    }
    if (!ok) {
      const TfpRecoveryRow *row = &r.rows[name];
      fprintf(stderr, "recovery vector %u (op %d, name %d, now %.3f): C row present=%d phase=%d attempts=%d class=%d pending=%d next=%.6f deadline=%.6f\n"
                      "  retail flag=%d phase=%d attempts=%d class=%d pending=%d next=%.6f deadline=%.6f wanted=%d\n",
              i, v->op, v->name, v->now, row->present, row->phase, row->attempts, row->classification,
              row->pending_classification, row->next_retry, row->proof_deadline, v->flag, v->phase, v->attempts,
              v->classification, v->pending, v->next_retry, v->proof_deadline, v->wanted);
      return 1;
    }
  }
  printf("Recovery ledger: %u operations answered as retail's recovery.lua (%s)\n", count, RECOVERY_VECTOR_COMMIT);
  return 0;
}
