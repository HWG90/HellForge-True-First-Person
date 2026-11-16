#ifndef TFP_FOV_POLICY_H
#define TFP_FOV_POLICY_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "backend.h"
#include "dispatch_lease.h"
#include "slot_platform.h"
#include "lens.h"
typedef struct FpFovPolicy {
    SRWLOCK lock;FpMemory memory;FpSnapshot origin;FpSlotPlatform platform;FpDispatchLease lease;
    uint64_t token;uintptr_t slot;FpLens lens;float native,applied,degrees;uint64_t seen,changed;
} FpFovPolicy;
void fp_fov_policy_init(FpFovPolicy *);
bool fp_fov_policy_open(FpFovPolicy *,const FpMemory *,const FpSnapshot *,char *,size_t);
bool fp_fov_policy_close(FpFovPolicy *);
#endif
