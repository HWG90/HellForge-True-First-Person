#include "vignette.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* A raised Lua error: its text, or the error value nil. */
typedef struct Raised { bool is_nil; char text[TFP_VIGNETTE_TEXT]; } Raised;

/* assert(false, message) at retail line `line`: LuaJIT puts the position before the text. */
static bool fail(Raised *e, int line, const char *message) {
  e->is_nil = false;
  snprintf(e->text, sizeof e->text, "%s:%d: %s", TFP_VIGNETTE_LUA_CHUNK, line, message);
  return false;
}
/* An error raised by the host passes through unchanged. */
static bool raised(Raised *e, const char *text) {
  e->is_nil = text == NULL;
  if (text) snprintf(e->text, sizeof e->text, "%s", text);
  return false;
}
static const char *text_of(const Raised *e) { return e->is_nil ? "nil" : e->text; }

/* retail report(), lines 11-15. `why` is at most 255 bytes and never v->error_message itself. */
static void report(TfpVignette *v, TfpVignettePhase phase, const char *why) {
  v->phase = phase;
  if (why && !(v->has_error && strcmp(why, v->error_message) == 0)) {
    char line[TFP_VIGNETTE_TEXT + 16];
    snprintf(line, sizeof line, "FPV vignette: %s", why);
    v->host->log(v->host->user, line);
  }
  v->has_error = why != NULL;
  if (why) snprintf(v->error_message, sizeof v->error_message, "%s", why);
}

/* retail foreign(compat), lines 16-21. stop() calls it without the compat object. */
static bool foreign(const TfpVignette *v, bool with_compat) {
  const TfpVignetteHost *h = v->host;
  if (with_compat && h->compat_foreign) return h->compat_foreign(h->user);
  if (h->backend_foreign_camera) return h->backend_foreign_camera(h->user);
  return false;
}

/* retail api(), lines 22-29. */
static bool api(const TfpVignette *v, TfpVignetteApi *a, Raised *e) {
  memset(a, 0, sizeof *a);
  v->host->stingray_api(v->host->user, a);
  if (!a->available) return fail(e, 23, "Vignette component API unavailable");
  for (int i = 0; i < TFP_VIGNETTE_FN_COUNT; i++)
    if (a->fn[i].kind == TFP_VIGNETTE_BINDING_NOT_A_FUNCTION) return fail(e, 26, "Vignette component binding unavailable");
  for (int i = 0; i < TFP_VIGNETTE_FN_COUNT; i++)
    if (a->fn[i].kind == TFP_VIGNETTE_BINDING_NIL) return fail(e, 27, "Vignette component binding incomplete");
  return true;
}
/* retail bindings(c), lines 30-32: the functions are still the ones the context was made with. */
static bool bindings(const TfpVignette *v, Raised *e) {
  TfpVignetteApi a;
  if (!api(v, &a, e)) return false;
  for (int i = 0; i < TFP_VIGNETTE_FN_COUNT; i++)
    if (a.fn[i].token != v->api.fn[i].token) return fail(e, 31, "Vignette component binding changed");
  return true;
}

/* The context's own functions (retail c.api.<name>). */
static bool main_world(const TfpVignette *v, uint64_t binding, uint64_t *world, Raised *e) {
  const char *why = NULL;
  *world = 0;
  return v->host->application_main_world(v->host->user, binding, world, &why) || raised(e, why);
}
static bool alive(const TfpVignette *v, uint64_t entity, bool *out, Raised *e) {
  const char *why = NULL;
  *out = false;
  return v->host->entity_manager_alive(v->host->user, v->api.fn[TFP_VIGNETTE_FN_ALIVE].token, entity, out, &why) || raised(e, why);
}
/* retail instances(c,e), lines 33-36. */
static bool instances(const TfpVignette *v, uint64_t entity, double *ids, uint32_t *count, Raised *e) {
  const char *why = NULL;
  *count = 0;
  if (!v->host->render_data_component_instances(v->host->user, v->api.fn[TFP_VIGNETTE_FN_INSTANCES].token, v->manager, entity, ids,
                                                TFP_VIGNETTE_MAX_INSTANCES, count, &why))
    return raised(e, why);
  if (!(*count <= TFP_VIGNETTE_MAX_INSTANCES)) return fail(e, 35, "Vignette instance count changed");
  return true;
}
static bool property(const TfpVignette *v, const TfpVignetteRow *r, const char *name, TfpVignetteValue *value, Raised *e) {
  const char *why = NULL;
  memset(value, 0, sizeof *value);
  return v->host->render_data_component_get_property(v->host->user, v->api.fn[TFP_VIGNETTE_FN_GET].token, v->manager, r->entity, r->id, name,
                                                     value, &why) ||
         raised(e, why);
}
/* retail valid(c,r), lines 37-42. */
static bool valid(const TfpVignette *v, const TfpVignetteRow *r, bool *out, Raised *e) {
  double ids[TFP_VIGNETTE_MAX_INSTANCES];
  uint32_t count;
  bool is_alive, found = false;
  TfpVignetteValue mapping;
  *out = false;
  if (!bindings(v, e)) return false;
  if (!alive(v, r->entity, &is_alive, e)) return false;
  if (!is_alive) return true;
  if (!instances(v, r->entity, ids, &count, e)) return false;
  for (uint32_t i = 0; i < count; i++) if (ids[i] == r->id) found = true;
  if (!found) return true;
  if (!property(v, r, "shading_environment_mapping", &mapping, e)) return false;
  *out = mapping.kind == TFP_VIGNETTE_VALUE_TEXT && mapping.text_len == sizeof TFP_VIGNETTE_MAPPING - 1 &&
         memcmp(mapping.text, TFP_VIGNETTE_MAPPING, mapping.text_len) == 0;
  return true;
}
/* retail get(c,r), lines 43-46. */
static bool get(const TfpVignette *v, const TfpVignetteRow *r, bool *out, Raised *e) {
  TfpVignetteValue value;
  if (!property(v, r, "vignette_enabled", &value, e)) return false;
  if (value.kind != TFP_VIGNETTE_VALUE_BOOLEAN) return fail(e, 45, "Vignette enabled property unavailable");
  *out = value.boolean;
  return true;
}
/* retail write(c,r,v), lines 47-51. */
static bool write_value(const TfpVignette *v, const TfpVignetteRow *r, bool value, Raised *e) {
  const TfpVignetteHost *h = v->host;
  const char *why = NULL;
  uint64_t world;
  bool ok, refused = false, now;
  if (!bindings(v, e)) return false;
  if (!main_world(v, v->api.fn[TFP_VIGNETTE_FN_MAIN_WORLD].token, &world, e)) return false;
  if (world != v->world) return fail(e, 48, "Vignette owner changed before write");
  if (!valid(v, r, &ok, e)) return false;
  if (!ok) return fail(e, 48, "Vignette owner changed before write");
  if (!h->render_data_component_set_property(h->user, v->api.fn[TFP_VIGNETTE_FN_SET].token, v->manager, r->entity, r->id, "vignette_enabled", value,
                                             &refused, &why))
    return raised(e, why);
  if (refused) return fail(e, 49, "Vignette setter refused");
  if (!get(v, r, &now, e)) return false;
  if (now != value) return fail(e, 50, "Vignette property readback differs");
  return true;
}

void tfp_vignette_init(TfpVignette *v, const TfpVignetteHost *host) {
  memset(v, 0, sizeof *v);
  v->host = host;
  v->phase = TFP_VIGNETTE_IDLE;
}
bool tfp_vignette_is_clean(const TfpVignette *v) {
  for (uint32_t i = 0; i < v->row_count; i++) if (v->rows[i].owned) return false;
  return true;
}

/* The protected body of stop(), retail lines 57-64. */
static bool release(TfpVignette *v, Raised *e) {
  uint64_t world;
  if (!bindings(v, e)) return false;
  /* Entity handles from a destroyed or replaced world must never be reused. */
  if (!main_world(v, v->api.fn[TFP_VIGNETTE_FN_MAIN_WORLD].token, &world, e)) return false;
  if (world != v->world) { v->row_count = 0; return true; }
  if (foreign(v, false)) return tfp_vignette_is_clean(v) || fail(e, 60, "Another camera owns the view");
  for (uint32_t i = 0; i < v->row_count; i++) {
    TfpVignetteRow *r = &v->rows[i];
    bool ok, enabled;
    if (!r->owned) continue;
    if (!valid(v, r, &ok, e)) return false;
    if (ok) {
      if (!get(v, r, &enabled, e)) return false;
      if (!enabled) {
        if (!write_value(v, r, r->before, e)) return false;
        v->restores++;
      }
    }
    r->owned = false;
  }
  return true;
}
bool tfp_vignette_stop(TfpVignette *v, const char **why) {
  Raised e;
  if (why) *why = NULL;
  v->has_scan = false;
  if (!v->has_context) { report(v, TFP_VIGNETTE_IDLE, NULL); return true; }
  if (!release(v, &e)) {
    report(v, TFP_VIGNETTE_CLEANUP_PENDING, text_of(&e));
    if (why) *why = v->error_message;
    return false;
  }
  v->row_count = 0; v->has_context = false; v->next_scan = 0; v->next_poll = 0;
  report(v, TFP_VIGNETTE_IDLE, NULL);
  return true;
}
bool tfp_vignette_shutdown(TfpVignette *v, const char **why) {
  v->closing = true;
  return tfp_vignette_stop(v, why);
}

/* retail active(), lines 70-75; a raise inside it is false (update stops either way). */
static bool active(const TfpVignette *v) {
  const TfpVignetteHost *h = v->host;
  TfpVignetteSnapshot s = {false, false, 0};
  TfpVignetteControllerState state = {false};
  const char *why = NULL;
  if (v->closing || foreign(v, true)) return false;
  if (h->active_view) return h->active_view(h->user);
  if (!h->backend_player_snapshot(h->user, &s, &why)) return false;
  h->controller_result(h->user, &state);
  return s.present && s.safe && s.camera_kind == 6 && h->controller_is_enabled(h->user) && state.pose_offset;
}

/* One entity of the inventory, retail lines 92-98. */
static bool inspect(TfpVignette *v, uint64_t entity, Raised *e) {
  double ids[TFP_VIGNETTE_MAX_INSTANCES];
  uint32_t count;
  bool is_alive;
  if (!alive(v, entity, &is_alive, e)) return false;
  if (!is_alive) return true;
  if (!instances(v, entity, ids, &count, e)) return false;
  for (uint32_t i = 0; i < count; i++) {
    double id = ids[i], wraps, low;
    TfpVignetteRow r = {entity, id, false, false};
    bool known = false, ok, enabled;
    /* id % 4294967296 as Lua computes it: a - floor(a/b)*b. */
    wraps = floor(id / 4294967296.0);
    wraps = wraps * 4294967296.0;
    low = id - wraps;
    if (!(low == (double)TFP_VIGNETTE_CLASS)) continue;
    for (uint32_t p = 0; p < v->row_count; p++)
      if (v->rows[p].entity == entity && v->rows[p].id == id) { known = true; break; }
    if (known) continue;
    if (!valid(v, &r, &ok, e)) return false;
    if (!ok) continue;
    if (!(v->row_count < TFP_VIGNETTE_MAX_ROWS)) return fail(e, 96, "Vignette profile count changed");
    if (!get(v, &r, &enabled, e)) return false;
    v->rows[v->row_count++] = r;
  }
  return true;
}

/* The protected body of update(), retail lines 80-114. */
static bool work(TfpVignette *v, Raised *e) {
  const TfpVignetteHost *h = v->host;
  const char *why = NULL;
  TfpVignetteApi a;
  uint64_t world;
  double now = NAN;
  if (!h->clock(h->user, &now, &why)) return raised(e, why);
  if (!(now == now)) return fail(e, 80, "Vignette clock unavailable");
  if (!api(v, &a, e)) return false;
  if (!main_world(v, a.fn[TFP_VIGNETTE_FN_MAIN_WORLD].token, &world, e)) return false;
  if (!world) return fail(e, 81, "Vignette world unavailable");
  /* Another world: the rows and the inventory of the old one are dropped, nothing is written to it. */
  if (v->has_context && v->world != world) { v->row_count = 0; v->has_context = false; v->has_scan = false; }
  if (!v->has_context) {
    uint64_t manager = 0;
    if (!h->entity_manager_render_data_component(h->user, a.fn[TFP_VIGNETTE_FN_MANAGER].token, world, &manager, &why)) return raised(e, why);
    if (!manager) return fail(e, 83, "Vignette provider unavailable");
    v->api = a; v->world = world; v->manager = manager; v->has_context = true;
  }
  if (!bindings(v, e)) return false;
  if (now >= v->next_scan && !v->has_scan) {
    TfpVignetteList kind = TFP_VIGNETTE_LIST_NIL;
    uint32_t count = 0;
    if (!h->world_entities(h->user, a.fn[TFP_VIGNETTE_FN_ENTITIES].token, world, v->scan_entities, TFP_VIGNETTE_MAX_ENTITIES, &kind, &count, &why))
      return raised(e, why);
    if (kind == TFP_VIGNETTE_LIST_NIL) return fail(e, 86, "Vignette entities unavailable");
    if (!(kind == TFP_VIGNETTE_LIST_TABLE && count <= TFP_VIGNETTE_MAX_ENTITIES)) return fail(e, 86, "Vignette entity count changed");
    v->scan_count = count; v->scan_index = 1; v->has_scan = true;
    v->next_scan = now + 1;
  }
  if (v->has_scan) {
    for (int n = 0; n < TFP_VIGNETTE_SCAN_SLICE; n++) {
      uint64_t entity = v->scan_index <= v->scan_count ? v->scan_entities[v->scan_index - 1] : 0;
      if (!entity) { v->has_scan = false; break; }
      v->scan_index++;
      if (!inspect(v, entity, e)) return false;
    }
  }
  if (now >= v->next_poll) {
    v->next_poll = now + .1;
    for (uint32_t i = v->row_count; i >= 1; i--) {
      TfpVignetteRow *r = &v->rows[i - 1];
      bool ok, enabled;
      if (!valid(v, r, &ok, e)) return false;
      if (!ok) {
        memmove(r, r + 1, (v->row_count - i) * sizeof *r);
        v->row_count--;
        continue;
      }
      if (!get(v, r, &enabled, e)) return false;
      if (!enabled) continue;
      if (r->owned) v->reassertions++;
      else { r->before = true; r->owned = true; }
      /* Acquire before the setter, which can throw after changing the value. */
      if (foreign(v, true)) return fail(e, 109, "Another camera owns the view");
      if (!write_value(v, r, false, e)) return false;
      v->writes++;
    }
  }
  report(v, v->has_scan ? TFP_VIGNETTE_DISCOVERING : v->row_count > 0 ? TFP_VIGNETTE_ACTIVE : TFP_VIGNETTE_UNAVAILABLE,
         v->row_count == 0 && !v->has_scan ? "No vignette components found" : NULL);
  return true;
}
bool tfp_vignette_update(TfpVignette *v, const char **why) {
  Raised e;
  if (!active(v)) return tfp_vignette_stop(v, why);
  if (why) *why = NULL;
  if (!work(v, &e)) {
    report(v, TFP_VIGNETTE_UNAVAILABLE, text_of(&e));
    if (why) *why = v->error_message;
  }
  return true;
}

void tfp_vignette_result(const TfpVignette *v, TfpVignetteResult *out) {
  out->phase = v->phase;
  out->disabled = v->phase == TFP_VIGNETTE_ACTIVE;
  out->owned = !tfp_vignette_is_clean(v);
  out->profiles = v->row_count; out->writes = v->writes; out->restores = v->restores; out->reassertions = v->reassertions;
  out->error_message = v->has_error ? v->error_message : NULL;
  out->cleanup_pending = v->phase == TFP_VIGNETTE_CLEANUP_PENDING;
}
const char *tfp_vignette_phase_name(TfpVignettePhase phase) {
  static const char *const names[] = {"idle", "discovering", "active", "unavailable", "cleanup_pending"};
  return names[phase];
}
const char *tfp_vignette_status(const TfpVignette *v, char *out, size_t size) {
  snprintf(out, size, "%s; environment vignette disabled=%s; profiles=%u; %s", tfp_vignette_phase_name(v->phase),
           v->phase == TFP_VIGNETTE_ACTIVE ? "true" : "false", v->row_count, v->has_error ? v->error_message : "");
  return out;
}
