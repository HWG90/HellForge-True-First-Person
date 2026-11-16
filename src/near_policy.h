#ifndef TFP_NEAR_POLICY_H
#define TFP_NEAR_POLICY_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "backend.h"
#include "dispatch_lease.h"
#include "slot_platform.h"
typedef struct FpNearPolicy {
    SRWLOCK lock;FpMemory memory;FpSnapshot origin,property_owner;FpSlotPlatform platform;FpDispatchLease lease;
    uint64_t token;uintptr_t slot,body_object,camera_object;
    float hip,aim,written,baseline;bool property_owned;
} FpNearPolicy;
void fp_near_policy_init(FpNearPolicy *);
bool fp_near_policy_open(FpNearPolicy *,const FpMemory *,const FpSnapshot *,char *,size_t);
/* Zero disables that state; a positive 0..1m value replaces the native near
 * exactly on qualified non-ADS native FP writes, in either direction. */
bool fp_near_policy_set(FpNearPolicy *,uint64_t,float hip,float aim);
bool fp_near_policy_close(FpNearPolicy *);
#endif
