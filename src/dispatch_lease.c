#include "dispatch_lease.h"
#include <string.h>
void fp_dispatch_init(FpDispatchLease *s){memset(s,0,sizeof *s);atomic_init(&s->armed,false);}
bool fp_dispatch_owns(const FpDispatchLease *s,uint64_t token){return s&&token&&s->generation==token;}
bool fp_dispatch_end(FpDispatchLease *s,uint64_t token,FpSlotCas cas,void *user){
    if(!s||!token||!cas)return false;
    if(!s->generation)return s->retired==token;
    if(s->generation!=token)return false;
    s->armed=false;bool clean=true;
    for(unsigned i=0;i<s->count;i++){
        FpDispatchSlot *slot=s->slots+i;if(!slot->possible)continue;
        uintptr_t observed=0;FpCasResult r=cas(user,slot->address,slot->replacement,slot->original,&observed);
        if(r==FP_CAS_CHANGED||(r==FP_CAS_DIFFERENT&&observed!=slot->replacement))slot->possible=false;
        else clean=false;
    }
    if(clean){s->retired=token;s->generation=0;s->count=0;}
    return clean;
}
unsigned fp_dispatch_begin(FpDispatchLease *s,const FpDispatchSlot *slots,unsigned count,FpSlotRead read,FpSlotCas cas,void *user,uint64_t *token){
    if(!s||s->generation)return 0;
    if(token)*token=0;
    if(!slots||!count||count>FP_DISPATCH_LIMIT||!read||!cas||!token)return 0;
    for(unsigned i=0;i<count;i++){
        uintptr_t actual=0;
        if(!slots[i].address||!slots[i].original||!slots[i].replacement||slots[i].original==slots[i].replacement
            ||slots[i].address%sizeof(uintptr_t)||!read(user,slots[i].address,&actual)||actual!=slots[i].original)return 0;
        for(unsigned j=0;j<i;j++)if(slots[j].address==slots[i].address)return 0;
    }
    uint64_t next=s->counter+1;if(!next)return 0; /* Never recycle opaque tokens. */
    s->counter=s->generation=next;s->count=count;s->armed=false;
    for(unsigned i=0;i<count;i++){s->slots[i]=slots[i];s->slots[i].possible=false;}
    *token=next;
    for(unsigned i=0;i<count;i++){
        FpDispatchSlot *slot=s->slots+i;uintptr_t observed=0;
        slot->possible=true;
        FpCasResult r=cas(user,slot->address,slot->original,slot->replacement,&observed);
        if(r==FP_CAS_CHANGED)continue;
        if(r==FP_CAS_DIFFERENT)slot->possible=false;
        if(fp_dispatch_end(s,next,cas,user)){*token=0;return 0;}
        return 2;
    }
    s->armed=true;return 1;
}
