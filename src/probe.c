/* Native read-only target census, built separately from the eventual runtime.
 * It intentionally has no write API, hooks, F11 handler or camera calls. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "hellforge_mod.h"
#include "goose_overlay.h"
#include "backend.h"
#include "profile.h"
static const HfApi *hf;
static HfMod *owner;
static const OvApi *ov;
static SRWLOCK lock=SRWLOCK_INIT;
static char status[256]="Waiting for gameplay";
static uint64_t next_tick,observations;
static bool sdk_read(void *unused,uintptr_t at,void *out,size_t size){(void)unused;return hf->read(at,out,size)==0;}
static void draw(const OvFrame *frame,void *unused){
    (void)frame;(void)unused;char copy[256];
    AcquireSRWLockShared(&lock);snprintf(copy,sizeof copy,"%s",status);ReleaseSRWLockShared(&lock);
    ov->heading("True First Person — target check");
    ov->text_wrapped("Read-only check for the C backport. It observes the native camera and weapon rig; it does not change the view or animations.");
    ov->text_wrapped(copy);
}
static uintptr_t object(const FpMemory *m,uint32_t ref){
    if(!ref||ref==UINT32_MAX)return 0;
    uintptr_t registry=0,units=0,generations=0,unit=0,vt=0,alive=0,visible=0;
    uint32_t count=0,again=0,index=ref&0x3fffff;uint8_t generation=0;
    if(!fp_read(m,m->exe+FP_REGISTRY,&registry,8)||!fp_read(m,registry+0x98,&count,4)||count>4194304||index>=count
        ||!fp_read(m,registry+0xa0,&generations,8)||!fp_read(m,generations+index,&generation,1)||generation!=(ref>>22)
        ||!fp_read(m,registry+0x88,&units,8)||!fp_read(m,units+(uintptr_t)index*8,&unit,8)
        ||!fp_read(m,unit,&vt,8)||!fp_read(m,vt+0xf8,&alive,8)||alive!=m->exe+0x2bd890
        ||!fp_read(m,vt+0xc8,&visible,8)||visible!=m->exe+0x2c3990
        ||!fp_read(m,unit+8,&again,4)||again!=ref)return 0;
    return unit;
}
static void graph(const FpMemory *m,uint32_t ref,const char *name){
    uintptr_t unit=object(m,ref),hashes=0,links=0;uint32_t count=0;
    if(!unit||!fp_read(m,unit+0x70,&count,4)||count>512||!count||!fp_read(m,unit+0xa0,&hashes,8)
        ||!fp_read(m,unit+0x98,&links,8)){hf->log(owner,"graph %s ref=%u unavailable",name,ref);return;}
    uint32_t h[512],l[512];uint8_t resource[8]={0};
    if(!fp_read(m,hashes,h,count*4)||!fp_read(m,links,l,count*4)||!fp_read(m,unit+0x358,resource,8))return;
    char text[17];for(unsigned i=0;i<8;i++)snprintf(text+i*2,3,"%02x",resource[i]);
    hf->log(owner,"graph %s ref=%u nodes=%u resource=%s",name,ref,count,text);
    for(uint32_t i=0;i<count;i++)if(h[i]==0x8c5570c9||h[i]==0x9b115563||h[i]==0x60ed8c39||i==13||i==34||i==8)
        hf->log(owner,"node %s index=%u hash=%08x link=%08x",name,i,h[i],l[i]);
}
static void tick(HfMod *mod,float dt,void *unused){
    (void)mod;(void)dt;(void)unused;
    uint64_t now=GetTickCount64();if(now<next_tick)return;next_tick=now+1000;
    size_t size=0;FpMemory memory={sdk_read,NULL,hf->module_base("game.dll",&size),hf->module_base("helldivers2.exe",&size),NULL};
    if(!fp_image(&memory,memory.game,FP_GAME_STAMP,FP_GAME_SIZE)||!fp_image(&memory,memory.exe,FP_EXE_STAMP,FP_EXE_SIZE))return;
    FpProof proof=fp_prove_profile(&memory);FpSnapshot s;char why[160];
    bool ready=fp_snapshot(&memory,true,&s,why,sizeof why);
    char text[256];snprintf(text,sizeof text,"Code %u/%u; %s",proof.matched,(unsigned)FP_WITNESS_COUNT,ready?"player and firearm verified":why);
    AcquireSRWLockExclusive(&lock);snprintf(status,sizeof status,"%s",text);ReleaseSRWLockExclusive(&lock);
    hf->log(owner,"census %llu: code=%u mismatch=%u unreadable=%u; %s; %s",(unsigned long long)++observations,proof.matched,proof.mismatched,proof.unreadable,proof.first,text);
    if(ready){
        hf->log(owner,"player avatar=%u body=%u weapon=%u weapon_unit=%u rig=%u camera=%u kind=%u mask=%u ADS=%u aim=%u seated=%u",
            s.avatar,s.body,s.weapon,s.weapon_unit,s.viewmodel,s.camera_unit,s.kind,s.mask,s.full_ads,s.shoulder_aim,s.seated);
        graph(&memory,s.body,"body");if(s.viewmodel&&s.viewmodel!=UINT32_MAX)graph(&memory,s.viewmodel,"fps");
    }
}
static void loaded(HfMod *mod,HfEvent event,void *unused){
    (void)event;(void)unused;ov=ov_connect();
    if(ov){OvPanelDef panel={0};panel.size=sizeof panel;panel.id="goose.true-first-person-probe";panel.title="True First Person target check";
        panel.kind=OV_PANEL_WINDOW;panel.width=560;panel.draw=draw;ov->register_panel(&panel);}
    if(!ov_on_game_thread(hf,mod,tick,NULL))hf->log(mod,"Read-only census could not register a game-thread callback");
}
__declspec(dllexport) int hellforge_mod_init(const HfApi *api,HfMod *mod){
    if(!api||api->version<16||strcmp(api->build_id(),"25480438"))return 1;
    hf=api;owner=mod;hf->log(mod,"C backport target census. READ ONLY: no writes, hooks or camera entry/exit calls.");
    return hf->on(mod,HF_EVENT_GAME_LOADED,loaded,NULL);
}
