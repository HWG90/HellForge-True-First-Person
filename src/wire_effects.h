/* The effect modules of the feel build behind the runtime host: depth of field with the vignette
 * behind it (dof.c, vignette.c), aiming markers (aim_markers.c), the focused-movement byte
 * (first_person_movement.c) and the native aim option (native_ads_option.c).
 *
 *   tfp_runtime.c  ->  TfpRtHost.dof_* / movement_* / native_aim_* / aim_markers_*  ->  this file
 *                      ->  the five units  ->  their hosts, bound here to the backend (w->ops), the
 *                          controller, the runtime's own entries and a TfpFxPlatform
 *
 * Nothing here is Windows or the engine: the engine's script functions, VirtualQuery,
 * WriteProcessMemory and the guarded call of the game's widget setter come in as a TfpFxPlatform.
 * src/wire_win_effects.c supplies the real one, tests/wire_effects_test.c a scripted one.
 *
 * Camera sharing with Photo Mode (camera_compat) is not bound: it stays the off answer of
 * wire_off.c (no other camera mod is detected on HellForge). */
#ifndef TFP_WIRE_EFFECTS_H
#define TFP_WIRE_EFFECTS_H
#include "wire.h"
#include "dof.h"
#include "vignette.h"
#include "aim_markers.h"
#include "first_person_movement.h"
#include "native_ads_option.h"

typedef enum TfpFxModule { TFP_FX_DOF, TFP_FX_VIGNETTE, TFP_FX_MARKERS, TFP_FX_MOVEMENT, TFP_FX_AIM_OPTION, TFP_FX_MODULES } TfpFxModule;
/* WAITING: the engine's script state is not known yet (dof, vignette, movement); the module answers
 * "off" until its functions were checked. REFUSED: off for the session, with the logged reason. */
typedef enum TfpFxState { TFP_FX_WAITING, TFP_FX_READY, TFP_FX_REFUSED } TfpFxState;

/* The engine script functions the modules call, each with the address the census of build 25480438
 * has for it (docs/history/research-0.5.34/live-sdk-census-g1-9472b065.tsv), relative to the executable. A
 * module is allowed on only when every function it uses is registered at that address. */
typedef enum TfpFxFunction {
  TFP_FX_FN_RENDER_SETTING, TFP_FX_FN_SET_RENDER_SETTING,
  TFP_FX_FN_MAIN_WORLD, TFP_FX_FN_ENTITIES, TFP_FX_FN_RENDER_DATA_COMPONENT, TFP_FX_FN_ALIVE, TFP_FX_FN_INSTANCES, TFP_FX_FN_GET_PROPERTY,
  TFP_FX_FN_SET_PROPERTY,
  TFP_FX_FN_BUTTON_INDEX, TFP_FX_FN_BUTTON_ID, TFP_FX_FN_BUTTON, TFP_FX_FN_MOUSE_FOCUS, TFP_FX_FN_SHOW_CURSOR,
  TFP_FX_FUNCTIONS
} TfpFxFunction;
typedef struct TfpFxFunctionRow { const char *path; uint32_t rva; TfpFxModule module; } TfpFxFunctionRow;
extern const TfpFxFunctionRow tfp_fx_functions[TFP_FX_FUNCTIONS];

/* A constant of a module that was not verified on build 25480438 keeps the module off. All are
 * verified today (tools/verify_effects_constants.py); the row stays so that a new one cannot slip in. */
typedef struct TfpFxConstants { TfpFxModule module; const char *unverified; /* NULL: every constant verified */ } TfpFxConstants;
extern const TfpFxConstants tfp_fx_constants[TFP_FX_MODULES];

typedef enum TfpFxSet { TFP_FX_SET_DONE, TFP_FX_SET_REFUSED, TFP_FX_SET_RAISED } TfpFxSet;
/* Everything outside: the engine, Windows, the game's own function. A function with `error` answers
 * false when the engine call raised or failed; *error is the text (NULL: none), valid until the next
 * call. Engine calls are made on the game-thread tick only (the runtime's on_update runs there). */
typedef struct TfpFxPlatform {
  void *user;
  /* The engine's script state is known and `stingray` exists; *why says what is missing. */
  bool (*engine_ready)(void *user, const char **why);
  /* Changes when the engine replaces its script state: every handle and token is then stale. */
  uint64_t (*engine_generation)(void *user);
  /* The address of the C function behind stingray.<path>; 0: not a function. */
  uint64_t (*engine_function)(void *user, const char *path);
  /* stingray.<name> is a table. */
  bool (*engine_table)(void *user, const char *name);
  /* The base of the executable (the census addresses are relative to it); 0: unknown. */
  uint64_t (*exe_base)(void *user);

  /* Application.render_setting(key): 1 true, 0 false, -1 any other value. */
  bool (*render_setting)(void *user, const char *key, int *value, const char **error);
  /* Application.set_render_setting(key, 'true' | 'false'). REFUSED: it returned false. */
  TfpFxSet (*set_render_setting)(void *user, const char *key, bool value, const char **error);

  /* The vignette's seven. Worlds, managers and entities are handles that are equal exactly when the
   * Lua values are, and never 0 for a value that is neither nil nor false. */
  bool (*main_world)(void *user, uint64_t *world, const char **error);
  bool (*world_entities)(void *user, uint64_t world, uint64_t *entities, uint32_t capacity, TfpVignetteList *kind, uint32_t *count,
                         const char **error);
  bool (*render_data_component)(void *user, uint64_t world, uint64_t *manager, const char **error);
  bool (*entity_alive)(void *user, uint64_t entity, bool *alive, const char **error);
  bool (*instances)(void *user, uint64_t manager, uint64_t entity, double *ids, uint32_t capacity, uint32_t *count, const char **error);
  bool (*get_property)(void *user, uint64_t manager, uint64_t entity, double id, const char *property, TfpVignetteValue *out,
                       const char **error);
  bool (*set_property)(void *user, uint64_t manager, uint64_t entity, double id, const char *property, bool value, bool *refused,
                       const char **error);

  /* Keyboard.button_index(name) / .button_id(name) (index false: button_id), Keyboard.button(key):
   * *has false for nil. Window.mouse_focus() (*focus: exactly true), Window.show_cursor() (*shown
   * false: exactly false). */
  bool (*keyboard_find)(void *user, bool index, const char *name, bool *has, double *key, const char **error);
  bool (*keyboard_button)(void *user, double key, bool *has, double *value, const char **error);
  bool (*window_mouse_focus)(void *user, bool *focus, const char **error);
  bool (*window_show_cursor)(void *user, bool *shown, const char **error);

  /* VirtualQuery(address, region, size): the bytes filled. `region` is MEMORY_BASIC_INFORMATION (48
   * bytes: TfpAimMarkersRegion and TfpMovementRegion have its layout). */
  size_t (*virtual_query)(void *user, uint64_t address, void *region, size_t size);
  /* WriteProcessMemory(current process, address, bytes, size, written): its BOOL. */
  int (*write_process_memory)(void *user, uint64_t address, const uint8_t *bytes, size_t size, size_t *written);
  /* The game's own void (*)(uintptr_t widget, uint8_t shown) at `function`, guarded: false when it
   * faulted (it may have written before). */
  bool (*set_widget_shown)(void *user, uint64_t function, uint64_t widget, uint8_t shown, const char **error);
} TfpFxPlatform;

/* The saved per-weapon aiming-marker table as a value, when no settings store is bound:
 * `markers_save` then hands mod.set a REF to one of these (the whole new table: the keys whose value
 * is true), and `markers_settings` may answer the same REF back or a REF to runtime.lua's `{}`. */
typedef struct TfpFxMarkersTable { size_t count; char keys[TFP_AIM_MARKERS_SAVED + 1][TFP_AIM_MARKERS_KEY]; } TfpFxMarkersTable;
typedef struct TfpFxOptions {
  /* The settings store's two conversions (src/wire_settings.h). markers_decode: the value of
   * (mod.config or {}).aim_markers as the keys whose value is true; false when it is not a table.
   * markers_value: the new table for mod.set('aim_markers', ...) (tfp_wire_settings_markers has this
   * signature); nil: it could not be made. NULL: the REFs above. */
  bool (*markers_decode)(TfpWire *w, TfpRtValue value, TfpAimMarkersSaved *out);
  TfpRtValue (*markers_value)(TfpWire *w, const char *const *keys, size_t count);
  /* Which builds' constants the units get. NULL: build 25480438's (the mod). Tests pass retail's. */
  const TfpDofBuild *dof;
  const TfpAimMarkersBuild *markers;
  const TfpMovementBuild *movement;
  const TfpNativeAimBuild *aim_option;
} TfpFxOptions;

typedef struct TfpFx {
  TfpWire *w;
  TfpFxPlatform platform;
  TfpFxOptions options;
  TfpRtHost off;                                  /* the answers this binder replaced (wire_off.c's) */
  TfpFxState state[TFP_FX_MODULES];
  char reason[TFP_FX_MODULES][200];               /* why a module is REFUSED */
  bool made[TFP_FX_MODULES];                      /* the runtime constructed the module (its `new`) */
  bool announced, checked;
  uint64_t generation;                            /* the engine generation the functions were checked in */
  /* the units and their hosts */
  TfpDofHost dof_host;
  TfpDof dof;
  TfpDofCompat compat;
  TfpVignetteHost vignette_host;
  TfpVignette vignette;
  TfpAimMarkersHost markers_host;
  TfpAimMarkers markers;
  TfpMovementHost movement_host;
  TfpMovement movement;
  TfpNativeAimHost aim_host;
  TfpNativeAim aim;
  /* storage behind the host answers */
  TfpOpsSnapshot move_snap, marker_snap[2];
  TfpOpsAction move_action;
  TfpAimMarkersMenuTarget menu_target;
  TfpFxMarkersTable saved;                        /* the table last handed to markers_save */
  char text[4][320];               /* error texts handed to the units (a ring) */
  unsigned text_next;
  char status[2 * (TFP_DOF_TEXT + TFP_DOF_KEY) + TFP_DOF_TEXT + TFP_VIGNETTE_TEXT];
  char vignette_status[TFP_VIGNETTE_TEXT + 128];
  /* the session log: what was last reported */
  struct {
    uint32_t dof_writes, dof_restores, vignette_rows, vignette_writes, vignette_restores, move_writes, move_restores, aim_writes, aim_restores;
    bool dof_record, markers_hidden, move_verified;
  } seen;
} TfpFx;

/* After tfp_wire_init, before the runtime's on_enable. Keeps its state in w->ext[TFP_WIRE_EXT_EFFECTS]
 * (`fx`, the caller's storage: it must outlive the binder; it is about 300 KiB, not for the stack)
 * and overrides the dof_*, movement_*, native_aim_* and aim_markers_* pointers of w->host. Logs
 * nothing: the first tfp_wire_effects_frame does (the session log may not be open before). */
void tfp_wire_effects_bind(TfpWire *w, TfpFx *fx, const TfpFxPlatform *platform, const TfpFxOptions *options);
/* Once per tick, after the runtime's on_update: the load lines (`Ready: <module> …` /
 * `Refused: <module>: …`, once each; the engine functions are checked here, as soon as the engine's
 * script state is known and again when it is replaced) and the event lines (a module took or gave
 * back something in the game). */
void tfp_wire_effects_frame(TfpWire *w);
/* Once a second while the mode is on: "dof=owned vignette=owned:2 markers=hidden move=idle aimopt=off".
 * Returns the characters written (0: not bound). */
size_t tfp_wire_effects_second(TfpWire *w, char *out, size_t size);
TfpFx *tfp_wire_effects_of(TfpWire *w);

/* src/memory.lua writer:copy(address, one byte) on its shipped path (native_ads_option.lua:54), over
 * the platform's VirtualQuery / WriteProcessMemory and the backend's read. Exported for the test. */
TfpNativeAimCopy tfp_fx_memory_copy(TfpFx *fx, uint64_t address, uint8_t value, const char **why);
#endif
