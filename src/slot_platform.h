#ifndef TFP_SLOT_PLATFORM_H
#define TFP_SLOT_PLATFORM_H
#include "dispatch_lease.h"
typedef struct FpSlotPlatform {uintptr_t image;bool wine;} FpSlotPlatform;
bool fp_slot_read(void *,uintptr_t,uintptr_t *);
FpCasResult fp_slot_cas(void *,uintptr_t,uintptr_t,uintptr_t,uintptr_t *);
bool fp_atomic_pointer(uintptr_t,uintptr_t,uintptr_t,uintptr_t *);
#endif
