/* The effect modules behind the runtime host (src/wire_effects.c) and their engine half
 * (src/wire_win_effects.c), without a game:
 *   1. the build constants: the 25480438 tables against the two image files (skipped with a line when
 *      a file is not there), the engine function table against the census file, `--constants` prints
 *      the tables for tools/verify_effects_constants.py;
 *   2. the engine half over a scripted bridge (eng_call and friends): paths, signatures, arguments,
 *      handles, references;
 *   3. the load checks: which module is allowed on, and the line that says why not;
 *   4. the whole binder with no game behind it: enable, frames, the menu's four marker calls, the
 *      settings conversions, disable;
 *   5. depth of field and vignette driven through the real units, glue and engine half; the movement
 *      keyboard path; the one-byte write rule of the native aim option. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wire_effects.h"
#include "wire_win_effects.h"
#include "wire_off.h"

static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { printf("wire effects: FAILED %s (line %d)\n", #condition, __LINE__); return 1; } } while (0)

static char lines[200][420];
static int line_count;
static void capture(void *user, const char *text) {
  (void)user;
  if (line_count < 200) snprintf(lines[line_count++], sizeof lines[0], "%s", text);
}
static bool logged(const char *part) {
  for (int i = 0; i < line_count; i++) if (strstr(lines[i], part)) return true;
  return false;
}
static int logged_count(const char *part) {
  int n = 0;
  for (int i = 0; i < line_count; i++) n += strstr(lines[i], part) != NULL;
  return n;
}

/* ======================================================================================== */
/* A scripted engine bridge: the functions of src/engine.h that wire_win_effects.c uses.       */
typedef int EngRef;
enum { T_NIL = 0, T_BOOLEAN = 1, T_LIGHT = 2, T_NUMBER = 3, T_STRING = 4, T_TABLE = 5, T_USERDATA = 7 };
typedef struct LVal { int type; double number; bool boolean; const char *string; uintptr_t pointer; int first, count; } LVal;
static LVal vals[512];
enum { TEMPORARY = 256 };              /* values from here on are one call's results */
static int kept_values = 1, temporary_values = TEMPORARY; /* 0 is nil */
static bool in_call;
static int elems[4096], kept_elems, temporary_elems = 2048;
static int new_value(void) {
  int at = in_call ? temporary_values++ : kept_values++;
  if (at >= (in_call ? 512 : TEMPORARY)) abort();
  return at;
}
static int registry[4096];             /* slot -> value; 0: free */
static int live_refs;
static uint64_t eng_generation_now = 1;
static bool eng_up = true;
static const uint64_t EXE = UINT64_C(0x7ff700000000);
static const char *moved_path, *missing_path;

static int v_bool(bool b) { int at = new_value(); vals[at] = (LVal){T_BOOLEAN, 0, b, 0, 0, 0, 0}; return at; }
static int v_num(double n) { int at = new_value(); vals[at] = (LVal){T_NUMBER, n, false, 0, 0, 0, 0}; return at; }
static int v_str(const char *s) { int at = new_value(); vals[at] = (LVal){T_STRING, 0, false, s, 0, 0, 0}; return at; }
static int v_obj(int type, uintptr_t p) { int at = new_value(); vals[at] = (LVal){type, 0, false, 0, p, 0, 0}; return at; }
static int v_table(const int *items, int count) {
  int at = new_value(), *next = in_call ? &temporary_elems : &kept_elems;
  if (*next + count > (in_call ? 4096 : 2048)) abort();
  vals[at] = (LVal){T_TABLE, 0, false, 0, (uintptr_t)(0x900000 + at), *next, count};
  for (int i = 0; i < count; i++) elems[(*next)++] = items[i];
  return at;
}
static EngRef new_ref(int value) {
  if (!value) return 0;
  for (int i = 1; i < 4096; i++)
    if (!registry[i]) { registry[i] = value; live_refs++; return i | (int)(eng_generation_now << 16); }
  abort();
}
static int value_of(EngRef ref) { /* -1: a dead reference */
  if (!ref) return 0;
  if ((uint64_t)(ref >> 16) != eng_generation_now) return -1;
  return registry[ref & 0xffff] ? registry[ref & 0xffff] : -1;
}
static void bump_generation(void) { eng_generation_now++; memset(registry, 0, sizeof registry); live_refs = 0; }

bool eng_ready(void) { return eng_up; }
const char *eng_status(void) { return eng_up ? "ready" : "the stingray table is not available yet"; }
const char *eng_error(void) { return "scripted failure"; }
uint64_t eng_state_generation(void) { return eng_generation_now; }
static const char *const tables[] = {"Application", "World", "EntityManager", "RenderDataComponent", "Keyboard", "Window"};
uintptr_t eng_cfunction(const char *path) {
  if (!eng_up) return 0;
  for (int i = 0; i < TFP_FX_FUNCTIONS; i++) {
    if (strcmp(path, tfp_fx_functions[i].path)) continue;
    if (missing_path && !strcmp(path, missing_path)) return 0;
    return (uintptr_t)(EXE + tfp_fx_functions[i].rva + (moved_path && !strcmp(path, moved_path) ? 0x10 : 0));
  }
  return 0;
}
bool eng_has(const char *path) {
  if (!eng_up) return false;
  for (size_t i = 0; i < sizeof tables / sizeof tables[0]; i++) if (!strcmp(path, tables[i])) return true;
  return eng_cfunction(path) != 0;
}
void eng_unref(EngRef *ref) {
  if (*ref && value_of(*ref) > 0) { registry[*ref & 0xffff] = 0; live_refs--; }
  *ref = 0;
}
bool eng_same(EngRef a, EngRef b) {
  int x = value_of(a), y = value_of(b);
  if (x <= 0 || y <= 0) return a == b;
  if (vals[x].type != vals[y].type) return false;
  switch (vals[x].type) {
    case T_BOOLEAN: return vals[x].boolean == vals[y].boolean;
    case T_NUMBER: return vals[x].number == vals[y].number;
    case T_STRING: return !strcmp(vals[x].string, vals[y].string);
    default: return vals[x].pointer == vals[y].pointer;
  }
}
EngRef eng_copy(EngRef ref) { int v = value_of(ref); return v > 0 ? new_ref(v) : 0; }
uintptr_t eng_pointer(EngRef ref) {
  int v = value_of(ref);
  return v > 0 && (vals[v].type == T_LIGHT || vals[v].type == T_USERDATA || vals[v].type == T_TABLE) ? vals[v].pointer : 0;
}
int eng_len(EngRef table) { int v = value_of(table); return v > 0 && vals[v].type == T_TABLE ? vals[v].count : 0; }
bool eng_index(EngRef table, int i, EngRef *out) {
  int v = value_of(table);
  *out = 0;
  if (v <= 0 || vals[v].type != T_TABLE) return false;
  if (i >= 1 && i <= vals[v].count) *out = new_ref(elems[vals[v].first + i - 1]);
  return true;
}
int eng_type(EngRef ref) { int v = value_of(ref); return v < 0 ? -1 : vals[v].type; }
bool eng_tonumber(EngRef ref, double *out) { int v = value_of(ref); if (v <= 0 || vals[v].type != T_NUMBER) return false; *out = vals[v].number; return true; }
bool eng_toboolean(EngRef ref, bool *out) { int v = value_of(ref); if (v <= 0 || vals[v].type != T_BOOLEAN) return false; *out = vals[v].boolean; return true; }
bool eng_tostring(EngRef ref, char *out, size_t size) {
  int v = value_of(ref);
  if (v <= 0 || vals[v].type != T_STRING) return false;
  snprintf(out, size, "%s", vals[v].string);
  return true;
}

typedef struct EArg { char kind; double n; int i; const char *s; int value; } EArg;
typedef struct ECall { char path[48], sig[16]; EArg a[6]; int count; } ECall;
static ECall calls[400];
static int call_count;
static const char *fail_path;          /* this function raises */
/* the scripted world */
static struct {
  bool dof, vignette_setting, refuse_set, instances_as_table, cursor_nil;
  int world, manager, list, entity, mapping, row_enabled_true, row_enabled_false, key, button;
  double row_id;
  bool row_enabled;
  int script_errors, set_property_calls;
} G;
static bool script(const ECall *c, int *results, int *count) {
  const char *p = c->path;
  *count = 0;
  if (fail_path && !strcmp(p, fail_path)) return false;
  if (!strcmp(p, "Application.render_setting")) {
    results[(*count)++] = !strcmp(c->a[0].s, "dof_enabled") ? v_bool(G.dof) : !strcmp(c->a[0].s, "vignette_enabled") ? v_bool(G.vignette_setting) : 0;
  } else if (!strcmp(p, "Application.set_render_setting")) {
    if (G.refuse_set) { results[(*count)++] = v_bool(false); return true; }
    bool value = !strcmp(c->a[1].s, "true");
    if (strcmp(c->a[1].s, "true") && strcmp(c->a[1].s, "false")) G.script_errors++;
    if (!strcmp(c->a[0].s, "dof_enabled")) G.dof = value; else G.vignette_setting = value;
  } else if (!strcmp(p, "Application.main_world")) results[(*count)++] = G.world;
  else if (!strcmp(p, "EntityManager.render_data_component")) {
    if (c->a[0].value != G.world) G.script_errors++;
    results[(*count)++] = G.manager;
  } else if (!strcmp(p, "World.entities")) {
    if (c->a[0].value != G.world) G.script_errors++;
    results[(*count)++] = G.list;
  } else if (!strcmp(p, "EntityManager.alive")) results[(*count)++] = v_bool(c->a[0].value > 0);
  else if (!strcmp(p, "RenderDataComponent.instances")) {
    if (c->a[0].value != G.manager) G.script_errors++;
    int ids[2] = {v_num(77), v_num(G.row_id)};
    int n = c->a[1].value == G.entity ? 2 : 1;
    if (G.instances_as_table) results[(*count)++] = v_table(ids, n);
    else for (int i = 0; i < n; i++) results[(*count)++] = ids[i];
  } else if (!strcmp(p, "RenderDataComponent.get_property")) {
    if (c->a[0].value != G.manager || c->a[1].value != G.entity || c->a[2].n != G.row_id) G.script_errors++;
    if (!strcmp(c->a[3].s, "shading_environment_mapping")) results[(*count)++] = G.mapping;
    else if (!strcmp(c->a[3].s, "vignette_enabled")) results[(*count)++] = G.row_enabled ? G.row_enabled_true : G.row_enabled_false;
  } else if (!strcmp(p, "RenderDataComponent.set_property")) {
    if (c->a[0].value != G.manager || c->a[1].value != G.entity || c->a[2].n != G.row_id || strcmp(c->a[3].s, "vignette_enabled")) G.script_errors++;
    G.row_enabled = c->a[4].i != 0;
    G.set_property_calls++;
  } else if (!strcmp(p, "Keyboard.button_index") || !strcmp(p, "Keyboard.button_id")) {
    if (strcmp(c->a[0].s, "s")) G.script_errors++;
    results[(*count)++] = G.key;
  } else if (!strcmp(p, "Keyboard.button")) {
    if (c->a[0].n != 31) G.script_errors++;
    results[(*count)++] = G.button;
  } else if (!strcmp(p, "Window.mouse_focus")) results[(*count)++] = v_bool(true);
  else if (!strcmp(p, "Window.show_cursor")) { if (!G.cursor_nil) results[(*count)++] = v_bool(false); }
  else G.script_errors++;
  return true;
}
bool eng_call(const char *path, const char *sig, ...) {
  ECall c;
  va_list args;
  const char *s = sig;
  int results[8], count = 0, at = 0;
  memset(&c, 0, sizeof c);
  snprintf(c.path, sizeof c.path, "%s", path);
  snprintf(c.sig, sizeof c.sig, "%s", sig);
  va_start(args, sig);
  for (; *s && *s != '>'; s++) {
    EArg *a = &c.a[c.count++];
    a->kind = *s;
    switch (*s) {
      case 'n': a->n = va_arg(args, double); break;
      case 'i': case 'b': a->i = va_arg(args, int); break;
      case 's': case 'p': a->s = va_arg(args, const char *); break;
      case 'r': a->value = value_of(va_arg(args, EngRef)); break;
      default: abort();
    }
  }
  if (call_count < 400) calls[call_count++] = c;
  temporary_values = TEMPORARY, temporary_elems = 2048; /* the last call's results were read and released by now */
  in_call = true;
  if (!script(&c, results, &count)) { in_call = false; va_end(args); return false; }
  for (s += *s == '>'; *s; s++) {
    int value = at < count ? results[at] : 0;
    switch (*s) {
      case 'r': *va_arg(args, EngRef *) = new_ref(value); at++; break;
      case 'b': *va_arg(args, int *) = value && !(vals[value].type == T_BOOLEAN && !vals[value].boolean); at++; break;
      case 'R': *va_arg(args, EngRef *) = new_ref(v_table(results + (at < count ? at : count), at < count ? count - at : 0)); at = count; break;
      default: abort();
    }
  }
  in_call = false;
  va_end(args);
  return true;
}
static const ECall *last_call(void) { return &calls[call_count - 1]; }
static bool called(const char *path) {
  for (int i = 0; i < call_count; i++) if (!strcmp(calls[i].path, path)) return true;
  return false;
}
static void world_reset(void) {
  int entities[4];
  memset(&G, 0, sizeof G);
  kept_values = 1, kept_elems = 0;
  G.dof = true, G.vignette_setting = true, G.row_enabled = true;
  G.world = v_obj(T_LIGHT, 0x1000), G.manager = v_obj(T_USERDATA, 0x2000);
  entities[0] = v_obj(T_LIGHT, 0x3001), entities[1] = v_obj(T_LIGHT, 0x3003), entities[2] = v_num(4242), entities[3] = v_bool(false);
  G.entity = entities[1];
  G.list = v_table(entities, 4);
  G.mapping = v_str(TFP_VIGNETTE_MAPPING);
  G.row_enabled_true = v_bool(true), G.row_enabled_false = v_bool(false);
  G.row_id = 5.0 * 4294967296.0 + (double)TFP_VIGNETTE_CLASS;
  G.key = v_num(31), G.button = v_num(1);
  fail_path = NULL;
  call_count = 0;
}

/* ======================================================================================== */
/* The Windows half the host has not: page queries, the write, the widget setter.             */
static uint8_t heap[64];
static const uint64_t HEAP = UINT64_C(0x20000000);
static struct { uint32_t state, kind, protect; int write_result; size_t write_count; uint64_t function, widget; uint8_t shown; unsigned widget_calls, queries; } win;
static size_t t_query(void *u, uint64_t address, void *region, size_t size) {
  TfpMovementRegion *r = region;
  (void)u;
  win.queries++;
  if (address < HEAP || address >= HEAP + sizeof heap || size != sizeof *r) return 0;
  memset(r, 0, sizeof *r);
  r->base = HEAP, r->allocation = HEAP, r->size = sizeof heap, r->state = win.state, r->protect = win.protect, r->kind = win.kind;
  return sizeof *r;
}
static int t_write(void *u, uint64_t address, const uint8_t *bytes, size_t size, size_t *written) {
  (void)u;
  *written = win.write_count;
  if (!win.write_result) return 0;
  memcpy(heap + (address - HEAP), bytes, win.write_count < size ? win.write_count : size);
  return 1;
}
static bool t_widget(void *u, uint64_t function, uint64_t widget, uint8_t shown, const char **error) {
  (void)u; (void)error;
  win.function = function, win.widget = widget, win.shown = shown, win.widget_calls++;
  return true;
}
static void platform_of(TfpFxPlatform *p) {
  tfp_win_effects_platform(p, EXE);
  p->virtual_query = t_query, p->write_process_memory = t_write, p->set_widget_shown = t_widget;
}

/* ======================================================================================== */
/* A binder host with no game behind it (as tests/wire_glue_test.c).                           */
static double now_seconds = 100;
static uint64_t no_module(void *u, TfpOpsModule which) { (void)u; (void)which; return 0; }
static bool heap_blind;
static bool heap_read(void *u, uint64_t at, void *out, size_t size) {
  (void)u;
  if (heap_blind || at < HEAP || at + size > HEAP + sizeof heap) return false;
  memcpy(out, heap + (at - HEAP), size);
  return true;
}
static bool no_write(void *u, uint64_t at, const void *bytes, size_t size) { (void)u; (void)at; (void)bytes; (void)size; return false; }
static void no_query(void *u, uint64_t at, TfpOpsRegion *out) { (void)u; (void)at; memset(out, 0, sizeof *out); }
static bool no_call(void *u, TfpOpsNative which, uint64_t address, const uint64_t args[4], uint64_t *result, TfpOpsText *raised) {
  (void)u; (void)which; (void)address; (void)args; (void)result;
  raised->present = true;
  snprintf(raised->text, sizeof raised->text, "no game");
  return false;
}
static double test_clock(void *u) { (void)u; return now_seconds; }
static int test_mouse(void *u, int button) { (void)u; (void)button; return 0; }
static uint32_t h_validate_c(const TfpCameraConfig *c) { (void)c; return 0; }
static uint32_t h_begin_c(const TfpCameraConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_end(uint64_t g) { (void)g; return 1; }
static uint32_t h_stats_c(uint64_t g, TfpCameraStats *s) { (void)g; (void)s; return 0; }
static uint32_t h_error(void) { return 0; }
static uint32_t h_generation(uint64_t g) { (void)g; return 0; }
static uint32_t h_handoff(uint64_t g, float a, float b) { (void)g; (void)a; (void)b; return 0; }
static uint32_t h_u32(uint64_t g, uint32_t v) { (void)g; (void)v; return 0; }
static uint32_t h_validate_f(const TfpFovConfig *c) { (void)c; return 0; }
static uint32_t h_begin_f(const TfpFovConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_bind_f(uint64_t g, uint64_t r, uint32_t w) { (void)g; (void)r; (void)w; return 0; }
static uint32_t h_transition_f(uint64_t g, uint64_t e, float p, uint32_t a) { (void)g; (void)e; (void)p; (void)a; return 0; }
static uint32_t h_stats_f(uint64_t g, TfpFovStats *s) { (void)g; (void)s; return 0; }
static uint32_t h_begin_n(const TfpNearConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_set_n(uint64_t g, float a, float b) { (void)g; (void)a; (void)b; return 0; }
static uint32_t h_stats_n(uint64_t g, TfpNearStats *s) { (void)g; (void)s; return 0; }
static uint32_t h_validate_b(const TfpBlurConfig *c) { (void)c; return 0; }
static uint32_t h_begin_b(const TfpBlurConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_stats_b(uint64_t g, TfpBlurStats *s) { (void)g; (void)s; return 0; }

static TfpWire wire;
static TfpFx fx;
static bool text_has(TfpRtValue v, const char *part) {
  char text[400];
  if (v.kind != TFP_RT_TEXT) return false;
  snprintf(text, sizeof text, "%.*s", (int)(v.length < 399 ? v.length : 399), v.text);
  return strstr(text, part) != NULL;
}

/* the settings store's side of the marker table, scripted */
static TfpRtValue config_markers, config_show;
static TfpRtValue t_config_field(TfpRtHost *h, const char *name) {
  (void)h;
  return !strcmp(name, "aim_markers") ? config_markers : !strcmp(name, "show_aim_markers") ? config_show : tfp_wire_nil();
}
static char set_key[32];
static TfpRtValue set_value;
static bool t_mod_set(TfpRtHost *h, const char *key, TfpRtValue value, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h;
  snprintf(set_key, sizeof set_key, "%s", key);
  set_value = value;
  *o1 = tfp_wire_bool(true), *o2 = tfp_wire_nil();
  return true;
}
static const char store_table = 0;
static bool t_decode(TfpWire *w, TfpRtValue value, TfpAimMarkersSaved *out) {
  (void)w;
  if (value.kind != TFP_RT_REF || value.ref != &store_table) return false;
  out->count = 1;
  snprintf(out->keys[0], sizeof out->keys[0], "00000000000000aa");
  return true;
}
static const char *encoded_keys[4];
static size_t encoded_count;
static TfpRtValue t_encode(TfpWire *w, const char *const *keys, size_t count) {
  (void)w;
  encoded_count = count;
  for (size_t i = 0; i < count && i < 4; i++) encoded_keys[i] = keys[i];
  return tfp_wire_ref(&store_table);
}

/* mod-side stand-ins for the driven runs: the view is ours, the executable is where the thunks are */
static bool view_active;
static bool t_active_view(void *u, bool *is_true, const char **error) { (void)error; *is_true = tfp_wire_effects_dof_view(u, view_active); return true; } /* the product's own rule over the test's answer */
static bool vignette_view;
static bool t_vignette_view(void *u) { return tfp_wire_effects_vignette_view(u, view_active && vignette_view); } /* the product's own rule over the test's answer */
static bool t_executable(void *u, uint64_t *exe, const char **error) { (void)u; (void)error; *exe = EXE; return true; }
static const TfpDofBuild *image_build;   /* whose bytes the "executable" holds at this build's two addresses */
static bool t_thunks(void *u, uint64_t address, uint32_t size, uint8_t *out, const char **error) {
  (void)u;
  if (size == 8 && address == EXE + tfp_dof_build_25480438.get_thunk) { memcpy(out, image_build->get_bytes, 8); return true; }
  if (size == 8 && address == EXE + tfp_dof_build_25480438.set_thunk) { memcpy(out, image_build->set_bytes, 8); return true; }
  *error = "unreadable";
  return false;
}
static TfpMovementCall t_no_player(void *u, TfpMovementSnapshot *out, const char **why) { (void)u; (void)out; *why = "no player in the test"; return TFP_MOVEMENT_REFUSED; }

/* ======================================================================================== */
static void code_line(const char *build, const char *module, const char *name, int index, const char *where, uint32_t rva, const uint8_t *bytes,
                      uint32_t size) {
  if (index >= 0) printf("code %s %s %s%d %s %x ", build, module, name, index, where, (unsigned)rva);
  else printf("code %s %s %s %s %x ", build, module, name, where, (unsigned)rva);
  for (uint32_t i = 0; i < size; i++) printf("%02x", bytes[i]);
  printf("\n");
}
static void dump_constants(void) {
  static const char *const names[2] = {"retail", "25480438"};
  const TfpDofBuild *d[2] = {&tfp_dof_build_retail, &tfp_dof_build_25480438};
  const TfpMovementBuild *m[2] = {&tfp_movement_build_retail, &tfp_movement_build_25480438};
  const TfpNativeAimBuild *a[2] = {&tfp_native_aim_build_retail, &tfp_native_aim_build_25480438};
  const TfpAimMarkersBuild *k[2] = {&tfp_aim_markers_build_retail, &tfp_aim_markers_build_25480438};
  for (int b = 0; b < 2; b++) {
    code_line(names[b], "dof", "get_thunk", -1, "exe", d[b]->get_thunk, d[b]->get_bytes, 8);
    code_line(names[b], "dof", "set_thunk", -1, "exe", d[b]->set_thunk, d[b]->set_bytes, 8);
    for (int i = 0; i < 6; i++) code_line(names[b], "movement", "anchor", i, "game", m[b]->anchors[i].at, m[b]->anchors[i].bytes, m[b]->anchors[i].size);
    printf("data %s movement pool game %x\n", names[b], (unsigned)m[b]->pool);
    for (int i = 0; i < 3; i++) code_line(names[b], "native_aim", "witness", i, "game", a[b]->witnesses[i].at, a[b]->witnesses[i].bytes, a[b]->witnesses[i].size);
    printf("data %s native_aim player game %x\ndata %s native_aim weapons game %x\n", names[b], (unsigned)a[b]->player, names[b], (unsigned)a[b]->weapons);
    for (int i = 0; i < 4; i++) code_line(names[b], "aim_markers", "check", i, "game", k[b]->checks[i].at, k[b]->checks[i].bytes, k[b]->checks[i].size);
    printf("data %s aim_markers set_shown game %x\ndata %s aim_markers state game %x\ndata %s aim_markers scene game %x\n", names[b],
           (unsigned)k[b]->set_shown, names[b], (unsigned)k[b]->state, names[b], (unsigned)k[b]->scene);
    printf("data %s aim_markers root game %x\ndata %s aim_markers crosshair game %x\n", names[b], (unsigned)k[b]->root, names[b], (unsigned)k[b]->crosshair);
  }
}
static bool image_is(FILE *f, uint32_t rva, const uint8_t *bytes, uint32_t size) {
  uint8_t found[64];
  return size <= sizeof found && fseek(f, (long)rva, SEEK_SET) == 0 && fread(found, 1, size, f) == size && !memcmp(found, bytes, size);
}
static int32_t image_i32(FILE *f, uint32_t rva) {
  int32_t value = 0;
  if (fseek(f, (long)rva, SEEK_SET) != 0 || fread(&value, 1, 4, f) != 4) return 0;
  return value;
}
/* The 25480438 tables against the image files: 1 failed, 0 passed or skipped. */
static int constants_against_images(const char *game_path, const char *exe_path) {
  FILE *game = fopen(game_path, "rb"), *exe = fopen(exe_path, "rb");
  if (!game || !exe) {
    printf("wire effects: constants against the images skipped: %s not found\n", game ? exe_path : game_path);
    if (game) fclose(game);
    if (exe) fclose(exe);
    return 0;
  }
  const TfpDofBuild *d = &tfp_dof_build_25480438;
  const TfpMovementBuild *m = &tfp_movement_build_25480438;
  const TfpNativeAimBuild *a = &tfp_native_aim_build_25480438;
  const TfpAimMarkersBuild *k = &tfp_aim_markers_build_25480438;
  unsigned before = checks;
  CHECK(image_is(exe, d->get_thunk, d->get_bytes, 8) && image_is(exe, d->set_thunk, d->set_bytes, 8));
  for (int i = 0; i < 6; i++) CHECK(image_is(game, m->anchors[i].at, m->anchors[i].bytes, m->anchors[i].size));
  for (int i = 0; i < 3; i++) CHECK(image_is(game, a->witnesses[i].at, a->witnesses[i].bytes, a->witnesses[i].size));
  for (int i = 0; i < 4; i++) CHECK(image_is(game, k->checks[i].at, k->checks[i].bytes, k->checks[i].size));
  /* anchor 0 is `mov r11,[rip+d]`: the pool global; witness 1 is `mov rax,[rip+d]`: the avatar pool (src/profile.h FP_AVATAR) */
  CHECK(m->anchors[0].at + 7 + (uint32_t)image_i32(game, m->anchors[0].at + 3) == m->pool);
  CHECK(a->witnesses[1].at + 7 + (uint32_t)image_i32(game, a->witnesses[1].at + 3) == 0x3326d20);
  /* check 3 is `lea rcx,[r15+d]` / call / `lea rdx,[r15+d]`: the crosshair's place in the mission HUD */
  CHECK((uint32_t)image_i32(game, k->checks[3].at + 3) == k->crosshair && (uint32_t)image_i32(game, k->checks[3].at + 15) == k->crosshair);
  CHECK(k->checks[0].at == k->set_shown);
  /* game+0x541402: mov rax,[rip+d] (the state global) / cmp dword [rax+scene],4 */
  static const uint8_t load[3] = {0x48, 0x8b, 0x05}, compare[2] = {0x83, 0xb8};
  CHECK(image_is(game, 0x541402, load, 3) && 0x541409 + (uint32_t)image_i32(game, 0x541405) == k->state);
  CHECK(image_is(game, 0x541409, compare, 2) && (uint32_t)image_i32(game, 0x54140b) == k->scene);
  /* game+0xadab8d: mov rax,[rip+d] (the HUD global) / cmp byte [rax+0x24e334],0 */
  static const uint8_t built[2] = {0x80, 0xb8};
  CHECK(image_is(game, 0xadab8d, load, 3) && 0xadab94 + (uint32_t)image_i32(game, 0xadab90) == k->root);
  CHECK(image_is(game, 0xadab94, built, 2) && (uint32_t)image_i32(game, 0xadab96) == 0x24e334);
  /* the two thunks are mov rdx,rcx / jmp */
  static const uint8_t thunk[4] = {0x48, 0x8b, 0xd1, 0xe9};
  CHECK(image_is(exe, d->get_thunk, thunk, 4) && image_is(exe, d->set_thunk, thunk, 4));
  printf("wire effects: the 25480438 constants are the images' (%u checks)\n", checks - before);
  fclose(game);
  fclose(exe);
  return 0;
}
static int functions_against_census(const char *path) {
  FILE *f = fopen(path, "r");
  char row[512];
  unsigned found = 0;
  if (!f) { printf("wire effects: census file not found: %s\n", path); return 1; }
  while (fgets(row, sizeof row, f)) {
    char name[128], module[16];
    int type = 0;
    unsigned rva = 0;
    if (sscanf(row, "%127[^\t]\t%d\t%15[^\t]\t%x", name, &type, module, &rva) != 4) continue;
    for (int i = 0; i < TFP_FX_FUNCTIONS; i++)
      if (!strcmp(name, tfp_fx_functions[i].path)) {
        CHECK(type == 6 && !strcmp(module, "exe") && rva == tfp_fx_functions[i].rva);
        found++;
      }
  }
  fclose(f);
  CHECK(found == TFP_FX_FUNCTIONS);
  return 0;
}

/* One load check on an otherwise empty binder: which lines, which states. */
static void load_case(const TfpFxPlatform *platform, int frames) {
  static TfpFx local;
  memset(&wire, 0, sizeof wire);
  wire.platform.log = capture;
  line_count = 0;
  tfp_wire_effects_bind(&wire, &local, platform, NULL);
  for (int i = 0; i < frames; i++) tfp_wire_effects_frame(&wire);
  fx = local;
}

int main(int argc, char **argv) {
  const char *game_image = getenv("TFP_GAME_IMAGE"), *exe_image = getenv("TFP_EXE_IMAGE"), *census = getenv("TFP_CENSUS");
  if (argc > 1 && !strcmp(argv[1], "--constants")) { dump_constants(); return 0; }
  static char game_default[512], exe_default[512];
  const char *home = getenv("HOME");
  snprintf(game_default, sizeof game_default, "%s/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll", home ? home : "");
  snprintf(exe_default, sizeof exe_default, "%s/Applications/HellForge/mods-retired/localappdata-1019/LocalAppData/LinksLinkToMemory/r6_reload_exe_20261017.bin", home ? home : "");
  if (!game_image) game_image = game_default;
  if (!exe_image) exe_image = exe_default;

  /* ---- 1. constants ---- */
  if (constants_against_images(game_image, exe_image)) return 1;
  if (census && functions_against_census(census)) return 1;
  CHECK(tfp_fx_functions[TFP_FX_FN_RENDER_SETTING].rva == tfp_dof_build_25480438.get_thunk);
  CHECK(tfp_fx_functions[TFP_FX_FN_SET_RENDER_SETTING].rva == tfp_dof_build_25480438.set_thunk);
  for (int i = 0; i < TFP_FX_MODULES; i++) CHECK(tfp_fx_constants[i].module == (TfpFxModule)i && tfp_fx_constants[i].unverified == NULL);
  /* the retail tables are still retail's numbers (the vectors replay them) */
  CHECK(tfp_dof_build_retail.get_thunk == 0x3d43d0 && tfp_movement_build_retail.pool == 0x2ce1eb8 && tfp_native_aim_build_retail.player == 0x2ce1618 &&
        tfp_aim_markers_build_retail.root == 0x2e28788 && tfp_aim_markers_build_retail.set_shown == 0x12e4e20);
  /* this build's HUD global is not the 0.5.34 port's (src/markers.c:8 had game+0x346d518) */
  CHECK(tfp_aim_markers_build_25480438.root == 0x346d538);

  /* ---- 2. the engine half over the scripted bridge ---- */
  TfpFxPlatform p;
  const char *error = NULL;
  int value = -2;
  uint64_t world = 0, world2 = 0, manager = 0, entities[8];
  uint32_t count = 0;
  TfpVignetteList kind = TFP_VIGNETTE_LIST_NIL;
  world_reset();
  platform_of(&p);
  CHECK(p.engine_ready(p.user, &error) && p.engine_generation(p.user) == 1 && p.exe_base(p.user) == EXE);
  CHECK(p.engine_table(p.user, "Application") && p.engine_table(p.user, "Keyboard") && !p.engine_table(p.user, "Nothing") &&
        !p.engine_table(p.user, "Application.render_setting"));
  CHECK(p.engine_function(p.user, "Keyboard.button") == EXE + 0x482480 && p.engine_function(p.user, "Keyboard.nothing") == 0);
  /* depth of field */
  CHECK(p.render_setting(p.user, "dof_enabled", &value, &error) && value == 1);
  CHECK(!strcmp(last_call()->path, "Application.render_setting") && !strcmp(last_call()->sig, "s>r") && !strcmp(last_call()->a[0].s, "dof_enabled"));
  CHECK(p.render_setting(p.user, "something_else", &value, &error) && value == -1); /* nil: not a Boolean */
  CHECK(p.set_render_setting(p.user, "dof_enabled", false, &error) == TFP_FX_SET_DONE && !G.dof);
  CHECK(!strcmp(last_call()->path, "Application.set_render_setting") && !strcmp(last_call()->sig, "ss>R") && !strcmp(last_call()->a[0].s, "dof_enabled") &&
        !strcmp(last_call()->a[1].s, "false"));
  CHECK(p.render_setting(p.user, "dof_enabled", &value, &error) && value == 0);
  CHECK(p.set_render_setting(p.user, "dof_enabled", true, &error) == TFP_FX_SET_DONE && G.dof && !strcmp(last_call()->a[1].s, "true"));
  G.refuse_set = true;
  CHECK(p.set_render_setting(p.user, "dof_enabled", false, &error) == TFP_FX_SET_REFUSED && G.dof);
  G.refuse_set = false;
  fail_path = "Application.set_render_setting";
  error = NULL;
  CHECK(p.set_render_setting(p.user, "dof_enabled", false, &error) == TFP_FX_SET_RAISED && error && strstr(error, "Application.set_render_setting: scripted failure"));
  fail_path = "Application.render_setting";
  CHECK(!p.render_setting(p.user, "dof_enabled", &value, &error) && strstr(error, "Application.render_setting"));
  fail_path = NULL;
  CHECK(live_refs == 0);
  /* worlds and managers: the same object is the same handle */
  CHECK(p.main_world(p.user, &world, &error) && world != 0 && !strcmp(last_call()->sig, ">r"));
  CHECK(p.main_world(p.user, &world2, &error) && world2 == world);
  CHECK(p.render_data_component(p.user, world, &manager, &error) && manager != 0 && !strcmp(last_call()->sig, "r>r") && last_call()->a[0].value == G.world);
  CHECK(!p.render_data_component(p.user, world + 1, &manager, &error) && strstr(error, "not the engine's current world"));
  CHECK(p.render_data_component(p.user, world, &manager, &error) && live_refs == 2);
  /* the entity list: light userdata by address, a number by its bits, false as 0 */
  CHECK(p.world_entities(p.user, world, entities, 8, &kind, &count, &error) && kind == TFP_VIGNETTE_LIST_TABLE && count == 4);
  double number = 4242;
  uint64_t bits;
  memcpy(&bits, &number, 8);
  CHECK(entities[0] == 0x3001 && entities[1] == 0x3003 && entities[2] == bits && entities[3] == 0 && live_refs == 3);
  bool alive = false;
  CHECK(p.entity_alive(p.user, entities[1], &alive, &error) && alive && !strcmp(last_call()->path, "EntityManager.alive") &&
        !strcmp(last_call()->sig, "r>b") && last_call()->a[0].value == G.entity);
  CHECK(!p.entity_alive(p.user, 0x9999, &alive, &error) && strstr(error, "not one the engine handed out"));
  double ids[4] = {0, 0, 0, 0};
  CHECK(p.instances(p.user, manager, entities[1], ids, 4, &count, &error) && count == 2 && ids[0] == 77 && ids[1] == G.row_id);
  CHECK(!strcmp(last_call()->sig, "rr>R") && last_call()->a[0].value == G.manager && last_call()->a[1].value == G.entity);
  G.instances_as_table = true;
  ids[0] = ids[1] = 0;
  CHECK(p.instances(p.user, manager, entities[1], ids, 4, &count, &error) && count == 2 && ids[1] == G.row_id);
  CHECK(p.instances(p.user, manager, entities[0], ids, 1, &count, &error) && count == 1 && ids[0] == 77);
  G.instances_as_table = false;
  CHECK(!p.instances(p.user, manager + 5, entities[1], ids, 4, &count, &error) && strstr(error, "manager handle is stale"));
  TfpVignetteValue property;
  CHECK(p.get_property(p.user, manager, entities[1], G.row_id, "shading_environment_mapping", &property, &error));
  CHECK(property.kind == TFP_VIGNETTE_VALUE_TEXT && property.text_len == strlen(TFP_VIGNETTE_MAPPING) && !strcmp(property.text, TFP_VIGNETTE_MAPPING));
  CHECK(!strcmp(last_call()->sig, "rrnp>r") && last_call()->a[0].value == G.manager && last_call()->a[1].value == G.entity &&
        last_call()->a[2].n == G.row_id && !strcmp(last_call()->a[3].s, "shading_environment_mapping"));
  CHECK(p.get_property(p.user, manager, entities[1], G.row_id, "vignette_enabled", &property, &error) && property.kind == TFP_VIGNETTE_VALUE_BOOLEAN &&
        property.boolean);
  CHECK(p.get_property(p.user, manager, entities[1], G.row_id, "no_such_property", &property, &error) && property.kind == TFP_VIGNETTE_VALUE_NIL);
  bool refused = true;
  CHECK(p.set_property(p.user, manager, entities[1], G.row_id, "vignette_enabled", false, &refused, &error) && !refused && !G.row_enabled);
  CHECK(!strcmp(last_call()->sig, "rrnpb>R") && last_call()->a[4].i == 0 && !strcmp(last_call()->a[3].s, "vignette_enabled"));
  CHECK(p.set_property(p.user, manager, entities[1], G.row_id, "vignette_enabled", true, &refused, &error) && G.row_enabled && last_call()->a[4].i == 1);
  CHECK(G.script_errors == 0 && live_refs == 4); /* world, manager, list, the entity a property was read from */
  /* a row outlives the list it was found in: the kept entity still resolves, a never-read one does not */
  int only[1] = {v_obj(T_LIGHT, 0x3009)};
  G.list = v_table(only, 1);
  CHECK(p.world_entities(p.user, world, entities, 8, &kind, &count, &error) && count == 1 && entities[0] == 0x3009);
  CHECK(p.entity_alive(p.user, 0x3003, &alive, &error) && alive && last_call()->a[0].value == G.entity);
  CHECK(!p.entity_alive(p.user, 0x3001, &alive, &error));
  /* an entity that has no identity here is refused, with its Lua type */
  int odd[2] = {v_obj(T_LIGHT, 0x3009), v_obj(T_USERDATA, 0x4000)};
  G.list = v_table(odd, 2);
  CHECK(!p.world_entities(p.user, world, entities, 8, &kind, &count, &error) && strstr(error, "entry 2 is a value of Lua type 7"));
  G.list = v_str("not a table");
  CHECK(p.world_entities(p.user, world, entities, 8, &kind, &count, &error) && kind == TFP_VIGNETTE_LIST_NOT_A_TABLE);
  G.list = 0;
  CHECK(p.world_entities(p.user, world, entities, 8, &kind, &count, &error) && kind == TFP_VIGNETTE_LIST_NIL);
  /* another world: another handle, and the old one is stale */
  G.world = v_obj(T_LIGHT, 0x1001);
  CHECK(p.main_world(p.user, &world2, &error) && world2 != world && world2 != 0);
  CHECK(!p.render_data_component(p.user, world, &manager, &error));
  CHECK(!p.entity_alive(p.user, 0x3003, &alive, &error)); /* kept entities belonged to the old world */
  G.world = v_bool(false);
  CHECK(p.main_world(p.user, &world, &error) && world == 0);
  G.world = 0;
  CHECK(p.main_world(p.user, &world, &error) && world == 0);
  /* the engine replaced its script state: every handle is forgotten, nothing dead is used */
  world_reset();
  CHECK(p.main_world(p.user, &world, &error) && p.render_data_component(p.user, world, &manager, &error));
  bump_generation();
  CHECK(!p.render_data_component(p.user, world, &manager, &error) && strstr(error, "current world"));
  CHECK(p.main_world(p.user, &world2, &error) && world2 != world && live_refs == 1);
  tfp_win_effects_forget();
  CHECK(live_refs == 0);
  /* the keyboard and the window */
  bool has = false, focus = false, shown = true;
  double key = 0;
  CHECK(p.keyboard_find(p.user, true, "s", &has, &key, &error) && has && key == 31 && !strcmp(last_call()->path, "Keyboard.button_index") &&
        !strcmp(last_call()->sig, "s>r") && !strcmp(last_call()->a[0].s, "s"));
  CHECK(p.keyboard_find(p.user, false, "s", &has, &key, &error) && has && !strcmp(last_call()->path, "Keyboard.button_id"));
  CHECK(p.keyboard_button(p.user, 31, &has, &key, &error) && has && key == 1 && !strcmp(last_call()->sig, "n>r") && last_call()->a[0].n == 31);
  G.key = 0;
  CHECK(p.keyboard_find(p.user, true, "s", &has, &key, &error) && !has);
  G.key = v_str("s");
  CHECK(p.keyboard_find(p.user, true, "s", &has, &key, &error) && !has);
  CHECK(p.window_mouse_focus(p.user, &focus, &error) && focus && !strcmp(last_call()->sig, ">r"));
  CHECK(p.window_show_cursor(p.user, &shown, &error) && !shown);
  G.cursor_nil = true;
  CHECK(p.window_show_cursor(p.user, &shown, &error) && shown); /* only the Boolean false is "hidden" */
  fail_path = "Keyboard.button";
  CHECK(!p.keyboard_button(p.user, 31, &has, &key, &error) && strstr(error, "Keyboard.button: scripted failure"));
  CHECK(G.script_errors == 0 && live_refs == 0);
  unsigned engine_checks = checks;

  /* ---- 3. the load checks ---- */
  world_reset();
  platform_of(&p);
  eng_up = false;
  load_case(&p, 2);
  CHECK(logged("Ready: markers (setter game+0x144cfb0, HUD game+0x346d538, crosshair +0x19aaf8") && logged("Ready: aimopt (player game+0x3326468"));
  CHECK(logged("Off: camera sharing with Photo Mode") && logged_count("Waiting: dof, vignette, movement") == 1 && !logged("Refused"));
  CHECK(fx.state[TFP_FX_DOF] == TFP_FX_WAITING && fx.state[TFP_FX_VIGNETTE] == TFP_FX_WAITING && fx.state[TFP_FX_MOVEMENT] == TFP_FX_WAITING);
  CHECK(fx.state[TFP_FX_MARKERS] == TFP_FX_READY && fx.state[TFP_FX_AIM_OPTION] == TFP_FX_READY);
  char second[160];
  CHECK(tfp_wire_effects_second(&wire, second, sizeof second) == strlen(second) && !strcmp(second, "dof=wait vignette=wait markers=off move=wait aimopt=off"));
  eng_up = true;
  load_case(&p, 3);
  CHECK(logged_count("Ready: dof (Application.render_setting exe+0x3fa3d0, set_render_setting exe+0x3fa120") == 1);
  CHECK(logged_count("Ready: vignette") == 1 && logged_count("Ready: movement (pool game+0x3326cf8") == 1 && !logged("Refused") && !logged("Waiting"));
  CHECK(fx.state[TFP_FX_DOF] == TFP_FX_READY && fx.state[TFP_FX_VIGNETTE] == TFP_FX_READY && fx.state[TFP_FX_MOVEMENT] == TFP_FX_READY && line_count == 6);
  moved_path = "Keyboard.button";
  load_case(&p, 2);
  CHECK(logged("Refused: movement: Keyboard.button is at exe+0x482490, build 25480438 has it at exe+0x482480"));
  CHECK(fx.state[TFP_FX_MOVEMENT] == TFP_FX_REFUSED && fx.state[TFP_FX_DOF] == TFP_FX_READY && fx.state[TFP_FX_VIGNETTE] == TFP_FX_READY &&
        logged_count("Refused") == 1);
  moved_path = NULL, missing_path = "RenderDataComponent.set_property";
  load_case(&p, 2);
  CHECK(logged("Refused: vignette: RenderDataComponent.set_property is not registered (build 25480438 has it at exe+0x466fd0)"));
  CHECK(fx.state[TFP_FX_VIGNETTE] == TFP_FX_REFUSED && fx.state[TFP_FX_DOF] == TFP_FX_READY && fx.state[TFP_FX_MOVEMENT] == TFP_FX_READY);
  missing_path = NULL, moved_path = "Application.render_setting";
  load_case(&p, 2);
  CHECK(logged("Refused: dof: Application.render_setting is at exe+0x3fa3e0, build 25480438 has it at exe+0x3fa3d0"));
  CHECK(logged("Refused: vignette: depth of field is off, and the vignette is switched through it") && logged("Ready: movement"));
  CHECK(fx.state[TFP_FX_DOF] == TFP_FX_REFUSED && fx.state[TFP_FX_VIGNETTE] == TFP_FX_REFUSED);
  moved_path = NULL;
  /* a function that moves when the engine replaces its script state */
  load_case(&p, 2);
  CHECK(fx.state[TFP_FX_MOVEMENT] == TFP_FX_READY);
  {
    static TfpFx local;
    memset(&wire, 0, sizeof wire);
    wire.platform.log = capture;
    line_count = 0;
    tfp_wire_effects_bind(&wire, &local, &p, NULL);
    tfp_wire_effects_frame(&wire);
    moved_path = "Window.show_cursor";
    tfp_wire_effects_frame(&wire);
    CHECK(local.state[TFP_FX_MOVEMENT] == TFP_FX_READY); /* same state: not asked again */
    bump_generation();
    tfp_wire_effects_frame(&wire);
    CHECK(local.state[TFP_FX_MOVEMENT] == TFP_FX_REFUSED && logged("Refused: movement: Window.show_cursor is at exe+0x3ffc30") &&
          logged("the engine replaced its script state (generation"));
    moved_path = NULL;
  }
  /* a build of the mod without the Windows half, or without the bridge */
  TfpFxPlatform bare = p;
  bare.virtual_query = NULL;
  load_case(&bare, 1);
  CHECK(logged("Refused: markers: no VirtualQuery") && logged("Refused: movement: no VirtualQuery") && logged("Refused: aimopt: no VirtualQuery") &&
        logged("Ready: dof"));
  bare = p;
  bare.set_widget_shown = NULL, bare.write_process_memory = NULL;
  load_case(&bare, 1);
  CHECK(logged("Refused: markers: no guarded call of the game's widget setter") && logged("Refused: aimopt: no WriteProcessMemory") &&
        logged("Refused: movement: no WriteProcessMemory"));
  memset(&bare, 0, sizeof bare);
  bare.virtual_query = t_query, bare.write_process_memory = t_write, bare.set_widget_shown = t_widget;
  load_case(&bare, 2);
  CHECK(logged("Refused: dof: no engine bridge") && logged("Refused: vignette: no engine bridge") && logged("Refused: movement: no engine bridge") &&
        logged("Ready: markers") && logged("Ready: aimopt"));
  unsigned load_checks = checks;

  /* ---- 4. the whole binder with no game behind it ---- */
  static const TfpCameraHelperApi camera = {h_validate_c, h_begin_c, h_end, h_stats_c, h_error, h_generation, h_handoff, h_u32, h_u32};
  static const TfpFovHelperApi fov = {h_validate_f, h_begin_f, h_bind_f, h_transition_f, h_end, h_stats_f, h_error};
  static const TfpNearHelperApi near_clip = {h_begin_n, h_set_n, h_end, h_stats_n, h_error};
  static const TfpBlurHelperApi blur = {h_validate_b, h_begin_b, h_end, h_stats_b, h_error};
  TfpOpsHost memory;
  memset(&memory, 0, sizeof memory);
  memory.module = no_module, memory.read = heap_read, memory.write = no_write, memory.query = no_query, memory.call = no_call;
  TfpWirePlatform platform = {NULL, test_clock, test_mouse, capture};
  TfpWireHelpers helpers = {&camera, &fov, &near_clip, &blur};
  TfpFxOptions options = {t_decode, t_encode, NULL, NULL, NULL, NULL};
  TfpRtValue r[30], o1, o2;
  TfpRtTable table;
  TfpRtHost *h = &wire.host;
  world_reset();
  platform_of(&p);
  line_count = 0;
  tfp_wire_init(&wire, &memory, &platform, &helpers);
  TfpRtHost view = wire.host;
  tfp_wire_effects_bind(&wire, &fx, &p, &options);
  CHECK(tfp_wire_effects_of(&wire) == &fx && fx.dof_host.build == &tfp_dof_build_25480438 && fx.markers_host.build == &tfp_aim_markers_build_25480438 &&
        fx.movement_host.build == &tfp_movement_build_25480438 && fx.aim_host.build == &tfp_native_aim_build_25480438);
  /* exactly the wired modules' pointers changed; camera sharing and the two sources are the off answers */
  CHECK(h->dof_update != view.dof_update && h->movement_update != view.movement_update && h->native_aim_update != view.native_aim_update &&
        h->aim_markers_update != view.aim_markers_update && h->aim_markers_fault != view.aim_markers_fault);
  CHECK(h->compat_update == view.compat_update && h->compat_foreign == view.compat_foreign && h->native_aim_source == view.native_aim_source &&
        h->aim_markers_source == view.aim_markers_source && h->head_update == view.head_update && h->near_update == view.near_update);
  /* before the runtime made the modules, the pointers answer what wire_off.c does */
  CHECK(h->dof_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && o2.kind == TFP_RT_NIL);
  CHECK(h->dof_result(h, &table) && table.present && table.f[TFP_RT_DOF_OWNED].kind == TFP_RT_FALSE);
  CHECK(h->movement_result(h, &table) && text_has(table.f[TFP_RT_TARGET_PHASE], "idle"));
  CHECK(h->aim_markers_toggle(h, tfp_wire_nil(), tfp_wire_nil(), &o1, &o2) && o1.kind == TFP_RT_FALSE && text_has(o2, TFP_OFF_MARKERS));
  h->config_field = t_config_field;
  config_markers = tfp_wire_nil(), config_show = tfp_wire_nil();
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_enable, NULL, r) == TFP_RT_OK);
  CHECK(fx.made[TFP_FX_DOF] && fx.made[TFP_FX_VIGNETTE] && fx.made[TFP_FX_MARKERS] && fx.made[TFP_FX_MOVEMENT] && fx.made[TFP_FX_AIM_OPTION]);
  call_count = 0;
  for (int i = 0; i < 5; i++) {
    now_seconds += 1 / 60.0;
    tfp_wire_frame(&wire, NULL);
    tfp_wire_effects_frame(&wire);
  }
  CHECK(logged("Ready: dof") && logged("Ready: vignette") && logged("Ready: markers") && logged("Ready: movement") && logged("Ready: aimopt"));
  CHECK(!logged("Refused") && !logged("Effect:") && logged_count("Ready:") == 5);
  CHECK(wire.rt.idle_clean.kind == TFP_RT_TRUE && call_count == 0); /* idle: clean, and the engine is not asked anything */
  CHECK(tfp_wire_effects_second(&wire, second, sizeof second) && !strcmp(second, "dof=idle vignette=idle markers=shown move=idle aimopt=off"));
  CHECK(tfp_wire_effects_second(&wire, second, 8) == 7 && !strcmp(second, "dof=idl"));
  /* the units answer the runtime what the off answers did */
  CHECK(h->dof_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && o2.kind == TFP_RT_NIL);
  CHECK(h->dof_result(h, &table) && table.present && table.ref == &fx.dof && table.f[TFP_RT_DOF_OWNED].kind == TFP_RT_FALSE);
  CHECK(h->dof_is_clean(h, &o1) && o1.kind == TFP_RT_TRUE && h->dof_stop(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && o2.kind == TFP_RT_NIL);
  CHECK(h->dof_status(h, &o1) && text_has(o1, "idle"));
  CHECK(h->movement_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && h->movement_is_clean(h, &o1) && o1.kind == TFP_RT_TRUE);
  CHECK(h->movement_result(h, &table) && table.present && text_has(table.f[TFP_RT_TARGET_PHASE], "idle") && table.f[TFP_RT_TARGET_ARMED].kind == TFP_RT_NIL);
  CHECK(h->movement_stop(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && o2.kind == TFP_RT_NIL);
  CHECK(h->native_aim_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && h->native_aim_is_clean(h, &o1) && o1.kind == TFP_RT_TRUE);
  CHECK(h->native_aim_result(h, &table) && table.present && h->native_aim_stop(h, &o1, &o2) && o1.kind == TFP_RT_TRUE);
  CHECK(h->aim_markers_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && o2.kind == TFP_RT_NIL);
  CHECK(h->aim_markers_is_clean(h, &o1) && o1.kind == TFP_RT_TRUE && h->aim_markers_result(h, &table) && table.present);
  CHECK(h->aim_markers_fault(h).kind == TFP_RT_NIL);
  /* the menu's four marker calls: values to texts and Booleans */
  CHECK(h->aim_markers_target(h, &o1, &o2) && o1.kind == TFP_RT_NIL && o2.kind == TFP_RT_NIL);       /* no weapon in view */
  CHECK(h->aim_markers_setting(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && o2.kind == TFP_RT_NIL);      /* unset: the game's markers */
  CHECK(h->aim_markers_toggle(h, tfp_wire_text("00000000000000aa"), tfp_wire_text("W:1"), &o1, &o2) && o1.kind == TFP_RT_FALSE &&
        text_has(o2, "Weapon changed; select the aiming-marker setting again"));
  CHECK(h->aim_markers_toggle(h, tfp_wire_number(5), tfp_wire_nil(), &o1, &o2) && o1.kind == TFP_RT_FALSE && text_has(o2, "Weapon changed"));
  CHECK(h->aim_markers_set_setting(h, tfp_wire_number(1), &o1, &o2) && o1.kind == TFP_RT_FALSE && text_has(o2, "Aim-marker setting must be on or off"));
  CHECK(h->aim_markers_set_setting(h, tfp_wire_bool(false), &o1, &o2) && o1.kind == TFP_RT_FALSE &&
        text_has(o2, "attempt to call field 'set' (a nil value)"));                                   /* no settings store: mod.set is nil */
  h->mod_set = t_mod_set;
  CHECK(h->aim_markers_set_setting(h, tfp_wire_bool(false), &o1, &o2) && o1.kind == TFP_RT_TRUE && !strcmp(set_key, "show_aim_markers") &&
        set_value.kind == TFP_RT_FALSE);
  config_show = tfp_wire_bool(false);
  CHECK(fx.markers_host.global_setting(&fx) == TFP_AIM_MARKERS_HIDE && h->aim_markers_setting(h, &o1, &o2) && o1.kind == TFP_RT_FALSE);
  CHECK(h->aim_markers_toggle(h, tfp_wire_text("00000000000000aa"), tfp_wire_text("W:1"), &o1, &o2) && o1.kind == TFP_RT_FALSE &&
        text_has(o2, "Use Show aim markers to change all weapons"));
  config_show = tfp_wire_bool(true);
  CHECK(fx.markers_host.global_setting(&fx) == TFP_AIM_MARKERS_SHOW);
  config_show = tfp_wire_number(3);
  CHECK(fx.markers_host.global_setting(&fx) == TFP_AIM_MARKERS_UNSET);
  /* the saved table: nil, runtime.lua's {}, the store's own value, and this binder's own */
  TfpAimMarkersSaved saved;
  CHECK(!fx.markers_host.settings(&fx, &saved) && saved.count == 0);
  config_markers = tfp_wire_ref(&tfp_rt_empty_table);
  CHECK(fx.markers_host.settings(&fx, &saved) && saved.count == 0);
  config_markers = tfp_wire_ref(&store_table);
  CHECK(fx.markers_host.settings(&fx, &saved) && saved.count == 1 && !strcmp(saved.keys[0], "00000000000000aa"));
  const char keys[2][TFP_AIM_MARKERS_KEY] = {"00000000000000aa", "00000000000000bb"};
  const char *why = "x";
  CHECK(fx.markers_host.save(&fx, keys, 2, &why) && why == NULL && !strcmp(set_key, "aim_markers") && set_value.kind == TFP_RT_REF &&
        set_value.ref == &store_table && encoded_count == 2 && !strcmp(encoded_keys[1], "00000000000000bb"));
  fx.options.markers_value = NULL, fx.options.markers_decode = NULL;
  CHECK(fx.markers_host.save(&fx, keys, 2, &why) && set_value.kind == TFP_RT_REF && set_value.ref == &fx.saved && fx.saved.count == 2);
  config_markers = set_value;
  CHECK(fx.markers_host.settings(&fx, &saved) && saved.count == 2 && !strcmp(saved.keys[1], "00000000000000bb"));
  config_markers = tfp_wire_ref(&store_table);
  CHECK(!fx.markers_host.settings(&fx, &saved)); /* not a table anyone here knows */
  CHECK(fx.markers_host.save_global(&fx, true, &why) == TFP_AIM_MARKERS_SAVE_DONE && set_value.kind == TFP_RT_TRUE);
  h->mod_set = NULL;
  CHECK(fx.markers_host.save_global(&fx, true, &why) == TFP_AIM_MARKERS_SAVE_RAISED && why && strstr(why, "attempt to call field 'set'"));
  CHECK(!fx.markers_host.save(&fx, keys, 1, &why) && why && strstr(why, "attempt to call field 'set'"));
  config_markers = tfp_wire_nil(), config_show = tfp_wire_nil();
  /* the mod-side answers the units get in this state */
  double clock_now = 0;
  bool is_true = true;
  TfpDofForeign foreign;
  CHECK(fx.dof_host.clock(&fx, &clock_now, &error) && clock_now == now_seconds);
  CHECK(fx.dof_host.active_view(&fx, &is_true, &error) && !is_true && fx.dof_host.fallback_ready(&fx, &is_true, &error) && !is_true);
  CHECK(fx.dof_host.backend_foreign_camera(&fx, &foreign, &error) && foreign.is_false && !strcmp(foreign.value_text, "false") && !foreign.why);
  CHECK(fx.compat.foreign(fx.compat.user, &foreign, &error) && foreign.is_false);
  CHECK(!fx.vignette_host.compat_foreign(&fx) && !fx.vignette_host.backend_foreign_camera(&fx) && !fx.vignette_host.active_view(&fx));
  CHECK(!fx.markers_host.foreign(&fx) && !fx.markers_host.fallback_ready(&fx) && !fx.aim_host.enabled(&fx) && !fx.aim_host.foreign(&fx));
  TfpMovementForeign owner = TFP_MOVEMENT_FOREIGN_OTHER;
  CHECK(fx.movement_host.backend_foreign_camera(&fx, &owner, &error) && owner == TFP_MOVEMENT_FOREIGN_FALSE);
  CHECK(!fx.dof_host.controller_is_enabled(&fx) && !fx.markers_host.controller_pose_offset(&fx));
  TfpDofBinding binding;
  CHECK(fx.dof_host.application_binding(&fx, &binding) && binding.get == EXE + 0x3fa3d0 && binding.set == EXE + 0x3fa120 && binding.app != 0);
  uint64_t exe = 0;
  uint8_t bytes[8];
  uint32_t word = 0;
  CHECK(!fx.dof_host.backend_alignment_executable(&fx, &exe, &error) && error);                       /* no game: the backend's own refusal */
  CHECK(!fx.markers_host.backend_module_identity(&fx, &exe, &error) && error && !fx.movement_host.backend_u32(&fx, 16, &word, &error) && error);
  CHECK(!fx.aim_host.backend_read(&fx, 16, 8, bytes, &error) && error);
  TfpMovementSnapshot moved;
  TfpNativeAimSnapshot aimed;
  TfpAimMarkersSnapshot marked;
  CHECK(fx.movement_host.backend_player_snapshot(&fx, &moved, &why) != TFP_MOVEMENT_DONE && !fx.aim_host.backend_snapshot(&fx, &aimed, &why) &&
        !fx.markers_host.backend_snapshot(&fx, &marked));
  bool wine = true;
  CHECK(fx.markers_host.backend_windows(&fx, &wine, &error) && !wine);
  CHECK(fx.markers_host.game_set_widget_shown(&fx, 0x1440000, 0x5550, 1, &error) && win.function == 0x1440000 && win.widget == 0x5550 && win.shown == 1);
  unsigned binder_checks = checks;

  /* ---- 5a. depth of field and the vignette, driven: the units, the glue, the engine half ---- */
  /* the product's two view answers, kept to ask them as they are: the runtime's entry answers false here (no view),
   * so what they say is the hold alone */
  bool (*const product_dof_view)(void *, bool *, const char **) = fx.dof_host.active_view;
  bool (*const product_vignette_view)(void *) = fx.vignette_host.active_view;
  bool product_seen = true;
  fx.dof_host.active_view = t_active_view, fx.vignette_host.active_view = t_vignette_view;
  fx.dof_host.backend_alignment_executable = t_executable, fx.dof_host.backend_read = t_thunks;
  fx.dof_host.backend_renderer_dof_value = NULL; /* the cached observer needs the game's memory */
  image_build = &tfp_dof_build_retail;           /* an executable of another build: check() refuses, nothing is sent */
  view_active = true;
  call_count = 0, line_count = 0;
  for (int i = 0; i < 4; i++) {
    now_seconds += .6;
    CHECK(h->dof_update(h, &o1, &o2));
    tfp_wire_effects_frame(&wire);
  }
  CHECK(logged("FPV DoF: ") && logged("Renderer DoF SDK contract changed") && !called("Application.set_render_setting") && G.dof);
  image_build = &tfp_dof_build_25480438;
  vignette_view = true;
  call_count = 0, line_count = 0;
  for (int i = 0; i < 12; i++) {
    now_seconds += .6;
    CHECK(h->dof_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE);
    tfp_wire_effects_frame(&wire);
  }
  CHECK(!G.dof && !G.row_enabled && G.script_errors == 0 && G.vignette_setting);
  CHECK(fx.dof.dof.phase == TFP_DOF_ACTIVE && fx.vignette.phase == TFP_VIGNETTE_ACTIVE && fx.vignette.row_count == 1 && fx.vignette.rows[0].owned);
  CHECK(fx.vignette.rows[0].entity == 0x3003 && fx.vignette.rows[0].id == G.row_id);
  CHECK(logged("Effect: dof: dof_enabled read as true and saved") && logged("Effect: dof: dof_enabled set (commands sent: 1)"));
  CHECK(logged("Effect: vignette: 1 row(s) found") && logged("Effect: vignette: switched off (writes: 1)"));
  CHECK(tfp_wire_effects_second(&wire, second, sizeof second) && !strcmp(second, "dof=owned vignette=owned:1 markers=shown move=idle aimopt=off"));
  /* the glue: Booleans to dof's tri-state, no command_pending, the status text */
  TfpDofVignetteResult glued;
  fx.dof_host.vignette_result(&fx, &glued);
  CHECK(glued.owned == TFP_DOF_TRUE && glued.cleanup_pending == TFP_DOF_FALSE && glued.command_pending == TFP_DOF_NIL);
  CHECK(!fx.dof_host.vignette_is_clean(&fx) && strstr(fx.dof_host.vignette_status(&fx), "active; environment vignette disabled=true; profiles=1"));
  CHECK(h->dof_result(h, &table) && table.f[TFP_RT_DOF_OWNED].kind == TFP_RT_TRUE && h->dof_is_clean(h, &o1) && o1.kind == TFP_RT_FALSE);
  CHECK(h->dof_status(h, &o1) && text_has(o1, "environment vignette disabled=true"));
  TfpDofResult whole;
  tfp_dof_result(&fx.dof, &whole);
  CHECK(whole.kind == TFP_DOF_VIGNETTE_MODULE && whole.owned == TFP_DOF_TRUE && whole.command_pending != TFP_DOF_RAISED && whole.vignette.owned == TFP_DOF_TRUE);
  /* the vignette alone owning is enough for the runtime's idle shortcut to stay open */
  CHECK(called("Application.main_world") && called("World.entities") && called("EntityManager.render_data_component") && called("EntityManager.alive") &&
        called("RenderDataComponent.instances") && called("RenderDataComponent.get_property") && called("RenderDataComponent.set_property"));
  /* the game switches the vignette back on: it is taken again */
  G.row_enabled = true;
  now_seconds += .6;
  CHECK(h->dof_update(h, &o1, &o2) && !G.row_enabled && fx.vignette.reassertions == 1);
  /* a weapon swap: the view is away for a third of a second. Neither is given back for it (the game showed the
   * vignette for those frames: a dark rim at every swap, seen in the game 2026-11-22). */
  view_active = false;
  line_count = 0;
  const TfpPhase phase_before = wire.controller.phase;
  const bool enabled_before = wire.controller.enabled;
  wire.controller.enabled = true, wire.controller.phase = TFP_PHASE_WAITING; /* first person is on and waits for the camera, as in a swap */
  for (int i = 0; i < 4; i++) {
    now_seconds += .1;
    CHECK(h->dof_update(h, &o1, &o2));
    tfp_wire_effects_frame(&wire);
    CHECK(!G.row_enabled && !G.dof);
  }
  CHECK(!logged("Effect: vignette: restored (") && !logged("Effect: dof: dof_enabled restored (") && !fx.dof_host.vignette_is_clean(&fx));
  CHECK(fx.dof.dof.phase == TFP_DOF_ACTIVE && fx.vignette.phase == TFP_VIGNETTE_ACTIVE);
  product_seen = false;
  CHECK(product_dof_view(&fx, &product_seen, &error) && product_seen && product_vignette_view(&fx)); /* held: the view is still there to both */
  view_active = true;
  now_seconds += .1;
  CHECK(h->dof_update(h, &o1, &o2) && !G.row_enabled && !G.dof);
  tfp_wire_effects_frame(&wire);
  CHECK(!logged("Effect: vignette: restored (") && !logged("Effect: dof: dof_enabled restored ("));
  /* the same while the old lease is still being released (the action does not allow the exit yet): first person is
   * on all the same, so both are held */
  view_active = false;
  wire.controller.phase = TFP_PHASE_CLEANUP_PENDING;
  for (int i = 0; i < 4; i++) {
    now_seconds += .1;
    CHECK(h->dof_update(h, &o1, &o2));
    tfp_wire_effects_frame(&wire);
    CHECK(!G.row_enabled && !G.dof);
  }
  CHECK(!logged("Effect: vignette: restored (") && !logged("Effect: dof: dof_enabled restored ("));
  /* first person switched off while the view is away: both given back at once, not after the time */
  wire.controller.enabled = false, wire.controller.phase = TFP_PHASE_IDLE;
  now_seconds += .1;
  CHECK(h->dof_update(h, &o1, &o2));
  tfp_wire_effects_frame(&wire);
  CHECK(G.row_enabled && G.dof && logged("Effect: vignette: restored (1)") && logged_count("Effect: dof: dof_enabled restored (") == 1);
  product_seen = true;
  CHECK(product_dof_view(&fx, &product_seen, &error) && !product_seen && !product_vignette_view(&fx));
  /* and taken again when the view is back; then away for longer than the time with first person on: given back,
   * both in the same tick */
  view_active = true, wire.controller.enabled = true, wire.controller.phase = TFP_PHASE_RUNNING;
  for (int i = 0; i < 4; i++) {
    now_seconds += .6;
    CHECK(h->dof_update(h, &o1, &o2));
    tfp_wire_effects_frame(&wire);
  }
  CHECK(!G.row_enabled && !G.dof);
  view_active = false, wire.controller.phase = TFP_PHASE_WAITING;
  int given_back_at = -1, dof_back_at = -1;
  for (int i = 0; i < 60; i++) {
    now_seconds += .1;
    CHECK(h->dof_update(h, &o1, &o2));
    tfp_wire_effects_frame(&wire);
    if (G.row_enabled && given_back_at < 0) given_back_at = i;
    if (G.dof && dof_back_at < 0) dof_back_at = i;
  }
  CHECK(given_back_at >= 14 && given_back_at <= 16 && dof_back_at == given_back_at); /* the first away tick starts the 1.5 s; ticks are 0.1 s */
  view_active = true, wire.controller.phase = TFP_PHASE_RUNNING;
  for (int i = 0; i < 4; i++) {
    now_seconds += .6;
    CHECK(h->dof_update(h, &o1, &o2));
    tfp_wire_effects_frame(&wire);
  }
  CHECK(!G.row_enabled && !G.dof);
  wire.controller.phase = phase_before, wire.controller.enabled = enabled_before;
  /* the view ends: both are given back */
  view_active = false;
  line_count = 0;
  for (int i = 0; i < 12; i++) {
    now_seconds += .6;
    CHECK(h->dof_update(h, &o1, &o2));
    tfp_wire_effects_frame(&wire);
  }
  CHECK(G.dof && G.row_enabled && G.script_errors == 0 && fx.vignette.row_count == 0);
  CHECK(h->dof_result(h, &table) && table.f[TFP_RT_DOF_OWNED].kind == TFP_RT_FALSE && h->dof_is_clean(h, &o1) && o1.kind == TFP_RT_TRUE);
  CHECK(logged_count("Effect: dof: dof_enabled restored (") == 1 && logged_count("Effect: vignette: restored (") == 1 && logged("Effect: vignette: 0 row(s) found")); /* once each for this end; the counts in the lines go on from the swaps above */
  fx.dof_host.vignette_result(&fx, &glued);
  CHECK(glued.owned == TFP_DOF_FALSE && fx.dof_host.vignette_is_clean(&fx));
  CHECK(tfp_wire_effects_second(&wire, second, sizeof second) && !strcmp(second, "dof=idle vignette=idle markers=shown move=idle aimopt=off"));
  /* a vignette that is not allowed on is an idle one to dof: the setting still works alone */
  fx.state[TFP_FX_VIGNETTE] = TFP_FX_REFUSED;
  view_active = true;
  call_count = 0;
  for (int i = 0; i < 8; i++) {
    now_seconds += .6;
    CHECK(h->dof_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE);
  }
  CHECK(!G.dof && G.row_enabled && !called("Application.main_world") && fx.dof.dof.phase == TFP_DOF_ACTIVE);
  CHECK(tfp_wire_effects_second(&wire, second, sizeof second) && !strcmp(second, "dof=owned vignette=off markers=shown move=idle aimopt=off"));
  /* dof refused while it owns the setting: update turns into stop, and the setting comes back */
  fx.state[TFP_FX_DOF] = TFP_FX_REFUSED;
  for (int i = 0; i < 8; i++) {
    now_seconds += .6;
    CHECK(h->dof_update(h, &o1, &o2));
  }
  CHECK(G.dof && h->dof_is_clean(h, &o1) && o1.kind == TFP_RT_TRUE);
  CHECK(tfp_wire_effects_second(&wire, second, sizeof second) && !strcmp(second, "dof=off vignette=off markers=shown move=idle aimopt=off"));
  fx.state[TFP_FX_DOF] = fx.state[TFP_FX_VIGNETTE] = TFP_FX_READY;
  view_active = false;
  /* the setter refusing, and a function that is no longer the bound one */
  TfpDofValue got = TFP_DOF_VALUE_OTHER;
  CHECK(fx.dof_host.application_binding(&fx, &binding));
  CHECK(fx.dof_host.application_render_setting(&fx, &binding, "dof_enabled", &got, &error) && got == TFP_DOF_VALUE_TRUE);
  G.refuse_set = true;
  CHECK(fx.dof_host.application_set_render_setting(&fx, &binding, "dof_enabled", false, &error) == TFP_DOF_SET_REFUSED);
  G.refuse_set = false;
  fail_path = "Application.set_render_setting";
  CHECK(fx.dof_host.application_set_render_setting(&fx, &binding, "dof_enabled", false, &error) == TFP_DOF_SET_RAISED && strstr(error, "scripted failure"));
  fail_path = NULL;
  binding.get += 8;
  call_count = 0;
  CHECK(!fx.dof_host.application_render_setting(&fx, &binding, "dof_enabled", &got, &error) && call_count == 0 &&
        strstr(error, "Application.render_setting is no longer the function that was bound"));
  uint64_t stale = 0;
  CHECK(!fx.vignette_host.application_main_world(&fx, EXE + 5, &stale, &error) && strstr(error, "Application.main_world is no longer"));
  TfpVignetteApi api;
  fx.vignette_host.stingray_api(&fx, &api);
  CHECK(api.available && api.fn[TFP_VIGNETTE_FN_SET].kind == TFP_VIGNETTE_BINDING_FUNCTION && api.fn[TFP_VIGNETTE_FN_SET].token == EXE + 0x466fd0 &&
        api.fn[TFP_VIGNETTE_FN_MAIN_WORLD].token == EXE + 0x3f8310);
  missing_path = "World.entities";
  fx.vignette_host.stingray_api(&fx, &api);
  CHECK(api.available && api.fn[TFP_VIGNETTE_FN_ENTITIES].kind == TFP_VIGNETTE_BINDING_NIL);
  missing_path = NULL;
  eng_up = false;
  fx.vignette_host.stingray_api(&fx, &api);
  CHECK(!api.available && !fx.dof_host.application_binding(&fx, &binding));
  eng_up = true;
  /* the retail table is what the unit gets when asked for (the vectors' build) */
  {
    static TfpFx other;
    static TfpWire second_wire;
    TfpFxOptions retail = {NULL, NULL, &tfp_dof_build_retail, &tfp_aim_markers_build_retail, &tfp_movement_build_retail, &tfp_native_aim_build_retail};
    memset(&second_wire, 0, sizeof second_wire);
    tfp_wire_effects_bind(&second_wire, &other, &p, &retail);
    CHECK(other.dof_host.build == &tfp_dof_build_retail && other.markers_host.build == &tfp_aim_markers_build_retail &&
          other.movement_host.build == &tfp_movement_build_retail && other.aim_host.build == &tfp_native_aim_build_retail);
  }
  unsigned driven_checks = checks;

  /* ---- 5b. movement: the S key through the engine, in retail's order ---- */
  wire.controller.enabled = true, wire.controller.runs = 1; /* controller:is_enabled(), result().mode == 'pose_offset' */
  fx.movement_host.backend_player_snapshot = t_no_player;
  call_count = 0, line_count = 0;
  CHECK(h->movement_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && text_has(o2, "no player in the test"));
  CHECK(call_count == 4 && !strcmp(calls[0].path, "Keyboard.button_index") && !strcmp(calls[1].path, "Window.mouse_focus") &&
        !strcmp(calls[2].path, "Window.show_cursor") && !strcmp(calls[3].path, "Keyboard.button") && calls[3].a[0].n == 31 && G.script_errors == 0);
  CHECK(fx.movement.phase == TFP_MOVEMENT_UNAVAILABLE && logged("First-person movement: ") && h->movement_is_clean(h, &o1) && o1.kind == TFP_RT_TRUE);
  CHECK(h->movement_result(h, &table) && text_has(table.f[TFP_RT_TARGET_PHASE], "unavailable"));
  G.button = v_num(0); /* S up: no snapshot is taken, the binding is not looked up again */
  call_count = 0;
  CHECK(h->movement_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && o2.kind == TFP_RT_NIL && call_count == 3 && !strcmp(calls[0].path, "Window.mouse_focus"));
  CHECK(fx.movement.phase == TFP_MOVEMENT_IDLE);
  bump_generation(); /* another Keyboard table: the binding is looked up again */
  call_count = 0;
  CHECK(h->movement_update(h, &o1, &o2) && call_count == 4 && !strcmp(calls[0].path, "Keyboard.button_index"));
  fail_path = "Keyboard.button";
  CHECK(h->movement_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && text_has(o2, "Keyboard.button: scripted failure"));
  fail_path = NULL;
  eng_up = false; /* no Keyboard table */
  CHECK(h->movement_update(h, &o1, &o2) && text_has(o2, "Game keyboard input unavailable"));
  eng_up = true;
  fx.state[TFP_FX_MOVEMENT] = TFP_FX_REFUSED; /* refused and clean: the off answer, the engine is not asked */
  call_count = 0;
  CHECK(h->movement_update(h, &o1, &o2) && o1.kind == TFP_RT_TRUE && o2.kind == TFP_RT_NIL && call_count == 0);
  fx.state[TFP_FX_MOVEMENT] = TFP_FX_READY;
  wire.controller.enabled = false, wire.controller.runs = 0;
  CHECK(fx.movement_host.windows_virtual_query(&fx, HEAP + 3, &(TfpMovementRegion){0}, sizeof(TfpMovementRegion)) == sizeof(TfpMovementRegion));

  /* ---- 5c. the native aim option's one-byte write (memory.lua:164-208) ---- */
  win.state = 0x1000, win.kind = 0x20000, win.protect = 4, win.write_result = 1, win.write_count = 1;
  heap[9] = 0;
  CHECK(tfp_fx_memory_copy(&fx, HEAP + 9, 1, &why) == TFP_NATIVE_AIM_COPY_DONE && why == NULL && heap[9] == 1);
  CHECK(fx.aim_host.memory_copy(&fx, HEAP + 9, 0, &why) == TFP_NATIVE_AIM_COPY_DONE && heap[9] == 0);
  CHECK(tfp_fx_memory_copy(&fx, 4096, 1, &why) == TFP_NATIVE_AIM_COPY_REFUSED && !strcmp(why, "Address or copy size is out of bounds"));
  CHECK(tfp_fx_memory_copy(&fx, UINT64_C(140737488355328), 1, &why) == TFP_NATIVE_AIM_COPY_REFUSED && !strcmp(why, "Address or copy size is out of bounds"));
  CHECK(tfp_fx_memory_copy(&fx, HEAP + 4096, 1, &why) == TFP_NATIVE_AIM_COPY_REFUSED && !strcmp(why, "Write region is unavailable"));
  win.protect = 8; /* copy-on-write is not read-write (the movement byte accepts it, this writer does not) */
  CHECK(tfp_fx_memory_copy(&fx, HEAP + 9, 1, &why) == TFP_NATIVE_AIM_COPY_REFUSED && !strcmp(why, "Writes require committed private read-write memory") && heap[9] == 0);
  win.protect = 4, win.kind = 0x1000000;
  CHECK(tfp_fx_memory_copy(&fx, HEAP + 9, 1, &why) == TFP_NATIVE_AIM_COPY_REFUSED && heap[9] == 0);
  win.kind = 0x20000, win.state = 0x2000;
  CHECK(tfp_fx_memory_copy(&fx, HEAP + 9, 1, &why) == TFP_NATIVE_AIM_COPY_REFUSED);
  win.state = 0x1000, win.write_result = 0;
  CHECK(tfp_fx_memory_copy(&fx, HEAP + 9, 1, &why) == TFP_NATIVE_AIM_COPY_REFUSED && !strcmp(why, "Memory write failed or copied only part of the data"));
  win.write_result = 1, win.write_count = 0;
  CHECK(tfp_fx_memory_copy(&fx, HEAP + 9, 1, &why) == TFP_NATIVE_AIM_COPY_REFUSED && !strcmp(why, "Memory write failed or copied only part of the data"));
  win.write_count = 1;
  heap_blind = true; /* the write went through, the readback did not: reported, not undone */
  heap[9] = 0;
  CHECK(tfp_fx_memory_copy(&fx, HEAP + 9, 1, &why) == TFP_NATIVE_AIM_COPY_REFUSED && !strcmp(why, "Memory write readback failed") && heap[9] == 1);
  heap_blind = false;

  /* ---- the end: everything clean, the runtime disables ---- */
  now_seconds += 1;
  tfp_wire_frame(&wire, NULL);
  tfp_wire_effects_frame(&wire);
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_disable, NULL, r) == TFP_RT_OK && r[0].kind == TFP_RT_TRUE);
  CHECK(fx.dof.dof.closing && fx.vignette.closing && fx.markers.closing && fx.movement.closing && fx.aim.closing);
  CHECK(h->aim_markers_set_setting(h, tfp_wire_number(1), &o1, &o2) && text_has(o2, "Mod is stopping"));

  printf("wire effects: %u checks (the engine half %u, load checks %u, the binder with no game %u, driven units %u, the rest %u)\n", checks,
         engine_checks, load_checks - engine_checks, binder_checks - load_checks, driven_checks - binder_checks, checks - driven_checks);
  return 0;
}
