/* Writable image dispatch slots only. This never changes page permissions,
 * writes instruction bytes, scans hooks, or intercepts the optic selector. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "slot_platform.h"
static bool slot_valid(const FpSlotPlatform *p,uintptr_t slot,bool write){
    MEMORY_BASIC_INFORMATION region={0};
    if(!p||!p->image||slot<65536||slot>=UINT64_C(0x800000000000)||slot%sizeof(uintptr_t)
        ||VirtualQuery((const void *)slot,&region,sizeof region)!=sizeof region||region.State!=MEM_COMMIT
        ||(uintptr_t)region.AllocationBase!=p->image||slot<(uintptr_t)region.BaseAddress
        ||slot-(uintptr_t)region.BaseAddress>region.RegionSize||sizeof(uintptr_t)>region.RegionSize-(slot-(uintptr_t)region.BaseAddress)
        ||(region.Type!=MEM_IMAGE&&!(p->wine&&region.Type==MEM_MAPPED)))return false;
    if(write)return region.Protect==PAGE_READWRITE||region.Protect==PAGE_WRITECOPY;
    return !(region.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&region.Protect!=0;
}
bool fp_slot_read(void *user,uintptr_t slot,uintptr_t *out){
    SIZE_T copied=0;
    return out&&slot_valid(user,slot,false)&&ReadProcessMemory(GetCurrentProcess(),(const void *)slot,out,sizeof *out,&copied)&&copied==sizeof *out;
}
FpCasResult fp_slot_cas(void *user,uintptr_t slot,uintptr_t expected,uintptr_t value,uintptr_t *observed){
    if(!observed||!slot_valid(user,slot,true)||!fp_atomic_pointer(slot,expected,value,observed))return FP_CAS_UNCERTAIN;
    return *observed==expected?FP_CAS_CHANGED:FP_CAS_DIFFERENT;
}
