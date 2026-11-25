/* See wire_effects.h. Signature adapters between the runtime host and the five effect units, and
 * the units' hosts. Line references "rt:N" are retail runtime.lua at 0.5.41-smooth.66. No Windows,
 * no engine. */
#include "wire_effects.h"
#include "wire_off.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef TfpRtValue V;
#define nil tfp_wire_nil
#define boolean tfp_wire_bool
#define FXH(h) ((TfpFx *)((TfpWire *)(h)->user)->ext[TFP_WIRE_EXT_EFFECTS])
#define P(fx) (&(fx)->platform)

static const char *const module_name[TFP_FX_MODULES] = {"dof", "vignette", "markers", "movement", "aimopt"};

/* Census of build 25480438 (docs/history/research-0.5.34/live-sdk-census-g1-9472b065.tsv; rows 238, 267,
 * 282, 91, 599, 600, 1154, 1152, 1156, 1324, 1323, 1332, 493, 489). tests/wire_effects_test.c
 * compares this table with the file. */
const TfpFxFunctionRow tfp_fx_functions[TFP_FX_FUNCTIONS] = {
  {"Application.render_setting", 0x3fa3d0, TFP_FX_DOF},
  {"Application.set_render_setting", 0x3fa120, TFP_FX_DOF},
  {"Application.main_world", 0x3f8310, TFP_FX_VIGNETTE},
  {"World.entities", 0x464b50, TFP_FX_VIGNETTE},
  {"EntityManager.render_data_component", 0x4638d0, TFP_FX_VIGNETTE},
  {"EntityManager.alive", 0x462470, TFP_FX_VIGNETTE},
  {"RenderDataComponent.instances", 0x467770, TFP_FX_VIGNETTE},
  {"RenderDataComponent.get_property", 0x467410, TFP_FX_VIGNETTE},
  {"RenderDataComponent.set_property", 0x466fd0, TFP_FX_VIGNETTE},
  {"Keyboard.button_index", 0x4830c0, TFP_FX_MOVEMENT},
  {"Keyboard.button_id", 0x4830c0, TFP_FX_MOVEMENT},
  {"Keyboard.button", 0x482480, TFP_FX_MOVEMENT},
  {"Window.mouse_focus", 0x3ff400, TFP_FX_MOVEMENT},
  {"Window.show_cursor", 0x3ffc20, TFP_FX_MOVEMENT},
};
/* Every constant of the four tables was checked against the images of build 25480438
 * (tools/verify_effects_constants.py). The vignette has no build data: its class id and mapping
 * name are names, equal to the donor's (src/effects.c:157, :299). */
const TfpFxConstants tfp_fx_constants[TFP_FX_MODULES] = {
  {TFP_FX_DOF, NULL}, {TFP_FX_VIGNETTE, NULL}, {TFP_FX_MARKERS, NULL}, {TFP_FX_MOVEMENT, NULL}, {TFP_FX_AIM_OPTION, NULL},
};

TfpFx *tfp_wire_effects_of(TfpWire *w) { return w->ext[TFP_WIRE_EXT_EFFECTS]; }
/* Round 28: the time the depth of field and the vignette take, for the Perf line (logging only). The platform's
 * perf_ticks (QueryPerformanceCounter in the mod; NULL in the tests that do not time) is read around each entry. */
static int64_t perf_now(const TfpFx *fx) { return fx->platform.perf_ticks ? fx->platform.perf_ticks(fx->platform.user) : 0; }
void tfp_wire_effects_take_spent(TfpWire *w, int64_t *dof, int64_t *vignette) {
  TfpFx *fx = tfp_wire_effects_of(w);
  *dof = *vignette = 0;
  if (!fx) return;
  *vignette = fx->spent.vignette, *dof = fx->spent.dof - fx->spent.vignette;
  if (*dof < 0) *dof = 0;
  fx->spent.dof = fx->spent.vignette = 0;
}

/* ---- texts and the runtime's entries ------------------------------------------------------- */
/* A text for a unit: every unit copies an error text before its next host call. */
static const char *hold(TfpFx *fx, const char *text) {
  char *at = fx->text[fx->text_next++ % 4];
  snprintf(at, sizeof fx->text[0], "%s", text);
  return at;
}
static const char *ops_why(TfpFx *fx, const TfpOpsText *t) { return t->present ? hold(fx, t->text) : NULL; }
static V kept(TfpFx *fx, const char *text) { return text ? tfp_wire_text(tfp_wire_keep(fx->w, text)) : nil(); }
static bool truthy(V v) { return v.kind != TFP_RT_NIL && v.kind != TFP_RT_FALSE; }
/* The value a raised entry left in the runtime: its text, or NULL for nil. */
static const char *raised_text(TfpFx *fx) {
  V e = fx->w->rt.error;
  char text[320];
  if (e.kind == TFP_RT_NIL) return NULL;
  if (e.kind != TFP_RT_TEXT) return hold(fx, "error");
  snprintf(text, sizeof text, "%.*s", (int)(e.length < 300 ? e.length : 300), e.text);
  return hold(fx, text);
}
static TfpRtStatus entry(TfpFx *fx, TfpRtEntry which, const TfpRtArgs *args, V *r, int count) {
  for (int i = 0; i < count; i++) r[i] = nil();
  return tfp_rt_call(&fx->w->rt, which, args, r);
}
static void say(TfpFx *fx, const char *text) { tfp_wire_say(fx->w, text); }
static void unit_log(void *u, const char *text) { say(u, text); }

/* deps.clock (rt:707): the runtime's monotonic clock. A value that is not a number: NaN. */
static bool unit_clock(void *u, double *out, const char **error) {
  TfpFx *fx = u;
  V r[1];
  TfpRtStatus s = entry(fx, TFP_RT_E_clock, NULL, r, 1);
  if (s == TFP_RT_RAISED) { *error = raised_text(fx); return false; }
  *out = s == TFP_RT_OK && r[0].kind == TFP_RT_NUMBER ? r[0].number : NAN;
  return true;
}
/* backend.foreign_camera() (rt:718). false: it raised. A function that does not exist yet answers
 * false (not held), as wire_rt.c's hook does. */
static bool foreign_value(TfpFx *fx, V *out, const char **error) {
  V r[1];
  TfpRtStatus s = entry(fx, TFP_RT_E_foreign_camera, NULL, r, 1);
  if (s == TFP_RT_RAISED) { if (error) *error = raised_text(fx); return false; }
  *out = s == TFP_RT_ABSENT ? boolean(false) : r[0];
  return true;
}
/* `not ok or v~=false` */
static bool foreign_held(void *u) {
  V v;
  return !foreign_value(u, &v, NULL) || v.kind != TFP_RT_FALSE;
}
/* compat:foreign() through the runtime host (the off answer on HellForge: false). */
static bool compat_value(TfpFx *fx, V *out, const char **error) {
  TfpRtHost *h = &fx->w->host;
  *out = nil();
  if (h->compat_foreign(h, out)) return true;
  if (error) *error = h->raised ? hold(fx, h->raised) : NULL;
  return false;
}
static bool entry_true(TfpFx *fx, TfpRtEntry which, bool *is_true, const char **error) {
  V r[1];
  TfpRtStatus s = entry(fx, which, NULL, r, 1);
  *is_true = false;
  if (s == TFP_RT_RAISED) { if (error) *error = raised_text(fx); return false; }
  *is_true = s == TFP_RT_OK && r[0].kind == TFP_RT_TRUE;
  return true;
}
static bool controller_enabled(void *u) { return ((TfpFx *)u)->w->controller.enabled; }
static bool controller_pose_offset(void *u) { return tfp_wire_pose_offset(((TfpFx *)u)->w); }
static bool controller_running(const TfpFx *fx) { return fx->w->controller.phase == TFP_PHASE_RUNNING; }

/* ---- the backend's readers (backend:_read, _ptr, _u32, _u8, _map) --------------------------- */
static bool mem_read(void *u, uint64_t address, uint32_t size, uint8_t *out, const char **error) {
  TfpFx *fx = u;
  TfpOpsText why;
  if (tfp_ops_read(&fx->w->ops, address, out, size, &why)) return true;
  *error = ops_why(fx, &why);
  return false;
}
static bool mem_ptr(void *u, uint64_t address, uint64_t *out, const char **error) {
  TfpFx *fx = u;
  TfpOpsText why;
  if (tfp_ops_ptr(&fx->w->ops, address, out, &why)) return true;
  *error = ops_why(fx, &why);
  return false;
}
static bool mem_u32(void *u, uint64_t address, uint32_t *out, const char **error) {
  TfpFx *fx = u;
  TfpOpsText why;
  if (tfp_ops_u32(&fx->w->ops, address, out, &why)) return true;
  *error = ops_why(fx, &why);
  return false;
}
static bool mem_u8(void *u, uint64_t address, uint8_t *out, const char **error) {
  TfpFx *fx = u;
  TfpOpsText why;
  if (tfp_ops_u8(&fx->w->ops, address, out, &why)) return true;
  *error = ops_why(fx, &why);
  return false;
}
static bool mem_map(void *u, uint64_t address, uint32_t id, bool has_limit, uint32_t limit, uint32_t *index, const char **error) {
  TfpFx *fx = u;
  TfpOpsText why;
  if (tfp_ops_map(&fx->w->ops, address, id, limit, has_limit, index, &why)) return true;
  *error = ops_why(fx, &why);
  return false;
}
typedef TfpOpsResult (*SnapshotFn)(TfpOps *, TfpOpsSnapshot *, TfpOpsText *);
/* TRUE: *out; NIL / FALSE: nil with *why; RAISED: *why is the error. */
static TfpOpsResult take(TfpFx *fx, SnapshotFn which, TfpOpsSnapshot *out, const char **why) {
  TfpOpsText text;
  text.present = false;
  TfpOpsResult got = which(&fx->w->ops, out, &text);
  if (why) *why = got == TFP_OPS_TRUE ? NULL : ops_why(fx, &text);
  return got;
}

/* ---- the engine: is it there, and are its functions the census's ---------------------------- */
static bool engine_live(TfpFx *fx) {
  const char *why = NULL;
  return P(fx)->engine_ready && P(fx)->engine_ready(P(fx)->user, &why);
}
static uint64_t generation(TfpFx *fx) { return P(fx)->engine_generation ? P(fx)->engine_generation(P(fx)->user) : 0; }
/* The identity of a stingray table: another one after the engine replaced its script state. */
static uint64_t table_token(TfpFx *fx, const char *name) {
  if (!engine_live(fx) || !P(fx)->engine_table(P(fx)->user, name)) return 0;
  return generation(fx) * 2 + 1;
}
static uint64_t function_token(TfpFx *fx, TfpFxFunction which) {
  return engine_live(fx) ? P(fx)->engine_function(P(fx)->user, tfp_fx_functions[which].path) : 0;
}
/* The function retail retained (`binding`) is still the one registered under its name. */
static bool retained(TfpFx *fx, TfpFxFunction which, uint64_t binding, const char **error) {
  if (binding != 0 && function_token(fx, which) == binding) return true;
  char text[160];
  snprintf(text, sizeof text, "%s is no longer the function that was bound", tfp_fx_functions[which].path);
  *error = hold(fx, text);
  return false;
}

/* ---- depth of field (TfpDofHost) ------------------------------------------------------------ */
static bool dof_binding(void *u, TfpDofBinding *out) {
  TfpFx *fx = u;
  out->app = table_token(fx, "Application");
  out->get = function_token(fx, TFP_FX_FN_RENDER_SETTING);
  out->set = function_token(fx, TFP_FX_FN_SET_RENDER_SETTING);
  return out->app && out->get && out->set;
}
static bool dof_get(void *u, const TfpDofBinding *binding, const char *key, TfpDofValue *out, const char **error) {
  TfpFx *fx = u;
  int value = -1;
  if (!retained(fx, TFP_FX_FN_RENDER_SETTING, binding->get, error)) return false;
  if (!P(fx)->render_setting(P(fx)->user, key, &value, error)) return false;
  *out = value == 1 ? TFP_DOF_VALUE_TRUE : value == 0 ? TFP_DOF_VALUE_FALSE : TFP_DOF_VALUE_OTHER;
  return true;
}
static TfpDofSet dof_set(void *u, const TfpDofBinding *binding, const char *key, bool value, const char **error) {
  TfpFx *fx = u;
  if (!retained(fx, TFP_FX_FN_SET_RENDER_SETTING, binding->set, error)) return TFP_DOF_SET_RAISED;
  switch (P(fx)->set_render_setting(P(fx)->user, key, value, error)) {
    case TFP_FX_SET_DONE: return TFP_DOF_SET_DONE;
    case TFP_FX_SET_REFUSED: return TFP_DOF_SET_REFUSED;
    default: return TFP_DOF_SET_RAISED;
  }
}
/* backend:renderer_dof_value(get, set, key): the cached observer of backend_ops.c. */
static TfpDofValue dof_cached(void *u, const TfpDofBinding *binding, const char *key) {
  TfpFx *fx = u;
  TfpOpsBinding get = {(const void *)(uintptr_t)binding->get, binding->get, true};
  TfpOpsBinding set = {(const void *)(uintptr_t)binding->set, binding->set, true};
  TfpOpsSetting which = !strcmp(key, "dof_enabled") ? TFP_OPS_SETTING_DOF : !strcmp(key, "vignette_enabled") ? TFP_OPS_SETTING_VIGNETTE
                                                                                                           : TFP_OPS_SETTING_OTHER;
  TfpOpsText why;
  switch (tfp_ops_renderer_dof_value(&fx->w->ops, get, set, which, &why)) {
    case TFP_OPS_TRUE: return TFP_DOF_VALUE_TRUE;
    case TFP_OPS_FALSE: return TFP_DOF_VALUE_FALSE;
    default: return TFP_DOF_VALUE_OTHER;
  }
}
/* tostring() of a camera-ownership answer. */
static const char *value_name(TfpFx *fx, V v) {
  char text[64];
  switch (v.kind) {
    case TFP_RT_NIL: return "nil";
    case TFP_RT_FALSE: return "false";
    case TFP_RT_TRUE: return "true";
    case TFP_RT_NUMBER: tfp_ops_number(v.number, text, sizeof text); return hold(fx, text);
    case TFP_RT_TEXT: snprintf(text, sizeof text, "%.*s", (int)(v.length < 60 ? v.length : 60), v.text); return hold(fx, text);
    default: return "table";
  }
}
static bool dof_foreign(void *u, TfpDofForeign *out, const char **error) {
  TfpFx *fx = u;
  V v;
  if (!foreign_value(fx, &v, error)) return false;
  out->is_false = v.kind == TFP_RT_FALSE, out->value_text = value_name(fx, v), out->why = NULL;
  return true;
}
static bool dof_compat_foreign(void *u, TfpDofForeign *out, const char **error) {
  TfpFx *fx = u;
  V v;
  if (!compat_value(fx, &v, error)) return false;
  out->is_false = v.kind == TFP_RT_FALSE, out->value_text = value_name(fx, v), out->why = NULL;
  return true;
}
static bool dof_executable(void *u, uint64_t *exe, const char **error) {
  TfpFx *fx = u;
  TfpOpsText why;
  if (tfp_ops_alignment_executable(&fx->w->ops, TFP_OPS_EXECUTABLE_POSE, exe, &why)) return true;
  *error = ops_why(fx, &why);
  return false;
}
static bool vignette_held(TfpFx *fx);
/* The depth of field goes with the vignette through a weapon swap: the setting is not given back for the third of a
 * second the view is away (tfp_wire_effects_vignette_view below has the rule). */
bool tfp_wire_effects_dof_view(TfpFx *fx, bool active) { return active || vignette_held(fx); }
static bool dof_active_view(void *u, bool *is_true, const char **error) {
  if (!entry_true(u, TFP_RT_E_active_view, is_true, error)) return false;
  *is_true = tfp_wire_effects_dof_view(u, *is_true);
  return true;
}
static bool dof_fallback_ready(void *u, bool *is_true, const char **error) { return entry_true(u, TFP_RT_E_dof_fallback_ready, is_true, error); }
static void dof_controller(void *u, TfpDofControllerState *out) { out->pose_offset = controller_pose_offset(u), out->running = controller_running(u); }

/* The vignette behind the depth of field (rt:708: vignette_factory). vignette.lua answers Booleans
 * and never raises; its result() has no command_pending. While the vignette module is not allowed
 * on it is what an idle vignette is: update true, nothing owned. */
static bool vignette_on(const TfpFx *fx) { return fx->state[TFP_FX_VIGNETTE] == TFP_FX_READY; }
static TfpDofAnswer glue(bool done) { return done ? TFP_DOF_TRUE : TFP_DOF_FALSE; }
static TfpDofAnswer dv_update_work(void *u, const TfpDofCompat *compat, const char **why) {
  TfpFx *fx = u;
  (void)compat; /* the vignette's own host asks the same compat object (v_compat_foreign) */
  *why = NULL;
  if (vignette_on(fx)) return glue(tfp_vignette_update(&fx->vignette, why));
  return tfp_vignette_is_clean(&fx->vignette) ? TFP_DOF_TRUE : glue(tfp_vignette_stop(&fx->vignette, why));
}
/* The view going away is not always its end: a weapon swap takes it away for a third of a second, and giving the
 * vignette back for that showed a dark rim at every swap (seen in the game 2026-11-22). For VIGNETTE_AWAY_S after
 * the view went away the vignette stays off, and a row the game switches on meanwhile is taken again; after that,
 * or at once when first person is switched off, it is given back. Switched on is the controller's enabled flag: it
 * stays true while the controller waits for the camera or is still releasing the old lease, and a stop or a
 * shutdown clears it. A stop or a shutdown of the depth of field gives it back at once. Not clean meanwhile, so
 * the runtime keeps asking. */
#define VIGNETTE_AWAY_S 1.5
static bool first_person_wanted(const TfpFx *fx) { return fx->w->controller.enabled; }
/* True while the vignette is to stay off although the view is away (see above). */
static bool vignette_held(TfpFx *fx) {
  double now = 0; const char *error = NULL;
  if (!vignette_on(fx) || tfp_vignette_is_clean(&fx->vignette) || !first_person_wanted(fx) || !unit_clock(fx, &now, &error) || now != now) {
    fx->vignette_away_since = 0;
    return false;
  }
  if (!(fx->vignette_away_since > 0) || now < fx->vignette_away_since) fx->vignette_away_since = now > 0 ? now : 1e-9;
  if (now - fx->vignette_away_since < VIGNETTE_AWAY_S) return true;
  return false; /* the time is up: given back, and the mark stays until the view is back or it is clean */
}
/* A stop that is asked for stops at once: the hold is only in what the two take for "the view is there"
 * (tfp_wire_effects_vignette_view, tfp_wire_effects_dof_view). A swap never comes here: the runtime updates the
 * depth of field through it and stops it only for a manual stop and for the new-body recovery. */
static TfpDofAnswer dv_stop_work(void *u, const char **why) {
  TfpFx *fx = u;
  *why = NULL;
  fx->vignette_away_since = 0;
  return glue(tfp_vignette_stop(&fx->vignette, why));
}
static TfpDofAnswer dv_shutdown_work(void *u, const char **why) { *why = NULL; ((TfpFx *)u)->vignette_away_since = 0; return glue(tfp_vignette_shutdown(&((TfpFx *)u)->vignette, why)); }
/* the three above, timed (the vignette's share of the depth of field's time) */
#define TIMED_V(call) \
  { TfpFx *fx = u; const int64_t t0 = perf_now(fx); const TfpDofAnswer a = call; fx->spent.vignette += perf_now(fx) - t0; return a; }
static TfpDofAnswer dv_update(void *u, const TfpDofCompat *compat, const char **why) TIMED_V(dv_update_work(u, compat, why))
static TfpDofAnswer dv_stop(void *u, const char **why) TIMED_V(dv_stop_work(u, why))
static TfpDofAnswer dv_shutdown(void *u, const char **why) TIMED_V(dv_shutdown_work(u, why))
#undef TIMED_V
static bool dv_is_clean(void *u) { return tfp_vignette_is_clean(&((TfpFx *)u)->vignette); }
static void dv_result(void *u, TfpDofVignetteResult *out) {
  TfpVignetteResult r;
  tfp_vignette_result(&((TfpFx *)u)->vignette, &r);
  out->owned = glue(r.owned), out->cleanup_pending = glue(r.cleanup_pending), out->command_pending = TFP_DOF_NIL;
}
static const char *dv_status(void *u) {
  TfpFx *fx = u;
  return tfp_vignette_status(&fx->vignette, fx->vignette_status, sizeof fx->vignette_status);
}

/* ---- vignette (TfpVignetteHost) ------------------------------------------------------------- */
static bool v_clock(void *u, double *now, const char **error) { return unit_clock(u, now, error); }
static bool v_compat_foreign(void *u) {
  V v;
  return !compat_value(u, &v, NULL) || v.kind != TFP_RT_FALSE;
}
bool tfp_wire_effects_vignette_view(TfpFx *fx, bool active) {
  if (active) { fx->vignette_away_since = 0; return true; }
  return vignette_held(fx); /* a swap: the vignette's own update sees the view as still there */
}
static bool v_active_view(void *u) {
  bool is_true = false;
  return tfp_wire_effects_vignette_view(u, entry_true(u, TFP_RT_E_active_view, &is_true, NULL) && is_true);
}
static bool v_player_snapshot(void *u, TfpVignetteSnapshot *out, const char **error) {
  TfpFx *fx = u;
  TfpOpsSnapshot s;
  TfpOpsResult got = take(fx, tfp_ops_player_snapshot, &s, error);
  if (got == TFP_OPS_RAISED) return false;
  out->present = got == TFP_OPS_TRUE;
  if (out->present) out->safe = s.safe, out->camera_kind = (int)s.camera_kind;
  return true;
}
static void v_controller(void *u, TfpVignetteControllerState *out) { out->pose_offset = controller_pose_offset(u); }
static const TfpFxFunction vignette_function[TFP_VIGNETTE_FN_COUNT] = {
  TFP_FX_FN_MAIN_WORLD, TFP_FX_FN_ENTITIES, TFP_FX_FN_RENDER_DATA_COMPONENT, TFP_FX_FN_ALIVE, TFP_FX_FN_INSTANCES, TFP_FX_FN_GET_PROPERTY,
  TFP_FX_FN_SET_PROPERTY,
};
static void v_api(void *u, TfpVignetteApi *out) {
  TfpFx *fx = u;
  memset(out, 0, sizeof *out);
  out->available = table_token(fx, "Application") && table_token(fx, "World") && table_token(fx, "EntityManager") &&
                   table_token(fx, "RenderDataComponent");
  if (!out->available) return;
  for (int i = 0; i < TFP_VIGNETTE_FN_COUNT; i++) {
    out->fn[i].token = function_token(fx, vignette_function[i]);
    out->fn[i].kind = out->fn[i].token ? TFP_VIGNETTE_BINDING_FUNCTION : TFP_VIGNETTE_BINDING_NIL;
  }
}
static bool v_main_world(void *u, uint64_t binding, uint64_t *world, const char **error) {
  TfpFx *fx = u;
  return retained(fx, TFP_FX_FN_MAIN_WORLD, binding, error) && P(fx)->main_world(P(fx)->user, world, error);
}
static bool v_entities(void *u, uint64_t binding, uint64_t world, uint64_t *entities, uint32_t capacity, TfpVignetteList *kind, uint32_t *count,
                       const char **error) {
  TfpFx *fx = u;
  return retained(fx, TFP_FX_FN_ENTITIES, binding, error) && P(fx)->world_entities(P(fx)->user, world, entities, capacity, kind, count, error);
}
static bool v_manager(void *u, uint64_t binding, uint64_t world, uint64_t *manager, const char **error) {
  TfpFx *fx = u;
  return retained(fx, TFP_FX_FN_RENDER_DATA_COMPONENT, binding, error) && P(fx)->render_data_component(P(fx)->user, world, manager, error);
}
static bool v_alive(void *u, uint64_t binding, uint64_t entity, bool *alive, const char **error) {
  TfpFx *fx = u;
  return retained(fx, TFP_FX_FN_ALIVE, binding, error) && P(fx)->entity_alive(P(fx)->user, entity, alive, error);
}
static bool v_instances(void *u, uint64_t binding, uint64_t manager, uint64_t entity, double *ids, uint32_t capacity, uint32_t *count,
                        const char **error) {
  TfpFx *fx = u;
  return retained(fx, TFP_FX_FN_INSTANCES, binding, error) && P(fx)->instances(P(fx)->user, manager, entity, ids, capacity, count, error);
}
static bool v_get(void *u, uint64_t binding, uint64_t manager, uint64_t entity, double id, const char *property, TfpVignetteValue *out,
                  const char **error) {
  TfpFx *fx = u;
  return retained(fx, TFP_FX_FN_GET_PROPERTY, binding, error) && P(fx)->get_property(P(fx)->user, manager, entity, id, property, out, error);
}
static bool v_set(void *u, uint64_t binding, uint64_t manager, uint64_t entity, double id, const char *property, bool value, bool *refused,
                  const char **error) {
  TfpFx *fx = u;
  return retained(fx, TFP_FX_FN_SET_PROPERTY, binding, error) &&
         P(fx)->set_property(P(fx)->user, manager, entity, id, property, value, refused, error);
}

/* ---- aiming markers (TfpAimMarkersHost) ----------------------------------------------------- */
static bool k_module(void *u, uint64_t *game, const char **error) {
  TfpFx *fx = u;
  TfpOpsText why;
  if (tfp_ops_module(&fx->w->ops, TFP_OPS_SCOPE_IDENTITY, game, &why)) return true;
  *error = ops_why(fx, &why);
  return false;
}
static bool k_windows(void *u, bool *wine, const char **error) { (void)error; *wine = ((TfpFx *)u)->w->memory.wine; return true; }
static size_t k_query(void *u, uint64_t address, TfpAimMarkersRegion *region, size_t size) {
  TfpFx *fx = u;
  return P(fx)->virtual_query(P(fx)->user, address, region, size);
}
static bool k_call(void *u, uint64_t function, uint64_t widget, uint8_t shown, const char **error) {
  TfpFx *fx = u;
  return P(fx)->set_widget_shown(P(fx)->user, function, widget, shown, error);
}
static bool k_foreign(void *u) { /* options.foreign(): neither nil nor false; a raise keeps the markers alone */
  V v;
  return !foreign_value(u, &v, NULL) || truthy(v);
}
static void identity_texts(const TfpOpsSnapshot *s, TfpAimMarkersSnapshot *out) {
  char text[24];
  tfp_wire_identity_text(text, 'P', tfp_ops_player_key(s));
  out->has_player_identity = true, out->player_identity_len = strlen(text);
  memcpy(out->player_identity, text, out->player_identity_len);
  if (s->full) tfp_wire_identity_text(text, 'W', tfp_ops_identity_key(s));
  out->has_identity = true, out->identity_len = strlen(text);
  memcpy(out->identity, text, out->identity_len);
}
static bool k_take(TfpFx *fx, SnapshotFn which, int slot, TfpAimMarkersSnapshot *out) {
  TfpOpsSnapshot *s = &fx->marker_snap[slot];
  if (take(fx, which, s, NULL) != TFP_OPS_TRUE) return false;
  memset(out, 0, sizeof *out);
  out->source = s;
  out->safe = s->safe, out->camera_kind = (int)s->camera_kind;
  identity_texts(s, out);
  out->shoulder_aim = s->shoulder_aim, out->controller_active = s->controller_active, out->aiming = s->aiming;
  out->foreign_camera = s->foreign_camera;
  return true;
}
static bool k_snapshot(void *u, TfpAimMarkersSnapshot *out) { return k_take(u, tfp_ops_snapshot, 0, out); }
static bool k_pin_snapshot(void *u, TfpAimMarkersSnapshot *out) { return k_take(u, tfp_ops_pin_player_snapshot, 1, out); }
static bool k_pinned_ready(void *u, const TfpAimMarkersSnapshot *s) { /* options.pinned_ready(s)==true (rt:38-47) */
  TfpFx *fx = u;
  TfpRtArgs args;
  TfpRtTable table;
  V r[1];
  memset(&args, 0, sizeof args);
  tfp_wire_snapshot_table(fx->w, s->source, &table);
  args.table = &table;
  return entry(fx, TFP_RT_E_pinned_ready, &args, r, 1) == TFP_RT_OK && r[0].kind == TFP_RT_TRUE;
}
static bool k_fallback_ready(void *u) {
  V r[1];
  return entry(u, TFP_RT_E_fallback_ready, NULL, r, 1) == TFP_RT_OK && truthy(r[0]);
}
static bool k_pose_context(void *u, const TfpAimMarkersSnapshot *s, uint64_t *weapon_object, const char **error) {
  TfpFx *fx = u;
  TfpOpsContext context;
  TfpOpsText why;
  if (!tfp_ops_pose_context(&fx->w->ops, s->source, &context, &why)) { *error = ops_why(fx, &why); return false; }
  *weapon_object = context.weapon_object;
  return true;
}
/* options.settings() (rt:726): (mod.config or {}).aim_markers. */
static bool k_settings(void *u, TfpAimMarkersSaved *out) {
  TfpFx *fx = u;
  V r[1];
  out->count = 0;
  if (entry(fx, TFP_RT_E_markers_settings, NULL, r, 1) != TFP_RT_OK) return false;
  if (r[0].kind == TFP_RT_REF && r[0].ref == &tfp_rt_empty_table) return true;
  if (r[0].kind == TFP_RT_REF && r[0].ref == &fx->saved) {
    for (size_t i = 0; i < fx->saved.count && i < TFP_AIM_MARKERS_SAVED; i++)
      snprintf(out->keys[out->count++], sizeof out->keys[0], "%s", fx->saved.keys[i]);
    return true;
  }
  return fx->options.markers_decode && fx->options.markers_decode(fx->w, r[0], out);
}
/* options.save(values) (rt:727): mod.set('aim_markers', values). A raise (no settings store) is
 * "not saved" with the raised text: the lease has no other channel for it. */
static bool k_save(void *u, const char (*keys)[TFP_AIM_MARKERS_KEY], size_t count, const char **why) {
  TfpFx *fx = u;
  TfpRtArgs args;
  V r[2];
  memset(&args, 0, sizeof args);
  fx->saved.count = 0;
  for (size_t i = 0; i < count && i < TFP_AIM_MARKERS_SAVED + 1; i++) memcpy(fx->saved.keys[fx->saved.count++], keys[i], TFP_AIM_MARKERS_KEY);
  args.v[0] = tfp_wire_ref(&fx->saved);
  *why = NULL;
  if (fx->options.markers_value) {
    const char *list[TFP_AIM_MARKERS_SAVED + 1];
    for (size_t i = 0; i < fx->saved.count; i++) list[i] = fx->saved.keys[i];
    args.v[0] = fx->options.markers_value(fx->w, list, fx->saved.count);
    if (args.v[0].kind == TFP_RT_NIL) { *why = "The aiming-marker table could not be stored"; return false; }
  }
  TfpRtStatus s = entry(fx, TFP_RT_E_markers_save, &args, r, 2);
  if (s == TFP_RT_RAISED) { *why = raised_text(fx); return false; }
  if (s == TFP_RT_OK && r[0].kind == TFP_RT_TRUE) return true;
  if (s == TFP_RT_OK && r[1].kind == TFP_RT_TEXT) *why = value_name(fx, r[1]);
  return false;
}
static TfpAimMarkersTri k_global(void *u) { /* options.global_setting() (rt:728) */
  V r[1];
  if (entry(u, TFP_RT_E_markers_global_setting, NULL, r, 1) != TFP_RT_OK) return TFP_AIM_MARKERS_UNSET;
  return r[0].kind == TFP_RT_TRUE ? TFP_AIM_MARKERS_SHOW : r[0].kind == TFP_RT_FALSE ? TFP_AIM_MARKERS_HIDE : TFP_AIM_MARKERS_UNSET;
}
static TfpAimMarkersSave k_save_global(void *u, bool value, const char **why) { /* options.save_global(value) (rt:729) */
  TfpFx *fx = u;
  TfpRtArgs args;
  V r[2];
  memset(&args, 0, sizeof args);
  args.v[0] = boolean(value);
  *why = NULL;
  TfpRtStatus s = entry(fx, TFP_RT_E_markers_save_global, &args, r, 2);
  if (s == TFP_RT_RAISED) { *why = raised_text(fx); return TFP_AIM_MARKERS_SAVE_RAISED; }
  if (s == TFP_RT_OK && r[0].kind == TFP_RT_TRUE) return TFP_AIM_MARKERS_SAVE_DONE;
  if (s == TFP_RT_OK && r[1].kind == TFP_RT_TEXT) *why = value_name(fx, r[1]);
  return TFP_AIM_MARKERS_SAVE_REFUSED;
}

/* ---- movement (TfpMovementHost) ------------------------------------------------------------- */
static TfpMovementCall m_take(TfpFx *fx, SnapshotFn which, TfpMovementSnapshot *out, const char **why) {
  TfpOpsSnapshot *s = &fx->move_snap;
  TfpOpsResult got = take(fx, which, s, why);
  if (got == TFP_OPS_RAISED) return TFP_MOVEMENT_RAISED;
  if (got != TFP_OPS_TRUE) return TFP_MOVEMENT_REFUSED;
  memset(out, 0, sizeof *out);
  out->handle = s;
  out->base = s->base, out->world = s->world, out->avatar_owner = s->avatar_owner;
  out->avatar_index = s->avatar_index, out->avatar = s->avatar, out->body = s->body, out->camera_kind = s->camera_kind;
  memcpy(out->avatar_descriptor, s->avatar_descriptor, sizeof out->avatar_descriptor);
  out->safe = s->safe, out->seated = s->seated, out->controller_active = s->controller_active;
  return TFP_MOVEMENT_DONE;
}
static TfpMovementCall m_snapshot(void *u, TfpMovementSnapshot *out, const char **why) { return m_take(u, tfp_ops_player_snapshot, out, why); }
static TfpMovementCall m_pin_snapshot(void *u, TfpMovementSnapshot *out, const char **why) {
  return m_take(u, tfp_ops_pin_player_snapshot, out, why);
}
/* backend:action_state(s): the state, or nil (then nobody is on foot). It does not raise. */
static bool m_action_state(void *u, const TfpMovementSnapshot *s, const void **action, const char **error) {
  TfpFx *fx = u;
  TfpOpsText why;
  (void)error;
  *action = tfp_ops_action_state(&fx->w->ops, s->handle, &fx->move_action, &why) == TFP_OPS_TRUE ? &fx->move_action : NULL;
  return true;
}
static bool m_on_foot(void *u, const TfpMovementSnapshot *s, const void *action, bool *on_foot, const char **error) {
  TfpFx *fx = u;
  (void)error;
  *on_foot = tfp_ops_on_foot(&fx->w->ops, s->handle, action);
  return true;
}
static bool m_foreign(void *u, TfpMovementForeign *out, const char **error) {
  V v;
  if (!foreign_value(u, &v, error)) return false;
  *out = v.kind == TFP_RT_FALSE ? TFP_MOVEMENT_FOREIGN_FALSE : v.kind == TFP_RT_TRUE ? TFP_MOVEMENT_FOREIGN_TRUE : TFP_MOVEMENT_FOREIGN_OTHER;
  return true;
}
static size_t m_query(void *u, uint64_t address, TfpMovementRegion *region, size_t size) {
  TfpFx *fx = u;
  return P(fx)->virtual_query(P(fx)->user, address, region, size);
}
static int m_write(void *u, uint64_t address, const uint8_t *bytes, size_t size, size_t *written) {
  TfpFx *fx = u;
  tfp_ops_memo_wrote(&fx->w->ops); /* round 35: a snapshot memo must not outlive a game write */
  return P(fx)->write_process_memory(P(fx)->user, address, bytes, size, written);
}
static bool m_find(TfpFx *fx, bool index, const char *name, TfpMovementNumber *key, const char **error) {
  key->has = false, key->value = 0;
  return P(fx)->keyboard_find(P(fx)->user, index, name, &key->has, &key->value, error);
}
static bool m_button_index(void *u, const char *name, TfpMovementNumber *key, const char **error) { return m_find(u, true, name, key, error); }
static bool m_button_id(void *u, const char *name, TfpMovementNumber *key, const char **error) { return m_find(u, false, name, key, error); }
static bool m_button(void *u, double key, TfpMovementNumber *value, const char **error) {
  TfpFx *fx = u;
  value->has = false, value->value = 0;
  return P(fx)->keyboard_button(P(fx)->user, key, &value->has, &value->value, error);
}
static bool m_mouse_focus(void *u, bool *focus, const char **error) {
  TfpFx *fx = u;
  return P(fx)->window_mouse_focus(P(fx)->user, focus, error);
}
static bool m_show_cursor(void *u, bool *shown, const char **error) {
  TfpFx *fx = u;
  return P(fx)->window_show_cursor(P(fx)->user, shown, error);
}
static bool m_fallback_ready(void *u, const TfpMovementSnapshot *s) { /* deps.fallback_ready(s)==true (rt:697) */
  bool is_true = false;
  (void)s; /* runtime.lua's fallback_ready takes no argument */
  return entry_true(u, TFP_RT_E_fallback_ready, &is_true, NULL) && is_true;
}

/* ---- the native aim option (TfpNativeAimHost) ----------------------------------------------- */
static bool a_take(TfpFx *fx, SnapshotFn which, TfpNativeAimSnapshot *out, const char **why) {
  TfpOpsSnapshot s;
  char text[24];
  if (take(fx, which, &s, why) != TFP_OPS_TRUE) return false;
  memset(out, 0, sizeof *out);
  out->base = s.base, out->avatar_owner = s.avatar_owner;
  tfp_wire_identity_text(text, 'P', tfp_ops_player_key(&s));
  out->player_identity_len = strlen(text);
  memcpy(out->player_identity, text, out->player_identity_len);
  if (s.full) tfp_wire_identity_text(text, 'W', tfp_ops_identity_key(&s));
  out->identity_len = strlen(text);
  memcpy(out->identity, text, out->identity_len);
  memcpy(out->weapon_descriptor, s.weapon_descriptor, sizeof out->weapon_descriptor);
  out->weapon = s.weapon, out->safe = s.safe, out->camera_kind = (int)s.camera_kind;
  return true;
}
static bool a_snapshot(void *u, TfpNativeAimSnapshot *out, const char **why) { return a_take(u, tfp_ops_snapshot, out, why); }
static bool a_player_snapshot(void *u, TfpNativeAimSnapshot *out, const char **why) { return a_take(u, tfp_ops_player_snapshot, out, why); }
static bool a_pin_snapshot(void *u, TfpNativeAimSnapshot *out, const char **why) { return a_take(u, tfp_ops_pin_player_snapshot, out, why); }
/* src/memory.lua:164-208 with no injected io and no module attached: the address rule (:66-77),
 * committed private read-write pages only (:141-162), WriteProcessMemory, then the readback. */
TfpNativeAimCopy tfp_fx_memory_copy(TfpFx *fx, uint64_t address, uint8_t value, const char **why) {
  TfpMovementRegion r;
  size_t written = 0;
  uint8_t now = 0;
  *why = NULL;
  if (address < 65536 || address + 1 > UINT64_C(140737488355328)) { *why = "Address or copy size is out of bounds"; return TFP_NATIVE_AIM_COPY_REFUSED; }
  memset(&r, 0, sizeof r);
  if (P(fx)->virtual_query(P(fx)->user, address, &r, sizeof r) != sizeof r) { *why = "Write region is unavailable"; return TFP_NATIVE_AIM_COPY_REFUSED; }
  if (r.size == 0 || r.base > address || r.base + r.size <= address || r.state != 0x1000 || r.kind != 0x20000 || r.protect != 4) {
    *why = "Writes require committed private read-write memory";
    return TFP_NATIVE_AIM_COPY_REFUSED;
  }
  tfp_ops_memo_wrote(&fx->w->ops); /* round 35: a snapshot memo must not outlive a game write */
  if (P(fx)->write_process_memory(P(fx)->user, address, &value, 1, &written) == 0 || written != 1) {
    *why = "Memory write failed or copied only part of the data";
    return TFP_NATIVE_AIM_COPY_REFUSED;
  }
  if (!fx->w->memory.read(fx->w->memory.user, address, &now, 1) || now != value) { *why = "Memory write readback failed"; return TFP_NATIVE_AIM_COPY_REFUSED; }
  return TFP_NATIVE_AIM_COPY_DONE;
}
static TfpNativeAimCopy a_copy(void *u, uint64_t address, uint8_t value, const char **why) { return tfp_fx_memory_copy(u, address, value, why); }
static bool a_enabled(void *u) { /* rt:721-723 */
  V r[1];
  return entry(u, TFP_RT_E_native_aim_enabled, NULL, r, 1) == TFP_RT_OK && truthy(r[0]);
}
static void a_controller(void *u, TfpNativeAimControllerState *out) { out->pose_offset = controller_pose_offset(u); }

/* ---- the runtime host's pointers ------------------------------------------------------------ */
static bool ready(const TfpFx *fx, TfpFxModule module) { return fx->state[module] == TFP_FX_READY; }
static bool pair(TfpRtHost *h, bool done, const char *why, V *o1, V *o2) {
  *o1 = boolean(done);
  *o2 = kept(FXH(h), why);
  return true;
}
static bool answer(TfpRtHost *h, TfpDofAnswer a, const char *why, V *o1, V *o2) {
  if (a == TFP_DOF_RAISED) return tfp_wire_raise(h, why ? why : "nil");
  *o1 = a == TFP_DOF_NIL ? nil() : boolean(a == TFP_DOF_TRUE);
  *o2 = kept(FXH(h), why);
  return true;
}
static V tri(TfpDofAnswer a) { return a == TFP_DOF_TRUE ? boolean(true) : a == TFP_DOF_FALSE ? boolean(false) : nil(); }

/* dof (rt:707-712; update(controller, camera, compat) at rt:573, :621, :633) */
static bool rt_dof_new(TfpRtHost *h) {
  TfpFx *fx = FXH(h);
  tfp_vignette_init(&fx->vignette, &fx->vignette_host);
  tfp_dof_init(&fx->dof, &fx->dof_host, NULL, TFP_DOF_VIGNETTE_MODULE);
  fx->made[TFP_FX_DOF] = fx->made[TFP_FX_VIGNETTE] = true;
  return true;
}
static bool rt_dof_update_work(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_DOF]) return fx->off.dof_update(h, o1, o2);
  if (ready(fx, TFP_FX_DOF)) { TfpDofAnswer a = tfp_dof_update(&fx->dof, &fx->compat, &why); return answer(h, a, why, o1, o2); }
  if (tfp_dof_is_clean(&fx->dof)) return fx->off.dof_update(h, o1, o2);
  { TfpDofAnswer a = tfp_dof_stop(&fx->dof, &why); return answer(h, a, why, o1, o2); } /* refused while it owned: give back what it can */
}
static bool rt_dof_stop_work(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_DOF]) return fx->off.dof_stop(h, o1, o2);
  { TfpDofAnswer a = tfp_dof_stop(&fx->dof, &why); return answer(h, a, why, o1, o2); }
}
static bool rt_dof_shutdown_work(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_DOF]) return fx->off.dof_shutdown(h, o1, o2);
  { TfpDofAnswer a = tfp_dof_shutdown(&fx->dof, &why); return answer(h, a, why, o1, o2); }
}
/* the three above, timed (the vignette inside included; tfp_wire_effects_take_spent takes it out) */
#define TIMED_D(call) \
  { TfpFx *fx = FXH(h); const int64_t t0 = perf_now(fx); const bool done = call; fx->spent.dof += perf_now(fx) - t0; return done; }
static bool rt_dof_update(TfpRtHost *h, V *o1, V *o2) TIMED_D(rt_dof_update_work(h, o1, o2))
static bool rt_dof_stop(TfpRtHost *h, V *o1, V *o2) TIMED_D(rt_dof_stop_work(h, o1, o2))
static bool rt_dof_shutdown(TfpRtHost *h, V *o1, V *o2) TIMED_D(rt_dof_shutdown_work(h, o1, o2))
#undef TIMED_D
static bool rt_dof_is_clean(TfpRtHost *h, V *o1) {
  TfpFx *fx = FXH(h);
  if (!fx->made[TFP_FX_DOF]) return fx->off.dof_is_clean(h, o1);
  *o1 = boolean(tfp_dof_is_clean(&fx->dof));
  return true;
}
static bool rt_dof_result(TfpRtHost *h, TfpRtTable *o1) {
  TfpFx *fx = FXH(h);
  TfpDofResult r;
  if (!fx->made[TFP_FX_DOF]) return fx->off.dof_result(h, o1);
  tfp_dof_result(&fx->dof, &r);
  memset(o1, 0, sizeof *o1);
  o1->present = true, o1->ref = &fx->dof;
  o1->f[TFP_RT_DOF_OWNED] = tri(r.owned);
  return true;
}
static bool rt_dof_status(TfpRtHost *h, V *o1) {
  TfpFx *fx = FXH(h);
  if (!fx->made[TFP_FX_DOF]) return fx->off.dof_status(h, o1);
  snprintf(fx->status, sizeof fx->status, "%s", tfp_dof_status(&fx->dof));
  *o1 = kept(fx, fx->status);
  return true;
}

/* movement (rt:697; update(controller) at rt:630) */
static void movement_keyboard(TfpFx *fx) { fx->movement_host.engine_keyboard = table_token(fx, "Keyboard"); }
static bool rt_movement_new(TfpRtHost *h) {
  TfpFx *fx = FXH(h);
  tfp_movement_init(&fx->movement, &fx->movement_host);
  fx->made[TFP_FX_MOVEMENT] = true;
  return true;
}
static bool rt_movement_update(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_MOVEMENT]) return fx->off.movement_update(h, o1, o2);
  movement_keyboard(fx);
  if (ready(fx, TFP_FX_MOVEMENT)) { bool done = tfp_movement_update(&fx->movement, false, &why); return pair(h, done, why, o1, o2); }
  if (tfp_movement_is_clean(&fx->movement)) return fx->off.movement_update(h, o1, o2);
  { bool done = tfp_movement_stop(&fx->movement, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_movement_stop(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_MOVEMENT]) return fx->off.movement_stop(h, o1, o2);
  { bool done = tfp_movement_stop(&fx->movement, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_movement_shutdown(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_MOVEMENT]) return fx->off.movement_shutdown(h, o1, o2);
  { bool done = tfp_movement_shutdown(&fx->movement, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_movement_is_clean(TfpRtHost *h, V *o1) {
  TfpFx *fx = FXH(h);
  if (!fx->made[TFP_FX_MOVEMENT]) return fx->off.movement_is_clean(h, o1);
  *o1 = boolean(tfp_movement_is_clean(&fx->movement));
  return true;
}
static bool rt_movement_result(TfpRtHost *h, TfpRtTable *o1) { /* the recovery reads phase (and armed, which movement has not) */
  TfpFx *fx = FXH(h);
  TfpMovementResult r;
  if (!fx->made[TFP_FX_MOVEMENT]) return fx->off.movement_result(h, o1);
  tfp_movement_result(&fx->movement, &r);
  memset(o1, 0, sizeof *o1);
  o1->present = true, o1->ref = &fx->movement;
  o1->f[TFP_RT_TARGET_PHASE] = tfp_wire_text(tfp_movement_phase_name(r.phase));
  return true;
}

/* the native aim option (rt:720-723; update(controller) at rt:568) */
static bool rt_aim_new(TfpRtHost *h) {
  TfpFx *fx = FXH(h);
  tfp_native_aim_init(&fx->aim, &fx->aim_host);
  fx->made[TFP_FX_AIM_OPTION] = true;
  return true;
}
static bool rt_aim_update(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_AIM_OPTION]) return fx->off.native_aim_update(h, o1, o2);
  if (ready(fx, TFP_FX_AIM_OPTION)) { bool done = tfp_native_aim_update(&fx->aim, &why); return pair(h, done, why, o1, o2); }
  if (tfp_native_aim_is_clean(&fx->aim)) return fx->off.native_aim_update(h, o1, o2);
  { bool done = tfp_native_aim_stop(&fx->aim, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_aim_stop(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_AIM_OPTION]) return fx->off.native_aim_stop(h, o1, o2);
  { bool done = tfp_native_aim_stop(&fx->aim, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_aim_shutdown(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_AIM_OPTION]) return fx->off.native_aim_shutdown(h, o1, o2);
  { bool done = tfp_native_aim_shutdown(&fx->aim, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_aim_is_clean(TfpRtHost *h, V *o1) {
  TfpFx *fx = FXH(h);
  if (!fx->made[TFP_FX_AIM_OPTION]) return fx->off.native_aim_is_clean(h, o1);
  *o1 = boolean(tfp_native_aim_is_clean(&fx->aim));
  return true;
}
static bool rt_aim_result(TfpRtHost *h, TfpRtTable *o1) { /* only handed on (api.status) */
  TfpFx *fx = FXH(h);
  if (!fx->made[TFP_FX_AIM_OPTION]) return fx->off.native_aim_result(h, o1);
  memset(o1, 0, sizeof *o1);
  o1->present = true, o1->ref = &fx->aim;
  return true;
}

/* aiming markers (rt:724-732; update / stop at rt:584, :1216; the menu's four at rt:1081-1084) */
static bool rt_markers_new(TfpRtHost *h) {
  TfpFx *fx = FXH(h);
  tfp_aim_markers_init(&fx->markers, &fx->markers_host);
  fx->made[TFP_FX_MARKERS] = true;
  return true;
}
static bool rt_markers_update(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_MARKERS]) return fx->off.aim_markers_update(h, o1, o2);
  if (ready(fx, TFP_FX_MARKERS)) { bool done = tfp_aim_markers_update(&fx->markers, &why); return pair(h, done, why, o1, o2); }
  if (tfp_aim_markers_is_clean(&fx->markers)) return fx->off.aim_markers_update(h, o1, o2);
  { bool done = tfp_aim_markers_stop(&fx->markers, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_markers_stop(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_MARKERS]) return fx->off.aim_markers_stop(h, o1, o2);
  { bool done = tfp_aim_markers_stop(&fx->markers, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_markers_shutdown(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!fx->made[TFP_FX_MARKERS]) return fx->off.aim_markers_shutdown(h, o1, o2);
  { bool done = tfp_aim_markers_shutdown(&fx->markers, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_markers_is_clean(TfpRtHost *h, V *o1) {
  TfpFx *fx = FXH(h);
  if (!fx->made[TFP_FX_MARKERS]) return fx->off.aim_markers_is_clean(h, o1);
  *o1 = boolean(tfp_aim_markers_is_clean(&fx->markers));
  return true;
}
static bool live(const TfpFx *fx, TfpFxModule module) { return fx->made[module] && ready(fx, module); }
/* target(): {key, identity, allowed} or nil. The REF is a TfpAimMarkersMenuTarget (valid until the
 * next target()). */
static bool rt_markers_target(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *error = NULL;
  if (!live(fx, TFP_FX_MARKERS)) return fx->off.aim_markers_target(h, o1, o2);
  *o1 = *o2 = nil();
  switch (tfp_aim_markers_target(&fx->markers, &fx->menu_target, &error)) {
    case TFP_AIM_MARKERS_READ_RAISED: return tfp_wire_raise(h, error ? error : "nil");
    case TFP_AIM_MARKERS_READ_TARGET: *o1 = tfp_wire_ref(&fx->menu_target); break;
    default: break;
  }
  return true;
}
/* toggle(key, identity): anything that is not a text can never be the weapon's key or identity
 * (aim_markers.lua:244: `target.key~=key or target.identity~=identity`). */
static bool rt_markers_toggle(TfpRtHost *h, V key, V identity, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  char text[TFP_AIM_MARKERS_SAVED_KEY] = "";
  const char *why = NULL, *id = "\0";
  size_t id_len = 1; /* one zero byte: no identity is that */
  if (!live(fx, TFP_FX_MARKERS)) return fx->off.aim_markers_toggle(h, key, identity, o1, o2);
  if (key.kind == TFP_RT_TEXT && key.length < sizeof text && !memchr(key.text, 0, key.length)) memcpy(text, key.text, key.length);
  if (identity.kind == TFP_RT_TEXT) id = identity.text, id_len = identity.length;
  { bool done = tfp_aim_markers_toggle(&fx->markers, text, id, id_len, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_markers_setting(TfpRtHost *h, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  if (!live(fx, TFP_FX_MARKERS)) return fx->off.aim_markers_setting(h, o1, o2);
  *o1 = boolean(tfp_aim_markers_setting(&fx->markers));
  *o2 = nil();
  return true;
}
/* set_setting(value): aim_markers.lua:161-163 before the Boolean reaches the unit. */
static bool rt_markers_set_setting(TfpRtHost *h, V value, V *o1, V *o2) {
  TfpFx *fx = FXH(h);
  const char *why = NULL;
  if (!live(fx, TFP_FX_MARKERS)) return fx->off.aim_markers_set_setting(h, value, o1, o2);
  if (value.kind != TFP_RT_TRUE && value.kind != TFP_RT_FALSE)
    return pair(h, false, fx->markers.closing ? "Mod is stopping" : "Aim-marker setting must be on or off", o1, o2);
  { bool done = tfp_aim_markers_set_setting(&fx->markers, value.kind == TFP_RT_TRUE, &why); return pair(h, done, why, o1, o2); }
}
static bool rt_markers_result(TfpRtHost *h, TfpRtTable *o1) { /* only handed on (api.status) */
  TfpFx *fx = FXH(h);
  if (!fx->made[TFP_FX_MARKERS]) return fx->off.aim_markers_result(h, o1);
  memset(o1, 0, sizeof *o1);
  o1->present = true, o1->ref = &fx->markers;
  return true;
}
static V rt_markers_fault(TfpRtHost *h) { /* owned_markers.fault (rt:1091) */
  TfpFx *fx = FXH(h);
  return fx->made[TFP_FX_MARKERS] && fx->markers.has_fault ? kept(fx, fx->markers.fault) : nil();
}

/* ---- binding -------------------------------------------------------------------------------- */
static void refuse(TfpFx *fx, TfpFxModule module, const char *why) {
  if (fx->state[module] == TFP_FX_REFUSED) return;
  fx->state[module] = TFP_FX_REFUSED;
  snprintf(fx->reason[module], sizeof fx->reason[module], "%s", why);
}
void tfp_wire_effects_bind(TfpWire *w, TfpFx *fx, const TfpFxPlatform *platform, const TfpFxOptions *options) {
  TfpRtHost *h = &w->host;
  memset(fx, 0, sizeof *fx);
  fx->w = w;
  fx->platform = *platform;
  if (options) fx->options = *options;
  fx->off = *h;
  w->ext[TFP_WIRE_EXT_EFFECTS] = fx;

  fx->compat = (TfpDofCompat){fx, dof_compat_foreign};
  fx->dof_host = (TfpDofHost){
    .user = fx, .clock = unit_clock, .log = unit_log, .application_binding = dof_binding, .application_render_setting = dof_get,
    .application_set_render_setting = dof_set, .backend_renderer_dof_value = dof_cached, .backend_foreign_camera = dof_foreign,
    .backend_profile_pose = true, .backend_alignment_executable = dof_executable, .backend_read = mem_read, .verify = NULL,
    .active_view = dof_active_view, .fallback_ready = dof_fallback_ready, .controller_is_enabled = controller_enabled,
    .controller_result = dof_controller, .vignette_update = dv_update, .vignette_stop = dv_stop, .vignette_shutdown = dv_shutdown,
    .vignette_is_clean = dv_is_clean, .vignette_result = dv_result, .vignette_status = dv_status,
    .build = fx->options.dof ? fx->options.dof : &tfp_dof_build_25480438};
  fx->vignette_host = (TfpVignetteHost){
    .user = fx, .log = unit_log, .clock = v_clock, .compat_foreign = v_compat_foreign, .backend_foreign_camera = foreign_held,
    .active_view = v_active_view, .backend_player_snapshot = v_player_snapshot, .controller_result = v_controller,
    .controller_is_enabled = controller_enabled, .stingray_api = v_api, .application_main_world = v_main_world, .world_entities = v_entities,
    .entity_manager_render_data_component = v_manager, .entity_manager_alive = v_alive, .render_data_component_instances = v_instances,
    .render_data_component_get_property = v_get, .render_data_component_set_property = v_set};
  fx->markers_host = (TfpAimMarkersHost){
    .user = fx, .backend_module_identity = k_module, .backend_read = mem_read, .backend_ptr = mem_ptr, .backend_u32 = mem_u32,
    .backend_u8 = mem_u8, .backend_windows = k_windows, .windows_virtual_query = k_query, .game_set_widget_shown = k_call, .setter = NULL,
    .foreign = k_foreign, .backend_snapshot = k_snapshot, .backend_pin_first_person = false, .backend_pin_player_snapshot = k_pin_snapshot,
    .pinned_ready = k_pinned_ready, .fallback_ready = k_fallback_ready, .controller_is_enabled = controller_enabled,
    .controller_pose_offset = controller_pose_offset, .backend_pose_context = k_pose_context, .settings = k_settings, .save = k_save,
    .global_setting = k_global, .save_global = k_save_global, .log = unit_log,
    .build = fx->options.markers ? fx->options.markers : &tfp_aim_markers_build_25480438};
  fx->movement_host = (TfpMovementHost){
    .user = fx, .backend_player_snapshot = m_snapshot, .backend_pin_player_snapshot = m_pin_snapshot, .backend_pin_first_person = false,
    .backend_read = mem_read, .backend_ptr = mem_ptr, .backend_u32 = mem_u32, .backend_action_state = m_action_state,
    .backend_on_foot = m_on_foot, .backend_foreign_camera = m_foreign, .windows_virtual_query = m_query,
    .windows_write_process_memory = m_write, .engine_keyboard = 0, .keyboard_button_index = m_button_index, .keyboard_button_id = m_button_id,
    .keyboard_button = m_button, .window_mouse_focus = m_mouse_focus, .window_show_cursor = m_show_cursor,
    .controller_is_enabled = controller_enabled, .controller_result_pose_offset = controller_pose_offset, .controller_focus_owned = NULL,
    .deps_fallback_ready = m_fallback_ready, .deps_writable = NULL, .deps_write = NULL, .log = unit_log,
    .build = fx->options.movement ? fx->options.movement : &tfp_movement_build_25480438};
  fx->aim_host = (TfpNativeAimHost){
    .user = fx, .backend_snapshot = a_snapshot, .backend_player_snapshot = a_player_snapshot, .backend_pin_player_snapshot = a_pin_snapshot,
    .backend_pin_first_person = false, .backend_read = mem_read, .backend_ptr = mem_ptr, .backend_u32 = mem_u32, .backend_u8 = mem_u8,
    .backend_map = mem_map, .backend_foreign_camera = foreign_held, .memory_copy = a_copy, .enabled = a_enabled, .foreign = foreign_held,
    .controller_is_enabled = controller_enabled, .controller_result = a_controller,
    .build = fx->options.aim_option ? fx->options.aim_option : &tfp_native_aim_build_25480438, .fields = fx->w->memory.fields};

  /* What can be decided without the engine. */
  for (int i = 0; i < TFP_FX_MODULES; i++) {
    char why[200];
    fx->state[i] = i == TFP_FX_MARKERS || i == TFP_FX_AIM_OPTION ? TFP_FX_READY : TFP_FX_WAITING;
    if (tfp_fx_constants[i].unverified) {
      snprintf(why, sizeof why, "%s not verified on build 25480438", tfp_fx_constants[i].unverified);
      refuse(fx, (TfpFxModule)i, why);
    }
  }
  if (!platform->virtual_query) {
    refuse(fx, TFP_FX_MARKERS, "no VirtualQuery in this build of the mod");
    refuse(fx, TFP_FX_MOVEMENT, "no VirtualQuery in this build of the mod");
    refuse(fx, TFP_FX_AIM_OPTION, "no VirtualQuery in this build of the mod");
  }
  if (!platform->write_process_memory) {
    refuse(fx, TFP_FX_MOVEMENT, "no WriteProcessMemory in this build of the mod");
    refuse(fx, TFP_FX_AIM_OPTION, "no WriteProcessMemory in this build of the mod");
  }
  if (!platform->set_widget_shown) refuse(fx, TFP_FX_MARKERS, "no guarded call of the game's widget setter in this build of the mod");
  if (!(platform->engine_ready && platform->engine_function && platform->engine_table && platform->exe_base)) {
    refuse(fx, TFP_FX_DOF, "no engine bridge in this build of the mod");
    refuse(fx, TFP_FX_VIGNETTE, "no engine bridge in this build of the mod");
    refuse(fx, TFP_FX_MOVEMENT, "no engine bridge in this build of the mod");
  }
  if (!(platform->render_setting && platform->set_render_setting)) refuse(fx, TFP_FX_DOF, "Application.render_setting has no caller in this build of the mod");
  if (!(platform->main_world && platform->world_entities && platform->render_data_component && platform->entity_alive && platform->instances &&
        platform->get_property && platform->set_property))
    refuse(fx, TFP_FX_VIGNETTE, "the render-data functions have no caller in this build of the mod");
  if (!(platform->keyboard_find && platform->keyboard_button && platform->window_mouse_focus && platform->window_show_cursor))
    refuse(fx, TFP_FX_MOVEMENT, "Keyboard.button has no caller in this build of the mod");
  /* The vignette lives behind the depth-of-field lease (rt:708). */
  if (fx->state[TFP_FX_DOF] == TFP_FX_REFUSED) refuse(fx, TFP_FX_VIGNETTE, "depth of field is off, and the vignette is switched through it");

  h->dof_new = rt_dof_new, h->dof_update = rt_dof_update, h->dof_result = rt_dof_result, h->dof_stop = rt_dof_stop;
  h->dof_is_clean = rt_dof_is_clean, h->dof_shutdown = rt_dof_shutdown, h->dof_status = rt_dof_status;
  h->movement_new = rt_movement_new, h->movement_stop = rt_movement_stop, h->movement_update = rt_movement_update;
  h->movement_result = rt_movement_result, h->movement_is_clean = rt_movement_is_clean, h->movement_shutdown = rt_movement_shutdown;
  h->native_aim_new = rt_aim_new, h->native_aim_shutdown = rt_aim_shutdown, h->native_aim_update = rt_aim_update;
  h->native_aim_stop = rt_aim_stop, h->native_aim_is_clean = rt_aim_is_clean, h->native_aim_result = rt_aim_result;
  h->aim_markers_new = rt_markers_new, h->aim_markers_shutdown = rt_markers_shutdown, h->aim_markers_stop = rt_markers_stop;
  h->aim_markers_update = rt_markers_update, h->aim_markers_is_clean = rt_markers_is_clean, h->aim_markers_target = rt_markers_target;
  h->aim_markers_toggle = rt_markers_toggle, h->aim_markers_setting = rt_markers_setting;
  h->aim_markers_set_setting = rt_markers_set_setting, h->aim_markers_result = rt_markers_result, h->aim_markers_fault = rt_markers_fault;
  /* native_aim_source and aim_markers_source stay wire_off.c's `true`: the sources are part of the
   * units (tfp_native_aim_source_*, the lease's own TfpAimMarkersSource). */
}

/* ---- load checks and the session log --------------------------------------------------------- */
static void line(TfpFx *fx, const char *format, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void line(TfpFx *fx, const char *format, ...) {
  char text[400];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  say(fx, text);
}
/* Every engine function of the WAITING / READY modules is registered at the census address. */
static void check_engine(TfpFx *fx) {
  uint64_t exe = P(fx)->exe_base(P(fx)->user);
  bool bad[TFP_FX_MODULES] = {false};
  char why[TFP_FX_MODULES][200];
  for (int i = 0; i < TFP_FX_FUNCTIONS; i++) {
    const TfpFxFunctionRow *f = &tfp_fx_functions[i];
    uint64_t at = P(fx)->engine_function(P(fx)->user, f->path);
    if (bad[f->module] || (exe && at == exe + f->rva)) continue;
    bad[f->module] = true;
    if (!exe) snprintf(why[f->module], sizeof why[0], "the executable's base is unknown, %s cannot be checked", f->path);
    else if (!at) snprintf(why[f->module], sizeof why[0], "%s is not registered (build 25480438 has it at exe+0x%x)", f->path, (unsigned)f->rva);
    else if (at >= exe && at - exe < UINT64_C(0x10000000))
      snprintf(why[f->module], sizeof why[0], "%s is at exe+0x%llx, build 25480438 has it at exe+0x%x", f->path, (unsigned long long)(at - exe),
               (unsigned)f->rva);
    else snprintf(why[f->module], sizeof why[0], "%s is at 0x%llx, outside the executable (build 25480438: exe+0x%x)", f->path,
                  (unsigned long long)at, (unsigned)f->rva);
  }
  static const TfpFxModule engine_modules[] = {TFP_FX_DOF, TFP_FX_VIGNETTE, TFP_FX_MOVEMENT};
  for (size_t i = 0; i < sizeof engine_modules / sizeof engine_modules[0]; i++) {
    TfpFxModule m = engine_modules[i];
    if (fx->state[m] == TFP_FX_REFUSED) continue;
    if (bad[m]) {
      refuse(fx, m, why[m]);
      line(fx, "Refused: %s: %s", module_name[m], fx->reason[m]);
    } else if (fx->state[m] == TFP_FX_WAITING) fx->state[m] = TFP_FX_READY;
  }
  if (fx->state[TFP_FX_DOF] == TFP_FX_REFUSED && fx->state[TFP_FX_VIGNETTE] != TFP_FX_REFUSED) {
    refuse(fx, TFP_FX_VIGNETTE, "depth of field is off, and the vignette is switched through it");
    line(fx, "Refused: vignette: %s", fx->reason[TFP_FX_VIGNETTE]);
  }
}
static void announce_ready(TfpFx *fx, TfpFxModule m) {
  const TfpDofBuild *d = fx->dof_host.build;
  const TfpAimMarkersBuild *k = fx->markers_host.build;
  const TfpMovementBuild *mv = fx->movement_host.build;
  const TfpNativeAimBuild *a = fx->aim_host.build;
  switch (m) {
    case TFP_FX_DOF:
      line(fx, "Ready: dof (Application.render_setting exe+0x%x, set_render_setting exe+0x%x: both registered there)", (unsigned)d->get_thunk,
           (unsigned)d->set_thunk);
      break;
    case TFP_FX_VIGNETTE: line(fx, "Ready: vignette (its seven engine functions are registered at the addresses of build 25480438)"); break;
    case TFP_FX_MARKERS:
      line(fx, "Ready: markers (setter game+0x%x, HUD game+0x%x, crosshair +0x%x; the code is checked at the first hide)", (unsigned)k->set_shown,
           (unsigned)k->root, (unsigned)k->crosshair);
      break;
    case TFP_FX_MOVEMENT:
      line(fx, "Ready: movement (pool game+0x%x; Keyboard.button_index, Keyboard.button, Window.mouse_focus, Window.show_cursor registered at the "
               "addresses of build 25480438; the six anchors are checked at the first S press)", (unsigned)mv->pool);
      break;
    default:
      line(fx, "Ready: aimopt (player game+0x%x, weapons game+0x%x; the code is checked when the option is switched on)", (unsigned)a->player,
           (unsigned)a->weapons);
      break;
  }
}
static void events(TfpFx *fx) {
  if (fx->made[TFP_FX_DOF]) {
    const TfpDofSetting *s = &fx->dof.dof;
    if (s->has_record && !fx->seen.dof_record) line(fx, "Effect: dof: dof_enabled read as %s and saved", s->record.before ? "true" : "false");
    if (s->writes != fx->seen.dof_writes) line(fx, "Effect: dof: dof_enabled set (commands sent: %u)", (unsigned)s->writes);
    if (s->restores != fx->seen.dof_restores) line(fx, "Effect: dof: dof_enabled restored (%u)", (unsigned)s->restores);
    fx->seen.dof_record = s->has_record, fx->seen.dof_writes = s->writes, fx->seen.dof_restores = s->restores;
    const TfpVignette *v = &fx->vignette;
    if (v->row_count != fx->seen.vignette_rows) line(fx, "Effect: vignette: %u row(s) found", (unsigned)v->row_count);
    if (v->writes != fx->seen.vignette_writes) line(fx, "Effect: vignette: switched off (writes: %u)", (unsigned)v->writes);
    if (v->restores != fx->seen.vignette_restores) line(fx, "Effect: vignette: restored (%u)", (unsigned)v->restores);
    fx->seen.vignette_rows = v->row_count, fx->seen.vignette_writes = v->writes, fx->seen.vignette_restores = v->restores;
  }
  if (fx->made[TFP_FX_MARKERS]) {
    const TfpAimMarkers *k = &fx->markers;
    if (k->has_lease != fx->seen.markers_hidden) {
      if (k->has_lease) line(fx, "Effect: markers: hidden (%u group(s), HUD 0x%llx)", (unsigned)k->lease.count, (unsigned long long)k->lease.hud.root);
      else line(fx, "Effect: markers: shown again");
    }
    fx->seen.markers_hidden = k->has_lease;
  }
  if (fx->made[TFP_FX_MOVEMENT]) {
    const TfpMovement *m = &fx->movement;
    if (m->has_verified && !fx->seen.move_verified) line(fx, "Effect: movement: anchors ok (game 0x%llx)", (unsigned long long)m->verified);
    if (m->writes != fx->seen.move_writes) line(fx, "Effect: movement: focused byte written at 0x%llx (0 -> 1; writes: %u)", (unsigned long long)m->owned.address,
                                                (unsigned)m->writes);
    if (m->restores != fx->seen.move_restores) line(fx, "Effect: movement: focused byte restored (%u)", (unsigned)m->restores);
    fx->seen.move_verified = m->has_verified, fx->seen.move_writes = m->writes, fx->seen.move_restores = m->restores;
  }
  if (fx->made[TFP_FX_AIM_OPTION]) {
    const TfpNativeAim *a = &fx->aim;
    if (a->writes != fx->seen.aim_writes) line(fx, "Effect: aimopt: preference byte written at 0x%llx (0 -> 1; writes: %u)", (unsigned long long)a->lease.at,
                                               (unsigned)a->writes);
    if (a->restores != fx->seen.aim_restores) line(fx, "Effect: aimopt: preference byte restored (%u)", (unsigned)a->restores);
    fx->seen.aim_writes = a->writes, fx->seen.aim_restores = a->restores;
  }
}
void tfp_wire_effects_frame(TfpWire *w) {
  TfpFx *fx = tfp_wire_effects_of(w);
  const char *why = NULL;
  if (!fx) return;
  if (!fx->announced) {
    fx->announced = true;
    for (int i = 0; i < TFP_FX_MODULES; i++) {
      if (fx->state[i] == TFP_FX_REFUSED) line(fx, "Refused: %s: %s", module_name[i], fx->reason[i]);
      else if (fx->state[i] == TFP_FX_READY) announce_ready(fx, (TfpFxModule)i);
    }
    line(fx, "Off: camera sharing with Photo Mode (not detected on HellForge: do not open it while the view is on)");
    if (fx->state[TFP_FX_DOF] == TFP_FX_WAITING || fx->state[TFP_FX_VIGNETTE] == TFP_FX_WAITING || fx->state[TFP_FX_MOVEMENT] == TFP_FX_WAITING) {
      bool up = P(fx)->engine_ready(P(fx)->user, &why);
      if (!up) line(fx, "Waiting: dof, vignette, movement: the engine's script functions cannot be checked yet (%s)", why ? why : "no script state");
    }
  }
  if (P(fx)->engine_ready && P(fx)->engine_ready(P(fx)->user, &why) && (!fx->checked || fx->generation != generation(fx))) {
    TfpFxState before[TFP_FX_MODULES];
    bool again = fx->checked;
    memcpy(before, fx->state, sizeof before);
    fx->checked = true, fx->generation = generation(fx);
    check_engine(fx);
    for (int i = 0; i < TFP_FX_MODULES; i++)
      if (before[i] == TFP_FX_WAITING && fx->state[i] == TFP_FX_READY) announce_ready(fx, (TfpFxModule)i);
    if (again) line(fx, "Effect: the engine replaced its script state (generation %llu): its functions were checked again", (unsigned long long)fx->generation);
  }
  events(fx);
}

static const char *dof_word(const TfpFx *fx) {
  if (!fx->made[TFP_FX_DOF] || (fx->state[TFP_FX_DOF] != TFP_FX_READY && tfp_dof_setting_is_clean(&fx->dof.dof)))
    return fx->state[TFP_FX_DOF] == TFP_FX_WAITING ? "wait" : "off";
  switch (fx->dof.dof.phase) {
    case TFP_DOF_ACTIVE: return "owned";
    case TFP_DOF_CLEANUP_PENDING: return "pending";
    case TFP_DOF_ALREADY_DISABLED: return "already";
    default: return tfp_dof_phase_name(fx->dof.dof.phase);
  }
}
size_t tfp_wire_effects_second(TfpWire *w, char *out, size_t size) {
  TfpFx *fx = tfp_wire_effects_of(w);
  char vignette[32], markers[16], move[24], aim[24];
  if (!fx || !size) return 0;
  if (!fx->made[TFP_FX_VIGNETTE] || (!vignette_on(fx) && tfp_vignette_is_clean(&fx->vignette)))
    snprintf(vignette, sizeof vignette, "%s", fx->state[TFP_FX_VIGNETTE] == TFP_FX_WAITING ? "wait" : "off");
  else {
    const TfpVignette *v = &fx->vignette;
    uint32_t owned = 0;
    for (uint32_t i = 0; i < v->row_count; i++) owned += v->rows[i].owned;
    if (v->phase == TFP_VIGNETTE_ACTIVE || owned) snprintf(vignette, sizeof vignette, "owned:%u", (unsigned)owned);
    else snprintf(vignette, sizeof vignette, "%s", v->phase == TFP_VIGNETTE_DISCOVERING ? "scan" : v->phase == TFP_VIGNETTE_CLEANUP_PENDING ? "pending"
                                                                                                    : tfp_vignette_phase_name(v->phase));
  }
  if (!fx->made[TFP_FX_MARKERS] || (!ready(fx, TFP_FX_MARKERS) && !fx->markers.has_lease)) snprintf(markers, sizeof markers, "off");
  else snprintf(markers, sizeof markers, "%s", fx->markers.has_fault ? "fault" : fx->markers.has_lease ? "hidden" : "shown");
  if (!fx->made[TFP_FX_MOVEMENT] || (!ready(fx, TFP_FX_MOVEMENT) && !fx->movement.has_owned))
    snprintf(move, sizeof move, "%s", fx->state[TFP_FX_MOVEMENT] == TFP_FX_WAITING ? "wait" : "off");
  else snprintf(move, sizeof move, "%s", fx->movement.phase == TFP_MOVEMENT_ACTIVE ? "held" : fx->movement.phase == TFP_MOVEMENT_CLEANUP_PENDING ? "pending"
                                                                                                  : tfp_movement_phase_name(fx->movement.phase));
  if (!fx->made[TFP_FX_AIM_OPTION] || (!ready(fx, TFP_FX_AIM_OPTION) && !fx->aim.has_lease)) snprintf(aim, sizeof aim, "off");
  else snprintf(aim, sizeof aim, "%s", fx->aim.phase == TFP_NATIVE_AIM_IDLE ? "off" : fx->aim.phase == TFP_NATIVE_AIM_ACTIVE ? "owned"
                                         : fx->aim.phase == TFP_NATIVE_AIM_CLEANUP_PENDING ? "pending" : tfp_native_aim_phase_name(fx->aim.phase));
  int n = snprintf(out, size, "dof=%s vignette=%s markers=%s move=%s aimopt=%s", dof_word(fx), vignette, markers, move, aim);
  return n < 0 ? 0 : (size_t)n < size ? (size_t)n : size - 1;
}
