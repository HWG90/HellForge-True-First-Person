/* See motion_runtime.h. Line references are to the retail file
 * src/first_person/motion_runtime.lua at 0.5.41-smooth.66. Build with
 * -ffp-contract=off: every product and sum below is one IEEE operation in Lua. */
#include "motion_runtime.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* The chunk name Lua prints before an assertion's message ("<chunk>:<line>: "), as pose_adapter.h has it. */
#define LOCOMOTION_CHUNK "src/first_person/locomotion.lua"

static const double RAD = 1.74532925199432957692e-2; /* math.rad(x) = x * this constant (LuaJIT lib_math.c) */
static const double HANDOFF_SECONDS = .15;           /* :44 */
static const double GUARD_SECONDS = .12, GUARD_SPEED = 1, GUARD_TURN_DEGREES = 360; /* :49 */
static const double MAX_STEP = .05;                  /* :51 */

/* math.min / math.max as LuaJIT computes them: the second argument wins on a tie and on NaN. */
static double lua_min(double a, double b) { return a < b ? a : b; }
static double lua_max(double a, double b) { return a > b ? a : b; }
static bool finite_number(double n) { return n == n && n > -HUGE_VAL && n < HUGE_VAL; } /* :6 */
static TfpMotionTri tri(bool value) { return value ? TFP_MOTION_TRI_TRUE : TFP_MOTION_TRI_FALSE; }

const char *tfp_motion_runtime_phase_name(TfpMotionRuntimePhase phase) {
  static const char *const names[] = {"", "idle", "active", "neutral", "unavailable", "disabled", "editor_frozen", "capture"};
  return (unsigned)phase < sizeof names / sizeof *names ? names[phase] : "";
}

static bool vector(const double v[TFP_MOTION_AXES]) { /* :12-24 */
  for (int axis = TFP_MOTION_DX; axis <= TFP_MOTION_DZ; axis++)
    if (!finite_number(v[axis]) || fabs(v[axis]) > .15) return false;
  double length = 0;
  for (int axis = TFP_MOTION_QX; axis <= TFP_MOTION_QW; axis++) {
    if (!finite_number(v[axis])) return false;
    length = length + v[axis] * v[axis];
  }
  if (fabs(length - 1) > .0001 || v[TFP_MOTION_QW] < cos(120 * RAD / 2)) return false;
  return true;
}

static void copy_text(char *out, const char *text) { snprintf(out, TFP_MOTION_RUNTIME_TEXT, "%s", text); }

static void report(TfpMotionRuntime *rt, const char *reason) { /* :56-60 */
  char text[TFP_MOTION_RUNTIME_TEXT];
  if (reason) copy_text(text, reason); /* reason may be the state's own text or the host's */
  rt->state.has_reason = reason != NULL;
  if (reason) {
    memcpy(rt->state.reason, text, sizeof text);
    if (!(rt->has_last_message && !strcmp(text, rt->last_message)) && rt->host->log) {
      char message[TFP_MOTION_RUNTIME_TEXT + 16];
      snprintf(message, sizeof message, "FPV motion: %s", text);
      rt->host->log(rt->host->user, message);
    }
    memcpy(rt->last_message, text, sizeof text);
  }
  rt->has_last_message = reason != NULL;
}

static void clear(TfpMotionRuntime *rt, bool keep_continuity) { /* :61-72 */
  TfpMotionRuntimeState *state = &rt->state;
  if (!keep_continuity) rt->continuity.has = false;
  rt->has_profile_epoch = false;
  rt->shown_lease = NULL; rt->guard.has = false; rt->has_handoff_epoch = false;
  rt->profile_lease = NULL; rt->profile_action = TFP_ACT_NONE; rt->profile_target.has = false; rt->profile_value.has = false;
  tfp_motion_math_reset(&rt->model); rt->last_lease = NULL; rt->has_last_time = false;
  rt->sent_lease = NULL; rt->sent_phase = TFP_MOTION_PHASE_NONE; rt->sent_ads = TFP_MOTION_TRI_NIL; rt->sent_authored = TFP_MOTION_TRI_NIL;
  rt->handoff.has = false; rt->previous_ads = TFP_MOTION_TRI_NIL;
  state->dx = 0; state->dy = 0; state->dz = 0;
  state->qx = 0; state->qy = 0; state->qz = 0; state->qw = 1;
  state->has_sprint_weight = true; state->sprint_weight = 0;
  state->has_bob_weight = true; state->bob_weight = 0;
  state->sprint_target = TFP_MOTION_TRI_FALSE; state->sprint_held = TFP_MOTION_TRI_FALSE;
  state->has_shoulder_weight = true; state->shoulder_weight = 0;
}

static TfpMotionRuntimeModifier mix_modifier(const double from[TFP_MOTION_AXES], const TfpMotionRuntimeModifier *to, double mix) { /* :73-86 */
  TfpMotionRuntimeModifier out;
  out.authored_ads = to->authored_ads;
  for (int axis = TFP_MOTION_DX; axis <= TFP_MOTION_DZ; axis++) out.axis[axis] = from[axis] + (to->axis[axis] - from[axis]) * mix;
  double length = 0;
  for (int axis = TFP_MOTION_QX; axis <= TFP_MOTION_QW; axis++) {
    out.axis[axis] = from[axis] + (to->axis[axis] - from[axis]) * mix;
    length = length + out.axis[axis] * out.axis[axis];
  }
  length = sqrt(length);
  for (int axis = TFP_MOTION_QX; axis <= TFP_MOTION_QW; axis++) out.axis[axis] = out.axis[axis] / length;
  return out;
}

static TfpMotionRuntimeModifier soften(TfpMotionRuntime *rt, const TfpMotionRuntimeLease *lease, TfpMotionRuntimeModifier value,
                                       bool blending) { /* :92-108 */
  if (rt->shown_lease != lease) { rt->guard.has = false; return value; }
  if (!rt->guard.has) {
    if (blending) return value;
    const double *v = value.axis, *shown = rt->shown;
    double x = v[TFP_MOTION_DX] - shown[TFP_MOTION_DX], y = v[TFP_MOTION_DY] - shown[TFP_MOTION_DY], z = v[TFP_MOTION_DZ] - shown[TFP_MOTION_DZ];
    double distance = sqrt(x * x + y * y + z * z);
    double dot = v[TFP_MOTION_QX] * shown[TFP_MOTION_QX] + v[TFP_MOTION_QY] * shown[TFP_MOTION_QY] + v[TFP_MOTION_QZ] * shown[TFP_MOTION_QZ] +
                 v[TFP_MOTION_QW] * shown[TFP_MOTION_QW];
    double turn = 2 * acos(lua_max(-1, lua_min(1, dot)));
    if (distance <= lua_max(.002, GUARD_SPEED * rt->frame_step) && turn <= lua_max(1 * RAD, GUARD_TURN_DEGREES * RAD * rt->frame_step))
      return value;
    memcpy(rt->guard.from, rt->shown, sizeof rt->guard.from);
    rt->guard.has = true;
    rt->guard.progress = 0;
    rt->state.softened = rt->state.softened + 1;
  }
  rt->guard.progress = lua_min(1, rt->guard.progress + rt->frame_step / GUARD_SECONDS);
  double t = rt->guard.progress;
  if (t >= 1) { rt->guard.has = false; return value; }
  return mix_modifier(rt->guard.from, &value, t * t * (3 - 2 * t));
}

/* The second answer of apply() and neutral() is always what report() left in state.reason. */
static void apply(TfpMotionRuntime *rt, const TfpMotionRuntimeLease *lease, TfpMotionRuntimeModifier value, TfpMotionRuntimePhase phase,
                  const char *reason, bool direct, bool blending) { /* :109-131 */
  TfpMotionRuntimeState *state = &rt->state;
  bool ads = state->ads == TFP_MOTION_TRI_TRUE; /* :110; a sample's ads is a boolean */
  if (direct) rt->guard.has = false;
  else value = soften(rt, lease, value, blending);
  bool authored = value.authored_ads;
  bool unchanged = rt->sent_lease == lease && rt->sent_phase == phase && rt->sent_ads == tri(ads) && rt->sent_authored == tri(authored);
  if (unchanged)
    for (int axis = 0; axis < TFP_MOTION_AXES; axis++)
      if (rt->sent[axis] != value.axis[axis]) { unchanged = false; break; }
  if (unchanged) {
    state->coalesced = state->coalesced + 1; state->phase = phase; report(rt, reason);
    return;
  }
  const char *why = NULL;
  bool accepted = rt->host->set_pose_motion(rt->host->user, lease, &value, &why);
  if (!accepted) {
    state->write_refusals = state->write_refusals + 1; state->phase = TFP_MOTION_PHASE_UNAVAILABLE;
    clear(rt, false); report(rt, why ? why : "Owned native pose did not accept motion");
    return;
  }
  state->applied = state->applied + 1; state->phase = phase;
  for (int axis = 0; axis < TFP_MOTION_AXES; axis++) { rt->sent[axis] = value.axis[axis]; rt->shown[axis] = value.axis[axis]; }
  rt->sent_lease = lease; rt->sent_phase = phase; rt->sent_ads = tri(ads); rt->sent_authored = tri(authored);
  rt->shown_lease = lease;
  state->dx = value.axis[TFP_MOTION_DX]; state->dy = value.axis[TFP_MOTION_DY]; state->dz = value.axis[TFP_MOTION_DZ];
  state->qx = value.axis[TFP_MOTION_QX]; state->qy = value.axis[TFP_MOTION_QY]; state->qz = value.axis[TFP_MOTION_QZ];
  state->qw = value.axis[TFP_MOTION_QW];
  report(rt, reason);
}

static bool shared_phase(TfpMotionRuntime *rt, TfpTransitionPacket *packet) { /* :132-137 */
  if (!rt->host->presentation_transition) return false;
  if (!rt->host->presentation_transition(rt->host->user, packet)) return false;
  if (!finite_number(packet->progress) || packet->progress < 0 || packet->progress > 1) return false;
  return true;
}

static double phase_step(TfpMotionRuntime *rt, const TfpTransitionPacket *packet) { /* :138-143 */
  if (!rt->has_profile_epoch || rt->profile_epoch != packet->epoch) {
    rt->has_profile_epoch = true; rt->profile_epoch = packet->epoch; rt->profile_used = 0;
  }
  double e = packet->progress * packet->progress * (3 - 2 * packet->progress);
  double step = packet->progress == 1 ? 1 : lua_max(0, lua_min(1, (e - rt->profile_used) / lua_max(.000001, 1 - rt->profile_used)));
  rt->profile_used = e;
  return step;
}

static void handoff_parent(TfpMotionRuntime *rt, const TfpMotionRuntimeLease *lease, TfpMotionRuntimeModifier *value, bool ads,
                           double timestamp) { /* :146-176 */
  TfpTransitionPacket packet;
  bool has_packet = shared_phase(rt, &packet);
  bool shared = has_packet && packet.ads == ads;
  if ((shared && (!rt->has_handoff_epoch || rt->handoff_epoch != packet.epoch)) ||
      (rt->previous_ads != TFP_MOTION_TRI_NIL && rt->previous_ads != tri(ads)) ||
      (rt->previous_ads == TFP_MOTION_TRI_NIL && (ads || lease->pose_blend_entry))) {
    static const double identity[TFP_MOTION_AXES] = {0, 0, 0, 0, 0, 0, 1};
    memcpy(rt->handoff.from, rt->sent_lease == lease ? rt->sent : identity, sizeof rt->handoff.from);
    rt->handoff.has = true; rt->handoff.at = timestamp; rt->handoff.progress = 0;
    if (shared) { rt->has_handoff_epoch = true; rt->handoff_epoch = packet.epoch; }
  }
  rt->previous_ads = tri(ads);
  double mix = 1;
  if (rt->handoff.has) {
    rt->handoff.progress = lua_min(1, rt->handoff.progress + lua_max(0, lua_min(timestamp - rt->handoff.at, MAX_STEP)) / HANDOFF_SECONDS);
    rt->handoff.at = timestamp;
    double t = shared ? packet.progress : rt->handoff.progress;
    mix = t * t * (3 - 2 * t);
    const double *from = rt->handoff.from;
    if (mix == 0) {
      for (int axis = 0; axis < TFP_MOTION_AXES; axis++) value->axis[axis] = from[axis];
    } else if (mix < 1) {
      TfpMotionRuntimeModifier blended = mix_modifier(from, value, mix);
      for (int axis = 0; axis < TFP_MOTION_AXES; axis++) value->axis[axis] = blended.axis[axis];
    }
    if (t == 1) rt->handoff.has = false;
  }
  if (ads && rt->handoff.has &&
      (value->axis[TFP_MOTION_QX] != 0 || value->axis[TFP_MOTION_QY] != 0 || value->axis[TFP_MOTION_QZ] != 0 || value->axis[TFP_MOTION_QW] != 1))
    value->authored_ads = true;
  rt->state.has_ads_handoff = true; rt->state.ads_handoff = mix;
}

static void neutral(TfpMotionRuntime *rt, const TfpMotionRuntimeLease *lease, TfpMotionRuntimePhase phase, const char *reason) { /* :177-184 */
  static const TfpMotionRuntimeModifier identity = {{0, 0, 0, 0, 0, 0, 1}, false};
  TfpMotionRuntimeState *state = &rt->state;
  rt->handoff.has = false; rt->previous_ads = TFP_MOTION_TRI_NIL; rt->has_handoff_epoch = false;
  if (reason || state->phase != phase) rt->sent_lease = NULL;
  tfp_motion_math_reset(&rt->model); rt->has_last_time = false;
  state->dx = 0; state->dy = 0; state->dz = 0;
  state->has_sprint_weight = true; state->sprint_weight = 0;
  state->has_bob_weight = true; state->bob_weight = 0;
  state->sprint_target = TFP_MOTION_TRI_FALSE; state->sprint_held = TFP_MOTION_TRI_FALSE;
  state->has_shoulder_weight = true; state->shoulder_weight = 0;
  apply(rt, lease, identity, phase, reason, false, false);
}

/* `return neutral(...)` / `return apply(...)`: true and the reported reason. */
static TfpMotionRuntimeAnswer answered(TfpMotionRuntime *rt, const char **reason) {
  if (reason) *reason = rt->state.has_reason ? rt->state.reason : NULL;
  return TFP_MOTION_RUNTIME_TRUE;
}
#define NEUTRAL(phase, text) (neutral(rt, lease, phase, text), answered(rt, reason))

/* to_parent through pcall. A failed assertion of the real conversion carries Lua's "<chunk>:<line>: ". */
static bool to_parent(TfpMotionRuntime *rt, double dx, double dy, double dz, double pitch, double yaw, double roll,
                      const TfpLocomotionSample *sample, TfpMotionRuntimeModifier *parent, const char **error) {
  static const struct { int line; const char *text; } asserts[] = { /* locomotion.lua */
    {13, "Non-finite transform basis"}, {14, "Transform scale changed"}, {17, "Transform basis is skewed"},
    {36, "Motion angle out of bounds"}, {39, "Motion rotation basis is mirrored"}, {43, "Motion rotation axis changed"},
    {52, "Motion quaternion unavailable"}, {65, "Motion vector unavailable"}, {69, "Motion vector out of bounds"}};
  const TfpLocomotionValue value = {dx, dy, dz, pitch, yaw, roll, TFP_LOCO_ALL};
  TfpLocomotionParent out;
  const char *text = NULL;
  bool ok = rt->host->to_parent ? rt->host->to_parent(rt->host->user, &value, sample->camera_basis, sample->parent_basis, &out, &text)
                                : tfp_locomotion_to_parent(&value, sample->camera_basis, sample->parent_basis, &out, &text);
  if (!ok) {
    if (!text) text = "";
    if (!rt->host->to_parent) {
      int line = 0;
      for (size_t i = 0; i < sizeof asserts / sizeof *asserts; i++)
        if (!strcmp(text, asserts[i].text)) line = asserts[i].line;
      snprintf(rt->answer, sizeof rt->answer, LOCOMOTION_CHUNK ":%d: %s", line, text);
    } else copy_text(rt->answer, text);
    *error = rt->answer;
    return false;
  }
  parent->axis[TFP_MOTION_DX] = out.dx; parent->axis[TFP_MOTION_DY] = out.dy; parent->axis[TFP_MOTION_DZ] = out.dz;
  parent->axis[TFP_MOTION_QX] = out.qx; parent->axis[TFP_MOTION_QY] = out.qy; parent->axis[TFP_MOTION_QZ] = out.qz;
  parent->axis[TFP_MOTION_QW] = out.qw;
  parent->authored_ads = false;
  return true;
}
/* `p.yaw or 0` */
static double field(const TfpActionPlacement *p, unsigned bit, double value) { return p->present & bit ? value : 0; }

bool tfp_motion_runtime_init(TfpMotionRuntime *rt, const TfpMotionRuntimeHost *host, const TfpMotionOptions *model_options,
                             const char **error) { /* :25-55 */
  if (error) *error = NULL;
  if (!host || !host->clock) { /* :26 */
    if (error) *error = "Motion runtime dependencies required";
    return false;
  }
  memset(rt, 0, sizeof *rt);
  rt->host = host;
  tfp_motion_math_init(&rt->model, NULL, NULL); /* :32, Motion.new(): configure's refusal is raised by its assert */
  if (model_options) {
    const char *problem = NULL;
    int setting = -1;
    if (!tfp_motion_math_configure(&rt->model, model_options, &problem, &setting)) {
      snprintf(rt->answer, sizeof rt->answer, "%s%s", problem, setting >= 0 ? tfp_motion_setting_names[setting] : "");
      if (error) *error = rt->answer;
      return false;
    }
  }
  rt->profile_action = TFP_ACT_NONE;
  rt->frame_step = 1.0 / 60; /* :50 */
  rt->state.phase = TFP_MOTION_PHASE_IDLE; /* :54-55 */
  rt->state.qw = 1;
  rt->state.action = TFP_ACT_NONE;
  return true;
}

bool tfp_motion_runtime_reset(TfpMotionRuntime *rt) { /* :185-188 */
  clear(rt, false); rt->state.phase = TFP_MOTION_PHASE_IDLE; report(rt, NULL);
  return true;
}

void tfp_motion_runtime_result(const TfpMotionRuntime *rt, TfpMotionRuntimeState *out) { *out = rt->state; } /* :189-192 */

static TfpMotionRuntimeAnswer refused(TfpMotionRuntime *rt, const char **reason, const char *text) {
  if (text != rt->answer) copy_text(rt->answer, text);
  if (reason) *reason = rt->answer;
  return TFP_MOTION_RUNTIME_FALSE;
}

TfpMotionRuntimeAnswer tfp_motion_runtime_capture_neutral(TfpMotionRuntime *rt, bool native_ads, const char **reason) { /* :195-219 */
  const TfpMotionRuntimeHost *host = rt->host;
  TfpMotionRuntimeState *state = &rt->state;
  if (reason) *reason = NULL;
  state->capture_ready = TFP_MOTION_TRI_FALSE;
  const TfpMotionRuntimeLease *lease = host->controller_lease;
  TfpMotionRuntimeControllerResult result = {false, false};
  TfpMotionRuntimeGot got_result = host->controller_result(host->user, &result); /* :198, not protected */
  if (got_result == TFP_MOTION_GOT_RAISED) return TFP_MOTION_RUNTIME_RAISED;
  TfpMotionRuntimeGot enabled = host->controller_is_enabled(host->user); /* :199 */
  if (enabled == TFP_MOTION_GOT_RAISED) return TFP_MOTION_RUNTIME_RAISED;
  if (enabled != TFP_MOTION_GOT_VALUE) return TFP_MOTION_RUNTIME_TRUE;
  if (got_result != TFP_MOTION_GOT_VALUE) return TFP_MOTION_RUNTIME_RAISED; /* result.mode of something that is no table */
  if (!result.pose_offset || !result.running || !lease || lease != host->alignment_lease || lease->done || lease->cleanup_only)
    return TFP_MOTION_RUNTIME_TRUE;
  TfpLocomotionSample sample;
  const char *why = NULL;
  if (!host->sample(host->user, lease, &sample, &why)) return refused(rt, reason, why ? why : "Capture motion sample unavailable"); /* :203-204 */
  TfpActionPlacement placement = {0, 0, 0, 0, 0, 0, 0};
  TfpMotionRuntimeGot got = TFP_MOTION_GOT_NIL; /* :205-206 */
  if (host->action_placement) got = host->action_placement(host->user, host->placement_weapon_key, TFP_ACT_IDLE, &placement);
  if (got == TFP_MOTION_GOT_NIL) got = host->controller_get_placement(host->user, &placement);
  if (got == TFP_MOTION_GOT_RAISED) return TFP_MOTION_RUNTIME_RAISED;
  if (native_ads) { /* :207 */
    placement = (TfpActionPlacement){0, 0, 0, 0, 0, 0, TFP_PLACE_ALL};
    got = TFP_MOTION_GOT_VALUE;
  }
  const char *problem = NULL;
  if (!host->controller_set_placement(host->user, got == TFP_MOTION_GOT_VALUE ? &placement : NULL, &problem)) /* :208-209 */
    return refused(rt, reason, problem ? problem : "Capture idle placement unavailable");
  clear(rt, false); state->ads = TFP_MOTION_TRI_FALSE; state->action = TFP_ACT_IDLE; /* :210 */
  if (got != TFP_MOTION_GOT_VALUE) return TFP_MOTION_RUNTIME_RAISED; /* :211, placement.yaw of nil */
  TfpMotionRuntimeModifier parent;
  const char *error = NULL;
  if (!to_parent(rt, 0, 0, 0, field(&placement, TFP_PLACE_PITCH, placement.pitch) * RAD, field(&placement, TFP_PLACE_YAW, placement.yaw) * RAD,
                 field(&placement, TFP_PLACE_ROLL, placement.roll) * RAD, &sample, &parent, &error) ||
      !vector(parent.axis))
    return refused(rt, reason, "Capture rotation basis unavailable"); /* :213-214 */
  apply(rt, lease, parent, TFP_MOTION_PHASE_CAPTURE, NULL, true, false); /* :215 */
  bool ready = state->phase == TFP_MOTION_PHASE_CAPTURE;
  state->capture_ready = tri(ready);        /* :216 */
  state->capture_native_ads = tri(native_ads); /* :217 */
  if (reason) *reason = state->has_reason ? state->reason : NULL;
  return ready ? TFP_MOTION_RUNTIME_TRUE : TFP_MOTION_RUNTIME_FALSE;
}

static bool option_differs_flag(TfpMotionFlag a, TfpMotionFlag b) { return a == TFP_MOTION_OTHER || b == TFP_MOTION_OTHER || a != b; }
static bool option_differs(const TfpMotionNumber *a, const TfpMotionNumber *b) {
  if (a->kind == TFP_MOTION_NOT_NUMBER || b->kind == TFP_MOTION_NOT_NUMBER || a->kind != b->kind) return true;
  return a->kind == TFP_MOTION_NUMBER && a->value != b->value;
}

TfpMotionRuntimeAnswer tfp_motion_runtime_update(TfpMotionRuntime *rt, const char **reason) { /* :220-405 */
  const TfpMotionRuntimeHost *host = rt->host;
  TfpMotionRuntimeState *state = &rt->state;
  bool resumed_modifier = false;
  double timestamp = 0;
  bool ok;
  if (reason) *reason = NULL;
  state->capture_ready = TFP_MOTION_TRI_FALSE;
  ok = host->clock(host->user, &timestamp); /* :224-230 */
  if (ok && finite_number(timestamp)) {
    double elapsed = rt->has_frame_time ? timestamp - rt->frame_time : 1.0 / 60;
    if (elapsed > 0) rt->frame_step = lua_max(1.0 / 240, lua_min(elapsed, MAX_STEP));
    rt->has_frame_time = true; rt->frame_time = timestamp;
  }
  TfpMotionRuntimeControllerResult result = {false, false};
  bool has_result = host->controller_result(host->user, &result) == TFP_MOTION_GOT_VALUE; /* :232 */
  bool enabled = host->controller_is_enabled(host->user) == TFP_MOTION_GOT_VALUE;         /* :233 */
  const TfpMotionRuntimeLease *lease = host->controller_lease;
  if (!enabled || !has_result || !result.pose_offset || !result.running || !lease || lease != host->alignment_lease || lease->done ||
      lease->cleanup_only) { /* :235-249 */
    ok = host->clock(host->user, &timestamp);
    if (enabled && has_result && result.pose_offset && ok && finite_number(timestamp)) {
      const TfpMotionRuntimeLease *last = rt->last_lease;
      if (last && last->has_origin && rt->sent_lease == last) {
        if (last->has_identity && last->has_player_identity) {
          rt->continuity.has = true;
          rt->continuity.identity = last->identity; rt->continuity.player_identity = last->player_identity;
          rt->continuity.at = timestamp;
          memcpy(rt->continuity.value, rt->sent, sizeof rt->sent);
          rt->continuity.ads = rt->previous_ads;
        }
      }
      if (rt->continuity.has && (timestamp < rt->continuity.at || timestamp - rt->continuity.at > .25)) rt->continuity.has = false;
    } else rt->continuity.has = false;
    clear(rt, rt->continuity.has); state->phase = TFP_MOTION_PHASE_IDLE; report(rt, NULL);
    return TFP_MOTION_RUNTIME_TRUE;
  }
  if (rt->last_lease != lease) { /* :250-263 */
    bool carried = rt->continuity.has;
    uint64_t identity = rt->continuity.identity, player_identity = rt->continuity.player_identity;
    double at = rt->continuity.at, value[TFP_MOTION_AXES];
    TfpMotionTri carried_ads = rt->continuity.ads;
    memcpy(value, rt->continuity.value, sizeof value);
    ok = host->clock(host->user, &timestamp);
    bool keep = carried && ok && finite_number(timestamp) && timestamp >= at && timestamp - at <= .25 && lease->has_origin &&
                lease->has_identity && lease->identity == identity && lease->has_player_identity && lease->player_identity == player_identity;
    clear(rt, false); rt->last_lease = lease;
    if (keep) {
      resumed_modifier = true;
      memcpy(rt->sent, value, sizeof value);
      rt->sent_lease = lease; rt->previous_ads = carried_ads;
      rt->handoff.has = true; rt->handoff.at = timestamp; rt->handoff.progress = 0;
      memcpy(rt->handoff.from, value, sizeof value);
      if (host->presentation_resume) host->presentation_resume(host->user, lease->identity, lease->player_identity, timestamp);
    }
  }
  TfpMotionOptions current_options; /* :264-269 */
  memset(&current_options, 0, sizeof current_options);
  current_options.enabled = TFP_MOTION_TRUE;
  current_options.scale = (TfpMotionNumber){TFP_MOTION_NUMBER, 1};
  if (host->options && !host->options(host->user, &current_options)) return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, "Motion options unavailable");
  bool on = current_options.enabled != TFP_MOTION_FALSE; /* :270 */
  double scale = current_options.scale.kind == TFP_MOTION_NIL ? 1 : current_options.scale.kind == TFP_MOTION_NUMBER ? current_options.scale.value : NAN;
  if (!finite_number(scale) || scale < 0 || scale > 2) return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, "Motion scale is invalid"); /* :272 */
  bool options_changed = !rt->has_last_options; /* :273-274 */
  if (!options_changed) {
    options_changed = option_differs_flag(rt->last_options.enabled, current_options.enabled) ||
                      option_differs(&rt->last_options.scale, &current_options.scale);
    for (int key = 0; key < TFP_MOTION_SETTING_COUNT && !options_changed; key++)
      options_changed = option_differs(&rt->last_options.setting[key], &current_options.setting[key]);
  }
  if (options_changed) { /* :275-280 */
    const char *problem = NULL;
    int setting = -1;
    if (!tfp_motion_math_configure(&rt->model, &current_options, &problem, &setting)) {
      char text[TFP_MOTION_RUNTIME_TEXT];
      snprintf(text, sizeof text, "%s%s", problem, setting >= 0 ? tfp_motion_setting_names[setting] : "");
      return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, text);
    }
    rt->has_last_options = true; rt->last_options = current_options; rt->has_last_time = false;
    rt->sent_lease = NULL;
  }
  bool editing = false; /* :281-286 */
  if (host->editor_is_active) {
    const char *problem = NULL;
    bool active = false;
    if (!host->editor_is_active(host->user, &active, &problem))
      return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, problem ? problem : "Placement editor state unavailable");
    editing = active;
  }
  TfpLocomotionSample sample;
  if (editing) { /* :287-296 */
    rt->profile_lease = NULL;
    bool authored = false; /* not in retail (round 20): the ADS draft drawn live in full aim */
    if (host->editor_preview_ads) {
      const char *why = NULL;
      if (!host->editor_preview_ads(host->user, lease, &authored, &why)) return NEUTRAL(TFP_MOTION_PHASE_EDITOR_FROZEN, why ? why : "ADS placement preview refused");
    }
    TfpActionPlacement preview = {0, 0, 0, 0, 0, 0, 0};
    TfpMotionRuntimeGot got = host->editor_preview_rotation ? host->editor_preview_rotation(host->user, &preview) : TFP_MOTION_GOT_NIL;
    if (got == TFP_MOTION_GOT_RAISED) return TFP_MOTION_RUNTIME_RAISED;
    double yaw = field(&preview, TFP_PLACE_YAW, preview.yaw), pitch = field(&preview, TFP_PLACE_PITCH, preview.pitch),
           roll = field(&preview, TFP_PLACE_ROLL, preview.roll);
    if (got == TFP_MOTION_GOT_NIL || (yaw == 0 && pitch == 0 && roll == 0)) return NEUTRAL(TFP_MOTION_PHASE_EDITOR_FROZEN, NULL);
    const char *why = NULL;
    if (!host->sample(host->user, lease, &sample, &why)) return NEUTRAL(TFP_MOTION_PHASE_EDITOR_FROZEN, why);
    TfpMotionRuntimeModifier parent;
    const char *error = NULL;
    if (!to_parent(rt, 0, 0, 0, pitch * RAD, yaw * RAD, roll * RAD, &sample, &parent, &error) || !vector(parent.axis))
      return NEUTRAL(TFP_MOTION_PHASE_EDITOR_FROZEN, "Rotation preview basis unavailable");
    if (authored) parent.authored_ads = true;
    apply(rt, lease, parent, TFP_MOTION_PHASE_EDITOR_FROZEN, NULL, true, false);
    return answered(rt, reason);
  }
  if (!host->action_placement && (!on || scale == 0) && (state->has_shoulder_weight ? state->shoulder_weight : 0) == 0) { /* :297-308 */
    bool controller_active = false, shoulder_aim = false;
    if (host->player_snapshot(host->user, &controller_active, &shoulder_aim)) {
      state->ads = tri(controller_active); state->shoulder_aim = tri(shoulder_aim);
      if (!controller_active && !shoulder_aim && rt->previous_ads != TFP_MOTION_TRI_TRUE && !rt->handoff.has)
        return NEUTRAL(TFP_MOTION_PHASE_DISABLED, NULL);
    }
  }
  ok = host->clock(host->user, &timestamp); /* :309-311 */
  if (!ok || !finite_number(timestamp)) return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, "Motion clock unavailable");
  if (rt->has_last_time && timestamp < rt->last_time) return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, "Motion clock moved backwards");
  const char *problem = NULL;
  if (!host->sample(host->user, lease, &sample, &problem)) { /* :312-316 */
    state->read_failures = state->read_failures + 1;
    return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, problem ? problem : "Motion sample unavailable");
  }
  double dt = rt->has_last_time ? timestamp - rt->last_time : 1.0 / 60; /* :317 */
  if (host->action_placement) { /* :318-363 */
    const TfpActionMapping *mapping = NULL;
    if (host->action_mapping) {
      TfpMotionRuntimeGot got = host->action_mapping(host->user, &mapping);
      if (got == TFP_MOTION_GOT_RAISED) return TFP_MOTION_RUNTIME_RAISED;
      if (got == TFP_MOTION_GOT_NIL) mapping = NULL;
    }
    const TfpActionSample selected = {
      sample.ads ? TFP_ACTION_ADS_TRUE : TFP_ACTION_ADS_OTHER, 0, sample.has_action ? sample.action_resource : NULL,
      sample.has_action && sample.action_verified ? TFP_VERIFIED_TRUE : TFP_VERIFIED_FALSY, sample.stance_flags, sizeof sample.stance_flags,
      sample.reload, sample.shoulder_aim, sample.sprint, sample.has_action && sample.has_l1, sample.has_action && sample.has_l3, sample.l1,
      sample.l3};
    TfpAct action = tfp_action_select(&selected, mapping); /* :320 */
    if (host->presentation_action) host->presentation_action(host->user, action, timestamp);
    state->action = action;
    double revision = host->action_placement_revision; /* :323 */
    bool first_profile = rt->profile_lease != lease;
    if (rt->profile_lease != lease || rt->profile_revision != revision) { /* :325-330 */
      TfpActionPlacement ads;
      TfpMotionRuntimeGot got = host->action_placement(host->user, host->placement_weapon_key, TFP_ACT_ADS, &ads);
      if (got == TFP_MOTION_GOT_RAISED) return TFP_MOTION_RUNTIME_RAISED;
      const char *why = NULL;
      if (!host->set_ads_placement(host->user, lease, got == TFP_MOTION_GOT_VALUE ? &ads : NULL, &why))
        return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, why);
      rt->profile_action = TFP_ACT_NONE;
    }
    if (rt->profile_lease != lease || rt->profile_revision != revision || rt->profile_action != action) { /* :331-342 */
      TfpMotionRuntimePlacement target = {false, {0, 0, 0, 0, 0, 0, 0}};
      TfpMotionRuntimeGot got = TFP_MOTION_GOT_NIL;
      if (action == TFP_ACT_ADS) got = host->controller_get_placement(host->user, &target.value);
      if (got == TFP_MOTION_GOT_NIL) got = host->action_placement(host->user, host->placement_weapon_key, action, &target.value);
      if (got == TFP_MOTION_GOT_RAISED) return TFP_MOTION_RUNTIME_RAISED;
      target.has = got == TFP_MOTION_GOT_VALUE;
      rt->profile_target = target;
      if (action == TFP_ACT_ADS) { /* :334-339 */
        TfpActionPlacement ads;
        got = host->action_placement(host->user, host->placement_weapon_key, TFP_ACT_ADS, &ads);
        if (got != TFP_MOTION_GOT_VALUE || !rt->profile_target.has) return TFP_MOTION_RUNTIME_RAISED; /* a raise, or ads.yaw / profile_target.yaw of nil */
        rt->profile_target.value.yaw = field(&ads, TFP_PLACE_YAW, ads.yaw);
        rt->profile_target.value.pitch = field(&ads, TFP_PLACE_PITCH, ads.pitch);
        rt->profile_target.value.roll = field(&ads, TFP_PLACE_ROLL, ads.roll);
        rt->profile_target.value.present |= TFP_PLACE_YAW | TFP_PLACE_PITCH | TFP_PLACE_ROLL;
      }
      TfpMotionRuntimePlacement value = {false, {0, 0, 0, 0, 0, 0, 0}};
      got = host->controller_get_placement(host->user, &value.value); /* :340 */
      if (got == TFP_MOTION_GOT_RAISED) return TFP_MOTION_RUNTIME_RAISED;
      value.has = got == TFP_MOTION_GOT_VALUE;
      rt->profile_value = value;
      rt->profile_lease = lease; rt->profile_revision = revision; rt->profile_action = action;
    }
    TfpTransitionPacket packet; /* :343-350 */
    bool has_packet = shared_phase(rt, &packet);
    bool spent = has_packet && rt->has_profile_epoch && rt->profile_epoch == packet.epoch && rt->profile_used >= 1;
    double blend = has_packet && (!spent || first_profile) ? phase_step(rt, &packet) : (1 - exp(-lua_max(0, lua_min(dt, .1)) / .10));
    if (!rt->profile_value.has || !rt->profile_target.has) return TFP_MOTION_RUNTIME_RAISED; /* :352, indexing nil */
    const TfpActionPlacement *from = &rt->profile_value.value, *to = &rt->profile_target.value;
    const double befores[6] = {from->x, from->y, from->z, from->yaw, from->pitch, from->roll};
    const double targets[6] = {to->x, to->y, to->z, to->yaw, to->pitch, to->roll};
    double next_value[6];
    bool changed = false;
    for (int axis = 0; axis < 6; axis++) { /* :351-357 */
      double before = from->present & (1u << axis) ? befores[axis] : 0, target = to->present & (1u << axis) ? targets[axis] : 0;
      bool angular = axis >= 3;
      double value = action == TFP_ACT_ADS && angular ? target : (fabs(before - target) < .0001 ? target : before + (target - before) * blend);
      next_value[axis] = value;
      if (before != value) changed = true;
    }
    if (changed) { /* :358-362 */
      const TfpActionPlacement next = {next_value[0], next_value[1], next_value[2], next_value[3], next_value[4], next_value[5], TFP_PLACE_ALL};
      const char *why = NULL;
      if (!host->controller_set_placement(host->user, &next, &why)) return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, why);
      rt->profile_value.value = next;
    }
  }
  state->sprint_raw = tri(sample.sprint); /* :364-366 */
  state->has_l1 = sample.has_action && sample.has_l1; state->l1 = sample.l1;
  state->has_l3 = sample.has_action && sample.has_l3; state->l3 = sample.l3;
  state->has_l14 = sample.has_action && sample.has_l14; state->l14 = sample.l14;
  state->ads = tri(sample.ads); state->shoulder_aim = tri(sample.shoulder_aim);
  state->has_sprint_evidence = true;
  bool ads = sample.ads; /* :367 */
  TfpMotionOutput output;
  bool full; /* false: the held clock's answer, which has the six motion fields only */
  if (dt == 0) { /* :369-375 */
    state->time_held = state->time_held + 1;
    if (rt->previous_ads == tri(ads)) return TFP_MOTION_RUNTIME_TRUE;
    if (ads) tfp_motion_math_clear_ads(&rt->model);
    memset(&output, 0, sizeof output);
    full = false;
  } else { /* :376-379 */
    rt->has_last_time = true; rt->last_time = timestamp;
    if (!rt->has_owner || !tfp_same_player(&rt->owner, &sample.player)) {
      rt->has_owner = true; rt->owner = sample.player; rt->owner_key = rt->owner_key + 1;
    }
    const TfpMotionSample moved = {true, rt->owner_key, sample.x, sample.y, sample.z, sample.yaw, sample.pitch,
                                   sample.ads ? TFP_MOTION_ADS_TRUE : TFP_MOTION_ADS_OTHER, 0, sample.shoulder_aim, sample.sprint, false, false};
    output = tfp_motion_math_update(&rt->model, &moved, dt);
    full = true;
  }
  /* :380: the model always answers a table. */
  if (full && output.reset != TFP_MOTION_RESET_NONE && !resumed_modifier) { /* :381-383 */
    rt->handoff.has = false; rt->previous_ads = TFP_MOTION_TRI_NIL; rt->sent_lease = NULL; rt->has_handoff_epoch = false;
  }
  if (rt->profile_value.has) { /* :384-388 */
    const TfpActionPlacement *p = &rt->profile_value.value;
    output.yaw = output.yaw + field(p, TFP_PLACE_YAW, p->yaw) * RAD;
    output.pitch = output.pitch + field(p, TFP_PLACE_PITCH, p->pitch) * RAD;
    output.roll = output.roll + field(p, TFP_PLACE_ROLL, p->roll) * RAD;
  }
  TfpMotionRuntimeModifier parent;
  const char *error = NULL;
  bool converted = to_parent(rt, output.dx, output.dy, output.dz, output.pitch, output.yaw, output.roll, &sample, &parent, &error); /* :389 */
  if (!converted || !vector(parent.axis)) { /* :390-393 */
    state->read_failures = state->read_failures + 1;
    return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, converted ? "Converted motion exceeds native bounds" : error);
  }
  parent.authored_ads = parent.axis[TFP_MOTION_QX] != 0 || parent.axis[TFP_MOTION_QY] != 0 || parent.axis[TFP_MOTION_QZ] != 0 ||
                        parent.axis[TFP_MOTION_QW] != 1; /* :396 */
  handoff_parent(rt, lease, &parent, ads, timestamp); /* :397 */
  if (!vector(parent.axis)) return NEUTRAL(TFP_MOTION_PHASE_NEUTRAL, "ADS handoff exceeds native bounds"); /* :398 */
  state->has_speed = full; state->speed = output.speed; /* :400-403 */
  state->has_gait_phase = full; state->gait_phase = output.phase;
  state->has_reset = full; state->reset = output.reset;
  state->has_rebased = full; state->rebased = output.rebased;
  state->has_sprint_weight = full; state->sprint_weight = output.sprint_weight;
  state->sprint_target = full ? tri(output.sprint_target) : TFP_MOTION_TRI_NIL;
  state->sprint_held = full ? tri(output.sprint_held) : TFP_MOTION_TRI_NIL;
  state->has_bob_weight = full; state->bob_weight = output.bob_weight;
  state->has_shoulder_weight = full; state->shoulder_weight = output.shoulder_weight;
  apply(rt, lease, parent, TFP_MOTION_PHASE_ACTIVE, NULL, false, rt->handoff.has); /* :404 */
  return answered(rt, reason);
}
