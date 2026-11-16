#include "placement.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static FpPlacements p;
int main(void){
    fp_placements_init(&p);bool authored=true;FpPlacement v=fp_placement_resolve(&p,42,FP_IDLE,&authored);
    assert(!authored&&v.x==.15&&v.z==-.12);v=fp_placement_resolve(&p,42,FP_ADS,&authored);assert(!v.x&&!v.y&&!v.z&&!authored);
    FpPlacement a={.2,.1,-.1,10,20,30},b={-.2,0,.2,-10,-20,-30};
    assert(fp_placement_save(&p,42,FP_IDLE,&a,false));v=fp_placement_resolve(&p,42,FP_RELOAD,&authored);assert(!authored&&v.x==a.x);
    assert(fp_placement_save(&p,43,FP_IDLE,&b,false));assert(fp_placement_save(&p,42,FP_IDLE,&a,true));
    v=fp_placement_resolve(&p,43,FP_IDLE,&authored);assert(authored&&v.x==a.x);
    assert(fp_placement_save(&p,43,FP_IDLE,&b,false));v=fp_placement_resolve(&p,43,FP_IDLE,&authored);assert(authored&&v.x==b.x);
    FpPlacement invalid=a;invalid.x=NAN;assert(!fp_placement_save(&p,42,FP_IDLE,&invalid,false));invalid=a;invalid.roll=91;assert(!fp_placement_valid(&invalid));
    FpActionSample sample={0};sample.ads=true;sample.reload=true;sample.sprint=true;assert(fp_action_select(&sample)==FP_ADS);
    sample.ads=false;assert(fp_action_select(&sample)==FP_RELOAD);sample.reload=false;sample.shoulder_aim=true;assert(fp_action_select(&sample)==FP_AIM);
    sample.verified_resource=true;sample.flags[9]=0x40;sample.flags[12]=32;assert(fp_action_select(&sample)==FP_DIVE);
    sample.flags[9]=0x20;sample.flags[10]=2;assert(fp_action_select(&sample)==FP_SLIDE);
    sample.flags[10]=0;sample.shoulder_aim=false;sample.sprint=false;assert(fp_action_select(&sample)==FP_CROUCH);
    sample.verified_resource=false;assert(fp_action_select(&sample)==FP_IDLE);
    sample.verified_resource=true;sample.flags[9]=0x40;sample.flags[12]=0;assert(fp_action_select(&sample)==FP_PRONE);
    sample.flags[9]=0;sample.mapping_present=true;sample.mapped=FP_RELOAD;assert(fp_action_select(&sample)==FP_RELOAD);
    p.count=FP_PROFILE_LIMIT;assert(!fp_placement_save(&p,999,FP_IDLE,&a,false));assert(fp_placement_save(&p,999,FP_AIM,&a,true));
    puts("C placement: authored overrides, all-weapons clearing, native ADS, action priority and bounds passed");return 0;
}
