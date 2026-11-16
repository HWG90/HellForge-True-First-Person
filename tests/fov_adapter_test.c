#include "fov_adapter.h"
#include <math.h>
#include "vectors/fov_adapter_vectors.h"
#include <stdio.h>
#include <string.h>

/* A helper, backend, controller and clock that answer what the vector says and record what
 * they were asked, built as tools/retail_ref/run_fov_adapter.lua builds its own. */
static const FovAdapterVector *w;
static char trace[8192];
static unsigned count_end, count_stats, count_error;
static struct { uint64_t gen, next, seen; bool armed; uint32_t error, lasterr; } sim;

static void note(const char *format, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void note(const char *format, ...) {
  size_t used = strlen(trace);
  va_list args;
  if (used) trace[used++] = ' ';
  va_start(args, format);
  vsnprintf(trace + used, sizeof trace - used, format, args);
  va_end(args);
}
static const char *reason(int index) { return index > 0 ? fov_adapter_reasons[index] : NULL; }
static void note_config(const char *name, const TfpFovConfig *c) {
  note("%s(%u %u %llu %llu %llu %llu %llu %llu %u %u %u %u %llu)", name, c->size, c->abi, (unsigned long long)c->game,
       (unsigned long long)c->exe, (unsigned long long)c->context, (unsigned long long)c->camera,
       (unsigned long long)c->avatar_owner, (unsigned long long)c->request, c->avatar_index, c->avatar, c->body, c->weapon,
       (unsigned long long)c->reserved);
}
static uint32_t h_validate(const TfpFovConfig *c) {
  note_config("V", c);
  return w->validate < 0 ? 1 : (uint32_t)w->validate;
}
static uint32_t h_begin(const TfpFovConfig *c, uint64_t *out) {
  note_config("B", c);
  long long r = w->begin;
  uint64_t g = w->begin_gen;
  if (r < 0) {
    if (sim.gen != 0) r = 0, g = 0, sim.lasterr = 2;
    else sim.next++, sim.gen = sim.next, sim.armed = true, sim.error = 0, r = 1, g = sim.gen;
  } else if (g != 0) sim.gen = g, sim.armed = r == 1, sim.error = 0;
  *out = g;
  return (uint32_t)r;
}
static uint32_t h_bind(uint64_t gen, uint64_t request, uint32_t weapon) {
  note("b(%llu %llu %u)", (unsigned long long)gen, (unsigned long long)request, weapon);
  long long r = w->bind;
  if (r < 0) {
    if (gen == sim.gen && sim.armed) r = 1;
    else r = 0, sim.lasterr = 17;
  }
  return (uint32_t)r;
}
static uint32_t h_transition(uint64_t gen, uint64_t epoch, float progress, uint32_t ads) {
  note("T(%llu %llu %.9g %u)", (unsigned long long)gen, (unsigned long long)epoch, (double)progress, ads);
  long long r = w->trans;
  if (r < 0) {
    if (gen == sim.gen && sim.armed) r = 1;
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
static uint32_t h_stats(uint64_t gen, TfpFovStats *s) {
  note("S(%llu %u %u)", (unsigned long long)gen, s->size, s->abi);
  const FovAnswer *a = &w->st[count_stats < 2 ? count_stats : 2];
  count_stats++;
  if (a->ret < 0) {
    if ((gen == sim.gen && gen != 0) || (gen == 0 && sim.gen == 0)) {
      sim.seen += 3;
      s->size = 88, s->abi = 1, s->installed = sim.gen != 0, s->armed = sim.armed, s->error = sim.error;
      s->seen = sim.seen, s->forwarded = sim.seen + 1, s->replaced = sim.seen * 2, s->immediate_applies = sim.seen + 3;
      s->binds = sim.seen + 4, s->ads_forwarded = sim.seen * 3;
      s->last_native = 1.25f, s->last_applied = (float)((double)sim.seen * 0.001), s->source_degrees = 70;
      return 1;
    }
    sim.lasterr = 17;
    return 0;
  }
  if (a->fill) {
    uint64_t b = w->sbase;
    s->size = w->ssize, s->abi = w->sabi, s->installed = (uint32_t)a->inst, s->armed = (uint32_t)a->armed, s->error = (uint32_t)a->err;
    s->seen = b, s->forwarded = b + 1, s->replaced = b * 2, s->immediate_applies = b + 3, s->binds = b + 4, s->ads_forwarded = b * 3;
    s->last_native = (float)w->f1, s->last_applied = (float)w->f2, s->source_degrees = (float)w->f3;
  }
  return (uint32_t)a->ret;
}
static uint32_t h_error(void) {
  note("e");
  long long r = w->err[count_error < 2 ? count_error : 2];
  count_error++;
  return r < 0 ? sim.lasterr : (uint32_t)r;
}

static void fill_config(TfpFovCamera *c) {
  *c = (TfpFovCamera){w->cfg_missing, w->cfg[0], w->cfg[1], w->cfg[2], w->cfg[3], w->cfg[4], w->cfg[5],
                      (uint32_t)w->cfg[6], (uint32_t)w->cfg[7], (uint32_t)w->cfg[8], (uint32_t)w->cfg[9]};
}
static bool b_config(void *user, const TfpFovPoseLease *lease, TfpFovCamera *out, const char **why) {
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
static void token(TfpFovPacket *out, const char **why) {
  memset(out, 0, sizeof *out);
  if (w->pk == 1) {
    *out = (TfpFovPacket){TFP_FOV_PACKET_TABLE, w->pk_epoch_num != 0, w->pk_epoch, w->pk_progress_num != 0, w->pk_progress, w->pk_ads};
  } else if (w->pk == 2) {
    out->kind = TFP_FOV_PACKET_OTHER;
    *why = reason(w->pk_why);
  }
}
static void b_packet(void *user, TfpFovPacket *out, const char **why) {
  (void)user;
  note("pkt");
  token(out, why);
}
static void c_result(void *user, TfpFovControllerState *out) {
  (void)user;
  note("res");
  *out = (TfpFovControllerState){w->mode != 0, w->running != 0};
}
static bool c_enabled(void *user) { (void)user; note("en"); return w->en != 0; }
static TfpFovPoseLease pose_lease;
static const TfpFovPoseLease *c_lease(void *user) {
  (void)user;
  if (w->lease <= 0) return NULL;
  pose_lease = (TfpFovPoseLease){(uint64_t)w->lease, w->lr >= 0, (uint64_t)w->lr, w->lw >= 0, (uint64_t)w->lw, w->lo != 0,
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
static const char *stats_text(const TfpFovStatsView *s, char *out, size_t size) {
  char n[9][40];
  if (!s->has) snprintf(out, size, "~,~,~,~,~,~,~,~,~,~,~,~");
  else snprintf(out, size, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%u", num(s->seen, n[0], 40), num(s->forwarded, n[1], 40),
                num(s->replaced, n[2], 40), num(s->immediate_applies, n[3], 40), num(s->binds, n[4], 40),
                num(s->ads_forwarded, n[5], 40), num(s->last_native, n[6], 40), num(s->last_applied, n[7], 40),
                num(s->source_degrees, n[8], 40), s->installed ? "T" : "F", s->armed ? "T" : "F", s->error);
  return out;
}
static const char *const phases[] = {"idle", "active", "cleanup_pending"};
static const char *const kinds[] = {"~", "temporary_read", "unsafe"};

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(FOV_ADAPTER_VECTOR_COMMIT, commit) || strlen(FOV_ADAPTER_VECTOR_SHA256) != 64) {
    fprintf(stderr, "fov adapter vectors were cut from retail %s, expected %s\n", FOV_ADAPTER_VECTOR_COMMIT, commit);
    return 2;
  }
  static const TfpFovHelperApi api = {h_validate, h_begin, h_bind, h_transition, h_end, h_stats, h_error};
  static TfpFovIo io;
  static TfpFovLease lease;
  static TfpFovAdapter other;
  static TfpFovHandle foreign, stale, pool[64], *slots[4];
  unsigned count = sizeof fov_adapter_vectors / sizeof *fov_adapter_vectors, runs = 0, pooled = 0, begins = 0, ends = 0;
  int run = -1;
  for (unsigned i = 0; i < count; i++) {
    w = fov_adapter_vectors + i;
    if (w->run != run) {
      run = w->run;
      runs++;
      const FovAdapterRun *r = fov_adapter_runs + run;
      io = (TfpFovIo){NULL, b_config, b_packet, c_result, c_enabled, c_lease, r->noclock ? NULL : b_clock, r->nolog ? NULL : b_log};
      tfp_fov_lease_init(&lease, &api, &io, TFP_FOV_PROFILE_SHIPPED);
      tfp_fov_adapter_init(&other, &api, &io, TFP_FOV_PROFILE_SHIPPED);
      foreign = (TfpFovHandle){.owner = &other, .generation = 5};
      stale = (TfpFovHandle){.owner = &lease.helper, .generation = 6};
      memset(&sim, 0, sizeof sim);
      sim.next = 100u * (unsigned)(run + 1);
      memset(slots, 0, sizeof slots);
      pooled = 0;
    }
    trace[0] = 0;
    count_end = count_stats = count_error = 0;
    if (w->sim_disarm >= 0) sim.armed = false, sim.error = (uint32_t)w->sim_disarm;
    io.presentation_transition = w->pk_fn ? b_packet : NULL;
    TfpFovHandle *handle = w->slot == -1 ? lease.handle : w->slot == -3 ? &foreign : w->slot == -4 ? &stale : w->slot >= 0 ? slots[w->slot] : NULL;
    TfpFovCamera config;
    fill_config(&config);
    const TfpFovCamera *given = w->cfg_ok ? &config : NULL;
    TfpFovStatsView view = {0};
    TfpFovPacket packet;
    const char *why = NULL, *unused = NULL;
    bool a = false, table = false;
    switch (w->call) {
    case FV_UPDATE: a = tfp_fov_lease_update(&lease, &why); break;
    case FV_STOP: a = tfp_fov_lease_stop(&lease, &why); break;
    case FV_SHUTDOWN: a = tfp_fov_lease_shutdown(&lease, &why); break;
    case FV_PREFLIGHT: a = tfp_fov_lease_preflight(&lease, &why); break;
    case FV_A_VALIDATE: a = tfp_fov_adapter_validate(&lease.helper, given, &why); break;
    case FV_A_BEGIN:
      if (pooled >= 64) return fprintf(stderr, "vector %u: handle pool exhausted\n", i), 2;
      a = tfp_fov_adapter_begin(&lease.helper, given, &pool[pooled], &why);
      slots[w->slot] = a ? &pool[pooled] : lease.helper.owned;
      pooled++;
      break;
    case FV_A_BIND: a = tfp_fov_adapter_bind(&lease.helper, handle, &config, &why); break;
    case FV_A_TRANSITION:
      token(&packet, &unused);
      a = tfp_fov_adapter_transition(&lease.helper, handle, &packet, &why);
      break;
    case FV_A_STATS: a = table = tfp_fov_adapter_stats(&lease.helper, handle, &view, &why); break;
    case FV_A_FINISH:
      a = tfp_fov_adapter_finish(&lease.helper, handle, &view, &why);
      table = a && view.has;
      break;
    }
    static char line[16384], second[512], last[512], result_stats[512], status[512];
    char n[13][40], player[32];
    TfpFovResult r;
    tfp_fov_lease_result(&lease, &r);
    tfp_fov_lease_status(&lease, status, sizeof status);
    if (table) stats_text(&view, second, sizeof second);
    else snprintf(second, sizeof second, "%s", why ? why : "~");
    if (lease.bound_player) snprintf(player, sizeof player, "p%llu", (unsigned long long)lease.bound_player);
    else snprintf(player, sizeof player, "~");
    const TfpFovAdapter *h = &lease.helper;
    snprintf(line, sizeof line, "%s|%s|%s|%d|%llu|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%d|%s|%d|%s|%s|%s|%s;%s;%s;%s;%s;%s;%s;%s;%s;%s|%s|%s",
             a ? "T" : "F", second, phases[lease.phase], lease.handle ? (lease.handle->disarmed ? 2 : 1) : 0,
             (unsigned long long)lease.bound, opt(lease.has_bound_request, (double)lease.bound_request, n[0], 40),
             opt(lease.has_bound_weapon, (double)lease.bound_weapon, n[1], 40), player, lease.closing ? "T" : "F",
             lease.has_fault ? lease.fault : "~", opt(lease.has_failure_native_error, lease.failure_native_error, n[2], 40),
             kinds[lease.failure_kind], opt(lease.has_presentation, lease.presentation_epoch, n[3], 40),
             opt(lease.has_presentation, lease.presentation_progress, n[4], 40), stats_text(&lease.last, last, sizeof last),
             h->owned != NULL, opt(h->has_last_failure_code, h->last_failure_code, n[5], 40), h->has_cache,
             opt(h->has_at, h->stats_at, n[6], 40), opt(h->has_cache, h->stats_due, n[7], 40),
             tfp_fov_lease_is_clean(&lease) ? "T" : "F", phases[r.phase], r.cleanup_pending ? "T" : "F",
             r.error_message ? r.error_message : "~", opt(r.has_failure_native_error, r.failure_native_error, n[8], 40),
             kinds[r.failure_kind], opt(r.has_presentation, r.presentation_epoch, n[9], 40),
             opt(r.has_presentation, r.presentation_progress, n[10], 40), opt(r.has_presentation, r.transition_epoch, n[11], 40),
             opt(r.has_presentation, r.transition_progress, n[12], 40), stats_text(&r.stats, result_stats, sizeof result_stats),
             trace, status);
    if (strcmp(line, w->expect)) {
      fprintf(stderr, "fov adapter vector %u (run %s, call %d)\n  C      %s\n  retail %s\n", i, fov_adapter_runs[run].name, w->call,
              line, w->expect);
      return 1;
    }
    begins += strstr(trace, "B(") != NULL;
    ends += strstr(trace, "E(") != NULL;
  }
  printf("FOV adapter: %u operations in %u runs answered as retail's fov.lua (%s): same results, texts, state and helper/backend "
         "calls; %u with Begin, %u with End\n", count, runs, FOV_ADAPTER_VECTOR_COMMIT, begins, ends);
  return 0;
}
