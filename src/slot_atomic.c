/* C11 port of the existing fault-contained dispatch CAS. A real call through
 * an immutable volatile pointer keeps LLVM from removing the Windows SEH
 * scope around an intrinsic it otherwise considers nounwind. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "slot_platform.h"
typedef void (*AtomicPointer)(uintptr_t,uintptr_t,uintptr_t,uintptr_t *);
static __declspec(noinline) void atomic_pointer(uintptr_t slot,uintptr_t expected,uintptr_t value,uintptr_t *observed){
    *observed=(uintptr_t)InterlockedCompareExchangePointer((PVOID volatile *)slot,(void *)value,(void *)expected);
}
static AtomicPointer volatile atomic_dispatch=atomic_pointer;
bool fp_atomic_pointer(uintptr_t slot,uintptr_t expected,uintptr_t value,uintptr_t *observed){
    __try {atomic_dispatch(slot,expected,value,observed);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
