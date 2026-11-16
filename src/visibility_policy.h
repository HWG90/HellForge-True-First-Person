#ifndef TFP_VISIBILITY_POLICY_H
#define TFP_VISIBILITY_POLICY_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "backend.h"
#include "dispatch_lease.h"
#include "slot_platform.h"

/* Current held Unit plus a bounded linked-Unit subtree. These are native
 * UnitRefs, never Lua userdata or render-mesh indices. */
#define FP_VISIBILITY_UNITS 33
typedef struct FpVisibilityUnit {
  uint32_t ref;
  uintptr_t object, parent, world, child, next;
} FpVisibilityUnit;
typedef struct FpVisibilityPolicy {
  SRWLOCK lock;
  FpMemory memory;
  FpSnapshot origin, current;
  FpSlotPlatform platform;
  FpDispatchLease lease;
  uint64_t token;
  uintptr_t slot, body_object, camera_object;
  FpVisibilityUnit units[FP_VISIBILITY_UNITS];
  unsigned count;
  bool wanted;
  atomic_uint candidate_refs[FP_VISIBILITY_UNITS], candidate_count;
  atomic_uint_fast64_t suppressed, forwarded;
} FpVisibilityPolicy;

void fp_visibility_policy_init(FpVisibilityPolicy *);
/* Lease only, from qualified local kind4 before entry or existing kind6. No
 * visibility call, cache repair or property restoration is performed. */
bool fp_visibility_policy_open(FpVisibilityPolicy *, const FpMemory *,
                               const FpSnapshot *, char *, size_t);
/* Returns lease ownership, not presentation acceptance. An ineligible or
 * unreadable current view clears candidates and forwards every native call. */
bool fp_visibility_policy_update(FpVisibilityPolicy *, uint64_t,
                                 const FpSnapshot *, bool wanted);
bool fp_visibility_policy_close(FpVisibilityPolicy *);
#endif
