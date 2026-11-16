#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "slot_platform.h"
#include <assert.h>
#include <stdio.h>
static void first(void){}
static void second(void){}
static void *slot=first;
int main(void){
    FpSlotPlatform p={(uintptr_t)GetModuleHandleA(NULL),GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version")!=NULL};
    uintptr_t observed=0;assert(fp_slot_read(&p,(uintptr_t)&slot,&observed)&&observed==(uintptr_t)first);
    assert(fp_slot_cas(&p,(uintptr_t)&slot,(uintptr_t)first,(uintptr_t)second,&observed)==FP_CAS_CHANGED&&slot==second);
    assert(fp_slot_cas(&p,(uintptr_t)&slot,(uintptr_t)first,(uintptr_t)first,&observed)==FP_CAS_DIFFERENT&&slot==second);
    assert(fp_slot_cas(&p,(uintptr_t)&slot,(uintptr_t)second,(uintptr_t)first,&observed)==FP_CAS_CHANGED&&slot==first);
    FpSlotPlatform other={p.image+4096,p.wine};assert(!fp_slot_read(&other,(uintptr_t)&slot,&observed));
    assert(fp_slot_cas(&other,(uintptr_t)&slot,(uintptr_t)first,(uintptr_t)second,&observed)==FP_CAS_UNCERTAIN&&slot==first);
    void *private=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(private);*(void **)private=first;
    assert(fp_slot_cas(&p,(uintptr_t)private,(uintptr_t)first,(uintptr_t)second,&observed)==FP_CAS_UNCERTAIN&&*(void **)private==first);
    assert(!fp_atomic_pointer(1,(uintptr_t)first,(uintptr_t)second,&observed));
    assert(!fp_atomic_pointer((uintptr_t)&slot,(uintptr_t)first,(uintptr_t)second,(uintptr_t *)1));
    VirtualFree(private,0,MEM_RELEASE);puts("C slot platform: owned image, foreign/private refusal, atomic CAS and fault containment passed");return 0;
}
