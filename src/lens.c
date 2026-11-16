#include "lens.h"
#include <math.h>
#include <string.h>
void fp_lens_init(FpLens *s,unsigned blend_ms){memset(s,0,sizeof *s);s->blend_ms=blend_ms;}
float fp_lens_shoulder(FpLens *s,float hip,bool shoulder,double now,bool clock_ok){
    float target=shoulder?1:0;
    if(s->shoulder_weight!=target){
        if(!clock_ok||!isfinite(now)){s->shoulder_weight=0;s->shoulder_target=s->shoulder_clock=false;return hip;}
        if(!s->shoulder_clock||s->shoulder_target!=shoulder){s->shoulder_target=shoulder;s->shoulder_clock=true;}
        else{
            double dt=now-s->shoulder_previous;
            if(dt>=0&&dt<=.25){float step=(float)(fmin(dt,.12)/.12);s->shoulder_weight=shoulder?fminf(1,s->shoulder_weight+step):fmaxf(0,s->shoulder_weight-step);}
        }
        s->shoulder_previous=now;
    }
    if(!s->shoulder_weight)return hip;
    float w=s->shoulder_weight*s->shoulder_weight*(3-2*s->shoulder_weight);
    return 2*atanf(tanf(hip*.5f)/(1+.05f*w));
}
float fp_lens_handoff(FpLens *s,float target,bool ads,double now,bool clock_ok){
    if(!s->blend_ms)return target;
    if(!isfinite(target)||target<=0||target>=3.14159265f){s->ready=s->blending=false;return target;}
    if(!s->ready){s->ready=true;s->ads=ads;s->last=target;return target;}
    if(ads!=s->ads){
        if(!clock_ok||!isfinite(now)){s->ready=s->blending=false;return target;}
        s->ads=ads;s->from=s->last;s->start=now;s->blending=true;
    }
    float result=target;
    if(s->blending){
        if(!clock_ok||!isfinite(now)){s->ready=s->blending=false;return target;}
        double elapsed=now-s->start,duration=(double)s->blend_ms/1000;
        if(elapsed<0||elapsed>=duration)s->blending=false;
        else{
            float t=(float)(elapsed/duration),w=t*t*(3-2*t),from=1/tanf(s->from*.5f),to=1/tanf(target*.5f);
            result=2*atanf(1/(from+(to-from)*w));
        }
    }
    s->last=result;return result;
}
