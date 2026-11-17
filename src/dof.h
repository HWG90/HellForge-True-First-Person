/* The renderer Boolean settings lease (depth of field, vignette): translation of retail
 * src/first_person/dof.lua at 0.5.41-smooth.66 (commit 1d0d453). While the first-person view is
 * the actual view, one renderer Boolean ("dof_enabled", "vignette_enabled") is leased to false and
 * handed back when the view ends; the setting is process-global and asynchronous, so every command
 * is kept as a reservation until a readback acknowledges it. Two layers, as in retail:
 * TfpDofSetting (retail's Setting.new: the lease on one Boolean) and TfpDof (retail's Dof.new: the
 * depth-of-field Setting plus a vignette object). Every text is retail's.
 * tests/vectors/dof_vectors.h holds the retail file's own answers. */
#ifndef TFP_DOF_H
#define TFP_DOF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFP_DOF_TEXT 256                      /* every text is capped at 255 bytes; Lua has no cap */
#define TFP_DOF_KEY 64
/* The chunk name LuaJIT prints before an assertion's message ("<chunk>:<line>: "). */
#define TFP_DOF_LUA_CHUNK "src/first_person/dof.lua"
#define TFP_DOF_INTERVAL .5
/* check(): the two SDK thunks (Application.render_setting, .set_render_setting) with their first 8
 * bytes, relative to the executable; dof.lua:62-63. The only build data of this file. */
typedef struct TfpDofBuild { uint32_t get_thunk, set_thunk; uint8_t get_bytes[8], set_bytes[8]; } TfpDofBuild;
/* Retail 26568081: the numbers of the retail file; the vectors are replayed with these. */
extern const TfpDofBuild tfp_dof_build_retail;
/* HellForge's build 25480438: tools/verify_effects_constants.py shows where each number comes from. */
extern const TfpDofBuild tfp_dof_build_25480438;

/* retail's context(): stingray.Application, its render_setting and its set_render_setting, each
 * an opaque identity. Two bindings are the same when all three are equal. */
typedef struct TfpDofBinding { uint64_t app, get, set; } TfpDofBinding;
/* A renderer setting as read: retail accepts Booleans only. */
typedef enum TfpDofValue { TFP_DOF_VALUE_OTHER, TFP_DOF_VALUE_FALSE, TFP_DOF_VALUE_TRUE } TfpDofValue;
typedef enum TfpDofSet { TFP_DOF_SET_DONE, TFP_DOF_SET_REFUSED, TFP_DOF_SET_RAISED } TfpDofSet;
/* What a camera-ownership function returned: `is_false` when its first result is false (the view
 * is free), `value_text` = tostring(first result) ("true", "nil", ...; NULL reads as "nil"), `why`
 * its second result when that is a text (NULL: nil or false). */
typedef struct TfpDofForeign { bool is_false; const char *value_text, *why; } TfpDofForeign;
typedef bool (*TfpDofForeignFn)(void *user, TfpDofForeign *out, const char **error);
/* The camera_compat argument of update. `foreign` NULL: retail's value is not a table with a
 * function `foreign` (the backend's foreign_camera is asked instead). The lease keeps the pointer
 * of the last one given and reads `foreign` at every use. */
typedef struct TfpDofCompat { void *user; TfpDofForeignFn foreign; } TfpDofCompat;
typedef struct TfpDofControllerState { bool pose_offset, running; } TfpDofControllerState;
/* Lua nil / false / true where retail hands a foreign value on, and a raised error. */
typedef enum TfpDofAnswer { TFP_DOF_NIL, TFP_DOF_FALSE, TFP_DOF_TRUE, TFP_DOF_RAISED } TfpDofAnswer;
typedef struct TfpDofVignetteResult { TfpDofAnswer owned, cleanup_pending, command_pending; } TfpDofVignetteResult;

/* Everything outside this file. A function that returns bool with `error` answers false when its
 * Lua counterpart raises; *error is the raised text (NULL: the error value nil). */
typedef struct TfpDofHost {
  void *user;
  /* deps.clock(). Any number may come back; retail refuses NaN and the infinities itself. */
  bool (*clock)(void *user, double *out, const char **error);
  /* The log function. NULL: none. */
  void (*log)(void *user, const char *text);
  /* stingray.Application with its two functions. False: Application is not a table or one of
   * render_setting / set_render_setting is not a function. */
  bool (*application_binding)(void *user, TfpDofBinding *out);
  /* binding.get(key): Application.render_setting(key) through the retained function. */
  bool (*application_render_setting)(void *user, const TfpDofBinding *binding, const char *key, TfpDofValue *out, const char **error);
  /* binding.set(key, 'true' | 'false'). REFUSED: it returned false. RAISED: it raised *error. */
  TfpDofSet (*application_set_render_setting)(void *user, const TfpDofBinding *binding, const char *key, bool value, const char **error);
  /* backend:renderer_dof_value(binding.get, binding.set, key): the cached, non-blocking observer.
   * OTHER: it raised or did not return a Boolean (retail does not tell these apart). NULL: not a function. */
  TfpDofValue (*backend_renderer_dof_value)(void *user, const TfpDofBinding *binding, const char *key);
  /* backend.foreign_camera(). NULL: not a function. */
  TfpDofForeignFn backend_foreign_camera;
  /* backend.profile and backend.profile.pose is true-ish; backend:_alignment_executable(that profile);
   * backend:_read(address, size). Used when `verify` is NULL. */
  bool backend_profile_pose;
  bool (*backend_alignment_executable)(void *user, uint64_t *exe, const char **error);
  bool (*backend_read)(void *user, uint64_t address, uint32_t size, uint8_t *out, const char **error);
  /* deps.verify(binding.app): *is_true when it returned true. NULL: deps.verify is not set. */
  bool (*verify)(void *user, uint64_t app, bool *is_true, const char **error);
  /* deps.active_view(controller, camera), deps.fallback_ready(): *is_true when it returned true.
   * NULL: not a function. */
  bool (*active_view)(void *user, bool *is_true, const char **error);
  bool (*fallback_ready)(void *user, bool *is_true, const char **error);
  /* controller:is_enabled() (true: not nil / false) and controller:result(). is_enabled NULL: the
   * controller is not a table with a function is_enabled. Only asked when active_view is NULL. */
  bool (*controller_is_enabled)(void *user);
  void (*controller_result)(void *user, TfpDofControllerState *out);
  /* The vignette module of the vignette_factory construction (TFP_DOF_VIGNETTE_MODULE): its
   * update(controller, camera, camera_compat), stop(), shutdown() with their two results (*why NULL:
   * nil), is_clean(), result() and status(). */
  TfpDofAnswer (*vignette_update)(void *user, const TfpDofCompat *camera_compat, const char **why);
  TfpDofAnswer (*vignette_stop)(void *user, const char **why);
  TfpDofAnswer (*vignette_shutdown)(void *user, const char **why);
  bool (*vignette_is_clean)(void *user);
  void (*vignette_result)(void *user, TfpDofVignetteResult *out);
  const char *(*vignette_status)(void *user);
  /* The build whose thunks check() expects. NULL: retail's. */
  const TfpDofBuild *build;
} TfpDofHost;

typedef enum TfpDofPhase {
  TFP_DOF_IDLE, TFP_DOF_APPLYING, TFP_DOF_RESTORING, TFP_DOF_ACTIVE, TFP_DOF_CLEANUP_PENDING, TFP_DOF_FOREIGN, TFP_DOF_UNAVAILABLE,
  TFP_DOF_ALREADY_DISABLED
} TfpDofPhase;
/* record.stage. NONE is Lua's nil: a record whose first command raised before it was sent. */
typedef enum TfpDofStage { TFP_DOF_STAGE_NONE, TFP_DOF_STAGE_APPLYING, TFP_DOF_STAGE_RESTORING, TFP_DOF_STAGE_ACTIVE } TfpDofStage;
/* retail's record: the binding the reservation was made through, and the reservation. */
typedef struct TfpDofRecord {
  TfpDofBinding binding;
  bool before, cancel;
  double sent;
  TfpDofStage stage;
  bool has_next_poll; double next_poll;
  bool has_issue; char issue[TFP_DOF_TEXT];
} TfpDofRecord;

typedef struct TfpDofSetting {
  const TfpDofHost *host;
  char key[TFP_DOF_KEY];
  const char *label;
  bool mandatory;
  bool has_record; TfpDofRecord record;
  const TfpDofCompat *compat;
  bool has_verified; TfpDofBinding verified;
  bool closing;
  TfpDofPhase phase;
  bool has_message; char message[TFP_DOF_TEXT];
  bool session, preserved, yielded;
  double next_attempt;
  bool has_last_time; double last_time;
  uint32_t reads, writes, restores, foreign_changes, cached_reads, cache_fallbacks, reassertions;
  char status[TFP_DOF_TEXT + TFP_DOF_KEY];
} TfpDofSetting;
typedef struct TfpDofSettingResult {
  TfpDofPhase phase;
  bool owned, cleanup_pending, command_pending, disabled;
  uint32_t reads, writes, restores, foreign_changes, cached_reads, cache_fallbacks, reassertions;
  const char *error_message;                    /* NULL: nil */
  bool closing;
} TfpDofSettingResult;

/* One renderer Boolean. `setting` NULL: "dof_enabled". *why / *message (may be passed NULL)
 * receive retail's second result: a text valid until the next call on the setting, or NULL. */
void tfp_dof_setting_init(TfpDofSetting *s, const TfpDofHost *host, const char *setting);
bool tfp_dof_setting_update(TfpDofSetting *s, const TfpDofCompat *camera_compat, const char **message);   /* always true */
bool tfp_dof_setting_stop(TfpDofSetting *s, const char **why);
bool tfp_dof_setting_shutdown(TfpDofSetting *s, const char **why);
static inline bool tfp_dof_setting_is_clean(const TfpDofSetting *s) { return !s->has_record; }
bool tfp_dof_setting_is_suppressed(TfpDofSetting *s);
void tfp_dof_setting_result(const TfpDofSetting *s, TfpDofSettingResult *out);
const char *tfp_dof_setting_status(TfpDofSetting *s);
const char *tfp_dof_phase_name(TfpDofPhase phase);
const char *tfp_dof_stage_name(TfpDofStage stage);            /* NULL for NONE */

/* What Dof.new builds beside the depth-of-field Setting. */
typedef enum TfpDofVignette {
  TFP_DOF_VIGNETTE_NONE,       /* deps.suppress_vignette==false: the Setting alone */
  TFP_DOF_VIGNETTE_MODULE,     /* deps.vignette_factory: the host's vignette_* functions (the shipped path) */
  TFP_DOF_VIGNETTE_SETTING     /* neither: a second Setting on "vignette_enabled" */
} TfpDofVignette;
typedef struct TfpDof {
  const TfpDofHost *host;
  TfpDofVignette kind;
  TfpDofSetting dof, vignette;
  char status[2 * (TFP_DOF_TEXT + TFP_DOF_KEY) + TFP_DOF_TEXT];
} TfpDof;
typedef struct TfpDofResult {
  TfpDofSettingResult dof;                     /* owned / cleanup_pending / command_pending here are the Setting's own */
  /* result.owned, .cleanup_pending, .command_pending: the Setting's `or` the vignette's (NIL when the
   * Setting's is false and the vignette module's is nil). */
  TfpDofAnswer owned, cleanup_pending, command_pending;
  TfpDofVignette kind;
  TfpDofSettingResult vignette_setting;        /* result.vignette of TFP_DOF_VIGNETTE_SETTING */
  TfpDofVignetteResult vignette;               /* result.vignette of TFP_DOF_VIGNETTE_MODULE */
} TfpDofResult;

/* `setting`: deps.setting of the depth-of-field Setting (NULL: "dof_enabled"). The first results
 * are retail's `ready and other` / `done and other`; RAISED: the vignette module raised *why. */
void tfp_dof_init(TfpDof *d, const TfpDofHost *host, const char *setting, TfpDofVignette kind);
TfpDofAnswer tfp_dof_update(TfpDof *d, const TfpDofCompat *camera_compat, const char **why);
TfpDofAnswer tfp_dof_stop(TfpDof *d, const char **why);
TfpDofAnswer tfp_dof_shutdown(TfpDof *d, const char **why);
bool tfp_dof_is_clean(TfpDof *d);
bool tfp_dof_is_suppressed(TfpDof *d);
void tfp_dof_result(TfpDof *d, TfpDofResult *out);
const char *tfp_dof_status(TfpDof *d);
#endif
