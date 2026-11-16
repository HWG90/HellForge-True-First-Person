#ifndef TFP_PLACEMENT_H
#define TFP_PLACEMENT_H
#include <stdbool.h>
#include <stdint.h>
#define FP_PROFILE_LIMIT 256
typedef enum FpAction {FP_IDLE,FP_AIM,FP_ADS,FP_SPRINT,FP_RELOAD,FP_CROUCH,FP_PRONE,FP_SLIDE,FP_DIVE,FP_ACTION_COUNT} FpAction;
typedef struct FpPlacement {double x,y,z,yaw,pitch,roll;} FpPlacement;
typedef struct FpWeaponProfile {
    uint64_t weapon;bool present[FP_ACTION_COUNT];FpPlacement action[FP_ACTION_COUNT];
    bool legacy_present;FpPlacement legacy;
} FpWeaponProfile;
typedef struct FpPlacements {
    FpPlacement base;FpWeaponProfile shared,weapon[FP_PROFILE_LIMIT];unsigned count;
} FpPlacements;
typedef struct FpActionSample {
    bool verified_resource,ads,reload,shoulder_aim,sprint;uint8_t flags[24];
    bool mapping_present;FpAction mapped;
} FpActionSample;
bool fp_placement_valid(const FpPlacement *);
void fp_placements_init(FpPlacements *);
FpPlacement fp_placement_resolve(const FpPlacements *,uint64_t,FpAction,bool *authored);
bool fp_placement_save(FpPlacements *,uint64_t,FpAction,const FpPlacement *,bool all);
FpAction fp_action_select(const FpActionSample *);
const char *fp_action_name(FpAction);
#endif
