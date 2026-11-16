/* See camera_adapter.h. Line references (:N) are to retail
 * src/first_person/camera.lua at 0.5.41-smooth.66. */
#include "camera_adapter.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const PHASES[] = {"idle", "waiting", "active", "cleanup_pending"};
static const char *const FIELDS[14] = {"game", "exe", "context", "camera", "avatar_owner", "avatar_index", "avatar",
                                       "body", "camera_unit", "forward", "up", "smoothing_ms", "blend_ms", "max_gap_ms"}; /* :94 */

static void text_clear(TfpCameraText *t) { t->present = false; t->text[0] = 0; }
static void text_set(TfpCameraText *t, const char *format, ...) {
  char line[TFP_CAMERA_ADAPTER_TEXT];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(line, sizeof line, format, arguments);
  va_end(arguments);
  t->present = true;
  memcpy(t->text, line, sizeof line);
}
static bool refuse(TfpCameraText *why, const char *text) { text_set(why, "%s", text); return false; }

size_t tfp_camera_adapter_number(double value, char *out, size_t size) {
  int length;
  if (value != value) length = snprintf(out, size, "nan");
  else if (value == HUGE_VAL || value == -HUGE_VAL) length = snprintf(out, size, value > 0 ? "inf" : "-inf");
  else length = snprintf(out, size, "%.14g", value);
  return length < 0 ? 0 : (size_t)length;
}

/* ---- Adapter ---- */
static void invalidate_stats(TfpCameraAdapter *a) { a->has_stats_cache = a->has_stats_at = false; } /* :13 */

/* :16. false: the load raised; `why` is the error's text. */
static bool load(TfpCameraAdapter *a, TfpCameraText *why) {
  if (a->loaded) return true;
  if (a->api->load) {
    text_clear(why);
    if (!a->api->load(a->api->user, why)) { why->present = true; return false; }
  }
  a->loaded = true;
  return true;
}
/* :86. The helper is loaded wherever this is reached. */
static void helper_error(TfpCameraAdapter *a, const char *operation, TfpCameraText *out) {
  uint32_t code = a->api->helper->error();
  a->has_last_failure_code = true;
  a->last_failure_code = code;
  text_set(out, "%s failed (native head-camera code %u)", operation, (unsigned)code);
}
/* :91. false: an assert raised; `why` is its text. */
static bool helper_config(const TfpCameraAdapter *a, const TfpCameraBackendConfig *config, TfpCameraConfig *c, TfpCameraText *why) {
  memset(c, 0, sizeof *c);
  c->size = 88; c->abi = a->profile.abi; c->reserved = 0;
  for (unsigned i = 0; i < 14; i++)
    if (!(config->present & (1u << i))) { text_set(why, "%s unavailable", FIELDS[i]); return false; }
  c->game = config->game; c->exe = config->exe; c->context = config->context; c->camera = config->camera;
  c->avatar_owner = config->avatar_owner; c->avatar_index = config->avatar_index; c->avatar = config->avatar;
  c->body = config->body; c->camera_unit = config->camera_unit;
  c->forward = (float)config->forward; c->up = (float)config->up;
  c->smoothing_ms = (float)config->smoothing_ms; c->blend_ms = (float)config->blend_ms;
  c->max_gap_ms = config->max_gap_ms;
  return true;
}
/* :98. `handle`: the owned handle is passed (generation, clock); false: nil (generation 0).
 * false: nil and `why`. */
static bool adapter_stats(TfpCameraAdapter *a, bool handle, TfpCameraAdapterStats *out, TfpCameraText *why) {
  TfpCameraStats *s = &a->stats_buffer;
  s->size = 80; s->abi = a->profile.abi;
  uint64_t generation = handle ? a->generation : 0;
  if (a->api->helper->read_stats(generation, s) != 1) {
    invalidate_stats(a);
    helper_error(a, "Head-camera statistics", why);
    return false;
  }
  if (s->size != 80 || s->abi != a->profile.abi) {
    invalidate_stats(a);
    return refuse(why, "Head-camera statistics ABI changed");
  }
  uint32_t code = s->error;
  bool has_now = false;
  double now = 0;
  if (a->api->clock && handle) {
    double value = a->api->clock(a->api->user);
    if (value == value && fabs(value) != HUGE_VAL) { now = value; has_now = true; }
  }
  if (!has_now || code != 0 || (a->has_stats_at && now < a->stats_at)) invalidate_stats(a);
  double counters[8];
  if (!has_now || code != 0 || !a->has_stats_cache || now >= a->stats_due) { /* :124 */
    counters[0] = (double)s->seen; counters[1] = (double)s->forwarded; counters[2] = (double)s->replaced;
    counters[3] = (double)s->native6_forwarded; counters[4] = (double)s->resets; counters[5] = (double)s->blend;
    counters[6] = (double)s->head_distance; counters[7] = (double)s->last_dt;
    if (has_now && code == 0) {
      memcpy(a->stats_cache, counters, sizeof counters);
      a->has_stats_cache = true;
      double due = (floor((now - .0333) / .1) + 1) * .1;
      due = due + .0333;
      a->stats_due = due > now ? due : due + .1;
    }
  } else memcpy(counters, a->stats_cache, sizeof counters);
  if (has_now && code == 0) { a->has_stats_at = true; a->stats_at = now; }
  out->seen = counters[0]; out->forwarded = counters[1]; out->replaced = counters[2]; out->native6_forwarded = counters[3];
  out->resets = counters[4]; out->blend = counters[5]; out->head_distance = counters[6]; out->last_dt = counters[7];
  out->installed = s->installed != 0; out->armed = s->armed != 0; out->error = code;
  return true;
}
/* :136. 1 true; 0 false; -1 raised (nil). */
static int adapter_validate(TfpCameraAdapter *a, const TfpCameraBackendConfig *config, TfpCameraText *why) {
  TfpCameraConfig c;
  invalidate_stats(a);
  if (!load(a, why)) return -1;
  if (!helper_config(a, config, &c, why)) return -1;
  if (a->api->helper->validate(&c) != 1) { helper_error(a, "Head-camera read-only validation", why); return 0; }
  text_clear(why);
  return 1;
}
/* :142. false: refused or raised; `why` is the text. true: `owned` is the new handle. */
static bool adapter_begin(TfpCameraAdapter *a, const TfpCameraBackendConfig *config, TfpCameraText *why) {
  TfpCameraConfig c;
  invalidate_stats(a);
  if (!load(a, why)) return false;
  if (!helper_config(a, config, &c, why)) return false;
  uint64_t generation = 0;
  uint32_t result = a->api->helper->begin(&c, &generation);
  if (generation != 0) { a->owned = true; a->generation = generation; }
  if ((result == 1 || result == 2) && !a->owned) return refuse(why, "Head-camera arm returned no owned generation");
  if (result != 1) {
    helper_error(a, result == 2 ? "Head-camera partial installation cleanup" : "Head-camera arm", why);
    return false;
  }
  return true;
}
/* :176 */
static bool adapter_set_presentation(TfpCameraAdapter *a) { return a->api->helper->set_presentation(a->generation) == 1; }
/* :181 */
static bool adapter_set_handoff(TfpCameraAdapter *a, double entry_ms, double rotation_ms, TfpCameraText *why) {
  const double values[2] = {entry_ms, rotation_ms};
  for (unsigned i = 0; i < 2; i++) {
    double n = values[i];
    if (n != n || n < 0 || n > 500) return refuse(why, "Camera handoff must be from 0 to 500 ms");
  }
  if (a->api->helper->set_handoff(a->generation, (float)entry_ms, (float)rotation_ms) != 1) {
    helper_error(a, "Camera handoff ease", why);
    return false;
  }
  text_clear(why);
  return true;
}
/* :193 */
static bool adapter_set_cut_hold(TfpCameraAdapter *a, uint32_t mode, TfpCameraText *why) {
  if (a->api->helper->set_cut_hold(a->generation, mode) != 1) { helper_error(a, "Camera hold", why); return false; }
  text_clear(why);
  return true;
}
/* :200 */
static bool adapter_set_weapon_key(TfpCameraAdapter *a, double key) {
  if (key < 0 || key > 4294967295.0 || key != floor(key)) return false;
  return a->api->helper->set_weapon_key(a->generation, (uint32_t)key) == 1;
}
/* :246. true: `*has_stats` says whether final statistics came with it. */
static bool adapter_finish(TfpCameraAdapter *a, TfpCameraAdapterStats *stats, bool *has_stats, TfpCameraText *why) {
  TfpCameraAdapterStats before, after;
  TfpCameraText ignored;
  invalidate_stats(a);
  bool has_before = adapter_stats(a, true, &before, &ignored);
  invalidate_stats(a);
  if (a->api->helper->end(a->generation) != 1) { helper_error(a, "Head-camera cleanup", why); return false; }
  a->owned = false;
  invalidate_stats(a);
  bool has_after = adapter_stats(a, false, &after, &ignored);
  *has_stats = has_after || has_before;
  if (has_after) *stats = after;
  else if (has_before) *stats = before;
  return true;
}

/* ---- Camera.new ---- */
void tfp_camera_adapter_init(TfpCameraAdapter *a, const TfpCameraAdapterApi *api, const TfpCameraAdapterProfile *profile) {
  static const TfpCameraAdapterProfile shipped = TFP_CAMERA_ADAPTER_PROFILE_SHIPPED;
  memset(a, 0, sizeof *a);
  a->api = api;
  a->profile = profile ? *profile : shipped;
  a->phase = TFP_CAMERA_ADAPTER_IDLE; /* :266 */
}

static bool same_identity(const TfpCameraAdapter *a, const TfpCameraBackendConfig *b) { /* :268 */
  const TfpCameraBackendConfig *o = &a->bound;
  if (!a->has_bound) return false;
  const uint64_t left[9] = {o->game, o->exe, o->context, o->camera, o->avatar_owner, o->avatar_index, o->avatar, o->body,
                            o->camera_unit};
  const uint64_t right[9] = {b->game, b->exe, b->context, b->camera, b->avatar_owner, b->avatar_index, b->avatar, b->body,
                             b->camera_unit};
  for (unsigned i = 0; i < 9; i++) {
    bool here = (o->present & (1u << i)) != 0, there = (b->present & (1u << i)) != 0;
    if (here != there || (here && left[i] != right[i])) return false;
  }
  return true;
}
/* :273. `message` may point into `why`. */
static bool problem(TfpCameraAdapter *a, const char *message, bool has_cause, uint32_t cause, TfpCameraText *why) {
  char line[TFP_CAMERA_ADAPTER_TEXT + 24];
  TfpCameraText fault;
  text_set(&fault, "%s", message);
  a->fault = fault;
  a->has_failure_native_error = has_cause || a->has_last_failure_code;
  a->failure_native_error = has_cause ? cause : a->has_last_failure_code ? a->last_failure_code : 0;
  a->failure_kind = a->has_failure_native_error && a->failure_native_error == 3 ? TFP_CAMERA_FAILURE_TEMPORARY_READ
                    : a->has_failure_native_error ? TFP_CAMERA_FAILURE_UNSAFE : TFP_CAMERA_FAILURE_NONE;
  snprintf(line, sizeof line, "Native head camera: %s", a->fault.text);
  if (a->api->log) a->api->log(a->api->user, line);
  *why = a->fault;
  return false;
}
static bool collect(TfpCameraAdapter *a, TfpCameraText *why) { /* :279 */
  TfpCameraAdapterStats stats;
  TfpCameraText issue;
  text_clear(&issue);
  if (!adapter_stats(a, true, &stats, &issue)) return problem(a, issue.text, false, 0, why);
  a->last = stats;
  a->has_last = true;
  if (stats.error != 0) { a->has_last_failure_code = true; a->last_failure_code = stats.error; }
  return true;
}
static bool stop(TfpCameraAdapter *a, TfpCameraText *why) { /* :359 */
  text_clear(why);
  if (!a->handle) { a->has_bound = false; a->phase = TFP_CAMERA_ADAPTER_IDLE; return true; }
  a->phase = TFP_CAMERA_ADAPTER_CLEANUP_PENDING;
  TfpCameraAdapterStats stats;
  TfpCameraText issue;
  bool has_stats = false;
  text_clear(&issue);
  if (!adapter_finish(a, &stats, &has_stats, &issue)) return problem(a, issue.text, false, 0, why);
  if (has_stats) { a->last = stats; a->has_last = true; }
  a->handle = false;
  a->has_bound = false;
  a->phase = TFP_CAMERA_ADAPTER_IDLE;
  return true;
}
/* invoke(backend,'camera_config') and the test both callers apply to it (:369-371, :378-380, :434-435). */
static bool camera_config(TfpCameraAdapter *a, TfpCameraBackendConfig *config, TfpCameraText *reason) {
  memset(config, 0, sizeof *config);
  text_clear(reason);
  return a->api->camera_config(a->api->user, config, reason);
}
static bool gameplay_kind(const TfpCameraBackendConfig *config) { return config->camera_kind == 4 || config->camera_kind == 6; }

static bool prepare(TfpCameraAdapter *a, TfpCameraText *why) { /* :374 */
  TfpCameraBackendConfig config;
  TfpCameraText reason, ignored;
  a->has_last_failure_code = false;
  a->has_failure_native_error = false;
  a->failure_kind = TFP_CAMERA_FAILURE_NONE;
  text_clear(why);
  if (a->closing) return refuse(why, "Head-camera manager is shutting down");
  if (a->phase == TFP_CAMERA_ADAPTER_CLEANUP_PENDING) return refuse(why, "Head-camera cleanup remains pending");
  if (!camera_config(a, &config, &reason)) return refuse(why, reason.present ? reason.text : "Head-camera configuration unavailable");
  if (!gameplay_kind(&config)) return refuse(why, "Camera preparation requires an owned on-foot gameplay view");
  if (a->handle) {
    if (!collect(a, &reason)) { stop(a, &ignored); *why = reason; return false; }
    if (a->last.armed && same_identity(a, &config)) return true;
    if (!stop(a, why)) return false;
  }
  bool done = adapter_begin(a, &config, &reason);
  a->handle = a->owned; /* :387: the returned handle, or whatever the adapter still owns */
  if (!done || !a->handle) {
    stop(a, &ignored);
    return problem(a, reason.text, false, 0, why);
  }
  if (config.presentation_enabled) { /* :395 */
    if (!adapter_set_presentation(a)) {
      stop(a, &ignored);
      return refuse(why, "Camera handoff preparation refused");
    }
    /* Optional eases: a refusal leaves the exact cut and first person running. */
    if (a->has_handoff || a->profile.has_handoff) {
      double entry_ms = a->has_handoff ? a->handoff_entry_ms : a->profile.handoff_entry_ms;
      double rotation_ms = a->has_handoff ? a->handoff_rotation_ms : a->profile.handoff_rotation_ms;
      TfpCameraText issue;
      if (adapter_set_handoff(a, entry_ms, rotation_ms, &issue)) text_clear(&a->handoff_issue);
      else a->handoff_issue = issue;
    }
    uint32_t hold = a->has_cut_hold ? a->cut_hold
                    : (a->profile.cut_hold ? 1u : 0u) + (a->profile.eye_hold ? 2u : 0u) + (a->profile.ads_anchor ? 4u : 0u) +
                      (a->profile.seated_head ? 8u : 0u);
    if (hold > 0) {
      TfpCameraText issue;
      if (adapter_set_cut_hold(a, hold, &issue)) text_clear(&a->cut_hold_issue);
      else a->cut_hold_issue = issue;
    }
  }
  a->has_weapon_key = false; /* :418 */
  a->bound = config;
  a->has_bound = true;
  a->phase = TFP_CAMERA_ADAPTER_ACTIVE;
  text_clear(&a->fault);
  if (!collect(a, &reason)) { stop(a, &ignored); *why = reason; return false; }
  if (!a->last.armed) {
    stop(a, &ignored);
    return problem(a, "Head-camera policy did not remain armed", false, 0, why);
  }
  return true;
}

static bool update(TfpCameraAdapter *a, TfpCameraText *why) { /* :426 */
  TfpCameraControllerResult result = {false};
  TfpCameraText reason, ignored;
  a->has_last_failure_code = false;
  a->has_failure_native_error = false;
  a->failure_kind = TFP_CAMERA_FAILURE_NONE;
  text_clear(why);
  if (a->phase == TFP_CAMERA_ADAPTER_CLEANUP_PENDING) return stop(a, why);
  a->api->controller_result(a->api->user, &result);
  if (a->closing || !a->api->controller_is_enabled(a->api->user) || !result.pose_offset) return stop(a, why);
  if (!a->handle) {
    /* Fresh local on-foot ownership permits rearming after acknowledged cleanup. */
    TfpCameraBackendConfig config;
    if (!camera_config(a, &config, &reason) || !gameplay_kind(&config)) {
      a->phase = TFP_CAMERA_ADAPTER_WAITING;
      *why = reason;
      return true;
    }
    return prepare(a, why);
  }
  if (!collect(a, &reason)) { stop(a, &ignored); *why = reason; return false; }
  if (!a->last.armed) {
    /* The typed cause is taken before End/ReadStats can replace it. */
    uint32_t cause = a->last.error;
    bool typed_fault = cause != 0 && cause != 7;
    bool stopped = stop(a, &reason);
    if (typed_fault) return problem(a, reason.present ? reason.text : "Head-camera native policy disarmed", true, cause, why);
    if (!stopped) { *why = reason; return false; }
    a->phase = TFP_CAMERA_ADAPTER_WAITING;
    text_clear(&a->fault);
    text_set(why, "Head-camera player changed; waiting for a fresh normal camera");
    return true;
  }
  /* Optional: report the weapon in hand so the helper can remember its eye. Never a reason to stop. */
  if (a->api->snapshot) { /* :452 */
    TfpCameraBackendSnapshot snapshot = {false, 0};
    double key = a->api->snapshot(a->api->user, &snapshot) && snapshot.has_weapon_unit ? snapshot.weapon_unit : 0;
    if ((!a->has_weapon_key || key != a->weapon_key) && adapter_set_weapon_key(a, key)) {
      a->has_weapon_key = true;
      a->weapon_key = key;
    }
  }
  /* Errors 14/15/16 describe a forwarded transient gate: no teardown. */
  a->phase = TFP_CAMERA_ADAPTER_ACTIVE;
  return true;
}

static TfpCameraText *sink(TfpCameraText *why, TfpCameraText *spare) { return why ? why : spare; }
bool tfp_camera_adapter_update(TfpCameraAdapter *a, TfpCameraText *why) { TfpCameraText spare; return update(a, sink(why, &spare)); }
bool tfp_camera_adapter_prepare(TfpCameraAdapter *a, TfpCameraText *why) { TfpCameraText spare; return prepare(a, sink(why, &spare)); }
bool tfp_camera_adapter_stop(TfpCameraAdapter *a, TfpCameraText *why) { TfpCameraText spare; return stop(a, sink(why, &spare)); }
bool tfp_camera_adapter_shutdown(TfpCameraAdapter *a, TfpCameraText *why) { /* :366 */
  TfpCameraText spare;
  a->closing = true;
  return stop(a, sink(why, &spare));
}
int tfp_camera_adapter_preflight(TfpCameraAdapter *a, TfpCameraText *why) { /* :367 */
  TfpCameraBackendConfig config;
  TfpCameraText spare, reason;
  why = sink(why, &spare);
  text_clear(why);
  if (a->handle) return refuse(why, "Head-camera cleanup is still active");
  if (!camera_config(a, &config, &reason)) return refuse(why, reason.present ? reason.text : "Head-camera configuration unavailable");
  if (!gameplay_kind(&config)) return refuse(why, "Camera preparation requires an owned on-foot gameplay view");
  return adapter_validate(a, &config, why);
}
/* :327. Live switch; the choice is kept for later leases. */
bool tfp_camera_adapter_set_handoff(TfpCameraAdapter *a, double entry_ms, double rotation_ms, TfpCameraText *why) {
  TfpCameraText spare;
  why = sink(why, &spare);
  a->has_handoff = true;
  a->handoff_entry_ms = entry_ms;
  a->handoff_rotation_ms = rotation_ms;
  if (!a->handle) { text_set(why, "Applies when the head camera next arms"); return true; }
  return adapter_set_handoff(a, entry_ms, rotation_ms, why);
}
/* :333 */
bool tfp_camera_adapter_set_cut_hold(TfpCameraAdapter *a, bool exit, bool eye, bool anchor, bool seated, TfpCameraText *why) {
  TfpCameraText spare;
  why = sink(why, &spare);
  a->has_cut_hold = true;
  a->cut_hold = (exit ? 1u : 0u) + (eye ? 2u : 0u) + (anchor ? 4u : 0u) + (seated ? 8u : 0u);
  if (!a->handle) { text_set(why, "Applies when the head camera next arms"); return true; }
  return adapter_set_cut_hold(a, a->cut_hold, why);
}
void tfp_camera_adapter_result(const TfpCameraAdapter *a, TfpCameraAdapterResult *out) { /* :287 */
  memset(out, 0, sizeof *out);
  out->has_stats = a->has_last;
  if (a->has_last) out->stats = a->last;
  out->phase = a->phase;
  out->cleanup_pending = a->phase == TFP_CAMERA_ADAPTER_CLEANUP_PENDING;
  out->error_message = a->fault;
  out->has_failure_native_error = a->has_failure_native_error;
  out->failure_native_error = a->has_failure_native_error ? a->failure_native_error : 0;
  out->failure_kind = a->failure_kind;
}
size_t tfp_camera_adapter_status(const TfpCameraAdapter *a, char *out, size_t size) { /* :293 */
  char replaced[32], native6[32], blend[32];
  tfp_camera_adapter_number(a->has_last ? a->last.replaced : 0, replaced, sizeof replaced);
  tfp_camera_adapter_number(a->has_last ? a->last.native6_forwarded : 0, native6, sizeof native6);
  tfp_camera_adapter_number(a->has_last ? a->last.blend : 0, blend, sizeof blend);
  int length = snprintf(out, size, "%s; normal head calls=%s; native ADS calls=%s; blend=%s; gate=%u", PHASES[a->phase],
                        replaced, native6, blend, a->has_last ? (unsigned)a->last.error : 0u);
  return length < 0 ? 0 : (size_t)length;
}
