/* Translation of retail src/first_person/pose.lua (0.5.41-smooth.66, commit
 * 1d0d453). Line numbers in comments are that file's. See pose_adapter.h. */
#include "pose_adapter.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* pose.lua:9-21. */
static const char *const reasons[] = {
  NULL,
  "invalid pose configuration", "another native helper lease is reserved",
  "required memory is unreadable", "game or executable build changed",
  "native instruction fingerprint changed", "engine API pointers changed",
  "player or camera ownership changed", "model or weapon generation changed",
  "native helper could not remain resident", "engine API slot is not writable",
  "engine API slot ownership changed", "engine API pointer update failed",
  "owned pointer restoration is still pending", "a nonzero native sight node is unavailable",
  "arms/weapon branch or separate camera hierarchy changed",
  "native branch position is unavailable or invalid",
  "native editor generation no longer belongs to this lease",
  "native editor deadline has expired",
};

void tfp_pose_adapter_init(TfpPoseAdapter *a, const TfpPoseHelperApi *api, const TfpPoseAdapterIo *io, TfpPoseAdapterProfile profile) {
  memset(a, 0, sizeof *a); /* Pose.new (23-25): owned=false; new buffers are zero */
  a->profile = profile;
  a->api = api;
  a->io = io;
}

static void invalidate_stats(TfpPoseAdapter *a) { /* pose.lua:26-28 */
  a->has_cache = false;
  a->stats_handle = NULL;
  a->has_at = false;
  a->stats_at = a->stats_due = 0;
}

/* pose.lua:29-36 under the pcall of line 117. False: no usable time. */
static bool stats_time(TfpPoseAdapter *a, double *out) {
  if (a->io->clock) return a->io->clock(a->io->user, out);
  if (!a->io->tick) return false; /* kernel32 could not be loaded: the pcall fails */
  *out = (double)a->io->tick(a->io->user) / 1000;
  return true;
}

/* pose.lua:92-97. */
static const char *native_error(TfpPoseAdapter *a, const char *operation) {
  uint32_t code = a->api->error();
  const char *detail = code >= 1 && code <= 18 ? reasons[code] : NULL;
  if (detail) snprintf(a->text, sizeof a->text, "%s failed (%s; native code %u)", operation, detail, (unsigned)code);
  else snprintf(a->text, sizeof a->text, "%s failed (native code %u)", operation, (unsigned)code);
  return a->text;
}

/* pose.lua:99-139. */
bool tfp_pose_adapter_stats(TfpPoseAdapter *a, TfpPoseHandle *handle, TfpPoseStatsView *out, const char **why) {
  *why = NULL;
  out->has = false;
  if (handle && (handle->owner != a || (a->owned && a->owned != handle))) {
    *why = "Pose statistics handle is not owned";
    return false;
  }
  if (!handle) handle = a->owned;
  if (handle && handle->disarmed) {
    *out = handle->stats;
    return out->has;
  }
  TfpPoseStats *s = &a->stats_buffer;
  s->size = 80;
  s->abi = a->profile.abi;
  uint64_t generation = handle ? handle->generation : 0;
  uint32_t read = a->api->read_stats(generation, s);
  if (read != 1) {
    invalidate_stats(a);
    *why = native_error(a, "Pose statistics");
    return false;
  }
  if (s->size != 80 || s->abi != a->profile.abi) {
    invalidate_stats(a);
    *why = "Pose statistics ABI mismatch";
    return false;
  }
  uint32_t code = s->error;
  bool installed = s->installed != 0, armed = s->armed != 0, expired = s->expired != 0;
  bool has_now = false;
  double now = 0;
  if (handle && a->owned == handle) {
    double value = 0;
    if (stats_time(a, &value) && value == value && fabs(value) != INFINITY) has_now = true, now = value;
  }
  bool healthy = code == 0 && installed && armed && !expired;
  if (!has_now || !healthy || (a->has_at && now < a->stats_at)) invalidate_stats(a);
  bool shared = a->has_cache && a->stats_handle == handle; /* `out` is the cached table */
  TfpPoseStatsView view;
  memset(&view, 0, sizeof view);
  if (shared) view = a->cache;
  if (!a->has_cache || a->stats_handle != handle || !has_now || now >= a->stats_due) {
    view.seen_position = (double)s->seen_position, view.seen_rotation = (double)s->seen_rotation;
    view.forwarded_position = (double)s->forwarded_position, view.forwarded_rotation = (double)s->forwarded_rotation;
    view.weapon_updates = (double)s->weapon_updates, view.zero_restores = (double)s->zero_restores;
    if (has_now && healthy) {
      a->has_cache = true, a->stats_handle = handle, shared = true;
      /* Stagger display conversion after the other camera helpers. The
       * product goes through a volatile so that no compiler setting can
       * contract it with the addition into one fused multiply-add. */
      double due = now - .085;
      due = due / .1;
      due = floor(due) + 1;
      volatile double product = due * .1;
      due = product + .085;
      if (due > now) a->stats_due = due;
      else a->stats_due = due + .1;
    }
  }
  if (a->has_cache) a->has_at = true, a->stats_at = now;
  view.installed = installed, view.armed = armed, view.expired = expired, view.error = code;
  /* Cheap numeric state stays current even though it is display-only today. */
  view.blend = (double)s->blend, view.target_weight = (double)s->target_weight;
  view.has = true;
  if (shared) a->cache = view;
  *out = view;
  return true;
}

/* pose.lua:141-149. NULL: built; otherwise the text of the failed assertion,
 * which Lua's assert() prefixes with the chunk name and line. */
static const char *build_config(const TfpPoseAdapter *a, const TfpPoseAdapterConfig *config, TfpPoseConfig *c) {
#define AT TFP_POSE_LUA_CHUNK ":146: "
  static const char *const missing[TFP_POSE_ADAPTER_CONFIG_FIELDS] = {
    AT "game unavailable", AT "exe unavailable", AT "context unavailable", AT "request unavailable",
    AT "avatar_owner unavailable", AT "avatar_index unavailable", AT "avatar unavailable", AT "weapon unavailable",
    AT "body unavailable", AT "viewmodel unavailable", AT "duration_ms unavailable", AT "weapon_descriptor unavailable",
    AT "weapon_unit unavailable", AT "blend_ms unavailable", AT "offset_x unavailable", AT "offset_y unavailable",
    AT "offset_z unavailable"};
#undef AT
  if (!config) return TFP_POSE_LUA_CHUNK ":142: Pose configuration unavailable";
  memset(c, 0, sizeof *c);
  c->size = 104;
  c->abi = a->profile.abi;
  c->reserved = 0;
  for (unsigned i = 0; i < TFP_POSE_ADAPTER_CONFIG_FIELDS; i++)
    if (config->missing & (1u << i)) return missing[i];
  c->game = config->game, c->exe = config->exe, c->context = config->context, c->request = config->request;
  c->avatar_owner = config->avatar_owner, c->avatar_index = config->avatar_index, c->avatar = config->avatar;
  c->weapon = config->weapon, c->body = config->body, c->viewmodel = config->viewmodel, c->duration_ms = config->duration_ms;
  c->weapon_descriptor = config->weapon_descriptor, c->weapon_unit = config->weapon_unit, c->blend_ms = config->blend_ms;
  c->offset_x = (float)config->offset_x, c->offset_y = (float)config->offset_y, c->offset_z = (float)config->offset_z;
  return NULL;
}

/* The handle test shared by depth_stats and input_stats (154-156, 184-186). */
static bool diagnostic_handle(const TfpPoseAdapter *a, TfpPoseHandle **handle) {
  TfpPoseHandle *h = *handle;
  if (h && (h->owner != a || h->disarmed || (a->owned && a->owned != h))) return false;
  if (!h) *handle = a->owned;
  return true;
}

/* pose.lua:153-180. Optional console diagnostics only. */
bool tfp_pose_adapter_depth_stats(TfpPoseAdapter *a, TfpPoseHandle *handle, TfpPoseDepthView *out, const char **why) {
  *why = NULL;
  if (!diagnostic_handle(a, &handle)) {
    *why = "Depth statistics handle is not owned";
    return false;
  }
  TfpPoseDepthStats *s = &a->depth_buffer;
  s->size = 104, s->abi = 1, s->reserved = 0;
  uint32_t accepted = a->api->read_depth_stats(handle ? handle->generation : 0, s);
  if (accepted != 1) {
    *why = native_error(a, "Depth statistics");
    return false;
  }
  if (s->size != 104 || s->abi != 1 || s->reserved != 0) {
    *why = "Depth statistics ABI mismatch";
    return false;
  }
  out->installed = s->installed != 0, out->armed = s->armed != 0, out->supported = s->supported != 0;
  out->reason = (double)s->reason, out->zoom_index = (double)s->zoom_index, out->applied = (double)s->applied;
  out->unsupported = (double)s->unsupported, out->cache_refreshes = (double)s->cache_refreshes;
  out->cache_checks = (double)s->cache_checks, out->ads_bypassed = (double)s->ads_bypassed;
  out->native_distance = (double)s->native_distance, out->reference_distance = (double)s->reference_distance;
  out->correction = (double)s->correction, out->zoom = (double)s->zoom, out->target_fov = (double)s->target_fov;
  out->current_fov = (double)s->current_fov, out->gain = (double)s->gain;
  return true;
}

/* pose.lua:183-210. Explicit diagnostics only. */
bool tfp_pose_adapter_input_stats(TfpPoseAdapter *a, TfpPoseHandle *handle, TfpPoseInputView *out, const char **why) {
  *why = NULL;
  if (!diagnostic_handle(a, &handle)) {
    *why = "Input statistics handle is not owned";
    return false;
  }
  TfpPoseInputStats *s = &a->input_buffer;
  s->size = 96, s->abi = 1, s->reserved = 0;
  uint32_t accepted = a->api->read_input_stats(handle ? handle->generation : 0, s);
  if (accepted != 1) {
    *why = native_error(a, "Input statistics");
    return false;
  }
  if (s->size != 96 || s->abi != 1 || s->reserved != 0) {
    *why = "Input statistics ABI mismatch";
    return false;
  }
  out->installed = s->installed != 0, out->active = s->active != 0;
  out->reason = (double)s->reason, out->pending = (double)s->pending, out->cache_entries = (double)s->cache_entries;
  out->observed = (double)s->observed, out->normalized = (double)s->normalized, out->unsupported = (double)s->unsupported;
  out->restored = (double)s->restored, out->foreign_wins = (double)s->foreign_wins, out->quarantined = (double)s->quarantined;
  out->ratio = (double)s->ratio, out->yaw_gain = (double)s->yaw_gain, out->pitch_gain = (double)s->pitch_gain;
  return true;
}

/* pose.lua:212-222. */
TfpPoseAnswer tfp_pose_adapter_prepare(TfpPoseAdapter *a, const TfpPoseAdapterConfig *config, TfpPosePrepared *prepared, const char **why) {
  *why = NULL;
  invalidate_stats(a);
  TfpPoseStatsView stats, after;
  if (!tfp_pose_adapter_stats(a, NULL, &stats, why)) return TFP_POSE_FALSE;
  if (stats.armed) {
    *why = "Another pose lease is already armed";
    return TFP_POSE_FALSE;
  }
  TfpPoseConfig c;
  if ((*why = build_config(a, config, &c)) != NULL) return TFP_POSE_RAISED;
  uint32_t valid = a->profile.normalize_zoom_input ? a->api->validate_input(&c) : a->api->validate(&c);
  if (valid != 1) {
    *why = native_error(a, "Pose read-only validation");
    return TFP_POSE_FALSE;
  }
  if (!tfp_pose_adapter_stats(a, NULL, &after, why)) return TFP_POSE_FALSE;
  if (after.armed || after.installed != stats.installed) {
    *why = "Pose helper state changed during read-only preparation";
    return TFP_POSE_FALSE;
  }
  prepared->owner = a;
  return TFP_POSE_TRUE;
}

/* pose.lua:224-249. */
TfpPoseAnswer tfp_pose_adapter_begin(TfpPoseAdapter *a, const TfpPoseAdapterConfig *config, const TfpPosePrepared *prepared,
                                     TfpPoseHandle *handle, const char **why) {
  *why = NULL;
  invalidate_stats(a);
  if (!prepared || prepared->owner != a) {
    *why = TFP_POSE_LUA_CHUNK ":226: Invalid native pose preparation";
    return TFP_POSE_RAISED;
  }
  if (a->owned) {
    *why = "Previous pose generation still requires cleanup";
    return TFP_POSE_FALSE;
  }
  TfpPoseConfig c;
  if ((*why = build_config(a, config, &c)) != NULL) return TFP_POSE_RAISED;
  /* The native helper publishes this opaque generation before pointer CAS. */
  memset(handle, 0, sizeof *handle);
  handle->owner = a;
  uint32_t result;
  if (a->profile.normalize_zoom_input)
    result = config->steady_entry ? a->api->begin_steady_input(&c, &handle->generation) : a->api->begin_input(&c, &handle->generation);
  else result = config->steady_entry ? a->api->begin_steady(&c, &handle->generation) : a->api->begin(&c, &handle->generation);
  if (handle->generation != 0) a->owned = handle;
  if ((result == 1 || result == 2) && !a->owned) {
    *why = "Pose arm returned no owned generation";
    return TFP_POSE_FALSE;
  }
  if (result == 2) {
    *why = native_error(a, "Pose partial install cleanup required");
    return TFP_POSE_FALSE;
  }
  if (result != 1) {
    *why = native_error(a, "Pose arm");
    return TFP_POSE_FALSE;
  }
  a->owned = handle;
  return TFP_POSE_TRUE;
}

/* The handle test of every setter (252, 263, 275, 301, 312). */
static bool owned(const TfpPoseAdapter *a, const TfpPoseHandle *handle) {
  return handle && handle->owner == a && a->owned == handle && !handle->disarmed;
}

/* pose.lua:251-261. */
bool tfp_pose_adapter_set_placement(TfpPoseAdapter *a, TfpPoseHandle *handle, const double *x, const double *y, const double *z,
                                    const char **why) {
  *why = NULL;
  if (!owned(a, handle)) {
    *why = "Pose placement handle is not owned";
    return false;
  }
  const double *values[3] = {x, y, z};
  for (unsigned i = 0; i < 3 && values[i]; i++) { /* ipairs stops at the first nil */
    double value = *values[i];
    if (value != value || value < -.5 || value > .5) {
      *why = "Pose placement must be finite XYZ within 0.5 metres";
      return false;
    }
  }
  if (!x || !y || !z) {
    *why = "Pose placement requires XYZ";
    return false;
  }
  if (a->api->set_placement(handle->generation, (float)*x, (float)*y, (float)*z) != 1) {
    *why = native_error(a, "Pose placement");
    return false;
  }
  return true;
}

/* pose.lua:262-272. */
bool tfp_pose_adapter_set_ads_placement(TfpPoseAdapter *a, TfpPoseHandle *handle, const double *x, const double *y, const double *z,
                                        const char **why) {
  *why = NULL;
  if (!owned(a, handle)) {
    *why = "ADS placement handle is not owned";
    return false;
  }
  const double *values[3] = {x, y, z};
  for (unsigned i = 0; i < 3; i++) { /* pairs skips a nil and goes on */
    if (!values[i]) continue;
    double n = *values[i];
    if (n != n || fabs(n) > .5) {
      *why = "Invalid ADS placement";
      return false;
    }
  }
  if (!x || !y || !z) {
    *why = "ADS placement requires XYZ";
    return false;
  }
  if (a->api->set_ads_placement(handle->generation, (float)*x, (float)*y, (float)*z) != 1) {
    *why = native_error(a, "ADS placement");
    return false;
  }
  return true;
}

/* pose.lua:274-298. */
bool tfp_pose_adapter_set_motion(TfpPoseAdapter *a, TfpPoseHandle *handle, const double *dx, const double *dy, const double *dz,
                                 const double *qx, const double *qy, const double *qz, const double *qw, bool authored_ads,
                                 const char **why) {
  static const double identity[4] = {0, 0, 0, 1};
  *why = NULL;
  if (!owned(a, handle)) {
    *why = "Pose motion handle is not owned";
    return false;
  }
  if (!dx || !dy || !dz) {
    *why = "Pose motion requires XYZ";
    return false;
  }
  const double *offset[3] = {dx, dy, dz};
  for (unsigned i = 0; i < 3; i++) {
    double value = *offset[i];
    if (value != value || value < -.15 || value > .15) {
      *why = "Pose motion must be finite XYZ within 0.15 metres";
      return false;
    }
  }
  if (!qx && !qy && !qz && !qw) qx = &identity[0], qy = &identity[1], qz = &identity[2], qw = &identity[3];
  const double *q[4] = {qx, qy, qz, qw};
  double norm = 0;
  for (unsigned i = 0; i < 4; i++) {
    if (!q[i] || *q[i] != *q[i] || fabs(*q[i]) > 1.001) {
      *why = "Motion requires a finite unit quaternion";
      return false;
    }
    volatile double square = *q[i] * *q[i]; /* volatile: never fused with the addition */
    norm = norm + square;
  }
  if (fabs(norm - 1) > .002 || fabs(*qw) / sqrt(norm) < .5) {
    *why = "Motion rotation must be a unit quaternion within 120 degrees";
    return false;
  }
  uint32_t done = authored_ads
    ? a->api->set_authored_transform(handle->generation, (float)*dx, (float)*dy, (float)*dz, (float)*qx, (float)*qy, (float)*qz, (float)*qw)
    : a->api->set_transform(handle->generation, (float)*dx, (float)*dy, (float)*dz, (float)*qx, (float)*qy, (float)*qz, (float)*qw);
  if (done != 1) {
    *why = native_error(a, "Pose motion");
    return false;
  }
  return true;
}

/* pose.lua:300-309. */
bool tfp_pose_adapter_set_zoom_policy(TfpPoseAdapter *a, TfpPoseHandle *handle, const bool *active, const char **why) {
  *why = NULL;
  if (!owned(a, handle)) {
    *why = "Pose zoom policy handle is not owned";
    return false;
  }
  if (!active) {
    *why = "Pose zoom policy requires a Boolean state";
    return false;
  }
  if (a->api->set_zoom_policy(handle->generation, *active ? 1 : 0) != 1) {
    *why = native_error(a, "Pose zoom policy");
    return false;
  }
  return true;
}

/* pose.lua:311-318. */
bool tfp_pose_adapter_transition(TfpPoseAdapter *a, TfpPoseHandle *handle, const TfpPosePhasePacket *packet, const char **why) {
  *why = NULL;
  if (!owned(a, handle)) {
    *why = "Pose transition handle is not owned";
    return false;
  }
  /* epoch%1~=0: not a whole number (NaN included; an infinity is caught by the bounds). */
  if (!packet || !packet->has_epoch || packet->epoch < 1 || packet->epoch != floor(packet->epoch) || packet->epoch > 9007199254740991.0 ||
      !packet->has_progress || packet->progress != packet->progress || packet->progress < 0 || packet->progress > 1 || !packet->has_ads) {
    *why = "Invalid shared presentation phase";
    return false;
  }
  if (a->api->set_transition(handle->generation, (uint64_t)packet->epoch, (float)packet->progress, packet->ads ? 1 : 0) != 1) {
    *why = native_error(a, "Pose transition");
    return false;
  }
  return true;
}

/* pose.lua:322-334. */
bool tfp_pose_adapter_finish(TfpPoseAdapter *a, TfpPoseHandle *handle, TfpPoseStatsView *stats, const char **why) {
  *why = NULL;
  stats->has = false;
  if (!handle || handle->owner != a) {
    *why = "Pose handle is not owned by this adapter";
    return false;
  }
  if (handle->disarmed) {
    *stats = handle->stats;
    return true;
  }
  if (a->owned != handle) {
    *why = "Pose helper lease ownership changed";
    return false;
  }
  TfpPoseStatsView before, after;
  const char *ignored;
  invalidate_stats(a);
  tfp_pose_adapter_stats(a, handle, &before, &ignored);
  invalidate_stats(a);
  if (a->api->end(handle->generation) != 1) {
    *why = native_error(a, "Pose disarm");
    return false;
  }
  handle->disarmed = true;
  a->owned = NULL;
  invalidate_stats(a);
  tfp_pose_adapter_stats(a, NULL, &after, &ignored);
  handle->stats = after.has ? after : before;
  *stats = handle->stats;
  return true;
}
