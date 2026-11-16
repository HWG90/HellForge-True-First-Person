#ifndef TFP_DISPATCH_LEASE_H
#define TFP_DISPATCH_LEASE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#define FP_DISPATCH_LIMIT 8
typedef enum FpCasResult {FP_CAS_CHANGED,FP_CAS_DIFFERENT,FP_CAS_UNCERTAIN} FpCasResult;
/* The platform adapter checks image/slot allocation identity and writable
 * data protection before its atomic operation. UNCERTAIN may have written. */
typedef FpCasResult (*FpSlotCas)(void *,uintptr_t slot,uintptr_t expected,uintptr_t value,uintptr_t *observed);
typedef bool (*FpSlotRead)(void *,uintptr_t slot,uintptr_t *value);
typedef struct FpDispatchSlot {uintptr_t address,original,replacement;bool possible;} FpDispatchSlot;
typedef struct FpDispatchLease {
    FpDispatchSlot slots[FP_DISPATCH_LIMIT];unsigned count;
    uint64_t counter,generation,retired;atomic_bool armed;
} FpDispatchLease;
void fp_dispatch_init(FpDispatchLease *);
/* 1 armed; 2 owns partial cleanup; 0 owns nothing. Publish *token BEFORE the
 * first CAS. Caller serializes lifecycle operations and pins callback code. */
unsigned fp_dispatch_begin(FpDispatchLease *,const FpDispatchSlot *,unsigned,FpSlotRead,FpSlotCas,void *,uint64_t *token);
bool fp_dispatch_end(FpDispatchLease *,uint64_t,FpSlotCas,void *);
bool fp_dispatch_owns(const FpDispatchLease *,uint64_t);
#endif
