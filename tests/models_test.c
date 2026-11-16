#include "fake_native.h"
#include "feature_contract.h"
#include "models.h"
#include <stdarg.h>
#include <assert.h>
static FpMemory memory;static unsigned sdk_writes;static bool late_error;
static uint32_t inventory[40]={401,801,501,601,901,1001};
static int inventory_count=6;static uintptr_t unit_tag=1;
static bool cache_on_write,fail_redraw_once;
static uint32_t fail_redraw_ref;static unsigned fail_redraw_index;
static uintptr_t deny_after_redraw;static unsigned redraw_writes;
static bool action_on_write;
static uintptr_t hide_on_write;
static void whole_hide(uintptr_t object);
static void refresh_cache(uintptr_t object){
 uint32_t count=0,meshes=0;uintptr_t cache=0,array=0;
 if(!cache_on_write||!fp_u32(&memory,object+0xf8,&count)||!count)return;
 assert(count<=128&&fp_u32(&memory,object+0xc8,&meshes)&&count==meshes);
 assert(fp_pointer(&memory,object+0x100,&cache)&&fp_pointer(&memory,object+0xd0,&array));
 uint8_t bytes[128],current[128],all=0;assert(fp_read(&memory,cache,bytes,count));
 for(unsigned i=0;i<count;i++){uintptr_t mesh=0;assert(fp_pointer(&memory,array+i*16,&mesh));assert(fp_read(&memory,mesh+0xdc,current+i,1));all|=current[i];}
 for(unsigned i=0;i<count;i++)bytes[i]=(uint8_t)((bytes[i]&~all)|current[i]);
 put(cache,bytes,count);
}
bool eng_ready(void){return true;}uint64_t eng_state_generation(void){return 1;}const char *eng_error(void){return "fixture";}
uintptr_t eng_cfunction(const char *path){return !strcmp(path,"Unit.set_mesh_visibility")?memory.exe+0x4128e0:0;}
void eng_unref(EngRef *ref){*ref=0;}EngRef eng_copy(EngRef ref){return ref;}bool eng_same(EngRef a,EngRef b){return a==b;}
uintptr_t eng_pointer(EngRef ref){if(ref==1)return 0x1900000000ULL;return ref>=1000?((uintptr_t)(ref-1000)<<2)|unit_tag:0;}
int eng_len(EngRef ref){return ref==2?inventory_count:0;}bool eng_index(EngRef table,int at,EngRef *out){if(table!=2||at<1||at>inventory_count)return false;*out=(int)inventory[at-1]+1000;return true;}
bool eng_call(const char *path,const char *sig,...){
 (void)sig;va_list args;va_start(args,sig);bool ok=true;
 if(!strcmp(path,"Application.main_world")){*va_arg(args,EngRef *)=1;}
 else if(!strcmp(path,"World.units")){assert(va_arg(args,EngRef)==1);*va_arg(args,EngRef *)=2;}
 else if(!strcmp(path,"Unit.has_mesh")){(void)va_arg(args,EngRef);(void)va_arg(args,const char *);*va_arg(args,int *)=0;}
 else if(!strcmp(path,"Unit.set_mesh_visibility")){
  EngRef handle=va_arg(args,EngRef);int index=va_arg(args,int),shown=va_arg(args,int);const char *pass=va_arg(args,const char *);assert(handle>=1000);
  uintptr_t object=fp_unit_object(&memory,(uint32_t)(handle-1000)),array=0,mesh=0;assert(object&&index>0);assert(fp_pointer(&memory,object+0xd0,&array));assert(fp_pointer(&memory,array+(uintptr_t)(index-1)*16,&mesh));
  uint8_t before=0,bit=!strcmp(pass,"default")?1:2;assert(!strcmp(pass,"default")||!strcmp(pass,"shadow_caster"));assert(fp_read(&memory,mesh+0xdc,&before,1));
  uint8_t after=shown?(uint8_t)(before|bit):(uint8_t)(before&~bit);put(mesh+0xdc,&after,1);sdk_writes++;refresh_cache(object);ok=!late_error;
  if(action_on_write){uint8_t value=1;put(0x500000005ULL,&value,1);action_on_write=false;}
  if(hide_on_write){uintptr_t hidden=hide_on_write;hide_on_write=0;whole_hide(hidden);}
  if(bit==1&&shown){redraw_writes++;if(fail_redraw_once&&(uint32_t)(handle-1000)==fail_redraw_ref&&(unsigned)index==fail_redraw_index){fail_redraw_once=false;ok=false;}if(deny_after_redraw){f.denied=deny_after_redraw;deny_after_redraw=0;}}
 }else {fprintf(stderr,"Unknown SDK call %s\n",path);assert(false);}
 va_end(args);return ok;
}
static void seed(void){for(unsigned i=0;i<sizeof fp_feature_sites/sizeof *fp_feature_sites;i++){
 const FpFeatureSite *s=fp_feature_sites+i;if(s->feature!=FP_FEATURE_MESH)continue;uint8_t b[128];unsigned n=0;for(;s->hex[n*2];n++){unsigned x;assert(sscanf(s->hex+n*2,"%2x",&x)==1);b[n]=(uint8_t)x;}put((s->exe?memory.exe:memory.game)+s->rva,b,n);
}}
static void meshes(uintptr_t object,uintptr_t array,const uintptr_t *p,unsigned count){n32(object+0xc8,count);p64(object+0xd0,array);for(unsigned i=0;i<count;i++){p64(array+i*16,p[i]);uint8_t mask=255;put(p[i]+0xdc,&mask,1);}}
static uint8_t mask(uintptr_t mesh){uint8_t m=0;assert(fp_read(&memory,mesh+0xdc,&m,1));return m;}
static void inventory_part(uint32_t ref,uintptr_t object,uintptr_t parent,uintptr_t mesh){
 uint8_t bytes[8192]={0};uintptr_t registry=0,units=0,vt=0;
 assert(fp_pointer(&memory,memory.exe+FP_REGISTRY,&registry));assert(fp_pointer(&memory,registry+0x88,&units));assert(fp_pointer(&memory,parent,&vt));
 put(object,bytes,sizeof bytes);p64(object,vt);n32(object+8,ref);p64(object+0x18,0x400000000ULL);p64(object+0x1d0,parent);p64(units+(uintptr_t)ref*8,object);
 meshes(object,object+0x1000,&mesh,1);
}
static void cached_redraw_tests(void);
static void subtree_bound_test(void);
int main(void){
 memory=native_setup();seed();FpSnapshot s;assert(fp_snapshot(&memory,true,&s,NULL,0));
 uintptr_t body=fp_unit_object(&memory,s.body),rig=fp_unit_object(&memory,s.viewmodel),world=0x400000000ULL;
 p64(body+0x1d0,0);p64(rig+0x1d0,0);p64(0x1900000000ULL,world);
 unit(&memory,501,0x1a00000000ULL,1,0x1b00000000ULL);unit(&memory,601,0x1c00000000ULL,1,0x1d00000000ULL);unit(&memory,901,0x1e00000000ULL,1,0x1f00000000ULL);unit(&memory,1001,0x2000000000ULL,1,0x2100000000ULL);
 p64(0x1a00000000ULL+0x1d0,rig);p64(0x1a00000000ULL+0x1d8,0x2000000000ULL);p64(0x2000000000ULL+0x1d0,0x1a00000000ULL);p64(0x2000000000ULL+0x1d8,0);p64(0x2000000000ULL+0x1e0,0);
 p64(0x1c00000000ULL+0x1d0,rig);p64(0x1e00000000ULL+0x1d0,body);
 uintptr_t base=0x2200000000ULL,body_mesh[]={base,base+0x1000},rig_mesh[]={base+0x2000,base+0x3000,base+0x4000,base+0x5000,base+0x6000};
 meshes(body,base+0x10000,body_mesh,2);meshes(rig,base+0x11000,rig_mesh,5);
 uintptr_t gun_mesh[]={base+0x7000},arm_mesh[]={base+0x8000},child_mesh[]={base+0x9000};meshes(0x1a00000000ULL,base+0x12000,gun_mesh,1);meshes(0x1c00000000ULL,base+0x13000,arm_mesh,1);meshes(0x2000000000ULL,base+0x14000,child_mesh,1);meshes(0x1e00000000ULL,base+0x15000,body_mesh+1,1);
 uint32_t appearance[30]={0};appearance[3]=901;appearance[6]=601;put(s.avatar_owner+0x546a70+0xdc,appearance,sizeof appearance);
 static FpModels models;fp_models_init(&models);assert(fp_models_update(&models,&memory,&s,true,1000));
 assert(mask(body_mesh[0])==254&&mask(body_mesh[1])==255);assert(mask(gun_mesh[0])==253&&mask(arm_mesh[0])==253&&mask(child_mesh[0])==253);assert(mask(rig_mesh[0])==253);
 unsigned before=sdk_writes;assert(fp_models_update(&models,&memory,&s,true,1001)&&sdk_writes==before);
 f.denied=body+0xc8;assert(!fp_models_update(&models,&memory,NULL,false,1002)&&models.count>0);f.denied=0;
 assert(fp_models_update(&models,&memory,NULL,false,1003)&&models.count==0);assert(mask(body_mesh[0])==255&&mask(gun_mesh[0])==255&&mask(child_mesh[0])==255);
 late_error=true;assert(!fp_models_update(&models,&memory,&s,true,2000));late_error=false;assert(fp_models_update(&models,&memory,NULL,false,2001)&&models.count==0&&mask(body_mesh[0])==255);
 /* Match the observed 15-target inventory, including an actual held root mesh.
  * These are fake SDK-owned references; product code never fabricates userdata. */
 const unsigned hidden[]={0,2,8,9};
 uintptr_t extra_meshes[10];
 uintptr_t registry=0,units=0;uint64_t extra_units[10]={0};
 assert(fp_pointer(&memory,memory.exe+FP_REGISTRY,&registry));assert(fp_pointer(&memory,registry+0x88,&units));
 put(units+610*8,extra_units,sizeof extra_units);
 for(unsigned i=0;i<10;i++){
  uint32_t ref=610+i;uintptr_t object=0x2500000000ULL+(uintptr_t)i*0x10000;
  extra_meshes[i]=object+0x1800;inventory_part(ref,object,body,extra_meshes[i]);
  inventory[6+i]=ref;appearance[(i/4)*10+hidden[i%4]]=ref;
 }
 inventory_count=16;put(s.avatar_owner+0x546a70+0xdc,appearance,sizeof appearance);
 unsigned clean_writes=sdk_writes;
 const uintptr_t bad_tags[]={0,2,3};
 for(unsigned i=0;i<sizeof bad_tags/sizeof *bad_tags;i++){
  unit_tag=bad_tags[i];
  assert(!fp_models_update(&models,&memory,&s,true,3000+i*1000));
  assert(models.target_count==15&&models.count==0&&sdk_writes==clean_writes);
  for(int j=0;j<models.target_count;j++)assert(models.targets[j].handle==0);
  assert(strstr(models.status,"15 model targets unresolved")!=NULL);
  assert(fp_models_update(&models,&memory,NULL,false,3500+i*1000));
 }
 unit_tag=1;
 assert(fp_models_update(&models,&memory,&s,true,6000));
 assert(models.target_count==15&&models.count==20);
 for(int i=0;i<models.target_count;i++){
  FpModelTarget *target=models.targets+i;
  assert(target->handle==(EngRef)(target->ref+1000));
  assert((eng_pointer(target->handle)&3)==1);
  assert((eng_pointer(target->handle)>>2)==target->ref);
 }
 bool root_collected=false;
 for(int i=0;i<models.count;i++)if(models.rows[i].ref==s.weapon_unit&&models.rows[i].mesh==gun_mesh[0])root_collected=true;
 assert(root_collected&&mask(gun_mesh[0])==253&&mask(child_mesh[0])==253);
 for(unsigned i=0;i<10;i++)assert(mask(extra_meshes[i])==254);
 unsigned held_writes=sdk_writes;assert(fp_models_update(&models,&memory,&s,true,6001)&&sdk_writes==held_writes);
 assert(fp_models_update(&models,&memory,NULL,false,6002)&&models.count==0);
 assert(mask(gun_mesh[0])==255&&mask(child_mesh[0])==255);
 for(unsigned i=0;i<10;i++)assert(mask(extra_meshes[i])==255);
 cached_redraw_tests();
 subtree_bound_test();
 puts("C model passes: SDK tag1/bad-tag inventory, all15 target handles/20 meshes, full cached variant redraw for root/children, repeated hide, partial SDK/read recovery, successor/array/cache/Unit/role retirement, body restore and shadow leases passed");return 0;
}
static unsigned active_repairs(const FpModels *p){unsigned n=0;for(unsigned i=0;i<FP_MODEL_REPAIRS;i++)n+=p->repairs[i].active;return n;}
static void mesh_pattern(uintptr_t object,const uint8_t *current,const uint8_t *cached,unsigned count){
 uintptr_t array=0,cache=0;uint32_t meshes_count=0;
 assert(fp_u32(&memory,object+0xc8,&meshes_count)&&meshes_count==count);
 assert(fp_pointer(&memory,object+0xd0,&array)&&fp_pointer(&memory,object+0x100,&cache));
 for(unsigned i=0;i<count;i++){uintptr_t mesh=0;assert(fp_pointer(&memory,array+i*16,&mesh));put(mesh+0xdc,current+i,1);}
 put(cache,cached,count);
}
static void whole_hide(uintptr_t object){
 uint32_t count=0;uintptr_t array=0;assert(fp_u32(&memory,object+0xc8,&count)&&count<=128);assert(fp_pointer(&memory,object+0xd0,&array));
 for(unsigned i=0;i<count;i++){uintptr_t mesh=0;uint8_t zero=0;assert(fp_pointer(&memory,array+i*16,&mesh));put(mesh+0xdc,&zero,1);}
 refresh_cache(object); /* G==0 retains the authored cache. */
}
static void select_weapon(uint32_t entity){
 uint32_t wd[6]={33,44,entity,501,601,1};
 map(0x400000000ULL+FP_ENTITY_MAP,0x400000200ULL,entity,1);put(0x400000000ULL+FP_ENTITY_ROWS+24,wd,sizeof wd);
 map(0xa00000000ULL+0x30,0xa00000200ULL,entity,0);put(0xa00000400ULL,wd,20);
 n32(0x900000400ULL,entity);n32(0x300000208ULL,entity);
}
static void cached_redraw_tests(void){
 memory=native_setup();seed();inventory_count=4;inventory[0]=401;inventory[1]=801;inventory[2]=501;inventory[3]=1001;unit_tag=1;
 FpSnapshot s;assert(fp_snapshot(&memory,true,&s,NULL,0));
 uintptr_t body=fp_unit_object(&memory,s.body),rig=fp_unit_object(&memory,s.viewmodel),gun=fp_unit_object(&memory,s.weapon_unit);
 const uintptr_t arena=0x2600000000ULL,child=0x2700000000ULL;
 uint8_t arena_bytes[8192]={0};put(arena,arena_bytes,sizeof arena_bytes);
 p64(0x1900000000ULL,0x400000000ULL);p64(body+0x1d0,0);p64(rig+0x1d0,0);p64(gun+0x1d0,rig);
 inventory_part(1001,child,gun,child+0x1800);p64(gun+0x1d8,child);p64(child+0x1d8,0);p64(child+0x1e0,0);
 uintptr_t root_mesh[]={arena+0x500,arena+0x600,arena+0x700},child_mesh[]={arena+0xb00,arena+0xc00},body_mesh=arena+0xf00;
 meshes(body,arena+0x1100,&body_mesh,1);meshes(rig,arena+0x1200,NULL,0);meshes(gun,arena+0x100,root_mesh,3);meshes(child,arena+0x900,child_mesh,2);
 n32(gun+0xf8,3);p64(gun+0x100,arena+0x300);n32(child+0xf8,2);p64(child+0x100,arena+0xa00);
 uint32_t appearance[30]={0};put(s.avatar_owner+0x546a70+0xdc,appearance,sizeof appearance);
 static FpModels p;fp_models_init(&p);cache_on_write=true;
 const uint8_t zero3[]={0,0,0},zero2[]={0,0},authored3[]={255,0,255},authored2[]={0,255};
 mesh_pattern(gun,zero3,authored3,3);mesh_pattern(child,zero2,authored2,2);
 assert(fp_models_update(&p,&memory,&s,true,7000));
 assert(mask(root_mesh[0])==1&&mask(root_mesh[1])==0&&mask(root_mesh[2])==1);
 assert(mask(child_mesh[0])==0&&mask(child_mesh[1])==1&&mask(body_mesh)==254);
 assert(active_repairs(&p)==0);for(int i=0;i<p.count;i++)if(p.rows[i].ref==s.weapon_unit||p.rows[i].ref==1001)assert(!(p.rows[i].bits&1));
 unsigned no_repeat=redraw_writes;assert(fp_models_update(&p,&memory,&s,true,7001)&&redraw_writes==no_repeat);
 whole_hide(gun);whole_hide(child);assert(fp_models_update(&p,&memory,&s,true,7002));
 assert(mask(root_mesh[0])==1&&mask(root_mesh[1])==0&&mask(root_mesh[2])==1&&mask(child_mesh[0])==0&&mask(child_mesh[1])==1);
 assert(fp_models_update(&p,&memory,NULL,false,7003)&&mask(body_mesh)==255);
 assert(mask(root_mesh[0])==1&&mask(root_mesh[2])==1&&mask(child_mesh[1])==1); /* Never restore hidden bit0. */
 const uint8_t partly_visible[]={1,0,0},all_cached[]={255,255,255};
 mesh_pattern(gun,partly_visible,all_cached,3);no_repeat=redraw_writes;
 assert(fp_models_update(&p,&memory,&s,true,7010)&&redraw_writes==no_repeat&&mask(root_mesh[1])==0&&mask(root_mesh[2])==0);
 assert(fp_models_update(&p,&memory,NULL,false,7011));
 /* The failed first setter has already destroyed the later cached default1.
  * The pre-copied plan must finish it despite a now partly visible Unit. */
 mesh_pattern(gun,zero3,authored3,3);mesh_pattern(child,zero2,authored2,2);
 fail_redraw_ref=s.weapon_unit;fail_redraw_index=1;fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7020));
 uint8_t cache3[3];assert(fp_read(&memory,arena+0x300,cache3,3));
 assert(mask(root_mesh[0])==1&&mask(root_mesh[2])==0&&!(cache3[2]&1)&&active_repairs(&p)==1);
 assert(fp_models_update(&p,&memory,&s,true,7021)&&mask(root_mesh[2])==1&&active_repairs(&p)==0);
 assert(fp_models_update(&p,&memory,NULL,false,7022));
 mesh_pattern(gun,zero3,authored3,3);deny_after_redraw=root_mesh[2]+0xdc;
 assert(!fp_models_update(&p,&memory,&s,true,7030)&&active_repairs(&p)==1);f.denied=0;
 assert(mask(root_mesh[0])==1&&mask(root_mesh[2])==0);
 assert(fp_models_update(&p,&memory,&s,true,7031)&&mask(root_mesh[2])==1&&active_repairs(&p)==0);
 assert(fp_models_update(&p,&memory,NULL,false,7032));
 /* An unavailable final cached byte prevents any default repair write. */
 mesh_pattern(gun,zero3,authored3,3);no_repeat=redraw_writes;f.denied=arena+0x302;
 assert(!fp_models_update(&p,&memory,&s,true,7040)&&redraw_writes==no_repeat);f.denied=0;
 assert(fp_models_update(&p,&memory,&s,true,7041)&&mask(root_mesh[2])==1);
 assert(fp_models_update(&p,&memory,NULL,false,7042));
 /* A new array cannot inherit an unfinished old-array repair. */
 mesh_pattern(gun,zero3,authored3,3);fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7050)&&active_repairs(&p)==1);no_repeat=redraw_writes;
 for(unsigned i=0;i<3;i++)p64(arena+0xd00+i*16,root_mesh[i]);p64(gun+0xd0,arena+0xd00);put(arena+0x300,zero3,3);
 assert(fp_models_update(&p,&memory,&s,true,7051)&&redraw_writes==no_repeat&&active_repairs(&p)==0&&mask(root_mesh[2])==0);
 p64(gun+0xd0,arena+0x100);assert(fp_models_update(&p,&memory,NULL,false,7052));
 mesh_pattern(gun,zero3,authored3,3);fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7060)&&active_repairs(&p)==1);no_repeat=redraw_writes;
 put(arena+0xe00,zero3,3);p64(gun+0x100,arena+0xe00);
 assert(!fp_models_update(&p,&memory,&s,true,7061)&&redraw_writes==no_repeat&&active_repairs(&p)==0&&mask(root_mesh[2])==0);
 assert(fp_models_update(&p,&memory,&s,true,7062)&&redraw_writes==no_repeat);p64(gun+0x100,arena+0x300);
 assert(fp_models_update(&p,&memory,NULL,false,7063));
 /* Even a new weapon reusing this Unit cannot replay its predecessor's plan. */
 mesh_pattern(gun,zero3,authored3,3);fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7070)&&active_repairs(&p)==1);no_repeat=redraw_writes;
 select_weapon(202);FpSnapshot successor;assert(fp_snapshot(&memory,true,&successor,NULL,0)&&successor.weapon==202);
 assert(fp_models_update(&p,&memory,&successor,true,7071)&&redraw_writes==no_repeat&&active_repairs(&p)==0&&mask(root_mesh[2])==0);
 select_weapon(201);assert(fp_snapshot(&memory,true,&s,NULL,0));assert(fp_models_update(&p,&memory,NULL,false,7072));
 mesh_pattern(gun,zero3,authored3,3);fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7080)&&active_repairs(&p)==1);no_repeat=redraw_writes;
 uintptr_t registry=0,generations=0,units=0;assert(fp_pointer(&memory,memory.exe+FP_REGISTRY,&registry));assert(fp_pointer(&memory,registry+0xa0,&generations));assert(fp_pointer(&memory,registry+0x88,&units));
 uint8_t generation=1;put(generations+s.weapon_unit,&generation,1);
 assert(fp_models_update(&p,&memory,&s,true,7081)&&redraw_writes==no_repeat&&active_repairs(&p)==0&&mask(root_mesh[2])==0);
 generation=0;put(generations+s.weapon_unit,&generation,1);assert(fp_models_update(&p,&memory,NULL,false,7082));
 /* Object replacement with the same numerical ref also cancels old intent. */
 mesh_pattern(gun,zero3,authored3,3);fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7090)&&active_repairs(&p)==1);no_repeat=redraw_writes;
 uintptr_t replacement=0x2800000000ULL,vt=0;uint8_t object_bytes[512]={0};assert(fp_pointer(&memory,gun,&vt));put(replacement,object_bytes,sizeof object_bytes);
 p64(replacement,vt);n32(replacement+8,s.weapon_unit);p64(replacement+0x18,0x400000000ULL);p64(replacement+0x1d0,rig);p64(units+(uintptr_t)s.weapon_unit*8,replacement);
 meshes(replacement,arena+0xd00,root_mesh,3);n32(replacement+0xf8,3);p64(replacement+0x100,arena+0xe00);mesh_pattern(replacement,zero3,zero3,3);
 assert(fp_models_update(&p,&memory,&s,true,7091)&&redraw_writes==no_repeat&&active_repairs(&p)==0&&mask(root_mesh[2])==0);
 p64(units+(uintptr_t)s.weapon_unit*8,gun);assert(fp_models_update(&p,&memory,NULL,false,7092));
 /* A detached child becoming body geometry loses its held-weapon role. */
 mesh_pattern(gun,partly_visible,all_cached,3);const uint8_t child_both[]={255,255};mesh_pattern(child,zero2,child_both,2);
 fail_redraw_ref=1001;fail_redraw_index=1;fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7100)&&active_repairs(&p)==1&&mask(child_mesh[0])==1&&mask(child_mesh[1])==0);no_repeat=redraw_writes;
 p64(gun+0x1d8,0);p64(child+0x1d0,body);appearance[7]=1001;put(s.avatar_owner+0x546a70+0xdc,appearance,sizeof appearance);
 assert(fp_models_update(&p,&memory,&s,true,7101)&&redraw_writes==no_repeat&&active_repairs(&p)==0&&mask(child_mesh[1])==0);
 assert(fp_models_update(&p,&memory,NULL,false,7102));
 /* TFP-off abandons a partial redraw, without leasing its previous invisibility. */
 p64(gun+0x1d8,child);p64(child+0x1d0,gun);appearance[7]=0;put(s.avatar_owner+0x546a70+0xdc,appearance,sizeof appearance);
 mesh_pattern(gun,zero3,authored3,3);fail_redraw_ref=s.weapon_unit;fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7110)&&active_repairs(&p)==1);
 assert(fp_models_update(&p,&memory,NULL,false,7111)&&active_repairs(&p)==0&&mask(root_mesh[0])==1&&mask(root_mesh[2])==0&&mask(body_mesh)==255);
 /* No setter was attempted for the root before another target failed its
  * prepare pass. The next tick must use the newly authored native cache. */
 mesh_pattern(gun,zero3,authored3,3);mesh_pattern(child,zero2,authored2,2);no_repeat=redraw_writes;f.denied=arena+0xa01;
 assert(!fp_models_update(&p,&memory,&s,true,7120)&&redraw_writes==no_repeat&&active_repairs(&p)==1);
 for(unsigned i=0;i<FP_MODEL_REPAIRS;i++)if(p.repairs[i].active)assert(!p.repairs[i].started);
 const uint8_t changed_authored[]={0,255,0};put(arena+0x300,changed_authored,3);f.denied=0;
 assert(fp_models_update(&p,&memory,&s,true,7121)&&mask(root_mesh[0])==0&&mask(root_mesh[1])==1&&mask(root_mesh[2])==0);
 assert(fp_models_update(&p,&memory,NULL,false,7122));
 /* An action beginning after global preparation but before the first root
  * setter invalidates both cached intents before any default redraw. */
 mesh_pattern(gun,zero3,authored3,3);mesh_pattern(child,zero2,authored2,2);no_repeat=redraw_writes;action_on_write=true;
 assert(!fp_models_update(&p,&memory,&s,true,7130)&&redraw_writes==no_repeat&&active_repairs(&p)==0);
 uint8_t clear=0;put(0x500000005ULL,&clear,1);assert(fp_models_update(&p,&memory,NULL,false,7131));
 /* A reload flag transition cannot inherit an attempted older variant plan. */
 mesh_pattern(gun,zero3,authored3,3);fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7140)&&active_repairs(&p)==1);no_repeat=redraw_writes;
 uint8_t reload=2;put(s.avatar_owner+FP_AVATAR_FLAGS+15,&reload,1);assert(fp_snapshot(&memory,true,&successor,NULL,0));
 assert(fp_models_update(&p,&memory,&successor,true,7141)&&redraw_writes==no_repeat&&active_repairs(&p)==0&&mask(root_mesh[2])==0);
 put(s.avatar_owner+FP_AVATAR_FLAGS+15,&clear,1);assert(fp_snapshot(&memory,true,&s,NULL,0));assert(fp_models_update(&p,&memory,NULL,false,7142));
 /* A camera Unit successor retires intent even if player and gun are stable. */
 mesh_pattern(gun,zero3,authored3,3);fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7150)&&active_repairs(&p)==1);no_repeat=redraw_writes;
 n32(s.context+8,s.viewmodel);assert(fp_snapshot(&memory,true,&successor,NULL,0));
 assert(fp_models_update(&p,&memory,&successor,true,7151)&&redraw_writes==no_repeat&&active_repairs(&p)==0&&mask(root_mesh[2])==0);
 n32(s.context+8,s.camera_unit);assert(fp_models_update(&p,&memory,NULL,false,7152));
 /* A transient subtree read cannot erase an attempted repair's saved cache. */
 mesh_pattern(gun,zero3,authored3,3);fail_redraw_once=true;
 assert(!fp_models_update(&p,&memory,&s,true,7160)&&active_repairs(&p)==1);no_repeat=redraw_writes;f.denied=gun+0x1d8;
 assert(!fp_models_update(&p,&memory,&s,true,7161)&&redraw_writes==no_repeat&&active_repairs(&p)==1);f.denied=0;
 assert(fp_models_update(&p,&memory,&s,true,7162)&&mask(root_mesh[2])==1&&active_repairs(&p)==0);
 assert(fp_models_update(&p,&memory,NULL,false,7163));
 /* A Unit that becomes hidden after preparation must wait for the next full
  * prepare pass; apply cannot capture cache data after earlier SDK writes. */
 mesh_pattern(gun,partly_visible,authored3,3);no_repeat=redraw_writes;hide_on_write=gun;
 assert(fp_models_update(&p,&memory,&s,true,7170)&&redraw_writes==no_repeat&&mask(root_mesh[0])==0&&mask(root_mesh[2])==0&&active_repairs(&p)==0);
 assert(fp_models_update(&p,&memory,&s,true,7171)&&mask(root_mesh[0])==1&&mask(root_mesh[1])==0&&mask(root_mesh[2])==1);
 assert(fp_models_update(&p,&memory,NULL,false,7172));
 /* Re-hide an already completed entry while a later read remains pending. */
 mesh_pattern(gun,zero3,authored3,3);deny_after_redraw=root_mesh[2]+0xdc;
 assert(!fp_models_update(&p,&memory,&s,true,7180)&&active_repairs(&p)==1);f.denied=0;
 assert(mask(root_mesh[0])==1&&mask(root_mesh[2])==0);whole_hide(gun);
 assert(fp_models_update(&p,&memory,&s,true,7181)&&mask(root_mesh[0])==1&&mask(root_mesh[1])==0&&mask(root_mesh[2])==1&&active_repairs(&p)==0);
 assert(fp_models_update(&p,&memory,NULL,false,7182));
 cache_on_write=false;
}
static void subtree_bound_test(void){
 memory=native_setup();seed();unit_tag=1;cache_on_write=true;
 FpSnapshot s;assert(fp_snapshot(&memory,true,&s,NULL,0));
 uintptr_t body=fp_unit_object(&memory,s.body),rig=fp_unit_object(&memory,s.viewmodel),gun=fp_unit_object(&memory,s.weapon_unit),vt=0,registry=0,units=0;
 assert(fp_pointer(&memory,gun,&vt)&&fp_pointer(&memory,memory.exe+FP_REGISTRY,&registry)&&fp_pointer(&memory,registry+0x88,&units));
 const uintptr_t arena=0x2a00000000ULL,children=0x2900000000ULL;uint8_t bytes[8192]={0};put(arena,bytes,sizeof bytes);put(children,bytes,sizeof bytes);put(children+8192,bytes,sizeof bytes);
 uint64_t child_objects[32];for(unsigned i=0;i<32;i++)child_objects[i]=children+(uintptr_t)i*512;put(units+600*8,child_objects,sizeof child_objects);
 for(unsigned i=0;i<32;i++){
  uintptr_t object=child_objects[i];p64(object,vt);n32(object+8,600+i);p64(object+0x18,0x400000000ULL);p64(object+0x1d0,gun);p64(object+0x1e0,i<31?child_objects[i+1]:0);
  inventory[i+3]=600+i;
 }
 inventory[0]=s.body;inventory[1]=s.viewmodel;inventory[2]=s.weapon_unit;inventory_count=35;
 p64(0x1900000000ULL,0x400000000ULL);p64(body+0x1d0,0);p64(rig+0x1d0,0);p64(gun+0x1d0,rig);p64(gun+0x1d8,children);
 uint32_t appearance[30]={0};put(s.avatar_owner+0x546a70+0xdc,appearance,sizeof appearance);
 meshes(body,arena+0x100,NULL,0);meshes(rig,arena+0x200,NULL,0);uintptr_t root_mesh=arena+0x500;meshes(gun,arena+0x300,&root_mesh,1);n32(gun+0xf8,1);p64(gun+0x100,arena+0x700);
 uint8_t zero=0,authored=255;mesh_pattern(gun,&zero,&authored,1);static FpModels p;fp_models_init(&p);
 assert(fp_models_update(&p,&memory,&s,true,8000)&&p.target_count==35&&mask(root_mesh)==1);
 unsigned held=0;for(int i=0;i<p.target_count;i++)if(p.targets[i].held_weapon){held++;assert(p.targets[i].handle);}
 assert(held==33&&active_repairs(&p)==0);assert(fp_models_update(&p,&memory,NULL,false,8001)&&mask(root_mesh)==1);
 cache_on_write=false;
}
