#include "blur_adapter.h"
#include <math.h>
#include "vectors/blur_adapter_vectors.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* A helper, backend, controller and clock that answer what the vector says and record what
 * they were asked, built as tools/retail_ref/run_blur_adapter.lua builds its own. */
static const BlurAdapterVector *w;
static char trace[8192];
static unsigned count_end, count_stats, count_error;
static struct { uint64_t gen, next, seen; bool armed; uint32_t error, lasterr; } sim;

static void note(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void note(const char *format, ...) {
  size_t used = strlen(trace);
  va_list args;
  if (used) trace[used++] = ' ';
  va_start(args, format);
  vsnprintf(trace + used, sizeof trace - used, format, args);
  va_end(args);
}
static const char *reason(int index) { return index > 0 ? blur_adapter_reasons[index] : NULL; }
static void note_config(const char *name, const TfpBlurConfig *c) {
  note("%s(%u %u %llu %llu %llu %llu %llu %llu %u %u %u %u %llu)", name, c->size, c->abi, (unsigned long long)c->game,
       (unsigned long long)c->exe, (unsigned long long)c->context, (unsigned long long)c->camera,
       (unsigned long long)c->avatar_owner, (unsigned long long)c->request, c->avatar_index, c->avatar, c->body, c->weapon,
       (unsigned long long)c->reserved);
}
static uint32_t h_validate(const TfpBlurConfig *c) {
  note_config("V", c);
  long long r = w->validate;
  if (r < 0) {
    if (w->nosrc) r = 0, sim.lasterr = 18;
    else r = 1;
  }
  return (uint32_t)r;
}
static uint32_t h_begin(const TfpBlurConfig *c, uint64_t *out) {
  note_config("B", c);
  long long r = w->begin;
  uint64_t g = w->begin_gen;
  if (r < 0) {
    if (sim.gen != 0) r = 0, g = 0, sim.lasterr = 2;
    else if (w->nosrc) r = 0, g = 0, sim.lasterr = 18;
    else sim.next++, sim.gen = sim.next, sim.armed = true, sim.error = 0, r = 1, g = sim.gen;
  } else if (g != 0) sim.gen = g, sim.armed = r == 1, sim.error = 0;
  *out = g;
  return (uint32_t)r;
}
static uint32_t h_end(uint64_t gen) {
  note("E(%llu)", (unsigned long long)gen);
  long long r = w->endr[count_end < 1 ? count_end : 1];
  count_end++;
  if (r < 0) {
    if (gen == sim.gen && sim.gen != 0) sim.gen = 0, sim.armed = false, r = 1;
    else r = 0, sim.lasterr = 17;
  } else if (r == 1) sim.gen = 0, sim.armed = false;
  return (uint32_t)r;
}
static uint32_t h_stats(uint64_t gen, TfpBlurStats *s) {
  note("S(%llu %u %u)", (unsigned long long)gen, s->size, s->abi);
  const BlurAnswer *a = &w->st[count_stats < 2 ? count_stats : 2];
  count_stats++;
  if (a->ret < 0) {
    if ((gen == sim.gen && gen != 0) || (gen == 0 && sim.gen == 0)) {
      sim.seen += 3;
      s->size = 80, s->abi = 1, s->installed = sim.gen != 0, s->armed = sim.armed, s->error = sim.error;
      s->seen = sim.seen, s->forwarded = sim.seen + 1, s->replaced = sim.seen * 2, s->immediate_applies = sim.seen + 3;
      s->last_hash = 0x9B115563u, s->last_instance = 4;
      s->last_native = 1.25f, s->last_applied = (float)((double)sim.seen * 0.001);
      return 1;
    }
    sim.lasterr = 17;
    return 0;
  }
  if (a->fill) {
    uint64_t b = w->sbase;
    s->size = w->ssize, s->abi = w->sabi, s->installed = (uint32_t)a->inst, s->armed = (uint32_t)a->armed, s->error = (uint32_t)a->err;
    s->seen = b, s->forwarded = b + 1, s->replaced = b * 2, s->immediate_applies = b + 3;
    s->last_hash = (uint32_t)(b + 4), s->last_instance = (uint32_t)(b * 3);
    s->last_native = (float)w->f1, s->last_applied = (float)w->f2;
  }
  return (uint32_t)a->ret;
}
static uint32_t h_error(void) {
  note("e");
  long long r = w->err[count_error < 2 ? count_error : 2];
  count_error++;
  return r < 0 ? sim.lasterr : (uint32_t)r;
}

static void fill_config(TfpBlurCamera *c) {
  *c = (TfpBlurCamera){w->cfg_missing, w->cfg[0], w->cfg[1], w->cfg[2], w->cfg[3], w->cfg[4], w->cfg[5],
                       (uint32_t)w->cfg[6], (uint32_t)w->cfg[7], (uint32_t)w->cfg[8], (uint32_t)w->cfg[9]};
}
static bool b_config(void *user, const TfpBlurPoseLease *lease, TfpBlurCamera *out, const char **why) {
  (void)user;
  if (lease) note("cfg(%llu)", (unsigned long long)lease->identity);
  else note("cfg(-)");
  if (w->cfg_throw || !w->cfg_ok) {
    *why = reason(w->cfg_why);
    return false;
  }
  fill_config(out);
  return true;
}
static void c_result(void *user, TfpBlurControllerState *out) {
  (void)user;
  note("res");
  *out = (TfpBlurControllerState){w->mode != 0, w->running != 0};
}
static bool c_enabled(void *user) { (void)user; note("en"); return w->en != 0; }
static TfpBlurPoseLease pose_lease;
static const TfpBlurPoseLease *c_lease(void *user) {
  (void)user;
  if (w->lease <= 0) return NULL;
  pose_lease = (TfpBlurPoseLease){(uint64_t)w->lease, w->lr >= 0, (uint64_t)w->lr, w->lw >= 0, (uint64_t)w->lw, w->lo != 0,
                                  w->lo ? (uint64_t)w->lp : 0, w->lo && w->low >= 0, w->lo ? (uint64_t)w->low : 0};
  return &pose_lease;
}
static bool b_clock(void *user, double *out) {
  (void)user;
  note("clk");
  if (w->clk) return false;
  *out = w->now;
  return true;
}
static void b_log(void *user, const char *text) { (void)user; note("L[%s]", text); }

static const char *num(double x, char *out, size_t size) {
  if (x != x) snprintf(out, size, "nan");
  else if (x == INFINITY) snprintf(out, size, "inf");
  else if (x == -INFINITY) snprintf(out, size, "-inf");
  else snprintf(out, size, "%.17g", x);
  return out;
}
static const char *opt(bool has, double x, char *out, size_t size) { return has ? num(x, out, size) : "~"; }
static const char *stats_text(const TfpBlurStatsView *s, char *out, size_t size) {
  char n[8][40];
  if (!s->has) snprintf(out, size, "~,~,~,~,~,~,~,~,~,~,~");
  else snprintf(out, size, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%u", num(s->seen, n[0], 40), num(s->forwarded, n[1], 40),
                num(s->replaced, n[2], 40), num(s->immediate_applies, n[3], 40), num(s->last_hash, n[4], 40),
                num(s->last_instance, n[5], 40), num(s->last_native, n[6], 40), num(s->last_applied, n[7], 40),
                s->installed ? "T" : "F", s->armed ? "T" : "F", s->error);
  return out;
}
static const char *const phases[] = {"idle", "waiting", "unavailable", "active", "cleanup_pending"};
static const char *const kinds[] = {"~", "temporary_read", "unsafe"};
static const char *const answers[] = {"F", "T", "~"};

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(BLUR_ADAPTER_VECTOR_COMMIT, commit) || strlen(BLUR_ADAPTER_VECTOR_SHA256) != 64) {
    fprintf(stderr, "blur adapter vectors were cut from retail %s, expected %s\n", BLUR_ADAPTER_VECTOR_COMMIT, commit);
    return 2;
  }
  static const TfpBlurHelperApi api = {h_validate, h_begin, h_end, h_stats, h_error};
  static TfpBlurIo io;
  static TfpBlurLease lease;
  static TfpBlurAdapter other;
  static TfpBlurHandle foreign, stale, pool[64], *slots[4];
  unsigned count = sizeof blur_adapter_vectors / sizeof *blur_adapter_vectors, runs = 0, pooled = 0, begins = 0, ends = 0;
  int run = -1;
  for (unsigned i = 0; i < count; i++) {
    w = blur_adapter_vectors + i;
    if (w->run != run) {
      run = w->run;
      runs++;
      io = (TfpBlurIo){NULL, b_config, c_result, c_enabled, c_lease, b_clock, blur_adapter_runs[run].nolog ? NULL : b_log};
      tfp_blur_lease_init(&lease, &api, &io, TFP_BLUR_PROFILE_SHIPPED);
      tfp_blur_adapter_init(&other, &api, &io, TFP_BLUR_PROFILE_SHIPPED);
      foreign = (TfpBlurHandle){.owner = &other, .generation = 5};
      stale = (TfpBlurHandle){.owner = &lease.helper, .generation = 6};
      memset(&sim, 0, sizeof sim);
      sim.next = 100u * (unsigned)(run + 1);
      memset(slots, 0, sizeof slots);
      pooled = 0;
    }
    trace[0] = 0;
    count_end = count_stats = count_error = 0;
    if (w->sim_disarm >= 0) sim.armed = false, sim.error = (uint32_t)w->sim_disarm;
    TfpBlurHandle *handle = w->slot == -1 ? lease.handle : w->slot == -3 ? &foreign : w->slot == -4 ? &stale
                            : w->slot >= 0 ? slots[w->slot] : NULL;
    TfpBlurCamera config;
    fill_config(&config);
    const TfpBlurCamera *given = w->cfg_ok ? &config : NULL;
    TfpBlurStatsView view = {0};
    TfpBlurCode code = {false, 0};
    const char *why = NULL, *first = "F";
    bool a = false, table = false;
    switch (w->call) {
    case BV_UPDATE: a = tfp_blur_lease_update(&lease, &why); first = answers[a]; break;
    case BV_STOP: a = tfp_blur_lease_stop(&lease, &why); first = answers[a]; break;
    case BV_SHUTDOWN: a = tfp_blur_lease_shutdown(&lease, &why); first = answers[a]; break;
    case BV_PREFLIGHT: first = answers[tfp_blur_lease_preflight(&lease, &why)]; break;
    case BV_A_VALIDATE: first = answers[tfp_blur_adapter_validate(&lease.helper, given, &why, &code)]; break;
    case BV_A_BEGIN:
      if (pooled >= 64) return fprintf(stderr, "vector %u: handle pool exhausted\n", i), 2;
      a = tfp_blur_adapter_begin(&lease.helper, given, &pool[pooled], &why, &code);
      slots[w->slot] = a ? &pool[pooled] : lease.helper.owned;
      pooled++;
      first = answers[a];
      break;
    case BV_A_STATS: a = table = tfp_blur_adapter_stats(&lease.helper, handle, &view, &why); first = answers[a]; break;
    case BV_A_FINISH:
      a = tfp_blur_adapter_finish(&lease.helper, handle, &view, &why);
      table = a && view.has;
      first = answers[a];
      break;
    }
    static char line[16384], second[512], last[512], result_stats[512], status[512];
    char n[10][40], player[32];
    TfpBlurResult r;
    tfp_blur_lease_result(&lease, &r);
    tfp_blur_lease_status(&lease, status, sizeof status);
    if (table) stats_text(&view, second, sizeof second);
    else snprintf(second, sizeof second, "%s", why ? why : "~");
    if (lease.bound_player) snprintf(player, sizeof player, "p%llu", (unsigned long long)lease.bound_player);
    else snprintf(player, sizeof player, "~");
    const TfpBlurAdapter *h = &lease.helper;
    snprintf(line, sizeof line, "%s|%s|%s|%s|%d|%llu|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%d|%s|%d|%s|%s|%s|%s;%s;%s;%s;%s;%s;%s;%s|%s|%s",
             first, second, opt(code.has, code.value, n[0], 40), phases[lease.phase],
             lease.handle ? (lease.handle->disarmed ? 2 : 1) : 0, (unsigned long long)lease.bound,
             opt(lease.has_bound_request, (double)lease.bound_request, n[1], 40),
             opt(lease.has_bound_weapon, (double)lease.bound_weapon, n[2], 40), player, lease.closing ? "T" : "F",
             lease.has_fault ? lease.fault : "~", opt(lease.failure_native_error.has, lease.failure_native_error.value, n[3], 40),
             kinds[lease.failure_kind], lease.retiring ? "T" : "~", opt(lease.has_source_times, lease.source_checked_at, n[4], 40),
             opt(lease.has_source_times, lease.source_retry_at, n[5], 40), stats_text(&lease.last, last, sizeof last),
             h->owned != NULL, opt(h->failure_native_error.has, h->failure_native_error.value, n[6], 40), h->has_cache,
             opt(h->has_at, h->stats_at, n[7], 40), opt(h->has_cache, h->stats_due, n[8], 40),
             tfp_blur_lease_is_clean(&lease) ? "T" : "F", phases[r.phase], r.cleanup_pending ? "T" : "F",
             r.error_message ? r.error_message : "~", r.failure_native_error.has ? num(r.failure_native_error.value, n[9], 40) : "~",
             kinds[r.failure_kind], r.retiring ? "T" : "F", r.retirement_pending ? "T" : "F",
             stats_text(&r.stats, result_stats, sizeof result_stats), trace, status);
    if (strcmp(line, w->expect)) {
      fprintf(stderr, "blur adapter vector %u (run %s, call %d)\n  C      %s\n  retail %s\n", i, blur_adapter_runs[run].name, w->call,
              line, w->expect);
      return 1;
    }
    begins += strstr(trace, "B(") != NULL;
    ends += strstr(trace, "E(") != NULL;
  }
  printf("Blur adapter: %u operations in %u runs answered as retail's blur.lua (%s): same results, texts, state and helper/backend "
         "calls; %u with Begin, %u with End\n", count, runs, BLUR_ADAPTER_VECTOR_COMMIT, begins, ends);
  return 0;
}
