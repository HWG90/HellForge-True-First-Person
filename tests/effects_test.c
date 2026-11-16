#include "fake_native.h"
#include "feature_contract.h"
#include "effects.h"
#include <stdarg.h>
#include <assert.h>
static FpMemory memory;static bool flags[2]={true,false},component_value=true,fail_alive,late_set;static unsigned commands,getters;static uint64_t generation=1;
static uintptr_t rows=0x1600001000ULL;
static void render_rows(void){for(unsigned k=0;k<2;k++){uint8_t row[24]={4};row[8]=flags[k];uint32_t key=k?0x72e8f18c:0x06f1d0d3;memcpy(row+16,&key,4);put(rows+k*24,row,24);}}
bool eng_ready(void){return true;}uint64_t eng_state_generation(void){return generation;}const char *eng_error(void){return "fixture";}
uintptr_t eng_cfunction(const char *name){return memory.exe+(!strcmp(name,"Application.render_setting")?0x3fa3d0:0x3fa120);}
void eng_unref(EngRef *ref){*ref=0;}EngRef eng_copy(EngRef ref){return ref;}bool eng_same(EngRef a,EngRef b){return a==b;}
int eng_type(EngRef ref){return ref==100||ref==101?1:ref==30?3:ref==10||ref==11?5:7;}
bool eng_toboolean(EngRef ref,bool *out){if(eng_type(ref)!=1)return false;*out=ref==101;return true;}
bool eng_tonumber(EngRef ref,double *out){if(ref!=30)return false;*out=0x35b0f6c2;return true;}
int eng_len(EngRef ref){return ref==10||ref==11?1:0;}
bool eng_index(EngRef table,int at,EngRef *out){if(at!=1)return false;*out=table==10?20:table==11?30:0;return *out!=0;}
bool eng_call(const char *path,const char *sig,...){
 va_list args;va_start(args,sig);bool ok=true;
 if(!strcmp(path,"Application.render_setting")){const char *name=va_arg(args,const char *);unsigned k=!strcmp(name,"dof_enabled")?0:1;*va_arg(args,EngRef *)=flags[k]?101:100;getters++;}
 else if(!strcmp(path,"Application.set_render_setting")){const char *name=va_arg(args,const char *),*value=va_arg(args,const char *);unsigned k=!strcmp(name,"dof_enabled")?0:1;flags[k]=!strcmp(value,"true");render_rows();*va_arg(args,EngRef *)=0;commands++;ok=!late_set;}
 else if(!strcmp(path,"Application.main_world"))*va_arg(args,EngRef *)=1;
 else if(!strcmp(path,"EntityManager.render_data_component")){assert(va_arg(args,EngRef)==1);*va_arg(args,EngRef *)=2;}
 else if(!strcmp(path,"World.entities")){assert(va_arg(args,EngRef)==1);*va_arg(args,EngRef *)=10;}
 else if(!strcmp(path,"EntityManager.alive")){assert(va_arg(args,EngRef)==20);*va_arg(args,int *)=1;ok=!fail_alive;}
 else if(!strcmp(path,"RenderDataComponent.instances")){assert(va_arg(args,EngRef)==2);assert(va_arg(args,EngRef)==20);*va_arg(args,EngRef *)=11;}
 else if(!strcmp(path,"RenderDataComponent.get_property")){
  assert(va_arg(args,EngRef)==2);assert(va_arg(args,EngRef)==20);assert(va_arg(args,EngRef)==30);const char *key=va_arg(args,const char *);
  if(!strcmp(key,"shading_environment_mapping")){char *out=va_arg(args,char *);size_t size=va_arg(args,size_t);snprintf(out,size,"core/stingray_renderer/shading_environment_components/vignette");}
  else {assert(!strcmp(key,"vignette_enabled"));*va_arg(args,EngRef *)=component_value?101:100;}
 }else if(!strcmp(path,"RenderDataComponent.set_property")){
  assert(va_arg(args,EngRef)==2);assert(va_arg(args,EngRef)==20);assert(va_arg(args,EngRef)==30);assert(!strcmp(va_arg(args,const char *),"vignette_enabled"));component_value=va_arg(args,int)!=0;*va_arg(args,EngRef *)=0;
 }else{fprintf(stderr,"Unknown effect SDK call %s\n",path);assert(false);}
 va_end(args);return ok;
}
int main(void){
 memory=native_setup();for(unsigned i=0;i<sizeof fp_feature_sites/sizeof *fp_feature_sites;i++){const FpFeatureSite *s=fp_feature_sites+i;if(s->feature!=FP_FEATURE_EFFECTS)continue;uint8_t bytes[128];unsigned n=0;for(;s->hex[n*2];n++){unsigned x;assert(sscanf(s->hex+n*2,"%2x",&x)==1);bytes[n]=(uint8_t)x;}put((s->exe?memory.exe:memory.game)+s->rva,bytes,n);}
 uintptr_t table=0x1600000000ULL;p64(memory.exe+0x1a102b8,table);n32(table,2);p64(table+8,rows);render_rows();
 FpEffects effects;fp_effects_init(&effects);assert(fp_effects_update(&effects,&memory,true,1000));assert(effects.setting[0].held&&!flags[0]&&!effects.setting[1].held&&!component_value&&effects.count==1&&effects.rows[0].held);
 assert(fp_effects_update(&effects,&memory,true,1500));unsigned before_getters=getters;flags[0]=true;render_rows();assert(fp_effects_update(&effects,&memory,true,2000)&&!flags[0]&&commands==2&&getters==before_getters);
 fail_alive=true;assert(!fp_effects_update(&effects,&memory,false,2500));assert(effects.count==1&&effects.rows[0].held);fail_alive=false;
 assert(fp_effects_update(&effects,&memory,false,3000));assert(flags[0]&&!flags[1]&&component_value&&effects.count==0&&!effects.setting[0].held);
 late_set=true;assert(fp_effects_update(&effects,&memory,true,4000)&&effects.setting[0].held);late_set=false;assert(!fp_effects_update(&effects,&memory,false,4500));assert(fp_effects_update(&effects,&memory,false,5000)&&flags[0]);
 puts("C effects: global DOF, actual vignette component, cached zoom reassertion, preserved disabled baseline, deferred and uncertain cleanup passed");return 0;
}
