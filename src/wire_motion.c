/* See wire_motion.h. Signature adapters between the motion runtime's host (typed C), the runtime
 * order's entries (tagged values) and backend_ops. Line references "mr:N" are retail
 * motion_runtime.lua, "loc:N" retail locomotion.lua, "rt:N" retail runtime.lua, "tr:N" retail
 * transition.lua, all at 0.5.41-smooth.66 (1d0d453). No Windows. */
#include "wire_motion.h"
#include "wire_off.h"
#include "wire_settings.h"
#include "wire_editor.h"
#include "editor_motion.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef TfpRtValue V;
#define nil tfp_wire_nil

/* One native-view lease as the motion runtime and the sampler see it. The motion runtime compares
 * leases by address and keeps the address of the one it last ran with (motion_runtime.h): a row is
 * made once per binder lease and, as the binder's own leases, never freed while the binder lives. */
typedef struct MotionLease {
  struct MotionLease *next;
  const TfpWireLease *wire;
  TfpMotionRuntimeLease view;
  TfpSnapshot origin;
} MotionLease;
typedef struct Motion {
  TfpWire *w;
  bool made;                       /* motion_new ran */
  TfpMotionRuntimeHost host;
  TfpMotionRuntime rt;
  MotionLease *leases;             /* newest first */
  FpMemory memory;
  TfpLocomotionBackend sampler;
  /* The snapshots and the action state of the sample being taken, as backend_ops answered them. */
  TfpOpsSnapshot taken[2];
  TfpSnapshot taken_view[2];
  unsigned taken_count;
  TfpOpsAction action;
  bool has_action;
  char raised[TFP_MOTION_RUNTIME_TEXT]; /* the text of the raise a host call passed up */
  char why[TFP_MOTION_RUNTIME_TEXT], sample_why[TFP_LOCOMOTION_ERROR];
  /* the session log */
  TfpAct logged_action;
  unsigned frames;
  double peak_move, peak_turn;
  uint64_t applied, coalesced, softened, refused;
} Motion;
static Motion *state(TfpWire *w) { return w ? w->ext[TFP_WIRE_EXT_MOTION] : NULL; }
#define M(h) state((TfpWire *)(h)->user)

static bool text_is(V v, const char *text) { return v.kind == TFP_RT_TEXT && v.length == strlen(text) && !memcmp(v.text, text, v.length); }
static bool truthy(V v) { return v.kind != TFP_RT_NIL && v.kind != TFP_RT_FALSE; }
static void copy_text(char *out, size_t size, V v, const char *fallback) {
  if (v.kind == TFP_RT_TEXT) snprintf(out, size, "%.*s", (int)(v.length < size - 1 ? v.length : size - 1), v.text);
  else snprintf(out, size, "%s", fallback);
}
/* The value a runtime entry raised (TfpRt.error), kept for the raise this binder passes on. */
static void keep_raise(Motion *m) { copy_text(m->raised, sizeof m->raised, m->w->rt.error, "error in a motion collaborator"); }
static void keep_host_raise(Motion *m) { snprintf(m->raised, sizeof m->raised, "%s", m->w->host.raised ? m->w->host.raised : "error in a motion collaborator"); }

/* ---- the two shapes of a snapshot and of an action state ------------------------------------ */
void tfp_wire_motion_snapshot(const TfpOpsSnapshot *from, TfpSnapshot *out) {
  memset(out, 0, sizeof *out);
#define COPY(field) out->field = (uintptr_t)from->field
  COPY(base); COPY(context); COPY(camera); COPY(record); COPY(world); COPY(controller); COPY(player);
  COPY(avatar_owner); COPY(avatar_entry); COPY(flags_address); COPY(weapon_component_descriptor);
#undef COPY
#define COPY(field) out->field = from->field
  COPY(head); COPY(tail); COPY(camera_kind); COPY(request_avatar); COPY(request_weapon); COPY(local_entity); COPY(avatar);
  COPY(avatar_index); COPY(body); COPY(viewmodel); COPY(weapon); COPY(weapon_unit);
  COPY(controller_active); COPY(blocked); COPY(request_local); COPY(aiming); COPY(shoulder_aim); COPY(seated); COPY(reloading);
  COPY(safe); COPY(engine_safe); COPY(weapon_transition); COPY(head_camera_transition); COPY(weapon_ready); COPY(full);
#undef COPY
  memcpy(&out->mask, from->controller_bytes, 4);
  memcpy(out->controller_bytes, from->controller_bytes, sizeof out->controller_bytes);
  memcpy(out->flags, from->flags, sizeof out->flags);
  memcpy(out->avatar_descriptor, from->avatar_descriptor, sizeof out->avatar_descriptor);
  memcpy(out->weapon_descriptor, from->weapon_descriptor, sizeof out->weapon_descriptor);
}
void tfp_wire_motion_action(const TfpOpsAction *from, const TfpSnapshot *owner, TfpActionState *out) {
  memset(out, 0, sizeof *out);
  if (owner) out->player = *owner;
  out->body = from->body;
  out->body_object = (uintptr_t)from->body_object, out->graph = (uintptr_t)from->graph, out->instance = (uintptr_t)from->instance;
  out->layer_count = from->layer_count;
  for (int i = 0; i < 8; i++) snprintf(out->resource + i * 2, 3, "%02X", from->resource[7 - i]); /* action_state.lua:24-28 */
  out->resource_match = from->resource_match;
  out->sprint_hypothesis = from->sprint_hypothesis;
  for (int layer = 1; layer <= TFP_ACTION_STATE_LAYERS; layer++) {
    if (!(from->sampled >> layer & 1)) continue;
    out->has[layer] = true;
    out->states[layer] = from->states[layer];
    if (from->unknown >> layer & 1) out->unknown_layers[out->unknown_count++] = layer;
  }
}

/* ---- leases ------------------------------------------------------------------------------------ */
static void lease_sync(MotionLease *l) {
  const TfpOpsLease *ops = &l->wire->ops;
  l->view.done = ops->done, l->view.cleanup_only = ops->cleanup_only, l->view.pose_blend_entry = ops->pose_blend_entry == 1;
}
static MotionLease *lease_row(Motion *m, const TfpWireLease *wire) {
  if (!wire) return NULL;
  for (MotionLease *l = m->leases; l; l = l->next) if (l->wire == wire) return l;
  MotionLease *l = calloc(1, sizeof *l);
  if (!l) return NULL;
  l->wire = wire;
  /* lease.origin is the full snapshot the view was entered from; its two identity strings as keys. */
  l->view.has_origin = l->view.has_identity = l->view.has_player_identity = true;
  l->view.identity = tfp_ops_identity_key(&wire->ops.origin), l->view.player_identity = tfp_ops_player_key(&wire->ops.origin);
  tfp_wire_motion_snapshot(&wire->ops.origin, &l->origin);
  l->next = m->leases, m->leases = l;
  return l;
}
static MotionLease *lease_of_view(const TfpMotionRuntimeLease *view) {
  return view ? (MotionLease *)((char *)view - offsetof(MotionLease, view)) : NULL;
}
/* The four fields the motion runtime reads live (motion_runtime.h): refreshed before every call into it. */
static void refresh(Motion *m) {
  TfpWire *w = m->w;
  MotionLease *held = lease_row(m, tfp_wire_held(w)), *alignment = lease_row(m, tfp_wire_lease_of_ops(w->ops.alignment_lease));
  int recent = 0;
  for (MotionLease *l = m->leases; l && recent < 8; l = l->next, recent++) lease_sync(l);
  if (held) lease_sync(held);
  if (alignment) lease_sync(alignment);
  m->host.controller_lease = held ? &held->view : NULL;
  m->host.alignment_lease = alignment ? &alignment->view : NULL;
  m->host.placement_weapon_key = w->ops.has_placement_weapon_key ? w->ops.placement_weapon_key : NULL;
  m->host.action_placement_revision = w->has_revision ? w->revision : 0; /* backend.action_placement_revision or 0 */
}

/* ---- the sampler's backend: locomotion.c over backend_ops ------------------------------------ */
static bool sampler_read(void *user, uintptr_t at, void *out, size_t size) {
  TfpWire *w = user;
  return w->memory.read(w->memory.user, at, out, size);
}
static bool sampler_player_snapshot(void *user, TfpSnapshot *out, char *error, size_t error_size) { /* backend:player_snapshot() */
  Motion *m = user;
  TfpOpsSnapshot s;
  TfpOpsText why = {false, ""};
  if (tfp_ops_player_snapshot(&m->w->ops, &s, &why) != TFP_OPS_TRUE) {
    if (error && error_size) snprintf(error, error_size, "%.*s", (int)error_size - 1, why.present ? why.text : "");
    return false;
  }
  tfp_wire_motion_snapshot(&s, out);
  if (m->taken_count < 2) m->taken[m->taken_count] = s, m->taken_view[m->taken_count] = *out, m->taken_count++;
  return true;
}
static bool sampler_executable(void *user, uintptr_t *exe, char *error, size_t error_size) { /* backend:_alignment_executable(profile.pose) */
  Motion *m = user;
  uint64_t base = 0;
  TfpOpsText why = {false, ""};
  if (!tfp_ops_alignment_executable(&m->w->ops, TFP_OPS_EXECUTABLE_POSE, &base, &why)) {
    if (error && error_size) snprintf(error, error_size, "%.*s", (int)error_size - 1, why.present ? why.text : "");
    return false;
  }
  *exe = (uintptr_t)base;
  return true;
}
/* The backend_ops snapshot behind the copy the sampler hands back: the one it took first (loc:122). */
static const TfpOpsSnapshot *taken(const Motion *m, const TfpSnapshot *s) {
  for (unsigned i = 0; i < m->taken_count; i++) if (!memcmp(&m->taken_view[i], s, sizeof *s)) return &m->taken[i];
  return NULL;
}
static bool sampler_action_state(void *user, const TfpSnapshot *s, TfpActionState *out) { /* backend:action_state(s) */
  Motion *m = user;
  TfpOpsText why;
  const TfpOpsSnapshot *ops = taken(m, s);
  m->has_action = ops && tfp_ops_action_state(&m->w->ops, ops, &m->action, &why) == TFP_OPS_TRUE;
  if (m->has_action) tfp_wire_motion_action(&m->action, s, out);
  return m->has_action;
}
static bool sampler_on_foot(void *user, const TfpSnapshot *s, const TfpActionState *action) { /* backend:on_foot(s, action) */
  Motion *m = user;
  const TfpOpsSnapshot *ops = taken(m, s);
  return ops && tfp_ops_on_foot(&m->w->ops, ops, action && m->has_action ? &m->action : NULL);
}
static bool host_sample(void *user, const TfpMotionRuntimeLease *view, TfpLocomotionSample *out, const char **why) {
  Motion *m = user;
  TfpWire *w = m->w;
  MotionLease *l = lease_of_view(view);
  TfpLocomotionLease lease;
  memset(&lease, 0, sizeof lease);
  if (l) {
    lease_sync(l);
    lease.current = l->wire == tfp_wire_lease_of_ops(w->ops.alignment_lease); /* lease == backend.alignment_lease */
    lease.done = l->view.done, lease.cleanup_only = l->view.cleanup_only;
    lease.origin = &l->origin;
    lease.request = (uintptr_t)l->wire->ops.request;
    lease.rig_object = l->wire->ops.has_alignment ? (uintptr_t)l->wire->ops.alignment.context.object : 0;
  }
  m->memory = (FpMemory){sampler_read, w, (uintptr_t)w->memory.module(w->memory.user, TFP_OPS_GAME),
                         (uintptr_t)w->memory.module(w->memory.user, TFP_OPS_EXE)};
  m->taken_count = 0, m->has_action = false;
  if (tfp_locomotion_sample(&m->sampler, l ? &lease : NULL, out, m->sample_why, sizeof m->sample_why)) return true;
  if (why) *why = m->sample_why;
  return false;
}

bool tfp_wire_motion_sample(TfpWire *w, const TfpWireLease *lease, TfpLocomotionSample *out, const char **why) {
  Motion *m = state(w);
  if (why) *why = NULL;
  if (!m) return false;
  MotionLease *row = lease_row(m, lease);
  return host_sample(m, row ? &row->view : NULL, out, why);
}

/* ---- the motion runtime's host ------------------------------------------------------------------ */
static bool host_clock(void *user, double *seconds) { /* the runtime's monotonic clock (rt:160-173) */
  Motion *m = user;
  *seconds = tfp_wire_clock(m->w);
  return *seconds == *seconds;
}
static void host_log(void *user, const char *text) { tfp_wire_say(((Motion *)user)->w, text); }
static bool host_options(void *user, TfpMotionOptions *out) { /* rt:816-821 */
  Motion *m = user;
  V r[5] = {nil(), nil(), nil(), nil(), nil()};
  if (tfp_rt_call(&m->w->rt, TFP_RT_E_motion_options, NULL, r) != TFP_RT_OK) return false;
  if (r[0].kind != TFP_RT_NIL) return tfp_wire_settings_options_of(m->w, r[0], out); /* the menu's table */
  memset(out, 0, sizeof *out); /* the runtime's defaults: enabled, scale, camera_bob_strength, camera_sway_strength */
  V enabled = r[1 + TFP_RT_DEFAULTS_ENABLED], scale = r[1 + TFP_RT_DEFAULTS_SCALE];
  V bob = r[1 + TFP_RT_DEFAULTS_CAMERA_BOB_STRENGTH], sway = r[1 + TFP_RT_DEFAULTS_CAMERA_SWAY_STRENGTH];
  out->enabled = enabled.kind == TFP_RT_TRUE ? TFP_MOTION_TRUE : enabled.kind == TFP_RT_FALSE ? TFP_MOTION_FALSE
                 : enabled.kind == TFP_RT_NIL ? TFP_MOTION_ABSENT : TFP_MOTION_OTHER;
#define NUMBER(v) ((v).kind == TFP_RT_NUMBER ? (TfpMotionNumber){TFP_MOTION_NUMBER, (v).number} \
                   : (v).kind == TFP_RT_NIL ? (TfpMotionNumber){TFP_MOTION_NIL, 0} : (TfpMotionNumber){TFP_MOTION_NOT_NUMBER, 0})
  out->scale = NUMBER(scale);
  out->setting[TFP_MOTION_CAMERA_BOB_STRENGTH] = NUMBER(bob);
  out->setting[TFP_MOTION_CAMERA_SWAY_STRENGTH] = NUMBER(sway);
#undef NUMBER
  return true;
}
static TfpMotionRuntimeGot host_controller_result(void *user, TfpMotionRuntimeControllerResult *out) { /* controller:result() */
  Motion *m = user;
  TfpRtHost *h = &m->w->host;
  TfpRtTable table;
  memset(&table, 0, sizeof table);
  if (!h->controller_result(h, &table)) { keep_host_raise(m); return TFP_MOTION_GOT_RAISED; }
  if (!table.present) return TFP_MOTION_GOT_NIL;
  out->pose_offset = text_is(table.f[TFP_RT_STATE_MODE], "pose_offset");
  out->running = text_is(table.f[TFP_RT_STATE_PHASE], "running");
  return TFP_MOTION_GOT_VALUE;
}
static TfpMotionRuntimeGot host_controller_is_enabled(void *user) {
  Motion *m = user;
  TfpRtHost *h = &m->w->host;
  V value = nil();
  if (!h->controller_is_enabled(h, &value)) { keep_host_raise(m); return TFP_MOTION_GOT_RAISED; }
  return truthy(value) ? TFP_MOTION_GOT_VALUE : TFP_MOTION_GOT_NIL;
}
static TfpMotionRuntimeGot host_controller_get_placement(void *user, TfpActionPlacement *out) { /* controller:get_placement() */
  Motion *m = user;
  TfpRtHost *h = &m->w->host;
  TfpRtTable table;
  memset(&table, 0, sizeof table);
  if (!h->controller_get_placement(h, &table)) { keep_host_raise(m); return TFP_MOTION_GOT_RAISED; }
  if (!table.present) return TFP_MOTION_GOT_NIL;
  double *const fields[6] = {&out->x, &out->y, &out->z, &out->yaw, &out->pitch, &out->roll};
  memset(out, 0, sizeof *out);
  for (int i = 0; i < 6; i++) {
    V v = table.f[TFP_RT_PLACE_X + i];
    if (v.kind == TFP_RT_NIL || v.kind == TFP_RT_FALSE) continue; /* `p.yaw or 0` */
    *fields[i] = v.kind == TFP_RT_NUMBER ? v.number : NAN;
    out->present |= 1u << i;
  }
  return TFP_MOTION_GOT_VALUE;
}
static bool host_controller_set_placement(void *user, const TfpActionPlacement *value, const char **why) { /* pcall(controller.set_placement, controller, value) == true */
  Motion *m = user;
  TfpRtHost *h = &m->w->host;
  V first = nil(), second = nil();
  if (!h->controller_set_placement(h, value ? tfp_wire_settings_placement(m->w, value) : nil(), nil(), &first, &second)) {
    snprintf(m->why, sizeof m->why, "%s", h->raised ? h->raised : "");
    if (why) *why = m->why;
    return false;
  }
  if (first.kind == TFP_RT_TRUE) return true;
  if (why && second.kind == TFP_RT_TEXT) { copy_text(m->why, sizeof m->why, second, ""); *why = m->why; }
  return false;
}
static bool ops_answer(Motion *m, TfpOpsResult result, const TfpOpsText *text, const char **why) {
  if (result == TFP_OPS_TRUE) return true;
  if (why && text->present) { snprintf(m->why, sizeof m->why, "%.*s", (int)sizeof m->why - 1, text->text); *why = m->why; }
  return false;
}
static bool host_set_pose_motion(void *user, const TfpMotionRuntimeLease *view, const TfpMotionRuntimeModifier *value, const char **why) {
  Motion *m = user;
  MotionLease *l = lease_of_view(view);
  TfpOpsText text = {false, ""};
  TfpOpsMotion motion = {value->axis[TFP_MOTION_DX], value->axis[TFP_MOTION_DY], value->axis[TFP_MOTION_DZ],
                         {value->axis[TFP_MOTION_QX], value->axis[TFP_MOTION_QY], value->axis[TFP_MOTION_QZ], value->axis[TFP_MOTION_QW]},
                         15, value->authored_ads};
  return ops_answer(m, tfp_ops_set_pose_motion(&m->w->ops, l ? (TfpOpsLease *)&l->wire->ops : NULL, &motion, &text), &text, why);
}
static bool host_set_ads_placement(void *user, const TfpMotionRuntimeLease *view, const TfpActionPlacement *ads, const char **why) {
  Motion *m = user;
  MotionLease *l = lease_of_view(view);
  TfpOpsText text = {false, ""};
  TfpOpsPlacement p;
  if (ads) p = (TfpOpsPlacement){ads->x, ads->y, ads->z, ads->yaw, ads->pitch, ads->roll, ads->present};
  return ops_answer(m, tfp_ops_set_ads_placement(&m->w->ops, l ? (TfpOpsLease *)&l->wire->ops : NULL, ads ? &p : NULL, &text), &text, why);
}
static bool host_player_snapshot(void *user, bool *controller_active, bool *shoulder_aim) {
  Motion *m = user;
  TfpOpsSnapshot s;
  TfpOpsText why;
  if (tfp_ops_player_snapshot(&m->w->ops, &s, &why) != TFP_OPS_TRUE) return false;
  *controller_active = s.controller_active, *shoulder_aim = s.shoulder_aim;
  return true;
}
/* editor:is_active() and editor:preview_rotation() (rt:673-674 hands the motion runtime the editor):
 * the placement editor's yaw, pitch and roll are drawn here, as retail draws them (src/editor_motion.h).
 * Asked for the editor at each call: it is bound after motion, and made anew by the runtime's on_enable. */
static bool host_editor_is_active(void *user, bool *active, const char **problem) {
  (void)problem;
  return tfp_editor_motion_is_active(tfp_wire_editor_of(((Motion *)user)->w), active);
}
static TfpMotionRuntimeGot host_editor_preview_rotation(void *user, TfpActionPlacement *out) {
  return tfp_editor_motion_preview(tfp_wire_editor_of(((Motion *)user)->w), out);
}
/* Round 20 (src/editor_motion.h): while the editor's "ads" action is edited, its draft is the ADS placement, every frame:
 * full aim draws only that one, and before this a drag in full aim moved nothing that is drawn. */
static bool host_editor_preview_ads(void *user, const TfpMotionRuntimeLease *view, bool *authored, const char **why) {
  TfpActionPlacement draft;
  *authored = false;
  if (tfp_editor_motion_ads_draft(tfp_wire_editor_of(((Motion *)user)->w), &draft) != TFP_MOTION_GOT_VALUE) return true;
  *authored = true;
  return host_set_ads_placement(user, view, &draft, why);
}
static TfpMotionRuntimeGot host_action_placement(void *user, const char *key, TfpAct action, TfpActionPlacement *out) { /* rt:740 */
  Motion *m = user;
  TfpRtArgs args;
  V r[2] = {nil(), nil()};
  memset(&args, 0, sizeof args);
  args.v[0] = tfp_wire_text(key);
  args.v[1] = tfp_wire_text(tfp_action_name(action));
  TfpRtStatus status = tfp_rt_call(&m->w->rt, TFP_RT_E_action_placement, &args, r);
  if (status == TFP_RT_RAISED) { keep_raise(m); return TFP_MOTION_GOT_RAISED; }
  return status == TFP_RT_OK && tfp_wire_settings_placement_of(m->w, r[0], out) ? TFP_MOTION_GOT_VALUE : TFP_MOTION_GOT_NIL;
}
static TfpMotionRuntimeGot host_action_mapping(void *user, const TfpActionMapping **out) { /* rt:741 */
  Motion *m = user;
  V r[1] = {nil()};
  TfpRtStatus status = tfp_rt_call(&m->w->rt, TFP_RT_E_action_mapping, NULL, r);
  if (status == TFP_RT_RAISED) { keep_raise(m); return TFP_MOTION_GOT_RAISED; }
  *out = status == TFP_RT_OK ? tfp_wire_settings_mapping_of(m->w, r[0]) : NULL;
  return *out ? TFP_MOTION_GOT_VALUE : TFP_MOTION_GOT_NIL;
}
static bool host_presentation_transition(void *user, TfpTransitionPacket *out) { /* rt:672 */
  Motion *m = user;
  TfpWire *w = m->w;
  V r[2] = {nil(), nil()};
  if (tfp_rt_call(&w->rt, TFP_RT_E_presentation_transition, NULL, r) != TFP_RT_OK) return false;
  if (r[0].kind != TFP_RT_REF || r[0].ref != &w->packet || !w->has_packet) return false;
  *out = w->packet;
  return true;
}
static void host_presentation_action(void *user, TfpAct action, double now) { /* rt:674; the entry reads the clock itself */
  Motion *m = user;
  TfpRtArgs args;
  V r[2] = {nil(), nil()};
  (void)now;
  memset(&args, 0, sizeof args);
  args.v[0] = tfp_wire_text(tfp_action_name(action));
  tfp_rt_call(&m->w->rt, TFP_RT_E_presentation_action, &args, r);
}
static void host_presentation_resume(void *user, uint64_t identity, uint64_t player_identity, double now) { /* rt:675 */
  Motion *m = user;
  TfpRtArgs args;
  V r[2] = {nil(), nil()};
  char weapon[24], player[24];
  (void)now;
  tfp_wire_identity_text(weapon, 'W', identity);
  tfp_wire_identity_text(player, 'P', player_identity);
  memset(&args, 0, sizeof args);
  args.v[0] = tfp_wire_text(weapon);
  args.v[1] = tfp_wire_text(player);
  tfp_rt_call(&m->w->rt, TFP_RT_E_presentation_resume, &args, r);
}

/* ---- motion: the runtime host's pointers ---------------------------------------------------------- */
static bool rt_motion_new(TfpRtHost *h) { /* rt:816 MotionRuntime.new(controller, backend, editor, log, clock, options) */
  Motion *m = M(h);
  const char *error = NULL;
  refresh(m);
  if (!tfp_motion_runtime_init(&m->rt, &m->host, NULL, &error)) return tfp_wire_raise(h, error ? error : "Motion runtime dependencies required");
  m->made = true;
  m->logged_action = TFP_ACT_NONE;
  return true;
}
static bool raise_kept(TfpRtHost *h, Motion *m) { return tfp_wire_raise(h, m->raised[0] ? m->raised : "attempt to index a nil value"); }
static bool rt_motion_update(TfpRtHost *h) { /* rt:614, not protected */
  Motion *m = M(h);
  const char *reason = NULL;
  refresh(m);
  m->raised[0] = 0;
  if (tfp_motion_runtime_update(&m->rt, &reason) == TFP_MOTION_RUNTIME_RAISED) return raise_kept(h, m);
  return true;
}
static bool rt_motion_reset(TfpRtHost *h, V *o1, V *o2) {
  Motion *m = M(h);
  refresh(m);
  *o1 = tfp_wire_bool(tfp_motion_runtime_reset(&m->rt));
  *o2 = nil();
  return true;
}
static bool rt_motion_capture_neutral(TfpRtHost *h, bool native_ads, V *o1, V *o2) { /* rt:612 */
  Motion *m = M(h);
  const char *reason = NULL;
  refresh(m);
  m->raised[0] = 0;
  TfpMotionRuntimeAnswer answer = tfp_motion_runtime_capture_neutral(&m->rt, native_ads, &reason);
  if (answer == TFP_MOTION_RUNTIME_RAISED) return raise_kept(h, m);
  *o1 = tfp_wire_bool(answer == TFP_MOTION_RUNTIME_TRUE);
  *o2 = reason ? tfp_wire_text(tfp_wire_keep(m->w, reason)) : nil();
  return true;
}
static bool rt_motion_result(TfpRtHost *h, TfpRtTable *o1) { /* motion:result(): the fields rt:440-531 reads */
  Motion *m = M(h);
  TfpMotionRuntimeState s;
  tfp_motion_runtime_result(&m->rt, &s);
  memset(o1, 0, sizeof *o1);
  o1->present = true;
  o1->ref = &m->rt.state;
  o1->f[TFP_RT_MOTION_ACTION] = tfp_wire_text(s.action == TFP_ACT_NONE ? NULL : tfp_action_name(s.action));
  o1->f[TFP_RT_MOTION_SOFTENED] = tfp_wire_number((double)s.softened);
  o1->f[TFP_RT_MOTION_DX] = tfp_wire_number(s.dx);
  o1->f[TFP_RT_MOTION_DY] = tfp_wire_number(s.dy);
  o1->f[TFP_RT_MOTION_DZ] = tfp_wire_number(s.dz);
  return true;
}

/* ---- presentation:action and :resume (tr:85-104) -------------------------------------------------- */
static void packet_answer(TfpWire *w, bool has, const TfpTransitionPacket *packet, V *o1) { /* as wire_rt.c's packet_out */
  w->has_packet = has;
  if (!has) return;
  w->packet = *packet;
  *o1 = tfp_wire_ref(&w->packet);
}
static bool rt_transition_action(TfpRtHost *h, V action, double now, V *o1, V *o2) {
  TfpWire *w = h->user;
  TfpTransitionPacket packet;
  *o1 = *o2 = nil();
  if (action.kind != TFP_RT_TEXT || !action.length) return true; /* tr:86 text(action) */
  char name[32];
  copy_text(name, sizeof name, action, "");
  int code = tfp_action_from_name(name);
  if (code == TFP_ACT_NONE) { /* a name outside the list is still an action to the clock: any other number stands for it */
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < action.length; i++) hash = (hash ^ (uint8_t)action.text[i]) * 16777619u;
    code = TFP_ACT_COUNT + (int)(hash & 0x3fffffff);
  }
  packet_answer(w, tfp_transition_action(&w->transition, code, now, &packet), &packet, o1);
  return true;
}
static bool identity_key(V text, char level, uint64_t *out) { /* tfp_wire_identity_text back to its key */
  if (text.kind != TFP_RT_TEXT || text.length != 18 || text.text[0] != level || text.text[1] != ':') return false;
  uint64_t key = 0;
  for (int i = 2; i < 18; i++) {
    char c = text.text[i];
    int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (digit < 0) return false;
    key = key << 4 | (uint64_t)digit;
  }
  *out = key;
  return true;
}
static bool rt_transition_resume(TfpRtHost *h, V identity, V player, double now, V *o1, V *o2) {
  TfpWire *w = h->user;
  TfpTransitionPacket packet;
  uint64_t weapon_key, player_key;
  *o1 = *o2 = nil();
  /* A value that is not one of this binder's identity texts equals no packet's identity (tr:97). */
  if (!identity_key(identity, 'W', &weapon_key) || !identity_key(player, 'P', &player_key)) return true;
  packet_answer(w, tfp_transition_resume(&w->transition, weapon_key, player_key, now, &packet), &packet, o1);
  return true;
}

/* ---- the session log -------------------------------------------------------------------------------- */
const TfpMotionRuntime *tfp_wire_motion_runtime(TfpWire *w) {
  Motion *m = state(w);
  return m && m->made ? &m->rt : NULL;
}
void tfp_wire_motion_frame(TfpWire *w) {
  Motion *m = state(w);
  tfp_wire_settings_frame(w);
  if (m && !w->controller.enabled) {
    m->logged_action = TFP_ACT_NONE;
    return;
  }
  if (!m || !m->made || !w->rt.has[TFP_RT_O_MOTION]) return;
  const TfpMotionRuntimeState *s = &m->rt.state;
  /* The runtime keeps its last sampled action while the pose lease is unavailable. */
  if (s->phase == TFP_MOTION_PHASE_IDLE || s->phase == TFP_MOTION_PHASE_NONE) return;
  if (s->action != TFP_ACT_NONE && m->logged_action != s->action) {
    TfpActionPlacement p = {0, 0, 0, 0, 0, 0, 0};
    char line[320], source[48];
    const char *key = w->ops.has_placement_weapon_key ? w->ops.placement_weapon_key : NULL;
    tfp_wire_settings_resolve(w, key, s->action, &p, NULL);
    snprintf(line, sizeof line, "Motion: action=%s placement=%s x=%.4f y=%.4f z=%.4f yaw=%.2f pitch=%.2f roll=%.2f softened=%llu phase=%s",
             tfp_action_name(s->action), tfp_wire_settings_source(w, key, s->action, source, sizeof source), p.x, p.y, p.z,
             p.present & TFP_PLACE_YAW ? p.yaw : 0, p.present & TFP_PLACE_PITCH ? p.pitch : 0, p.present & TFP_PLACE_ROLL ? p.roll : 0,
             (unsigned long long)s->softened, tfp_motion_runtime_phase_name(s->phase));
    tfp_wire_say(w, line);
  }
  m->logged_action = s->action;
  double move = sqrt(s->dx * s->dx + s->dy * s->dy + s->dz * s->dz);
  double qw = fabs(s->qw) > 1 ? 1 : fabs(s->qw), turn = 2 * acos(qw) * 57.29577951308232;
  m->frames++;
  if (move > m->peak_move) m->peak_move = move;
  if (turn > m->peak_turn) m->peak_turn = turn;
}
size_t tfp_wire_motion_second(TfpWire *w, char *out, size_t size) {
  Motion *m = state(w);
  if (!out || !size) return 0;
  *out = 0;
  if (!m || !m->made) return 0;
  const TfpMotionRuntimeState *s = &m->rt.state;
  const TfpMotionOptions *options = &m->rt.last_options;
  double scale = m->rt.has_last_options && options->scale.kind == TFP_MOTION_NUMBER ? options->scale.value : 1;
  bool on = (!m->rt.has_last_options || options->enabled != TFP_MOTION_FALSE) && scale > 0;
  int n = snprintf(out, size, "motion=%s action=%s phase=%s move=%.4fm turn=%.2fdeg bob_weight=%.2f speed=%.2f scale=%.2f applied=%llu softened=%llu refused=%llu%s%.80s%s",
                   on ? "on" : "off", s->action == TFP_ACT_NONE ? "none" : tfp_action_name(s->action), tfp_motion_runtime_phase_name(s->phase),
                   m->peak_move, m->peak_turn, s->has_bob_weight ? s->bob_weight : 0, s->has_speed ? s->speed : 0, scale,
                   (unsigned long long)(s->applied - m->applied), (unsigned long long)(s->softened - m->softened),
                   (unsigned long long)(s->write_refusals + s->read_failures - m->refused), s->has_reason ? " why=\"" : "",
                   s->has_reason ? s->reason : "", s->has_reason ? "\"" : "");
  m->applied = s->applied, m->softened = s->softened, m->refused = s->write_refusals + s->read_failures, m->coalesced = s->coalesced;
  m->frames = 0, m->peak_move = m->peak_turn = 0;
  return n < 0 ? 0 : (size_t)n < size ? (size_t)n : size - 1;
}

/* ---- joining ------------------------------------------------------------------------------------------ */
bool tfp_wire_motion_bind(TfpWire *w) {
  if (!tfp_wire_settings(w)) {
    tfp_wire_say(w, "Refused: motion: the settings store is not bound (placements travel through it); weapon motion stays off");
    return false;
  }
  Motion *m = calloc(1, sizeof *m);
  if (!m) { tfp_wire_say(w, "Refused: motion: out of memory; weapon motion stays off"); return false; }
  m->w = w;
  m->sampler = (TfpLocomotionBackend){&m->memory, m, sampler_player_snapshot, sampler_executable, sampler_action_state, sampler_on_foot};
  m->host = (TfpMotionRuntimeHost){
    .user = m, .clock = host_clock, .log = host_log, .options = host_options,
    .controller_result = host_controller_result, .controller_is_enabled = host_controller_is_enabled,
    .controller_get_placement = host_controller_get_placement, .controller_set_placement = host_controller_set_placement,
    .set_pose_motion = host_set_pose_motion, .set_ads_placement = host_set_ads_placement, .player_snapshot = host_player_snapshot,
    .action_placement = host_action_placement, .action_mapping = host_action_mapping,
    .presentation_transition = host_presentation_transition, .presentation_action = host_presentation_action,
    .presentation_resume = host_presentation_resume,
    .editor_is_active = host_editor_is_active, .editor_preview_rotation = host_editor_preview_rotation, /* round 16 */
    .editor_preview_ads = host_editor_preview_ads, /* round 20 */
    .sample = host_sample, .to_parent = NULL};
  w->ext[TFP_WIRE_EXT_MOTION] = m;
  TfpRtHost *h = &w->host;
  h->motion_new = rt_motion_new;
  h->motion_update = rt_motion_update;
  h->motion_reset = rt_motion_reset;
  h->motion_result = rt_motion_result;
  h->motion_capture_neutral = rt_motion_capture_neutral;
  h->transition_action = rt_transition_action;
  h->transition_resume = rt_transition_resume;
  tfp_wire_say(w, "Ready: motion bound: bob, sway and per-action placements through the game's own snapshot (backend_ops); "
                  "sprint is seen by animation layer 3 = 71 only (the gait-flag and bridge tests are off on build 25480438); "
                  "the placement editor's yaw, pitch and roll drawn here");
  return true;
}
void tfp_wire_motion_free(TfpWire *w) {
  Motion *m = state(w);
  if (!m) return;
  for (MotionLease *l = m->leases, *next; l; l = next) { next = l->next; free(l); }
  w->ext[TFP_WIRE_EXT_MOTION] = NULL;
  free(m);
}
