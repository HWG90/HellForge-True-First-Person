#include "placement.h"
#include <math.h>
#include <string.h>
bool fp_placement_valid(const FpPlacement *v){
    if(!v)return false;
    return isfinite(v->x)&&isfinite(v->y)&&isfinite(v->z)&&fabs(v->x)<=.5&&fabs(v->y)<=.5&&fabs(v->z)<=.5
        &&isfinite(v->yaw)&&isfinite(v->pitch)&&isfinite(v->roll)&&fabs(v->yaw)<=90&&fabs(v->pitch)<=90&&fabs(v->roll)<=90;
}
void fp_placements_init(FpPlacements *p){memset(p,0,sizeof *p);p->base=(FpPlacement){.15,0,-.12,0,0,0};}
FpPlacement fp_placement_resolve(const FpPlacements *p,uint64_t weapon,FpAction action,bool *authored){
    FpPlacement fallback={.15,0,-.12,0,0,0};const FpWeaponProfile *individual=NULL;
    if(authored)*authored=false;
    if(!p||action<FP_IDLE||action>=FP_ACTION_COUNT)return fallback;
    for(unsigned i=0;i<p->count&&i<FP_PROFILE_LIMIT;i++)if(p->weapon[i].weapon==weapon){individual=p->weapon+i;break;}
    if(individual&&individual->present[action]&&fp_placement_valid(individual->action+action)){
        if(authored)*authored=true;
        return individual->action[action];
    }
    if(p->shared.present[action]&&fp_placement_valid(p->shared.action+action)){
        if(authored)*authored=true;
        return p->shared.action[action];
    }
    if(action==FP_ADS)return (FpPlacement){0};
    if(action!=FP_IDLE)return fp_placement_resolve(p,weapon,FP_IDLE,NULL);
    if(individual&&individual->legacy_present&&fp_placement_valid(&individual->legacy))return individual->legacy;
    if(p->shared.legacy_present&&fp_placement_valid(&p->shared.legacy))return p->shared.legacy;
    return fp_placement_valid(&p->base)?p->base:fallback;
}
bool fp_placement_save(FpPlacements *p,uint64_t weapon,FpAction action,const FpPlacement *v,bool all){
    if(!p||!weapon||action<FP_IDLE||action>=FP_ACTION_COUNT||!fp_placement_valid(v)||p->count>FP_PROFILE_LIMIT)return false;
    FpWeaponProfile *profile=NULL;
    if(all){
        for(unsigned i=0;i<p->count;i++)p->weapon[i].present[action]=false;
        profile=&p->shared;
    }else{
        for(unsigned i=0;i<p->count;i++)if(p->weapon[i].weapon==weapon){profile=p->weapon+i;break;}
        if(!profile){if(p->count==FP_PROFILE_LIMIT)return false;profile=p->weapon+p->count++;memset(profile,0,sizeof *profile);profile->weapon=weapon;}
    }
    profile->action[action]=*v;profile->present[action]=true;return true;
}
FpAction fp_action_select(const FpActionSample *s){
    if(!s)return FP_IDLE;
    if(s->ads)return FP_ADS;
    FpAction stance=FP_IDLE;
    if(s->verified_resource){
        unsigned posture=(s->flags[9]>>5)&3;
        if(posture==2&&(s->flags[12]&32))stance=FP_DIVE;
        else if(posture==1&&(s->flags[10]&2))stance=FP_SLIDE;
        else if(posture==1)stance=FP_CROUCH;
        else if(posture==2)stance=FP_PRONE;
    }
    if(stance==FP_DIVE||stance==FP_SLIDE)return stance;
    if(s->reload)return FP_RELOAD;
    if(s->shoulder_aim)return FP_AIM;
    if(s->sprint)return FP_SPRINT;
    if(stance!=FP_IDLE)return stance;
    if(s->verified_resource&&s->mapping_present&&s->mapped>=FP_IDLE&&s->mapped<FP_ACTION_COUNT)return s->mapped;
    return FP_IDLE;
}
const char *fp_action_name(FpAction a){
    static const char *const names[]={"idle","aim","ads","sprint","reload","crouch","prone","slide","dive"};
    return a>=FP_IDLE&&a<FP_ACTION_COUNT?names[a]:"unknown";
}
