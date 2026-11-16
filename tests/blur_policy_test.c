#define fp_slot_read fixture_read
#define fp_slot_cas fixture_cas
#include "../src/blur_policy.c"
#include "fake_native.h"
#include <assert.h>
static FpBlurPolicy *current;static float received;static unsigned writes;
bool fixture_read(void *u,uintptr_t at,uintptr_t *out){(void)u;return fp_pointer(&current->memory,at,out);}
FpCasResult fixture_cas(void *u,uintptr_t at,uintptr_t expected,uintptr_t value,uintptr_t *observed){(void)u;if(!fp_read(&current->memory,at,observed,8))return FP_CAS_UNCERTAIN;if(*observed!=expected)return FP_CAS_DIFFERENT;p64(at,value);return FP_CAS_CHANGED;}
static void collect(void *object,uint64_t instance,uint32_t hash,const void *variant){
 memcpy(&received,(const uint8_t *)variant+8,4);writes++;
 if((uintptr_t)object==current->provider&&(uint32_t)instance==777)for(unsigned k=0;k<3;k++)if(hash==hashes[k])put(current->sources[0].fields[k]+8,&received,4);
}
int main(void){
 FpMemory m=native_setup();n32(0x300000000ULL+0x200,6);FpSnapshot s;assert(fp_snapshot(&m,true,&s,NULL,0));
 uintptr_t system=0x2600000000ULL,object=0x2700000000ULL,apis=0x2800000000ULL,hash_api=0x2800010000ULL;
 p64(s.player+0x18,system);p64(system+0xdc8,object);p64(object,m.exe+0x1677dc8);p64(m.exe+0x1677dc8+0x38,m.exe+0x62adb0);p64(m.exe+0x1677dc8+0x28,m.exe+0x2dd5e0);
 p64(system+0xdd8,m.exe+0x27cc5e0);p64(m.exe+0x27cc5e0+0x28,m.exe+0x30ffc0);p64(system+0xde8,m.exe+0x27cc638);p64(m.exe+0x27cc638+8,m.exe+0x3100c0);
 p64(m.game+0x3326308,apis);p64(apis+0xc0,hash_api);p64(hash_api+0x28,m.exe+0x310140);
 uintptr_t ids=object+0x1000,classes=object+0x2000,chains=object+0x3000,blobs=object+0x4000,buckets=object+0x5000,blob=object+0x6000;
 p64(object+0xc8,ids);p64(object+0xd0,classes);p64(object+0xd8,chains);p64(object+0xe8,blobs);p64(object+0x98,buckets);n32(object+0x90,4);n32(object+0x94,4);n32(object+0xa8,1);n32(object+0xac,4);n32(object+0xb8,2);n32(object+0xbc,2);
 uint32_t map_rows[12]={0};for(unsigned i=0;i<4;i++)map_rows[i*3+2]=0xfffffffe;map_rows[3]=777;map_rows[4]=1;map_rows[5]=0x7fffffff;put(buckets,map_rows,sizeof map_rows);n32(ids+4,777);n32(classes+4,FP_DOF_CLASS);n32(chains+4,0);p64(blobs+8,blob);n32(blob+4,3);put(blob+16,hashes,sizeof hashes);
 for(unsigned k=0;k<3;k++){uint8_t field[16]={2};float v=.6f+(float)k;memcpy(field+8,&v,4);put(blob+28+k*16,field,16);}
 uintptr_t pool=0x2a00000000ULL;p64(m.game+0x3326a38,pool);map(pool+0x278,pool+0x1000,s.weapon,0);p64(pool+0x290,pool+0x2000);p64(pool+0x298,pool+0x3000);p64(pool+0x2000,pool+0x4000);uint8_t descriptor[24]={0};memcpy(descriptor,s.weapon_bytes,20);descriptor[20]=1;put(pool+0x4000,descriptor,24);uint32_t indices[2]={0,UINT32_MAX};put(pool+0x3000,indices,8);n32(system+0x348,777);
 FpBlurPolicy p;fp_blur_policy_init(&p);p.backing=m;p.memory=m;p.memory.read=tracked_read;p.memory.user=&p;p.origin=s;current=&p;assert(sources(&p,true)==1&&p.count==1);
 p.token=42;p.lease.generation=42;p.lease.count=1;p.lease.slots[0]=(FpDispatchSlot){p.table,m.exe+0x310050,(uintptr_t)property_setter,true};p64(p.table,(uintptr_t)property_setter);atomic_store(&p.lease.armed,true);atomic_store(&active,&p);atomic_store(&original_setter,collect);
 _Alignas(16) uint8_t variant[24]={2};float native=1.7f;memcpy(variant+8,&native,4);live_begin=(uintptr_t)variant;live_end=live_begin+24;
 uint64_t instance=((uint64_t)FP_DOF_CLASS<<32)|777;property_call(m.game+0x100a80e,(void *)object,instance,hashes[0],variant);assert(received==0&&p.sources[0].owned[0]&&p.sources[0].native[0]==native);
 property_call(m.game+0x100a80f,(void *)object,instance,hashes[0],variant);assert(received==native);
 property_call(m.game+0x100a80e,(void *)object,instance,hashes[0],variant);assert(received==0);
 p64(p.table,m.exe+0x123456);property_call(m.game+0x100a80e,(void *)object,instance,hashes[0],variant);assert(received==native);p64(p.table,(uintptr_t)property_setter);
 property_call(m.game+0x100a80e,(void *)object,instance,hashes[0],variant);f.denied=p.sources[0].fields[0]+8;assert(!fp_blur_policy_close(&p)&&p.token==42&&p.sources[0].owned[0]);f.denied=0;
 assert(fp_blur_policy_close(&p)&&p.token==0&&received==native);assert(writes>=6);
 puts("C scoped blur: exact weapon component/variant, caller filter, foreign dispatch preservation, zero maxima and acknowledged deferred restoration passed");return 0;
}
