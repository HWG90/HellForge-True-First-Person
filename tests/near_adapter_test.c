#include "near_adapter.h"
#include <math.h>
#include "vectors/near_adapter_vectors.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* A helper, backend, controller, clock and menu that answer what the vector says and record
 * what they were asked, built as tools/retail_ref/run_near_adapter.lua builds its own. */
static const NearAdapterVector *w;
static char trace[8192];
static unsigned count_end, count_stats, count_error;
static struct { uint64_t gen, next, seen; bool armed; uint32_t error, lasterr; float hip, aim; } sim;

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
static const char *reason(int index) { return index > 0 ? near_adapter_reasons[index] : NULL; }
static uint32_t h_begin(const TfpNearConfig *c, uint64_t *out) {
  char hip[40], aim[40];
  note("B(%u %u %llu %llu %llu %llu %llu %u %s %s %u %llu)", c->size, c->abi, (unsigned long long)c->game, (unsigned long long)c->exe,
       (unsigned long long)c->context, (unsigned long long)c->camera, (unsigned long long)c->avatar_owner, c->avatar_index,
       num(c->hip, hip, sizeof hip), num(c->aim, aim, sizeof aim), c->reserved, (unsigned long long)c->reserved2);
  long long r = w->begin;
  uint64_t g = w->begin_gen;
  if (r < 0) {
    if (sim.gen != 0) r = 0, g = 0, sim.lasterr = 2;
    else sim.next++, sim.gen = sim.next, sim.armed = true, sim.error = 0, sim.hip = c->hip, sim.aim = c->aim, r = 1, g = sim.gen;
  } else if (g != 0) sim.gen = g, sim.armed = r == 1, sim.error = 0;
  *out = g;
  return (uint32_t)r;
}
static uint32_t h_set(uint64_t gen, float hip, float aim) {
  char a[40], b[40];
  note("s(%llu %s %s)", (unsigned long long)gen, num(hip, a, sizeof a), num(aim, b, sizeof b));
  long long r = w->setr;
  if (r < 0) {
    if (gen == sim.gen && sim.armed) sim.hip = hip, sim.aim = aim, r = 1;
    else r = 0, sim.lasterr = 17;
  }
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
static uint32_t h_stats(uint64_t gen, TfpNearStats *s) {
  note("S(%llu %u %u)", (unsigned long long)gen, s->size, s->abi);
  const NearAnswer *a = &w->st[count_stats < 2 ? count_stats : 2];
  count_stats++;
  if (a->ret < 0) {
    if ((gen == sim.gen && gen != 0) || (gen == 0 && sim.gen == 0)) {
      sim.seen += 3;
      s->size = 88, s->abi = 1, s->installed = sim.gen != 0, s->armed = sim.armed, s->error = sim.error;
      s->seen = sim.seen, s->forwarded = sim.seen + 1, s->replaced = sim.seen * 2, s->ads_forwarded = sim.seen + 3;
      s->aim_applied = sim.seen + 4, s->other_kind = sim.seen * 3;
      s->last_native = 0.05f, s->last_applied = (float)((double)sim.seen * 0.001), s->hip = sim.hip, s->aim = sim.aim;
      return 1;
    }
    sim.lasterr = 17;
    return 0;
  }
  if (a->fill) {
    uint64_t b = w->sbase;
    s->size = w->ssize, s->abi = w->sabi, s->installed = (uint32_t)a->inst, s->armed = (uint32_t)a->armed, s->error = (uint32_t)a->err;
    s->seen = b, s->forwarded = b + 1, s->replaced = b * 2, s->ads_forwarded = b + 3, s->aim_applied = b + 4, s->other_kind = b * 3;
    s->last_native = (float)w->f1, s->last_applied = (float)w->f2, s->hip = (float)w->f3, s->aim = (float)w->f4;
  }
  return (uint32_t)a->ret;
}
static uint32_t h_error(void) {
  note("e");
  long long r = w->err[count_error < 2 ? count_error : 2];
  count_error++;
  return r < 0 ? sim.lasterr : (uint32_t)r;
}

static void fill_config(TfpNearCamera *c) {
  *c = (TfpNearCamera){w->cfg_missing, w->cfg[0], w->cfg[1], w->cfg[2], w->cfg[3], w->cfg[4], (uint32_t)w->cfg[5]};
}
static bool b_config(void *user, const TfpNearPoseLease *lease, TfpNearCamera *out, const char **why) {
  (void)user; (void)lease;
  note("cfg");
  if (w->cfg_throw || !w->cfg_ok) {
    *why = reason(w->cfg_why);
    return false;
  }
  fill_config(out);
  return true;
}
static void c_result(void *user, TfpNearControllerState *out) {
  (void)user;
  note("res");
  *out = (TfpNearControllerState){w->mode != 0, w->running != 0};
}
static bool c_enabled(void *user) { (void)user; note("en"); return w->en != 0; }
static TfpNearPoseLease pose_lease;
static const TfpNearPoseLease *c_lease(void *user) {
  (void)user;
  if (w->lease <= 0) return NULL;
  pose_lease = (TfpNearPoseLease){w->lo != 0, w->lo ? (uint64_t)w->lp : 0};
  return &pose_lease;
}
static void b_options(void *user, TfpNearOptions *out) {
  (void)user;
  note("opt");
  *out = (TfpNearOptions){w->opt_table != 0, w->hip_num != 0, w->hip, w->aim_num != 0, w->aim};
}
static bool b_clock(void *user, double *out, const char **error) {
  (void)user;
  note("clk");
  if (w->clk) {
    *error = "Engine FPV clock sample invalid";
    return false;
  }
  *out = w->now;
  return true;
}
static void b_log(void *user, const char *text) { (void)user; note("L[%s]", text); }

static const char *opt(bool has, double x, char *out, size_t size) { return has ? num(x, out, size) : "~"; }
static const char *stats_text(const TfpNearStatsView *s, char *out, size_t size) {
  char n[10][40];
  if (!s->has) snprintf(out, size, "~,~,~,~,~,~,~,~,~,~,~,~,~");
  else snprintf(out, size, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%u", num(s->seen, n[0], 40), num(s->forwarded, n[1], 40),
                num(s->replaced, n[2], 40), num(s->ads_forwarded, n[3], 40), num(s->aim_applied, n[4], 40),
                num(s->other_kind, n[5], 40), num(s->last_native, n[6], 40), num(s->last_applied, n[7], 40), num(s->hip, n[8], 40),
                num(s->aim, n[9], 40), s->installed ? "T" : "F", s->armed ? "T" : "F", s->error);
  return out;
}
static const char *const phases[] = {"idle", "active", "cleanup_pending"};

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(NEAR_ADAPTER_VECTOR_COMMIT, commit) || strlen(NEAR_ADAPTER_VECTOR_SHA256) != 64) {
    fprintf(stderr, "near adapter vectors were cut from retail %s, expected %s\n", NEAR_ADAPTER_VECTOR_COMMIT, commit);
    return 2;
  }
  static const TfpNearHelperApi api = {h_begin, h_set, h_end, h_stats, h_error};
  static TfpNearIo io;
  static TfpNearLease lease;
  static TfpNearAdapter other;
  static TfpNearHandle foreign, stale, pool[64], *slots[4];
  unsigned count = sizeof near_adapter_vectors / sizeof *near_adapter_vectors, runs = 0, pooled = 0, begins = 0, ends = 0, sets = 0;
  int run = -1;
  for (unsigned i = 0; i < count; i++) {
    w = near_adapter_vectors + i;
    if (w->run != run) {
      run = w->run;
      runs++;
      const NearAdapterRun *r = near_adapter_runs + run;
      io = (TfpNearIo){NULL, b_config, c_result, c_enabled, c_lease, b_options, b_clock, r->nolog ? NULL : b_log};
      TfpNearProfile profile = TFP_NEAR_PROFILE_SHIPPED;
      profile.pull_sight_distance = r->pull != 0;
      tfp_near_lease_init(&lease, &api, &io, profile);
      tfp_near_adapter_init(&other, &api, TFP_NEAR_PROFILE_SHIPPED);
      foreign = (TfpNearHandle){.owner = &other, .generation = 5};
      stale = (TfpNearHandle){.owner = &lease.helper, .generation = 6};
      memset(&sim, 0, sizeof sim);
      sim.next = 100u * (unsigned)(run + 1);
      memset(slots, 0, sizeof slots);
      pooled = 0;
    }
    trace[0] = 0;
    count_end = count_stats = count_error = 0;
    if (w->sim_disarm >= 0) sim.armed = false, sim.error = (uint32_t)w->sim_disarm;
    TfpNearHandle *handle = w->slot == -1 ? lease.handle : w->slot == -3 ? &foreign : w->slot == -4 ? &stale
                            : w->slot >= 0 ? slots[w->slot] : NULL;
    TfpNearCamera config;
    fill_config(&config);
    TfpNearStatsView view = {0};
    const char *why = NULL, *first = "F";
    bool a = false, table = false;
    switch (w->call) {
    case NV_UPDATE: {
      TfpNearAnswer answer = tfp_near_lease_update(&lease, &why);
      first = answer == TFP_NEAR_RAISED ? "R" : answer == TFP_NEAR_TRUE ? "T" : "F";
      a = answer == TFP_NEAR_TRUE;
      break;
    }
    case NV_STOP: a = tfp_near_lease_stop(&lease, &why); break;
    case NV_SHUTDOWN: a = tfp_near_lease_shutdown(&lease, &why); break;
    case NV_A_BEGIN:
      if (pooled >= 64) return fprintf(stderr, "vector %u: handle pool exhausted\n", i), 2;
      a = tfp_near_adapter_begin(&lease.helper, w->cfg_ok ? &config : NULL, w->hip, w->aim, &pool[pooled], &why);
      slots[w->slot] = a ? &pool[pooled] : lease.helper.owned;
      pooled++;
      break;
    case NV_A_SET: a = tfp_near_adapter_set(&lease.helper, handle, w->hip, w->aim, &why); break;
    case NV_A_STATS: a = table = tfp_near_adapter_stats(&lease.helper, handle, &view, &why); break;
    case NV_A_FINISH:
      a = tfp_near_adapter_finish(&lease.helper, handle, &view, &why);
      table = a && view.has;
      break;
    }
    if (a) first = "T";
    static char line[16384], second[512], last[512], result_stats[512], status[512];
    char n[5][40], player[32];
    TfpNearResult r;
    tfp_near_lease_result(&lease, &r);
    tfp_near_lease_status(&lease, status, sizeof status);
    if (table) stats_text(&view, second, sizeof second);
    else snprintf(second, sizeof second, "%s", why ? why : "~");
    if (lease.bound_player) snprintf(player, sizeof player, "p%llu", (unsigned long long)lease.bound_player);
    else snprintf(player, sizeof player, "~");
    snprintf(line, sizeof line, "%s|%s|%s|%d|%s|%s|%s|%s|%s|%s|%s|%s|%s|%d|%s|%s;%s;%s;%s|%s|%s", first, second, phases[lease.phase],
             lease.handle ? (lease.handle->disarmed ? 2 : 1) : 0, player, opt(lease.has_hip, lease.hip, n[0], 40),
             opt(lease.has_aim, lease.aim, n[1], 40), lease.closing ? "T" : "F", lease.has_fault ? lease.fault : "~",
             opt(lease.has_retry_at, lease.retry_at, n[2], 40), opt(lease.has_next_collect, lease.next_collect, n[3], 40),
             num(lease.last_collect, n[4], 40), stats_text(&lease.last, last, sizeof last), lease.helper.owned != NULL,
             tfp_near_lease_is_clean(&lease) ? "T" : "F", phases[r.phase], r.cleanup_pending ? "T" : "F",
             r.error_message ? r.error_message : "~", stats_text(&r.stats, result_stats, sizeof result_stats), trace, status);
    if (strcmp(line, w->expect)) {
      fprintf(stderr, "near adapter vector %u (run %s, call %d)\n  C      %s\n  retail %s\n", i, near_adapter_runs[run].name, w->call,
              line, w->expect);
      return 1;
    }
    begins += strstr(trace, "B(") != NULL;
    ends += strstr(trace, "E(") != NULL;
    sets += strstr(trace, "s(") != NULL;
  }
  printf("Near adapter: %u operations in %u runs answered as retail's near_clip.lua (%s): same results, texts, state and "
         "helper/backend calls; %u with Begin, %u with Set, %u with End\n", count, runs, NEAR_ADAPTER_VECTOR_COMMIT, begins, sets, ends);
  return 0;
}
