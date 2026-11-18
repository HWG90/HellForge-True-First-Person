/* See wire_recovery.c. */
#ifndef TFP_WIRE_RECOVERY_H
#define TFP_WIRE_RECOVERY_H
#include "tfp_runtime.h"

/* What recovery.lua reads off the failing policy's result(): failure_kind == 'temporary_read' and
 * failure_native_error. false: that policy's result carries neither. */
typedef bool (*TfpWireFailureOf)(TfpRtHost *h, const char *policy, bool *temporary_read, int *native_error);
/* Binds the twelve recovery_* pointers. One ledger per process, as the mode has one runtime. */
void tfp_wire_recovery_bind(TfpRtHost *h, TfpWireFailureOf lookup);
/* Retail's texts. NULL: not a phase. -1: not one of the five managed policies. */
const char *tfp_wire_recovery_phase(int phase);
int tfp_wire_recovery_policy(const char *name);
/* For a live replace: a policy has a row in the ledger (a failed part is being cleaned up or retried). */
bool tfp_wire_recovery_busy(void);
#endif
