/* Bob, sway and sprint-pose arithmetic. Translation of retail
 * src/first_person/motion.lua at 0.5.41-smooth.66 (commit 1d0d453);
 * tests/vectors/motion_vectors.h holds that file's own answers. Line numbers
 * in motion_math.c are the Lua file's.
 *
 * Pure: no engine, no clock, no memory. All arithmetic is in double, as Lua's;
 * nothing is converted to float here (the consumer, locomotion's to_parent,
 * takes doubles too).
 *
 * Lua values that C types cannot hold are spelled out:
 *  - a field that may be nil, a boolean or "something else" is a TfpMotionFlag;
 *  - a field that may be nil, a number or "something else" is a TfpMotionNumber;
 *  - a sample number that is not a number at all is passed as NaN (retail
 *    refuses both the same way, motion.lua:9);
 *  - the owner (a non-empty string or a finite number in retail) is a key:
 *    equal keys, equal owners; has_owner false stands for nil, '' and the rest. */
#ifndef TFP_MOTION_MATH_H
#define TFP_MOTION_MATH_H
#include <stdbool.h>
#include <stdint.h>

typedef enum TfpMotionFlag { TFP_MOTION_ABSENT, TFP_MOTION_FALSE, TFP_MOTION_TRUE, TFP_MOTION_OTHER } TfpMotionFlag;
typedef enum TfpMotionKind { TFP_MOTION_NIL, TFP_MOTION_NUMBER, TFP_MOTION_NOT_NUMBER } TfpMotionKind;
typedef struct TfpMotionNumber { TfpMotionKind kind; double value; } TfpMotionNumber;

/* The nine settings of motion.lua:41, in that order (the first invalid one names the error). */
enum { TFP_MOTION_BOB_STRENGTH, TFP_MOTION_BOB_SPEED, TFP_MOTION_SWAY_STRENGTH, TFP_MOTION_SWAY_SPEED,
       TFP_MOTION_SPRINT_STRENGTH, TFP_MOTION_CAMERA_BOB_STRENGTH, TFP_MOTION_CAMERA_BOB_SPEED,
       TFP_MOTION_CAMERA_SWAY_STRENGTH, TFP_MOTION_CAMERA_SWAY_SPEED, TFP_MOTION_SETTING_COUNT };
extern const char *const tfp_motion_setting_names[TFP_MOTION_SETTING_COUNT];

typedef struct TfpMotionOptions {
  TfpMotionFlag enabled, editor_frozen, shoulder_aim_enabled;
  TfpMotionNumber scale, setting[TFP_MOTION_SETTING_COUNT];
} TfpMotionOptions;

typedef enum TfpMotionAds { TFP_MOTION_ADS_OTHER, TFP_MOTION_ADS_TRUE, TFP_MOTION_ADS_NUMBER } TfpMotionAds;
typedef struct TfpMotionSample {
  bool has_owner;
  uint64_t owner;
  double x, y, z, yaw, pitch;
  TfpMotionAds ads_kind;  /* sample.ads: true, a number (ads), or anything else (false, nil) */
  double ads;
  bool shoulder_aim, sprint, editor_frozen, frozen; /* each is retail's "== true" */
} TfpMotionSample;

typedef enum TfpMotionReset { TFP_MOTION_RESET_NONE /* false */, TFP_MOTION_RESET_INVALID_SAMPLE, TFP_MOTION_RESET_INVALID_DT,
  TFP_MOTION_RESET_EDITOR_FROZEN, TFP_MOTION_RESET_INITIAL, TFP_MOTION_RESET_OWNER_CHANGED, TFP_MOTION_RESET_TIME_GAP
} TfpMotionReset;
typedef enum TfpMotionRebase { TFP_MOTION_REBASE_NONE /* false */, TFP_MOTION_REBASE_TIME_GAP, TFP_MOTION_REBASE_DISCONTINUITY
} TfpMotionRebase;
const char *tfp_motion_reset_name(TfpMotionReset reset);    /* retail's text; "" for false */
const char *tfp_motion_rebase_name(TfpMotionRebase rebase);

typedef struct TfpMotionOutput {
  double dx, dy, dz, pitch, yaw, roll;
  bool has_camera; /* false on a reset answer: retail's zero() has no camera_* fields (nil) */
  double camera_dx, camera_dy, camera_dz, camera_pitch, camera_yaw, camera_roll;
  double weight, speed, phase, sprint_weight, shoulder_weight, bob_weight;
  bool sprint_target, sprint_held;
  TfpMotionReset reset;
  TfpMotionRebase rebased;
} TfpMotionOutput;

typedef struct TfpMotionMath {
  bool enabled, editor_frozen, shoulder_aim_enabled;
  double scale, setting[TFP_MOTION_SETTING_COUNT];
  bool has_previous;
  struct { uint64_t owner; double x, y, z, yaw, pitch; } previous;
  double phase, camera_phase, camera_look_x, camera_look_z, amplitude, sprint, sprint_hold, shoulder, aim;
  double look_x, look_z, right_speed, forward_speed;
} TfpMotionMath;

/* Motion.new(options). options NULL: none given. false with retail's text when
 * configure refuses (retail raises that text through assert). */
bool tfp_motion_math_init(TfpMotionMath *m, const TfpMotionOptions *options, const char **error);
/* self:configure(value). value NULL: "not a table". true, or false and *error
 * (for a bad setting *error is "Invalid motion setting: " and *setting names it,
 * else *setting is -1). As retail, shoulder_aim_enabled is taken before the
 * nine settings are checked, so a refusal for a setting can still change it. */
bool tfp_motion_math_configure(TfpMotionMath *m, const TfpMotionOptions *value, const char **error, int *setting);
void tfp_motion_math_reset(TfpMotionMath *m);
void tfp_motion_math_clear_ads(TfpMotionMath *m);
/* self:update(sample, dt). sample NULL: "not a table". dt NaN also stands for "not a number". */
TfpMotionOutput tfp_motion_math_update(TfpMotionMath *m, const TfpMotionSample *sample, double dt);
#endif
