/* See wire_win_effects.h.
 *
 * The engine calls are the 0.5.34 port's (src/effects.c:47-70, :112-186, :224-270; that port ran in
 * the game on this build), re-cut to the shape the translated units ask for. Three were never called
 * on this build by that port: Keyboard.button_index / button_id, Keyboard.button, and the two Window
 * functions; they go through the same protected eng_call.
 *
 * Handles. The vignette unit compares worlds, managers and entities for identity and keeps them
 * across frames; the bridge hands out registry references that are a new number at every call.
 *   world, manager: one kept reference each; the handle is a serial that changes when the engine
 *     answers another object (eng_same).
 *   entities: the handle is the Lua value's own identity: the address of a light userdata, or the
 *     bits of a number. Any other Lua type is refused (its identity would not survive a call).
 *     The list World.entities answered is kept until the next one, and an entity a property was
 *     read from is kept (it may become one of the unit's rows, which outlive the list).
 *
 * The part under _WIN32 is Windows; the rest needs only the bridge, so tests/wire_effects_test.c
 * compiles this file with a scripted bridge. */
#include "wire_win_effects.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* The bridge functions used here, as src/engine.h declares them (the Windows build includes that
 * header too, so a difference does not compile). */
typedef int EngRef;
bool eng_ready(void);
const char *eng_status(void);
const char *eng_error(void);
uint64_t eng_state_generation(void);
bool eng_call(const char *path, const char *sig, ...);
bool eng_has(const char *path);
uintptr_t eng_cfunction(const char *path);
void eng_unref(EngRef *ref);
bool eng_same(EngRef a, EngRef b);
EngRef eng_copy(EngRef ref);
uintptr_t eng_pointer(EngRef ref);
int eng_len(EngRef table);
bool eng_index(EngRef table, int i, EngRef *out);
int eng_type(EngRef ref);
bool eng_tonumber(EngRef ref, double *out);
bool eng_toboolean(EngRef ref, bool *out);
bool eng_tostring(EngRef ref, char *out, size_t size);
#ifdef _WIN32
#include "engine.h"
#endif

enum { LUA_NIL = 0, LUA_BOOLEAN = 1, LUA_LIGHTUSERDATA = 2, LUA_NUMBER = 3, LUA_STRING = 4, LUA_TABLE = 5 };
enum { KEPT = 64 };

static uint64_t exe_base;
static char problem[256], text_value[256];
static struct {
  uint64_t generation;
  EngRef world, manager, list;
  uint64_t world_serial, manager_serial;
  uint32_t list_count, cursor;
  uint64_t ids[TFP_VIGNETTE_MAX_ENTITIES];       /* the identity of each entry of `list`; 0: nil / false */
  struct { uint64_t id; EngRef ref; } kept[KEPT];
  unsigned kept_next;
} held;

static bool fail(const char **error, const char *text) {
  snprintf(problem, sizeof problem, "%s", text);
  if (error) *error = problem;
  return false;
}
static bool engine_failed(const char **error, const char *path) {
  char text[256];
  snprintf(text, sizeof text, "%s: %s", path, eng_error());
  return fail(error, text);
}
void tfp_win_effects_forget(void) {
  eng_unref(&held.world), eng_unref(&held.manager), eng_unref(&held.list);
  for (int i = 0; i < KEPT; i++) eng_unref(&held.kept[i].ref), held.kept[i].id = 0;
  held.list_count = held.cursor = 0;
}
/* The references of a replaced script state are dead: forget them (eng_unref only forgets those). */
static void fresh(void) {
  if (held.generation == eng_state_generation()) return;
  tfp_win_effects_forget();
  held.generation = eng_state_generation();
}

static bool p_ready(void *u, const char **why) {
  (void)u;
  if (eng_ready()) return true;
  if (why) *why = eng_status();
  return false;
}
static uint64_t p_generation(void *u) { (void)u; return eng_state_generation(); }
static uint64_t p_function(void *u, const char *path) { (void)u; return (uint64_t)eng_cfunction(path); }
static bool p_table(void *u, const char *name) { (void)u; return eng_has(name) && eng_cfunction(name) == 0; }
static uint64_t p_exe(void *u) { (void)u; return exe_base; }

/* ---- depth of field (effects.c:47-70) -------------------------------------------------------- */
static bool p_render_setting(void *u, const char *key, int *value, const char **error) {
  EngRef v = 0;
  bool truth = false;
  (void)u;
  if (!eng_call("Application.render_setting", "s>r", key, &v)) return engine_failed(error, "Application.render_setting");
  *value = eng_type(v) == LUA_BOOLEAN && eng_toboolean(v, &truth) ? (truth ? 1 : 0) : -1;
  eng_unref(&v);
  return true;
}
/* The first result of a call whose results came back packed ('R'): false only when it is the Boolean false. */
static bool first_is_false(EngRef results) {
  EngRef v = 0;
  bool truth = true, is_false = false;
  if (eng_len(results) > 0 && eng_index(results, 1, &v)) is_false = eng_type(v) == LUA_BOOLEAN && eng_toboolean(v, &truth) && !truth;
  eng_unref(&v);
  return is_false;
}
static TfpFxSet p_set_render_setting(void *u, const char *key, bool value, const char **error) {
  EngRef results = 0;
  (void)u;
  if (!eng_call("Application.set_render_setting", "ss>R", key, value ? "true" : "false", &results)) {
    engine_failed(error, "Application.set_render_setting");
    return TFP_FX_SET_RAISED;
  }
  bool refused = first_is_false(results);
  eng_unref(&results);
  return refused ? TFP_FX_SET_REFUSED : TFP_FX_SET_DONE;
}

/* ---- vignette (effects.c:112-186, :224-270) --------------------------------------------------- */
/* A world or manager answer as a handle: 0 for nil / false, else the serial of the kept reference. */
static uint64_t keep_object(EngRef *slot, uint64_t *serial, EngRef answer) {
  bool truth = true;
  if (!answer) return 0;
  if (eng_type(answer) == LUA_BOOLEAN && eng_toboolean(answer, &truth) && !truth) { eng_unref(&answer); return 0; }
  if (*slot && eng_same(*slot, answer)) { eng_unref(&answer); return *serial; }
  eng_unref(slot);
  *slot = answer;
  return ++*serial;
}
static bool p_main_world(void *u, uint64_t *world, const char **error) {
  EngRef answer = 0;
  uint64_t before = held.world_serial;
  (void)u;
  fresh();
  if (!eng_call("Application.main_world", ">r", &answer)) return engine_failed(error, "Application.main_world");
  *world = keep_object(&held.world, &held.world_serial, answer);
  if (held.world_serial != before) { /* another world: nothing of the old one may be used again */
    EngRef world_ref = held.world;
    held.world = 0;
    tfp_win_effects_forget();
    held.world = world_ref;
  }
  return true;
}
static bool is_world(uint64_t world, const char **error) {
  fresh();
  return (world && held.world && world == held.world_serial) || fail(error, "The world handle is not the engine's current world");
}
static bool is_manager(uint64_t manager, const char **error) {
  fresh();
  return (manager && held.manager && manager == held.manager_serial) || fail(error, "The render-data manager handle is stale");
}
static bool p_render_data_component(void *u, uint64_t world, uint64_t *manager, const char **error) {
  EngRef answer = 0;
  (void)u;
  if (!is_world(world, error)) return false;
  if (!eng_call("EntityManager.render_data_component", "r>r", held.world, &answer)) return engine_failed(error, "EntityManager.render_data_component");
  *manager = keep_object(&held.manager, &held.manager_serial, answer);
  return true;
}
/* The identity of an entity value: 0 for nil / false; false with the Lua type when it has none here. */
static bool identity(EngRef value, uint64_t *id, int *type) {
  bool truth = true;
  double number = 0;
  *id = 0;
  *type = eng_type(value);
  switch (*type) {
    case LUA_NIL: return true;
    case LUA_BOOLEAN: return eng_toboolean(value, &truth) && !truth;
    case LUA_LIGHTUSERDATA: *id = (uint64_t)eng_pointer(value); return *id != 0;
    case LUA_NUMBER:
      if (!eng_tonumber(value, &number)) return false;
      memcpy(id, &number, sizeof *id);
      return *id != 0;
    default: return false;
  }
}
/* How long reading the last entity list took (four bridge calls per entry, once a second while the
 * vignette is held): the cost retail does not have, since its Lua keeps the engine's table. */
static struct { uint32_t entries, microseconds, worst, lists; } scan;
static uint64_t ticks(void);
bool tfp_win_effects_last_scan(uint32_t *entries, uint32_t *microseconds, uint32_t *worst) {
  *entries = scan.entries, *microseconds = scan.microseconds, *worst = scan.worst;
  return scan.entries != 0;
}
uint32_t tfp_win_effects_scans(void) { return scan.lists; }
static bool p_world_entities(void *u, uint64_t world, uint64_t *entities, uint32_t capacity, TfpVignetteList *kind, uint32_t *count,
                             const char **error) {
  EngRef list = 0;
  bool truth = true;
  (void)u;
  *count = 0;
  if (!is_world(world, error)) return false;
  if (!eng_call("World.entities", "r>r", held.world, &list)) return engine_failed(error, "World.entities");
  if (!list || (eng_type(list) == LUA_BOOLEAN && eng_toboolean(list, &truth) && !truth)) {
    eng_unref(&list);
    *kind = TFP_VIGNETTE_LIST_NIL;
    return true;
  }
  if (eng_type(list) != LUA_TABLE) {
    eng_unref(&list);
    *kind = TFP_VIGNETTE_LIST_NOT_A_TABLE;
    return true;
  }
  int length = eng_len(list);
  uint64_t started = ticks();
  *kind = TFP_VIGNETTE_LIST_TABLE;
  *count = (uint32_t)length;
  eng_unref(&held.list);
  held.list = list;
  held.list_count = 0, held.cursor = 0;
  for (uint32_t i = 0; i < (uint32_t)length && i < capacity && i < TFP_VIGNETTE_MAX_ENTITIES; i++) {
    EngRef entity = 0;
    int type = 0;
    uint64_t id = 0;
    bool known = eng_index(list, (int)i + 1, &entity) && identity(entity, &id, &type);
    eng_unref(&entity);
    if (!known) {
      char text[128];
      snprintf(text, sizeof text, "World.entities: entry %u is a value of Lua type %d, which has no identity here", (unsigned)i + 1, type);
      held.list_count = 0;
      return fail(error, text);
    }
    entities[i] = held.ids[i] = id;
    held.list_count = i + 1;
  }
  scan.entries = held.list_count, scan.microseconds = (uint32_t)(ticks() - started), scan.lists++;
  if (scan.microseconds > scan.worst) scan.worst = scan.microseconds;
  return true;
}
/* A reference to the entity behind a handle: from the kept ones, else from the list (the unit walks
 * it in order, so the search starts where the last one ended). *owned: the caller releases it. */
static bool entity_ref(uint64_t entity, EngRef *out, bool *owned, const char **error) {
  *owned = false;
  fresh();
  for (int i = 0; i < KEPT; i++)
    if (held.kept[i].ref && held.kept[i].id == entity) { *out = held.kept[i].ref; return true; }
  for (uint32_t n = 0; held.list && n < held.list_count; n++) {
    uint32_t at = (held.cursor + n) % held.list_count;
    uint64_t id = 0;
    int type = 0;
    if (held.ids[at] != entity) continue;
    if (!eng_index(held.list, (int)at + 1, out) || !identity(*out, &id, &type) || id != entity) {
      eng_unref(out);
      return fail(error, "The entity list changed under its handles");
    }
    held.cursor = at;
    *owned = true;
    return true;
  }
  return fail(error, "The entity handle is not one the engine handed out");
}
static void keep_entity(uint64_t entity, EngRef ref) {
  for (int i = 0; i < KEPT; i++)
    if (held.kept[i].ref && held.kept[i].id == entity) return;
  unsigned at = held.kept_next++ % KEPT;
  eng_unref(&held.kept[at].ref);
  held.kept[at].ref = eng_copy(ref);
  held.kept[at].id = held.kept[at].ref ? entity : 0;
}
static bool p_entity_alive(void *u, uint64_t entity, bool *alive, const char **error) {
  EngRef ref = 0;
  bool owned = false;
  int truth = 0;
  (void)u;
  if (!entity_ref(entity, &ref, &owned, error)) return false;
  bool done = eng_call("EntityManager.alive", "r>b", ref, &truth);
  if (owned) eng_unref(&ref);
  if (!done) return engine_failed(error, "EntityManager.alive");
  *alive = truth != 0;
  return true;
}
static bool p_instances(void *u, uint64_t manager, uint64_t entity, double *ids, uint32_t capacity, uint32_t *count, const char **error) {
  EngRef ref = 0, results = 0, first = 0;
  bool owned = false;
  (void)u;
  *count = 0;
  if (!is_manager(manager, error) || !entity_ref(entity, &ref, &owned, error)) return false;
  bool done = eng_call("RenderDataComponent.instances", "rr>R", held.manager, ref, &results);
  if (owned) eng_unref(&ref);
  if (!done) return engine_failed(error, "RenderDataComponent.instances");
  int length = eng_len(results);
  if (length == 1 && eng_index(results, 1, &first) && eng_type(first) == LUA_TABLE) { /* one table instead of several results */
    eng_unref(&results);
    results = first, first = 0;
    length = eng_len(results);
  }
  eng_unref(&first);
  *count = (uint32_t)length;
  for (uint32_t i = 0; i < (uint32_t)length && i < capacity; i++) {
    EngRef id = 0;
    ids[i] = NAN;
    if (eng_index(results, (int)i + 1, &id)) eng_tonumber(id, &ids[i]);
    eng_unref(&id);
  }
  eng_unref(&results);
  return true;
}
static bool p_get_property(void *u, uint64_t manager, uint64_t entity, double id, const char *property, TfpVignetteValue *out, const char **error) {
  EngRef ref = 0, value = 0;
  bool owned = false, truth = false;
  (void)u;
  memset(out, 0, sizeof *out);
  if (!is_manager(manager, error) || !entity_ref(entity, &ref, &owned, error)) return false;
  bool done = eng_call("RenderDataComponent.get_property", "rrnp>r", held.manager, ref, id, property, &value);
  if (done) keep_entity(entity, ref);
  if (owned) eng_unref(&ref);
  if (!done) return engine_failed(error, "RenderDataComponent.get_property");
  switch (eng_type(value)) {
    case LUA_NIL: out->kind = TFP_VIGNETTE_VALUE_NIL; break;
    case LUA_BOOLEAN: out->kind = TFP_VIGNETTE_VALUE_BOOLEAN, out->boolean = eng_toboolean(value, &truth) && truth; break;
    case LUA_STRING:
      if (eng_tostring(value, text_value, sizeof text_value)) out->kind = TFP_VIGNETTE_VALUE_TEXT, out->text = text_value, out->text_len = strlen(text_value);
      else out->kind = TFP_VIGNETTE_VALUE_OTHER;
      break;
    default: out->kind = TFP_VIGNETTE_VALUE_OTHER; break;
  }
  eng_unref(&value);
  return true;
}
static bool p_set_property(void *u, uint64_t manager, uint64_t entity, double id, const char *property, bool value, bool *refused, const char **error) {
  EngRef ref = 0, results = 0;
  bool owned = false;
  (void)u;
  *refused = false;
  if (!is_manager(manager, error) || !entity_ref(entity, &ref, &owned, error)) return false;
  bool done = eng_call("RenderDataComponent.set_property", "rrnpb>R", held.manager, ref, id, property, value ? 1 : 0, &results);
  if (done) keep_entity(entity, ref);
  if (owned) eng_unref(&ref);
  if (!done) return engine_failed(error, "RenderDataComponent.set_property");
  *refused = first_is_false(results);
  eng_unref(&results);
  return true;
}

/* ---- movement: the keyboard and the window ---------------------------------------------------- */
static bool number_of(EngRef *value, bool *has, double *out) {
  *has = eng_type(*value) == LUA_NUMBER && eng_tonumber(*value, out);
  eng_unref(value);
  return true;
}
static bool p_keyboard_find(void *u, bool index, const char *name, bool *has, double *key, const char **error) {
  const char *path = index ? "Keyboard.button_index" : "Keyboard.button_id";
  EngRef value = 0;
  (void)u;
  if (!eng_call(path, "s>r", name, &value)) return engine_failed(error, path);
  return number_of(&value, has, key);
}
static bool p_keyboard_button(void *u, double key, bool *has, double *out, const char **error) {
  EngRef value = 0;
  (void)u;
  if (!eng_call("Keyboard.button", "n>r", key, &value)) return engine_failed(error, "Keyboard.button");
  return number_of(&value, has, out);
}
/* 1 true, 0 false, -1 anything else */
static bool boolean_of(const char *path, int *out, const char **error) {
  EngRef value = 0;
  bool truth = false;
  if (!eng_call(path, ">r", &value)) return engine_failed(error, path);
  *out = eng_type(value) == LUA_BOOLEAN && eng_toboolean(value, &truth) ? (truth ? 1 : 0) : -1;
  eng_unref(&value);
  return true;
}
static bool p_mouse_focus(void *u, bool *focus, const char **error) {
  int value = -1;
  (void)u;
  if (!boolean_of("Window.mouse_focus", &value, error)) return false;
  *focus = value == 1;
  return true;
}
static bool p_show_cursor(void *u, bool *shown, const char **error) {
  int value = -1;
  (void)u;
  if (!boolean_of("Window.show_cursor", &value, error)) return false;
  *shown = value != 0;
  return true;
}

/* ---- Windows ---------------------------------------------------------------------------------- */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "wire_win.h" /* tfp_win_query, tfp_win_count: the Perf line's counts */
_Static_assert(sizeof(MEMORY_BASIC_INFORMATION) == 48, "NativeADS_Region is MEMORY_BASIC_INFORMATION");
static uint64_t ticks(void) { /* microseconds */
  LARGE_INTEGER now, rate;
  if (!QueryPerformanceFrequency(&rate) || !QueryPerformanceCounter(&now) || rate.QuadPart <= 0) return 0;
  return (uint64_t)(now.QuadPart / rate.QuadPart) * 1000000u + (uint64_t)(now.QuadPart % rate.QuadPart) * 1000000u / (uint64_t)rate.QuadPart;
}
static int64_t p_perf_ticks(void *u) { /* round 28: the Perf line's dof and vignette times */
  LARGE_INTEGER now;
  (void)u;
  return QueryPerformanceCounter(&now) ? (int64_t)now.QuadPart : 0;
}
static size_t p_query(void *u, uint64_t address, void *region, size_t size) {
  (void)u;
  return tfp_win_query(address, region, size); /* src/wire_win.c: VirtualQuery, counted for the Perf line */
}
/* retail: WriteProcessMemory(GetCurrentProcess(), address, bytes, size, &count) (first_person_movement.lua:98-103, memory.lua:197). */
static int p_write(void *u, uint64_t address, const uint8_t *bytes, size_t size, size_t *written) {
  SIZE_T count = 0;
  (void)u;
  tfp_win_count(TFP_WIN_COUNT_PROCESS_WRITE); /* game memory: stays on WriteProcessMemory (round 28 changed host_write only) */
  int done = WriteProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)address, bytes, size, &count) != 0;
  *written = (size_t)count;
  return done;
}
/* The game's void (*)(uintptr_t widget, uint8_t shown) (aim_markers.lua:57-60; the 0.5.34 port's
 * guarded call, src/markers_native.c). The address is the unit's: game + its build's set_shown, after
 * its own fingerprint and executable-page checks. */
static __declspec(noinline) void widget_raw(uintptr_t function, uintptr_t widget, uint8_t shown) {
  ((void (*)(uintptr_t, uint8_t))function)(widget, shown);
}
static void (*volatile widget_call)(uintptr_t, uintptr_t, uint8_t) = widget_raw;
static bool p_widget(void *u, uint64_t function, uint64_t widget, uint8_t shown, const char **error) {
  bool done = false;
  (void)u;
  __try { widget_call((uintptr_t)function, (uintptr_t)widget, shown); done = true; }
  __except (EXCEPTION_EXECUTE_HANDLER) { done = false; }
  if (done) return true;
  char text[128];
  snprintf(text, sizeof text, "The game's widget setter at %#llx faulted", (unsigned long long)function);
  return fail(error, text);
}
#else
static uint64_t ticks(void) { return 0; }
#endif

void tfp_win_effects_platform(TfpFxPlatform *out, uint64_t exe) {
  memset(out, 0, sizeof *out);
  exe_base = exe;
  out->engine_ready = p_ready, out->engine_generation = p_generation, out->engine_function = p_function, out->engine_table = p_table;
  out->exe_base = p_exe;
  out->render_setting = p_render_setting, out->set_render_setting = p_set_render_setting;
  out->main_world = p_main_world, out->world_entities = p_world_entities, out->render_data_component = p_render_data_component;
  out->entity_alive = p_entity_alive, out->instances = p_instances, out->get_property = p_get_property, out->set_property = p_set_property;
  out->keyboard_find = p_keyboard_find, out->keyboard_button = p_keyboard_button;
  out->window_mouse_focus = p_mouse_focus, out->window_show_cursor = p_show_cursor;
#ifdef _WIN32
  out->virtual_query = p_query, out->write_process_memory = p_write, out->set_widget_shown = p_widget;
  out->perf_ticks = p_perf_ticks;
#endif
}
