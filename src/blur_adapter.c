/* Translation of retail src/first_person/blur.lua (0.5.41-smooth.66, commit
 * 1d0d453). Line numbers in comments are that file's. See blur_adapter.h. */
#include "blur_adapter.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void invalidate_stats(TfpBlurAdapter *a) { /* blur.lua:12-14 */
  a->has_cache = false;
  a->stats_handle = NULL;
  a->has_at = false;
  a->stats_at = a->stats_due = 0;
}

void tfp_blur_adapter_init(TfpBlurAdapter *a, const TfpBlurHelperApi *api, const TfpBlurIo *io, TfpBlurProfile profile) {
  memset(a, 0, sizeof *a); /* Adapter.new: owned=false; a new stats buffer is zero */
  a->profile = profile;
  a->api = api;
  a->io = io;
}

/* blur.lua:50-55: the text and, as a second result, the helper's error number. */
static const char *native_error(TfpBlurAdapter *a, const char *operation, TfpBlurCode *code) {
  uint32_t value = a->api->error();
  a->failure_native_error = (TfpBlurCode){true, value};
  if (code) *code = a->failure_native_error;
  snprintf(a->text, sizeof a->text, "%s failed (native blur code %u)", operation, (unsigned)value);
  return a->text;
}

/* blur.lua:56-63. NULL: built; otherwise the text of the failed assertion, which Lua's
 * assert() prefixes with the chunk name and line. */
static const char *build_config(const TfpBlurAdapter *a, const TfpBlurCamera *config, TfpBlurConfig *c) {
#define AT TFP_BLUR_LUA_CHUNK ":60: "
  static const char *const missing[] = {AT "game unavailable", AT "exe unavailable", AT "context unavailable",
                                        AT "camera unavailable", AT "avatar_owner unavailable", AT "request unavailable",
                                        AT "avatar_index unavailable", AT "avatar unavailable", AT "body unavailable",
                                        AT "weapon unavailable"};
#undef AT
  if (!config) return TFP_BLUR_LUA_CHUNK ":57: Blur configuration unavailable";
  memset(c, 0, sizeof *c);
  c->size = 80;
  c->abi = a->profile.abi;
  c->reserved = 0;
  for (unsigned i = 0; i < 10; i++)
    if (config->missing & (1u << i)) return missing[i];
  c->game = config->game, c->exe = config->exe, c->context = config->context, c->camera = config->camera;
  c->avatar_owner = config->avatar_owner, c->request = config->request, c->avatar_index = config->avatar_index;
  c->avatar = config->avatar, c->body = config->body, c->weapon = config->weapon;
  return NULL;
}

/* blur.lua:64-69. */
TfpBlurAnswer tfp_blur_adapter_validate(TfpBlurAdapter *a, const TfpBlurCamera *config, const char **why, TfpBlurCode *code) {
  *why = NULL;
  *code = (TfpBlurCode){false, 0};
  invalidate_stats(a);
  if (a->owned) {
    *why = "Blur generation still requires cleanup";
    return TFP_BLUR_FALSE;
  }
  TfpBlurConfig c;
  if ((*why = build_config(a, config, &c)) != NULL) return TFP_BLUR_NIL;
  if (a->api->validate(&c) != 1) {
    *why = native_error(a, "Blur read-only validation", code);
    return TFP_BLUR_FALSE;
  }
  return TFP_BLUR_TRUE;
}

/* blur.lua:70-107. */
bool tfp_blur_adapter_stats(TfpBlurAdapter *a, TfpBlurHandle *handle, TfpBlurStatsView *out, const char **why) {
  *why = NULL;
  out->has = false;
  if (handle && (handle->owner != a || (a->owned && a->owned != handle))) {
    *why = "Blur statistics handle is not owned";
    return false;
  }
  if (handle && handle->disarmed) {
    *out = handle->stats;
    return out->has;
  }
  TfpBlurStats *s = &a->buffer;
  s->size = 80;
  s->abi = a->profile.abi;
  uint64_t generation = handle ? handle->generation : 0;
  uint32_t read = a->api->read_stats(generation, s);
  if (read != 1) {
    invalidate_stats(a);
    *why = native_error(a, "Blur statistics", NULL);
    return false;
  }
  if (s->size != 80 || s->abi != a->profile.abi) {
    invalidate_stats(a);
    *why = "Blur statistics ABI changed";
    return false;
  }
  /* Ownership and health are read every call; the display counters are shared
   * between the staggered 10 Hz diagnostic ticks. */
  uint32_t code = s->error;
  bool has_now = false;
  double now = 0;
  if (a->io->clock && handle && a->owned == handle) {
    double value = 0;
    if (a->io->clock(a->io->user, &value) && value == value && fabs(value) != INFINITY) has_now = true, now = value;
  }
  if (!has_now || code != 0 || (a->has_at && now < a->stats_at)) invalidate_stats(a);
  bool shared = a->has_cache && a->stats_handle == handle; /* `out` is the cached table */
  TfpBlurStatsView view;
  memset(&view, 0, sizeof view);
  if (shared) view = a->cache;
  if (!has_now || code != 0 || !a->has_cache || a->stats_handle != handle || now >= a->stats_due) {
    view.seen = (double)s->seen, view.forwarded = (double)s->forwarded, view.replaced = (double)s->replaced;
    view.immediate_applies = (double)s->immediate_applies, view.last_hash = (double)s->last_hash;
    view.last_instance = (double)s->last_instance, view.last_native = (double)s->last_native;
    view.last_applied = (double)s->last_applied;
    if (has_now && code == 0) {
      a->has_cache = true, a->stats_handle = handle, shared = true;
      double due = now - .0667;
      due = due / .1;
      due = floor(due) + 1;
      due = due * .1;
      due = due + .0667;
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

/* blur.lua:108-120. */
bool tfp_blur_adapter_begin(TfpBlurAdapter *a, const TfpBlurCamera *config, TfpBlurHandle *handle, const char **why, TfpBlurCode *code) {
  *why = NULL;
  *code = (TfpBlurCode){false, 0};
  invalidate_stats(a);
  if (a->owned) {
    *why = "Blur generation still requires cleanup";
    return false;
  }
  TfpBlurConfig c;
  if ((*why = build_config(a, config, &c)) != NULL) return false;
  memset(handle, 0, sizeof *handle);
  handle->owner = a;
  uint32_t result = a->api->begin(&c, &handle->generation);
  if (handle->generation != 0) a->owned = handle;
  if ((result == 1 || result == 2) && !a->owned) {
    *why = "Blur arm returned no owned generation";
    return false;
  }
  if (result != 1) {
    *why = native_error(a, result == 2 ? "Blur partial installation cleanup" : "Blur arm", code);
    return false;
  }
  return true;
}

/* blur.lua:128-138. */
bool tfp_blur_adapter_finish(TfpBlurAdapter *a, TfpBlurHandle *handle, TfpBlurStatsView *stats, const char **why) {
  *why = NULL;
  stats->has = false;
  if (!handle || handle->owner != a) {
    *why = "Blur cleanup handle is not owned";
    return false;
  }
  if (handle->disarmed) {
    *stats = handle->stats;
    return true;
  }
  if (a->owned != handle) {
    *why = "Blur generation ownership changed";
    return false;
  }
  invalidate_stats(a);
  TfpBlurStatsView before, after;
  const char *ignored;
  tfp_blur_adapter_stats(a, handle, &before, &ignored);
  invalidate_stats(a);
  if (a->api->end(handle->generation) != 1) {
    *why = native_error(a, "Blur cleanup", NULL);
    return false;
  }
  handle->disarmed = true;
  a->owned = NULL;
  invalidate_stats(a);
  tfp_blur_adapter_stats(a, NULL, &after, &ignored);
  handle->stats = after.has ? after : before;
  *stats = handle->stats;
  return true;
}

/* ---- the lease: Blur.new, blur.lua:144-287 ---- */
static bool refuse(TfpBlurLease *l, const char *text) {
  if (!text) l->why = NULL;
  else {
    if (text != l->reply) snprintf(l->reply, sizeof l->reply, "%s", text);
    l->why = l->reply;
  }
  return false;
}
static bool accept(TfpBlurLease *l) {
  l->why = NULL;
  return true;
}
/* blur.lua:152-160. False: 'Blur source recheck clock unavailable'. */
static bool clock_now(TfpBlurLease *l, double *out) {
  double value = 0;
  if (!l->io->clock(l->io->user, &value) || value != value || fabs(value) == INFINITY) return false;
  *out = value;
  return true;
}
/* blur.lua:161-166. */
static bool fault(TfpBlurLease *l, const char *why) {
  TfpBlurCode code = l->failure_native_error.has ? l->failure_native_error : l->helper.failure_native_error;
  l->failure_native_error = (TfpBlurCode){code.has && code.value != 0, code.has && code.value != 0 ? code.value : 0};
  l->failure_kind = !l->failure_native_error.has ? TFP_BLUR_FAILURE_NONE
                    : l->failure_native_error.value == 3 ? TFP_BLUR_FAILURE_TEMPORARY_READ : TFP_BLUR_FAILURE_UNSAFE;
  if (why != l->fault) {
    char copy[TFP_BLUR_TEXT];
    snprintf(copy, sizeof copy, "%s", why);
    memcpy(l->fault, copy, sizeof copy);
  }
  l->has_fault = true;
  if (l->io->log) {
    char line[TFP_BLUR_TEXT + 16];
    snprintf(line, sizeof line, "Native blur: %s", l->fault);
    l->io->log(l->io->user, line);
  }
  return refuse(l, l->fault);
}
/* blur.lua:167-170. */
static void clear_binding(TfpBlurLease *l) {
  l->bound = 0, l->has_bound_request = false, l->bound_request = 0, l->has_bound_weapon = false, l->bound_weapon = 0;
  l->bound_player = 0;
  l->has_source_times = false, l->source_retry_at = l->source_checked_at = 0;
}
/* blur.lua:174-178. */
static bool collect(TfpBlurLease *l) {
  TfpBlurStatsView result;
  const char *why;
  tfp_blur_adapter_stats(&l->helper, l->handle, &result, &why);
  if (!result.has) return refuse(l, why ? why : "Blur statistics unavailable");
  l->last = result;
  return accept(l);
}
/* blur.lua:192-199. */
static bool stop(TfpBlurLease *l) {
  if (!l->handle) {
    clear_binding(l);
    l->retiring = false;
    l->phase = TFP_BLUR_LEASE_IDLE;
    return accept(l);
  }
  l->phase = TFP_BLUR_LEASE_CLEANUP_PENDING;
  TfpBlurStatsView result;
  const char *text;
  if (!tfp_blur_adapter_finish(&l->helper, l->handle, &result, &text)) return fault(l, text ? text : "Blur cleanup remains pending");
  if (result.has) l->last = result;
  l->handle = NULL;
  clear_binding(l);
  l->retiring = false;
  l->phase = TFP_BLUR_LEASE_IDLE;
  return accept(l);
}
/* blur.lua:201-209: always true; with the cleanup text while End is still owed. */
static bool retire(TfpBlurLease *l) {
  l->retiring = true;
  if (stop(l)) {
    l->phase = TFP_BLUR_LEASE_WAITING, l->has_fault = false;
    return accept(l);
  }
  /* Error 7/8 acknowledged that this policy is disarmed; its retained callback
   * only forwards while End retries. Keep manual first-person intent. */
  return true; /* l->why: stop()'s text */
}
/* `self:stop(); return fault(text)` with a text that stop() may overwrite. */
static bool stop_then_fault(TfpBlurLease *l, const char *text) {
  char keep[TFP_BLUR_TEXT];
  snprintf(keep, sizeof keep, "%s", text);
  stop(l);
  return fault(l, keep);
}
static bool same_optional(bool has_a, uint64_t a, bool has_b, uint64_t b) { return has_a == has_b && (!has_a || a == b); }
static bool retired_code(uint32_t code) { return code == 7 || code == 8; } /* blur.lua:8 */

/* blur.lua:218-285. */
static bool update(TfpBlurLease *l) {
  l->failure_native_error = (TfpBlurCode){false, 0}, l->failure_kind = TFP_BLUR_FAILURE_NONE;
  l->helper.failure_native_error = (TfpBlurCode){false, 0};
  if (l->phase == TFP_BLUR_LEASE_CLEANUP_PENDING) {
    if (l->retiring) return retire(l);
    return stop(l);
  }
  TfpBlurControllerState state;
  l->io->controller_result(l->io->user, &state);
  if (l->closing || !l->io->controller_enabled(l->io->user) || !state.pose_offset) return stop(l);
  const TfpBlurPoseLease *lease = l->io->controller_lease(l->io->user);
  if (!lease || !state.running) {
    bool done = stop(l);
    if (done) l->phase = TFP_BLUR_LEASE_WAITING;
    return done;
  }
  bool has_request = lease->has_request;
  uint64_t request = lease->request;
  bool has_weapon = lease->has_origin && lease->origin_has_weapon ? true : lease->has_weapon;
  uint64_t weapon = lease->has_origin && lease->origin_has_weapon ? lease->origin_weapon : lease->weapon;
  uint64_t player = lease->has_origin ? lease->origin_player_identity : 0;
  bool same = l->bound == lease->identity && same_optional(l->has_bound_request, l->bound_request, has_request, request) &&
              same_optional(l->has_bound_weapon, l->bound_weapon, has_weapon, weapon) && l->bound_player == player;
  double timestamp = 0;
  if (same && l->phase == TFP_BLUR_LEASE_UNAVAILABLE) {
    /* An optic component can appear later on the same weapon and request. */
    if (!clock_now(l, &timestamp)) return fault(l, "Blur source recheck clock unavailable");
    if (timestamp >= l->source_checked_at && timestamp < l->source_retry_at) return accept(l);
  }
  if (same && l->handle) {
    if (!collect(l)) {
      char keep[TFP_BLUR_TEXT];
      snprintf(keep, sizeof keep, "%s", l->why);
      return stop_then_fault(l, keep);
    }
    if (!(l->last.has && l->last.armed) || (l->last.has && l->last.error != 0)) {
      if (l->last.has && !l->last.armed && retired_code(l->last.error)) return retire(l);
      l->failure_native_error = (TfpBlurCode){l->last.has, l->last.error};
      return stop_then_fault(l, "Native blur policy disarmed or rejected ownership");
    }
    return accept(l);
  }
  /* End before resolving a replacement. */
  if (!stop(l)) return false;
  TfpBlurCamera config;
  const char *why = NULL;
  memset(&config, 0, sizeof config);
  if (!l->io->blur_config(l->io->user, lease, &config, &why)) return fault(l, why ? why : "Owned blur camera unavailable");
  const char *text = NULL;
  TfpBlurCode code;
  bool done = tfp_blur_adapter_begin(&l->helper, &config, &l->storage, &text, &code);
  if (done) l->handle = &l->storage;
  else if (l->helper.owned) l->handle = l->helper.owned;
  if (!done || !l->handle) {
    if (!l->handle && code.has && code.value == TFP_BLUR_NO_SOURCE) {
      if (!clock_now(l, &timestamp)) return fault(l, "Blur source recheck clock unavailable");
      l->bound = lease->identity, l->has_bound_request = has_request, l->bound_request = has_request ? request : 0;
      l->has_bound_weapon = has_weapon, l->bound_weapon = has_weapon ? weapon : 0, l->bound_player = player;
      l->phase = TFP_BLUR_LEASE_UNAVAILABLE, l->has_fault = false;
      l->has_source_times = true, l->source_checked_at = timestamp, l->source_retry_at = timestamp + TFP_BLUR_SOURCE_RECHECK_SECONDS;
      return accept(l);
    }
    if (code.has && retired_code(code.value)) return retire(l);
    /* Capture the fresh Begin cause before cleanup can publish its own error. */
    l->failure_native_error = code.has ? code : l->helper.failure_native_error;
    return stop_then_fault(l, text ? text : "Native blur policy was not accepted");
  }
  l->bound = lease->identity, l->has_bound_request = has_request, l->bound_request = has_request ? request : 0;
  l->has_bound_weapon = has_weapon, l->bound_weapon = has_weapon ? weapon : 0, l->bound_player = player;
  l->phase = TFP_BLUR_LEASE_ACTIVE, l->has_fault = false;
  if (!collect(l)) {
    char keep[TFP_BLUR_TEXT];
    snprintf(keep, sizeof keep, "%s", l->why);
    return stop_then_fault(l, keep);
  }
  if (!(l->last.has && l->last.armed) || (l->last.has && l->last.error != 0)) {
    if (l->last.has && !l->last.armed && retired_code(l->last.error)) return retire(l);
    l->failure_native_error = (TfpBlurCode){l->last.has, l->last.error};
    return stop_then_fault(l, "Native blur policy did not remain armed");
  }
  return accept(l);
}

void tfp_blur_lease_init(TfpBlurLease *l, const TfpBlurHelperApi *api, const TfpBlurIo *io, TfpBlurProfile profile) {
  memset(l, 0, sizeof *l); /* blur.lua:151: phase='idle', last={}, closing=false */
  l->io = io;
  tfp_blur_adapter_init(&l->helper, api, io, profile);
}
bool tfp_blur_lease_update(TfpBlurLease *l, const char **why) {
  bool done = update(l);
  if (why) *why = l->why;
  return done;
}
bool tfp_blur_lease_stop(TfpBlurLease *l, const char **why) {
  bool done = stop(l);
  if (why) *why = l->why;
  return done;
}
bool tfp_blur_lease_shutdown(TfpBlurLease *l, const char **why) { /* blur.lua:200 */
  l->closing = true;
  return tfp_blur_lease_stop(l, why);
}
/* blur.lua:210-217. */
TfpBlurAnswer tfp_blur_lease_preflight(TfpBlurLease *l, const char **why) {
  TfpBlurAnswer answer;
  TfpBlurCamera config;
  const char *text = NULL;
  memset(&config, 0, sizeof config);
  if (l->handle) answer = TFP_BLUR_FALSE, refuse(l, "Blur cleanup is still active");
  else if (!l->io->blur_config(l->io->user, NULL, &config, &text))
    answer = TFP_BLUR_FALSE, refuse(l, text ? text : "Blur configuration unavailable");
  else {
    TfpBlurCode code;
    const char *reason;
    answer = tfp_blur_adapter_validate(&l->helper, &config, &reason, &code);
    if (answer != TFP_BLUR_TRUE && code.has && code.value == TFP_BLUR_NO_SOURCE)
      answer = TFP_BLUR_TRUE, refuse(l, "Weapon has no scoped blur source");
    else refuse(l, reason);
  }
  if (why) *why = l->why;
  return answer;
}
/* blur.lua:180-187. */
void tfp_blur_lease_result(const TfpBlurLease *l, TfpBlurResult *out) {
  out->stats = l->last;
  out->phase = l->phase;
  out->cleanup_pending = l->phase == TFP_BLUR_LEASE_CLEANUP_PENDING;
  out->error_message = l->has_fault ? l->fault : NULL;
  out->failure_native_error = l->failure_native_error;
  out->failure_kind = l->failure_kind;
  out->retiring = l->retiring;
  out->retirement_pending = l->retiring && out->cleanup_pending;
}

/* tostring() of a number as LuaJIT prints it. */
static const char *number_text(double value, char *out, size_t size) {
  if (value != value) snprintf(out, size, "nan");
  else if (value == INFINITY) snprintf(out, size, "inf");
  else if (value == -INFINITY) snprintf(out, size, "-inf");
  else snprintf(out, size, "%.14g", value);
  return out;
}
/* blur.lua:188-191. */
int tfp_blur_lease_status(const TfpBlurLease *l, char *out, size_t size) {
  static const char *const phases[] = {"idle", "waiting", "unavailable", "active", "cleanup_pending"};
  char replaced[32];
  return snprintf(out, size, "%s; scoped blur overrides=%s%s%s", phases[l->phase],
                  l->last.has ? number_text(l->last.replaced, replaced, sizeof replaced) : "0", l->has_fault ? "; " : "",
                  l->has_fault ? l->fault : "");
}
