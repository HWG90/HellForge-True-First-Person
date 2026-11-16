/* Scope/weapon lens normalization at the same existing native FOV dispatch.
 * Native sight magnification is the fresh target throughout ADS. */
#include "fov_policy.h"
#include "profile.h"
#include "policy_owner.h"
#include "native_contract.h"
#include <intrin.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
typedef void (*LensSetter)(void *,float);
static _Atomic(FpFovPolicy *) active;
static _Atomic(LensSetter) native_setter;
static bool fail(char *out,size_t size,const char *why){if(out&&size)snprintf(out,size,"%s",why);return false;}
static double seconds(bool *ok){LARGE_INTEGER now,frequency;*ok=QueryPerformanceCounter(&now)&&QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0;return *ok?(double)now.QuadPart/(double)frequency.QuadPart:0;}
static bool owner(FpFovPolicy *p,FpSnapshot *s){return fp_policy_dispatch(&p->memory,&p->lease)&&fp_snapshot(&p->memory,false,s,NULL,0)&&fp_same_player(&p->origin,s);}
static void lens_call(uintptr_t caller,void *camera,float native){
    LensSetter original=atomic_load_explicit(&native_setter,memory_order_acquire);FpFovPolicy *p=atomic_load_explicit(&active,memory_order_acquire);
    if(!original)return;
    if(!p||!atomic_load_explicit(&p->lease.armed,memory_order_acquire)||(uintptr_t)camera!=p->origin.camera
        ||caller!=p->memory.game+0x127a120){original(camera,native);return;}
    AcquireSRWLockExclusive(&p->lock);float applied=native;FpSnapshot s;
    if(atomic_load_explicit(&p->lease.armed,memory_order_acquire)&&owner(p,&s)&&s.local_request
        &&(s.mask==0||s.mask==2)&&!s.controls[5]&&!s.seated){
        bool clock_ok=false;double now=seconds(&clock_ok);
        uint32_t request_body=0;
        bool native6=s.kind==6&&s.head!=s.tail&&fp_u32(&p->memory,s.request+12,&request_body)&&request_body==s.body;
        if(native6&&s.full_ads){
            p->lens.shoulder_weight=0;p->lens.shoulder_clock=false;p->lens.shoulder_target=false;
            applied=fp_lens_handoff(&p->lens,native,true,now,clock_ok);
        }else if(native6){
            uintptr_t settings=0;float degrees=0;
            if(fp_pointer(&p->memory,p->memory.game+0x3326340,&settings)&&fp_read(&p->memory,settings+0xac408,&degrees,4)
                &&isfinite(degrees)&&degrees>=1&&degrees<=179){
                applied=degrees*.01745329251994329577f;p->degrees=degrees;
                applied=fp_lens_shoulder(&p->lens,applied,s.shoulder_aim,now,clock_ok);
                applied=fp_lens_handoff(&p->lens,applied,false,now,clock_ok);
            }
        }else if(s.kind==4&&p->lens.ready&&(p->lens.ads||p->lens.blending)){
            p->lens.shoulder_weight=0;p->lens.shoulder_clock=false;p->lens.shoulder_target=false;
            applied=fp_lens_handoff(&p->lens,native,false,now,clock_ok);
        }
        FpSnapshot after;if(!owner(p,&after)||!fp_same_player(&s,&after)||after.request!=s.request||after.full_ads!=s.full_ads)applied=native;
    }
    p->native=native;p->applied=applied;p->seen++;if(applied!=native)p->changed++;
    original(camera,applied);ReleaseSRWLockExclusive(&p->lock);
}
static __declspec(noinline) void lens_setter(void *camera,float native){lens_call((uintptr_t)_ReturnAddress(),camera,native);}
void fp_fov_policy_init(FpFovPolicy *p){memset(p,0,sizeof *p);InitializeSRWLock(&p->lock);fp_dispatch_init(&p->lease);fp_lens_init(&p->lens,180);}
bool fp_fov_policy_open(FpFovPolicy *p,const FpMemory *m,const FpSnapshot *s,char *error,size_t size){
    if(!p||!m||!s||p->token||atomic_load(&active))return fail(error,size,"Camera lens owner is busy");
    if(!fp_image(m,m->game,FP_GAME_STAMP,FP_GAME_SIZE)||!fp_image(m,m->exe,FP_EXE_STAMP,FP_EXE_SIZE))return fail(error,size,"Camera lens build differs");
    for(unsigned i=0;i<FP_NATIVE_SITE_COUNT;i++){
        const FpNativeSite *site=fp_native_sites+i;
        if(!fp_bytes(m,(site->exe?m->exe:m->game)+site->rva,site->hex))return fail(error,size,site->name);
    }
    uintptr_t root=0,api=0,setter=0,getter=0;
    if(!fp_pointer(m,m->game+0x3326308,&root)||!fp_pointer(m,root+0x20,&api)
        ||!fp_pointer(m,api+0x80,&setter)||setter!=m->exe+0x30d990
        ||!fp_pointer(m,api+0x78,&getter)||getter!=m->exe+0x30d980)return fail(error,size,"Camera lens dispatch differs");
    HMODULE pinned=NULL;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)(uintptr_t)lens_setter,&pinned))return fail(error,size,"Cannot retain lens callback lifetime");
    p->memory=*m;p->origin=*s;p->slot=api+0x80;p->platform=(FpSlotPlatform){m->exe,GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version")!=NULL};
    fp_lens_init(&p->lens,180);atomic_store_explicit(&native_setter,(LensSetter)setter,memory_order_release);atomic_store_explicit(&active,p,memory_order_release);
    FpDispatchSlot slot={p->slot,setter,(uintptr_t)lens_setter,false};unsigned result=fp_dispatch_begin(&p->lease,&slot,1,fp_slot_read,fp_slot_cas,&p->platform,&p->token);
    if(!result)atomic_store(&active,NULL);
    return result==1?true:fail(error,size,result==2?"Camera lens restoration pending":"Camera lens slot changed");
}
bool fp_fov_policy_close(FpFovPolicy *p){
    if(!p||!p->token)return true;
    AcquireSRWLockExclusive(&p->lock);bool done=fp_dispatch_end(&p->lease,p->token,fp_slot_cas,&p->platform);
    if(done){p->token=0;if(atomic_load(&active)==p)atomic_store(&active,NULL);}
    ReleaseSRWLockExclusive(&p->lock);return done;
}
