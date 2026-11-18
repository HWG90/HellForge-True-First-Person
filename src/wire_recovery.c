/* The recovery ledger behind the runtime host: retail recovery.lua's methods as runtime.lua calls
 * them (policy names and phases as texts), on src/recovery.c. A file of its own because recovery.h
 * and controller.h both define TFP_PHASE_WAITING (docs/translation/RUNTIME-TRANSLATION.md). No Windows. */
#include "wire_recovery.h"
#include "recovery.h"
#include "wire_off.h"
#include <string.h>

static TfpRecovery ledger;
static TfpWireFailureOf failure_of;

static const char *const POLICY[TFP_POLICY_COUNT] = {"fov", "blur", "camera", "head", "movement"};
const char *tfp_wire_recovery_phase(int phase) {
  switch ((TfpRecoveryPhase)phase) {
    case TFP_PHASE_CLEANUP: return "cleanup";
    case TFP_PHASE_WAITING: return "waiting";
    case TFP_PHASE_WAITING_PROOF: return "waiting_proof";
    case TFP_PHASE_RETRYING: return "retrying";
    case TFP_PHASE_EXHAUSTED: return "exhausted";
    case TFP_PHASE_FATAL: return "fatal";
    case TFP_PHASE_UNAVAILABLE: return "unavailable";
    case TFP_PHASE_HEALTHY: return "healthy";
    case TFP_ACTION_UPDATE: return "update";
    case TFP_ACTION_WAIT: return "wait";
    default: return NULL;
  }
}
/* -1: a name recovery.lua would have made a new row for; the ledger of recovery.c has the five. */
int tfp_wire_recovery_policy(const char *name) {
  for (int i = 0; name && i < TFP_POLICY_COUNT; i++)
    if (!strcmp(name, POLICY[i])) return i;
  return -1;
}
static bool truthy(TfpRtValue v) { return v.kind != TFP_RT_NIL && v.kind != TFP_RT_FALSE; }
static TfpProof proof_of(const TfpRtValue *p) {
  TfpProof proof = {p[TFP_RT_PROOF_FRESH].kind == TFP_RT_TRUE, p[TFP_RT_PROOF_SAFE].kind == TFP_RT_TRUE,
                    p[TFP_RT_PROOF_SAME_PLAYER].kind == TFP_RT_TRUE, truthy(p[TFP_RT_PROOF_FOREIGN])};
  return proof;
}
static void row_table(const TfpRecoveryRow *row, TfpRtTable *t) {
  memset(t, 0, sizeof *t);
  t->present = true;
  t->ref = row;
  t->f[TFP_RT_ROW_CORE] = tfp_wire_bool(row->core);
  t->f[TFP_RT_ROW_PHASE] = tfp_wire_text(tfp_wire_recovery_phase(row->phase));
}
#define POLICY_OR(answer) int policy = tfp_wire_recovery_policy(name); if (policy < 0) { h->raised = "Unknown presentation policy"; return answer; }

static bool rc_new(TfpRtHost *h) { (void)h; tfp_recovery_init(&ledger); return true; }
static bool rc_reset(TfpRtHost *h) { (void)h; tfp_recovery_reset(&ledger); return true; }
static bool rc_owner_reset(TfpRtHost *h) { (void)h; tfp_recovery_owner_reset(&ledger); return true; }
static bool rc_has_policies(TfpRtHost *h, TfpRtValue *o1) { (void)h; *o1 = tfp_wire_bool(tfp_recovery_has_policies(&ledger)); return true; }
static bool rc_action(TfpRtHost *h, const char *name, double now, TfpRtValue *o1) {
  POLICY_OR(false)
  *o1 = tfp_wire_text(tfp_wire_recovery_phase(tfp_recovery_action(&ledger, (TfpPolicy)policy, now)));
  return true;
}
static bool rc_cleanup(TfpRtHost *h, const char *name, bool clean, double now) {
  POLICY_OR(false)
  tfp_recovery_cleanup(&ledger, (TfpPolicy)policy, clean, now);
  return true;
}
static bool rc_needs_proof(TfpRtHost *h, const char *name, TfpRtValue *o1) {
  POLICY_OR(false)
  *o1 = tfp_wire_bool(tfp_recovery_needs_proof(&ledger, (TfpPolicy)policy));
  return true;
}
static bool rc_revalidate(TfpRtHost *h, const char *name, const TfpRtValue *proof, double now, TfpRtValue *o1) {
  POLICY_OR(false)
  TfpProof p = proof_of(proof);
  *o1 = tfp_wire_bool(tfp_recovery_revalidate(&ledger, (TfpPolicy)policy, &p, now));
  return true;
}
static bool rc_success(TfpRtHost *h, const char *name, double now, bool active) {
  POLICY_OR(false)
  tfp_recovery_success(&ledger, (TfpPolicy)policy, now, active);
  return true;
}
static bool rc_failure(TfpRtHost *h, const char *name, const TfpRtTable *state, bool clean, const TfpRtValue *proof, double now,
                       TfpRtValue why, TfpRtTable *o1) {
  POLICY_OR(false)
  (void)state; (void)why; /* the row keeps no reason in recovery.c; the runtime logs `why` itself */
  TfpFailure failure = {false, 0};
  bool known = failure_of && failure_of(h, name, &failure.temporary_read, &failure.native_error);
  TfpProof p = proof_of(proof);
  static TfpRecoveryRow answered[TFP_POLICY_COUNT];
  answered[policy] = tfp_recovery_failure(&ledger, (TfpPolicy)policy, known ? &failure : NULL, clean, &p, now);
  row_table(&answered[policy], o1);
  return true;
}
static bool rc_snapshot(TfpRtHost *h, TfpRtPolicies *out) {
  (void)h;
  memset(out, 0, sizeof *out);
  out->present = true;
  out->ref = &ledger;
  for (int i = 0; i < TFP_POLICY_COUNT; i++) {
    if (!ledger.rows[i].present) continue;
    out->name[out->count] = POLICY[i];
    row_table(&ledger.rows[i], &out->row[out->count++]);
  }
  return true;
}
static bool rc_blocks_view(TfpRtHost *h, double now, TfpRtValue *o1, TfpRtValue *o2, TfpRtValue *o3) {
  TfpPolicy name = TFP_POLICY_FOV;
  TfpRecoveryPhase wanted = TFP_PHASE_NONE;
  (void)h;
  bool blocked = tfp_recovery_blocks_view(&ledger, now, &name, &wanted);
  *o1 = tfp_wire_bool(blocked);
  *o2 = blocked ? tfp_wire_text(POLICY[name]) : tfp_wire_nil();
  *o3 = blocked ? tfp_wire_text(tfp_wire_recovery_phase(wanted)) : tfp_wire_nil();
  return true;
}

void tfp_wire_recovery_bind(TfpRtHost *h, TfpWireFailureOf lookup) {
  failure_of = lookup;
  h->recovery_new = rc_new;
  h->recovery_reset = rc_reset;
  h->recovery_owner_reset = rc_owner_reset;
  h->recovery_has_policies = rc_has_policies;
  h->recovery_action = rc_action;
  h->recovery_cleanup = rc_cleanup;
  h->recovery_needs_proof = rc_needs_proof;
  h->recovery_revalidate = rc_revalidate;
  h->recovery_success = rc_success;
  h->recovery_failure = rc_failure;
  h->recovery_snapshot = rc_snapshot;
  h->recovery_blocks_view = rc_blocks_view;
}
bool tfp_wire_recovery_busy(void) { return tfp_recovery_has_policies(&ledger); }
