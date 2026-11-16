#include "pose_adapter.h"
#include <math.h>
#include "vectors/pose_adapter_vectors.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A helper and clock that answer what the vector says and record what they were asked, built
 * as tools/retail_ref/run_pose_adapter.lua builds its own. */
static const PoseAdapterVector *w;
static char trace[8192];
static unsigned count_end, count_stats, count_error;
/* The small model of the real helper; gen is kept as the number Lua holds. */
static struct { double gen; uint64_t next, seen; bool armed, expired; uint32_t error, lasterr; } sim;

static void note(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void note(const char *format, ...) {
  size_t used = strlen(trace);
  va_list args;
  if (used) trace[used++] = ' ';
  va_start(args, format);
  vsnprintf(trace + used, sizeof trace - used, format, args);
  va_end(args);
}
static const char *num(double x, char *out, size_t size) {
  if (x != x) snprintf(out, size, "nan");
  else if (x == INFINITY) snprintf(out, size, "inf");
  else if (x == -INFINITY) snprintf(out, size, "-inf");
  else snprintf(out, size, "%.17g", x);
  return out;
}
#define U(x) ((unsigned long long)(x))
static bool token(uint64_t gen) { return sim.gen != 0 ? (double)gen == sim.gen : gen == 0; }

static void note_config(const char *tag, const TfpPoseConfig *c) {
  char x[40], y[40], z[40];
  note("%s(%u %u %llu %llu %llu %llu %llu %u %u %u %u %u %u %llu %u %u %s %s %s %u)", tag, c->size, c->abi, U(c->game), U(c->exe),
       U(c->context), U(c->request), U(c->avatar_owner), c->avatar_index, c->avatar, c->weapon, c->body, c->viewmodel, c->duration_ms,
       U(c->weapon_descriptor), c->weapon_unit, c->blend_ms, num(c->offset_x, x, sizeof x), num(c->offset_y, y, sizeof y),
       num(c->offset_z, z, sizeof z), c->reserved);
}
static uint32_t validate(const char *tag, const TfpPoseConfig *c) {
  note_config(tag, c);
  long long r = w->validate;
  if (r < 0) {
    if (sim.gen != 0) r = 0, sim.lasterr = 2;
    else r = 1;
  }
  return (uint32_t)r;
}
static uint32_t h_validate(const TfpPoseConfig *c) { return validate("V", c); }
static uint32_t h_validate_input(const TfpPoseConfig *c) { return validate("Vi", c); }
static uint32_t begin(const char *tag, const TfpPoseConfig *c, uint64_t *out) {
  note_config(tag, c);
  long long r = w->begin;
  uint64_t g = w->begin_gen;
  if (r < 0) {
    if (sim.gen != 0) r = 0, g = 0, sim.lasterr = 2;
    else sim.next++, sim.gen = (double)sim.next, sim.armed = true, sim.expired = false, sim.error = 0, r = 1, g = sim.next;
  } else if (g != 0) sim.gen = (double)g, sim.armed = r == 1, sim.expired = false, sim.error = 0;
  *out = g;
  return (uint32_t)r;
}
static uint32_t h_begin(const TfpPoseConfig *c, uint64_t *out) { return begin("B", c, out); }
static uint32_t h_begin_steady(const TfpPoseConfig *c, uint64_t *out) { return begin("Bs", c, out); }
static uint32_t h_begin_input(const TfpPoseConfig *c, uint64_t *out) { return begin("Bi", c, out); }
static uint32_t h_begin_steady_input(const TfpPoseConfig *c, uint64_t *out) { return begin("Bsi", c, out); }
static uint32_t setter(uint64_t gen) {
  long long r = w->setr;
  if (r < 0) {
    if ((double)gen == sim.gen && sim.armed) r = 1;
    else r = 0, sim.lasterr = 17;
  }
  return (uint32_t)r;
}
static uint32_t xyz(const char *tag, uint64_t gen, float x, float y, float z) {
  char a[40], b[40], c[40];
  note("%s(%llu %s %s %s)", tag, U(gen), num(x, a, sizeof a), num(y, b, sizeof b), num(z, c, sizeof c));
  return setter(gen);
}
static uint32_t h_placement(uint64_t gen, float x, float y, float z) { return xyz("P", gen, x, y, z); }
static uint32_t h_ads_placement(uint64_t gen, float x, float y, float z) { return xyz("A", gen, x, y, z); }
static uint32_t transform(const char *tag, uint64_t gen, const float *v) {
  char n[7][40];
  note("%s(%llu %s %s %s %s %s %s %s)", tag, U(gen), num(v[0], n[0], 40), num(v[1], n[1], 40), num(v[2], n[2], 40), num(v[3], n[3], 40),
       num(v[4], n[4], 40), num(v[5], n[5], 40), num(v[6], n[6], 40));
  return setter(gen);
}
static uint32_t h_transform(uint64_t gen, float dx, float dy, float dz, float qx, float qy, float qz, float qw) {
  return transform("M", gen, (float[]){dx, dy, dz, qx, qy, qz, qw});
}
static uint32_t h_authored(uint64_t gen, float dx, float dy, float dz, float qx, float qy, float qz, float qw) {
  return transform("Ma", gen, (float[]){dx, dy, dz, qx, qy, qz, qw});
}
static uint32_t h_zoom(uint64_t gen, uint32_t active) {
  note("Z(%llu %u)", U(gen), active);
  return setter(gen);
}
static uint32_t h_transition(uint64_t gen, uint64_t epoch, float progress, uint32_t ads) {
  char p[40];
  note("T(%llu %llu %s %u)", U(gen), U(epoch), num(progress, p, sizeof p), ads);
  return setter(gen);
}
static uint32_t h_end(uint64_t gen) {
  note("E(%llu)", U(gen));
  long long r = w->endr[count_end < 1 ? count_end : 1];
  count_end++;
  if (r < 0) {
    if ((double)gen == sim.gen && sim.gen != 0) sim.gen = 0, sim.armed = false, sim.expired = false, sim.error = 0, r = 1;
    else r = 0, sim.lasterr = 17;
  } else if (r == 1) sim.gen = 0, sim.armed = false, sim.expired = false, sim.error = 0;
  return (uint32_t)r;
}
static uint32_t h_stats(uint64_t gen, TfpPoseStats *s) {
  note("S(%llu %u %u)", U(gen), s->size, s->abi);
  const PoseAnswer *a = &w->st[count_stats < 2 ? count_stats : 2];
  count_stats++;
  if (a->ret < 0) {
    if (!token(gen)) {
      sim.lasterr = 17;
      return 0;
    }
    sim.seen += 3;
    s->installed = sim.gen != 0, s->armed = sim.armed, s->expired = sim.expired, s->error = sim.error;
    s->seen_position = sim.seen, s->seen_rotation = sim.seen + 1, s->forwarded_position = sim.seen * 2;
    s->forwarded_rotation = sim.seen + 3, s->weapon_updates = sim.seen + 4, s->zero_restores = sim.seen * 3;
    s->blend = sim.armed ? 1 : 0, s->target_weight = 0.5f;
    return 1;
  }
  if (a->fill) {
    uint64_t b = w->sbase;
    s->size = w->ssize, s->abi = w->sabi, s->installed = (uint32_t)a->inst, s->armed = (uint32_t)a->armed;
    s->expired = (uint32_t)a->expired, s->error = (uint32_t)a->err;
    s->seen_position = b, s->seen_rotation = b + 1, s->forwarded_position = b * 2, s->forwarded_rotation = b + 3;
    s->weapon_updates = b + 4, s->zero_restores = b * 3;
    s->blend = (float)w->f1, s->target_weight = (float)w->f2;
  }
  return (uint32_t)a->ret;
}
static void fill_depth(TfpPoseDepthStats *s, uint64_t b, uint32_t i1, uint32_t i2, uint32_t i3) {
  s->installed = i1, s->armed = i2, s->supported = i3, s->reason = (uint32_t)(b % 7), s->zoom_index = (uint32_t)(b % 5);
  s->applied = b, s->unsupported = b + 1, s->cache_refreshes = b * 2, s->cache_checks = b + 3, s->ads_bypassed = b * 3;
  s->native_distance = (float)w->f1, s->reference_distance = (float)w->f2, s->correction = (float)(w->f1 + w->f2);
  s->zoom = (float)(w->f1 * 2), s->target_fov = (float)(w->f2 * 3), s->current_fov = (float)-w->f1, s->gain = (float)(w->f2 - w->f1);
}
static void fill_input(TfpPoseInputStats *s, uint64_t b, uint32_t i1, uint32_t i2, uint32_t i3) {
  s->installed = i1, s->active = i2, s->reason = (uint32_t)(b % 7), s->pending = i3, s->cache_entries = (uint32_t)(b % 5);
  s->observed = b, s->normalized = b + 1, s->unsupported = b * 2, s->restored = b + 3, s->foreign_wins = b + 4, s->quarantined = b * 3;
  s->ratio = (float)w->f1, s->yaw_gain = (float)w->f2, s->pitch_gain = (float)(w->f1 + w->f2);
}
#define DIAGNOSTIC(name, tag, type, filler)                                                             \
  static uint32_t name(uint64_t gen, type *s) {                                                        \
    note(tag "(%llu %u %u %u)", U(gen), s->size, s->abi, s->reserved);                                 \
    const PoseDiagnostic *d = &w->dg;                                                                  \
    if (d->ret < 0) {                                                                                  \
      if (!token(gen)) {                                                                               \
        sim.lasterr = 17;                                                                              \
        return 0;                                                                                      \
      }                                                                                                \
      filler(s, sim.seen, sim.gen != 0, sim.armed, 1);                                                 \
      return 1;                                                                                        \
    }                                                                                                  \
    if (d->fill) {                                                                                     \
      filler(s, w->sbase, (uint32_t)d->i1, (uint32_t)d->i2, (uint32_t)d->i3);                            \
      s->size = d->size, s->abi = d->abi, s->reserved = d->reserved;                                   \
    }                                                                                                  \
    return (uint32_t)d->ret;                                                                           \
  }
DIAGNOSTIC(h_depth, "D", TfpPoseDepthStats, fill_depth)
DIAGNOSTIC(h_input, "I", TfpPoseInputStats, fill_input)
static uint32_t h_error(void) {
  note("e");
  long long r = w->err[count_error < 2 ? count_error : 2];
  count_error++;
  return r < 0 ? sim.lasterr : (uint32_t)r;
}
static bool b_clock(void *user, double *out) {
  (void)user;
  note("clk");
  if (w->clk) return false; /* 1: raised; 2, 3: returned something that is not a number */
  *out = w->now;
  return true;
}
static uint64_t b_tick(void *user) {
  (void)user;
  note("tick");
  return w->tick;
}

static const char *stats_text(const TfpPoseStatsView *s, char *out, size_t size) {
  char n[8][40];
  snprintf(out, size, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%u,%s,%s", num(s->seen_position, n[0], 40), num(s->seen_rotation, n[1], 40),
           num(s->forwarded_position, n[2], 40), num(s->forwarded_rotation, n[3], 40), num(s->weapon_updates, n[4], 40),
           num(s->zero_restores, n[5], 40), s->installed ? "T" : "F", s->armed ? "T" : "F", s->expired ? "T" : "F", s->error,
           num(s->blend, n[6], 40), num(s->target_weight, n[7], 40));
  return out;
}
static void numbers(char *out, size_t size, const double *values, unsigned count) {
  size_t used = strlen(out);
  char n[40];
  for (unsigned i = 0; i < count; i++) used += (size_t)snprintf(out + used, size - used, ",%s", num(values[i], n, sizeof n));
}

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(POSE_ADAPTER_VECTOR_COMMIT, commit) || strlen(POSE_ADAPTER_VECTOR_SHA256) != 64) {
    fprintf(stderr, "pose adapter vectors were cut from retail %s, expected %s\n", POSE_ADAPTER_VECTOR_COMMIT, commit);
    return 2;
  }
  static const TfpPoseHelperApi api = {h_validate, h_validate_input, h_begin, h_begin_steady, h_begin_input, h_begin_steady_input,
                                       h_placement, h_ads_placement, h_transform, h_authored, h_zoom, h_transition, h_end, h_stats,
                                       h_depth, h_input, h_error};
  static TfpPoseAdapterIo io;
  static TfpPoseAdapter adapter, other;
  static TfpPoseHandle foreign, stale, *slots[4];
  static TfpPosePrepared prepared, foreign_token;
  TfpPoseHandle *pool = NULL;
  unsigned count = sizeof pose_adapter_vectors / sizeof *pose_adapter_vectors, runs = 0, pooled = 0;
  unsigned begins = 0, ends = 0, setters = 0, reads = 0, validations = 0, refused = 0, raised = 0, calls = 0;
  int run = -1;
  for (unsigned i = 0; i < count; i++) {
    w = pose_adapter_vectors + i;
    if (w->run != run) {
      run = w->run;
      runs++;
      const PoseAdapterRun *r = pose_adapter_runs + run;
      io = (TfpPoseAdapterIo){NULL, r->clock ? b_clock : NULL, r->tick ? b_tick : NULL};
      TfpPoseAdapterProfile profile = TFP_POSE_ADAPTER_PROFILE_SHIPPED;
      profile.normalize_zoom_input = r->normalize != 0;
      tfp_pose_adapter_init(&adapter, &api, &io, profile);
      tfp_pose_adapter_init(&other, &api, &io, TFP_POSE_ADAPTER_PROFILE_SHIPPED);
      foreign = (TfpPoseHandle){.owner = &other, .generation = 5};
      stale = (TfpPoseHandle){.owner = &adapter, .generation = 6};
      prepared = (TfpPosePrepared){&adapter}; /* the runner hands a well-formed token when prepare never succeeded */
      foreign_token = (TfpPosePrepared){&other};
      memset(&sim, 0, sizeof sim);
      sim.next = 100u * (unsigned)(run + 1);
      memset(slots, 0, sizeof slots);
      unsigned needed = 0;
      for (unsigned j = i; j < count && pose_adapter_vectors[j].run == run; j++) needed += pose_adapter_vectors[j].call == PV_BEGIN;
      free(pool);
      pool = calloc(needed ? needed : 1, sizeof *pool);
      if (!pool) return 2;
      pooled = 0;
    }
    trace[0] = 0;
    count_end = count_stats = count_error = 0;
    if (w->sim_disarm >= 0) sim.armed = false, sim.error = (uint32_t)w->sim_disarm, sim.expired = w->sim_disarm == 18;
    TfpPoseHandle *handle = w->slot == -1 ? tfp_pose_adapter_owned_handle(&adapter) : w->slot == -3 ? &foreign : w->slot == -4 ? &stale
                            : w->slot >= 0 ? slots[w->slot] : NULL;
    TfpPoseAdapterConfig config = {w->cfg_missing, w->cfg[0], w->cfg[1], w->cfg[2], w->cfg[3], w->cfg[4], (uint32_t)w->cfg[5],
                                   (uint32_t)w->cfg[6], (uint32_t)w->cfg[7], (uint32_t)w->cfg[8], (uint32_t)w->cfg[9],
                                   (uint32_t)w->cfg[10], w->cfg[11], (uint32_t)w->cfg[12], (uint32_t)w->cfg[13],
                                   w->offset[0], w->offset[1], w->offset[2], w->steady != 0};
    const TfpPoseAdapterConfig *given = w->cfg_ok ? &config : NULL;
    const double *v[7];
    for (unsigned k = 0; k < 7; k++) v[k] = w->nilmask & (1u << k) ? NULL : &w->v[k];
    static char line[16384], second[1024], cache[512];
    const char *why = NULL, *first = "F";
    bool done = false, text = true;
    TfpPoseStatsView view;
    memset(&view, 0, sizeof view);
    second[0] = 0;
    switch (w->call) {
    case PV_STATS:
      done = tfp_pose_adapter_stats(&adapter, handle, &view, &why);
      if (done) stats_text(&view, second, sizeof second), text = false;
      break;
    case PV_DEPTH: {
      TfpPoseDepthView d;
      memset(&d, 0, sizeof d);
      done = tfp_pose_adapter_depth_stats(&adapter, handle, &d, &why);
      if (done) {
        snprintf(second, sizeof second, "%s,%s,%s", d.installed ? "T" : "F", d.armed ? "T" : "F", d.supported ? "T" : "F");
        numbers(second, sizeof second, (double[]){d.reason, d.zoom_index, d.applied, d.unsupported, d.cache_refreshes, d.cache_checks,
                d.ads_bypassed, d.native_distance, d.reference_distance, d.correction, d.zoom, d.target_fov, d.current_fov, d.gain}, 14);
        text = false;
      }
      break;
    }
    case PV_INPUT: {
      TfpPoseInputView d;
      memset(&d, 0, sizeof d);
      done = tfp_pose_adapter_input_stats(&adapter, handle, &d, &why);
      if (done) {
        snprintf(second, sizeof second, "%s,%s", d.installed ? "T" : "F", d.active ? "T" : "F");
        numbers(second, sizeof second, (double[]){d.reason, d.pending, d.cache_entries, d.observed, d.normalized, d.unsupported,
                d.restored, d.foreign_wins, d.quarantined, d.ratio, d.yaw_gain, d.pitch_gain}, 12);
        text = false;
      }
      break;
    }
    case PV_PREPARE: {
      TfpPosePrepared fresh = {NULL};
      TfpPoseAnswer answer = tfp_pose_adapter_prepare(&adapter, given, &fresh, &why);
      if (answer == TFP_POSE_RAISED) first = "R";
      done = answer == TFP_POSE_TRUE;
      if (done) {
        if (fresh.owner != &adapter) return fprintf(stderr, "vector %u: prepare returned a token of another owner\n", i), 1;
        prepared = fresh;
      }
      break;
    }
    case PV_BEGIN: {
      TfpPoseHandle *storage = &pool[pooled++];
      TfpPoseAnswer answer = tfp_pose_adapter_begin(&adapter, given, w->prep == 1 ? &prepared : w->prep == 2 ? &foreign_token : NULL,
                                                    storage, &why);
      if (answer == TFP_POSE_RAISED) first = "R";
      done = answer == TFP_POSE_TRUE;
      if (done && tfp_pose_adapter_owned_handle(&adapter) != storage)
        return fprintf(stderr, "vector %u: begin succeeded without owning its handle\n", i), 1;
      slots[w->slot] = tfp_pose_adapter_owned_handle(&adapter);
      break;
    }
    case PV_PLACEMENT: done = tfp_pose_adapter_set_placement(&adapter, handle, v[0], v[1], v[2], &why); break;
    case PV_ADS: done = tfp_pose_adapter_set_ads_placement(&adapter, handle, v[0], v[1], v[2], &why); break;
    case PV_MOTION:
      done = tfp_pose_adapter_set_motion(&adapter, handle, v[0], v[1], v[2], v[3], v[4], v[5], v[6], w->authored != 0, &why);
      break;
    case PV_ZOOM: {
      bool active = w->active == 1;
      done = tfp_pose_adapter_set_zoom_policy(&adapter, handle, w->active == 2 ? NULL : &active, &why);
      break;
    }
    case PV_TRANSITION: {
      TfpPosePhasePacket packet = {w->pk_epoch_num != 0, w->pk_epoch, w->pk_progress_num != 0, w->pk_progress, w->pk_ads != 2, w->pk_ads == 1};
      done = tfp_pose_adapter_transition(&adapter, handle, w->pk ? &packet : NULL, &why);
      break;
    }
    case PV_OWNED: done = tfp_pose_adapter_owned_handle(&adapter) != NULL; break;
    case PV_FINISH:
      done = tfp_pose_adapter_finish(&adapter, handle, &view, &why);
      if (done && view.has) stats_text(&view, second, sizeof second), text = false;
      break;
    default: return fprintf(stderr, "vector %u: unknown call %d\n", i, w->call), 2;
    }
    if (done) first = "T";
    if (done && why) return fprintf(stderr, "vector %u: a successful call left a text: %s\n", i, why), 1;
    if (text) snprintf(second, sizeof second, "%s", why ? why : "~");
    const TfpPoseHandle *owned = tfp_pose_adapter_owned_handle(&adapter);
    char owned_text[32] = "-", which[8] = "-", list[256] = "", at[40], due[40];
    if (owned) {
      snprintf(owned_text, sizeof owned_text, "%llu", U(owned->generation));
      snprintf(which, sizeof which, "x");
      for (int j = 3; j >= 0; j--)
        if (slots[j] == owned) snprintf(which, sizeof which, "%d", j);
    }
    for (int j = 0; j < 4; j++) {
      const TfpPoseHandle *h = slots[j];
      size_t used = strlen(list);
      if (!h) snprintf(list + used, sizeof list - used, "%s-", j ? " " : "");
      else snprintf(list + used, sizeof list - used, "%s%llu:%d:%s", j ? " " : "", U(h->generation), h->disarmed,
                    h->stats.has ? "T" : h->disarmed ? "~" : "F");
    }
    snprintf(line, sizeof line, "%s|%s|%s|%s|%s|%s|%s|%s|%s|%s", first, second, owned_text, which,
             adapter.has_cache ? stats_text(&adapter.cache, cache, sizeof cache) : "-",
             !adapter.stats_handle ? "~" : adapter.stats_handle == owned ? "o" : "x", adapter.has_at ? num(adapter.stats_at, at, sizeof at) : "~",
             adapter.has_cache ? num(adapter.stats_due, due, sizeof due) : "~", list, trace);
    if (strcmp(line, w->expect)) {
      fprintf(stderr, "pose adapter vector %u (run %s, call %d)\n  C      %s\n  retail %s\n", i, pose_adapter_runs[run].name, w->call,
              line, w->expect);
      return 1;
    }
    begins += strstr(trace, "B") != NULL;
    ends += strstr(trace, "E(") != NULL;
    validations += strstr(trace, "V") != NULL;
    setters += w->call >= PV_PLACEMENT && w->call <= PV_TRANSITION && trace[0];
    reads += strstr(trace, "S(") != NULL || strstr(trace, "D(") != NULL || strstr(trace, "I(") != NULL;
    refused += first[0] == 'F' && w->call != PV_OWNED;
    raised += first[0] == 'R';
    int depth = 0; /* one trace entry per call; a configuration's fields are inside its brackets */
    for (const char *p = trace; *p; p++) {
      calls += depth == 0 && (p == trace || p[-1] == ' ');
      depth += *p == '(' ? 1 : *p == ')' ? -1 : 0;
    }
  }
  free(pool);
  printf("Pose adapter: %u operations in %u runs answered as retail's pose.lua (%s): same results, texts, state and helper calls; "
         "%u refused, %u raised; %u with Validate, %u with Begin, %u with a setter, %u with a read, %u with End; %u helper and clock calls\n",
         count, runs, POSE_ADAPTER_VECTOR_COMMIT, refused, raised, validations, begins, setters, reads, ends, calls);
  return 0;
}
