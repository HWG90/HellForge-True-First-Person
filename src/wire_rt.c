/* See wire.h. Signature adapters between the runtime host (tagged values, tables, texts) and the
 * translated units of the view (typed C). Line references "rt:N" are retail runtime.lua, "ctl:N"
 * retail controller.lua, both at 0.5.41-smooth.66. No Windows. */
#include "wire.h"
#include "wire_off.h"
#include "wire_recovery.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W(h) ((TfpWire *)(h)->user)
typedef TfpRtValue V;
#define nil tfp_wire_nil
#define boolean tfp_wire_bool
#define num tfp_wire_number
/* A text into a smaller buffer, cut where it does not fit. */
static void cut(char *out, size_t size, const char *text) {
  size_t n = strlen(text);
  if (n >= size) n = size - 1;
  memcpy(out, text, n);
  out[n] = 0;
}
static bool truthy(V v) { return v.kind != TFP_RT_NIL && v.kind != TFP_RT_FALSE; }

/* ---- texts that must outlive the call that made them --------------------------------------- */
static char *arena(TfpWire *w, size_t size) {
  static char spill[1024]; /* an exhausted arena: the text is cut, never a wild pointer */
  if (size > sizeof w->arena - w->arena_used) return size <= sizeof spill ? spill : NULL;
  char *at = w->arena + w->arena_used;
  w->arena_used += size;
  return at;
}
static const char *keep(TfpWire *w, const char *text) {
  size_t n = strlen(text) + 1;
  char *at = arena(w, n);
  if (!at) return "(text too long)";
  memcpy(at, text, n);
  return at;
}
static V kept_text(TfpWire *w, const char *text) { return tfp_wire_text(keep(w, text)); }
static V ops_text(TfpWire *w, const TfpOpsText *t) { return t->present ? kept_text(w, t->text) : nil(); }
static bool raise(TfpRtHost *h, const char *text) { h->raised = keep(W(h), text); return false; }

void tfp_wire_identity_text(char out[24], char level, uint64_t key) {
  snprintf(out, 24, "%c:%016llx", level, (unsigned long long)key);
}

/* ---- snapshots ----------------------------------------------------------------------------- */
/* The 19 fields runtime.lua reads (backend.lua:157-290). A player-level snapshot has no weapon
 * fields and its identity is its player identity, of another shape than a weapon-level one. */
void tfp_wire_snapshot_table(TfpWire *w, const TfpOpsSnapshot *s, TfpRtTable *out) {
  TfpWireSnap *p = &w->snaps[w->snap_next++ % TFP_WIRE_SNAPS];
  p->s = *s;
  tfp_wire_identity_text(p->player, 'P', tfp_ops_player_key(s));
  if (s->full) tfp_wire_identity_text(p->identity, 'W', tfp_ops_identity_key(s));
  else memcpy(p->identity, p->player, sizeof p->identity);
  memset(out, 0, sizeof *out);
  out->present = true;
  out->ref = p;
#define SET(field, value) out->f[TFP_RT_SNAP_##field] = (value)
  SET(CAMERA_KIND, num(s->camera_kind));
  SET(SAFE, boolean(s->safe));
  SET(PLAYER_IDENTITY, tfp_wire_text(p->player));
  SET(IDENTITY, tfp_wire_text(p->identity));
  SET(BODY, num(s->body));
  SET(WORLD, num((double)s->world));
  V bytes = {TFP_RT_TEXT, 0, (const char *)p->s.controller_bytes, 6, 0};
  SET(CONTROLLER_BYTES, bytes);
  SET(CAMERA, num((double)s->camera));
  SET(VIEWMODEL, num(s->viewmodel));
  SET(WEAPON_UNIT, s->full ? num(s->weapon_unit) : nil());
  SET(AVATAR_OWNER, num((double)s->avatar_owner));
  SET(AVATAR_INDEX, num(s->avatar_index));
  SET(AIMING, boolean(s->aiming));
  SET(REASON, s->has_reason ? tfp_wire_text(p->s.reason) : nil());
  SET(RECORD, num((double)s->record));
  SET(SEATED, boolean(s->seated));
  SET(CONTROLLER_ACTIVE, boolean(s->controller_active));
  SET(WEAPON_READY, s->full ? boolean(s->weapon_ready) : nil());
  SET(ZOOM_IDENTITY, nil());
#undef SET
}
static TfpWireSnap *snap_of(TfpWire *w, const TfpRtTable *t) {
  const TfpWireSnap *p = t && t->present ? t->ref : NULL;
  if (!p || p < w->snaps || p >= w->snaps + TFP_WIRE_SNAPS || ((const char *)p - (const char *)w->snaps) % sizeof *p) return NULL;
  return &w->snaps[p - w->snaps];
}
typedef TfpOpsResult (*SnapshotFn)(TfpOps *, TfpOpsSnapshot *, TfpOpsText *);
static bool snapshot_call(TfpRtHost *h, SnapshotFn take, TfpRtTable *out1, V *out2) {
  TfpWire *w = W(h);
  TfpOpsSnapshot s;
  TfpOpsText why;
  memset(out1, 0, sizeof *out1);
  TfpOpsResult r = take(&w->ops, &s, &why);
  if (r == TFP_OPS_RAISED) return raise(h, why.text);
  if (r == TFP_OPS_TRUE) {
    tfp_wire_snapshot_table(w, &s, out1);
    if (s.full) w->frame_full = true, w->frame_identity = tfp_ops_identity_key(&s);
  } else if (out2) *out2 = ops_text(w, &why);
  return true;
}
static bool rt_backend_snapshot(TfpRtHost *h, TfpRtTable *o1, V *o2) { *o2 = nil(); return snapshot_call(h, tfp_ops_snapshot, o1, o2); }
static bool rt_backend_player_snapshot(TfpRtHost *h, TfpRtTable *o1) { return snapshot_call(h, tfp_ops_player_snapshot, o1, NULL); }
static bool rt_backend_pin_player_snapshot(TfpRtHost *h, TfpRtTable *o1) { return snapshot_call(h, tfp_ops_pin_player_snapshot, o1, NULL); }

/* ---- the pose stand-in --------------------------------------------------------------------- */
static TfpOpsResult np_prepare(void *u, const TfpOpsHelperConfig *c, void **native, TfpOpsText *why) {
  (void)u; (void)c; (void)why; *native = NULL; return TFP_OPS_TRUE;
}
static TfpOpsResult np_begin(void *u, const TfpOpsHelperConfig *c, void *native, TfpOpsHandle **handle, TfpOpsText *why) {
  TfpWire *w = u;
  (void)c; (void)native; (void)why;
  w->null_handle.owner = &w->null_pose, w->null_handle.disarmed = false, w->null_armed = true;
  *handle = &w->null_handle;
  return TFP_OPS_TRUE;
}
static TfpOpsResult np_owned(void *u, TfpOpsHandle **handle, TfpOpsText *why) {
  TfpWire *w = u;
  (void)why; *handle = w->null_armed ? &w->null_handle : NULL; return TFP_OPS_TRUE;
}
static TfpOpsResult np_finish(void *u, TfpOpsHandle *handle, bool *has_stats, TfpOpsHelperStats *stats, TfpOpsText *why) {
  TfpWire *w = u;
  (void)why;
  w->null_armed = false;
  if (handle) handle->disarmed = true;
  memset(stats, 0, sizeof *stats);
  stats->target_weight = stats->blend = 1;
  *has_stats = true;
  return TFP_OPS_TRUE;
}
static TfpOpsResult np_stats(void *u, TfpOpsHandle *handle, TfpOpsHelperStats *stats, TfpOpsText *why) {
  TfpWire *w = u;
  (void)handle;
  memset(stats, 0, sizeof *stats);
  if (!w->null_armed) {
    why->present = true;
    snprintf(why->text, sizeof why->text, "Pose stand-in is not armed");
    return TFP_OPS_FALSE;
  }
  stats->armed = 1, stats->target_weight = stats->blend = 1;
  return TFP_OPS_TRUE;
}
static TfpOpsResult np_xyz(void *u, TfpOpsHandle *h, double x, double y, double z, TfpOpsText *why) {
  (void)u; (void)h; (void)x; (void)y; (void)z; (void)why; return TFP_OPS_TRUE;
}
static TfpOpsResult np_zoom(void *u, TfpOpsHandle *h, bool active, TfpOpsText *why) { (void)u; (void)h; (void)active; (void)why; return TFP_OPS_TRUE; }
static TfpOpsResult np_transition(void *u, TfpOpsHandle *h, const TfpOpsPhase *t, TfpOpsText *why) { (void)u; (void)h; (void)t; (void)why; return TFP_OPS_TRUE; }
static TfpOpsResult np_motion(void *u, TfpOpsHandle *h, double dx, double dy, double dz, double qx, double qy, double qz, double qw,
                              bool ads, TfpOpsText *why) {
  (void)u; (void)h; (void)dx; (void)dy; (void)dz; (void)qx; (void)qy; (void)qz; (void)qw; (void)ads; (void)why; return TFP_OPS_TRUE;
}
static const TfpOpsPoseHelper *no_alignment(void *u, TfpOpsText *raised) {
  (void)u;
  raised->present = true;
  snprintf(raised->text, sizeof raised->text, "The alignment helper is not part of this mod");
  return NULL;
}

/* ---- the hooks runtime.lua installs on the backend (rt:672-741) ---------------------------- */
static void raised_text(TfpWire *w, TfpOpsText *out) {
  V e = w->rt.error;
  out->present = true;
  if (e.kind == TFP_RT_TEXT) snprintf(out->text, sizeof out->text, "%.*s", (int)(e.length < 300 ? e.length : 300), e.text);
  else snprintf(out->text, sizeof out->text, "%s", e.kind == TFP_RT_NIL ? "nil" : "error");
}
static void value_text(V v, TfpOpsText *out) {
  out->present = v.kind == TFP_RT_TEXT;
  if (out->present) snprintf(out->text, sizeof out->text, "%.*s", (int)(v.length < 300 ? v.length : 300), v.text);
}
static TfpOpsForeign hook_foreign_camera(void *u, TfpOpsText *raised) {
  TfpWire *w = u;
  V r[1] = {nil()};
  TfpRtStatus s = tfp_rt_call(&w->rt, TFP_RT_E_foreign_camera, NULL, r);
  if (s == TFP_RT_RAISED) { raised_text(w, raised); return TFP_OPS_FOREIGN_RAISED; }
  if (s == TFP_RT_ABSENT || r[0].kind == TFP_RT_FALSE) return TFP_OPS_FOREIGN_FALSE;
  return r[0].kind == TFP_RT_TRUE ? TFP_OPS_FOREIGN_TRUE : TFP_OPS_FOREIGN_OTHER;
}
static TfpOpsResult hook_camera_before_entry(void *u, TfpOpsText *why) {
  TfpWire *w = u;
  V r[2] = {nil(), nil()};
  TfpRtStatus s = tfp_rt_call(&w->rt, TFP_RT_E_camera_before_entry, NULL, r);
  if (s == TFP_RT_RAISED) { raised_text(w, why); return TFP_OPS_RAISED; }
  if (s == TFP_RT_ABSENT) return TFP_OPS_TRUE;
  value_text(r[1], why);
  return r[0].kind == TFP_RT_TRUE ? TFP_OPS_TRUE : r[0].kind == TFP_RT_NIL ? TFP_OPS_NIL : TFP_OPS_FALSE;
}
bool tfp_wire_placement_of(TfpWire *w, V value, TfpOpsPlacement *out) {
  if (value.kind != TFP_RT_REF) return false;
  if (value.ref != tfp_rt_default_placement) return w->placement_decode && w->placement_decode(w, value, out);
  memset(out, 0, sizeof *out);
  out->x = tfp_rt_default_placement[0], out->y = tfp_rt_default_placement[1], out->z = tfp_rt_default_placement[2];
  out->present = TFP_OPS_X | TFP_OPS_Y | TFP_OPS_Z;
  return true;
}
static TfpOpsResult hook_placement_resolver(void *u, const char *key, TfpOpsPlacement *out, TfpOpsText *raised) {
  TfpWire *w = u;
  TfpRtArgs args;
  V r[2] = {nil(), nil()};
  memset(&args, 0, sizeof args);
  args.v[0] = tfp_wire_text(key);
  TfpRtStatus s = tfp_rt_call(&w->rt, TFP_RT_E_action_placement, &args, r);
  if (s == TFP_RT_RAISED) { raised_text(w, raised); return TFP_OPS_RAISED; }
  return s == TFP_RT_OK && tfp_wire_placement_of(w, r[0], out) ? TFP_OPS_TRUE : TFP_OPS_NIL;
}
static bool current_packet(TfpWire *w, bool *raised) { /* backend.presentation_transition() */
  V r[2] = {nil(), nil()};
  TfpRtStatus s = tfp_rt_call(&w->rt, TFP_RT_E_presentation_transition, NULL, r);
  if (raised) *raised = s == TFP_RT_RAISED;
  return s == TFP_RT_OK && r[0].kind == TFP_RT_REF && r[0].ref == &w->packet && w->has_packet;
}
static void phase_of(const TfpTransitionPacket *p, TfpOpsPhase *out) {
  memset(out, 0, sizeof *out);
  out->has_epoch = out->has_progress = out->has_started = out->has_duration = true;
  out->epoch = (double)p->epoch, out->progress = p->progress, out->started = p->started, out->duration = p->duration;
  out->ads = p->ads, out->continuous = p->continuous;
  out->identity = p->identity, out->player_identity = p->player_identity;
  out->reason = (int)p->reason;
}
static TfpOpsResult hook_presentation_transition(void *u, TfpOpsPhase *out, TfpOpsText *raised) {
  TfpWire *w = u;
  bool failed = false;
  if (!current_packet(w, &failed)) {
    if (failed) { raised_text(w, raised); return TFP_OPS_RAISED; }
    return TFP_OPS_NIL;
  }
  phase_of(&w->packet, out);
  return TFP_OPS_TRUE;
}

/* ---- the controller's backend (controller.h TfpBackendApi) --------------------------------- */
void tfp_wire_view(const TfpOpsSnapshot *s, const char *reason_storage, TfpView *out) {
  memset(out, 0, sizeof *out);
  out->available = true;
  out->identity = tfp_ops_identity_key(s), out->player_identity = tfp_ops_player_key(s);
  out->aiming = s->aiming, out->controller_active = s->controller_active, out->safe = s->safe;
  out->camera_kind = s->camera_kind;
  out->record = s->record;
  out->guard = s->controller_bytes[5];
  out->reason = s->has_reason ? reason_storage : NULL;
}
static void view_call(TfpWire *w, SnapshotFn take, TfpView *out) {
  unsigned slot = w->view_next++ % TFP_WIRE_VIEWS;
  TfpOpsText why;
  for (unsigned i = 0; i < TFP_WIRE_VIEWS; i++) /* this TfpView is being rewritten: no older snapshot stands for it */
    if (w->views[i].view == out) w->views[i].view = NULL;
  TfpOpsResult r = take(&w->ops, &w->views[slot].s, &why);
  if (r != TFP_OPS_TRUE) {
    memset(out, 0, sizeof *out);
    cut(out->error, sizeof out->error, why.present ? why.text : "");
    w->views[slot].view = NULL;
    return;
  }
  cut(w->view_reason[slot], sizeof w->view_reason[slot], w->views[slot].s.reason);
  tfp_wire_view(&w->views[slot].s, w->view_reason[slot], out);
  w->views[slot].view = out;
}
static const TfpOpsSnapshot *view_snapshot(TfpWire *w, const TfpView *view) { /* the latest snapshot written to this TfpView */
  for (unsigned i = 1; i <= TFP_WIRE_VIEWS; i++) {
    unsigned slot = (w->view_next - i) % TFP_WIRE_VIEWS;
    if (w->views[slot].view == view) return &w->views[slot].s;
  }
  return NULL;
}
static void api_snapshot(void *u, TfpView *out) { view_call(u, tfp_ops_snapshot, out); }
static void api_player_snapshot(void *u, TfpView *out) { view_call(u, tfp_ops_player_snapshot, out); }
static void why_out(const TfpOpsText *text, char *why, size_t size) { if (why && size) cut(why, size, text->present ? text->text : ""); }
static bool api_camera_only_weapon(void *u, const TfpView *current) {
  TfpWire *w = u;
  TfpOpsText why;
  const TfpOpsSnapshot *s = view_snapshot(w, current);
  return s && tfp_ops_camera_only_weapon(&w->ops, s, &why) == TFP_OPS_TRUE;
}
static bool api_prepare_pose(void *u, const TfpView *current, char *why, size_t size) {
  TfpWire *w = u;
  TfpOpsText text = {false, ""};
  w->has_token = tfp_ops_prepare_pose(&w->ops, view_snapshot(w, current), &w->token, &text) == TFP_OPS_TRUE;
  why_out(&text, why, size);
  return w->has_token;
}
static void lease_sync(TfpWireLease *l) {
  l->view.request = l->ops.request;
  l->view.cleanup_only = l->ops.cleanup_only;
  l->view.pose_owner_retired = l->ops.pose_owner_retired;
  /* lease.pose_error, else lease.pose_stats.error (ctl:432) */
  l->view.has_pose_error = l->ops.has_pose_error || (l->ops.has_pose_stats && l->ops.pose_stats.has_error);
  l->view.pose_error = (int)(l->ops.has_pose_error ? l->ops.pose_error : l->ops.pose_stats.error);
}
static TfpLease *api_enter_pose(void *u, const TfpView *current, uint32_t remaining_ms, bool blend_entry, char *why, size_t size) {
  TfpWire *w = u;
  TfpOpsText text = {false, ""};
  /* A lease is a Lua table compared by identity and the backend may name it for as long as it
   * lives (backend_ops.h): its storage is never reused, so it is never freed once entered. */
  TfpWireLease *l = calloc(1, sizeof *l);
  if (!l) { if (why && size) snprintf(why, size, "Out of memory for a lease"); return NULL; }
  TfpOpsResult r = tfp_ops_enter_pose(&w->ops, view_snapshot(w, current), w->has_token ? &w->token : NULL, remaining_ms,
                                      blend_entry ? 1 : 0, &l->ops, &text);
  w->has_token = false;
  why_out(&text, why, size);
  if (r != TFP_OPS_TRUE) { free(l); return NULL; }
  l->serial = ++w->lease_serial;
  lease_sync(l);
  l->fov.identity = l->blur.identity = l->serial;
  l->fov.has_request = l->blur.has_request = true;
  l->fov.request = l->blur.request = l->ops.request;
  l->fov.has_origin = l->blur.has_origin = l->near_clip.has_origin = true;
  l->fov.origin_player_identity = l->blur.origin_player_identity = l->near_clip.origin_player_identity = tfp_ops_player_key(&l->ops.origin);
  l->fov.origin_has_weapon = l->blur.origin_has_weapon = true;
  l->fov.origin_weapon = l->blur.origin_weapon = l->ops.origin.weapon;
  return &l->view;
}
static bool api_pose_status(void *u, TfpLease *lease, TfpPoseStats *out, char *why, size_t size) {
  TfpWire *w = u;
  TfpWireLease *l = (TfpWireLease *)lease;
  TfpOpsHelperStats stats;
  TfpOpsText text = {false, ""};
  memset(&stats, 0, sizeof stats);
  TfpOpsResult r = tfp_ops_pose_status(&w->ops, &l->ops, &stats, &text);
  lease_sync(l);
  why_out(&text, why, size);
  if (r != TFP_OPS_TRUE) return false;
  out->armed = stats.armed == 1, out->expired = stats.expired == 1;
  out->target_weight = stats.target_weight, out->blend = stats.blend, out->error = (int)stats.error;
  return true;
}
static bool api_release(void *u, TfpLease *lease, char *why, size_t size) {
  TfpWire *w = u;
  TfpWireLease *l = (TfpWireLease *)lease;
  TfpOpsText text = {false, ""};
  TfpOpsResult r = tfp_ops_release(&w->ops, &l->ops, &text);
  lease_sync(l);
  why_out(&text, why, size);
  return r == TFP_OPS_TRUE;
}
static void api_presentation_transition(void *u, TfpPhasePacket *out) {
  TfpWire *w = u;
  memset(out, 0, sizeof *out);
  if (!current_packet(w, NULL)) return;
  out->available = true;
  out->identity = w->packet.identity, out->player_identity = w->packet.player_identity;
  out->epoch = w->packet.epoch, out->progress = w->packet.progress;
}
static bool api_set_pose_transition(void *u, TfpLease *lease, const TfpPhasePacket *packet, char *why, size_t size) {
  TfpWire *w = u;
  TfpOpsPhase phase;
  TfpOpsText text = {false, ""};
  (void)packet; /* the packet just read: w->packet holds all of its fields */
  phase_of(&w->packet, &phase);
  TfpOpsResult r = tfp_ops_set_pose_transition(&w->ops, &((TfpWireLease *)lease)->ops, &phase, &text);
  why_out(&text, why, size);
  return r == TFP_OPS_TRUE;
}
static double runtime_clock(TfpWire *w) { /* the closure of monotonic_clock() (rt:160-173) */
  V r[1] = {nil()};
  return tfp_rt_call(&w->rt, TFP_RT_E_clock, NULL, r) == TFP_RT_OK && r[0].kind == TFP_RT_NUMBER ? r[0].number : NAN;
}
static double api_clock(void *u) { return runtime_clock(u); }

/* ---- what the four lens / camera adapters read ---------------------------------------------- */
static void io_log(void *u, const char *text) { TfpWire *w = u; if (w->platform.log) w->platform.log(w->platform.user, text); }
static bool io_clock(void *u, double *seconds) { *seconds = runtime_clock(u); return *seconds == *seconds; }
static bool io_enabled(void *u) { return ((TfpWire *)u)->controller.enabled; }
static bool pose_offset(const TfpWire *w) { return w->controller.runs > 0; } /* summary.mode, set by the first start */
static TfpWireLease *held(TfpWire *w) { return (TfpWireLease *)w->controller.lease; }
#define LEASE_OF(member, pointer) ((TfpWireLease *)((char *)(pointer) - offsetof(TfpWireLease, member)))

static bool cam_config(void *u, TfpCameraBackendConfig *out, TfpCameraText *why) {
  TfpWire *w = u;
  TfpOpsConfig c;
  TfpOpsText text;
  if (tfp_ops_camera_config(&w->ops, &c, &text) != TFP_OPS_TRUE) {
    if (why) { why->present = text.present; cut(why->text, sizeof why->text, text.present ? text.text : ""); }
    return false;
  }
  memset(out, 0, sizeof *out);
  out->present = TFP_CAMERA_FIELDS_ALL;
  out->game = c.game, out->exe = c.exe, out->context = c.context, out->camera = c.camera, out->avatar_owner = c.avatar_owner;
  out->avatar_index = c.avatar_index, out->avatar = c.avatar, out->body = c.body, out->camera_unit = c.camera_unit;
  out->forward = c.forward, out->up = c.up, out->smoothing_ms = c.smoothing_ms, out->blend_ms = c.blend_ms;
  out->max_gap_ms = (uint32_t)c.max_gap_ms;
  out->camera_kind = c.camera_kind;
  out->presentation_enabled = c.presentation_enabled;
  return true;
}
static bool cam_snapshot(void *u, TfpCameraBackendSnapshot *out) {
  TfpWire *w = u;
  TfpOpsSnapshot s;
  TfpOpsText why;
  if (tfp_ops_snapshot(&w->ops, &s, &why) != TFP_OPS_TRUE) return false;
  out->has_weapon_unit = true, out->weapon_unit = s.weapon_unit;
  return true;
}
static void cam_controller(void *u, TfpCameraControllerResult *out) { out->pose_offset = pose_offset(u); }
static double cam_clock(void *u) { return runtime_clock(u); }

static bool lens_config(TfpWire *w, TfpWireLease *l, TfpOpsConfig *c, const char **why) {
  TfpOpsText text;
  if (tfp_ops_fov_config(&w->ops, l ? &l->ops : NULL, c, &text) == TFP_OPS_TRUE) return true;
  if (why) *why = text.present ? keep(w, text.text) : NULL;
  return false;
}
static bool fov_config(void *u, const TfpFovPoseLease *lease, TfpFovCamera *out, const char **why) {
  TfpOpsConfig c;
  if (!lens_config(u, lease ? LEASE_OF(fov, lease) : NULL, &c, why)) return false;
  memset(out, 0, sizeof *out);
  out->missing = (c.has_request ? 0 : TFP_FOV_MISSING_REQUEST) | (c.has_weapon ? 0 : TFP_FOV_MISSING_WEAPON);
  out->game = c.game, out->exe = c.exe, out->context = c.context, out->camera = c.camera, out->avatar_owner = c.avatar_owner;
  out->request = c.request, out->avatar_index = c.avatar_index, out->avatar = c.avatar, out->body = c.body, out->weapon = c.weapon;
  return true;
}
static bool blur_config(void *u, const TfpBlurPoseLease *lease, TfpBlurCamera *out, const char **why) {
  TfpFovCamera c;
  if (!fov_config(u, lease ? &LEASE_OF(blur, lease)->fov : NULL, &c, why)) return false;
  memset(out, 0, sizeof *out);
  out->missing = c.missing;
  out->game = c.game, out->exe = c.exe, out->context = c.context, out->camera = c.camera, out->avatar_owner = c.avatar_owner;
  out->request = c.request, out->avatar_index = c.avatar_index, out->avatar = c.avatar, out->body = c.body, out->weapon = c.weapon;
  return true;
}
static bool near_config(void *u, const TfpNearPoseLease *lease, TfpNearCamera *out, const char **why) {
  TfpFovCamera c;
  if (!fov_config(u, lease ? &LEASE_OF(near_clip, lease)->fov : NULL, &c, why)) return false;
  memset(out, 0, sizeof *out);
  out->game = c.game, out->exe = c.exe, out->context = c.context, out->camera = c.camera, out->avatar_owner = c.avatar_owner;
  out->avatar_index = c.avatar_index;
  return true;
}
static void fov_packet(void *u, TfpFovPacket *out, const char **why) {
  TfpWire *w = u;
  bool failed = false;
  memset(out, 0, sizeof *out);
  out->kind = TFP_FOV_PACKET_NONE;
  if (!current_packet(w, &failed)) {
    if (failed) { /* the hook raised: fov.lua's pcall answers its error text, not a packet */
      TfpOpsText text;
      raised_text(w, &text);
      out->kind = TFP_FOV_PACKET_OTHER;
      if (why) *why = keep(w, text.text);
    }
    return;
  }
  out->kind = TFP_FOV_PACKET_TABLE;
  out->epoch_is_number = out->progress_is_number = true;
  out->epoch = (double)w->packet.epoch, out->progress = w->packet.progress;
  out->ads = w->packet.ads ? TFP_FOV_ADS_TRUE : TFP_FOV_ADS_FALSE;
}
static void fov_controller(void *u, TfpFovControllerState *out) {
  TfpWire *w = u; out->pose_offset = pose_offset(w), out->running = w->controller.phase == TFP_PHASE_RUNNING;
}
static void blur_controller(void *u, TfpBlurControllerState *out) {
  TfpWire *w = u; out->pose_offset = pose_offset(w), out->running = w->controller.phase == TFP_PHASE_RUNNING;
}
static void near_controller(void *u, TfpNearControllerState *out) {
  TfpWire *w = u; out->pose_offset = pose_offset(w), out->running = w->controller.phase == TFP_PHASE_RUNNING;
}
static const TfpFovPoseLease *fov_lease(void *u) { TfpWireLease *l = held(u); return l ? &l->fov : NULL; }
static const TfpBlurPoseLease *blur_lease(void *u) { TfpWireLease *l = held(u); return l ? &l->blur : NULL; }
static const TfpNearPoseLease *near_lease(void *u) { TfpWireLease *l = held(u); return l ? &l->near_clip : NULL; }
static void near_options(void *u, TfpNearOptions *out) { /* rt:713-716 */
  TfpWire *w = u;
  V r[3] = {nil(), nil(), nil()};
  memset(out, 0, sizeof *out);
  if (tfp_rt_call(&w->rt, TFP_RT_E_near_options, NULL, r) != TFP_RT_OK || r[0].kind != TFP_RT_NIL) return; /* no menu table in this build */
  out->is_table = true;
  out->hip_is_number = r[1].kind == TFP_RT_NUMBER, out->hip = r[1].number;
  out->aim_is_number = r[2].kind == TFP_RT_NUMBER, out->aim = r[2].number;
}
static bool near_clock(void *u, double *seconds, const char **error) {
  *seconds = runtime_clock(u);
  if (*seconds == *seconds) return true;
  if (error) *error = "Engine FPV clock unavailable";
  return false;
}

/* ---- backend: the runtime host's pointers --------------------------------------------------- */
static bool rt_backend_new(TfpRtHost *h) {
  TfpWire *w = W(h);
  tfp_ops_init(&w->ops, &w->ops_host, NULL, w->pose_helper ? w->pose_helper : &w->null_pose);
  return true;
}
static bool rt_backend_begin_frame(TfpRtHost *h) { tfp_ops_begin_frame(&W(h)->ops); return true; }
static bool rt_backend_end_frame(TfpRtHost *h) { tfp_ops_end_frame(&W(h)->ops); return true; }
static bool address_of(double value, uint64_t *out) {
  if (!(value >= 0) || value >= 18446744073709551616.0) return false;
  *out = (uint64_t)value;
  return true;
}
static bool rt_backend_ptr(TfpRtHost *h, double address, V *o1) {
  uint64_t at, value = 0;
  TfpOpsText why;
  if (!address_of(address, &at)) return raise(h, "Pointer unavailable");
  if (!tfp_ops_ptr(&W(h)->ops, at, &value, &why)) return raise(h, why.text);
  *o1 = num((double)value);
  return true;
}
static bool rt_backend_read(TfpRtHost *h, V address, double size, V *o1) {
  TfpWire *w = W(h);
  uint64_t at;
  TfpOpsText why;
  /* rt:490, rt:506 hand the result of _ptr on unchecked: anything but a number fails the read. */
  if (address.kind != TFP_RT_NUMBER || !address_of(address.number, &at) || !(size >= 1) || size > 1024) return raise(h, "Memory unavailable");
  char *bytes = arena(w, (size_t)size);
  if (!bytes) return raise(h, "Memory unavailable");
  if (!tfp_ops_read(&w->ops, at, bytes, (size_t)size, &why)) return raise(h, why.text);
  o1->kind = TFP_RT_TEXT, o1->text = bytes, o1->length = (size_t)size, o1->number = 0, o1->ref = NULL;
  return true;
}
static bool rt_backend_keep_aim_view(TfpRtHost *h, const TfpRtTable *snapshot) {
  TfpWire *w = W(h);
  TfpWireSnap *p = snap_of(w, snapshot);
  TfpOpsText why;
  if (tfp_ops_keep_aim_view(&w->ops, p ? &p->s : NULL, &why) == TFP_OPS_RAISED) return raise(h, why.text);
  return true;
}
static bool rt_backend_detach_seated_camera(TfpRtHost *h) {
  TfpOpsText why;
  if (tfp_ops_detach_seated_camera(&W(h)->ops, &why) == TFP_OPS_RAISED) return raise(h, why.text);
  return true;
}
static bool rt_set_ignored(TfpRtHost *h, V value) { (void)h; (void)value; return true; } /* pin_first_person=false, camera_update_token=nil */
static bool rt_backend_set_revision(TfpRtHost *h, double value) { W(h)->revision = value, W(h)->has_revision = true; return true; }
static V rt_backend_frame_active(TfpRtHost *h) { return boolean(W(h)->ops.frame_active); }
static V rt_backend_frame_epoch(TfpRtHost *h) { return num((double)W(h)->ops.frame_epoch); }
static V rt_backend_weapon_key(TfpRtHost *h) {
  TfpWire *w = W(h);
  return w->ops.has_placement_weapon_key ? tfp_wire_text(w->ops.placement_weapon_key) : nil();
}
static V rt_backend_pose_epoch(TfpRtHost *h) {
  TfpWire *w = W(h);
  return w->ops.has_presentation_pose_epoch ? num(w->ops.presentation_pose_epoch) : nil();
}
static V rt_backend_revision(TfpRtHost *h) { return W(h)->has_revision ? num(W(h)->revision) : nil(); }

/* A lease as the flat LEASE table. The pose stand-in has no depth_stats / input_stats function. */
void tfp_wire_lease_table(const TfpWireLease *l, TfpRtTable *out, char identity[24], char player[24]) {
  memset(out, 0, sizeof *out);
  if (!l) return;
  tfp_wire_identity_text(identity, 'W', tfp_ops_identity_key(&l->ops.origin));
  tfp_wire_identity_text(player, 'P', tfp_ops_player_key(&l->ops.origin));
  out->present = true;
  out->ref = l;
#define SET(field, value) out->f[TFP_RT_LEASE_##field] = (value)
  SET(ORIGIN, boolean(true));
  SET(ORIGIN_IDENTITY, tfp_wire_text(identity));
  SET(ORIGIN_PLAYER_IDENTITY, tfp_wire_text(player));
  SET(ORIGIN_BODY, num(l->ops.origin.body));
  SET(ORIGIN_WORLD, num((double)l->ops.origin.world));
  if (l->ops.has_alignment) {
    SET(ALIGNMENT, boolean(true));
    SET(ENDED, boolean(l->ops.alignment.ended));
    SET(KIND, tfp_wire_text(l->ops.alignment.kind == TFP_OPS_KIND_POSE ? "pose" : l->ops.alignment.kind == TFP_OPS_KIND_BODY ? "body" : "alignment"));
    SET(HELPER, boolean(true));
    SET(HANDLE, l->ops.alignment.handle ? tfp_wire_ref(l->ops.alignment.handle) : nil());
  }
#undef SET
}
static void lease_out(TfpRtHost *h, const TfpWireLease *l, TfpRtTable *out) {
  char *texts = arena(W(h), 48);
  static char spill[48];
  if (!texts) texts = spill;
  tfp_wire_lease_table(l, out, texts, texts + 24);
  if (l && W(h)->lease_extra) W(h)->lease_extra(W(h), l, out);
}
static void rt_controller_lease(TfpRtHost *h, TfpRtTable *out) { lease_out(h, held(W(h)), out); }
static const TfpWireLease *ops_lease(const TfpOpsLease *l) { return l ? LEASE_OF(ops, l) : NULL; }
static void rt_alignment_lease(TfpRtHost *h, TfpRtTable *out) { lease_out(h, ops_lease(W(h)->ops.alignment_lease), out); }
static void rt_pose_lease(TfpRtHost *h, TfpRtTable *out) { lease_out(h, ops_lease(W(h)->ops.presentation_pose_lease), out); }
static void rt_warm_lease(TfpRtHost *h, TfpRtTable *out) { (void)h; memset(out, 0, sizeof *out); } /* warm_start is Fird Person's */

/* ---- controller ------------------------------------------------------------------------------ */
static const char *phase_text(TfpPhase phase) {
  static const char *const names[] = {"idle", "waiting", "running", "cleanup_pending", "disabled"};
  return names[phase];
}
void tfp_wire_controller_state(TfpWire *w, TfpRtTable *out) { /* controller:result(), ctl:148-155 */
  const TfpController *c = &w->controller;
  memset(out, 0, sizeof *out);
  out->present = true;
  out->ref = c;
  out->f[TFP_RT_STATE_MODE] = pose_offset(w) ? tfp_wire_text("pose_offset") : nil();
  out->f[TFP_RT_STATE_ENABLED] = boolean(c->enabled);
  out->f[TFP_RT_STATE_PHASE] = tfp_wire_text(phase_text(c->phase));
  if (c->runs > 0) {
    char *text = arena(w, 24);
    if (text) {
      tfp_wire_identity_text(text, 'P', c->initial.player_identity);
      out->f[TFP_RT_STATE_PLAYER_IDENTITY] = tfp_wire_text(text);
    }
  }
  out->f[TFP_RT_STATE_CLEANUP_PENDING] = boolean(c->lease && c->phase == TFP_PHASE_CLEANUP_PENDING);
  out->f[TFP_RT_STATE_WAIT_REASON] = c->wait_reason[0] ? kept_text(w, c->wait_reason) : nil();
  /* blend: the pose helper's hip blend; nil without a pose helper. */
  if (w->controller_extra) w->controller_extra(w, out);
}
static bool rt_controller_new(TfpRtHost *h) { tfp_controller_init(&W(h)->controller, &W(h)->api); return true; }
static bool rt_controller_is_enabled(TfpRtHost *h, V *o1) { *o1 = boolean(W(h)->controller.enabled); return true; }
static bool rt_controller_is_clean(TfpRtHost *h, V *o1) { *o1 = boolean(tfp_controller_is_clean(&W(h)->controller)); return true; }
static bool rt_controller_result(TfpRtHost *h, TfpRtTable *o1) { tfp_wire_controller_state(W(h), o1); return true; }
static bool rt_controller_update(TfpRtHost *h) { tfp_controller_update(&W(h)->controller); return true; }
static bool controller_answer(TfpRtHost *h, bool done, V *o1, V *o2) { /* (true) or (false, the label) */
  *o1 = boolean(done);
  *o2 = done ? nil() : kept_text(W(h), W(h)->controller.label);
  return true;
}
static bool rt_controller_stop(TfpRtHost *h, const char *reason, V *o1, V *o2) {
  return controller_answer(h, tfp_controller_stop(&W(h)->controller, reason), o1, o2);
}
static bool rt_controller_suspend(TfpRtHost *h, const char *reason, V *o1, V *o2) {
  return controller_answer(h, tfp_controller_suspend(&W(h)->controller, reason), o1, o2);
}
static bool rt_controller_retry_cleanup(TfpRtHost *h, V *o1, V *o2) {
  return controller_answer(h, tfp_controller_retry_cleanup(&W(h)->controller), o1, o2);
}
static bool rt_controller_shutdown(TfpRtHost *h, V *o1, V *o2) {
  return controller_answer(h, tfp_controller_shutdown(&W(h)->controller), o1, o2);
}
static bool rt_controller_start(TfpRtHost *h, V seconds, V mode, V *o1, V *o2) {
  (void)seconds; /* 0: until stopped; the timed test modes are not translated (controller.h) */
  if (mode.kind != TFP_RT_TEXT || mode.length != 11 || memcmp(mode.text, "pose_offset", 11)) {
    *o1 = boolean(false), *o2 = tfp_wire_text("Only the arms/weapon mode is translated");
    return true;
  }
  return controller_answer(h, tfp_controller_start(&W(h)->controller), o1, o2);
}
/* Placement and the status line are the controller's (controller.c, controller.lua:102-147); what
 * follows only carries values between the runtime, the controller and the backend. */
static const unsigned place_bits[TFP_AXIS_N] = {TFP_OPS_X, TFP_OPS_Y, TFP_OPS_Z, TFP_OPS_YAW, TFP_OPS_PITCH, TFP_OPS_ROLL};
static void place_from_ops(const TfpOpsPlacement *p, TfpPlacementValue *out) {
  const double values[TFP_AXIS_N] = {p->x, p->y, p->z, p->yaw, p->pitch, p->roll};
  for (int i = 0; i < TFP_AXIS_N; i++)
    out->kind[i] = p->present & place_bits[i] ? TFP_AXIS_NUMBER : TFP_AXIS_NIL, out->v[i] = values[i];
}
static TfpOpsPlacement place_to_ops(const double value[TFP_AXIS_N]) { /* the controller's next_value: six numbers */
  return (TfpOpsPlacement){value[TFP_AXIS_X], value[TFP_AXIS_Y], value[TFP_AXIS_Z], value[TFP_AXIS_YAW], value[TFP_AXIS_PITCH],
                           value[TFP_AXIS_ROLL], TFP_OPS_X | TFP_OPS_Y | TFP_OPS_Z | TFP_OPS_YAW | TFP_OPS_PITCH | TFP_OPS_ROLL};
}
static bool place_pose_placement(void *u, TfpPlacementValue *out) { /* backend.pose_placement */
  TfpWire *w = u;
  if (!w->ops.has_pose_placement) return false;
  place_from_ops(&w->ops.pose_placement, out);
  return true;
}
static const char *place_weapon_key(void *u) { /* backend.placement_weapon_key */
  TfpWire *w = u;
  return w->ops.has_placement_weapon_key ? w->ops.placement_weapon_key : NULL;
}
static bool place_answer(TfpOpsResult accepted, const TfpOpsText *text, char *why, size_t size) {
  why_out(text, why, size);
  return accepted == TFP_OPS_TRUE;
}
static bool place_set_pose(void *u, TfpLease *lease, const double value[TFP_AXIS_N], char *why, size_t size) {
  TfpWire *w = u;
  TfpOpsText text = {false, ""};
  TfpOpsPlacement p = place_to_ops(value);
  return place_answer(tfp_ops_set_pose_placement(&w->ops, &((TfpWireLease *)lease)->ops, &p, &text), &text, why, size);
}
static bool place_set_defaults(void *u, const double value[TFP_AXIS_N], char *why, size_t size) {
  TfpWire *w = u;
  TfpOpsText text = {false, ""};
  TfpOpsPlacement p = place_to_ops(value);
  return place_answer(tfp_ops_set_placement_defaults(&w->ops, &p, &text), &text, why, size);
}
static TfpPlacementApi place_api(TfpWire *w) {
  return (TfpPlacementApi){w, place_pose_placement, place_weapon_key, place_set_pose, place_set_defaults};
}
static bool rt_controller_get_placement(TfpRtHost *h, TfpRtTable *o1) { /* ctl:102-105 */
  TfpWire *w = W(h);
  TfpPlacementApi api = place_api(w);
  TfpPlacementValue value;
  tfp_controller_get_placement(&w->controller, &api, &value);
  memset(o1, 0, sizeof *o1);
  o1->present = true;
  o1->ref = &w->ops.pose_placement;
  for (int i = 0; i < TFP_AXIS_N; i++) o1->f[TFP_RT_PLACE_X + i] = value.kind[i] == TFP_AXIS_NUMBER ? num(value.v[i]) : nil();
  return true;
}
static bool rt_controller_placement_target(TfpRtHost *h, V *o1, V *o2) { /* ctl:106-109 */
  TfpWire *w = W(h);
  TfpPlacementApi api = place_api(w);
  const char *key = NULL;
  TfpLease *lease = NULL;
  *o1 = *o2 = nil();
  if (!tfp_controller_placement_target(&w->controller, &api, &key, &lease)) return true;
  *o1 = tfp_wire_text(key);
  /* lease.origin and lease.origin.identity: a lease of this binder always has its origin. */
  char *identity = arena(w, 24);
  if (identity) {
    tfp_wire_identity_text(identity, 'W', tfp_ops_identity_key(&((TfpWireLease *)lease)->ops.origin));
    *o2 = tfp_wire_text(identity);
  }
  return true;
}
/* The bounds rule alone (ctl:112-124), asked of the controller with a backend that accepts. */
static bool place_accept_lease(void *u, TfpLease *lease, const double value[TFP_AXIS_N], char *why, size_t size) {
  (void)u; (void)lease; (void)value; (void)why; (void)size;
  return true;
}
static bool place_accept(void *u, const double value[TFP_AXIS_N], char *why, size_t size) {
  (void)u; (void)value; (void)why; (void)size;
  return true;
}
const char *tfp_wire_placement_check(const double xyz[3], const double angles[3]) {
  static const char *const refusals[] = {"Placement must be finite XYZ within 0.5 metres", "Angles must be within90degrees"};
  static const TfpPlacementApi accepting = {NULL, NULL, NULL, place_accept_lease, place_accept};
  TfpController scratch;
  TfpPlacementValue value;
  char why[160] = "";
  memset(&scratch, 0, sizeof scratch);
  for (int i = 0; i < 3; i++) {
    value.kind[i] = value.kind[3 + i] = TFP_AXIS_NUMBER;
    value.v[i] = xyz[i], value.v[3 + i] = angles[i];
  }
  if (tfp_controller_set_placement(&scratch, &accepting, &value, why, sizeof why)) return NULL;
  return strcmp(why, refusals[0]) ? refusals[1] : refusals[0];
}
static bool rt_controller_set_placement(TfpRtHost *h, V value, V extra, V *o1, V *o2) { /* ctl:110-135 */
  TfpWire *w = W(h);
  TfpPlacementApi api = place_api(w);
  TfpOpsPlacement p;
  TfpPlacementValue table, *given = NULL;
  char why[320] = "";
  (void)extra; /* self:set_placement(value) takes one argument */
  *o1 = boolean(false), *o2 = nil();
  if (value.kind == TFP_RT_REF) { /* a table: the shipped default, one placement_decode reads, or {} */
    if (tfp_wire_placement_of(w, value, &p)) place_from_ops(&p, &table);
    else if (value.ref == &tfp_rt_empty_table) memset(&table, 0, sizeof table); /* every field nil */
    else { *o2 = tfp_wire_text("Placement table is not one the binder can read"); return true; } /* the binder's own refusal */
    given = &table;
  }
  if (tfp_controller_set_placement(&w->controller, &api, given, why, sizeof why)) *o1 = boolean(true);
  else *o2 = kept_text(w, why);
  return true;
}
static bool rt_controller_status(TfpRtHost *h, V *o1) { /* ctl:138-147 */
  TfpWire *w = W(h);
  char line[512];
  tfp_controller_status(&w->controller, line, sizeof line);
  *o1 = kept_text(w, line);
  return true;
}

/* ---- FOV, blur, camera, near ------------------------------------------------------------------ */
static bool pair(TfpRtHost *h, bool done, const char *why, V *o1, V *o2) {
  *o1 = boolean(done);
  *o2 = why ? kept_text(W(h), why) : nil();
  return true;
}
static bool rt_fov_new(TfpRtHost *h) { TfpWire *w = W(h); tfp_fov_lease_init(&w->fov, w->helpers.fov, &w->fov_io, TFP_FOV_PROFILE_SHIPPED); return true; }
#define LENS2(name, call) static bool rt_##name(TfpRtHost *h, V *o1, V *o2) { const char *why = NULL; bool done = call; return pair(h, done, why, o1, o2); }
LENS2(fov_update, tfp_fov_lease_update(&W(h)->fov, &why))
LENS2(fov_stop, tfp_fov_lease_stop(&W(h)->fov, &why))
LENS2(fov_shutdown, tfp_fov_lease_shutdown(&W(h)->fov, &why))
LENS2(fov_preflight, tfp_fov_lease_preflight(&W(h)->fov, &why))
LENS2(blur_update, tfp_blur_lease_update(&W(h)->blur, &why))
LENS2(blur_stop, tfp_blur_lease_stop(&W(h)->blur, &why))
LENS2(blur_shutdown, tfp_blur_lease_shutdown(&W(h)->blur, &why))
LENS2(near_stop, tfp_near_lease_stop(&W(h)->near_clip, &why))
LENS2(near_shutdown, tfp_near_lease_shutdown(&W(h)->near_clip, &why))
static bool rt_fov_is_clean(TfpRtHost *h, V *o1) { *o1 = boolean(tfp_fov_lease_is_clean(&W(h)->fov)); return true; }
static bool rt_fov_result(TfpRtHost *h, TfpRtTable *o1) {
  static const char *const names[] = {"idle", "active", "cleanup_pending"};
  TfpFovResult r;
  tfp_fov_lease_result(&W(h)->fov, &r);
  memset(o1, 0, sizeof *o1);
  o1->present = true;
  o1->ref = &W(h)->fov;
  o1->f[TFP_RT_FOV_PHASE] = tfp_wire_text(names[r.phase]);
  if (r.stats.has) {
    o1->f[TFP_RT_FOV_ARMED] = boolean(r.stats.armed);
    o1->f[TFP_RT_FOV_LAST_NATIVE] = num(r.stats.last_native);
    o1->f[TFP_RT_FOV_LAST_APPLIED] = num(r.stats.last_applied);
  }
  if (r.has_presentation) o1->f[TFP_RT_FOV_TRANSITION_EPOCH] = num(r.transition_epoch);
  return true;
}
static bool rt_fov_status(TfpRtHost *h, V *o1) {
  char line[TFP_FOV_TEXT];
  tfp_fov_lease_status(&W(h)->fov, line, sizeof line);
  *o1 = kept_text(W(h), line);
  return true;
}
static bool rt_blur_new(TfpRtHost *h) { TfpWire *w = W(h); tfp_blur_lease_init(&w->blur, w->helpers.blur, &w->blur_io, TFP_BLUR_PROFILE_SHIPPED); return true; }
static bool rt_blur_preflight(TfpRtHost *h, V *o1, V *o2) {
  const char *why = NULL;
  TfpBlurAnswer r = tfp_blur_lease_preflight(&W(h)->blur, &why);
  pair(h, r == TFP_BLUR_TRUE, why, o1, o2);
  if (r == TFP_BLUR_NIL) *o1 = nil();
  return true;
}
static bool rt_blur_is_clean(TfpRtHost *h, V *o1) { *o1 = boolean(tfp_blur_lease_is_clean(&W(h)->blur)); return true; }
static bool rt_blur_result(TfpRtHost *h, TfpRtTable *o1) {
  static const char *const names[] = {"idle", "waiting", "unavailable", "active", "cleanup_pending"};
  TfpBlurResult r;
  tfp_blur_lease_result(&W(h)->blur, &r);
  memset(o1, 0, sizeof *o1);
  o1->present = true;
  o1->ref = &W(h)->blur;
  o1->f[TFP_RT_TARGET_PHASE] = tfp_wire_text(names[r.phase]);
  if (r.stats.has) o1->f[TFP_RT_TARGET_ARMED] = boolean(r.stats.armed);
  return true;
}
static bool rt_blur_status(TfpRtHost *h, V *o1) {
  char line[TFP_BLUR_TEXT];
  tfp_blur_lease_status(&W(h)->blur, line, sizeof line);
  *o1 = kept_text(W(h), line);
  return true;
}
static bool rt_near_new(TfpRtHost *h) { TfpWire *w = W(h); tfp_near_lease_init(&w->near_clip, w->helpers.near_clip, &w->near_io, TFP_NEAR_PROFILE_SHIPPED); return true; }
static bool rt_near_update(TfpRtHost *h, V *o1, V *o2) {
  const char *why = NULL;
  TfpNearAnswer r = tfp_near_lease_update(&W(h)->near_clip, &why);
  if (r == TFP_NEAR_RAISED) return raise(h, why ? why : "Near clip update failed"); /* the clock raised (near_adapter.h) */
  return pair(h, r == TFP_NEAR_TRUE, why, o1, o2);
}
static bool rt_near_is_clean(TfpRtHost *h, V *o1) { *o1 = boolean(tfp_near_lease_is_clean(&W(h)->near_clip)); return true; }
static bool rt_near_result(TfpRtHost *h, TfpRtTable *o1) { memset(o1, 0, sizeof *o1); o1->present = true; o1->ref = &W(h)->near_clip; return true; }
static bool rt_near_status(TfpRtHost *h, V *o1) {
  char line[TFP_NEAR_TEXT];
  tfp_near_lease_status(&W(h)->near_clip, line, sizeof line);
  *o1 = kept_text(W(h), line);
  return true;
}
static bool camera_pair(TfpRtHost *h, bool done, const TfpCameraText *why, V *o1, V *o2) {
  return pair(h, done, why->present ? why->text : NULL, o1, o2);
}
static bool rt_camera_new(TfpRtHost *h) { TfpWire *w = W(h); tfp_camera_adapter_init(&w->camera, &w->camera_api, NULL); return true; }
#define CAMERA2(name, call) static bool rt_##name(TfpRtHost *h, V *o1, V *o2) { TfpCameraText why = {false, ""}; bool done = call; return camera_pair(h, done, &why, o1, o2); }
CAMERA2(camera_update, tfp_camera_adapter_update(&W(h)->camera, &why))
CAMERA2(camera_prepare, tfp_camera_adapter_prepare(&W(h)->camera, &why))
CAMERA2(camera_stop, tfp_camera_adapter_stop(&W(h)->camera, &why))
CAMERA2(camera_shutdown, tfp_camera_adapter_shutdown(&W(h)->camera, &why))
static bool rt_camera_preflight(TfpRtHost *h, V *o1, V *o2) {
  TfpCameraText why = {false, ""};
  int r = tfp_camera_adapter_preflight(&W(h)->camera, &why);
  camera_pair(h, r == 1, &why, o1, o2);
  if (r < 0) *o1 = nil();
  return true;
}
static bool rt_camera_is_clean(TfpRtHost *h, V *o1) { *o1 = boolean(tfp_camera_adapter_is_clean(&W(h)->camera)); return true; }
static bool rt_camera_result(TfpRtHost *h, TfpRtTable *o1) {
  static const char *const names[] = {"idle", "waiting", "active", "cleanup_pending"};
  TfpCameraAdapterResult r;
  tfp_camera_adapter_result(&W(h)->camera, &r);
  memset(o1, 0, sizeof *o1);
  o1->present = true;
  o1->ref = &W(h)->camera;
  o1->f[TFP_RT_CAM_PHASE] = tfp_wire_text(names[r.phase]);
  if (r.has_stats) {
    o1->f[TFP_RT_CAM_ARMED] = boolean(r.stats.armed);
    o1->f[TFP_RT_CAM_ERROR] = num(r.stats.error);
    o1->f[TFP_RT_CAM_HEAD_DISTANCE] = num(r.stats.head_distance);
    o1->f[TFP_RT_CAM_REPLACED] = num(r.stats.replaced);
    o1->f[TFP_RT_CAM_BLEND] = num(r.stats.blend);
  }
  return true;
}
static bool rt_camera_status(TfpRtHost *h, V *o1) {
  char line[512];
  tfp_camera_adapter_status(&W(h)->camera, line, sizeof line);
  *o1 = kept_text(W(h), line);
  return true;
}
static bool rt_camera_set_handoff(TfpRtHost *h, V entry_ms, V rotation_ms, V *o1, V *o2) {
  TfpCameraText why = {false, ""};
  if (entry_ms.kind != TFP_RT_NUMBER || rotation_ms.kind != TFP_RT_NUMBER) return pair(h, false, "Camera handoff takes two numbers", o1, o2);
  return camera_pair(h, tfp_camera_adapter_set_handoff(&W(h)->camera, entry_ms.number, rotation_ms.number, &why), &why, o1, o2);
}
static bool rt_camera_set_cut_hold(TfpRtHost *h, V exit_hold, V eye, V anchor, V seated, V *o1, V *o2) {
  TfpCameraText why = {false, ""};
  bool done = tfp_camera_adapter_set_cut_hold(&W(h)->camera, truthy(exit_hold), truthy(eye), truthy(anchor), truthy(seated), &why);
  return camera_pair(h, done, &why, o1, o2);
}
/* recovery.lua:15 reads failure_kind and failure_native_error off the failing policy's result(). */
static bool failure_of(TfpRtHost *h, const char *policy, bool *temporary_read, int *native_error) {
  TfpWire *w = W(h);
  if (!strcmp(policy, "fov")) {
    TfpFovResult r;
    tfp_fov_lease_result(&w->fov, &r);
    *temporary_read = r.failure_kind == TFP_FOV_FAILURE_TEMPORARY_READ;
    *native_error = r.has_failure_native_error ? (int)r.failure_native_error : -1;
    return true;
  }
  if (!strcmp(policy, "blur")) {
    TfpBlurResult r;
    tfp_blur_lease_result(&w->blur, &r);
    *temporary_read = r.failure_kind == TFP_BLUR_FAILURE_TEMPORARY_READ;
    *native_error = r.failure_native_error.has ? (int)r.failure_native_error.value : -1;
    return true;
  }
  if (!strcmp(policy, "camera")) {
    TfpCameraAdapterResult r;
    tfp_camera_adapter_result(&w->camera, &r);
    *temporary_read = r.failure_kind == TFP_CAMERA_FAILURE_TEMPORARY_READ;
    *native_error = r.has_failure_native_error ? (int)r.failure_native_error : -1;
    return true;
  }
  return false; /* head, movement: stubbed off, they never fail */
}

/* ---- transition (the shared 0.18 s clock) ---------------------------------------------------- */
static bool rt_transition_new(TfpRtHost *h, double duration, double max_gap) {
  (void)duration; (void)max_gap; /* .18 and .25, fixed in transition.c */
  tfp_transition_init(&W(h)->transition);
  W(h)->has_packet = false;
  return true;
}
static bool rt_transition_native_ads(TfpRtHost *h, const TfpRtTable *value, V *o1) {
  TfpWireSnap *p = snap_of(W(h), value);
  *o1 = boolean(p && tfp_transition_native_ads(p->s.controller_active, p->s.seated, p->s.controller_bytes));
  return true;
}
static void packet_out(TfpWire *w, bool has, const TfpTransitionPacket *packet, TfpRtTable *out) {
  memset(out, 0, sizeof *out);
  w->has_packet = has;
  if (!has) return;
  w->packet = *packet;
  out->present = true;
  out->ref = &w->packet;
  out->f[TFP_RT_TRANS_EPOCH] = num((double)packet->epoch);
  out->f[TFP_RT_TRANS_PROGRESS] = num(packet->progress);
}
static bool rt_transition_result(TfpRtHost *h, TfpRtTable *o1) {
  TfpTransitionPacket packet;
  bool has = tfp_transition_result(&W(h)->transition, &packet);
  packet_out(W(h), has, &packet, o1);
  return true;
}
static bool rt_transition_update(TfpRtHost *h, const TfpRtTable *value, double now, V enabled, V foreign) {
  TfpWire *w = W(h);
  TfpWireSnap *p = snap_of(w, value);
  TfpTransitionPacket packet;
  TfpTransitionInput input;
  memset(&input, 0, sizeof input);
  if (p) {
    V zoom = value->f[TFP_RT_SNAP_ZOOM_IDENTITY];
    input.player_identity = tfp_ops_player_key(&p->s), input.identity = tfp_ops_identity_key(&p->s);
    input.controller_active = value->f[TFP_RT_SNAP_CONTROLLER_ACTIVE].kind == TFP_RT_TRUE; /* the field runtime.lua added (rt:384) */
    input.seated = p->s.seated, input.shoulder_aim = p->s.shoulder_aim, input.camera_kind = p->s.camera_kind;
    input.action = TFP_TRANSITION_NONE; /* the motion module is off: no action placement */
    input.zoom_identity = zoom.kind == TFP_RT_NUMBER ? (int)zoom.number : TFP_TRANSITION_NONE;
  }
  tfp_transition_update(&w->transition, p ? &input : NULL, now, truthy(enabled), truthy(foreign), &packet);
  return true;
}
static bool rt_transition_reset(TfpRtHost *h, const char *reason) { (void)reason; tfp_transition_reset(&W(h)->transition); return true; }
static bool rt_transition_action(TfpRtHost *h, V action, double now, V *o1, V *o2) { /* only the motion module calls it */
  TfpWire *w = W(h);
  TfpTransitionPacket packet;
  TfpRtTable table;
  *o1 = *o2 = nil();
  if (action.kind != TFP_RT_NUMBER) return true;
  packet_out(w, tfp_transition_action(&w->transition, (int)action.number, now, &packet), &packet, &table);
  if (table.present) *o1 = tfp_wire_ref(table.ref);
  return true;
}
static bool rt_transition_resume(TfpRtHost *h, V identity, V player, double now, V *o1, V *o2) { /* only the motion module calls it */
  (void)h; (void)identity; (void)player; (void)now;
  *o1 = *o2 = nil();
  return true;
}

/* ---- diagnostics: the session log takes the row ------------------------------------------------ */
static bool rt_diagnostics_new(TfpRtHost *h, double capacity) { (void)h; (void)capacity; return true; }
static bool rt_diagnostics_clear(TfpRtHost *h) { (void)h; return true; }
static void value_show(V v, char *out, size_t size) {
  if (v.kind == TFP_RT_TEXT) snprintf(out, size, "%.*s", (int)(v.length < 200 ? v.length : 200), v.text);
  else if (v.kind == TFP_RT_NUMBER) snprintf(out, size, "%.14g", v.number);
  else snprintf(out, size, "%s", v.kind == TFP_RT_TRUE ? "true" : v.kind == TFP_RT_FALSE ? "false" : "nil");
}
static bool rt_diagnostics_event(TfpRtHost *h, double tick, const char *kind, const V *event) { /* rt:301 */
  char policy[32], reason[220], phase[32], pending[8];
  value_show(event[TFP_RT_EVENT_POLICY], policy, sizeof policy);
  value_show(event[TFP_RT_EVENT_REASON], reason, sizeof reason);
  value_show(event[TFP_RT_EVENT_PHASE], phase, sizeof phase);
  value_show(event[TFP_RT_EVENT_CLEANUP_PENDING], pending, sizeof pending);
  tfp_view_log_event(&W(h)->log, "Refused: policy %s failed (%s): %s; recovery phase %s, cleanup pending %s; frame %.0f", policy, kind, reason,
                     phase, pending, tick);
  return true;
}
static bool rt_diagnostics_record(TfpRtHost *h, double tick, const V *rec) { /* rt:513 */
  TfpWire *w = W(h);
  (void)tick;
  w->has_row = true;
  w->row_has_eye = rec[TFP_RT_REC_CAM_X].kind == TFP_RT_NUMBER && rec[TFP_RT_REC_CAM_Y].kind == TFP_RT_NUMBER &&
                   rec[TFP_RT_REC_CAM_Z].kind == TFP_RT_NUMBER;
  if (w->row_has_eye) w->row_eye[0] = rec[TFP_RT_REC_CAM_X].number, w->row_eye[1] = rec[TFP_RT_REC_CAM_Y].number, w->row_eye[2] = rec[TFP_RT_REC_CAM_Z].number;
  w->row_has_fov = rec[TFP_RT_REC_FOV_APPLIED].kind == TFP_RT_NUMBER && rec[TFP_RT_REC_FOV_NATIVE].kind == TFP_RT_NUMBER;
  if (w->row_has_fov) w->row_fov_applied = rec[TFP_RT_REC_FOV_APPLIED].number, w->row_fov_native = rec[TFP_RT_REC_FOV_NATIVE].number;
  return true;
}

/* ---- the mod's own: log, clock, mouse, the debug global, F11 ---------------------------------- */
static bool rt_mod_log(TfpRtHost *h, const char *text) { io_log(W(h), text); return true; }
static bool rt_clock_now(TfpRtHost *h, V *o1) {
  TfpWire *w = W(h);
  double now = w->platform.clock ? w->platform.clock(w->platform.user) : NAN;
  *o1 = now == now ? num(now) : nil();
  return true;
}
static bool rt_mouse_button_index(TfpRtHost *h, const char *name, V *o1) {
  (void)h;
  *o1 = !strcmp(name, "right") ? num(1) : !strcmp(name, "middle") ? num(2) : nil();
  return true;
}
static bool rt_mouse_button(TfpRtHost *h, double index, V *o1) {
  TfpWire *w = W(h);
  int level = w->platform.mouse(w->platform.user, (int)index);
  *o1 = level < 0 ? nil() : num(level);
  return true;
}
static V rt_global_get(TfpRtHost *h) { return num(W(h)->global_ours ? 1 : 0); }
static void rt_global_set(TfpRtHost *h, bool ours) { W(h)->global_ours = ours; }
/* Retail's menu polls F11 inside menu:update(), the last thing of update_frame (rt:639; inventory 2.1
 * step 15), and calls toggle_pose there: inside the owned frame. The same here. */
static bool rt_menu_update(TfpRtHost *h) {
  TfpWire *w = W(h);
  if (!w->toggle_requested) return true;
  w->toggle_requested = false;
  bool was = w->controller.enabled;
  V r[2] = {nil(), nil()};
  tfp_view_log_event(&w->log, "State: %s requested (F11)", was ? "exit" : "enter");
  TfpRtStatus s = tfp_rt_call(&w->rt, TFP_RT_E_toggle_pose, NULL, r);
  char why[240];
  if (s == TFP_RT_RAISED) {
    value_show(w->rt.error, why, sizeof why);
    tfp_view_log_event(&w->log, "Refused: F11 raised: %s", why);
  } else if (s == TFP_RT_ABSENT) tfp_view_log_event(&w->log, "Refused: F11 before the mode was ready");
  else if (r[0].kind != TFP_RT_TRUE) {
    value_show(r[1], why, sizeof why);
    tfp_view_log_event(&w->log, "Refused: F11 %s: %s", was ? "exit" : "enter", r[1].kind == TFP_RT_NIL ? w->controller.label : why);
  }
  return true;
}

/* ---- for the other binder files (wire.h) ------------------------------------------------------- */
const char *tfp_wire_keep(TfpWire *w, const char *t) { return keep(w, t); }
bool tfp_wire_raise(TfpRtHost *h, const char *t) { return raise(h, t); }
TfpWireSnap *tfp_wire_snap_of(TfpWire *w, const TfpRtTable *table) { return snap_of(w, table); }
TfpWireLease *tfp_wire_held(TfpWire *w) { return held(w); }
const TfpWireLease *tfp_wire_lease_of_ops(const TfpOpsLease *lease) { return ops_lease(lease); }
const TfpWireLease *tfp_wire_lease_of_table(const TfpRtTable *table) { return table && table->present ? table->ref : NULL; }
double tfp_wire_clock(TfpWire *w) { return runtime_clock(w); }
void tfp_wire_say(TfpWire *w, const char *t) { io_log(w, t); }
bool tfp_wire_pose_offset(const TfpWire *w) { return pose_offset(w); }
const char *tfp_wire_phase_text(TfpPhase phase) { return phase_text(phase); }
bool tfp_wire_address_of(double value, uint64_t *out) { return address_of(value, out); }

/* ---- joining --------------------------------------------------------------------------------- */
/* TfpOpsHost has one `user` for memory and for runtime.lua's hooks: the backend gets this binder, and
 * these forward the memory half to the caller's host. */
static uint64_t mem_module(void *u, TfpOpsModule which) { TfpWire *w = u; return w->memory.module(w->memory.user, which); }
static bool mem_read(void *u, uint64_t at, void *out, size_t size) { TfpWire *w = u; return w->memory.read(w->memory.user, at, out, size); }
static bool mem_write(void *u, uint64_t at, const void *bytes, size_t size) { TfpWire *w = u; return w->memory.write(w->memory.user, at, bytes, size); }
static void mem_query(void *u, uint64_t at, TfpOpsRegion *out) { TfpWire *w = u; w->memory.query(w->memory.user, at, out); }
static bool mem_call(void *u, TfpOpsNative which, uint64_t address, const uint64_t args[4], uint64_t *result, TfpOpsText *raised) {
  TfpWire *w = u;
  return w->memory.call(w->memory.user, which, address, args, result, raised);
}
static double mem_clock(void *u) { TfpWire *w = u; return w->platform.clock ? w->platform.clock(w->platform.user) : NAN; }
static void mem_log(void *u, const char *text) { io_log(u, text); }
static void log_sink(void *u, const char *text) { io_log(u, text); }
void tfp_wire_init(TfpWire *w, const TfpOpsHost *memory, const TfpWirePlatform *platform, const TfpWireHelpers *helpers) {
  memset(w, 0, sizeof *w);
  w->platform = *platform;
  w->helpers = *helpers;
  tfp_view_log_init(&w->log, log_sink, w);

  w->memory = *memory;
  w->ops_host = (TfpOpsHost){w, memory->wine, mem_module, mem_read, mem_write, mem_query, mem_call, mem_clock, mem_log,
                             NULL, NULL, NULL, NULL, NULL, NULL};
  w->ops_host.foreign_camera = hook_foreign_camera;
  w->ops_host.camera_before_entry = hook_camera_before_entry;
  w->ops_host.placement_resolver = hook_placement_resolver;
  w->ops_host.presentation_transition = hook_presentation_transition;
  w->ops_host.load_pose = NULL; /* the stand-in is handed to tfp_ops_init, as Backend.new(log, io, alignment, pose) takes it */
  w->ops_host.load_alignment = no_alignment;

  w->null_pose = (TfpOpsPoseHelper){w, np_prepare, np_begin, np_owned, np_finish, np_stats, np_xyz, np_zoom, np_xyz, np_transition, np_motion};
  w->api = (TfpBackendApi){w, api_snapshot, api_player_snapshot, api_camera_only_weapon, api_prepare_pose, api_enter_pose, api_pose_status,
                           api_release, api_presentation_transition, api_set_pose_transition, api_clock};
  w->camera_api = (TfpCameraAdapterApi){w, helpers->camera, NULL, cam_config, cam_snapshot, cam_controller, io_enabled, cam_clock, io_log};
  w->fov_io = (TfpFovIo){w, fov_config, fov_packet, fov_controller, io_enabled, fov_lease, io_clock, io_log};
  w->blur_io = (TfpBlurIo){w, blur_config, blur_controller, io_enabled, blur_lease, io_clock, io_log};
  w->near_io = (TfpNearIo){w, near_config, near_controller, io_enabled, near_lease, near_options, near_clock, io_log};

  TfpRtHost *h = &w->host;
  h->user = w;
  tfp_wire_off_bind(h);
  tfp_wire_recovery_bind(h, failure_of);
  h->backend_new = rt_backend_new;
  h->backend_snapshot = rt_backend_snapshot;
  h->backend_player_snapshot = rt_backend_player_snapshot;
  h->backend_pin_player_snapshot = rt_backend_pin_player_snapshot;
  h->backend_begin_frame = rt_backend_begin_frame;
  h->backend_end_frame = rt_backend_end_frame;
  h->backend_ptr = rt_backend_ptr;
  h->backend_read = rt_backend_read;
  h->backend_keep_aim_view = rt_backend_keep_aim_view;
  h->backend_detach_seated_camera = rt_backend_detach_seated_camera;
  h->backend_set_pin_first_person = rt_set_ignored;
  h->backend_set_camera_update_token = rt_set_ignored;
  h->backend_set_action_placement_revision = rt_backend_set_revision;
  h->backend_frame_active = rt_backend_frame_active;
  h->backend_frame_epoch = rt_backend_frame_epoch;
  h->backend_placement_weapon_key = rt_backend_weapon_key;
  h->backend_presentation_pose_epoch = rt_backend_pose_epoch;
  h->backend_action_placement_revision = rt_backend_revision;
  h->backend_warm_lease = rt_warm_lease;
  h->backend_alignment_lease = rt_alignment_lease;
  h->backend_presentation_pose_lease = rt_pose_lease;
  h->controller_lease = rt_controller_lease;
  h->controller_new = rt_controller_new;
  h->controller_is_enabled = rt_controller_is_enabled;
  h->controller_result = rt_controller_result;
  h->controller_update = rt_controller_update;
  h->controller_stop = rt_controller_stop;
  h->controller_suspend = rt_controller_suspend;
  h->controller_retry_cleanup = rt_controller_retry_cleanup;
  h->controller_shutdown = rt_controller_shutdown;
  h->controller_get_placement = rt_controller_get_placement;
  h->controller_set_placement = rt_controller_set_placement;
  h->controller_start = rt_controller_start;
  h->controller_is_clean = rt_controller_is_clean;
  h->controller_placement_target = rt_controller_placement_target;
  h->controller_status = rt_controller_status;
  h->fov_new = rt_fov_new, h->fov_stop = rt_fov_stop, h->fov_update = rt_fov_update, h->fov_result = rt_fov_result;
  h->fov_is_clean = rt_fov_is_clean, h->fov_shutdown = rt_fov_shutdown, h->fov_preflight = rt_fov_preflight, h->fov_status = rt_fov_status;
  h->blur_new = rt_blur_new, h->blur_stop = rt_blur_stop, h->blur_update = rt_blur_update, h->blur_result = rt_blur_result;
  h->blur_is_clean = rt_blur_is_clean, h->blur_shutdown = rt_blur_shutdown, h->blur_preflight = rt_blur_preflight, h->blur_status = rt_blur_status;
  h->camera_new = rt_camera_new, h->camera_stop = rt_camera_stop, h->camera_update = rt_camera_update, h->camera_result = rt_camera_result;
  h->camera_is_clean = rt_camera_is_clean, h->camera_shutdown = rt_camera_shutdown, h->camera_preflight = rt_camera_preflight;
  h->camera_status = rt_camera_status, h->camera_prepare = rt_camera_prepare;
  h->camera_set_handoff = rt_camera_set_handoff, h->camera_set_cut_hold = rt_camera_set_cut_hold;
  h->near_new = rt_near_new, h->near_stop = rt_near_stop, h->near_update = rt_near_update, h->near_is_clean = rt_near_is_clean;
  h->near_shutdown = rt_near_shutdown, h->near_result = rt_near_result, h->near_status = rt_near_status;
  h->transition_new = rt_transition_new, h->transition_native_ads = rt_transition_native_ads, h->transition_result = rt_transition_result;
  h->transition_update = rt_transition_update, h->transition_reset = rt_transition_reset, h->transition_action = rt_transition_action;
  h->transition_resume = rt_transition_resume;
  h->diagnostics_new = rt_diagnostics_new, h->diagnostics_event = rt_diagnostics_event, h->diagnostics_record = rt_diagnostics_record;
  h->diagnostics_clear = rt_diagnostics_clear;
  h->mod_log = rt_mod_log;
  h->clock_now = rt_clock_now;
  if (platform->mouse) h->mouse_button_index = rt_mouse_button_index, h->mouse_button = rt_mouse_button;
  h->global_get = rt_global_get, h->global_set = rt_global_set;
  h->menu_update = rt_menu_update;
  tfp_rt_init(&w->rt, h);
}

TfpRtStatus tfp_wire_call(TfpWire *w, TfpRtEntry entry, const TfpRtArgs *args, TfpRtValue *ret) {
  if (w->depth++ == 0) w->arena_used = 0;
  TfpRtStatus status = tfp_rt_call(&w->rt, entry, args, ret);
  w->depth--;
  return status;
}

void tfp_wire_frame(TfpWire *w, const char *extra) {
  V r[2] = {nil(), nil()};
  w->has_row = w->frame_full = false;
  if (tfp_wire_call(w, TFP_RT_E_on_update, NULL, r) == TFP_RT_RAISED) {
    char why[240];
    value_show(w->rt.error, why, sizeof why);
    if (strncmp(why, w->entry_fault, sizeof w->entry_fault - 1)) {
      snprintf(w->entry_fault, sizeof w->entry_fault, "%s", why);
      tfp_view_log_event(&w->log, "Refused: the frame raised: %s", why);
    }
  } else w->entry_fault[0] = 0;

  if (w->frame_extra) w->frame_extra(w);
  TfpViewSample s;
  TfpFovResult fov;
  TfpBlurResult blur;
  TfpCameraAdapterResult camera;
  TfpNearResult near_clip;
  memset(&s, 0, sizeof s);
  if (!w->rt.has[TFP_RT_O_CONTROLLER]) return; /* not enabled (yet): nothing to sample */
  tfp_fov_lease_result(&w->fov, &fov);
  tfp_blur_lease_result(&w->blur, &blur);
  tfp_camera_adapter_result(&w->camera, &camera);
  tfp_near_lease_result(&w->near_clip, &near_clip);
  const TfpController *c = &w->controller;
  const TfpWireSnap *last = w->snap_next ? &w->snaps[(w->snap_next - 1) % TFP_WIRE_SNAPS] : NULL;
  s.time = w->rt.clock_previous.kind == TFP_RT_NUMBER ? w->rt.clock_previous.number : 0;
  s.enabled = c->enabled, s.has_lease = c->lease != NULL;
  s.phase = phase_text(c->phase), s.label = c->label, s.wait = c->wait_reason;
  s.request = c->lease ? c->lease->request : 0;
  if (last && w->has_row) {
    s.safe = last->s.safe, s.controller_active = last->s.controller_active, s.camera_kind = last->s.camera_kind;
  }
  s.has_identity = w->frame_full;
  s.identity = w->frame_identity;
  s.aim_keeps = w->ops.aim_view_keeps, s.seated_detaches = w->ops.seated_detaches;
  s.fault[0] = fov.error_message;
  s.fault[1] = blur.error_message;
  s.fault[2] = camera.error_message.present ? camera.error_message.text : NULL;
  s.fault[3] = near_clip.error_message;
  s.has_eye = w->has_row && w->row_has_eye;
  memcpy(s.eye, w->row_eye, sizeof s.eye);
  s.has_fov = w->has_row && w->row_has_fov;
  s.fov_native = w->row_fov_native, s.fov_applied = w->row_fov_applied;
  s.has_near = near_clip.stats.has;
  s.near_applied = near_clip.stats.last_applied;
  s.extra = extra;
  tfp_view_log_frame(&w->log, &s);
}
