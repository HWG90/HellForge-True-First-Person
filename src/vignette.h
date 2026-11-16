/* The environment vignette switch: translation of retail src/first_person/vignette.lua at
 * 0.5.41-smooth.66 (commit 1d0d453). While the first-person view is active, every render-data instance
 * of the vignette shading-environment component that the main world's entities carry has its
 * `vignette_enabled` property held at false; the values are written back on stop. Entities and
 * instances come from the engine's own inventory, 96 entities per update. Every text is retail's.
 * tests/vectors/vignette_vectors.h holds the retail file's own answers. */
#ifndef TFP_VIGNETTE_H
#define TFP_VIGNETTE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFP_VIGNETTE_TEXT 256                     /* texts are cut to 255 bytes; Lua has no limit */
/* The chunk name LuaJIT prints before an assertion's message ("<chunk>:<line>: "). */
#define TFP_VIGNETTE_LUA_CHUNK "src/first_person/vignette.lua"
#define TFP_VIGNETTE_CLASS 0x35b0f6c2u            /* low 32 bits of a vignette instance id */
#define TFP_VIGNETTE_MAPPING "core/stingray_renderer/shading_environment_components/vignette"
#define TFP_VIGNETTE_MAX_INSTANCES 64             /* per entity */
#define TFP_VIGNETTE_MAX_ENTITIES 32768
#define TFP_VIGNETTE_MAX_ROWS 32
#define TFP_VIGNETTE_SCAN_SLICE 96                /* entities looked at per update */

/* The seven engine functions, as retail's api() collects them. */
typedef enum TfpVignetteFn {
  TFP_VIGNETTE_FN_MAIN_WORLD,       /* stingray.Application.main_world */
  TFP_VIGNETTE_FN_ENTITIES,         /* stingray.World.entities */
  TFP_VIGNETTE_FN_MANAGER,          /* stingray.EntityManager.render_data_component */
  TFP_VIGNETTE_FN_ALIVE,            /* stingray.EntityManager.alive */
  TFP_VIGNETTE_FN_INSTANCES,        /* stingray.RenderDataComponent.instances */
  TFP_VIGNETTE_FN_GET,              /* stingray.RenderDataComponent.get_property */
  TFP_VIGNETTE_FN_SET,              /* stingray.RenderDataComponent.set_property */
  TFP_VIGNETTE_FN_COUNT
} TfpVignetteFn;
typedef enum TfpVignetteBindingKind {
  TFP_VIGNETTE_BINDING_NIL,              /* the field is nil */
  TFP_VIGNETTE_BINDING_NOT_A_FUNCTION,   /* any other value that is not a function */
  TFP_VIGNETTE_BINDING_FUNCTION
} TfpVignetteBindingKind;
/* token: the identity of the function (two readings of the same Lua function give the same token,
 * another function another one). Only read when kind is FUNCTION. */
typedef struct TfpVignetteBinding { TfpVignetteBindingKind kind; uint64_t token; } TfpVignetteBinding;
/* available: stingray and its four tables Application, World, EntityManager, RenderDataComponent exist. */
typedef struct TfpVignetteApi { bool available; TfpVignetteBinding fn[TFP_VIGNETTE_FN_COUNT]; } TfpVignetteApi;

typedef enum TfpVignetteList {
  TFP_VIGNETTE_LIST_TABLE,          /* a table; *count entries */
  TFP_VIGNETTE_LIST_NIL,            /* nil or false */
  TFP_VIGNETTE_LIST_NOT_A_TABLE     /* any other value */
} TfpVignetteList;
typedef enum TfpVignetteValueKind { TFP_VIGNETTE_VALUE_NIL, TFP_VIGNETTE_VALUE_BOOLEAN, TFP_VIGNETTE_VALUE_TEXT, TFP_VIGNETTE_VALUE_OTHER } TfpVignetteValueKind;
/* A property value. text: text_len bytes, valid until the next host call. */
typedef struct TfpVignetteValue { TfpVignetteValueKind kind; bool boolean; const char *text; size_t text_len; } TfpVignetteValue;
/* backend:player_snapshot(). present false: it returned nil. */
typedef struct TfpVignetteSnapshot { bool present, safe; int camera_kind; } TfpVignetteSnapshot;
typedef struct TfpVignetteControllerState { bool pose_offset; } TfpVignetteControllerState;

/* Everything outside this file. World, manager and entity handles are opaque and compared for identity;
 * 0 stands for nil / false. Instance ids are Lua numbers and stay doubles (their high 32 bits vary); an
 * id that is not a number is reported as NaN, which like it never matches anything.
 * A function with `error` answers false when its Lua counterpart raises; *error is the raised text
 * (NULL: the error value nil), valid until the next host call. The seven engine functions receive
 * `binding`: the token of the Lua function retail would call at that point (the one captured when
 * the context was made, which can differ from the current one when the table changed mid-call). */
typedef struct TfpVignetteHost {
  void *user;
  void (*log)(void *user, const char *text);
  /* deps.clock(). A value that is not a number is reported as NaN. */
  bool (*clock)(void *user, double *now, const char **error);
  /* compat:foreign() and backend.foreign_camera(): true unless retail's returns false; a raise is true
   * as well (retail: `not ok or v~=false`). NULL: no compat object / no such field. */
  bool (*compat_foreign)(void *user);
  bool (*backend_foreign_camera)(void *user);
  /* deps.active_view(): true only when retail's returns exactly true; a raise is false (retail stops
   * either way). NULL: not set; the three calls below decide instead. */
  bool (*active_view)(void *user);
  bool (*backend_player_snapshot)(void *user, TfpVignetteSnapshot *out, const char **error);
  void (*controller_result)(void *user, TfpVignetteControllerState *out);
  bool (*controller_is_enabled)(void *user);
  /* The stingray table as it is now (plain field reads; not a call in retail). */
  void (*stingray_api)(void *user, TfpVignetteApi *out);
  bool (*application_main_world)(void *user, uint64_t binding, uint64_t *world, const char **error);
  /* World.entities(world). TABLE: *count entries, the first min(*count, capacity) written to
   * `entities` in order (a false entry as 0). */
  bool (*world_entities)(void *user, uint64_t binding, uint64_t world, uint64_t *entities, uint32_t capacity, TfpVignetteList *kind,
                         uint32_t *count, const char **error);
  bool (*entity_manager_render_data_component)(void *user, uint64_t binding, uint64_t world, uint64_t *manager, const char **error);
  bool (*entity_manager_alive)(void *user, uint64_t binding, uint64_t entity, bool *alive, const char **error);
  /* RenderDataComponent.instances(manager, entity): several results or one table, either way *count
   * ids, the first min(*count, capacity) written to `ids`. */
  bool (*render_data_component_instances)(void *user, uint64_t binding, uint64_t manager, uint64_t entity, double *ids, uint32_t capacity,
                                          uint32_t *count, const char **error);
  /* RenderDataComponent.get_property(manager, entity, id, {property}). */
  bool (*render_data_component_get_property)(void *user, uint64_t binding, uint64_t manager, uint64_t entity, double id, const char *property,
                                             TfpVignetteValue *out, const char **error);
  /* RenderDataComponent.set_property(manager, entity, id, {property}, value). *refused: it returned false. */
  bool (*render_data_component_set_property)(void *user, uint64_t binding, uint64_t manager, uint64_t entity, double id, const char *property,
                                             bool value, bool *refused, const char **error);
} TfpVignetteHost;

typedef enum TfpVignettePhase {
  TFP_VIGNETTE_IDLE, TFP_VIGNETTE_DISCOVERING, TFP_VIGNETTE_ACTIVE, TFP_VIGNETTE_UNAVAILABLE, TFP_VIGNETTE_CLEANUP_PENDING
} TfpVignettePhase;
/* One vignette instance. owned: this module switched it off and owes the restore; before: the value to
 * write back (retail only ever stores true). */
typedef struct TfpVignetteRow { uint64_t entity; double id; bool owned, before; } TfpVignetteRow;
typedef struct TfpVignette {
  const TfpVignetteHost *host;
  TfpVignettePhase phase;
  uint32_t writes, restores, reassertions;
  bool has_error; char error_message[TFP_VIGNETTE_TEXT];
  bool closing;
  double next_scan, next_poll;
  /* retail's context: the functions, world and manager everything in `rows` belongs to. */
  bool has_context; TfpVignetteApi api; uint64_t world, manager;
  TfpVignetteRow rows[TFP_VIGNETTE_MAX_ROWS]; uint32_t row_count;
  /* The inventory in progress. Retail keeps the table World.entities returned and walks it over
   * several updates; here the handles are copied when the inventory begins (the engine's table is not
   * referenced afterwards). scan_index is 1-based, as in Lua. */
  bool has_scan; uint32_t scan_index, scan_count;
  uint64_t scan_entities[TFP_VIGNETTE_MAX_ENTITIES];
} TfpVignette;
typedef struct TfpVignetteResult {
  TfpVignettePhase phase;
  bool disabled, owned, cleanup_pending;
  uint32_t profiles, writes, restores, reassertions;
  const char *error_message;                     /* NULL: nil */
} TfpVignetteResult;

/* *why (may be passed NULL) receives retail's second result: a text valid until the next call, or NULL. */
void tfp_vignette_init(TfpVignette *v, const TfpVignetteHost *host);
bool tfp_vignette_is_clean(const TfpVignette *v);
bool tfp_vignette_update(TfpVignette *v, const char **why);
bool tfp_vignette_stop(TfpVignette *v, const char **why);
bool tfp_vignette_shutdown(TfpVignette *v, const char **why);
void tfp_vignette_result(const TfpVignette *v, TfpVignetteResult *out);
/* Writes the status text (cut to `size`) and returns `out`. */
const char *tfp_vignette_status(const TfpVignette *v, char *out, size_t size);
const char *tfp_vignette_phase_name(TfpVignettePhase phase);
#endif
