#ifndef TFP_POLICY_OWNER_H
#define TFP_POLICY_OWNER_H
#include "backend.h"
#include "dispatch_lease.h"
/* A cached callback must forward unchanged after a foreign slot successor.
 * Every slot in a paired pose lease must still belong to this generation. */
static inline bool fp_policy_dispatch(const FpMemory *m,
                                      const FpDispatchLease *lease) {
  if (!lease->count || lease->count > FP_DISPATCH_LIMIT)
    return false;
  for (unsigned i = 0; i < lease->count; i++) {
    uintptr_t value = 0;
    if (!fp_pointer(m, lease->slots[i].address, &value) ||
        value != lease->slots[i].replacement)
      return false;
  }
  return true;
}
#endif
