#define map depth_map
#include "../src/depth.c"
#undef map
#include "fake_native.h"
static void scalar(uintptr_t at,float v){put(at,&v,4);}
int main(void){
 FpMemory m=native_setup();FpSnapshot s;assert(fp_snapshot(&m,true,&s,NULL,0));
 uintptr_t pool=0xa00000000ULL,parameters=0x1600000000ULL,packed=0x1600010000ULL,definitions=0x1600020000ULL,settings=0x1600030000ULL;
 uint8_t header[0x88]={0};put(pool+0x30,header,sizeof header);map(pool+0x30,pool+0x200,s.weapon,0);p64(pool+0x48,pool+0x300);p64(pool+0x300,s.weapon_descriptor);
 p64(pool+0x58,parameters);p64(pool+0x60,packed);map(pool+0x70,pool+0x500,s.weapon,1);p64(pool+0xb0,definitions);
 uint32_t params[4]={1,0,0,0};put(parameters+0x350,params,16);n32(packed+4,2<<4);p64(m.game+0x3326340,settings);scalar(settings+0xac408,90);
 uint8_t values[0xdc]={0};float zoom[]={3.5f,2.5f,1},coefficient[]={1,1,1};memcpy(values,zoom,12);memcpy(values+0xc8,coefficient,12);put(definitions+0x4d0+0x74,values,sizeof values);
 FpDepthData d;uint32_t selected=0;assert(data(&m,&s,&d,&selected)&&selected==2&&d.data==definitions+0x4d0);
 float reference=0;assert(fp_depth_reference(&d,selected,.3f,&reference)&&reference==.3f);
 n32(packed+4,0);assert(data(&m,&s,&d,&selected)&&selected==0);assert(fp_depth_reference(&d,selected,.3f,&reference)&&reference<.3f&&reference>0);
 n32(s.request,6);assert(fp_snapshot(&m,true,&s,NULL,0));uintptr_t stack=0x1600040000ULL;uint8_t state[0x24]={0},frame[0x20]={0};float lens=.5f,current=.3f;
 memcpy(state+4,&lens,4);memcpy(state+0x10,&current,4);memcpy(frame+0x18,&lens,4);memcpy(frame+0x1c,&lens,4);put(s.request+0x80,state,sizeof state);put(stack+0x44,frame,sizeof frame);
 FpDepth depth={.checked=true,.code_ok=true};fp_depth_observe(&depth,&m,s.request,stack,42,7);
 float delta=fp_depth_delta(&depth,&m,&s,stack,42,7,1000);assert(delta<0&&fabsf(delta-(reference-current))<.000001f&&depth.carrier_ready);
 fp_depth_observe(&depth,&m,s.request,stack,42,7);assert(fp_depth_delta(&depth,&m,&s,stack+8,42,7,1001)==0&&!depth.carrier_ready);
 fp_depth_observe(&depth,&m,s.request,stack,42,7);s.full_ads=true;assert(fp_depth_delta(&depth,&m,&s,stack,42,7,1002)==0);
 s.full_ads=false;scalar(definitions+0x4d0+0x74+0xc8,2);fp_depth_observe(&depth,&m,s.request,stack,42,7);assert(fp_depth_delta(&depth,&m,&s,stack,42,7,2000)==0&&!depth.valid);
 assert(depth.retry_at==2500);
 scalar(definitions+0x4d0+0x74+0xc8,1);
 fp_depth_observe(&depth,&m,s.request,stack,42,7);assert(fp_depth_delta(&depth,&m,&s,stack,42,7,2499)==0&&!depth.valid);
 fp_depth_observe(&depth,&m,s.request,stack,42,7);assert(fabsf(fp_depth_delta(&depth,&m,&s,stack,42,7,2500)-delta)<.000001f&&depth.valid);
 f.denied=definitions+0x4d0+0x74;
 fp_depth_observe(&depth,&m,s.request,stack,42,7);assert(fp_depth_delta(&depth,&m,&s,stack,42,7,3000)==0&&!depth.valid&&depth.retry_at==3500);
 f.denied=0;
 fp_depth_observe(&depth,&m,s.request,stack,42,7);assert(fp_depth_delta(&depth,&m,&s,stack,42,7,3499)==0&&!depth.valid);
 fp_depth_observe(&depth,&m,s.request,stack,42,7);assert(fabsf(fp_depth_delta(&depth,&m,&s,stack,42,7,3500)-delta)<.000001f&&depth.valid);
 scalar(definitions+0x4d0+0x74+0xc8,NAN);
 fp_depth_observe(&depth,&m,s.request,stack,42,7);assert(fp_depth_delta(&depth,&m,&s,stack,42,7,4000)==0&&!depth.valid);
 fp_depth_observe(&depth,&m,s.request,stack,42,7);assert(fp_depth_delta(&depth,&m,&s,stack,42,7,4500)==0&&!depth.valid);
 scalar(definitions+0x4d0+0x74+0xc8,1);
 fp_depth_observe(&depth,&m,s.request,stack,42,7);assert(fabsf(fp_depth_delta(&depth,&m,&s,stack,42,7,5000)-delta)<.000001f&&depth.valid);
 assert(retry_time(UINT64_MAX-100)==UINT64_MAX&&retry_time(1000)==1500);
 puts("C weapon depth: original layout/reference math, same-invocation/ADS guards, stale/transient data refusal, bounded fresh recovery and invalid-definition rejection passed");return 0;
}
