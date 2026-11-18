/* The lease on the camera helper: when it is validated, armed, configured,
 * watched and let go, and what is reported about it. Translation of retail
 * src/first_person/camera.lua at 0.5.41-smooth.66 (commit 1d0d453): its
 * `Adapter` (the FFI wrapper, lines 12-256) and `Camera.new` (the lease
 * manager, lines 260-462) as they run on the shipped path, in one unit.
 * tests/vectors/camera_adapter_vectors.h holds the retail file's own answers.
 * What is not translated and why: docs/translation/CAMERA-ADAPTER-TRANSLATION.md. */
#ifndef TFP_CAMERA_ADAPTER_H
#define TFP_CAMERA_ADAPTER_H
#include "camera_helper.h"

/* The helper's functions, with the signatures of camera_helper.h (retail: the
 * NADSC_* exports declared in camera.lua:19-74). In the mod: TFP_CAMERA_HELPER_LINKED. */
typedef struct TfpCameraHelperApi {
  uint32_t (*validate)(const TfpCameraConfig *config);                           /* NADSC_Validate */
  uint32_t (*begin)(const TfpCameraConfig *config, uint64_t *generation);        /* NADSC_Begin */
  uint32_t (*end)(uint64_t generation);                                          /* NADSC_End */
  uint32_t (*read_stats)(uint64_t generation, TfpCameraStats *stats);            /* NADSC_ReadStats */
  uint32_t (*error)(void);                                                       /* NADSC_Error */
  uint32_t (*set_presentation)(uint64_t generation);                             /* NADSC_SetPresentation47 */
  uint32_t (*set_handoff)(uint64_t generation, float entry_ms, float rotation_ms); /* NADSC_SetHandoff49 */
  uint32_t (*set_cut_hold)(uint64_t generation, uint32_t mode);                  /* NADSC_SetCutHold50 */
  uint32_t (*set_weapon_key)(uint64_t generation, uint32_t key);                 /* NADSC_SetWeaponKey50 */
} TfpCameraHelperApi;
#define TFP_CAMERA_HELPER_LINKED {tfp_camera_validate, tfp_camera_begin, tfp_camera_end, tfp_camera_read_stats, \
  tfp_camera_error, tfp_camera_set_presentation, tfp_camera_set_handoff, tfp_camera_set_cut_hold, tfp_camera_set_weapon_key}

/* A Lua value that is a text or nil. An empty text is a text (true in Lua). */
#define TFP_CAMERA_ADAPTER_TEXT 256
typedef struct TfpCameraText { bool present; char text[TFP_CAMERA_ADAPTER_TEXT]; } TfpCameraText;

/* What backend:camera_config() returns, reduced to the fields camera.lua reads.
 * `present`: one bit per field in the order of TFP_CAMERA_FIELD_*; a clear bit
 * is a field that is nil in Lua ("<name> unavailable"). */
enum {
  TFP_CAMERA_FIELD_GAME = 1u << 0, TFP_CAMERA_FIELD_EXE = 1u << 1, TFP_CAMERA_FIELD_CONTEXT = 1u << 2,
  TFP_CAMERA_FIELD_CAMERA = 1u << 3, TFP_CAMERA_FIELD_AVATAR_OWNER = 1u << 4, TFP_CAMERA_FIELD_AVATAR_INDEX = 1u << 5,
  TFP_CAMERA_FIELD_AVATAR = 1u << 6, TFP_CAMERA_FIELD_BODY = 1u << 7, TFP_CAMERA_FIELD_CAMERA_UNIT = 1u << 8,
  TFP_CAMERA_FIELD_FORWARD = 1u << 9, TFP_CAMERA_FIELD_UP = 1u << 10, TFP_CAMERA_FIELD_SMOOTHING_MS = 1u << 11,
  TFP_CAMERA_FIELD_BLEND_MS = 1u << 12, TFP_CAMERA_FIELD_MAX_GAP_MS = 1u << 13,
  TFP_CAMERA_FIELDS_ALL = 0x3fffu, TFP_CAMERA_FIELDS_IDENTITY = 0x1ffu /* camera.lua:267 */
};
typedef struct TfpCameraBackendConfig {
  uint32_t present;
  uint64_t game, exe, context, camera, avatar_owner;
  uint32_t avatar_index, avatar, body, camera_unit;
  double forward, up, smoothing_ms, blend_ms;   /* Lua numbers; stored as float in the helper's config */
  uint32_t max_gap_ms;
  uint32_t camera_kind;                         /* 0: none */
  bool presentation_enabled;                    /* config.presentation_enabled == true */
} TfpCameraBackendConfig;
/* backend:snapshot(), reduced to weapon_unit. */
typedef struct TfpCameraBackendSnapshot { bool has_weapon_unit; double weapon_unit; } TfpCameraBackendSnapshot;
/* controller:result(), reduced to mode == 'pose_offset'. */
typedef struct TfpCameraControllerResult { bool pose_offset; } TfpCameraControllerResult;

/* profile.lua `camera`, the values camera.lua reads. */
typedef struct TfpCameraAdapterProfile {
  uint32_t abi;
  bool has_handoff; double handoff_entry_ms, handoff_rotation_ms;
  bool cut_hold, eye_hold, ads_anchor, seated_head;
} TfpCameraAdapterProfile;
#define TFP_CAMERA_ADAPTER_PROFILE_SHIPPED {1, true, 0, 0, true, true, true, true} /* profile.lua:71-84 */

/* Everything the unit takes from outside. */
typedef struct TfpCameraAdapterApi {
  void *user;
  const TfpCameraHelperApi *helper;
  /* May be NULL: the helper is linked in. Set: asked before the helper is first
   * used, again on every later use until it says yes (retail _load: ffi.load of
   * the DLL and the ABI checks). false: `why` holds the raised error's text. */
  bool (*load)(void *user, TfpCameraText *why);
  /* false: no configuration (Lua nil, or the call raised); `why` may stay absent. */
  bool (*camera_config)(void *user, TfpCameraBackendConfig *out, TfpCameraText *why);
  /* May be NULL (backend.snapshot is not a function). false: no snapshot. */
  bool (*snapshot)(void *user, TfpCameraBackendSnapshot *out);
  void (*controller_result)(void *user, TfpCameraControllerResult *out);
  bool (*controller_is_enabled)(void *user);
  /* May be NULL (no clock). Seconds. A clock that fails returns NAN. */
  double (*clock)(void *user);
  void (*log)(void *user, const char *text);   /* may be NULL */
} TfpCameraAdapterApi;

/* The table Adapter:stats returns. Counters are Lua numbers. */
typedef struct TfpCameraAdapterStats {
  double seen, forwarded, replaced, native6_forwarded, resets, blend, head_distance, last_dt;
  bool installed, armed;
  uint32_t error;
} TfpCameraAdapterStats;
typedef enum TfpCameraAdapterPhase { TFP_CAMERA_ADAPTER_IDLE, TFP_CAMERA_ADAPTER_WAITING, TFP_CAMERA_ADAPTER_ACTIVE,
                                     TFP_CAMERA_ADAPTER_CLEANUP_PENDING } TfpCameraAdapterPhase;
typedef enum TfpCameraFailureKind { TFP_CAMERA_FAILURE_NONE, TFP_CAMERA_FAILURE_TEMPORARY_READ,
                                    TFP_CAMERA_FAILURE_UNSAFE } TfpCameraFailureKind;

typedef struct TfpCameraAdapter {
  const TfpCameraAdapterApi *api;
  TfpCameraAdapterProfile profile;
  /* Adapter (camera.lua:12-256) */
  bool loaded;                      /* self.library */
  bool owned;                       /* self.owned is a handle */
  uint64_t generation;              /* handle.generation[0] of that handle */
  TfpCameraStats stats_buffer;      /* self.stats_buffer: kept between reads */
  bool has_stats_cache, has_stats_at;
  double stats_cache[8], stats_at, stats_due;
  bool has_last_failure_code; uint32_t last_failure_code;
  /* Camera.new (camera.lua:260-462) */
  bool handle;                      /* self.handle; the same handle as `owned` whenever both are set */
  bool has_bound; TfpCameraBackendConfig bound;
  bool closing;
  TfpCameraText fault;
  bool has_last; TfpCameraAdapterStats last;
  TfpCameraAdapterPhase phase;
  bool has_failure_native_error; uint32_t failure_native_error;
  TfpCameraFailureKind failure_kind;
  bool has_handoff; double handoff_entry_ms, handoff_rotation_ms;   /* self.handoff */
  bool has_cut_hold; uint32_t cut_hold;                             /* self.cut_hold */
  TfpCameraText handoff_issue, cut_hold_issue;
  bool has_weapon_key; double weapon_key;
} TfpCameraAdapter;

/* camera:result(). `has_stats` false: no statistics were ever read (the stats fields are nil). */
typedef struct TfpCameraAdapterResult {
  bool has_stats; TfpCameraAdapterStats stats;
  TfpCameraAdapterPhase phase;
  bool cleanup_pending;
  TfpCameraText error_message;
  bool has_failure_native_error; uint32_t failure_native_error;
  TfpCameraFailureKind failure_kind;
} TfpCameraAdapterResult;

/* profile NULL: the shipped values. */
void tfp_camera_adapter_init(TfpCameraAdapter *a, const TfpCameraAdapterApi *api, const TfpCameraAdapterProfile *profile);
/* Each returns Lua's first result (true or not) and puts the second in `why`
 * (absent: nil). `why` may be NULL. */
bool tfp_camera_adapter_update(TfpCameraAdapter *a, TfpCameraText *why);     /* camera:update(controller) */
bool tfp_camera_adapter_prepare(TfpCameraAdapter *a, TfpCameraText *why);    /* camera:prepare(), also camera:prearm() */
bool tfp_camera_adapter_stop(TfpCameraAdapter *a, TfpCameraText *why);
bool tfp_camera_adapter_shutdown(TfpCameraAdapter *a, TfpCameraText *why);
/* 1: true; 0: false; -1: nil (the validation raised: helper not loadable, or a field missing). */
int tfp_camera_adapter_preflight(TfpCameraAdapter *a, TfpCameraText *why);
bool tfp_camera_adapter_set_handoff(TfpCameraAdapter *a, double entry_ms, double rotation_ms, TfpCameraText *why);
bool tfp_camera_adapter_set_cut_hold(TfpCameraAdapter *a, bool exit, bool eye, bool anchor, bool seated, TfpCameraText *why);
static inline bool tfp_camera_adapter_is_clean(const TfpCameraAdapter *a) { return !a->handle; }
void tfp_camera_adapter_result(const TfpCameraAdapter *a, TfpCameraAdapterResult *out);
/* camera:status(); returns the length the whole text needs. */
size_t tfp_camera_adapter_status(const TfpCameraAdapter *a, char *out, size_t size);
/* tostring(number) as the reference LuaJIT prints it ("%.14g"; nan, inf, -inf). */
size_t tfp_camera_adapter_number(double value, char *out, size_t size);
#endif
