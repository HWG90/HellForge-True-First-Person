/* Presentation additions are derived from the player root, never the animated
 * rig. Every result is applied to that frame's native animation baseline. */
#include "motion.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
#define TAU (2*PI)
static double clamp(double x,double a,double b){return fmax(a,fmin(b,x));}
static double damp(double x,double target,double dt,double time){return x+(target-x)*(1-exp(-dt/time));}
static double settle(double x){return fabs(x)<.00001?0:x;}
static double angle(double x){double v=fmod(x+PI,TAU);if(v<0)v+=TAU;return v-PI;}
static void reset(FpMotion *s){
    FpMotionOptions options=s->options;memset(s,0,sizeof *s);s->options=options;
}
void fp_motion_init(FpMotion *s){
    memset(s,0,sizeof *s);s->options=(FpMotionOptions){.enabled=true,.scale=1,.bob_strength=1,.bob_speed=1,.sway_strength=1,.sway_speed=1,.camera_bob_speed=1,.camera_sway_speed=1};
}
bool fp_motion_configure(FpMotion *s,const FpMotionOptions *o){
    if(!s||!o||!isfinite(o->scale)||o->scale<0||o->scale>2)return false;
    const double strength[]={o->bob_strength,o->sway_strength,o->sprint_strength,o->camera_bob_strength,o->camera_sway_strength};
    const double speed[]={o->bob_speed,o->sway_speed,o->camera_bob_speed,o->camera_sway_speed};
    for(unsigned i=0;i<sizeof strength/sizeof strength[0];i++)if(!isfinite(strength[i])||strength[i]<0||strength[i]>3)return false;
    for(unsigned i=0;i<sizeof speed/sizeof speed[0];i++)if(!isfinite(speed[i])||speed[i]<.25||speed[i]>2)return false;
    FpMotionSample previous=s->previous;bool known=s->previous_valid;double shoulder=s->shoulder;
    s->options=*o;reset(s);
    if(!o->editor_frozen){s->previous=previous;s->previous_valid=known;s->shoulder=shoulder;}
    return true;
}
static bool valid(const FpMotionSample *v){return v&&v->owner&&isfinite(v->x)&&isfinite(v->y)&&isfinite(v->z)&&isfinite(v->yaw)&&isfinite(v->pitch);}
static FpMotionOutput zero(FpMotion *s,FpMotionReset reason){reset(s);FpMotionOutput out={0};out.reset=reason;return out;}
static FpMotionOutput discontinuity(FpMotion *s,const FpMotionSample *v,FpMotionReset reason){
    FpMotionOutput out=zero(s,reason);s->previous=*v;s->previous_valid=true;return out;
}
FpMotionOutput fp_motion_update(FpMotion *s,const FpMotionSample *v,double dt){
    if(!valid(v))return zero(s,FP_MOTION_SAMPLE);
    if(!isfinite(dt)||dt<=0)return zero(s,FP_MOTION_DT);
    if(s->options.editor_frozen||v->editor_frozen||v->frozen)return zero(s,FP_MOTION_FROZEN);
    if(!s->previous_valid)return discontinuity(s,v,FP_MOTION_INITIAL);
    if(s->previous.owner!=v->owner)return discontinuity(s,v,FP_MOTION_OWNER);
    if(dt>.25)return discontinuity(s,v,FP_MOTION_GAP);
    FpMotionSample p=s->previous;s->previous=*v;
    double wx=v->x-p.x,wy=v->y-p.y,wz=v->z-p.z,travel=sqrt(wx*wx+wy*wy),distance=sqrt(wx*wx+wy*wy+wz*wz);
    double turn=angle(v->yaw-p.yaw),pitch_turn=v->pitch-p.pitch;
    if(distance>2||distance/dt>20||fabs(turn)>PI/2||fabs(pitch_turn)>PI/2)return discontinuity(s,v,FP_MOTION_DISCONTINUITY);
    const FpMotionOptions *o=&s->options;double step=fmin(dt,.05),speed=travel/dt;
    double cosine=cos(v->yaw),sine=sin(v->yaw),right=(wx*cosine+wy*sine)/dt,forward=(-wx*sine+wy*cosine)/dt;
    s->phase=fmod(s->phase+TAU*travel/3.4*o->bob_speed,TAU);s->camera_phase=fmod(s->camera_phase+TAU*travel/3.4*o->camera_bob_speed,TAU);
    s->amplitude=damp(s->amplitude,clamp(speed/4,0,1),step,.10);
    double ads=isfinite(v->ads)?clamp(v->ads,0,1):0;bool shoulder=v->shoulder_aim;
    if(ads==1){s->sprint_hold=s->sprint=s->shoulder=0;}
    else{
        bool pose_aim=shoulder&&o->shoulder_aim_enabled;s->shoulder=damp(s->shoulder,pose_aim?1:0,step,shoulder?.10:.12);
        if(!pose_aim&&s->shoulder<.00001)s->shoulder=0;else if(pose_aim&&s->shoulder>1-.00001)s->shoulder=1;
    }
    bool motion=o->enabled&&o->scale>0,sprint=motion&&v->sprint&&ads<1&&!shoulder;
    if(ads==1)s->sprint_hold=s->sprint=0;
    else if(shoulder)s->sprint_hold=0;
    else if(sprint)s->sprint_hold=.10;
    else s->sprint_hold=fmax(0,s->sprint_hold-dt);
    bool sprint_target=sprint||s->sprint_hold>0;
    s->sprint=damp(s->sprint,sprint_target?1:0,step,sprint_target?.12:(shoulder?.15:.22));
    if(!sprint_target&&s->sprint<.00001)s->sprint=0;else if(sprint_target&&s->sprint>1-.00001)s->sprint=1;
    s->right_speed=damp(s->right_speed,right,step,.12);s->forward_speed=damp(s->forward_speed,forward,step,.12);
    s->look_x=damp(s->look_x,clamp(turn/dt*.0015,-.012,.012),step,.065/o->sway_speed);
    s->look_z=damp(s->look_z,clamp(-pitch_turn/dt*.001,-.008,.008),step,.065/o->sway_speed);
    s->camera_look_x=damp(s->camera_look_x,clamp(turn/dt*.002,-.015,.015),step,.08/o->camera_sway_speed);
    s->camera_look_z=damp(s->camera_look_z,clamp(-pitch_turn/dt*.0015,-.01,.01),step,.08/o->camera_sway_speed);
    double stance_weight=1-ads,weight=stance_weight*(motion?o->scale:0),walk=(1-s->sprint)*(shoulder?0:1),gait=walk+s->sprint*o->sprint_strength*2.5;
    double bx=sin(s->phase)*.008*s->amplitude*gait*o->bob_strength,by=cos(s->phase*2)*.003*s->amplitude*gait*o->bob_strength,bz=sin(s->phase*2)*.006*s->amplitude*gait*o->bob_strength;
    double ox=settle((bx+(s->look_x-clamp(s->right_speed*.001,-.008,.008))*walk*o->sway_strength)*weight);
    double oy=settle((by-clamp(s->forward_speed*.0006,-.006,.006)*walk*o->sway_strength)*weight);
    double oz=settle((bz+s->look_z*walk*o->sway_strength)*weight);
    FpMotionOutput out={
        .dx=settle(clamp(ox-s->shoulder*.04*stance_weight,-.15,.15)),.dy=settle(clamp(oy-s->shoulder*.06*stance_weight,-.15,.15)),.dz=oz,
        .pitch=-(12*PI/180)*s->sprint*o->sprint_strength*stance_weight,.yaw=(18*PI/180)*s->sprint*o->sprint_strength*stance_weight,.roll=-(10*PI/180)*s->sprint*o->sprint_strength*stance_weight,
        .camera_dx=settle(sin(s->camera_phase)*.006*s->amplitude*(1+s->sprint)*o->camera_bob_strength*stance_weight),
        .camera_dz=settle(sin(s->camera_phase*2)*.004*s->amplitude*(1+s->sprint)*o->camera_bob_strength*stance_weight),
        .camera_pitch=settle(s->camera_look_z*o->camera_sway_strength*stance_weight),.camera_yaw=settle(-s->camera_look_x*o->camera_sway_strength*stance_weight),
        .weight=weight,.speed=speed,.phase=s->phase,.sprint_weight=s->sprint,.shoulder_weight=s->shoulder,.bob_weight=motion?walk*s->amplitude:0,
        .sprint_target=sprint_target,.sprint_held=!sprint&&s->sprint_hold>0
    };return out;
}
