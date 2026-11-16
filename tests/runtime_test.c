#include "fake_native.h"
#include "runtime.h"
#include <assert.h>
static unsigned enters,exits,camera_opens,near_configured,visibility_opens;static bool fail_lens,fail_visibility,models_wanted,effects_wanted,visibility_wanted;
static void arm(FpDispatchLease *l,unsigned index){l->count=1;l->slots[0]=(FpDispatchSlot){0x2900000000ULL+index*16,0,0x2900010000ULL+index*16,true};p64(l->slots[0].address,l->slots[0].replacement);atomic_store(&l->armed,true);}
#define INIT(name,type) void fp_##name##_policy_init(type *p){memset(p,0,sizeof *p);}
#define CLOSE(name,type) bool fp_##name##_policy_close(type *p){p->token=0;atomic_store(&p->lease.armed,false);return true;}
INIT(camera,FpCameraPolicy) INIT(pose,FpPosePolicy) INIT(fov,FpFovPolicy) INIT(near,FpNearPolicy) INIT(blur,FpBlurPolicy) INIT(visibility,FpVisibilityPolicy)
CLOSE(camera,FpCameraPolicy) CLOSE(pose,FpPosePolicy) CLOSE(fov,FpFovPolicy) CLOSE(near,FpNearPolicy) CLOSE(blur,FpBlurPolicy) CLOSE(visibility,FpVisibilityPolicy)
static unsigned handoffs;
bool fp_pose_policy_handoff(FpPosePolicy *p){handoffs++;return fp_pose_policy_close(p);}
bool fp_camera_policy_open(FpCameraPolicy *p,const FpMemory *m,const FpSnapshot *s,char *why,size_t n){(void)why;(void)n;p->memory=*m;p->origin=*s;p->token=1;arm(&p->lease,1);camera_opens++;return true;}
bool fp_fov_policy_open(FpFovPolicy *p,const FpMemory *m,const FpSnapshot *s,char *why,size_t n){(void)m;(void)s;(void)why;(void)n;p->token=1;arm(&p->lease,2);if(fail_lens){atomic_store(&p->lease.armed,false);return false;}return true;}
bool fp_near_policy_open(FpNearPolicy *p,const FpMemory *m,const FpSnapshot *s,char *why,size_t n){(void)m;(void)s;(void)why;(void)n;p->token=1;arm(&p->lease,3);return true;}
bool fp_pose_policy_open(FpPosePolicy *p,const FpMemory *m,const FpSnapshot *s,FpCameraPolicy *c,char *why,size_t n){(void)m;(void)c;(void)why;(void)n;p->origin=*s;p->request=s->context+0x200+s->tail*0xe8;p->token=1;arm(&p->lease,4);return true;}
bool fp_blur_policy_open(FpBlurPolicy *p,const FpMemory *m,const FpSnapshot *s,bool *absent,char *why,size_t n){(void)p;(void)m;(void)s;(void)why;(void)n;*absent=true;return true;}
bool fp_visibility_policy_open(FpVisibilityPolicy *p,const FpMemory *m,const FpSnapshot *s,char *why,size_t n){(void)m;(void)why;(void)n;assert(s->weapon_ready&&s->weapon_descriptor&&s->weapon_unit);if(fail_visibility)return false;p->token=1;arm(&p->lease,5);visibility_opens++;return true;}
bool fp_visibility_policy_update(FpVisibilityPolicy *p,uint64_t t,const FpSnapshot *s,bool wanted){(void)p;(void)t;(void)s;visibility_wanted=wanted;return true;}
bool fp_camera_policy_options(FpCameraPolicy *p,const FpEyeOptions *o){p->options=*o;return true;}
void fp_camera_policy_frame(FpCameraPolicy *p,uint64_t sequence,bool opening){p->sequence=sequence;p->opening=opening;}
void fp_camera_policy_actions(FpCameraPolicy *p,bool sprint){p->sprint=sprint;}
bool fp_camera_policy_free_aim(FpCameraPolicy *p,uint32_t n){p->free_until=n;return p->token!=0;}
bool fp_camera_policy_fallback_ready(const FpCameraPolicy *p){return atomic_load(&p->fallback_active);}
bool fp_near_policy_set(FpNearPolicy *p,uint64_t t,float hip,float aim){(void)t;p->hip=hip;p->aim=aim;near_configured++;return true;}
bool fp_pose_policy_place(FpPosePolicy *p,uint64_t t,const FpPlacement *hip,const FpPlacement *ads){(void)t;p->placement=*hip;p->ads=*ads;return true;}
void fp_effects_init(FpEffects *p){memset(p,0,sizeof *p);}void fp_models_init(FpModels *p){memset(p,0,sizeof *p);}
bool fp_effects_update(FpEffects *p,const FpMemory *m,bool wanted,uint64_t now){(void)p;(void)m;(void)now;effects_wanted=wanted;return true;}
bool fp_models_update(FpModels *p,const FpMemory *m,const FpSnapshot *s,bool wanted,uint64_t now){(void)p;(void)m;(void)s;(void)now;models_wanted=wanted;return true;}
bool fp_markers_native(void *u,uintptr_t w,bool v){(void)u;(void)w;(void)v;assert(false);return false;}
bool fp_private_byte(void *u,uintptr_t at,uint8_t before,uint8_t after){(void)u;(void)at;(void)before;(void)after;assert(false);return false;}
static void normal(const FpSnapshot *s){n32(s->context+0x1fc,s->head+1);n32(s->context+0x200+s->head*0xe8,4);}
static void complete_links(const FpMemory *m){
 FpSnapshot s;FpGraph rig,gun,camera;assert(fp_snapshot(m,true,&s,NULL,0));
 assert(fp_graph_rig(m,s.viewmodel,&rig)&&fp_graph(m,s.weapon_unit,&gun)&&fp_graph(m,s.camera_unit,&camera));
 n32(gun.links,0x80002);p64(gun.object+0x1d0,rig.object);p64(gun.object+0x78,rig.worlds);
 n32(camera.links,0x320002);p64(camera.object+0x1d0,rig.object);p64(camera.object+0x78,rig.worlds);
}
bool fp_native_platform(void *u,FpNativeOperation op,const FpMemory *m,const FpSnapshot *s,bool *allowed){
 (void)u;(void)m;if(op==FP_NATIVE_ALLOWED){*allowed=true;return true;}if(op==FP_NATIVE_ENTER){assert(near_configured&&visibility_opens);enters++;uint32_t request[5]={6,s->avatar,s->weapon,s->body,0};put(s->context+0x200+s->tail*0xe8,request,20);n32(s->context+0x1fc,(s->tail+1)%32);return true;}exits++;normal(s);return true;
}
int main(void){
 FpMemory m=native_setup();unit(&m,501,0x2400000000ULL,1,0x2500000000ULL);p64(0x2400000000ULL+0x358,UINT64_C(0x0102030405060708));FpSnapshot initial;assert(fp_snapshot(&m,true,&initial,NULL,0));
 for(unsigned i=0;i<FP_WITNESS_COUNT;i++){const FpWitness *w=fp_witnesses+i;uint8_t bytes[128];unsigned n=0;for(;w->hex[n*2];n++){unsigned x;assert(sscanf(w->hex+n*2,"%2x",&x)==1);bytes[n]=(uint8_t)x;}put(m.game+w->rva,bytes,n);}
 static FpSettings settings;static FpRuntime r;fp_settings_defaults(&settings);fp_runtime_init(&r,&m,&settings);
 fp_runtime_tick(&r,.016,1000,false,true,false);assert(!r.enabled&&fp_runtime_clean(&r)&&enters==0);
 fp_runtime_enable(&r,true);fp_runtime_tick(&r,.016,1100,false,true,false);assert(r.enabled&&r.native.held&&r.pose.token&&enters==1&&camera_opens==1);
 fp_runtime_tick(&r,.016,1110,false,true,false);assert(!models_wanted&&!effects_wanted&&!visibility_wanted);assert(r.clip.hip==.05f&&r.clip.aim==0);
 complete_links(&m);fp_runtime_tick(&r,.016,1120,false,true,false);assert(models_wanted&&effects_wanted&&visibility_wanted);
 uint32_t wheel_mask=64;put(initial.controller,&wheel_mask,4);fp_runtime_tick(&r,.016,1130,false,true,false);assert(models_wanted&&effects_wanted&&visibility_wanted);wheel_mask=0;put(initial.controller,&wheel_mask,4);fp_runtime_tick(&r,.016,1140,false,true,false);
 assert(fp_runtime_editor(&r,true));FpPlacement draft={.2,.1,-.1,1,2,3};assert(fp_runtime_draft(&r,FP_IDLE,&draft));assert(fp_runtime_editor(&r,false)&&r.camera.token&&r.pose.token);
 uint8_t controls[6]={0,0,0,0,1,0};put(initial.controller,controls,6);fp_runtime_tick(&r,.016,1200,false,true,false);assert(r.enabled&&r.native.held&&r.pose.token&&enters==1);
 controls[4]=0;put(initial.controller,controls,6);normal(&initial);fp_runtime_tick(&r,.016,1400,false,true,false);assert(r.enabled&&r.native.held&&r.pose.token&&enters==2&&camera_opens==1&&handoffs==1);
 fp_runtime_tick(&r,.016,1500,true,false,false);assert(r.enabled&&!r.camera.token&&r.native.held);fp_runtime_tick(&r,.016,1600,false,true,false);assert(r.enabled&&!r.native.held);fp_runtime_tick(&r,.016,1800,false,true,false);assert(r.camera.token&&r.pose.token&&enters==3);
 fp_runtime_enable(&r,false);fp_runtime_tick(&r,.016,2000,false,true,false);assert(fp_runtime_clean(&r)&&exits==2);
 fail_lens=true;fp_runtime_enable(&r,true);fp_runtime_tick(&r,.016,2200,false,true,false);assert(enters==3&&!r.native.held);fp_runtime_tick(&r,.016,2300,false,true,false);assert(fp_runtime_clean(&r)&&r.enabled);
 fail_lens=false;fp_runtime_tick(&r,.016,2500,false,true,false);assert(r.native.held&&enters==4);fp_runtime_enable(&r,false);fp_runtime_tick(&r,.016,2600,false,true,false);assert(fp_runtime_clean(&r));
 assert(fp_runtime_compare(&r,3000));fp_runtime_tick(&r,.016,33001,false,true,false);assert(!r.compare_until&&fp_runtime_clean(&r));
 unsigned before_enter=enters;fp_runtime_enable(&r,true);n32(initial.request,17);n32(initial.request+4,999);fp_runtime_tick(&r,.016,34000,false,true,false);assert(r.camera.token&&!r.native.held&&!r.pose.token&&enters==before_enter&&!models_wanted&&!effects_wanted);fp_runtime_enable(&r,false);fp_runtime_tick(&r,.016,34100,false,true,false);assert(fp_runtime_clean(&r));
 n32(initial.request,4);n32(initial.request+4,initial.avatar);p64(0x2400000000ULL+0x358,UINT64_C(0x5f3ec9bda2bd8553));fp_runtime_enable(&r,true);fp_runtime_tick(&r,.016,35000,false,true,false);assert(r.camera.token&&!r.pose.token&&enters==before_enter);fp_runtime_enable(&r,false);fp_runtime_tick(&r,.016,35100,false,true,false);assert(fp_runtime_clean(&r));
 f.denied=0x2400000000ULL+0x358;fp_runtime_enable(&r,true);fp_runtime_tick(&r,.016,36000,false,true,false);assert(r.native.held&&r.pose.token&&r.visibility.token&&r.weapon_key==0&&enters==before_enter+1);f.denied=0;fp_runtime_enable(&r,false);fp_runtime_tick(&r,.016,36100,false,true,false);assert(fp_runtime_clean(&r));
 p64(0x2400000000ULL+0x358,UINT64_C(0x0102030405060708));before_enter=enters;fail_visibility=true;fp_runtime_enable(&r,true);fp_runtime_tick(&r,.016,36500,false,true,false);assert(!r.visibility.token&&!r.pose.token&&!r.native.held&&enters==before_enter);fp_runtime_enable(&r,false);fp_runtime_tick(&r,.016,36600,false,true,false);assert(fp_runtime_clean(&r));fail_visibility=false;
 uint8_t witness_byte=0;assert(fp_read(&m,m.game+fp_witnesses[3].rva,&witness_byte,1));uint8_t unavailable_byte=(uint8_t)(witness_byte^0xff);put(m.game+fp_witnesses[3].rva,&unavailable_byte,1);visibility_opens=0;fp_runtime_enable(&r,true);fp_runtime_tick(&r,.016,37000,false,true,false);assert(r.warming&&r.native.held&&r.visibility.token&&!r.camera.token&&!r.pose.token&&visibility_opens==1&&enters==before_enter+1);put(m.game+fp_witnesses[3].rva,&witness_byte,1);fp_runtime_enable(&r,false);fp_runtime_tick(&r,.016,37100,false,true,false);assert(fp_runtime_clean(&r));
 puts("C runtime: disabled start, F11 lifecycle, F8 independence, native ADS re-entry, camera handoff, partial-install recovery and timed comparison passed");return 0;
}
