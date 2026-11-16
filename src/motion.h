#ifndef TFP_MOTION_H
#define TFP_MOTION_H
#include <stdbool.h>
#include <stdint.h>
typedef struct FpMotionOptions {
    bool enabled,editor_frozen,shoulder_aim_enabled;double scale;
    double bob_strength,bob_speed,sway_strength,sway_speed,sprint_strength;
    double camera_bob_strength,camera_bob_speed,camera_sway_strength,camera_sway_speed;
} FpMotionOptions;
typedef struct FpMotionSample {
    uint64_t owner;double x,y,z,yaw,pitch,ads;bool editor_frozen,frozen,shoulder_aim,sprint;
} FpMotionSample;
typedef enum FpMotionReset {FP_MOTION_NONE,FP_MOTION_SAMPLE,FP_MOTION_DT,FP_MOTION_FROZEN,FP_MOTION_INITIAL,FP_MOTION_OWNER,FP_MOTION_GAP,FP_MOTION_DISCONTINUITY} FpMotionReset;
typedef struct FpMotionOutput {
    double dx,dy,dz,pitch,yaw,roll,camera_dx,camera_dy,camera_dz,camera_pitch,camera_yaw,camera_roll;
    double weight,speed,phase,sprint_weight,shoulder_weight,bob_weight;bool sprint_target,sprint_held;FpMotionReset reset;
} FpMotionOutput;
typedef struct FpMotion {
    FpMotionOptions options;bool previous_valid;FpMotionSample previous;
    double phase,camera_phase,camera_look_x,camera_look_z,amplitude,sprint,sprint_hold,shoulder;
    double look_x,look_z,right_speed,forward_speed;
} FpMotion;
void fp_motion_init(FpMotion *);
bool fp_motion_configure(FpMotion *,const FpMotionOptions *);
FpMotionOutput fp_motion_update(FpMotion *,const FpMotionSample *,double dt);
#endif
