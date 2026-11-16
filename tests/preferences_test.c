#include "fake_native.h"
#include "feature_contract.h"
#include "preferences.h"
static unsigned writes;static bool late_failure;
static bool store(void *unused,uintptr_t at,uint8_t expected,uint8_t value){(void)unused;uint8_t current=2;if(!fp_read(&(FpMemory){read_fake,&f,0,0},at,&current,1)||current!=expected)return false;put(at,&value,1);writes++;return !late_failure;}
static void seed(const FpMemory *m,FpFeature feature){
 for(unsigned i=0;i<sizeof fp_feature_sites/sizeof *fp_feature_sites;i++){const FpFeatureSite *s=fp_feature_sites+i;if(s->feature!=feature)continue;uint8_t b[128];unsigned n=0;for(;s->hex[n*2];n++){unsigned x;assert(sscanf(s->hex+n*2,"%2x",&x)==1);b[n]=(uint8_t)x;}put((s->exe?m->exe:m->game)+s->rva,b,n);}
}
int main(void){
 FpMemory m=native_setup();FpSnapshot s;assert(fp_snapshot(&m,true,&s,NULL,0));seed(&m,FP_FEATURE_AIM);seed(&m,FP_FEATURE_MOVEMENT);
 uintptr_t settings=0x1700000000ULL,selected=0x1700010000ULL;p64(s.avatar_owner+0x40,settings);n32(settings+0xf0,0);
 uint8_t descriptor[20]={0};uint32_t entity=901;memcpy(descriptor+8,&entity,4);put(selected,descriptor,20);p64(s.player+0xe8,selected);map(s.player+0xd0,0x1700020000ULL,entity,0);uint8_t off=0;put(s.player+0x2ec,&off,1);
 FpByteOwner a={0};assert(fp_aim_preference(&a,&m,true,store,NULL)&&a.held&&writes==1);assert(fp_aim_preference(&a,&m,true,store,NULL)&&writes==1);assert(fp_aim_preference(&a,&m,false,store,NULL)&&!a.held&&writes==2);
 uint8_t on=1;put(s.player+0x2ec,&on,1);assert(fp_aim_preference(&a,&m,true,store,NULL)&&!a.held);assert(fp_aim_preference(&a,&m,false,store,NULL)&&writes==2);
 n32(settings+0xf0,1);p64(0xa00000000ULL+0x50,0x1700030000ULL);put(0x1700030000ULL,&off,1);late_failure=true;assert(!fp_aim_preference(&a,&m,true,store,NULL)&&a.held);late_failure=false;assert(fp_aim_preference(&a,&m,false,store,NULL)&&!a.held);
 uintptr_t pool=0x1800000000ULL,rows=0x1800010000ULL;p64(m.game+0x3326cf8,pool);n32(pool+12,1);map(pool+0x20,pool+0x1000,s.avatar,0);p64(pool+0x38,pool+0x2000);p64(pool+0x2000,s.avatar_descriptor);p64(pool+0x50,rows);put(rows+0x94,&off,1);
 FpByteOwner movement={0};assert(fp_movement_preference(&movement,&m,true,store,NULL)&&movement.held);put(rows+0x94,&off,1);assert(fp_movement_preference(&movement,&m,true,store,NULL)&&!movement.held&&movement.draining);
 unsigned before=writes;assert(fp_movement_preference(&movement,&m,true,store,NULL)&&writes==before);assert(fp_movement_preference(&movement,&m,false,store,NULL));assert(fp_movement_preference(&movement,&m,true,store,NULL)&&movement.held);assert(fp_movement_preference(&movement,&m,false,store,NULL)&&!movement.held);
 puts("C preferences: native shared/per-weapon ADS, preserved existing preferences, uncertain writes, S-only cleanup and foreign changes passed");return 0;
}
