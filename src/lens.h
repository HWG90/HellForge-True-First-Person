#ifndef TFP_LENS_H
#define TFP_LENS_H
#include <stdbool.h>
typedef struct FpLens {
    bool shoulder_target,shoulder_clock,ready,ads,blending;
    double shoulder_previous,start;float shoulder_weight,from,last;unsigned blend_ms;
} FpLens;
void fp_lens_init(FpLens *,unsigned blend_ms);
float fp_lens_shoulder(FpLens *,float hip,bool shoulder,double now,bool clock_ok);
float fp_lens_handoff(FpLens *,float target,bool ads,double now,bool clock_ok);
#endif
