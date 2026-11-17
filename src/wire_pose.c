/* See wire_pose.h. "pose:N" is retail src/first_person/pose.lua, "backend:N" backend.lua, "rt:N"
 * runtime.lua, "ctl:N" controller.lua, all at 0.5.41-smooth.66. No Windows.
 *
 * Every function of the TfpOpsPoseHelper runs on the tick (the backend calls it inside the runtime's
 * update), so it may log. Nothing here is reached from a hook. */
#include "wire_pose.h"
#include "wire_off.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef TfpRtValue V;
#define POSE(w) ((TfpWirePose *)(w)->ext[TFP_WIRE_EXT_POSE])
#define HANDLE_OF(pointer) ((TfpWirePoseHandle *)((char *)(pointer) - offsetof(TfpWirePoseHandle, pose)))

const TfpPoseHelperApi *tfp_wire_pose_linked(void) {
  static const TfpPoseHelperApi api = {
      tfp_pose_validate, tfp_pose_validate_input, tfp_pose_begin, tfp_pose_begin_steady, tfp_pose_begin_input, tfp_pose_begin_steady_input,
      tfp_pose_set_placement, tfp_pose_set_ads_placement, tfp_pose_set_transform, tfp_pose_set_authored_transform, tfp_pose_set_zoom_policy,
      tfp_pose_set_transition, tfp_pose_end, tfp_pose_read_stats, tfp_pose_read_depth_stats, tfp_pose_read_input_stats, tfp_pose_error};
  return &api;
}

/* ---- the session log -------------------------------------------------------------------------- */
static void say(TfpWirePose *p, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void say(TfpWirePose *p, const char *format, ...) {
  char line[420];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  tfp_wire_say(p->w, line);
}
/* A refusal is written once while it stays the same. */
static void refused(TfpWirePose *p, const char *what, const char *text) {
  char line[sizeof p->refusal];
  snprintf(line, sizeof line, "Refused: pose: %s: %s", what, text ? text : "no reason given");
  if (!strcmp(line, p->refusal)) return;
  memcpy(p->refusal, line, sizeof line);
  tfp_wire_say(p->w, line);
}
/* The helper's error numbers (native_ads_editor.cpp:83-86), worded as pose.lua:9-21 words them. */
const char *tfp_wire_pose_error_text(uint32_t code) {
  static const char *const names[] = {
      "no error", "invalid pose configuration", "another native helper lease is reserved", "required memory is unreadable",
      "game or executable build changed", "native instruction fingerprint changed", "engine API pointers changed",
      "player or camera ownership changed", "model or weapon generation changed", "native helper could not remain resident",
      "engine API slot is not writable", "engine API slot ownership changed", "engine API pointer update failed",
      "owned pointer restoration is still pending", "a nonzero native sight node is unavailable",
      "arms/weapon branch or separate camera hierarchy changed", "native branch position is unavailable or invalid",
      "native editor generation no longer belongs to this lease", "native editor deadline has expired"};
  return code < sizeof names / sizeof *names ? names[code] : "unknown";
}

/* ---- conversions -------------------------------------------------------------------------------- */
static void text_out(TfpOpsText *why, const char *text) {
  why->present = text != NULL;
  snprintf(why->text, sizeof why->text, "%s", text ? text : "");
}
/* A Lua number stored into a uint32_t field of the FFI structure (pose:146): toward zero, modulo 2^32. */
static uint32_t u32_of(double value) {
  if (!(value == value) || value >= 18446744073709551616.0 || value <= -9223372036854775808.0) return 0;
  return value < 0 ? (uint32_t)(int64_t)value : (uint32_t)(uint64_t)value;
}
/* backend:839-857 (_pose_config over _alignment_config) as pose:141-149 reads it. An alignment
 * configuration (pose false) has none of the six pose fields: pose.lua's assert names the first. */
void tfp_wire_pose_config(const TfpOpsHelperConfig *from, TfpPoseAdapterConfig *out) {
  memset(out, 0, sizeof *out);
  out->game = from->game, out->exe = from->exe, out->context = from->context, out->request = from->request;
  out->avatar_owner = from->avatar_owner, out->avatar_index = from->avatar_index, out->avatar = from->avatar;
  out->weapon = from->weapon, out->body = from->body, out->viewmodel = from->viewmodel;
  out->duration_ms = u32_of(from->duration_ms);
  if (!from->pose) {
    out->missing = 0x3fu << 11; /* weapon_descriptor, weapon_unit, blend_ms, offset_x, offset_y, offset_z */
    return;
  }
  out->weapon_descriptor = from->weapon_descriptor, out->weapon_unit = from->weapon_unit;
  out->blend_ms = u32_of(from->blend_ms);
  out->offset_x = from->offset_x, out->offset_y = from->offset_y, out->offset_z = from->offset_z;
  out->steady_entry = from->steady_entry;
}
/* The statistics table of pose:99-139 as backend:1118-1127 reads it: armed / expired are Booleans
 * there (the backend accepts true or 1), error is always a number. */
static void stats_of(const TfpPoseStatsView *view, TfpOpsHelperStats *out) {
  memset(out, 0, sizeof *out);
  out->armed = view->armed ? 1 : 0, out->expired = view->expired ? 1 : 0;
  out->has_error = true, out->error = view->error;
  out->target_weight = view->target_weight, out->blend = view->blend;
}
/* The adapter's handle behind the backend's. NULL is nil. A handle of another helper is "not owned"
 * to every adapter call: a handle whose owner is nobody. */
static TfpPoseHandle *pose_handle(TfpWirePose *p, TfpOpsHandle *handle) {
  static TfpPoseHandle foreign;
  if (!handle) return NULL;
  return handle->owner == &p->helper ? &((TfpWirePoseHandle *)handle)->pose : &foreign;
}
static TfpWirePoseHandle *owned(TfpWirePose *p) {
  TfpPoseHandle *handle = tfp_pose_adapter_owned_handle(&p->adapter);
  return handle ? HANDLE_OF(handle) : NULL;
}
static TfpOpsResult answer(TfpPoseAnswer r) { return r == TFP_POSE_TRUE ? TFP_OPS_TRUE : r == TFP_POSE_RAISED ? TFP_OPS_RAISED : TFP_OPS_FALSE; }

/* ---- TfpOpsPoseHelper: the Lua adapter's methods as backend.lua calls them ------------------------ */
/* pose:212-222: (true, token) / (false, why) / a raised assertion. */
static TfpOpsResult op_prepare(void *u, const TfpOpsHelperConfig *c, void **native, TfpOpsText *why) {
  TfpWirePose *p = u;
  TfpPoseAdapterConfig config;
  const char *text = NULL;
  tfp_wire_pose_config(c, &config);
  TfpPoseAnswer r = tfp_pose_adapter_prepare(&p->adapter, &config, &p->prepared, &text);
  *native = r == TFP_POSE_TRUE ? &p->prepared : NULL;
  text_out(why, text);
  if (r != TFP_POSE_TRUE) refused(p, "read-only validation", text);
  else if (!p->validated) {
    p->validated = true;
    say(p, "Ready: pose: the helper's read-only validation passed (owner, rig graph, the four dispatch slots and their code)");
  }
  return answer(r);
}
/* pose:224-249: (true, handle) / (false, why) / raised. A generation that came back with a refusal
 * stays owned by the adapter; the backend then asks owned_handle() and keeps a cleanup-only lease
 * (backend:604-625). */
static TfpOpsResult op_begin(void *u, const TfpOpsHelperConfig *c, void *native, TfpOpsHandle **handle, TfpOpsText *why) {
  TfpWirePose *p = u;
  TfpPoseAdapterConfig config;
  const char *text = NULL;
  *handle = NULL;
  TfpWirePoseHandle *h = calloc(1, sizeof *h);
  if (!h) {
    text_out(why, "Out of memory for a pose handle");
    return TFP_OPS_FALSE;
  }
  h->ops.owner = &p->helper;
  tfp_wire_pose_config(c, &config);
  h->steady = config.steady_entry;
  TfpPoseAnswer r = tfp_pose_adapter_begin(&p->adapter, &config, native, &h->pose, &text);
  text_out(why, text);
  if (r != TFP_POSE_TRUE) {
    bool kept = tfp_pose_adapter_owned_handle(&p->adapter) == &h->pose;
    refused(p, kept ? "begin (the generation is owned and will be cleaned up)" : "begin", text);
    if (!kept) free(h); /* the adapter never took it: nobody can name it */
    return answer(r);
  }
  *handle = &h->ops;
  p->hip[0] = config.offset_x, p->hip[1] = config.offset_y, p->hip[2] = config.offset_z;
  memset(p->ads, 0, sizeof p->ads);
  memset(p->motion, 0, sizeof p->motion);
  p->q[0] = p->q[1] = p->q[2] = 0, p->q[3] = 1;
  p->refusal[0] = 0, p->error_said = 0;
  p->meter.begins++;
  double size = sqrt(p->hip[0] * p->hip[0] + p->hip[1] * p->hip[1] + p->hip[2] * p->hip[2]);
  say(p, "Pose: begin generation %llu, %s, placement (%.3f, %.3f, %.3f) = %.4f m, duration %u ms, blend %u ms",
      (unsigned long long)h->pose.generation,
      h->steady ? "steady entry (the placement is there at once, the draw animation covers it)" : "blended entry (the placement fades in)",
      p->hip[0], p->hip[1], p->hip[2], size, (unsigned)config.duration_ms, (unsigned)config.blend_ms);
  return TFP_OPS_TRUE;
}
/* pose:320: self.owned (false or the handle). */
static TfpOpsResult op_owned(void *u, TfpOpsHandle **handle, TfpOpsText *why) {
  TfpWirePose *p = u;
  TfpWirePoseHandle *h = owned(p);
  text_out(why, NULL);
  *handle = h ? &h->ops : NULL;
  return TFP_OPS_TRUE;
}
/* pose:322-334: (true, stats or nil) / (false, why). */
static TfpOpsResult op_finish(void *u, TfpOpsHandle *handle, bool *has_stats, TfpOpsHelperStats *stats, TfpOpsText *why) {
  TfpWirePose *p = u;
  TfpPoseStatsView view;
  const char *text = NULL;
  TfpPoseHandle *h = pose_handle(p, handle);
  bool was_armed = h && !h->disarmed;
  *has_stats = false;
  memset(stats, 0, sizeof *stats);
  if (!tfp_pose_adapter_finish(&p->adapter, h, &view, &text)) {
    text_out(why, text);
    refused(p, "finish", text);
    return TFP_OPS_FALSE;
  }
  text_out(why, NULL);
  handle->disarmed = true; /* finish answers true only for a handle of this adapter */
  *has_stats = view.has;
  if (view.has) stats_of(&view, stats);
  if (was_armed) {
    p->meter.finishes++;
    if (view.has)
      say(p, "Pose: finish generation %llu: %.0f shifted rig writes, %.0f restores, helper error %u (%s)", (unsigned long long)h->generation,
          view.weapon_updates, view.zero_restores, (unsigned)view.error, tfp_wire_pose_error_text(view.error));
    else say(p, "Pose: finish generation %llu: no final statistics", (unsigned long long)h->generation);
  }
  return TFP_OPS_TRUE;
}
/* pose:99-139: the table, or (nil, why), or the final statistics of a disarmed handle (nil when it has none). */
static TfpOpsResult op_stats(void *u, TfpOpsHandle *handle, TfpOpsHelperStats *stats, TfpOpsText *why) {
  TfpWirePose *p = u;
  TfpPoseStatsView view;
  const char *text = NULL;
  TfpPoseHandle *h = pose_handle(p, handle);
  memset(stats, 0, sizeof *stats);
  if (!tfp_pose_adapter_stats(&p->adapter, h, &view, &text)) {
    text_out(why, text);
    if (text) refused(p, "statistics", text);
    return TFP_OPS_NIL;
  }
  text_out(why, NULL);
  stats_of(&view, stats);
  if (handle && handle->owner == &p->helper && !handle->disarmed) {
    p->polled = true, p->has_view = true, p->view = view;
    p->polled_handle = (const TfpWirePoseHandle *)handle;
    if (view.error != p->error_said) {
      p->error_said = view.error;
      if (view.error)
        say(p, "Refused: pose: helper error %u (%s); armed=%d expired=%d installed=%d", (unsigned)view.error, tfp_wire_pose_error_text(view.error),
            view.armed, view.expired, view.installed);
    }
  }
  return TFP_OPS_TRUE;
}
static TfpOpsResult setter(TfpWirePose *p, bool done, const char *what, const char *text, TfpOpsText *why) {
  text_out(why, done ? NULL : text);
  if (!done) refused(p, what, text);
  return done ? TFP_OPS_TRUE : TFP_OPS_FALSE;
}
/* pose:251-261 */
static TfpOpsResult op_set_placement(void *u, TfpOpsHandle *handle, double x, double y, double z, TfpOpsText *why) {
  TfpWirePose *p = u;
  const char *text = NULL;
  bool done = tfp_pose_adapter_set_placement(&p->adapter, pose_handle(p, handle), &x, &y, &z, &text);
  if (done) p->hip[0] = x, p->hip[1] = y, p->hip[2] = z, p->meter.placements++;
  return setter(p, done, "placement", text, why);
}
/* pose:300-309 */
static TfpOpsResult op_set_zoom_policy(void *u, TfpOpsHandle *handle, bool active, TfpOpsText *why) {
  TfpWirePose *p = u;
  const char *text = NULL;
  bool done = tfp_pose_adapter_set_zoom_policy(&p->adapter, pose_handle(p, handle), &active, &text);
  return setter(p, done, "zoom policy", text, why);
}
/* pose:262-272 */
static TfpOpsResult op_set_ads_placement(void *u, TfpOpsHandle *handle, double x, double y, double z, TfpOpsText *why) {
  TfpWirePose *p = u;
  const char *text = NULL;
  bool done = tfp_pose_adapter_set_ads_placement(&p->adapter, pose_handle(p, handle), &x, &y, &z, &text);
  if (done) p->ads[0] = x, p->ads[1] = y, p->ads[2] = z, p->meter.placements++;
  return setter(p, done, "ADS placement", text, why);
}
/* pose:311-318. The packet is transition.lua's; pose.lua looks at epoch, progress and ads. */
static TfpOpsResult op_transition(void *u, TfpOpsHandle *handle, const TfpOpsPhase *token, TfpOpsText *why) {
  TfpWirePose *p = u;
  TfpPosePhasePacket packet;
  const char *text = NULL;
  if (token) {
    packet.has_epoch = token->has_epoch, packet.epoch = token->epoch;
    packet.has_progress = token->has_progress, packet.progress = token->progress;
    packet.has_ads = token->ads == 0 || token->ads == 1, packet.ads = token->ads == 1;
  }
  bool done = tfp_pose_adapter_transition(&p->adapter, pose_handle(p, handle), token ? &packet : NULL, &text);
  return setter(p, done, "transition", text, why);
}
/* pose:274-298. backend:1018 always hands four quaternion numbers over (it has defaulted them). */
static TfpOpsResult op_set_motion(void *u, TfpOpsHandle *handle, double dx, double dy, double dz, double qx, double qy, double qz, double qw,
                                  bool authored_ads, TfpOpsText *why) {
  TfpWirePose *p = u;
  const char *text = NULL;
  bool done = tfp_pose_adapter_set_motion(&p->adapter, pose_handle(p, handle), &dx, &dy, &dz, &qx, &qy, &qz, &qw, authored_ads, &text);
  if (done) {
    p->motion[0] = dx, p->motion[1] = dy, p->motion[2] = dz;
    p->q[0] = qx, p->q[1] = qy, p->q[2] = qz, p->q[3] = qw;
    p->meter.motions++;
  }
  return setter(p, done, "motion", text, why);
}

/* ---- the runtime's side ------------------------------------------------------------------------- */
/* rt:377, rt:1136, rt:1147: `helper.depth_stats` / `helper.input_stats` are functions of the pose
 * adapter, so every lease whose helper is this one has them. */
static void lease_extra(TfpWire *w, const TfpWireLease *lease, TfpRtTable *out) {
  TfpWirePose *p = POSE(w);
  if (!p || !lease->ops.has_alignment || lease->ops.alignment.helper != &p->helper) return;
  out->f[TFP_RT_LEASE_DEPTH_STATS] = tfp_wire_bool(true);
  out->f[TFP_RT_LEASE_INPUT_STATS] = tfp_wire_bool(true);
}
/* ctl:94, ctl:148-155: result().blend is summary.blend, the helper's hip blend of the last
 * statistics the controller took; nil until the session's first. */
static void controller_extra(TfpWire *w, TfpRtTable *out) {
  out->f[TFP_RT_STATE_BLEND] = w->controller.has_blend ? tfp_wire_number(w->controller.blend) : tfp_wire_nil();
}
/* The handle a LEASE table carries (tfp_wire_lease_table: the backend's handle, or nil). */
static TfpPoseHandle *value_handle(TfpWirePose *p, V handle) {
  return handle.kind == TFP_RT_REF && handle.ref ? pose_handle(p, (TfpOpsHandle *)(uintptr_t)handle.ref) : NULL;
}
static V why_value(TfpWire *w, const char *why) { return why ? tfp_wire_text(tfp_wire_keep(w, why)) : tfp_wire_nil(); }
/* pose:153-180 through rt:378 and rt:1139: the table, or (nil, why). */
static bool rt_depth_stats(TfpRtHost *h, const TfpRtTable *lease, V handle, TfpRtTable *o1, V *o2) {
  TfpWire *w = h->user;
  TfpWirePose *p = POSE(w);
  const char *why = NULL;
  (void)lease;
  memset(o1, 0, sizeof *o1);
  *o2 = tfp_wire_nil();
  if (!tfp_pose_adapter_depth_stats(&p->adapter, value_handle(p, handle), &p->depth, &why)) {
    *o2 = why_value(w, why);
    return true;
  }
  o1->present = true;
  o1->ref = &p->depth;
  o1->f[TFP_RT_DEPTH_SUPPORTED] = tfp_wire_bool(p->depth.supported);
  o1->f[TFP_RT_DEPTH_ARMED] = tfp_wire_bool(p->depth.armed);
  o1->f[TFP_RT_DEPTH_ZOOM_INDEX] = tfp_wire_number(p->depth.zoom_index);
  return true;
}
/* pose:183-210 through rt:1150: the table (the runtime only hands it on), or (nil, why). */
static bool rt_input_stats(TfpRtHost *h, const TfpRtTable *lease, V handle, V *o1, V *o2) {
  TfpWire *w = h->user;
  TfpWirePose *p = POSE(w);
  const char *why = NULL;
  (void)lease;
  *o1 = *o2 = tfp_wire_nil();
  if (tfp_pose_adapter_input_stats(&p->adapter, value_handle(p, handle), &p->input, &why)) *o1 = tfp_wire_ref(&p->input);
  else *o2 = why_value(w, why);
  return true;
}
/* backend:1023-1068 through the debug entry `preflight` (rt:1189): (true, token) / (false, why). */
static bool rt_prepare_pose(TfpRtHost *h, const TfpRtTable *snapshot, V *o1, V *o2) {
  TfpWire *w = h->user;
  TfpWirePose *p = POSE(w);
  TfpWireSnap *s = tfp_wire_snap_of(w, snapshot);
  TfpOpsText why = {false, ""};
  TfpOpsResult r = tfp_ops_prepare_pose(&w->ops, s ? &s->s : NULL, &p->preflight, &why);
  if (r == TFP_OPS_RAISED) return tfp_wire_raise(h, why.text);
  *o1 = r == TFP_OPS_TRUE ? tfp_wire_bool(true) : r == TFP_OPS_NIL ? tfp_wire_nil() : tfp_wire_bool(false);
  *o2 = r != TFP_OPS_TRUE && why.present ? tfp_wire_text(tfp_wire_keep(w, why.text)) : tfp_wire_nil();
  return true;
}

void tfp_wire_pose_bind(TfpWire *w, TfpWirePose *state, const TfpPoseHelperApi *api, const TfpPoseAdapterIo *io,
                        TfpPoseAdapterProfile profile) {
  memset(state, 0, sizeof *state);
  state->w = w;
  state->io = *io;
  tfp_pose_adapter_init(&state->adapter, api, &state->io, profile); /* Pose.new(Profile.pose), backend:807 */
  state->helper = (TfpOpsPoseHelper){state, op_prepare, op_begin, op_owned, op_finish, op_stats, op_set_placement, op_set_zoom_policy,
                                     op_set_ads_placement, op_transition, op_set_motion};
  state->q[3] = 1;
  state->look_said = -1;
  snprintf(state->look, sizeof state->look, "%s", profile.normalize_zoom_input ? "-" : "off");
  w->ext[TFP_WIRE_EXT_POSE] = state;
  w->pose_helper = &state->helper;
  w->lease_extra = lease_extra;
  w->controller_extra = controller_extra;
  w->host.helper_depth_stats = rt_depth_stats;
  w->host.helper_input_stats = rt_input_stats;
  w->host.backend_prepare_pose = rt_prepare_pose;
  say(state, "Ready: pose helper bound (placement, ADS placement, motion, transitions, draw and entry blend, zoom depth); look gain %s",
      profile.normalize_zoom_input ? "asked for as retail does (the helper decides)" : "not asked for");
  if (!profile.normalize_zoom_input) {
    state->look_said = 0;
    say(state, "Refused: look gain: switched off in this build's pose profile; scoped hip sensitivity stays the game's own");
  }
}

bool tfp_wire_pose_counts(const TfpWire *w, double *weapon_updates, double *zero_restores) {
  const TfpWirePose *p = POSE(w);
  if (!p || !p->has_view) return false;
  *weapon_updates = p->view.weapon_updates, *zero_restores = p->view.zero_restores;
  return true;
}

/* ---- the weapon placement jump meter (wire_pose.h) ------------------------------------------------ */
static double turn_degrees(const double a[4], const double b[4]) {
  double dot = fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
  return dot >= 1 ? 0 : 2 * acos(dot) * (180 / 3.14159265358979323846);
}
void tfp_wire_pose_frame(TfpWire *w) {
  TfpWirePose *p = POSE(w);
  if (!p) return;
  TfpWirePoseMeter *m = &p->meter;
  bool polled = p->polled;
  p->polled = false;
  m->frames++;
  if (!polled || !p->view.armed || p->view.expired || p->polled_handle != owned(p)) {
    m->handle = NULL;
    return;
  }
  double shift[3], step[3], jump = 0, turn = 0;
  for (int i = 0; i < 3; i++) {
    shift[i] = (p->hip[i] + p->motion[i]) * p->view.blend;
    if (p->view.target_weight == 0) shift[i] += p->ads[i]; /* full ADS: the helper adds the ADS placement whole */
  }
  m->sampled++;
  m->blend = p->view.blend, m->has_blend = true;
  if (m->handle == p->polled_handle) {
    for (int i = 0; i < 3; i++) {
      step[i] = shift[i] - m->shift[i];
      jump += (step[i] - m->step[i]) * (step[i] - m->step[i]);
    }
    jump = sqrt(jump);
    turn = turn_degrees(p->q, m->q);
    if (jump > m->jump_m) m->jump_m = jump;
    if (fabs(turn - m->turn) > m->jump_deg) m->jump_deg = fabs(turn - m->turn);
    memcpy(m->step, step, sizeof step);
  } else memset(m->step, 0, sizeof m->step); /* a lease's first sample: nothing moved yet */
  m->turn = turn;
  memcpy(m->shift, shift, sizeof shift);
  memcpy(m->q, p->q, sizeof m->q);
  m->handle = p->polled_handle;
}

/* The look-gain state once a second, from the helper's own input statistics (pose:183-210). The
 * helper proves the look evaluator's code lazily and retries every 250 ms (pose_helper.c
 * input_check_code), so a lease is judged from its second sample on. */
static void look_state(TfpWirePose *p) {
  TfpWirePoseHandle *h = owned(p);
  TfpPoseInputView view;
  const char *why = NULL;
  if (!p->adapter.profile.normalize_zoom_input) return; /* "off", said at bind */
  if (!h || h->pose.disarmed || !tfp_pose_adapter_input_stats(&p->adapter, &h->pose, &view, &why)) {
    snprintf(p->look, sizeof p->look, "-");
    p->look_handle = NULL;
    return;
  }
  if (p->look_handle != h) p->look_handle = h, p->look_seconds = 0;
  p->look_seconds++;
  int state = view.installed && view.active ? 1 : 2 + (int)view.reason;
  if (state == 1) snprintf(p->look, sizeof p->look, "on");
  else snprintf(p->look, sizeof p->look, "refused(%.0f)", view.reason);
  if (state == p->look_said || (state != 1 && p->look_seconds < 2)) return;
  p->look_said = state;
  if (state == 1) say(p, "Ready: look gain: normalisation is active (the look evaluator's code proof passed)");
  else if (view.reason == TFP_POSE_INPUT_CODE_UNAVAILABLE)
    say(p, "Refused: look gain: the look evaluator's code proof is not established on build 25480438 (input reason 2, code "
           "unavailable). Retail's two extra hooks are %s, as in retail when a pin differs, and change no gain; scoped hip sensitivity "
           "stays the game's own.", view.installed ? "installed" : "not installed");
  else if (view.reason == TFP_POSE_INPUT_SDK_UNAVAILABLE)
    say(p, "Refused: look gain: the UnitAPI+0x90 slot could not be taken (input reason 3); scoped hip sensitivity stays the game's own");
  else say(p, "Refused: look gain: input reason %.0f; scoped hip sensitivity stays the game's own", view.reason);
}
size_t tfp_wire_pose_second(TfpWire *w, char *out, size_t size) {
  TfpWirePose *p = POSE(w);
  if (!p || !size) return 0;
  TfpWirePoseMeter *m = &p->meter;
  TfpWirePoseHandle *h = owned(p);
  look_state(p);
  bool current = h && p->has_view && p->polled_handle == h;
  const char *state = !h ? "none" : !current ? "unarmed" : p->view.armed && !p->view.expired ? "armed" : p->view.expired ? "expired" : "unarmed";
  int n = snprintf(out, size, "pose=%s blend=%.2f wjump=%.4fm/%.2fdeg placements=%u motion=%u err=%u writes=%.0f look=%s", state,
                   m->has_blend ? m->blend : 0.0, m->jump_m, m->jump_deg, m->placements, m->motions, current ? (unsigned)p->view.error : 0u,
                   current ? p->view.weapon_updates : 0.0, p->look);
  m->jump_m = m->jump_deg = 0;
  m->frames = m->sampled = m->placements = m->motions = m->begins = m->finishes = 0;
  m->has_blend = false;
  if (n < 0) return 0;
  return (size_t)n < size ? (size_t)n : size - 1;
}
