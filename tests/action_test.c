#include "fake_native.h"
#include "action.h"
int main(void){
 FpMemory m=native_setup();FpSnapshot s;assert(fp_snapshot(&m,true,&s,NULL,0));uintptr_t body=fp_unit_object(&m,s.body),vt=0;assert(fp_pointer(&m,body,&vt));p64(vt+0x1b0,m.exe+0x2bd9c0);
 const uint8_t code[]={0x48,0x8b,0x81,0x78,1,0,0,0xc3};put(m.exe+0x2bd9c0,code,8);p64(body+0x358,UINT64_C(0x4d1c334d294dfa97));
 uintptr_t instance=0x1700000000ULL,graph=0x1700010000ULL,active=0x1700020000ULL;p64(body+0x178,instance);p64(instance+0x28,graph);p64(instance+0x50,active);
 uint32_t header[]={0,31,12};put(graph,header,12);uint32_t definitions[32]={0};definitions[0]=0x1000;definitions[2]=0x2000;definitions[13]=0x3000;put(graph+16,definitions,31*4);
 unsigned layers[]={1,3,14},states[]={78,0,36};for(unsigned n=0;n<3;n++){
  uintptr_t def=graph+12+definitions[layers[n]-1];n32(def+8,100);uint32_t rows[100];for(unsigned i=0;i<100;i++)rows[i]=0x400+i*16;put(def+12,rows,sizeof rows);p64(active+(layers[n]-1)*8,def+rows[states[n]]);
 }
 FpActionReader reader={0};FpActions result;assert(fp_action_read(&reader,&m,&s,&result)&&result.verified&&result.sprint&&result.on_foot&&result.selected==FP_SPRINT);
 s.flags[9]=32;assert(fp_action_read(&reader,&m,&s,&result)&&result.selected==FP_SPRINT);p64(active,graph+12+definitions[0]+0x400);assert(fp_action_read(&reader,&m,&s,&result)&&result.selected==FP_CROUCH);
 s.full_ads=true;assert(fp_action_read(&reader,&m,&s,&result)&&result.selected==FP_ADS);s.full_ads=false;
 p64(active+13*8,0x777777777ULL);assert(!fp_action_read(&reader,&m,&s,&result));p64(body+0x358,0);assert(!fp_action_read(&reader,&m,&s,&result));
 puts("C actions: exact resource, copied animation states, sprint/on-foot gate, posture and ADS priority, unknown-state/resource refusal passed");return 0;
}
