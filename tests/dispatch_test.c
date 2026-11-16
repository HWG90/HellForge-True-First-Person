#include "dispatch_lease.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct Fake {uintptr_t values[3];unsigned calls,fail_at;bool fault,fail_restore;uint64_t *published;} Fake;
static bool read_slot(void *user,uintptr_t slot,uintptr_t *out){Fake *f=user;if(slot<8||slot>24||slot%8)return false;*out=f->values[slot/8-1];return true;}
static FpCasResult cas(void *user,uintptr_t slot,uintptr_t expected,uintptr_t value,uintptr_t *observed){
    Fake *f=user;assert(*f->published);f->calls++;
    if((f->fail_at&&f->calls==f->fail_at)||(f->fail_restore&&expected>=200)){
        if(f->fault)return FP_CAS_UNCERTAIN;
        f->values[slot/8-1]=999;*observed=999;return FP_CAS_DIFFERENT;
    }
    *observed=f->values[slot/8-1];if(*observed!=expected)return FP_CAS_DIFFERENT;
    f->values[slot/8-1]=value;return FP_CAS_CHANGED;
}
int main(void){
    FpDispatchLease lease;fp_dispatch_init(&lease);uint64_t token=0,old=0;
    FpDispatchSlot slots[3]={{8,100,200,false},{16,101,201,false},{24,102,202,false}};
    Fake f={{100,101,102},0,0,false,false,&token};
    assert(fp_dispatch_begin(&lease,slots,3,read_slot,cas,&f,&token)==1&&token&&lease.armed);
    uint64_t reserved=token;assert(!fp_dispatch_begin(&lease,slots,3,read_slot,cas,&f,&token)&&token==reserved);
    old=token;assert(fp_dispatch_end(&lease,token,cas,&f)&&f.values[0]==100&&f.values[2]==102);assert(fp_dispatch_end(&lease,old,cas,&f));
    assert(fp_dispatch_begin(&lease,slots,3,read_slot,cas,&f,&token)==1&&token!=old);
    unsigned calls=f.calls;assert(!fp_dispatch_end(&lease,old,cas,&f)&&f.calls==calls);
    f.values[1]=777;assert(fp_dispatch_end(&lease,token,cas,&f)&&f.values[1]==777); /* preserve successor */
    f=(Fake){{100,101,102},0,2,true,true,&token};
    assert(fp_dispatch_begin(&lease,slots,3,read_slot,cas,&f,&token)==2&&token&&!lease.armed);
    old=token;assert(!fp_dispatch_end(&lease,old,cas,&f));f.fail_restore=false;
    assert(fp_dispatch_end(&lease,old,cas,&f)&&f.values[0]==100&&f.values[1]==101);
    f=(Fake){{100,101,102},0,2,false,false,&token};
    assert(fp_dispatch_begin(&lease,slots,3,read_slot,cas,&f,&token)==0&&!token&&!lease.generation&&f.values[0]==100&&f.values[1]==999);
    f=(Fake){{100,101,102},0,0,false,false,&token};slots[1].address=8;calls=f.calls;
    assert(!fp_dispatch_begin(&lease,slots,3,read_slot,cas,&f,&token)&&f.calls==calls);slots[1].address=16;
    lease.counter=UINT64_MAX;assert(!fp_dispatch_begin(&lease,slots,3,read_slot,cas,&f,&token));
    puts("C dispatch leases: generation publication, partial failures, independent retries, stale tokens and successor preservation passed");return 0;
}
