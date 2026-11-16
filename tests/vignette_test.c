/* Replays tests/vectors/vignette_vectors.h (the retail vignette.lua's own answers) against
 * src/vignette.c. The fakes are those of tools/retail_ref/run_vignette.lua; its header describes the world. */
#include "vignette.h"
#include "effects_fx.h"
#include "vectors/vignette_vectors.h"

static TfpVignetteHost host;
static TfpVignette vignette;
static bool no_stingray, with_compat;

static bool f_compat_foreign(void *u);
static bool f_foreign_camera(void *u);
static bool f_active_view(void *u);
/* Fields the module reads at call time follow the world, also in the middle of a call. */
static void sync(void) {
  host.backend_foreign_camera = fx_truthy(fx_world[W_has_fc]) ? f_foreign_camera : NULL;
  host.compat_foreign = with_compat && fx_truthy(fx_world[W_has_cf]) ? f_compat_foreign : NULL;
  host.active_view = fx_truthy(fx_world[W_has_av]) ? f_active_view : NULL;
}
static void tick(void) { fx_tick(); sync(); }
static const char *tok(uint64_t binding) {
  static char out[4][32];
  static unsigned next;
  char *text = out[next++ % 4];
  snprintf(text, sizeof out[0], "@%s", fx_num((double)binding));
  return text;
}
static const char *h(uint64_t handle) { return handle ? fx_num((double)handle) : "~"; }
static uint64_t handle_of(int slot) { return fx_is(slot, 'd') ? (uint64_t)fx_n(slot) : 0; }

static int instance_count(uint64_t e) {
  FxValue n = fx_getk(W_ninst, (double)e);
  return n.t == 'd' ? (int)n.n : 0;
}
/* An id that is not a number is NaN here. */
static double id_at(uint64_t e, int i) {
  FxValue v = fx_getk(W_inst, (double)e * 256 + i);
  return v.t == 'n' ? 7 : v.t == 'd' ? v.n : NAN;
}
static double find(uint64_t e, double id) {
  for (int i = 1, n = instance_count(e); i <= n; i++) if (id_at(e, i) == id) return (double)e * 256 + i;
  return (double)e * 256;
}

static bool f_clock(void *u, double *now, const char **error) {
  (void)u;
  tick(); fx_note("clock()");
  if ((*error = fx_raise(W_x_clock))) return false;
  *now = fx_is(W_now, 'd') ? fx_n(W_now) : NAN;
  return true;
}
static bool f_compat_foreign(void *u) {
  (void)u;
  tick(); fx_note("compat_foreign(T)");
  return fx_raise(W_x_compat_foreign) || !fx_is(W_cf, 'f');
}
static bool f_foreign_camera(void *u) {
  (void)u;
  tick(); fx_note("foreign_camera(0)");
  return fx_raise(W_x_foreign_camera) || !fx_is(W_fc, 'f');
}
static bool f_active_view(void *u) {
  (void)u;
  tick(); fx_note("active_view(0)");
  return !fx_raise(W_x_active_view) && fx_is(W_av, 't');
}
static bool f_player_snapshot(void *u, TfpVignetteSnapshot *out, const char **error) {
  (void)u;
  tick(); fx_note("player_snapshot(T)");
  if ((*error = fx_raise(W_x_player_snapshot))) return false;
  out->present = fx_is(W_p_ok, 't');
  out->safe = fx_truthy(fx_world[W_p_safe]);
  out->camera_kind = fx_is(W_p_kind, 'd') ? (int)fx_n(W_p_kind) : 0;
  return true;
}
static void f_result(void *u, TfpVignetteControllerState *out) { (void)u; tick(); fx_note("result()"); out->pose_offset = fx_truthy(fx_world[W_c_pose]); }
static bool f_is_enabled(void *u) { (void)u; tick(); fx_note("is_enabled()"); return fx_truthy(fx_world[W_c_enabled]); }

static void f_api(void *u, TfpVignetteApi *out) {
  static const int slots[TFP_VIGNETTE_FN_COUNT] = {W_b_world, W_b_entities, W_b_manager, W_b_alive, W_b_instances, W_b_get, W_b_set};
  (void)u;
  out->available = !no_stingray && !(fx_is(W_no_api, 'd') && fx_n(W_no_api) >= 1 && fx_n(W_no_api) <= 4);
  for (int i = 0; i < TFP_VIGNETTE_FN_COUNT; i++) {
    FxValue v = fx_world[slots[i]];
    out->fn[i].kind = v.t == 'n' ? TFP_VIGNETTE_BINDING_NIL : v.t == 'd' ? TFP_VIGNETTE_BINDING_FUNCTION : TFP_VIGNETTE_BINDING_NOT_A_FUNCTION;
    out->fn[i].token = v.t == 'd' ? (uint64_t)v.n : 0;
  }
}
static bool f_main_world(void *u, uint64_t binding, uint64_t *world, const char **error) {
  (void)u;
  tick(); fx_note("main_world(%s)", tok(binding));
  if ((*error = fx_raise(W_x_main_world))) return false;
  *world = handle_of(W_world);
  return true;
}
static bool f_entities(void *u, uint64_t binding, uint64_t world, uint64_t *entities, uint32_t capacity, TfpVignetteList *kind, uint32_t *count,
                       const char **error) {
  int mode, n;
  (void)u;
  tick(); fx_note("entities(%s %s)", tok(binding), h(world));
  if ((*error = fx_raise(W_x_entities))) return false;
  mode = fx_is(W_ent_mode, 'd') ? (int)fx_n(W_ent_mode) : 0;
  *count = 0;
  if (mode == 1 || mode == 2) { *kind = TFP_VIGNETTE_LIST_NIL; return true; }
  if (mode == 3) { *kind = TFP_VIGNETTE_LIST_NOT_A_TABLE; return true; }
  *kind = TFP_VIGNETTE_LIST_TABLE;
  n = fx_is(W_nent, 'd') ? (int)fx_n(W_nent) : 0;
  *count = (uint32_t)n;
  for (int i = 1; i <= n && (uint32_t)i <= capacity; i++) {
    FxValue v = fx_getk(W_ent, i);
    entities[i - 1] = v.t == 'n' ? (uint64_t)(100000 + i) : (uint64_t)v.n;
  }
  return true;
}
static bool f_manager(void *u, uint64_t binding, uint64_t world, uint64_t *manager, const char **error) {
  (void)u;
  tick(); fx_note("manager(%s %s)", tok(binding), h(world));
  if ((*error = fx_raise(W_x_manager))) return false;
  *manager = handle_of(W_mgr);
  return true;
}
static bool f_alive(void *u, uint64_t binding, uint64_t entity, bool *alive, const char **error) {
  (void)u;
  tick(); fx_note("alive(%s %s)", tok(binding), h(entity));
  if ((*error = fx_raise(W_x_alive))) return false;
  *alive = !fx_truthy(fx_getk(W_dead, (double)entity));
  return true;
}
static bool f_instances(void *u, uint64_t binding, uint64_t manager, uint64_t entity, double *ids, uint32_t capacity, uint32_t *count,
                        const char **error) {
  int n;
  (void)u;
  tick(); fx_note("instances(%s %s %s)", tok(binding), h(manager), h(entity));
  if ((*error = fx_raise(W_x_instances))) return false;
  n = instance_count(entity);
  *count = (uint32_t)n;
  for (int i = 1; i <= n && (uint32_t)i <= capacity; i++) ids[i - 1] = id_at(entity, i);
  return true;
}
static bool f_get(void *u, uint64_t binding, uint64_t manager, uint64_t entity, double id, const char *property, TfpVignetteValue *out,
                  const char **error) {
  bool mapping = strcmp(property, "shading_environment_mapping") == 0;
  FxValue v;
  (void)u;
  tick(); fx_note("get(%s %s %s %s %s)", tok(binding), h(manager), h(entity), fx_num(id), property);
  if ((*error = fx_raise(W_x_get))) return false;
  v = fx_getk(mapping ? W_map : W_en, find(entity, id));
  memset(out, 0, sizeof *out);
  if (v.t == 'n') out->kind = TFP_VIGNETTE_VALUE_NIL;
  else if (v.t == 't' && mapping) { out->kind = TFP_VIGNETTE_VALUE_TEXT; out->text = TFP_VIGNETTE_MAPPING; out->text_len = strlen(TFP_VIGNETTE_MAPPING); }
  else if (v.t == 't' || v.t == 'f') { out->kind = TFP_VIGNETTE_VALUE_BOOLEAN; out->boolean = v.t == 't'; }
  else if (v.t == 's') { out->kind = TFP_VIGNETTE_VALUE_TEXT; out->text = v.s; out->text_len = (size_t)v.len; }
  else out->kind = TFP_VIGNETTE_VALUE_OTHER;
  return true;
}
static bool f_set(void *u, uint64_t binding, uint64_t manager, uint64_t entity, double id, const char *property, bool value, bool *refused,
                  const char **error) {
  int mode;
  (void)u;
  tick(); fx_note("set(%s %s %s %s %s %s)", tok(binding), h(manager), h(entity), fx_num(id), property, fx_bool(value));
  if ((*error = fx_raise(W_x_set))) return false;
  mode = fx_is(W_set_mode, 'd') ? (int)fx_n(W_set_mode) : 0;
  if (mode != 1 && mode != 3) fx_poke(W_en, find(entity, id), (FxValue){value ? 't' : 'f', 0, NULL, 0});
  if (mode == 4) { *error = "setter uncertain"; return false; }
  *refused = mode == 1 || mode == 2;
  return true;
}
static void f_log(void *u, const char *text) { (void)u; fx_log(text); }

static void fresh(void) {
  memset(&host, 0, sizeof host);
  host.log = f_log; host.clock = f_clock; host.backend_player_snapshot = f_player_snapshot; host.controller_result = f_result;
  host.controller_is_enabled = f_is_enabled; host.stingray_api = f_api; host.application_main_world = f_main_world;
  host.world_entities = f_entities; host.entity_manager_render_data_component = f_manager; host.entity_manager_alive = f_alive;
  host.render_data_component_instances = f_instances; host.render_data_component_get_property = f_get;
  host.render_data_component_set_property = f_set;
  no_stingray = with_compat = false;
  tfp_vignette_init(&vignette, &host);
}
static void dispatch(const FxOp *op) {
  const char *why = "(unset)", *call = op->call;
  TfpVignetteResult r;
  char status[TFP_VIGNETTE_TEXT + 96];
  if (!strcmp(call, "new")) { fresh(); no_stingray = fx_truthy(op->a[0]); }
  with_compat = fx_truthy(fx_world[W_has_compat]);
  sync();
  if (!strcmp(call, "new")) fx_put("new");
  else if (!strcmp(call, "peek")) fx_put("peek");
  else if (!strcmp(call, "update")) { bool done = tfp_vignette_update(&vignette, &why); fx_put("%s %s", fx_bool(done), fx_str(why)); }
  else if (!strcmp(call, "stop")) { bool done = tfp_vignette_stop(&vignette, &why); fx_put("%s %s", fx_bool(done), fx_str(why)); }
  else if (!strcmp(call, "shutdown")) { bool done = tfp_vignette_shutdown(&vignette, &why); fx_put("%s %s", fx_bool(done), fx_str(why)); }
  else { fprintf(stderr, "unknown call %s\n", call); exit(2); }
  tfp_vignette_result(&vignette, &r);
  fx_put(" | %s %u %u %u %s |", fx_str(tfp_vignette_phase_name(vignette.phase)), vignette.writes, vignette.restores, vignette.reassertions,
         fx_str(vignette.has_error ? vignette.error_message : NULL));
  fx_put(" %s %s %s %u %u %u %u %s %s |", fx_str(tfp_vignette_phase_name(r.phase)), fx_bool(r.disabled), fx_bool(r.owned), r.profiles, r.writes,
         r.restores, r.reassertions, fx_str(r.error_message), fx_bool(r.cleanup_pending));
  fx_put(" %s |", fx_str(tfp_vignette_status(&vignette, status, sizeof status)));
  fx_put(" %s %s %s %s |", fx_bool(tfp_vignette_is_clean(&vignette)), fx_bool(vignette.closing), fx_num(vignette.next_scan),
         fx_num(vignette.next_poll));
  if (vignette.has_context) {
    fx_put(" %s %s", h(vignette.world), h(vignette.manager));
    for (int i = 0; i < TFP_VIGNETTE_FN_COUNT; i++) fx_put(" %s", tok(vignette.api.fn[i].token));
  } else fx_put(" ~");
  fx_put(" |");
  if (vignette.has_scan) fx_put(" %u/%u", vignette.scan_index, vignette.scan_count);
  else fx_put(" ~");
  fx_put(" |");
  for (uint32_t i = 0; i < vignette.row_count; i++) {
    const TfpVignetteRow *row = &vignette.rows[i];
    fx_put(" [%s %s %s %s]", h(row->entity), fx_num(row->id), fx_bool(row->owned), row->before ? "T" : "~");
  }
}
int main(void) {
  fx_any = W_x_any;
  return fx_main("vignette", VIGNETTE_VECTOR_COMMIT, vignette_runs, (int)(sizeof vignette_runs / sizeof vignette_runs[0]), vignette_ops,
                 vignette_events, vignette_slots, W_COUNT, fresh, dispatch);
}
