/* Replays tests/vectors/dof_vectors.h (the retail dof.lua's own answers) against src/dof.c.
 * The fakes are those of tools/retail_ref/run_dof.lua. */
#include "dof.h"
#include "effects_fx.h"
#include "vectors/dof_vectors.h"

static TfpDofHost host;
static TfpDof lease;
static TfpDofCompat compats[3];        /* camera_compat 1 and 2, and 3: a value that is not a table */
static bool no_controller;             /* this operation passes no controller */

static bool f_is_enabled(void *u);
static bool f_foreign_camera(void *u, TfpDofForeign *out, const char **error);
static bool f_compat_foreign(void *u, TfpDofForeign *out, const char **error);
static TfpDofValue f_renderer_dof_value(void *u, const TfpDofBinding *b, const char *key);

/* Fields retail reads at call time follow the world. */
static void sync(void) {
  host.backend_foreign_camera = fx_truthy(fx_world[W_fc_has]) ? f_foreign_camera : NULL;
  host.backend_renderer_dof_value = fx_truthy(fx_world[W_rdv_has]) ? f_renderer_dof_value : NULL;
  host.backend_profile_pose = fx_is(W_profile, 't');
  host.controller_is_enabled = fx_truthy(fx_world[W_ctl_has]) && !no_controller ? f_is_enabled : NULL;
  compats[0].foreign = fx_truthy(fx_world[W_c1_has]) ? f_compat_foreign : NULL;
  compats[1].foreign = fx_truthy(fx_world[W_c2_has]) ? f_compat_foreign : NULL;
}
static void tick(void) { fx_tick(); sync(); }

static int value_slot(const char *key) {
  if (!strcmp(key, "dof_enabled")) return W_v_dof;
  if (!strcmp(key, "vignette_enabled")) return W_v_vig;
  if (!strcmp(key, "other_enabled")) return W_v_other;
  fprintf(stderr, "unknown setting %s\n", key);
  exit(2);
}
static TfpDofValue value_of(int slot) { return fx_is(slot, 't') ? TFP_DOF_VALUE_TRUE : fx_is(slot, 'f') ? TFP_DOF_VALUE_FALSE : TFP_DOF_VALUE_OTHER; }

static bool f_clock(void *u, double *out, const char **error) {
  (void)u;
  tick(); fx_note("clock()");
  if ((*error = fx_raise(W_x_clock))) return false;
  *out = fx_is(W_time, 'd') ? fx_n(W_time) : NAN;
  return true;
}
static void f_log(void *u, const char *text) { (void)u; fx_log(text); }
static bool f_binding(void *u, TfpDofBinding *out) {
  (void)u;
  tick(); fx_note("application_binding()");
  if (!fx_is(W_app, 'd') || !fx_is(W_get, 'd') || !fx_is(W_set, 'd')) return false;
  out->app = (uint64_t)fx_n(W_app); out->get = (uint64_t)fx_n(W_get); out->set = (uint64_t)fx_n(W_set);
  return true;
}
static bool f_render_setting(void *u, const TfpDofBinding *b, const char *key, TfpDofValue *out, const char **error) {
  (void)u;
  tick(); fx_note("render_setting(%d %s)", (int)b->get, key);
  if ((*error = fx_raise(W_x_render_setting))) return false;
  *out = value_of(value_slot(key));
  return true;
}
static TfpDofSet f_set_render_setting(void *u, const TfpDofBinding *b, const char *key, bool value, const char **error) {
  int effect;
  (void)u;
  tick(); fx_note("set_render_setting(%d %s %s)", (int)b->set, key, value ? "true" : "false");
  if ((*error = fx_raise(W_x_set_render_setting))) return TFP_DOF_SET_RAISED;
  effect = fx_is(W_set_effect, 'd') ? (int)fx_n(W_set_effect) : 0;
  if (effect != 1) fx_world[value_slot(key)] = (FxValue){value ? 't' : 'f', 0, NULL, 0};
  if (effect == 2) { *error = "setter after queue"; return TFP_DOF_SET_RAISED; }
  if (effect == 3) { *error = NULL; return TFP_DOF_SET_RAISED; }
  return fx_is(W_set_ret, 'f') ? TFP_DOF_SET_REFUSED : TFP_DOF_SET_DONE;
}
static TfpDofValue f_renderer_dof_value(void *u, const TfpDofBinding *b, const char *key) {
  int mode;
  (void)u;
  tick(); fx_note("renderer_dof_value(%d %d %s)", (int)b->get, (int)b->set, key);
  if (fx_raise(W_x_renderer_dof_value)) return TFP_DOF_VALUE_OTHER;
  mode = fx_is(W_rdv_mode, 'd') ? (int)fx_n(W_rdv_mode) : 0;
  if (mode == 1) return TFP_DOF_VALUE_TRUE;
  if (mode == 2) return TFP_DOF_VALUE_FALSE;
  if (mode == 3) return TFP_DOF_VALUE_OTHER;
  return value_of(value_slot(key));
}
/* tostring() of a scripted first result, and the second result when it is a text. */
static void foreign_answer(int v, int why, TfpDofForeign *out) {
  static char number[4][32];
  static unsigned next;
  FxValue value = fx_world[v];
  out->is_false = value.t == 'f';
  switch (value.t) {
    case 'f': out->value_text = "false"; break;
    case 't': out->value_text = "true"; break;
    case 'd': out->value_text = number[next % 4]; snprintf(number[next++ % 4], sizeof number[0], "%.14g", value.n); break;
    case 's': out->value_text = value.s; break;
    default: out->value_text = NULL;
  }
  out->why = fx_s(why);
}
static bool f_foreign_camera(void *u, TfpDofForeign *out, const char **error) {
  (void)u;
  tick(); fx_note("foreign_camera()");
  if ((*error = fx_raise(W_x_foreign_camera))) return false;
  foreign_answer(W_fc_v, W_fc_why, out);
  return true;
}
static bool f_compat_foreign(void *u, TfpDofForeign *out, const char **error) {
  int i = (int)((TfpDofCompat *)u - compats);
  tick(); fx_note("compat_foreign(%d)", i + 1);
  if ((*error = fx_raise(W_x_compat_foreign))) return false;
  foreign_answer(i ? W_c2_v : W_c1_v, i ? W_c2_why : W_c1_why, out);
  return true;
}
static bool f_alignment_executable(void *u, uint64_t *exe, const char **error) {
  (void)u;
  tick(); fx_note("alignment_executable(pose)");
  if ((*error = fx_raise(W_x_alignment_executable))) return false;
  *exe = (uint64_t)fx_n(W_exe);
  return true;
}
static bool f_read(void *u, uint64_t address, uint32_t size, uint8_t *out, const char **error) {
  (void)u;
  tick(); fx_note("read(%s %u)", fx_num((double)address), size);
  if ((*error = fx_raise(W_x_read))) return false;
  if (!fx_read((double)address, (int)size, out)) { *error = "Memory unavailable"; return false; }
  return true;
}
static bool truth(const char *name, int raise_slot, int slot, bool *is_true, const char **error) {
  tick(); fx_note("%s", name);
  if ((*error = fx_raise(raise_slot))) return false;
  *is_true = fx_is(slot, 't');
  return true;
}
static bool f_verify(void *u, uint64_t app, bool *is_true, const char **error) {
  char name[32];
  (void)u;
  snprintf(name, sizeof name, "verify(%d)", (int)app);
  return truth(name, W_x_verify, W_verify_ok, is_true, error);
}
static bool f_active_view(void *u, bool *is_true, const char **error) { (void)u; return truth("active_view()", W_x_active_view, W_view, is_true, error); }
static bool f_fallback_ready(void *u, bool *is_true, const char **error) {
  (void)u;
  return truth("fallback_ready()", W_x_fallback_ready, W_fallback, is_true, error);
}
static bool f_is_enabled(void *u) { (void)u; tick(); fx_note("is_enabled()"); return fx_truthy(fx_world[W_c_enabled]); }
static void f_result(void *u, TfpDofControllerState *out) {
  (void)u;
  tick(); fx_note("result()");
  out->pose_offset = fx_truthy(fx_world[W_c_pose]); out->running = fx_truthy(fx_world[W_c_running]);
}
static TfpDofAnswer tri(int slot) { return fx_is(slot, 't') ? TFP_DOF_TRUE : fx_is(slot, 'f') ? TFP_DOF_FALSE : TFP_DOF_NIL; }
static const char *compat_name(const TfpDofCompat *c) {
  static const char *const names[] = {"1", "2", "X"};
  return c ? names[c - compats] : "~";
}
static TfpDofAnswer vignette_answer(int raise_slot, const char **why) {
  if ((*why = fx_raise(raise_slot))) return TFP_DOF_RAISED;
  *why = fx_s(W_vg_why);
  return tri(W_vg_ret);
}
static TfpDofAnswer f_vignette_update(void *u, const TfpDofCompat *c, const char **why) {
  (void)u;
  tick(); fx_note("vignette_update(%s)", compat_name(c));
  return vignette_answer(W_x_vignette_update, why);
}
static TfpDofAnswer f_vignette_stop(void *u, const char **why) { (void)u; tick(); fx_note("vignette_stop()"); return vignette_answer(W_x_vignette_stop, why); }
static TfpDofAnswer f_vignette_shutdown(void *u, const char **why) {
  (void)u;
  tick(); fx_note("vignette_shutdown()");
  return vignette_answer(W_x_vignette_shutdown, why);
}
static bool f_vignette_is_clean(void *u) { (void)u; tick(); fx_note("vignette_is_clean()"); return fx_is(W_vg_clean, 't'); }
static void f_vignette_result(void *u, TfpDofVignetteResult *out) {
  (void)u;
  tick(); fx_note("vignette_result()");
  out->owned = tri(W_vg_owned); out->cleanup_pending = tri(W_vg_cp); out->command_pending = tri(W_vg_cmd);
}
static const char *f_vignette_status(void *u) { (void)u; tick(); fx_note("vignette_status()"); return fx_s(W_vg_status) ? fx_s(W_vg_status) : "v"; }

static void fresh(void) {
  memset(&host, 0, sizeof host);
  memset(&lease, 0, sizeof lease);
  for (int i = 0; i < 3; i++) compats[i] = (TfpDofCompat){&compats[i], NULL};
  no_controller = false;
}
/* new: a[0] construction, a[1] flags (1 active_view, 2 fallback_ready, 4 verify, 8 no log), a[2] deps.setting. */
static void build(const FxOp *op) {
  int flags = op->a[1].t == 'd' ? (int)op->a[1].n : 0;
  fresh();
  host.clock = f_clock; host.log = flags & 8 ? NULL : f_log; host.application_binding = f_binding;
  host.application_render_setting = f_render_setting; host.application_set_render_setting = f_set_render_setting;
  host.backend_alignment_executable = f_alignment_executable; host.backend_read = f_read;
  host.verify = flags & 4 ? f_verify : NULL; host.active_view = flags & 1 ? f_active_view : NULL;
  host.fallback_ready = flags & 2 ? f_fallback_ready : NULL; host.controller_result = f_result;
  host.vignette_update = f_vignette_update; host.vignette_stop = f_vignette_stop; host.vignette_shutdown = f_vignette_shutdown;
  host.vignette_is_clean = f_vignette_is_clean; host.vignette_result = f_vignette_result; host.vignette_status = f_vignette_status;
  sync();
  tfp_dof_init(&lease, &host, op->a[2].t == 's' ? op->a[2].s : NULL,
               op->a[0].n == 0 ? TFP_DOF_VIGNETTE_NONE : op->a[0].n == 1 ? TFP_DOF_VIGNETTE_MODULE : TFP_DOF_VIGNETTE_SETTING);
}
static const char *show(TfpDofAnswer a) { return a == TFP_DOF_TRUE ? "T" : a == TFP_DOF_FALSE ? "F" : "~"; }
static const char *when(bool has, double v) { return has ? fx_num(v) : "~"; }
static void put_binding(bool has, const TfpDofBinding *b) {
  if (has) fx_put("%d.%d.%d", (int)b->app, (int)b->get, (int)b->set);
  else fx_put("~");
}
static void put_setting(const TfpDofSetting *s) {
  const TfpDofRecord *r;
  if (!s) { fx_put("-"); return; }
  r = &s->record;
  fx_put("%s %s %s %s %s %s %s %s %u %u %u %u %u %u %u %s ", tfp_dof_phase_name(s->phase), s->has_message ? fx_str(s->message) : "~", fx_bool(s->closing),
         fx_bool(s->session), fx_bool(s->preserved), fx_bool(s->yielded), fx_num(s->next_attempt), when(s->has_last_time, s->last_time), s->reads, s->writes,
         s->restores, s->foreign_changes, s->cached_reads, s->cache_fallbacks, s->reassertions, compat_name(s->compat));
  put_binding(s->has_verified, &s->verified);
  fx_put(" ");
  if (!s->has_record) { fx_put("~"); return; }
  put_binding(true, &r->binding);
  fx_put(" %s %s %s %s %s %s", fx_bool(r->before), fx_bool(r->cancel), fx_num(r->sent), r->stage ? tfp_dof_stage_name(r->stage) : "~",
         when(r->has_next_poll, r->next_poll), r->has_issue ? fx_str(r->issue) : "~");
}
static void put_result(const TfpDofSettingResult *r, const char *owned, const char *cleanup, const char *command) {
  fx_put("%s %s %s %s %s %u %u %u %u %u %u %u %s %s", tfp_dof_phase_name(r->phase), owned, cleanup, command, fx_bool(r->disabled), r->reads, r->writes,
         r->restores, r->foreign_changes, r->cached_reads, r->cache_fallbacks, r->reassertions, fx_str(r->error_message), fx_bool(r->closing));
}
static void dispatch(const FxOp *op) {
  const char *why = "(unset)", *call = op->call, *status;
  TfpDofResult r;
  bool clean, suppressed;
  no_controller = false;
  if (!strcmp(call, "new")) { build(op); fx_put("new"); }
  else {
    TfpDofAnswer got;
    if (!strcmp(call, "update")) {
      int c = op->a[0].t == 'd' ? (int)op->a[0].n : 0;
      no_controller = fx_truthy(op->a[1]);
      sync();
      got = tfp_dof_update(&lease, c >= 1 && c <= 3 ? &compats[c - 1] : NULL, &why);
    } else {
      sync();
      if (!strcmp(call, "stop")) got = tfp_dof_stop(&lease, &why);
      else if (!strcmp(call, "shutdown")) got = tfp_dof_shutdown(&lease, &why);
      else { fprintf(stderr, "unknown call %s\n", call); exit(2); }
    }
    if (got == TFP_DOF_RAISED) fx_put("raised %s", fx_str(why));
    else fx_put("%s %s", show(got), fx_str(why));
  }
  fx_put(" | ");
  put_setting(&lease.dof);
  fx_put(" | ");
  put_setting(lease.kind == TFP_DOF_VIGNETTE_SETTING ? &lease.vignette : NULL);
  fx_put(" | ");
  tfp_dof_result(&lease, &r);
  put_result(&r.dof, show(r.owned), show(r.cleanup_pending), show(r.command_pending));
  if (r.kind == TFP_DOF_VIGNETTE_MODULE) fx_put(" V %s %s %s", show(r.vignette.owned), show(r.vignette.cleanup_pending), show(r.vignette.command_pending));
  else if (r.kind == TFP_DOF_VIGNETTE_SETTING) {
    fx_put(" V ");
    put_result(&r.vignette_setting, fx_bool(r.vignette_setting.owned), fx_bool(r.vignette_setting.cleanup_pending), fx_bool(r.vignette_setting.command_pending));
  }
  status = tfp_dof_status(&lease);
  clean = tfp_dof_is_clean(&lease);
  suppressed = tfp_dof_is_suppressed(&lease);
  fx_put(" | %s | %s | %s", fx_str(status), fx_bool(clean), fx_bool(suppressed));
}
int main(void) {
  fx_mem = W_mem; fx_any = W_x_any;
  return fx_main("dof", DOF_VECTOR_COMMIT, dof_runs, (int)(sizeof dof_runs / sizeof dof_runs[0]), dof_ops, dof_events, dof_slots, W_COUNT, fresh, dispatch);
}
