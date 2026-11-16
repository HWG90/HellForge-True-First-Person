/* Translation of retail src/first_person/fov.lua (0.5.41-smooth.66, commit
 * 1d0d453). Line numbers in comments are that file's. See fov_adapter.h. */
#include "fov_adapter.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void invalidate_stats(TfpFovAdapter *a) { /* fov.lua:10-12 */
  a->has_cache = false;
  a->stats_handle = NULL;
  a->has_at = false;
  a->stats_at = a->stats_due = 0;
}

void tfp_fov_adapter_init(TfpFovAdapter *a, const TfpFovHelperApi *api, const TfpFovIo *io, TfpFovProfile profile) {
  memset(a, 0, sizeof *a); /* Adapter.new: owned=false; a new stats buffer is zero */
  a->profile = profile;
  a->api = api;
  a->io = io;
}

/* fov.lua:48-52. */
static const char *native_error(TfpFovAdapter *a, const char *operation) {
  uint32_t code = a->api->error();
  a->has_last_failure_code = code != 0;
  a->last_failure_code = code;
  snprintf(a->text, sizeof a->text, "%s failed (native FOV code %u)", operation, (unsigned)code);
  return a->text;
}

/* fov.lua:53-60. NULL: built; otherwise the text of the failed assertion, which Lua's
 * assert() prefixes with the chunk name and line. */
static const char *build_config(const TfpFovAdapter *a, const TfpFovCamera *config, TfpFovConfig *c) {
#define AT TFP_FOV_LUA_CHUNK ":57: "
  static const char *const missing[] = {AT "game unavailable", AT "exe unavailable", AT "context unavailable",
                                        AT "camera unavailable", AT "avatar_owner unavailable", AT "request unavailable",
                                        AT "avatar_index unavailable", AT "avatar unavailable", AT "body unavailable",
                                        AT "weapon unavailable"};
#undef AT
  if (!config) return TFP_FOV_LUA_CHUNK ":54: FOV configuration unavailable";
  memset(c, 0, sizeof *c);
  c->size = 80;
  c->abi = a->profile.abi;
  c->reserved = a->profile.ads_blend_ms;
  for (unsigned i = 0; i < 10; i++)
    if (config->missing & (1u << i)) return missing[i];
  c->game = config->game, c->exe = config->exe, c->context = config->context, c->camera = config->camera;
  c->avatar_owner = config->avatar_owner, c->request = config->request, c->avatar_index = config->avatar_index;
  c->avatar = config->avatar, c->body = config->body, c->weapon = config->weapon;
  return NULL;
}

/* fov.lua:61-98. */
bool tfp_fov_adapter_stats(TfpFovAdapter *a, TfpFovHandle *handle, TfpFovStatsView *out, const char **why) {
  *why = NULL;
  out->has = false;
  if (handle && (handle->owner != a || (a->owned && a->owned != handle))) {
    *why = "FOV statistics handle is not owned";
    return false;
  }
  if (handle && handle->disarmed) {
    *out = handle->stats;
    return out->has;
  }
  TfpFovStats *s = &a->buffer;
  s->size = 88;
  s->abi = a->profile.abi;
  uint64_t generation = handle ? handle->generation : 0;
  uint32_t read = a->api->read_stats(generation, s);
  if (read != 1) {
    invalidate_stats(a);
    *why = native_error(a, "FOV statistics");
    return false;
  }
  if (s->size != 88 || s->abi != a->profile.abi) {
    invalidate_stats(a);
    *why = "FOV statistics ABI changed";
    return false;
  }
  /* Ownership and health are read every call; the display counters are shared
   * between the 10 Hz diagnostic ticks. */
  uint32_t code = s->error;
  bool has_now = false;
  double now = 0;
  if (a->io->clock && handle && a->owned == handle) {
    double value = 0;
    if (a->io->clock(a->io->user, &value) && value == value && fabs(value) != INFINITY) has_now = true, now = value;
  }
  if (!has_now || code != 0 || (a->has_at && now < a->stats_at)) invalidate_stats(a);
  bool shared = a->has_cache && a->stats_handle == handle; /* `out` is the cached table */
  TfpFovStatsView view;
  memset(&view, 0, sizeof view);
  if (shared) view = a->cache;
  if (!has_now || code != 0 || !a->has_cache || a->stats_handle != handle || now >= a->stats_due) {
    view.seen = (double)s->seen, view.forwarded = (double)s->forwarded, view.replaced = (double)s->replaced;
    view.immediate_applies = (double)s->immediate_applies, view.binds = (double)s->binds;
    view.ads_forwarded = (double)s->ads_forwarded, view.last_native = (double)s->last_native;
    view.last_applied = (double)s->last_applied, view.source_degrees = (double)s->source_degrees;
    if (has_now && code == 0) {
      a->has_cache = true, a->stats_handle = handle, shared = true;
      double due = floor(now / .1) + 1;
      due = due * .1;
      if (due > now) a->stats_due = due;
      else a->stats_due = due + .1;
    }
  }
  if (has_now && code == 0) a->has_at = true, a->stats_at = now;
  view.installed = s->installed != 0, view.armed = s->armed != 0, view.error = code;
  view.has = true;
  if (shared) a->cache = view;
  *out = view;
  return true;
}

/* fov.lua:99-104. */
bool tfp_fov_adapter_validate(TfpFovAdapter *a, const TfpFovCamera *config, const char **why) {
  *why = NULL;
  invalidate_stats(a);
  if (a->owned) {
    *why = "FOV generation still requires cleanup";
    return false;
  }
  TfpFovConfig c;
  if ((*why = build_config(a, config, &c)) != NULL) return false;
  if (a->api->validate(&c) != 1) {
    *why = native_error(a, "FOV read-only validation");
    return false;
  }
  return true;
}

/* fov.lua:105-117. */
bool tfp_fov_adapter_begin(TfpFovAdapter *a, const TfpFovCamera *config, TfpFovHandle *handle, const char **why) {
  *why = NULL;
  invalidate_stats(a);
  if (a->owned) {
    *why = "FOV generation still requires cleanup";
    return false;
  }
  TfpFovConfig c;
  if ((*why = build_config(a, config, &c)) != NULL) return false;
  memset(handle, 0, sizeof *handle);
  handle->owner = a;
  uint32_t result = a->api->begin(&c, &handle->generation);
  if (handle->generation != 0) a->owned = handle;
  if ((result == 1 || result == 2) && !a->owned) {
    *why = "FOV arm returned no owned generation";
    return false;
  }
  if (result != 1) {
    *why = native_error(a, result == 2 ? "FOV partial installation cleanup" : "FOV arm");
    return false;
  }
  return true;
}

/* fov.lua:118-127. */
bool tfp_fov_adapter_bind(TfpFovAdapter *a, TfpFovHandle *handle, const TfpFovCamera *config, const char **why) {
  *why = NULL;
  invalidate_stats(a);
  if (!handle || handle->owner != a || a->owned != handle || handle->disarmed) {
    *why = "FOV bind handle is not owned";
    return false;
  }
  /* A nil argument: the FFI call raises, in LuaJIT's words. */
  if (config->missing & TFP_FOV_MISSING_REQUEST) {
    *why = TFP_FOV_LUA_CHUNK ":123: bad argument #2 to 'NADSF_Bind' (cannot convert 'nil' to 'uint64_t')";
    return false;
  }
  if (config->missing & TFP_FOV_MISSING_WEAPON) {
    *why = TFP_FOV_LUA_CHUNK ":123: bad argument #3 to 'NADSF_Bind' (cannot convert 'nil' to 'unsigned int')";
    return false;
  }
  if (a->api->bind(handle->generation, config->request, config->weapon) != 1) {
    *why = native_error(a, "FOV camera binding");
    return false;
  }
  return true;
}

/* fov.lua:128-142. */
bool tfp_fov_adapter_transition(TfpFovAdapter *a, TfpFovHandle *handle, const TfpFovPacket *token, const char **why) {
  *why = NULL;
  if (!handle || handle->owner != a || a->owned != handle || handle->disarmed) {
    *why = "FOV transition handle is not owned";
    return false;
  }
  if (!token || token->kind != TFP_FOV_PACKET_TABLE || !token->epoch_is_number || token->epoch < 1 ||
      token->epoch >= 9007199254740992.0 || token->epoch - floor(token->epoch) != 0 || !token->progress_is_number ||
      token->progress != token->progress || token->progress < 0 || token->progress > 1 ||
      token->ads == TFP_FOV_ADS_NOT_BOOLEAN) {
    *why = "FOV shared presentation phase is invalid";
    return false;
  }
  if (a->api->set_transition(handle->generation, (uint64_t)token->epoch, (float)token->progress,
                             token->ads == TFP_FOV_ADS_TRUE ? 1 : 0) != 1) {
    *why = native_error(a, "FOV shared presentation phase");
    return false;
  }
  return true;
}

/* fov.lua:144-156. */
bool tfp_fov_adapter_finish(TfpFovAdapter *a, TfpFovHandle *handle, TfpFovStatsView *stats, const char **why) {
  *why = NULL;
  stats->has = false;
  if (!handle || handle->owner != a) {
    *why = "FOV cleanup handle is not owned";
    return false;
  }
  if (handle->disarmed) {
    *stats = handle->stats;
    return true;
  }
  if (a->owned != handle) {
    *why = "FOV generation ownership changed";
    return false;
  }
  invalidate_stats(a);
  TfpFovStatsView before, after;
  const char *ignored;
  tfp_fov_adapter_stats(a, handle, &before, &ignored);
  invalidate_stats(a);
  if (a->api->end(handle->generation) != 1) {
    *why = native_error(a, "FOV cleanup");
    return false;
  }
  handle->disarmed = true;
  a->owned = NULL;
  invalidate_stats(a);
  tfp_fov_adapter_stats(a, NULL, &after, &ignored);
  handle->stats = after.has ? after : before;
  *stats = handle->stats;
  return true;
}

/* ---- the lease: Fov.new, fov.lua:163-314 ---- */
static bool refuse(TfpFovLease *l, const char *text) {
  if (!text) l->why = NULL;
  else {
    if (text != l->reply) snprintf(l->reply, sizeof l->reply, "%s", text);
    l->why = l->reply;
  }
  return false;
}
static bool accept(TfpFovLease *l) {
  l->why = NULL;
  return true;
}
/* fov.lua:171-177. has_cause false: retail's native_cause is nil. */
static bool problem(TfpFovLease *l, const char *message, bool has_cause, uint32_t cause) {
  if (message != l->fault) {
    char copy[TFP_FOV_TEXT];
    snprintf(copy, sizeof copy, "%s", message);
    memcpy(l->fault, copy, sizeof copy);
  }
  l->has_fault = true;
  if (has_cause) l->has_failure_native_error = true, l->failure_native_error = cause;
  else l->has_failure_native_error = l->helper.has_last_failure_code, l->failure_native_error = l->helper.last_failure_code;
  l->failure_kind = !l->has_failure_native_error ? TFP_FOV_FAILURE_NONE
                    : l->failure_native_error == 3 ? TFP_FOV_FAILURE_TEMPORARY_READ : TFP_FOV_FAILURE_UNSAFE;
  if (l->io->log) {
    char line[TFP_FOV_TEXT + 16];
    snprintf(line, sizeof line, "Native FOV: %s", l->fault);
    l->io->log(l->io->user, line);
  }
  return refuse(l, l->fault);
}
/* fov.lua:178-185. */
static bool collect(TfpFovLease *l) {
  if (!l->handle) return accept(l);
  TfpFovStatsView stats;
  const char *why;
  tfp_fov_adapter_stats(&l->helper, l->handle, &stats, &why);
  if (!stats.has) return problem(l, why ? why : "FOV statistics unavailable", false, 0);
  l->last = stats;
  if (stats.error != 0) l->helper.has_last_failure_code = true, l->helper.last_failure_code = stats.error;
  return accept(l);
}
/* fov.lua:186-195. */
static bool present(TfpFovLease *l) {
  if (!l->handle || !l->io->presentation_transition) return accept(l);
  TfpFovPacket token;
  const char *why = NULL;
  memset(&token, 0, sizeof token);
  l->io->presentation_transition(l->io->user, &token, &why);
  if (token.kind == TFP_FOV_PACKET_NONE) return accept(l);
  if (token.kind != TFP_FOV_PACKET_TABLE) return problem(l, why ? why : "Shared presentation phase unavailable", false, 0);
  const char *reason;
  if (!tfp_fov_adapter_transition(&l->helper, l->handle, &token, &reason))
    return problem(l, reason ? reason : "FOV shared presentation phase was refused", false, 0);
  l->has_presentation = true, l->presentation_epoch = token.epoch, l->presentation_progress = token.progress;
  return accept(l);
}
static void clear_binding(TfpFovLease *l) {
  l->bound = 0, l->has_bound_request = false, l->bound_request = 0, l->has_bound_weapon = false, l->bound_weapon = 0;
  l->bound_player = 0;
  l->has_presentation = false, l->presentation_epoch = l->presentation_progress = 0;
  l->phase = TFP_FOV_LEASE_IDLE;
}
/* fov.lua:213-222. */
static bool stop(TfpFovLease *l) {
  if (!l->handle) {
    clear_binding(l);
    return accept(l);
  }
  l->phase = TFP_FOV_LEASE_CLEANUP_PENDING;
  bool has_cause = l->has_failure_native_error;
  uint32_t cause = l->failure_native_error;
  TfpFovStatsView stats;
  const char *text;
  if (!tfp_fov_adapter_finish(&l->helper, l->handle, &stats, &text))
    return problem(l, text ? text : "FOV cleanup remains pending", has_cause, cause);
  if (stats.has) l->last = stats;
  l->handle = NULL;
  clear_binding(l);
  return accept(l);
}
static void clear_failure(TfpFovLease *l) { /* fov.lua:225, 234 */
  l->helper.has_last_failure_code = false, l->helper.last_failure_code = 0;
  l->has_failure_native_error = false, l->failure_native_error = 0, l->failure_kind = TFP_FOV_FAILURE_NONE;
}
/* The repeated tail `problem(why); local done,issue=self:stop(); return false,done==true and why or issue`. */
static bool fault_and_stop(TfpFovLease *l, const char *why, bool has_cause, uint32_t cause) {
  char keep[TFP_FOV_TEXT];
  problem(l, why, has_cause, cause);
  snprintf(keep, sizeof keep, "%s", l->fault);
  if (stop(l)) return refuse(l, keep);
  return false; /* the issue stop() reported */
}
/* `local ok,why=…; if not ok then self:stop(); return false,why end` */
static bool stop_keeping(TfpFovLease *l) {
  char keep[TFP_FOV_TEXT];
  bool had = l->why != NULL;
  snprintf(keep, sizeof keep, "%s", had ? l->why : "");
  stop(l);
  return refuse(l, had ? keep : NULL);
}
static bool same_optional(bool has_a, uint64_t a, bool has_b, uint64_t b) { return has_a == has_b && (!has_a || a == b); }

/* fov.lua:233-312. */
static bool update(TfpFovLease *l) {
  clear_failure(l);
  if (l->phase == TFP_FOV_LEASE_CLEANUP_PENDING) return stop(l);
  TfpFovControllerState result;
  l->io->controller_result(l->io->user, &result);
  if (l->closing || !l->io->controller_enabled(l->io->user) || !result.pose_offset) return stop(l);
  const TfpFovPoseLease *lease = l->io->controller_lease(l->io->user);
  if (!lease || !result.running) {
    /* The camera policy survives helper suspension and weapon gaps. */
    if (l->handle) {
      if (!collect(l)) return false;
      if (l->last.has && !l->last.armed) {
        uint32_t cause = l->last.error;
        if (cause != 0 && cause != 7) return fault_and_stop(l, "Native FOV policy disarmed while waiting", true, cause);
        return stop(l);
      }
      if (!present(l)) return stop_keeping(l);
    }
    return accept(l);
  }
  uint64_t player = lease->has_origin ? lease->origin_player_identity : 0;
  if (l->handle && l->bound_player && player != l->bound_player) {
    if (!stop(l)) return false;
  }
  bool has_weapon = lease->has_origin && lease->origin_has_weapon ? true : lease->has_weapon;
  uint64_t weapon = lease->has_origin && lease->origin_has_weapon ? lease->origin_weapon : lease->weapon;
  if (l->handle && l->bound == lease->identity &&
      same_optional(l->has_bound_request, l->bound_request, lease->has_request, lease->request) &&
      same_optional(l->has_bound_weapon, l->bound_weapon, has_weapon, weapon)) {
    if (!collect(l)) return stop_keeping(l);
    if (!(l->last.has && l->last.armed) || (l->last.has && l->last.error != 0))
      return fault_and_stop(l, "Native FOV policy disarmed or rejected ownership", false, 0);
    if (!present(l)) return stop_keeping(l);
    return accept(l);
  }
  TfpFovCamera config;
  const char *why = NULL;
  memset(&config, 0, sizeof config);
  if (!l->io->fov_config(l->io->user, lease, &config, &why))
    return problem(l, why ? why : "Owned FOV camera is unavailable", false, 0);
  bool had_handle = l->handle != NULL;
  if (had_handle) {
    /* A callback may have disarmed since the preceding update: read health
     * before a control call can replace its typed error. */
    if (!collect(l)) return stop_keeping(l);
    if (!(l->last.has && l->last.armed) || (l->last.has && l->last.error != 0))
      return fault_and_stop(l, "Native FOV policy retired before request binding", false, 0);
    /* Bind can submit a lens at once: publish the current phase first. */
    if (!present(l)) return stop_keeping(l);
  }
  bool done;
  const char *value = NULL;
  bool has_request = !(config.missing & TFP_FOV_MISSING_REQUEST), has_config_weapon = !(config.missing & TFP_FOV_MISSING_WEAPON);
  if (l->handle) {
    if (same_optional(l->has_bound_request, l->bound_request, has_request, config.request) &&
        same_optional(l->has_bound_weapon, l->bound_weapon, has_config_weapon, config.weapon))
      done = true;
    else done = tfp_fov_adapter_bind(&l->helper, l->handle, &config, &value);
  } else {
    done = tfp_fov_adapter_begin(&l->helper, &config, &l->storage, &value);
    if (done) l->handle = &l->storage;
    else if (l->helper.owned) l->handle = l->helper.owned;
  }
  if (!done || !l->handle) return fault_and_stop(l, value ? value : "Native FOV policy was not accepted", false, 0);
  l->bound = lease->identity, l->phase = TFP_FOV_LEASE_ACTIVE, l->has_fault = false;
  l->has_bound_request = has_request, l->bound_request = has_request ? config.request : 0;
  l->has_bound_weapon = has_config_weapon, l->bound_weapon = has_config_weapon ? config.weapon : 0;
  l->bound_player = player;
  if (!collect(l)) return stop_keeping(l);
  if (!(l->last.has && l->last.armed) || (l->last.has && l->last.error != 0))
    return fault_and_stop(l, "Native FOV policy did not remain armed", false, 0);
  if (!had_handle) {
    if (!present(l)) return stop_keeping(l);
  }
  return accept(l);
}

void tfp_fov_lease_init(TfpFovLease *l, const TfpFovHelperApi *api, const TfpFovIo *io, TfpFovProfile profile) {
  memset(l, 0, sizeof *l); /* fov.lua:169: handle=nil, closing=false, fault=nil, last={}, phase='idle' */
  l->io = io;
  tfp_fov_adapter_init(&l->helper, api, io, profile);
}
static bool reply(const TfpFovLease *l, bool result, const char **why) {
  if (why) *why = l->why;
  return result;
}
bool tfp_fov_lease_update(TfpFovLease *l, const char **why) { return reply(l, update(l), why); }
bool tfp_fov_lease_stop(TfpFovLease *l, const char **why) { return reply(l, stop(l), why); }
bool tfp_fov_lease_shutdown(TfpFovLease *l, const char **why) { /* fov.lua:223 */
  l->closing = true;
  return reply(l, stop(l), why);
}
/* fov.lua:224-232. */
bool tfp_fov_lease_preflight(TfpFovLease *l, const char **why) {
  clear_failure(l);
  if (l->handle) return reply(l, refuse(l, "FOV cleanup is still active"), why);
  TfpFovCamera config;
  const char *text = NULL;
  memset(&config, 0, sizeof config);
  if (!l->io->fov_config(l->io->user, NULL, &config, &text))
    return reply(l, refuse(l, text ? text : "FOV configuration unavailable"), why);
  const char *reason;
  if (!tfp_fov_adapter_validate(&l->helper, &config, &reason))
    return reply(l, problem(l, reason ? reason : "FOV validation refused", false, 0), why);
  return reply(l, accept(l), why);
}
/* fov.lua:197-207. */
void tfp_fov_lease_result(const TfpFovLease *l, TfpFovResult *out) {
  out->stats = l->last;
  out->phase = l->phase;
  out->cleanup_pending = l->phase == TFP_FOV_LEASE_CLEANUP_PENDING;
  out->error_message = l->has_fault ? l->fault : NULL;
  out->has_failure_native_error = l->has_failure_native_error, out->failure_native_error = l->failure_native_error;
  out->failure_kind = l->failure_kind;
  out->has_presentation = l->has_presentation;
  out->presentation_epoch = out->transition_epoch = l->presentation_epoch;
  out->presentation_progress = out->transition_progress = l->presentation_progress;
}

/* tostring() of a number as LuaJIT prints it. */
static const char *number_text(double value, char *out, size_t size) {
  if (value != value) snprintf(out, size, "nan");
  else if (value == INFINITY) snprintf(out, size, "inf");
  else if (value == -INFINITY) snprintf(out, size, "-inf");
  else snprintf(out, size, "%.14g", value);
  return out;
}
/* fov.lua:208-212. */
int tfp_fov_lease_status(const TfpFovLease *l, char *out, size_t size) {
  static const char *const phases[] = {"idle", "active", "cleanup_pending"};
  char degrees[32], native[32], applied[32], replaced[32], forwarded[32];
  const TfpFovStatsView *s = &l->last;
  return snprintf(out, size, "%s; TPV degrees=%s; native/output=%s/%s; overrides=%s; ADS forwarded=%s", phases[l->phase],
                  s->has ? number_text(s->source_degrees, degrees, sizeof degrees) : "nil",
                  s->has ? number_text(s->last_native, native, sizeof native) : "nil",
                  s->has ? number_text(s->last_applied, applied, sizeof applied) : "nil",
                  s->has ? number_text(s->replaced, replaced, sizeof replaced) : "0",
                  s->has ? number_text(s->ads_forwarded, forwarded, sizeof forwarded) : "0");
}
