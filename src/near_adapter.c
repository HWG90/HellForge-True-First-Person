/* Translation of retail src/first_person/near_clip.lua (0.5.41-smooth.66,
 * commit 1d0d453). Line numbers in comments are that file's. See near_adapter.h. */
#include "near_adapter.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void tfp_near_adapter_init(TfpNearAdapter *a, const TfpNearHelperApi *api, TfpNearProfile profile) {
  memset(a, 0, sizeof *a); /* Adapter.new: owned=false; a new stats buffer is zero */
  a->profile = profile;
  a->api = api;
}

/* near_clip.lua:47-50. */
static const char *native_error(TfpNearAdapter *a, const char *operation) {
  uint32_t code = a->api->error();
  snprintf(a->text, sizeof a->text, "%s failed (native near clip code %u)", operation, (unsigned)code);
  return a->text;
}

/* near_clip.lua:51-58. NULL: built; otherwise the text of the failed assertion, which Lua's
 * assert() prefixes with the chunk name and line. */
static const char *build_config(const TfpNearAdapter *a, const TfpNearCamera *config, double hip, double aim, TfpNearConfig *c) {
#define AT TFP_NEAR_LUA_CHUNK ":55: "
  static const char *const missing[] = {AT "game unavailable", AT "exe unavailable", AT "context unavailable",
                                        AT "camera unavailable", AT "avatar_owner unavailable", AT "avatar_index unavailable"};
#undef AT
  if (!config) return TFP_NEAR_LUA_CHUNK ":52: Near clip configuration unavailable";
  memset(c, 0, sizeof *c);
  c->size = 72;
  c->abi = a->profile.abi;
  c->reserved = 0;
  c->reserved2 = 0;
  for (unsigned i = 0; i < 6; i++)
    if (config->missing & (1u << i)) return missing[i];
  c->game = config->game, c->exe = config->exe, c->context = config->context, c->camera = config->camera;
  c->avatar_owner = config->avatar_owner, c->avatar_index = config->avatar_index;
  c->hip = (float)hip, c->aim = (float)aim;
  return NULL;
}

/* near_clip.lua:59-74. */
bool tfp_near_adapter_stats(TfpNearAdapter *a, TfpNearHandle *handle, TfpNearStatsView *out, const char **why) {
  *why = NULL;
  out->has = false;
  if (handle && (handle->owner != a || (a->owned && a->owned != handle))) {
    *why = "Near clip statistics handle is not owned";
    return false;
  }
  if (handle && handle->disarmed) {
    *out = handle->stats;
    return out->has;
  }
  TfpNearStats *s = &a->buffer;
  s->size = 88;
  s->abi = a->profile.abi;
  if (a->api->read_stats(handle ? handle->generation : 0, s) != 1) {
    *why = native_error(a, "Near clip statistics");
    return false;
  }
  if (s->size != 88 || s->abi != a->profile.abi) {
    *why = "Near clip statistics ABI changed";
    return false;
  }
  out->seen = (double)s->seen, out->forwarded = (double)s->forwarded, out->replaced = (double)s->replaced;
  out->ads_forwarded = (double)s->ads_forwarded, out->aim_applied = (double)s->aim_applied;
  out->other_kind = (double)s->other_kind, out->last_native = (double)s->last_native;
  out->last_applied = (double)s->last_applied, out->hip = (double)s->hip, out->aim = (double)s->aim;
  out->installed = s->installed != 0, out->armed = s->armed != 0, out->error = s->error;
  out->has = true;
  return true;
}

/* near_clip.lua:75-86. */
bool tfp_near_adapter_begin(TfpNearAdapter *a, const TfpNearCamera *config, double hip, double aim, TfpNearHandle *handle,
                            const char **why) {
  *why = NULL;
  if (a->owned) {
    *why = "Near clip generation still requires cleanup";
    return false;
  }
  TfpNearConfig c;
  if ((*why = build_config(a, config, hip, aim, &c)) != NULL) return false;
  memset(handle, 0, sizeof *handle);
  handle->owner = a;
  uint32_t result = a->api->begin(&c, &handle->generation);
  if (handle->generation != 0) a->owned = handle;
  if ((result == 1 || result == 2) && !a->owned) {
    *why = "Near clip arm returned no owned generation";
    return false;
  }
  if (result != 1) {
    *why = native_error(a, result == 2 ? "Near clip partial installation cleanup" : "Near clip arm");
    return false;
  }
  return true;
}

/* near_clip.lua:87-93. */
bool tfp_near_adapter_set(TfpNearAdapter *a, TfpNearHandle *handle, double hip, double aim, const char **why) {
  *why = NULL;
  if (!handle || handle->owner != a || a->owned != handle || handle->disarmed) {
    *why = "Near clip handle is not owned";
    return false;
  }
  if (a->api->set(handle->generation, (float)hip, (float)aim) != 1) {
    *why = native_error(a, "Near clip distance update");
    return false;
  }
  return true;
}

/* near_clip.lua:95-105. */
bool tfp_near_adapter_finish(TfpNearAdapter *a, TfpNearHandle *handle, TfpNearStatsView *stats, const char **why) {
  *why = NULL;
  stats->has = false;
  if (!handle || handle->owner != a) {
    *why = "Near clip cleanup handle is not owned";
    return false;
  }
  if (handle->disarmed) {
    *stats = handle->stats;
    return true;
  }
  if (a->owned != handle) {
    *why = "Near clip generation ownership changed";
    return false;
  }
  TfpNearStatsView before, after;
  const char *ignored;
  tfp_near_adapter_stats(a, handle, &before, &ignored);
  if (a->api->end(handle->generation) != 1) {
    *why = native_error(a, "Near clip cleanup");
    return false;
  }
  handle->disarmed = true;
  a->owned = NULL;
  tfp_near_adapter_stats(a, NULL, &after, &ignored);
  handle->stats = after.has ? after : before;
  *stats = handle->stats;
  return true;
}

/* ---- the lease: Near.new, near_clip.lua:113-225 ---- */
static bool refuse(TfpNearLease *l, const char *text) {
  if (!text) l->why = NULL;
  else {
    if (text != l->reply) snprintf(l->reply, sizeof l->reply, "%s", text);
    l->why = l->reply;
  }
  return false;
}
static bool accept(TfpNearLease *l) {
  l->why = NULL;
  return true;
}
/* near_clip.lua:122-126: a refusal is logged once per distinct message. */
static bool problem(TfpNearLease *l, const char *message) {
  char copy[TFP_NEAR_TEXT];
  snprintf(copy, sizeof copy, "%s", message);
  if (!l->has_fault || strcmp(copy, l->fault)) {
    if (l->io->log) {
      char line[TFP_NEAR_TEXT + 24];
      snprintf(line, sizeof line, "Native near clip: %s", copy);
      l->io->log(l->io->user, line);
    }
  }
  memcpy(l->fault, copy, sizeof copy);
  l->has_fault = true;
  return refuse(l, l->fault);
}
/* near_clip.lua:127-134. */
static void wanted(TfpNearLease *l, double *hip, double *aim) {
  TfpNearOptions value;
  memset(&value, 0, sizeof value);
  l->io->options(l->io->user, &value);
  bool hip_number = value.is_table && value.hip_is_number, aim_number = value.is_table && value.aim_is_number;
  *hip = hip_number ? value.hip : 0;
  *aim = aim_number ? value.aim : 0;
  if (*hip != *hip || *hip < 0 || *hip > 1) *hip = 0;
  if (*aim != *aim || *aim < 0 || *aim > 1) *aim = 0;
}
/* near_clip.lua:135-139. */
static bool collect(TfpNearLease *l) {
  TfpNearStatsView stats;
  const char *why;
  tfp_near_adapter_stats(&l->helper, l->handle, &stats, &why);
  if (!stats.has) return problem(l, why ? why : "Near clip statistics unavailable");
  l->last = stats;
  return accept(l);
}
static void clear_binding(TfpNearLease *l) {
  l->bound_player = 0, l->has_hip = l->has_aim = false, l->hip = l->aim = 0;
  l->phase = TFP_NEAR_LEASE_IDLE;
}
/* near_clip.lua:152-161. */
static bool stop(TfpNearLease *l) {
  l->has_next_collect = false, l->next_collect = 0;
  if (!l->handle) {
    clear_binding(l);
    return accept(l);
  }
  l->phase = TFP_NEAR_LEASE_CLEANUP_PENDING;
  TfpNearStatsView stats;
  const char *text;
  if (!tfp_near_adapter_finish(&l->helper, l->handle, &stats, &text))
    return problem(l, text ? text : "Near clip cleanup remains pending");
  if (stats.has) l->last = stats;
  l->handle = NULL;
  clear_binding(l);
  return accept(l);
}
/* `self:stop(); return problem(text)` with a text that stop() may overwrite. */
static bool stop_then_problem(TfpNearLease *l, const char *text) {
  char keep[TFP_NEAR_TEXT];
  snprintf(keep, sizeof keep, "%s", text);
  stop(l);
  return problem(l, keep);
}
/* `local ok,why=collect(); if not ok then self:stop(); return false,why end` */
static bool stop_keeping(TfpNearLease *l) {
  char keep[TFP_NEAR_TEXT];
  bool had = l->why != NULL;
  snprintf(keep, sizeof keep, "%s", had ? l->why : "");
  stop(l);
  return refuse(l, had ? keep : NULL);
}
static TfpNearAnswer raised(TfpNearLease *l, const char *error) {
  refuse(l, error ? error : "");
  return TFP_NEAR_RAISED;
}
static const char *number_text(double value, char *out, size_t size);

/* near_clip.lua:163-223. */
static TfpNearAnswer update(TfpNearLease *l) {
  if (l->phase == TFP_NEAR_LEASE_CLEANUP_PENDING) return stop(l);
  TfpNearControllerState result;
  l->io->controller_result(l->io->user, &result);
  double hip, aim, now = 0;
  const char *error = NULL;
  wanted(l, &hip, &aim);
  /* With pull_sight_distance the helper stays armed at zero distances. */
  bool pull = l->helper.profile.pull_sight_distance;
  if (l->closing || !l->io->controller_enabled(l->io->user) || !result.pose_offset || (hip == 0 && aim == 0 && !pull)) {
    l->has_retry_at = false, l->retry_at = 0;
    return stop(l);
  }
  const TfpNearPoseLease *lease = l->io->controller_lease(l->io->user);
  if (!lease || !result.running) {
    /* Like the FOV lens, survive weapon gaps. */
    if (l->handle) {
      if (!collect(l)) return false;
      if (l->last.has && !l->last.armed) return stop(l);
    }
    return accept(l);
  }
  uint64_t player = lease->has_origin ? lease->origin_player_identity : 0;
  if (l->handle && l->bound_player && player != l->bound_player) {
    if (!stop(l)) return false;
  }
  if (l->handle) {
    if (!l->has_hip || hip != l->hip || !l->has_aim || aim != l->aim) {
      const char *why;
      if (!tfp_near_adapter_set(&l->helper, l->handle, hip, aim, &why))
        return stop_then_problem(l, why ? why : "Near clip distance update refused");
      l->has_hip = l->has_aim = true, l->hip = hip, l->aim = aim;
      l->has_next_collect = false, l->next_collect = 0;
    }
    /* Health is polled at 4 Hz; the helper disarms itself immediately. */
    if (!l->io->clock(l->io->user, &now, &error)) return raised(l, error);
    if (l->has_next_collect && now >= l->last_collect && now < l->next_collect) return accept(l);
    l->last_collect = now, l->has_next_collect = true, l->next_collect = now + .25;
    if (!collect(l)) return stop_keeping(l);
    if (!(l->last.has && l->last.armed)) {
      char text[64], code[32];
      snprintf(text, sizeof text, "Native near clip disarmed (code %s)", l->last.has ? number_text(l->last.error, code, sizeof code) : "nil");
      return stop_then_problem(l, text);
    }
    return accept(l);
  }
  if (!l->io->clock(l->io->user, &now, &error)) return raised(l, error);
  if (l->has_retry_at && now < l->retry_at) return accept(l);
  l->has_retry_at = true, l->retry_at = now + 1;
  TfpNearCamera config;
  const char *why = NULL;
  memset(&config, 0, sizeof config);
  if (!l->io->fov_config(l->io->user, lease, &config, &why)) return problem(l, why ? why : "Owned near-clip camera is unavailable");
  const char *value = NULL;
  bool done = tfp_near_adapter_begin(&l->helper, &config, hip, aim, &l->storage, &value);
  if (done) l->handle = &l->storage;
  else if (l->helper.owned) l->handle = l->helper.owned;
  if (!done || !l->handle) return stop_then_problem(l, value ? value : "Native near clip was not accepted");
  l->has_hip = l->has_aim = true, l->hip = hip, l->aim = aim;
  l->bound_player = player, l->phase = TFP_NEAR_LEASE_ACTIVE, l->has_fault = false, l->has_retry_at = false, l->retry_at = 0;
  if (!collect(l)) return stop_keeping(l);
  if (!(l->last.has && l->last.armed)) return stop_then_problem(l, "Native near clip did not remain armed");
  return accept(l);
}

void tfp_near_lease_init(TfpNearLease *l, const TfpNearHelperApi *api, const TfpNearIo *io, TfpNearProfile profile) {
  memset(l, 0, sizeof *l); /* near_clip.lua:120: phase='idle', last={}, last_collect=0, the rest nil */
  l->io = io;
  tfp_near_adapter_init(&l->helper, api, profile);
}
TfpNearAnswer tfp_near_lease_update(TfpNearLease *l, const char **why) {
  TfpNearAnswer answer = update(l);
  if (why) *why = l->why;
  return answer;
}
bool tfp_near_lease_stop(TfpNearLease *l, const char **why) {
  bool done = stop(l);
  if (why) *why = l->why;
  return done;
}
bool tfp_near_lease_shutdown(TfpNearLease *l, const char **why) { /* near_clip.lua:162 */
  l->closing = true;
  return tfp_near_lease_stop(l, why);
}
/* near_clip.lua:141-146. */
void tfp_near_lease_result(const TfpNearLease *l, TfpNearResult *out) {
  out->stats = l->last;
  out->phase = l->phase;
  out->cleanup_pending = l->phase == TFP_NEAR_LEASE_CLEANUP_PENDING;
  out->error_message = l->has_fault ? l->fault : NULL;
}

/* tostring() of a number as LuaJIT prints it. */
static const char *number_text(double value, char *out, size_t size) {
  if (value != value) snprintf(out, size, "nan");
  else if (value == INFINITY) snprintf(out, size, "inf");
  else if (value == -INFINITY) snprintf(out, size, "-inf");
  else snprintf(out, size, "%.14g", value);
  return out;
}
/* near_clip.lua:147-151. */
int tfp_near_lease_status(const TfpNearLease *l, char *out, size_t size) {
  static const char *const phases[] = {"idle", "active", "cleanup_pending"};
  char hip[32], aim[32], native[32], applied[32], replaced[32], forwarded[32];
  const TfpNearStatsView *s = &l->last;
  return snprintf(out, size, "%s; near hip/aim=%s/%s; native/output=%s/%s; clipped=%s; ADS forwarded=%s", phases[l->phase],
                  l->has_hip ? number_text(l->hip, hip, sizeof hip) : "nil", l->has_aim ? number_text(l->aim, aim, sizeof aim) : "nil",
                  s->has ? number_text(s->last_native, native, sizeof native) : "nil",
                  s->has ? number_text(s->last_applied, applied, sizeof applied) : "nil",
                  s->has ? number_text(s->replaced, replaced, sizeof replaced) : "0",
                  s->has ? number_text(s->ads_forwarded, forwarded, sizeof forwarded) : "0");
}
