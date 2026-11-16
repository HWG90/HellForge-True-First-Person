#include "settings.h"
#include <assert.h>
#include <stdio.h>
#include <wchar.h>
int main(void){
 wchar_t directory[MAX_PATH],path[MAX_PATH];assert(GetTempPathW(MAX_PATH,directory));assert(GetTempFileNameW(directory,L"tfp",0,path));DeleteFileW(path);
 static FpSettings s,read;fp_settings_defaults(&s);assert(s.toggle_key==VK_F11&&s.editor_key==VK_F8&&s.show_markers&&!s.native_ads);
 FpPlacement shared={.1,.2,-.1,10,20,30},weapon={.2,.1,-.2,20,30,40};assert(fp_placement_save(&s.placements,11,FP_IDLE,&shared,true));assert(fp_placement_save(&s.placements,UINT64_C(0xffeeddccbbaa9988),FP_IDLE,&weapon,false));
 s.marker_legacy=true;s.marker_allow[0]=UINT64_C(0xffeeddccbbaa9988);s.marker_count=1;s.action_mapping[0]=(FpActionMapping){78,3,FP_SPRINT};s.mapping_count=1;s.placements.shared.legacy=shared;s.placements.shared.legacy_present=true;s.placements.weapon[0].legacy=weapon;s.placements.weapon[0].legacy_present=true;s.native_ads=true;s.show_markers=false;s.near_cm=23;s.eye.motion.camera_bob_strength=.31;assert(fp_settings_save(&s,path));assert(fp_settings_load(&read,path));
 assert(read.marker_legacy&&read.marker_count==1&&read.marker_allow[0]==UINT64_C(0xffeeddccbbaa9988)&&read.mapping_count==1&&read.action_mapping[0].action==FP_SPRINT&&read.placements.shared.legacy_present&&read.placements.weapon[0].legacy_present);assert(read.native_ads&&!read.show_markers&&read.near_cm==23&&read.eye.motion.camera_bob_strength==.31);assert(read.placements.count==1);FpPlacement result=fp_placement_resolve(&read.placements,UINT64_C(0xffeeddccbbaa9988),FP_IDLE,NULL);assert(result.x==.2&&result.yaw==20);
 result=fp_placement_resolve(&read.placements,55,FP_IDLE,NULL);assert(result.x==.1&&result.roll==30);result=fp_placement_resolve(&read.placements,55,FP_ADS,NULL);assert(result.x==0&&result.roll==0);
 assert(WritePrivateProfileStringW(L"settings",L"near_cm",L"NaN",path));assert(fp_settings_load(&read,path)&&read.near_cm==5);DeleteFileW(path);
 puts("C settings: defaults, atomic save/load, weapon/action and shared placement, native ADS, marker policy and invalid value fallback passed");return 0;
}
