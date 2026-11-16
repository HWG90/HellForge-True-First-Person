/* See motion_math.h. Line references are to the retail file
 * src/first_person/motion.lua at 0.5.41-smooth.66. Build with
 * -ffp-contract=off: every product and sum below is one IEEE operation in Lua. */
#include "motion_math.h"
#include <math.h>
#include <stddef.h>

static const double PI = 3.14159265358979323846, TAU = 2 * 3.14159265358979323846; /* :8 */
static const double RAD = 1.74532925199432957692e-2; /* math.rad(x) = x * this constant (LuaJIT lib_math.c) */

const char *const tfp_motion_setting_names[TFP_MOTION_SETTING_COUNT] = { /* :41 */
  "bob_strength", "bob_speed", "sway_strength", "sway_speed", "sprint_strength", "camera_bob_strength",
  "camera_bob_speed", "camera_sway_strength", "camera_sway_speed"};
static const bool setting_is_speed[TFP_MOTION_SETTING_COUNT] = {false, true, false, true, false, false, true, false, true}; /* :42 */

/* math.min / math.max as LuaJIT computes them (minsd / maxsd with the first
 * argument as destination): the second argument wins on a tie and on NaN. */
static double lua_min(double a, double b) { return a < b ? a : b; }
static double lua_max(double a, double b) { return a > b ? a : b; }
/* Lua's a % b on numbers: a - floor(a/b)*b. */
static double lua_mod(double a, double b) { return a - floor(a / b) * b; }

static bool finite_number(double n) { return n == n && n > -HUGE_VAL && n < HUGE_VAL; }           /* :9 */
static double clamp(double n, double low, double high) { return lua_max(low, lua_min(high, n)); } /* :10 */
static double damp(double value, double target, double dt, double time) {                         /* :11 */
  return value + (target - value) * (1 - exp(-dt / time));
}
static double settle(double value) { return fabs(value) < .00001 ? 0 : value; } /* :12 */
static double angle(double n) { return lua_mod(n + PI, TAU) - PI; }             /* :13 */

static TfpMotionOutput zero(TfpMotionReset reason) { /* :14-17 */
  TfpMotionOutput out = {0};
  out.reset = reason;
  return out;
}
static bool valid(const TfpMotionSample *s) { /* :19-22 */
  return s && s->has_owner && finite_number(s->x) && finite_number(s->y) && finite_number(s->z) &&
         finite_number(s->yaw) && finite_number(s->pitch);
}

const char *tfp_motion_reset_name(TfpMotionReset reset) {
  static const char *const names[] = {"", "invalid_sample", "invalid_dt", "editor_frozen", "initial", "owner_changed", "time_gap"};
  return (unsigned)reset < sizeof names / sizeof *names ? names[reset] : "";
}
const char *tfp_motion_rebase_name(TfpMotionRebase rebase) {
  static const char *const names[] = {"", "time_gap", "discontinuity"};
  return (unsigned)rebase < sizeof names / sizeof *names ? names[rebase] : "";
}

void tfp_motion_math_reset(TfpMotionMath *m) { /* :25-28 */
  m->has_previous = false;
  m->phase = 0; m->camera_phase = 0; m->camera_look_x = 0; m->camera_look_z = 0; m->amplitude = 0;
  m->sprint = 0; m->sprint_hold = 0; m->shoulder = 0; m->aim = 0;
  m->look_x = 0; m->look_z = 0; m->right_speed = 0; m->forward_speed = 0;
}
void tfp_motion_math_clear_ads(TfpMotionMath *m) { /* :29-31 */
  m->sprint_hold = 0; m->sprint = 0; m->shoulder = 0;
}

static bool refuse(const char **error, int *setting, const char *text, int which) {
  if (error) *error = text;
  if (setting) *setting = which;
  return false;
}
static bool not_boolean(TfpMotionFlag f) { return f == TFP_MOTION_OTHER; } /* "~=nil and kind~='boolean'" */

bool tfp_motion_math_configure(TfpMotionMath *m, const TfpMotionOptions *v, const char **error, int *setting) { /* :32-54 */
  if (error) *error = NULL;
  if (setting) *setting = -1;
  if (!v) return refuse(error, setting, "Motion options require a table", -1);
  if (not_boolean(v->enabled)) return refuse(error, setting, "Motion enabled must be boolean", -1);
  if (v->scale.kind != TFP_MOTION_NIL &&
      (v->scale.kind != TFP_MOTION_NUMBER || !finite_number(v->scale.value) || v->scale.value < 0 || v->scale.value > 2))
    return refuse(error, setting, "Motion scale must be finite and between zero and two", -1);
  if (not_boolean(v->editor_frozen)) return refuse(error, setting, "Editor freeze must be boolean", -1);
  if (not_boolean(v->shoulder_aim_enabled)) return refuse(error, setting, "Aim movement must be boolean", -1);
  if (v->shoulder_aim_enabled != TFP_MOTION_ABSENT) m->shoulder_aim_enabled = v->shoulder_aim_enabled == TFP_MOTION_TRUE; /* :40 */
  for (int key = 0; key < TFP_MOTION_SETTING_COUNT; key++) { /* :41-44 */
    const TfpMotionNumber *n = v->setting + key;
    bool speed = setting_is_speed[key];
    if (n->kind != TFP_MOTION_NIL && (n->kind != TFP_MOTION_NUMBER || !finite_number(n->value) ||
                                      n->value < (speed ? .25 : 0) || n->value > (speed ? 2 : 3)))
      return refuse(error, setting, "Invalid motion setting: ", key);
  }
  for (int key = 0; key < TFP_MOTION_SETTING_COUNT; key++) /* :45 */
    if (v->setting[key].kind != TFP_MOTION_NIL) m->setting[key] = v->setting[key].value;
  /* :46-52. previous, shoulder and aim outlive the reset unless the editor freeze is on. */
  bool has_previous = m->has_previous;
  double shoulder = m->shoulder, aim = m->aim;
  if (v->enabled != TFP_MOTION_ABSENT) m->enabled = v->enabled == TFP_MOTION_TRUE;
  if (v->scale.kind != TFP_MOTION_NIL) m->scale = v->scale.value;
  if (v->editor_frozen != TFP_MOTION_ABSENT) m->editor_frozen = v->editor_frozen == TFP_MOTION_TRUE;
  tfp_motion_math_reset(m);
  if (!m->editor_frozen) {
    m->has_previous = has_previous; /* the previous sample itself is untouched by reset */
    m->shoulder = shoulder;
    m->aim = aim;
  }
  return true;
}

bool tfp_motion_math_init(TfpMotionMath *m, const TfpMotionOptions *options, const char **error) { /* :23-24, 159-161 */
  static const double defaults[TFP_MOTION_SETTING_COUNT] = {1, 1, 1, 1, 0, 0, 1, 0, 1};
  m->enabled = true;
  m->scale = 1;
  m->editor_frozen = false;
  m->shoulder_aim_enabled = false;
  for (int key = 0; key < TFP_MOTION_SETTING_COUNT; key++) m->setting[key] = defaults[key];
  m->previous.owner = 0;
  m->previous.x = m->previous.y = m->previous.z = m->previous.yaw = m->previous.pitch = 0;
  tfp_motion_math_reset(m);
  if (error) *error = NULL;
  if (options) return tfp_motion_math_configure(m, options, error, NULL);
  return true;
}

static void remember(TfpMotionMath *m, const TfpMotionSample *s) { /* :62, :65 */
  m->has_previous = true;
  m->previous.owner = s->owner;
  m->previous.x = s->x; m->previous.y = s->y; m->previous.z = s->z;
  m->previous.yaw = s->yaw; m->previous.pitch = s->pitch;
}

TfpMotionOutput tfp_motion_math_update(TfpMotionMath *m, const TfpMotionSample *sample, double dt) { /* :55-158 */
  if (!valid(sample)) { tfp_motion_math_reset(m); return zero(TFP_MOTION_RESET_INVALID_SAMPLE); }       /* :56 */
  if (!finite_number(dt) || dt <= 0) { tfp_motion_math_reset(m); return zero(TFP_MOTION_RESET_INVALID_DT); } /* :57 */
  if (m->editor_frozen || sample->editor_frozen || sample->frozen) {                                    /* :58-60 */
    tfp_motion_math_reset(m);
    return zero(TFP_MOTION_RESET_EDITOR_FROZEN);
  }
  bool had_previous = m->has_previous; /* :61 */
  uint64_t previous_owner = m->previous.owner;
  double px = m->previous.x, py = m->previous.y, pz = m->previous.z, pyaw = m->previous.yaw, ppitch = m->previous.pitch;
  remember(m, sample); /* :62 */
  if (!had_previous || previous_owner != sample->owner || dt > 1) { /* :63-67 */
    TfpMotionReset reason = !had_previous ? TFP_MOTION_RESET_INITIAL
        : previous_owner != sample->owner ? TFP_MOTION_RESET_OWNER_CHANGED : TFP_MOTION_RESET_TIME_GAP;
    tfp_motion_math_reset(m);
    remember(m, sample);
    return zero(reason);
  }
  double wx = sample->x - px, wy = sample->y - py, wz = sample->z - pz; /* :68 */
  double travel = sqrt(wx * wx + wy * wy);                               /* :69 */
  double distance = sqrt(wx * wx + wy * wy + wz * wz);                   /* :70 */
  double turn = angle(sample->yaw - pyaw), pitch_turn = sample->pitch - ppitch; /* :71 */
  TfpMotionRebase rebased = TFP_MOTION_REBASE_NONE;                      /* :76-78 */
  if (dt > .25) rebased = TFP_MOTION_REBASE_TIME_GAP;
  else if (distance > 2 || distance / dt > 20 || fabs(turn) > PI / 2 || fabs(pitch_turn) > PI / 2)
    rebased = TFP_MOTION_REBASE_DISCONTINUITY;
  if (rebased) { wx = 0; wy = 0; wz = 0; travel = 0; turn = 0; pitch_turn = 0; dt = lua_min(dt, .05); } /* :79 */
  (void)wz;
  double step = lua_min(dt, .05); /* :82 */
  double speed = travel / dt;     /* :83 */
  double cosine = cos(sample->yaw), sine = sin(sample->yaw); /* :84 */
  double right = (wx * cosine + wy * sine) / dt;             /* :85 */
  double forward = (-wx * sine + wy * cosine) / dt;          /* :86 */
  const double *k = m->setting;
  m->phase = lua_mod(m->phase + TAU * travel / 3.4 * k[TFP_MOTION_BOB_SPEED], TAU);                      /* :87 */
  m->camera_phase = lua_mod(m->camera_phase + TAU * travel / 3.4 * k[TFP_MOTION_CAMERA_BOB_SPEED], TAU); /* :88 */
  m->amplitude = damp(m->amplitude, clamp(speed / 4, 0, 1), step, .10);                                  /* :89 */
  double ads = sample->ads_kind == TFP_MOTION_ADS_TRUE ? 1                                               /* :90 */
      : (sample->ads_kind == TFP_MOTION_ADS_NUMBER && finite_number(sample->ads) ? clamp(sample->ads, 0, 1) : 0);
  bool shoulder = sample->shoulder_aim; /* :91 */
  if (ads == 1) { /* :98 */
    tfp_motion_math_clear_ads(m);
    m->aim = shoulder ? 1 : 0;
  } else { /* :99-107 */
    m->aim = damp(m->aim, shoulder ? 1 : 0, step, shoulder ? .10 : .12);
    if (!shoulder && m->aim < .00001) m->aim = 0;
    else if (shoulder && m->aim > 1 - .00001) m->aim = 1;
    bool pose_aim = shoulder && m->shoulder_aim_enabled;
    m->shoulder = damp(m->shoulder, pose_aim ? 1 : 0, step, shoulder ? .10 : .12);
    if (!pose_aim && m->shoulder < .00001) m->shoulder = 0;
    else if (pose_aim && m->shoulder > 1 - .00001) m->shoulder = 1;
  }
  bool motion = m->enabled && m->scale > 0;                 /* :108 */
  bool sprint = motion && sample->sprint && ads < 1 && !shoulder; /* :109 */
  if (ads == 1) { m->sprint_hold = 0; m->sprint = 0; }      /* :110 */
  else if (shoulder) m->sprint_hold = 0;                    /* :111 */
  else if (sprint) m->sprint_hold = .10;                    /* :112 */
  else m->sprint_hold = lua_max(0, m->sprint_hold - dt);    /* :113 */
  bool sprint_target = sprint || m->sprint_hold > 0;        /* :114 */
  m->sprint = damp(m->sprint, sprint_target ? 1 : 0, step, sprint_target ? .12 : (shoulder ? .15 : .22)); /* :115 */
  if (!sprint_target && m->sprint < .00001) m->sprint = 0;  /* :118 */
  else if (sprint_target && m->sprint > 1 - .00001) m->sprint = 1; /* :119 */
  m->right_speed = damp(m->right_speed, right, step, .12);         /* :120 */
  m->forward_speed = damp(m->forward_speed, forward, step, .12);   /* :121 */
  m->look_x = damp(m->look_x, clamp(turn / dt * .0015, -.012, .012), step, .065 / k[TFP_MOTION_SWAY_SPEED]);       /* :122 */
  m->look_z = damp(m->look_z, clamp(-pitch_turn / dt * .001, -.008, .008), step, .065 / k[TFP_MOTION_SWAY_SPEED]); /* :123 */
  m->camera_look_x = damp(m->camera_look_x, clamp(turn / dt * .002, -.015, .015), step, .08 / k[TFP_MOTION_CAMERA_SWAY_SPEED]);       /* :124 */
  m->camera_look_z = damp(m->camera_look_z, clamp(-pitch_turn / dt * .0015, -.01, .01), step, .08 / k[TFP_MOTION_CAMERA_SWAY_SPEED]); /* :125 */
  double stance_weight = 1 - ads;                               /* :126 */
  double weight = stance_weight * (motion ? m->scale : 0);      /* :127 */
  double walk = (1 - m->sprint) * (1 - m->aim);                 /* :130 */
  double gait = walk + m->sprint * k[TFP_MOTION_SPRINT_STRENGTH] * 2.5; /* :131 */
  double bob_x = sin(m->phase) * .008 * m->amplitude * gait * k[TFP_MOTION_BOB_STRENGTH];     /* :132 */
  double bob_y = cos(m->phase * 2) * .003 * m->amplitude * gait * k[TFP_MOTION_BOB_STRENGTH]; /* :133 */
  double bob_z = sin(m->phase * 2) * .006 * m->amplitude * gait * k[TFP_MOTION_BOB_STRENGTH]; /* :134 */
  double optional_x = settle((bob_x + (m->look_x - clamp(m->right_speed * .001, -.008, .008)) * walk * k[TFP_MOTION_SWAY_STRENGTH]) * weight); /* :137 */
  double optional_y = settle((bob_y - clamp(m->forward_speed * .0006, -.006, .006) * walk * k[TFP_MOTION_SWAY_STRENGTH]) * weight);            /* :138 */
  double optional_z = settle((bob_z + m->look_z * walk * k[TFP_MOTION_SWAY_STRENGTH]) * weight);                                               /* :139 */
  TfpMotionOutput out; /* :140-157 */
  out.dx = settle(clamp(optional_x - m->shoulder * .04 * stance_weight, -.15, .15));
  out.dy = settle(clamp(optional_y - m->shoulder * .06 * stance_weight, -.15, .15));
  out.dz = optional_z;
  out.pitch = -(12 * RAD) * m->sprint * k[TFP_MOTION_SPRINT_STRENGTH] * stance_weight;
  out.yaw = (18 * RAD) * m->sprint * k[TFP_MOTION_SPRINT_STRENGTH] * stance_weight;
  out.roll = -(10 * RAD) * m->sprint * k[TFP_MOTION_SPRINT_STRENGTH] * stance_weight;
  out.has_camera = true;
  out.camera_dx = settle(sin(m->camera_phase) * .006 * m->amplitude * (1 + m->sprint) * k[TFP_MOTION_CAMERA_BOB_STRENGTH] * stance_weight);
  out.camera_dy = 0;
  out.camera_dz = settle(sin(m->camera_phase * 2) * .004 * m->amplitude * (1 + m->sprint) * k[TFP_MOTION_CAMERA_BOB_STRENGTH] * stance_weight);
  out.camera_pitch = settle(m->camera_look_z * k[TFP_MOTION_CAMERA_SWAY_STRENGTH] * stance_weight);
  out.camera_yaw = settle(-m->camera_look_x * k[TFP_MOTION_CAMERA_SWAY_STRENGTH] * stance_weight);
  out.camera_roll = 0;
  out.weight = weight;
  out.speed = speed;
  out.phase = m->phase;
  out.reset = TFP_MOTION_RESET_NONE;
  out.rebased = rebased;
  out.sprint_weight = m->sprint;
  out.sprint_target = sprint_target;
  out.sprint_held = !sprint && m->sprint_hold > 0;
  out.shoulder_weight = m->shoulder;
  out.bob_weight = motion ? walk * m->amplitude : 0;
  return out;
}
