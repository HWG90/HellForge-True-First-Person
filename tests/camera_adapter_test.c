/* Replays tests/vectors/camera_adapter_vectors.h (the retail camera.lua's own answers) against
 * src/camera_adapter.c with a scripted helper, backend, controller and clock built as the Lua
 * runner's (tools/retail_ref/run_camera_adapter.lua). Compared after every operation: the result
 * and its text, result(), status(), the unit's state, and the ordered trace of every call made
 * with its arguments. */
#include "camera_adapter.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "vectors/camera_adapter_vectors.h"

static const CameraAdapterVector *world;
static char trace[16384];
static unsigned calls_end, calls_stats, calls_error, calls_config, calls_clock;
/* The generation the scripted helper last handed out and has not ended (see the Lua runner). */
static uint64_t live, serial;

static void note(const char *format, ...) {
  size_t used = strlen(trace);
  va_list arguments;
  if (used && used + 1 < sizeof trace) trace[used++] = ' ';
  va_start(arguments, format);
  vsnprintf(trace + used, sizeof trace - used, format, arguments);
  va_end(arguments);
}
static unsigned nth(unsigned *calls, unsigned size) { return (++*calls < size ? *calls : size) - 1; }
static const char *reason(int index) { return index > 0 ? camera_adapter_reasons[index] : ""; }
static void block(const char *name, const TfpCameraConfig *c) {
  char hex[2 * sizeof *c + 1];
  const unsigned char *bytes = (const unsigned char *)c;
  for (size_t i = 0; i < sizeof *c; i++) snprintf(hex + 2 * i, 3, "%02x", bytes[i]);
  for (size_t i = 0; i < sizeof camera_adapter_blocks / sizeof *camera_adapter_blocks; i++)
    if (!strcmp(hex, camera_adapter_blocks[i])) { note("%s(#%u)", name, (unsigned)i); return; }
  note("%s(%s)", name, hex);
}
static uint32_t float_bits(float value) { uint32_t bits; memcpy(&bits, &value, sizeof bits); return bits; }

/* ---- the scripted helper ---- */
static uint32_t h_validate(const TfpCameraConfig *c) { block("V", c); return world->validate; }
static uint32_t h_begin(const TfpCameraConfig *c, uint64_t *out) {
  block("B", c);
  uint32_t result = world->begin;
  uint64_t generation = world->gen;
  if (world->follow) {
    if (live != 0) result = 0, generation = 0;
    else if (result == 1 || result == 2) generation = ++serial;
    else generation = 0;
  }
  if (generation != 0) { *out = generation; live = generation; }
  return result;
}
static uint32_t h_end(uint64_t generation) {
  note("E(%.0f)", (double)generation);
  uint32_t result = world->ends[nth(&calls_end, 3)];
  if (world->follow && generation != live) result = 0;
  if (result == 1) live = 0;
  return result;
}
static uint32_t h_read_stats(uint64_t generation, TfpCameraStats *s) {
  note("S(%.0f,%u,%u)", (double)generation, (unsigned)s->size, (unsigned)s->abi);
  const CameraAdapterStatsAnswer *answer = camera_adapter_stats + world->st[nth(&calls_stats, 6)];
  uint32_t result = answer->ret;
  bool idle = false;
  if (world->follow) {
    if (generation == 0 && live == 0) result = 1, idle = true;
    else if (generation == 0 || generation != live) return 0;
  }
  if (idle) s->size = 80, s->installed = 0, s->armed = 0, s->error = 0;
  else if (answer->fill) s->size = answer->size, s->abi = answer->abi, s->installed = answer->installed, s->armed = answer->armed, s->error = answer->error;
  if (answer->fill || idle) {
    s->seen = answer->seen; s->forwarded = answer->forwarded; s->replaced = answer->replaced;
    s->native6_forwarded = answer->native6; s->resets = answer->resets;
    s->blend = (float)answer->blend; s->head_distance = (float)answer->head; s->last_dt = (float)answer->dt;
  }
  return result;
}
static uint32_t h_error(void) { note("Err"); return world->errs[nth(&calls_error, 4)]; }
static uint32_t h_set_presentation(uint64_t generation) {
  note("P(%.0f)", (double)generation);
  return world->follow && generation != live ? 0 : world->pres;
}
static uint32_t h_set_handoff(uint64_t generation, float entry_ms, float rotation_ms) {
  note("H(%.0f,%08x,%08x)", (double)generation, (unsigned)float_bits(entry_ms), (unsigned)float_bits(rotation_ms));
  return world->follow && generation != live ? 0 : world->hand;
}
static uint32_t h_set_cut_hold(uint64_t generation, uint32_t mode) {
  note("C(%.0f,%u)", (double)generation, (unsigned)mode);
  return world->follow && generation != live ? 0 : world->cut;
}
static uint32_t h_set_weapon_key(uint64_t generation, uint32_t key) {
  note("K(%.0f,%u)", (double)generation, (unsigned)key);
  return world->follow && generation != live ? 0 : world->key;
}
static const TfpCameraHelperApi helper = {h_validate, h_begin, h_end, h_read_stats, h_error, h_set_presentation, h_set_handoff,
                                          h_set_cut_hold, h_set_weapon_key};

/* ---- the scripted backend, controller, clock, logger ---- */
static bool b_load(void *user, TfpCameraText *why) {
  (void)user;
  note("load");
  if (world->load > 0) { snprintf(why->text, sizeof why->text, "%s", reason(world->load)); why->present = true; return false; }
  return true;
}
static bool b_camera_config(void *user, TfpCameraBackendConfig *out, TfpCameraText *why) {
  (void)user;
  note("config");
  const CameraAdapterConfigAnswer *c = camera_adapter_configs + world->cfg[nth(&calls_config, 2)];
  if (c->ok != 1) {
    if (c->why > 0) { snprintf(why->text, sizeof why->text, "%s", reason(c->why)); why->present = true; }
    return false;
  }
  *out = (TfpCameraBackendConfig){c->present, c->game, c->exe, c->context, c->camera, c->avatar_owner, c->avatar_index, c->avatar,
                                  c->body, c->camera_unit, c->forward, c->up, c->smoothing_ms, c->blend_ms, c->max_gap_ms, c->kind,
                                  c->pres == 1};
  return true;
}
static bool b_snapshot(void *user, TfpCameraBackendSnapshot *out) {
  (void)user;
  note("snapshot");
  if (world->snap == 0 || world->snap == 3) return false;
  out->has_weapon_unit = world->snap == 2;
  out->weapon_unit = world->unit;
  return true;
}
static void b_result(void *user, TfpCameraControllerResult *out) { (void)user; note("result"); out->pose_offset = world->mode == 1; }
static bool b_enabled(void *user) { (void)user; note("enabled"); return world->enabled != 0; }
static double b_clock(void *user) { (void)user; note("clock"); return world->clock[nth(&calls_clock, 4)]; }
static void b_log(void *user, const char *text) { (void)user; note("log(%s)", text); }

static bool same(double a, double b) { return (a != a && b != b) || (a == b && !signbit(a) == !signbit(b)); }
static bool same_text(const TfpCameraText *t, int present, const char *text) {
  return t->present == (present != 0) && !strcmp(t->present ? t->text : "", text);
}

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(CAMERA_ADAPTER_VECTOR_COMMIT, commit) || strlen(CAMERA_ADAPTER_VECTOR_SHA256) != 64) {
    fprintf(stderr, "camera adapter vectors were cut from retail %s, expected %s\n", CAMERA_ADAPTER_VECTOR_COMMIT, commit);
    return 2;
  }
  unsigned count = sizeof camera_adapter_vectors / sizeof *camera_adapter_vectors, runs = 0, arms = 0, armed_updates = 0, helper_calls = 0;
  int run = -1;
  static TfpCameraAdapter camera;
  static TfpCameraAdapterApi api;
  for (unsigned i = 0; i < count; i++) {
    const CameraAdapterVector *v = camera_adapter_vectors + i;
    if (v->run != run) {
      const CameraAdapterRun *r = camera_adapter_runs + v->run;
      run = v->run;
      runs++;
      live = serial = 0;
      api = (TfpCameraAdapterApi){NULL, &helper, b_load, b_camera_config, r->snapshot ? b_snapshot : NULL, b_result, b_enabled,
                                  r->clock ? b_clock : NULL, r->log ? b_log : NULL};
      TfpCameraAdapterProfile profile = {r->profile.abi, r->profile.has_handoff != 0, r->profile.entry_ms, r->profile.rotation_ms,
                                         r->profile.cut_hold != 0, r->profile.eye_hold != 0, r->profile.ads_anchor != 0,
                                         r->profile.seated_head != 0};
      static const TfpCameraAdapterProfile shipped = TFP_CAMERA_ADAPTER_PROFILE_SHIPPED;
      /* A run on the shipped profile takes the unit's own defaults, so those are compared with profile.lua too. */
      tfp_camera_adapter_init(&camera, &api, memcmp(&profile, &shipped, sizeof profile) ? &profile : NULL);
    }
    world = v;
    trace[0] = 0;
    calls_end = calls_stats = calls_error = calls_config = calls_clock = 0;
    TfpCameraText why;
    memset(&why, 0xa5, sizeof why);
    int result = 0;
    switch (v->call) {
    case CAV_UPDATE: result = tfp_camera_adapter_update(&camera, &why); break;
    case CAV_PREPARE: case CAV_PREARM: result = tfp_camera_adapter_prepare(&camera, &why); break;
    case CAV_STOP: result = tfp_camera_adapter_stop(&camera, &why); break;
    case CAV_SHUTDOWN: result = tfp_camera_adapter_shutdown(&camera, &why); break;
    case CAV_PREFLIGHT: result = tfp_camera_adapter_preflight(&camera, &why); break;
    case CAV_SET_HANDOFF: result = tfp_camera_adapter_set_handoff(&camera, v->a, v->b, &why); break;
    case CAV_SET_CUT_HOLD:
      result = tfp_camera_adapter_set_cut_hold(&camera, (v->hold & 1) != 0, (v->hold & 2) != 0, (v->hold & 4) != 0, (v->hold & 8) != 0, &why);
      break;
    }
    const TfpCameraAdapter *c = &camera;
    TfpCameraAdapterResult r;
    char status[256];
    tfp_camera_adapter_result(c, &r);
    tfp_camera_adapter_status(c, status, sizeof status);
    const double counters[8] = {r.stats.seen, r.stats.forwarded, r.stats.replaced, r.stats.native6_forwarded, r.stats.resets,
                                r.stats.blend, r.stats.head_distance, r.stats.last_dt};
    const uint64_t bound[9] = {c->bound.game, c->bound.exe, c->bound.context, c->bound.camera, c->bound.avatar_owner,
                               c->bound.avatar_index, c->bound.avatar, c->bound.body, c->bound.camera_unit};
    const char *wrong = NULL;
#define CHECK(name, condition) do { if (!wrong && !(condition)) wrong = name; } while (0)
    CHECK("result", result == v->result);
    CHECK("text", same_text(&why, v->has_text, v->text));
    CHECK("is_clean", tfp_camera_adapter_is_clean(c) == (v->clean != 0));
    CHECK("phase", (int)r.phase == v->phase && r.cleanup_pending == (v->phase == 3));
    CHECK("closing", c->closing == (v->closing != 0));
    CHECK("has statistics", r.has_stats == (v->has_stats != 0));
    for (unsigned k = 0; k < 8; k++) CHECK("counters", same(counters[k], v->counters[k]));
    CHECK("installed", r.stats.installed == (v->installed != 0));
    CHECK("armed", r.stats.armed == (v->armed != 0));
    CHECK("error", r.stats.error == v->error);
    CHECK("error_message", same_text(&r.error_message, v->has_fault, v->fault));
    CHECK("failure_native_error", r.has_failure_native_error == (v->has_native_error != 0) && r.failure_native_error == v->native_error);
    CHECK("failure_kind", (int)r.failure_kind == v->failure_kind);
    CHECK("status", !strcmp(status, v->status));
    CHECK("handle", c->handle == (v->handle != 0) && c->owned == c->handle && (!c->handle || c->generation == v->generation));
    CHECK("weapon_key", c->has_weapon_key == (v->has_weapon_key != 0) && (!c->has_weapon_key || same(c->weapon_key, v->weapon_key)));
    CHECK("handoff", c->has_handoff == (v->has_handoff != 0) &&
                     (!c->has_handoff || (same(c->handoff_entry_ms, v->entry_ms) && same(c->handoff_rotation_ms, v->rotation_ms))));
    CHECK("cut_hold", c->has_cut_hold == (v->has_cut_hold != 0) && (!c->has_cut_hold || c->cut_hold == v->cut_hold));
    CHECK("handoff_issue", same_text(&c->handoff_issue, v->has_handoff_issue, v->handoff_issue));
    CHECK("cut_hold_issue", same_text(&c->cut_hold_issue, v->has_cut_hold_issue, v->cut_hold_issue));
    CHECK("bound", c->has_bound == (v->has_bound != 0));
    if (c->has_bound) {
      CHECK("bound", (c->bound.present & TFP_CAMERA_FIELDS_IDENTITY) == TFP_CAMERA_FIELDS_IDENTITY);
      for (unsigned k = 0; k < 9; k++) CHECK("bound", bound[k] == v->bound[k]);
    }
    CHECK("loaded", c->loaded == (v->loaded != 0));
    CHECK("statistics cache", c->has_stats_cache == (v->has_cache != 0) && (!c->has_stats_cache || same(c->stats_due, v->stats_due)));
    CHECK("stats_at", c->has_stats_at == (v->has_at != 0) && (!c->has_stats_at || same(c->stats_at, v->stats_at)));
    CHECK("last_failure_code", c->has_last_failure_code == (v->has_last_code != 0) && (!c->has_last_failure_code || c->last_failure_code == v->last_code));
    CHECK("trace", !strcmp(trace, v->trace));
    if (wrong) {
      fprintf(stderr, "camera adapter vector %u (run %s, call %d): %s differs\n"
                      "  C      result=%d text=%d[%s] phase=%d handle=%d gen=%llu fault=%d[%s]\n         status=%s\n         trace=%s\n"
                      "  retail result=%d text=%d[%s] phase=%d handle=%d gen=%llu fault=%d[%s]\n         status=%s\n         trace=%s\n",
              i, camera_adapter_runs[v->run].name, v->call, wrong, result, why.present, why.present ? why.text : "", r.phase, c->handle,
              (unsigned long long)c->generation, r.error_message.present, r.error_message.text, status, trace, v->result, v->has_text,
              v->text, v->phase, v->handle, v->generation, v->has_fault, v->fault, v->status, v->trace);
      fprintf(stderr, "  C      counters");
      for (unsigned k = 0; k < 8; k++) fprintf(stderr, " %.17g", counters[k]);
      fprintf(stderr, " installed=%d armed=%d error=%u\n  retail counters", r.stats.installed, r.stats.armed, (unsigned)r.stats.error);
      for (unsigned k = 0; k < 8; k++) fprintf(stderr, " %.17g", v->counters[k]);
      fprintf(stderr, " installed=%d armed=%d error=%u\n", v->installed, v->armed, v->error);
      return 1;
    }
    arms += strstr(trace, "B(") != NULL && c->handle;
    armed_updates += v->call == CAV_UPDATE && c->phase == TFP_CAMERA_ADAPTER_ACTIVE;
    for (const char *p = trace; *p; p++) helper_calls += *p == '(' && p > trace && strchr("VBESPHCK", p[-1]) != NULL;
  }
  printf("Camera adapter: %u operations in %u runs answered as retail's camera.lua (%s): same results, texts, state, statistics and "
         "ordered helper and backend calls; %u arms, %u armed updates, %u helper calls\n",
         count, runs, CAMERA_ADAPTER_VECTOR_COMMIT, arms, armed_updates, helper_calls);
  return 0;
}
